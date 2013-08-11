// Copyright (c) 2010 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Restructured in 2009 by: Jim Blandy <jimb@mozilla.com> <jimb@red-bean.com>

// dump_symbols.cc: implement google_breakpad::WriteSymbolFile:
// Find all the debugging info in a file and dump it as a Breakpad symbol file.

#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/dwarf/bytereader-inl.h"
#include "common/dwarf/dwarf2diehandler.h"
#include "common/linux/dump_stabs.h"
#include "common/linux/dump_symbols.h"
#include "common/linux/dwarf_cfi_to_module.h"
#include "common/linux/dwarf_cu_to_module.h"
#include "common/linux/dwarf_line_to_module.h"
#include "common/linux/file_id.h"
#include "common/linux/module.h"
#include "common/linux/stabs_reader.h"

// This namespace contains helper functions.
namespace {

using google_breakpad::DumpStabsHandler;
using google_breakpad::DwarfCFIToModule;
using google_breakpad::DwarfCUToModule;
using google_breakpad::DwarfLineToModule;
using google_breakpad::Module;

// Fix offset into virtual address by adding the mapped base into offsets.
// Make life easier when want to find something by offset.
static void FixAddress(void *obj_base) {
  ElfW(Addr) base = reinterpret_cast<ElfW(Addr)>(obj_base);
  ElfW(Ehdr) *elf_header = static_cast<ElfW(Ehdr) *>(obj_base);
  elf_header->e_phoff += base;
  elf_header->e_shoff += base;
  ElfW(Shdr) *sections = reinterpret_cast<ElfW(Shdr) *>(elf_header->e_shoff);
  for (int i = 0; i < elf_header->e_shnum; ++i)
    sections[i].sh_offset += base;
}

// Find the prefered loading address of the binary.
static ElfW(Addr) GetLoadingAddress(const ElfW(Phdr) *program_headers,
                                    int nheader) {
  for (int i = 0; i < nheader; ++i) {
    const ElfW(Phdr) &header = program_headers[i];
    // For executable, it is the PT_LOAD segment with offset to zero.
    if (header.p_type == PT_LOAD &&
        header.p_offset == 0)
      return header.p_vaddr;
  }
  // For other types of ELF, return 0.
  return 0;
}

static bool IsValidElf(const ElfW(Ehdr) *elf_header) {
  return memcmp(elf_header, ELFMAG, SELFMAG) == 0;
}

static const ElfW(Shdr) *FindSectionByName(const char *name,
                                           const ElfW(Shdr) *sections,
                                           const ElfW(Shdr) *section_names,
                                           int nsection) {
  assert(name != NULL);
  assert(sections != NULL);
  assert(nsection > 0);

  int name_len = strlen(name);
  if (name_len == 0)
    return NULL;

  // Find the end of the section name section, to make sure that
  // comparisons don't run off the end of the section.
  const char *names_end = 
    reinterpret_cast<char*>(section_names->sh_offset + section_names->sh_size);

  for (int i = 0; i < nsection; ++i) {
    const char *section_name =
      reinterpret_cast<char*>(section_names->sh_offset + sections[i].sh_name);
    if (names_end - section_name >= name_len + 1 &&
        strcmp(name, section_name) == 0)
      return sections + i;
  }
  return NULL;
}

static bool LoadStabs(const ElfW(Shdr) *stab_section,
                      const ElfW(Shdr) *stabstr_section,
                      Module *module) {
  // A callback object to handle data from the STABS reader.
  DumpStabsHandler handler(module);
  // Find the addresses of the STABS data, and create a STABS reader object.
  uint8_t *stabs = reinterpret_cast<uint8_t *>(stab_section->sh_offset);
  uint8_t *stabstr = reinterpret_cast<uint8_t *>(stabstr_section->sh_offset);
  google_breakpad::StabsReader reader(stabs, stab_section->sh_size,
                                      stabstr, stabstr_section->sh_size,
                                      &handler);
  // Read the STABS data, and do post-processing.
  if (!reader.Process())
    return false;
  handler.Finalize();
  return true;
}

// A line-to-module loader that accepts line number info parsed by
// dwarf2reader::LineInfo and populates a Module and a line vector
// with the results.
class DumperLineToModule: public DwarfCUToModule::LineToModuleFunctor {
 public:
  // Create a line-to-module converter using BYTE_READER.
  DumperLineToModule(dwarf2reader::ByteReader *byte_reader)
      : byte_reader_(byte_reader) { }
  void operator()(const char *program, uint64 length,
                  Module *module, vector<Module::Line> *lines) {
    DwarfLineToModule handler(module, lines);
    dwarf2reader::LineInfo parser(program, length, byte_reader_, &handler);
    parser.Start();
  }
 private:
  dwarf2reader::ByteReader *byte_reader_;
};

static bool LoadDwarf(const string &dwarf_filename,
                      const ElfW(Ehdr) *elf_header,
                      Module *module) {
  // Figure out what endianness this file is.
  dwarf2reader::Endianness endianness;
  if (elf_header->e_ident[EI_DATA] == ELFDATA2LSB)
    endianness = dwarf2reader::ENDIANNESS_LITTLE;
  else if (elf_header->e_ident[EI_DATA] == ELFDATA2MSB)
    endianness = dwarf2reader::ENDIANNESS_BIG;
  else {
    fprintf(stderr, "bad data encoding in ELF header: %d\n",
            elf_header->e_ident[EI_DATA]);
    return false;
  }
  dwarf2reader::ByteReader byte_reader(endianness);

  // Construct a context for this file.
  DwarfCUToModule::FileContext file_context(dwarf_filename, module);

  // Build a map of the ELF file's sections.
  const ElfW(Shdr) *sections
      = reinterpret_cast<ElfW(Shdr) *>(elf_header->e_shoff);
  int num_sections = elf_header->e_shnum;
  const ElfW(Shdr) *section_names = sections + elf_header->e_shstrndx;
  for (int i = 0; i < num_sections; i++) {
    const ElfW(Shdr) *section = &sections[i];
    string name = reinterpret_cast<const char *>(section_names->sh_offset
                                                 + section->sh_name);
    const char *contents = reinterpret_cast<const char *>(section->sh_offset);
    uint64 length = section->sh_size;
    file_context.section_map[name] = std::make_pair(contents, length);
  }

  // Parse all the compilation units in the .debug_info section.
  DumperLineToModule line_to_module(&byte_reader);
  std::pair<const char *, uint64> debug_info_section
      = file_context.section_map[".debug_info"];
  // We should never have been called if the file doesn't have a
  // .debug_info section.
  assert(debug_info_section.first);
  uint64 debug_info_length = debug_info_section.second;
  for (uint64 offset = 0; offset < debug_info_length;) {
    // Make a handler for the root DIE that populates MODULE with the
    // data we find.
    DwarfCUToModule::WarningReporter reporter(dwarf_filename, offset);
    DwarfCUToModule root_handler(&file_context, &line_to_module, &reporter);
    // Make a Dwarf2Handler that drives our DIEHandler.
    dwarf2reader::DIEDispatcher die_dispatcher(&root_handler);
    // Make a DWARF parser for the compilation unit at OFFSET.
    dwarf2reader::CompilationUnit reader(file_context.section_map,
                                         offset,
                                         &byte_reader,
                                         &die_dispatcher);
    // Process the entire compilation unit; get the offset of the next.
    offset += reader.Start();
  }
  return true;
}

// Fill REGISTER_NAMES with the register names appropriate to the
// machine architecture given in HEADER, indexed by the register
// numbers used in DWARF call frame information. Return true on
// success, or false if we don't recognize HEADER's machine
// architecture.
static bool DwarfCFIRegisterNames(const ElfW(Ehdr) *elf_header,
                                  vector<string> *register_names)
{
  static const char *const i386_names[] = {
    "$eax", "$ecx", "$edx", "$ebx", "$esp", "$ebp", "$esi", "$edi",
    "$eip", "$eflags", "$unused1",
    "$st0", "$st1", "$st2", "$st3", "$st4", "$st5", "$st6", "$st7",
    "$unused2", "$unused3",
    "$xmm0", "$xmm1", "$xmm2", "$xmm3", "$xmm4", "$xmm5", "$xmm6", "$xmm7",
    "$mm0", "$mm1", "$mm2", "$mm3", "$mm4", "$mm5", "$mm6", "$mm7",
    "$fcw", "$fsw", "$mxcsr",
    "$es", "$cs", "$ss", "$ds", "$fs", "$gs", "$unused4", "$unused5",
    "$tr", "$ldtr",
    NULL
  };

  static const char *const x86_64_names[] = {
    "$rax", "$rdx", "$rcx", "$rbx", "$rsi", "$rdi", "$rbp", "$rsp",
    "$r8",  "$r9",  "$r10", "$r11", "$r12", "$r13", "$r14", "$r15",
    "$rip",
    "$xmm0","$xmm1","$xmm2", "$xmm3", "$xmm4", "$xmm5", "$xmm6", "$xmm7",
    "$xmm8","$xmm9","$xmm10","$xmm11","$xmm12","$xmm13","$xmm14","$xmm15",
    "$st0", "$st1", "$st2", "$st3", "$st4", "$st5", "$st6", "$st7",
    "$mm0", "$mm1", "$mm2", "$mm3", "$mm4", "$mm5", "$mm6", "$mm7",
    "$rflags",
    "$es", "$cs", "$ss", "$ds", "$fs", "$gs", "$unused1", "$unused2",
    "$fs.base", "$gs.base", "$unused3", "$unused4",
    "$tr", "$ldtr",
    "$mxcsr", "$fcw", "$fsw",
    NULL
  };

  static const char *const arm_names[] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8",  "r9",  "r10", "r11", "r12", "sp",  "lr",  "pc",
    "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
    "fps", "cpsr",
    NULL
  };

  const char * const *name_table;
  switch (elf_header->e_machine) {
    case EM_386:    name_table = i386_names;   break;
    case EM_ARM:    name_table = arm_names;    break;
    case EM_X86_64: name_table = x86_64_names; break;
    default:
      return false;
  }

  register_names->clear();
  for (int i = 0; name_table[i]; i++)
    register_names->push_back(name_table[i]);
  return true;
}

static bool LoadDwarfCFI(const string &dwarf_filename,
                         const ElfW(Ehdr) *elf_header,
                         const char *section_name,
                         const ElfW(Shdr) *section,
                         bool eh_frame,
                         const ElfW(Shdr) *got_section,
                         const ElfW(Shdr) *text_section,
                         Module *module) {
  // Find the appropriate set of register names for this file's
  // architecture.
  vector<string> register_names;
  if (!DwarfCFIRegisterNames(elf_header, &register_names)) {
    fprintf(stderr, "%s: unrecognized ELF machine architecture '%d';"
            " cannot convert DWARF call frame information\n",
            dwarf_filename.c_str(), elf_header->e_machine);
    return false;
  }

  // Figure out what endianness this file is.
  dwarf2reader::Endianness endianness;
  if (elf_header->e_ident[EI_DATA] == ELFDATA2LSB)
    endianness = dwarf2reader::ENDIANNESS_LITTLE;
  else if (elf_header->e_ident[EI_DATA] == ELFDATA2MSB)
    endianness = dwarf2reader::ENDIANNESS_BIG;
  else {
    fprintf(stderr, "%s: bad data encoding in ELF header: %d\n",
            dwarf_filename.c_str(), elf_header->e_ident[EI_DATA]);
    return false;
  }

  // Find the call frame information and its size.
  const char *cfi = reinterpret_cast<const char *>(section->sh_offset);
  size_t cfi_size = section->sh_size;

  // Plug together the parser, handler, and their entourages.
  DwarfCFIToModule::Reporter module_reporter(dwarf_filename, section_name);
  DwarfCFIToModule handler(module, register_names, &module_reporter);
  dwarf2reader::ByteReader byte_reader(endianness);
  // Since we're using the ElfW macro, we're not actually capable of
  // processing both ELF32 and ELF64 files with the same program; that
  // would take a bit more work. But this will work out well enough.
  if (elf_header->e_ident[EI_CLASS] == ELFCLASS32)
    byte_reader.SetAddressSize(4);
  else if (elf_header->e_ident[EI_CLASS] == ELFCLASS64)
    byte_reader.SetAddressSize(8);
  else {
    fprintf(stderr, "%s: bad file class in ELF header: %d\n",
            dwarf_filename.c_str(), elf_header->e_ident[EI_CLASS]);
    return false;
  }
  // Provide the base addresses for .eh_frame encoded pointers, if
  // possible.
  byte_reader.SetCFIDataBase(section->sh_addr, cfi);
  if (got_section)
    byte_reader.SetDataBase(got_section->sh_addr);
  if (text_section)
    byte_reader.SetTextBase(got_section->sh_addr);
    
  dwarf2reader::CallFrameInfo::Reporter dwarf_reporter(dwarf_filename,
                                                       section_name);
  dwarf2reader::CallFrameInfo parser(cfi, cfi_size,
                                     &byte_reader, &handler, &dwarf_reporter,
                                     eh_frame);
  parser.Start();
  return true;
}

static bool LoadSymbols(const std::string &obj_file, ElfW(Ehdr) *elf_header,
                        Module *module) {
  // Translate all offsets in section headers into address.
  FixAddress(elf_header);
  ElfW(Addr) loading_addr = GetLoadingAddress(
      reinterpret_cast<ElfW(Phdr) *>(elf_header->e_phoff),
      elf_header->e_phnum);
  module->SetLoadAddress(loading_addr);

  const ElfW(Shdr) *sections =
      reinterpret_cast<ElfW(Shdr) *>(elf_header->e_shoff);
  const ElfW(Shdr) *section_names = sections + elf_header->e_shstrndx;
  bool found_debug_info_section = false;

  // Look for STABS debugging information, and load it if present.
  const ElfW(Shdr) *stab_section
      = FindSectionByName(".stab", sections, section_names,
                          elf_header->e_shnum);
  if (stab_section) {
    const ElfW(Shdr) *stabstr_section = stab_section->sh_link + sections;
    if (stabstr_section) {
      found_debug_info_section = true;
      if (!LoadStabs(stab_section, stabstr_section, module))
        fprintf(stderr, "\".stab\" section found, but failed to load STABS"
                " debugging information\n");
    }
  }

  // Look for DWARF debugging information, and load it if present.
  const ElfW(Shdr) *dwarf_section
      = FindSectionByName(".debug_info", sections, section_names,
                          elf_header->e_shnum);
  if (dwarf_section) {
    found_debug_info_section = true;
    if (!LoadDwarf(obj_file, elf_header, module))
      fprintf(stderr, "\".debug_info\" section found, but failed to load "
              "DWARF debugging information\n");
  }

  // Dwarf Call Frame Information (CFI) is actually independent from
  // the other DWARF debugging information, and can be used alone.
  const ElfW(Shdr) *dwarf_cfi_section =
      FindSectionByName(".debug_frame", sections, section_names,
                          elf_header->e_shnum);
  if (dwarf_cfi_section) {
    // Ignore the return value of this function; even without call frame
    // information, the other debugging information could be perfectly
    // useful.
    LoadDwarfCFI(obj_file, elf_header, ".debug_frame",
                 dwarf_cfi_section, false, 0, 0, module);
  }

  // Linux C++ exception handling information can also provide
  // unwinding data.
  const ElfW(Shdr) *eh_frame_section =
      FindSectionByName(".eh_frame", sections, section_names,
                        elf_header->e_shnum);
  if (eh_frame_section) {
    // Pointers in .eh_frame data may be relative to the base addresses of
    // certain sections. Provide those sections if present.
    const ElfW(Shdr) *got_section =
      FindSectionByName(".got", sections, section_names, elf_header->e_shnum);
    const ElfW(Shdr) *text_section =
      FindSectionByName(".text", sections, section_names,
                        elf_header->e_shnum);
    // As above, ignore the return value of this function.
    LoadDwarfCFI(obj_file, elf_header, ".eh_frame",
                 eh_frame_section, true, got_section, text_section, module);
  }

  if (!found_debug_info_section) {
    fprintf(stderr, "file contains no debugging information"
            " (no \".stab\" or \".debug_info\" sections)\n");
    return false;
  }
  return true;
}

//
// FDWrapper
//
// Wrapper class to make sure opened file is closed.
//
class FDWrapper {
 public:
  explicit FDWrapper(int fd) :
    fd_(fd) {
    }
  ~FDWrapper() {
    if (fd_ != -1)
      close(fd_);
  }
  int get() {
    return fd_;
  }
  int release() {
    int fd = fd_;
    fd_ = -1;
    return fd;
  }
 private:
  int fd_;
};

//
// MmapWrapper
//
// Wrapper class to make sure mapped regions are unmapped.
//
class MmapWrapper {
  public:
   MmapWrapper(void *mapped_address, size_t mapped_size) :
     base_(mapped_address), size_(mapped_size) {
   }
   ~MmapWrapper() {
     if (base_ != NULL) {
       assert(size_ > 0);
       munmap(base_, size_);
     }
   }
   void release() {
     base_ = NULL;
     size_ = 0;
   }

  private:
   void *base_;
   size_t size_;
};

// Return the breakpad symbol file identifier for the architecture of
// ELF_HEADER.
const char *ElfArchitecture(const ElfW(Ehdr) *elf_header) {
  ElfW(Half) arch = elf_header->e_machine;
  switch (arch) {
    case EM_386:        return "x86";
    case EM_ARM:        return "arm";
    case EM_MIPS:       return "mips";
    case EM_PPC64:      return "ppc64";
    case EM_PPC:        return "ppc";
    case EM_S390:       return "s390";
    case EM_SPARC:      return "sparc";
    case EM_SPARCV9:    return "sparcv9";
    case EM_X86_64:     return "x86_64";
    default: return NULL;
  }
}

// Format the Elf file identifier in IDENTIFIER as a UUID with the
// dashes removed.
std::string FormatIdentifier(unsigned char identifier[16]) {
  char identifier_str[40];
  google_breakpad::FileID::ConvertIdentifierToString(
      identifier,
      identifier_str,
      sizeof(identifier_str));
  std::string id_no_dash;
  for (int i = 0; identifier_str[i] != '\0'; ++i)
    if (identifier_str[i] != '-')
      id_no_dash += identifier_str[i];
  // Add an extra "0" by the end.  PDB files on Windows have an 'age'
  // number appended to the end of the file identifier; this isn't
  // really used or necessary on other platforms, but let's preserve
  // the pattern.
  id_no_dash += '0';
  return id_no_dash;
}

// Return the non-directory portion of FILENAME: the portion after the
// last slash, or the whole filename if there are no slashes.
std::string BaseFileName(const std::string &filename) {
  // Lots of copies!  basename's behavior is less than ideal.
  char *c_filename = strdup(filename.c_str());
  std::string base = basename(c_filename);
  free(c_filename);
  return base;
}

}  // namespace

namespace google_breakpad {

bool WriteSymbolFile(const std::string &obj_file, FILE *sym_file) {
  int obj_fd = open(obj_file.c_str(), O_RDONLY);
  if (obj_fd < 0) {
    fprintf(stderr, "Failed to open ELF file '%s': %s\n",
            obj_file.c_str(), strerror(errno));
    return false;
  }
  FDWrapper obj_fd_wrapper(obj_fd);
  struct stat st;
  if (fstat(obj_fd, &st) != 0 && st.st_size <= 0) {
    fprintf(stderr, "Unable to fstat ELF file '%s': %s\n",
            obj_file.c_str(), strerror(errno));
    return false;
  }
  void *obj_base = mmap(NULL, st.st_size,
                        PROT_READ | PROT_WRITE, MAP_PRIVATE, obj_fd, 0);
  if (obj_base == MAP_FAILED) {
    fprintf(stderr, "Failed to mmap ELF file '%s': %s\n",
            obj_file.c_str(), strerror(errno));
    return false;
  }
  MmapWrapper map_wrapper(obj_base, st.st_size);
  ElfW(Ehdr) *elf_header = reinterpret_cast<ElfW(Ehdr) *>(obj_base);
  if (!IsValidElf(elf_header)) {
    fprintf(stderr, "Not a valid ELF file: %s\n", obj_file.c_str());
    return false;
  }

  unsigned char identifier[16];
  google_breakpad::FileID file_id(obj_file.c_str());
  if (!file_id.ElfFileIdentifier(identifier)) {
    fprintf(stderr, "Unable to generate file identifier\n");
    return false;
  }

  const char *architecture = ElfArchitecture(elf_header);
  if (!architecture) {
    fprintf(stderr, "Unrecognized ELF machine architecture: %d\n",
            elf_header->e_machine);
    return false;
  }

  std::string name = BaseFileName(obj_file);
  std::string os = "Linux";
  std::string id = FormatIdentifier(identifier);

  Module module(name, os, architecture, id);
  if (!LoadSymbols(obj_file, elf_header, &module))
    return false;
  if (!module.Write(sym_file))
    return false;

  return true;
}

}  // namespace google_breakpad
