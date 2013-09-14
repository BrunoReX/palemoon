/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <dlfcn.h>
#include <unistd.h>
#include <algorithm>
#include <fcntl.h>
#include "ElfLoader.h"
#include "CustomElf.h"
#include "Mappable.h"
#include "Logging.h"

#if defined(ANDROID)
#include <sys/syscall.h>

#include <android/api-level.h>
#if __ANDROID_API__ < 8
/* Android API < 8 doesn't provide sigaltstack */

extern "C" {

inline int sigaltstack(const stack_t *ss, stack_t *oss) {
  return syscall(__NR_sigaltstack, ss, oss);
}

} /* extern "C" */
#endif /* __ANDROID_API__ */
#endif /* ANDROID */

using namespace mozilla;

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#ifndef PAGE_MASK
#define PAGE_MASK (~ (PAGE_SIZE - 1))
#endif

/**
 * dlfcn.h replacements functions
 */

void *
__wrap_dlopen(const char *path, int flags)
{
  RefPtr<LibHandle> handle = ElfLoader::Singleton.Load(path, flags);
  if (handle)
    handle->AddDirectRef();
  return handle;
}

const char *
__wrap_dlerror(void)
{
  const char *error = ElfLoader::Singleton.lastError;
  ElfLoader::Singleton.lastError = NULL;
  return error;
}

void *
__wrap_dlsym(void *handle, const char *symbol)
{
  if (!handle) {
    ElfLoader::Singleton.lastError = "dlsym(NULL, sym) unsupported";
    return NULL;
  }
  if (handle != RTLD_DEFAULT && handle != RTLD_NEXT) {
    LibHandle *h = reinterpret_cast<LibHandle *>(handle);
    return h->GetSymbolPtr(symbol);
  }
  return dlsym(handle, symbol);
}

int
__wrap_dlclose(void *handle)
{
  if (!handle) {
    ElfLoader::Singleton.lastError = "No handle given to dlclose()";
    return -1;
  }
  reinterpret_cast<LibHandle *>(handle)->ReleaseDirectRef();
  return 0;
}

int
__wrap_dladdr(void *addr, Dl_info *info)
{
  RefPtr<LibHandle> handle = ElfLoader::Singleton.GetHandleByPtr(addr);
  if (!handle)
    return 0;
  info->dli_fname = handle->GetPath();
  return 1;
}

int
__wrap_dl_iterate_phdr(dl_phdr_cb callback, void *data)
{
  if (!ElfLoader::Singleton.dbg)
    return -1;

  for (ElfLoader::DebuggerHelper::iterator it = ElfLoader::Singleton.dbg.begin();
       it < ElfLoader::Singleton.dbg.end(); ++it) {
    dl_phdr_info info;
    info.dlpi_addr = reinterpret_cast<Elf::Addr>(it->l_addr);
    info.dlpi_name = it->l_name;
    info.dlpi_phdr = NULL;
    info.dlpi_phnum = 0;

    // Assuming l_addr points to Elf headers (in most cases, this is true),
    // get the Phdr location from there.
    uint8_t mapped;
    // If the page is not mapped, mincore returns an error.
    if (!mincore(const_cast<void*>(it->l_addr), PAGE_SIZE, &mapped)) {
      const Elf::Ehdr *ehdr = Elf::Ehdr::validate(it->l_addr);
      if (ehdr) {
        info.dlpi_phdr = reinterpret_cast<const Elf::Phdr *>(
                         reinterpret_cast<const char *>(ehdr) + ehdr->e_phoff);
        info.dlpi_phnum = ehdr->e_phnum;
      }
    }

    int ret = callback(&info, sizeof(dl_phdr_info), data);
    if (ret)
      return ret;
  }
  return 0;
}

/**
 * faulty.lib public API
 */

MFBT_API size_t
__dl_get_mappable_length(void *handle) {
  if (!handle)
    return 0;
  return reinterpret_cast<LibHandle *>(handle)->GetMappableLength();
}

MFBT_API void *
__dl_mmap(void *handle, void *addr, size_t length, off_t offset)
{
  if (!handle)
    return NULL;
  return reinterpret_cast<LibHandle *>(handle)->MappableMMap(addr, length,
                                                             offset);
}

MFBT_API void
__dl_munmap(void *handle, void *addr, size_t length)
{
  if (!handle)
    return;
  return reinterpret_cast<LibHandle *>(handle)->MappableMUnmap(addr, length);
}

namespace {

/**
 * Returns the part after the last '/' for the given path
 */
const char *
LeafName(const char *path)
{
  const char *lastSlash = strrchr(path, '/');
  if (lastSlash)
    return lastSlash + 1;
  return path;
}

} /* Anonymous namespace */

/**
 * LibHandle
 */
LibHandle::~LibHandle()
{
  free(path);
  if (mappable && mappable->GetKind() != Mappable::MAPPABLE_EXTRACT_FILE)
    delete mappable;
}

const char *
LibHandle::GetName() const
{
  return path ? LeafName(path) : NULL;
}

size_t
LibHandle::GetMappableLength() const
{
  MOZ_ASSERT(mappable != NULL, "GetMappableLength needs to be called first,"
                               " and only once");
  mappable = GetMappable();
  if (!mappable)
    return 0;
  return mappable->GetLength();
}

void *
LibHandle::MappableMMap(void *addr, size_t length, off_t offset) const
{
  MOZ_ASSERT(mappable == NULL, "MappableMMap must be called after"
                               " GetMappableLength");
  void* mapped = mappable->mmap(addr, length, PROT_READ, MAP_PRIVATE, offset);
  if (mapped != MAP_FAILED) {
    /* Ensure the availability of all pages within the mapping */
    for (size_t off = 0; off < length; off += PAGE_SIZE) {
      mappable->ensure(reinterpret_cast<char *>(mapped) + off);
    }
  }
  return mapped;
}

void
LibHandle::MappableMUnmap(void *addr, size_t length) const
{
  MOZ_ASSERT(mappable == NULL, "MappableMUnmap must be called after"
                               " MappableMMap and GetMappableLength");
  mappable->munmap(addr, length);
}

/**
 * SystemElf
 */
TemporaryRef<LibHandle>
SystemElf::Load(const char *path, int flags)
{
  /* The Android linker returns a handle when the file name matches an
   * already loaded library, even when the full path doesn't exist */
  if (path && path[0] == '/' && (access(path, F_OK) == -1)){
    debug("dlopen(\"%s\", 0x%x) = %p", path, flags, (void *)NULL);
    return NULL;
  }

  void *handle = dlopen(path, flags);
  debug("dlopen(\"%s\", 0x%x) = %p", path, flags, handle);
  ElfLoader::Singleton.lastError = dlerror();
  if (handle) {
    SystemElf *elf = new SystemElf(path, handle);
    ElfLoader::Singleton.Register(elf);
    return elf;
  }
  return NULL;
}

SystemElf::~SystemElf()
{
  if (!dlhandle)
    return;
  debug("dlclose(%p [\"%s\"])", dlhandle, GetPath());
  dlclose(dlhandle);
  ElfLoader::Singleton.lastError = dlerror();
  ElfLoader::Singleton.Forget(this);
}

void *
SystemElf::GetSymbolPtr(const char *symbol) const
{
  void *sym = dlsym(dlhandle, symbol);
  debug("dlsym(%p [\"%s\"], \"%s\") = %p", dlhandle, GetPath(), symbol, sym);
  ElfLoader::Singleton.lastError = dlerror();
  return sym;
}

Mappable *
SystemElf::GetMappable() const
{
  const char *path = GetPath();
  if (!path)
    return NULL;
#ifdef ANDROID
  /* On Android, if we don't have the full path, try in /system/lib */
  const char *name = LeafName(path);
  std::string systemPath;
  if (name == path) {
    systemPath = "/system/lib/";
    systemPath += path;
    path = systemPath.c_str();
  }
#endif

  return MappableFile::Create(path);
}

/**
 * ElfLoader
 */

/* Unique ElfLoader instance */
ElfLoader ElfLoader::Singleton;

TemporaryRef<LibHandle>
ElfLoader::Load(const char *path, int flags, LibHandle *parent)
{
  RefPtr<LibHandle> handle;

  /* Handle dlopen(NULL) directly. */
  if (!path) {
    handle = SystemElf::Load(NULL, flags);
    return handle;
  }

  /* TODO: Handle relative paths correctly */
  const char *name = LeafName(path);

  /* Search the list of handles we already have for a match. When the given
   * path is not absolute, compare file names, otherwise compare full paths. */
  if (name == path) {
    for (LibHandleList::iterator it = handles.begin(); it < handles.end(); ++it)
      if ((*it)->GetName() && (strcmp((*it)->GetName(), name) == 0))
        return *it;
  } else {
    for (LibHandleList::iterator it = handles.begin(); it < handles.end(); ++it)
      if ((*it)->GetPath() && (strcmp((*it)->GetPath(), path) == 0))
        return *it;
  }

  char *abs_path = NULL;
#ifdef MOZ_DEBUG_LINKER
  const char *requested_path = path;
#endif

  /* When the path is not absolute and the library is being loaded for
   * another, first try to load the library from the directory containing
   * that parent library. */
  if ((name == path) && parent) {
    const char *parentPath = parent->GetPath();
    abs_path = new char[strlen(parentPath) + strlen(path)];
    strcpy(abs_path, parentPath);
    char *slash = strrchr(abs_path, '/');
    strcpy(slash + 1, path);
    path = abs_path;
  }

  Mappable *mappable = GetMappableFromPath(path);

  /* Try loading with the custom linker if we have a Mappable */
  if (mappable)
    handle = CustomElf::Load(mappable, path, flags);

  /* Try loading with the system linker if everything above failed */
  if (!handle)
    handle = SystemElf::Load(path, flags);

  /* If we didn't have an absolute path and haven't been able to load
   * a library yet, try in the system search path */
  if (!handle && abs_path)
    handle = SystemElf::Load(name, flags);

  delete [] abs_path;
  debug("ElfLoader::Load(\"%s\", 0x%x, %p [\"%s\"]) = %p", requested_path, flags,
        reinterpret_cast<void *>(parent), parent ? parent->GetPath() : "",
        static_cast<void *>(handle));

  return handle;
}

mozilla::TemporaryRef<LibHandle>
ElfLoader::GetHandleByPtr(void *addr)
{
  /* Scan the list of handles we already have for a match */
  for (LibHandleList::iterator it = handles.begin(); it < handles.end(); ++it) {
    if ((*it)->Contains(addr))
      return *it;
  }
  return NULL;
}

Mappable *
ElfLoader::GetMappableFromPath(const char *path)
{
  const char *name = LeafName(path);
  Mappable *mappable = NULL;
  RefPtr<Zip> zip;
  const char *subpath;
  if ((subpath = strchr(path, '!'))) {
    char *zip_path = strndup(path, subpath - path);
    while (*(++subpath) == '/') { }
    zip = ZipCollection::GetZip(zip_path);
    Zip::Stream s;
    if (zip && zip->GetStream(subpath, &s)) {
      /* When the MOZ_LINKER_EXTRACT environment variable is set to "1",
       * compressed libraries are going to be (temporarily) extracted as
       * files, in the directory pointed by the MOZ_LINKER_CACHE
       * environment variable. */
      const char *extract = getenv("MOZ_LINKER_EXTRACT");
      if (extract && !strncmp(extract, "1", 2 /* Including '\0' */))
        mappable = MappableExtractFile::Create(name, zip, &s);
      if (!mappable) {
        if (s.GetType() == Zip::Stream::DEFLATE) {
          mappable = MappableDeflate::Create(name, zip, &s);
        } else if (s.GetType() == Zip::Stream::STORE) {
          mappable = MappableSeekableZStream::Create(name, zip, &s);
        }
      }
    }
  }
  /* If we couldn't load above, try with a MappableFile */
  if (!mappable && !zip)
    mappable = MappableFile::Create(path);

  return mappable;
}

void
ElfLoader::Register(LibHandle *handle)
{
  handles.push_back(handle);
  if (dbg && !handle->IsSystemElf())
    dbg.Add(static_cast<CustomElf *>(handle));
}

void
ElfLoader::Forget(LibHandle *handle)
{
  LibHandleList::iterator it = std::find(handles.begin(), handles.end(), handle);
  if (it != handles.end()) {
    debug("ElfLoader::Forget(%p [\"%s\"])", reinterpret_cast<void *>(handle),
                                            handle->GetPath());
    if (dbg && !handle->IsSystemElf())
      dbg.Remove(static_cast<CustomElf *>(handle));
    handles.erase(it);
  } else {
    debug("ElfLoader::Forget(%p [\"%s\"]): Handle not found",
          reinterpret_cast<void *>(handle), handle->GetPath());
  }
}

ElfLoader::~ElfLoader()
{
  LibHandleList list;
  /* Build up a list of all library handles with direct (external) references.
   * We actually skip system library handles because we want to keep at least
   * some of these open. Most notably, Mozilla codebase keeps a few libgnome
   * libraries deliberately open because of the mess that libORBit destruction
   * is. dlclose()ing these libraries actually leads to problems. */
  for (LibHandleList::reverse_iterator it = handles.rbegin();
       it < handles.rend(); ++it) {
    if ((*it)->DirectRefCount()) {
      if ((*it)->IsSystemElf()) {
        static_cast<SystemElf *>(*it)->Forget();
      } else {
        list.push_back(*it);
      }
    }
  }
  /* Force release all external references to the handles collected above */
  for (LibHandleList::iterator it = list.begin(); it < list.end(); ++it) {
    while ((*it)->ReleaseDirectRef()) { }
  }
  /* Remove the remaining system handles. */
  if (handles.size()) {
    list = handles;
    for (LibHandleList::reverse_iterator it = list.rbegin();
         it < list.rend(); ++it) {
      if ((*it)->IsSystemElf()) {
        debug("ElfLoader::~ElfLoader(): Remaining handle for \"%s\" "
              "[%d direct refs, %d refs total]", (*it)->GetPath(),
              (*it)->DirectRefCount(), (*it)->refCount());
      } else {
        debug("ElfLoader::~ElfLoader(): Unexpected remaining handle for \"%s\" "
              "[%d direct refs, %d refs total]", (*it)->GetPath(),
              (*it)->DirectRefCount(), (*it)->refCount());
        /* Not removing, since it could have references to other libraries,
         * destroying them as a side effect, and possibly leaving dangling
         * pointers in the handle list we're scanning */
      }
    }
  }
}

void
ElfLoader::stats(const char *when)
{
  for (LibHandleList::iterator it = Singleton.handles.begin();
       it < Singleton.handles.end(); ++it)
    if (!(*it)->IsSystemElf())
      static_cast<CustomElf *>(*it)->stats(when);
}

#ifdef __ARM_EABI__
int
ElfLoader::__wrap_aeabi_atexit(void *that, ElfLoader::Destructor destructor,
                               void *dso_handle)
{
  Singleton.destructors.push_back(
    DestructorCaller(destructor, that, dso_handle));
  return 0;
}
#else
int
ElfLoader::__wrap_cxa_atexit(ElfLoader::Destructor destructor, void *that,
                             void *dso_handle)
{
  Singleton.destructors.push_back(
    DestructorCaller(destructor, that, dso_handle));
  return 0;
}
#endif

void
ElfLoader::__wrap_cxa_finalize(void *dso_handle)
{
  /* Call all destructors for the given DSO handle in reverse order they were
   * registered. */
  std::vector<DestructorCaller>::reverse_iterator it;
  for (it = Singleton.destructors.rbegin();
       it < Singleton.destructors.rend(); ++it) {
    if (it->IsForHandle(dso_handle)) {
      it->Call();
    }
  }
}

void
ElfLoader::DestructorCaller::Call()
{
  if (destructor) {
    debug("ElfLoader::DestructorCaller::Call(%p, %p, %p)",
          FunctionPtr(destructor), object, dso_handle);
    destructor(object);
    destructor = NULL;
  }
}

ElfLoader::DebuggerHelper::DebuggerHelper(): dbg(NULL)
{
  /* Find ELF auxiliary vectors.
   *
   * The kernel stores the following data on the stack when starting a
   * program:
   *   argc
   *   argv[0] (pointer into argv strings defined below)
   *   argv[1] (likewise)
   *   ...
   *   argv[argc - 1] (likewise)
   *   NULL
   *   envp[0] (pointer into environment strings defined below)
   *   envp[1] (likewise)
   *   ...
   *   envp[n] (likewise)
   *   NULL
   *   auxv[0] (first ELF auxiliary vector)
   *   auxv[1] (second ELF auxiliary vector)
   *   ...
   *   auxv[p] (last ELF auxiliary vector)
   *   (AT_NULL, NULL)
   *   padding
   *   argv strings, separated with '\0'
   *   environment strings, separated with '\0'
   *   NULL
   *
   * What we are after are the auxv values defined by the following struct.
   */
  struct AuxVector {
    Elf::Addr type;
    Elf::Addr value;
  };

  /* Pointer to the environment variables list */
  extern char **environ;

  /* The environment may have changed since the program started, in which
   * case the environ variables list isn't the list the kernel put on stack
   * anymore. But in this new list, variables that didn't change still point
   * to the strings the kernel put on stack. It is quite unlikely that two
   * modified environment variables point to two consecutive strings in memory,
   * so we assume that if two consecutive environment variables point to two
   * consecutive strings, we found strings the kernel put on stack. */
  char **env;
  for (env = environ; *env; env++)
    if (*env + strlen(*env) + 1 == env[1])
      break;
  if (!*env)
    return;

  /* Next, we scan the stack backwards to find a pointer to one of those
   * strings we found above, which will give us the location of the original
   * envp list. As we are looking for pointers, we need to look at 32-bits or
   * 64-bits aligned values, depening on the architecture. */
  char **scan = reinterpret_cast<char **>(
                reinterpret_cast<uintptr_t>(*env) & ~(sizeof(void *) - 1));
  while (*env != *scan)
    scan--;

  /* Finally, scan forward to find the last environment variable pointer and
   * thus the first auxiliary vector. */
  while (*scan++);
  AuxVector *auxv = reinterpret_cast<AuxVector *>(scan);

  /* The two values of interest in the auxiliary vectors are AT_PHDR and
   * AT_PHNUM, which gives us the the location and size of the ELF program
   * headers. */
  Array<Elf::Phdr> phdrs;
  char *base = NULL;
  while (auxv->type) {
    if (auxv->type == AT_PHDR) {
      phdrs.Init(reinterpret_cast<Elf::Phdr*>(auxv->value));
      /* Assume the base address is the first byte of the same page */
      base = reinterpret_cast<char *>(auxv->value & PAGE_MASK);
    }
    if (auxv->type == AT_PHNUM)
      phdrs.Init(auxv->value);
    auxv++;
  }

  if (!phdrs) {
    debug("Couldn't find program headers");
    return;
  }

  /* In some cases, the address for the program headers we get from the
   * auxiliary vectors is not mapped, because of the PT_LOAD segments
   * definitions in the program executable. Trying to map anonymous memory
   * with a hint giving the base address will return a different address
   * if something is mapped there, and the base address otherwise. */
  MappedPtr mem(mmap(base, PAGE_SIZE, PROT_NONE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), PAGE_SIZE);
  if (mem == base) {
    /* If program headers aren't mapped, try to map them */
    int fd = open("/proc/self/exe", O_RDONLY);
    if (fd == -1) {
      debug("Failed to open /proc/self/exe");
      return;
    }
    mem.Assign(mmap(base, PAGE_SIZE, PROT_READ, MAP_PRIVATE, fd, 0), PAGE_SIZE);
    /* If we don't manage to map at the right address, just give up. */
    if (mem != base) {
      debug("Couldn't read program headers");
      return;
    }
  }
  /* Sanity check: the first bytes at the base address should be an ELF
   * header. */
  if (!Elf::Ehdr::validate(base)) {
     debug("Couldn't find program base");
     return;
  }

  /* Search for the program PT_DYNAMIC segment */
  Array<Elf::Dyn> dyns;
  for (Array<Elf::Phdr>::iterator phdr = phdrs.begin(); phdr < phdrs.end();
       ++phdr) {
    /* While the program headers are expected within the first mapped page of
     * the program executable, the executable PT_LOADs may actually make them
     * loaded at an address that is not the wanted base address of the
     * library. We thus need to adjust the base address, compensating for the
     * virtual address of the PT_LOAD segment corresponding to offset 0. */
    if (phdr->p_type == PT_LOAD && phdr->p_offset == 0)
      base -= phdr->p_vaddr;
    if (phdr->p_type == PT_DYNAMIC)
      dyns.Init(base + phdr->p_vaddr, phdr->p_filesz);
  }
  if (!dyns) {
    debug("Failed to find PT_DYNAMIC section in program");
    return;
  }

  /* Search for the DT_DEBUG information */
  for (Array<Elf::Dyn>::iterator dyn = dyns.begin(); dyn < dyns.end(); ++dyn) {
    if (dyn->d_tag == DT_DEBUG) {
      dbg = reinterpret_cast<r_debug *>(dyn->d_un.d_ptr);
      break;
    }
  }
  debug("DT_DEBUG points at %p", static_cast<void *>(dbg));
}

/**
 * Helper class to ensure the given pointer is writable within the scope of
 * an instance. Permissions to the memory page where the pointer lies are
 * restored to their original value when the instance is destroyed.
 */
class EnsureWritable
{
public:
  template <typename T>
  EnsureWritable(T *ptr, size_t length_ = sizeof(T))
  {
    MOZ_ASSERT(length_ < PAGE_SIZE);
    prot = -1;
    page = MAP_FAILED;

    uintptr_t firstPage = reinterpret_cast<uintptr_t>(ptr) & PAGE_MASK;
    length = (reinterpret_cast<uintptr_t>(ptr) + length_ - firstPage
              + PAGE_SIZE - 1) & PAGE_MASK;

    uintptr_t end;

    prot = getProt(firstPage, &end);
    if (prot == -1 || (firstPage + length) > end)
      MOZ_CRASH();

    if (prot & PROT_WRITE)
      return;

    page = reinterpret_cast<void *>(firstPage);
    mprotect(page, length, prot | PROT_WRITE);
  }

  ~EnsureWritable()
  {
    if (page != MAP_FAILED) {
      mprotect(page, length, prot);
}
  }

private:
  int getProt(uintptr_t addr, uintptr_t *end)
  {
    /* The interesting part of the /proc/self/maps format looks like:
     * startAddr-endAddr rwxp */
    int result = 0;
    AutoCloseFILE f(fopen("/proc/self/maps", "r"));
    while (f) {
      unsigned long long startAddr, endAddr;
      char perms[5];
      if (fscanf(f, "%llx-%llx %4s %*1024[^\n] ", &startAddr, &endAddr, perms) != 3)
        return -1;
      if (addr < startAddr || addr >= endAddr)
        continue;
      if (perms[0] == 'r')
        result |= PROT_READ;
      else if (perms[0] != '-')
        return -1;
      if (perms[1] == 'w')
        result |= PROT_WRITE;
      else if (perms[1] != '-')
        return -1;
      if (perms[2] == 'x')
        result |= PROT_EXEC;
      else if (perms[2] != '-')
        return -1;
      *end = endAddr;
      return result;
    }
    return -1;
  }

  int prot;
  void *page;
  size_t length;
};

/**
 * The system linker maintains a doubly linked list of library it loads
 * for use by the debugger. Unfortunately, it also uses the list pointers
 * in a lot of operations and adding our data in the list is likely to
 * trigger crashes when the linker tries to use data we don't provide or
 * that fall off the amount data we allocated. Fortunately, the linker only
 * traverses the list forward and accesses the head of the list from a
 * private pointer instead of using the value in the r_debug structure.
 * This means we can safely add members at the beginning of the list.
 * Unfortunately, gdb checks the coherency of l_prev values, so we have
 * to adjust the l_prev value for the first element the system linker
 * knows about. Fortunately, it doesn't use l_prev, and the first element
 * is not ever going to be released before our elements, since it is the
 * program executable, so the system linker should not be changing
 * r_debug::r_map.
 */
void
ElfLoader::DebuggerHelper::Add(ElfLoader::link_map *map)
{
  if (!dbg->r_brk)
    return;
  dbg->r_state = r_debug::RT_ADD;
  dbg->r_brk();
  map->l_prev = NULL;
  map->l_next = dbg->r_map;
  if (!firstAdded) {
    firstAdded = map;
    /* When adding a library for the first time, r_map points to data
     * handled by the system linker, and that data may be read-only */
    EnsureWritable w(&dbg->r_map->l_prev);
    dbg->r_map->l_prev = map;
  } else
    dbg->r_map->l_prev = map;
  dbg->r_map = map;
  dbg->r_state = r_debug::RT_CONSISTENT;
  dbg->r_brk();
}

void
ElfLoader::DebuggerHelper::Remove(ElfLoader::link_map *map)
{
  if (!dbg->r_brk)
    return;
  dbg->r_state = r_debug::RT_DELETE;
  dbg->r_brk();
  if (dbg->r_map == map)
    dbg->r_map = map->l_next;
  else
    map->l_prev->l_next = map->l_next;
  if (map == firstAdded) {
    firstAdded = map->l_prev;
    /* When removing the first added library, its l_next is going to be
     * data handled by the system linker, and that data may be read-only */
    EnsureWritable w(&map->l_next->l_prev);
    map->l_next->l_prev = map->l_prev;
  } else
    map->l_next->l_prev = map->l_prev;
  dbg->r_state = r_debug::RT_CONSISTENT;
  dbg->r_brk();
}

#if defined(ANDROID)
/* As some system libraries may be calling signal() or sigaction() to
 * set a SIGSEGV handler, effectively breaking MappableSeekableZStream,
 * or worse, restore our SIGSEGV handler with wrong flags (which using
 * signal() will do), we want to hook into the system's sigaction() to
 * replace it with our own wrapper instead, so that our handler is never
 * replaced. We used to only do that with libraries this linker loads,
 * but it turns out at least one system library does call signal() and
 * breaks us (libsc-a3xx.so on the Samsung Galaxy S4).
 * As libc's signal (bsd_signal/sysv_signal, really) calls sigaction
 * under the hood, instead of calling the signal system call directly,
 * we only need to hook sigaction. This is true for both bionic and
 * glibc.
 */

/* libc's sigaction */
extern "C" int
sigaction(int signum, const struct sigaction *act,
          struct sigaction *oldact);

/* Simple reimplementation of sigaction. This is roughly equivalent
 * to the assembly that comes in bionic, but not quite equivalent to
 * glibc's implementation, so we only use this on Android. */
int
sys_sigaction(int signum, const struct sigaction *act,
              struct sigaction *oldact)
{
  return syscall(__NR_sigaction, signum, act, oldact);
}

/* Replace the first instructions of the given function with a jump
 * to the given new function. */
template <typename T>
static bool
Divert(T func, T new_func)
{
  void *ptr = FunctionPtr(func);
  uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

#if defined(__i386__)
  // A 32-bit jump is a 5 bytes instruction.
  EnsureWritable w(ptr, 5);
  *reinterpret_cast<unsigned char *>(addr) = 0xe9; // jmp
  *reinterpret_cast<intptr_t *>(addr + 1) =
    reinterpret_cast<uintptr_t>(new_func) - addr - 5; // target displacement
  return true;
#elif defined(__arm__)
  const unsigned char trampoline[] = {
                            // .thumb
    0x46, 0x04,             // nop
    0x78, 0x47,             // bx pc
    0x46, 0x04,             // nop
                            // .arm
    0x04, 0xf0, 0x1f, 0xe5, // ldr pc, [pc, #-4]
                            // .word <new_func>
  };
  const unsigned char *start;
  if (addr & 0x01) {
    /* Function is thumb, the actual address of the code is without the
     * least significant bit. */
    addr--;
    /* The arm part of the trampoline needs to be 32-bit aligned */
    if (addr & 0x02)
      start = trampoline;
    else
      start = trampoline + 2;
  } else {
    /* Function is arm, we only need the arm part of the trampoline */
    start = trampoline + 6;
  }

  size_t len = sizeof(trampoline) - (start - trampoline);
  EnsureWritable w(reinterpret_cast<void *>(addr), len + sizeof(void *));
  memcpy(reinterpret_cast<void *>(addr), start, len);
  *reinterpret_cast<void **>(addr + len) = FunctionPtr(new_func);
  cacheflush(addr, addr + len + sizeof(void *), 0);
  return true;
#else
  return false;
#endif
}
#else
#define sys_sigaction sigaction
template <typename T>
static bool
Divert(T func, T new_func)
{
  return false;
}
#endif

SEGVHandler::SEGVHandler()
: registeredHandler(false)
{
  if (!Divert(sigaction, __wrap_sigaction))
    return;
  /* Setup an alternative stack if the already existing one is not big
   * enough, or if there is none. */
  if (sigaltstack(NULL, &oldStack) == -1 || !oldStack.ss_sp ||
      oldStack.ss_size < stackSize) {
    stackPtr.Assign(mmap(NULL, stackSize, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), stackSize);
    stack_t stack;
    stack.ss_sp = stackPtr;
    stack.ss_size = stackSize;
    stack.ss_flags = 0;
    sigaltstack(&stack, NULL);
  }
  /* Register our own handler, and store the already registered one in
   * SEGVHandler's struct sigaction member */
  struct sigaction action;
  action.sa_sigaction = &SEGVHandler::handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
  action.sa_restorer = NULL;
  registeredHandler = !sys_sigaction(SIGSEGV, &action, &this->action);
}

SEGVHandler::~SEGVHandler()
{
  /* Restore alternative stack for signals */
  sigaltstack(&oldStack, NULL);
  /* Restore original signal handler */
  sys_sigaction(SIGSEGV, &this->action, NULL);
}

/* TODO: "properly" handle signal masks and flags */
void SEGVHandler::handler(int signum, siginfo_t *info, void *context)
{
  //ASSERT(signum == SIGSEGV);
  debug("Caught segmentation fault @%p", info->si_addr);

  /* Check whether we segfaulted in the address space of a CustomElf. We're
   * only expecting that to happen as an access error. */
  if (info->si_code == SEGV_ACCERR) {
    mozilla::RefPtr<LibHandle> handle =
      ElfLoader::Singleton.GetHandleByPtr(info->si_addr);
    if (handle && !handle->IsSystemElf()) {
      debug("Within the address space of a CustomElf");
      CustomElf *elf = static_cast<CustomElf *>(static_cast<LibHandle *>(handle));
      if (elf->mappable->ensure(info->si_addr))
        return;
    }
  }

  /* Redispatch to the registered handler */
  SEGVHandler &that = ElfLoader::Singleton;
  if (that.action.sa_flags & SA_SIGINFO) {
    debug("Redispatching to registered handler @%p",
          FunctionPtr(that.action.sa_sigaction));
    that.action.sa_sigaction(signum, info, context);
  } else if (that.action.sa_handler == SIG_DFL) {
    debug("Redispatching to default handler");
    /* Reset the handler to the default one, and trigger it. */
    sys_sigaction(signum, &that.action, NULL);
    raise(signum);
  } else if (that.action.sa_handler != SIG_IGN) {
    debug("Redispatching to registered handler @%p",
          FunctionPtr(that.action.sa_handler));
    that.action.sa_handler(signum);
  } else {
    debug("Ignoring");
  }
}

int
SEGVHandler::__wrap_sigaction(int signum, const struct sigaction *act,
                              struct sigaction *oldact)
{
  /* Use system sigaction() function for all but SIGSEGV signals. */
  if (signum != SIGSEGV)
    return sys_sigaction(signum, act, oldact);

  SEGVHandler &that = ElfLoader::Singleton;
  if (oldact)
    *oldact = that.action;
  if (act)
    that.action = *act;
  return 0;
}
