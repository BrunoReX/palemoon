/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ElfLoader_h
#define ElfLoader_h

#include <vector>
#include <dlfcn.h>
#include <signal.h>
#include "mozilla/RefPtr.h"
#include "Zip.h"
#include "Elfxx.h"

/**
 * dlfcn.h replacement functions
 */
extern "C" {
  void *__wrap_dlopen(const char *path, int flags);
  const char *__wrap_dlerror(void);
  void *__wrap_dlsym(void *handle, const char *symbol);
  int __wrap_dlclose(void *handle);

#ifndef HAVE_DLADDR
  typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
  } Dl_info;
#endif
  int __wrap_dladdr(void *addr, Dl_info *info);

  struct dl_phdr_info {
    Elf::Addr dlpi_addr;
    const char *dlpi_name;
    const Elf::Phdr *dlpi_phdr;
    Elf::Half dlpi_phnum;
  };

  typedef int (*dl_phdr_cb)(struct dl_phdr_info *, size_t, void *);
  int __wrap_dl_iterate_phdr(dl_phdr_cb callback, void *data);

/**
 * faulty.lib public API
 */
MFBT_API size_t
__dl_get_mappable_length(void *handle);

MFBT_API void *
__dl_mmap(void *handle, void *addr, size_t length, off_t offset);

MFBT_API void
__dl_munmap(void *handle, void *addr, size_t length);

}

/**
 * Specialize RefCounted template for LibHandle. We may get references to
 * LibHandles during the execution of their destructor, so we need
 * RefCounted<LibHandle>::Release to support some reentrancy. See further
 * below.
 */
class LibHandle;

namespace mozilla {
namespace detail {

template <> inline void RefCounted<LibHandle, AtomicRefCount>::Release();

template <> inline RefCounted<LibHandle, AtomicRefCount>::~RefCounted()
{
  MOZ_ASSERT(refCnt == 0x7fffdead);
}

} /* namespace detail */
} /* namespace mozilla */

/* Forward declaration */
class Mappable;

/**
 * Abstract class for loaded libraries. Libraries may be loaded through the
 * system linker or this linker, both cases will be derived from this class.
 */
class LibHandle: public mozilla::AtomicRefCounted<LibHandle>
{
public:
  /**
   * Constructor. Takes the path of the loaded library and will store a copy
   * of the leaf name.
   */
  LibHandle(const char *path)
  : directRefCnt(0), path(path ? strdup(path) : NULL), mappable(NULL) { }

  /**
   * Destructor.
   */
  virtual ~LibHandle();

  /**
   * Returns the pointer to the address to which the given symbol resolves
   * inside the library. It is not supposed to resolve the symbol in other
   * libraries, although in practice, it will for system libraries.
   */
  virtual void *GetSymbolPtr(const char *symbol) const = 0;

  /**
   * Returns whether the given address is part of the virtual address space
   * covered by the loaded library.
   */
  virtual bool Contains(void *addr) const = 0;

  /**
   * Returns the file name of the library without the containing directory.
   */
  const char *GetName() const;

  /**
   * Returns the full path of the library, when available. Otherwise, returns
   * the file name.
   */
  const char *GetPath() const
  {
    return path;
  }

  /**
   * Library handles can be referenced from other library handles or
   * externally (when dlopen()ing using this linker). We need to be
   * able to distinguish between the two kind of referencing for better
   * bookkeeping.
   */
  void AddDirectRef()
  {
    ++directRefCnt;
    mozilla::AtomicRefCounted<LibHandle>::AddRef();
  }

  /**
   * Releases a direct reference, and returns whether there are any direct
   * references left.
   */
  bool ReleaseDirectRef()
  {
    bool ret = false;
    if (directRefCnt) {
      MOZ_ASSERT(directRefCnt <=
                 mozilla::AtomicRefCounted<LibHandle>::refCount());
      if (--directRefCnt)
        ret = true;
      mozilla::AtomicRefCounted<LibHandle>::Release();
    }
    return ret;
  }

  /**
   * Returns the number of direct references
   */
  int DirectRefCount()
  {
    return directRefCnt;
  }

  /**
   * Returns the complete size of the file or stream behind the library
   * handle.
   */
  size_t GetMappableLength() const;

  /**
   * Returns a memory mapping of the file or stream behind the library
   * handle.
   */
  void *MappableMMap(void *addr, size_t length, off_t offset) const;

  /**
   * Unmaps a memory mapping of the file or stream behind the library
   * handle.
   */
  void MappableMUnmap(void *addr, size_t length) const;

protected:
  /**
   * Returns a mappable object for use by MappableMMap and related functions.
   */
  virtual Mappable *GetMappable() const = 0;

  /**
   * Returns whether the handle is a SystemElf or not. (short of a better way
   * to do this without RTTI)
   */
  friend class ElfLoader;
  friend class CustomElf;
  friend class SEGVHandler;
  virtual bool IsSystemElf() const { return false; }

private:
  int directRefCnt;
  char *path;

  /* Mappable object keeping the result of GetMappable() */
  mutable Mappable *mappable;
};

/**
 * Specialized RefCounted<LibHandle>::Release. Under normal operation, when
 * refCnt reaches 0, the LibHandle is deleted. Its refCnt is however increased
 * to 1 on normal builds, and 0x7fffdead on debug builds so that the LibHandle
 * can still be referenced while the destructor is executing. The refCnt is
 * allowed to grow > 0x7fffdead, but not to decrease under that value, which
 * would mean too many Releases from within the destructor.
 */
namespace mozilla {
namespace detail {

template <> inline void RefCounted<LibHandle, AtomicRefCount>::Release() {
#ifdef DEBUG
  if (refCnt > 0x7fff0000)
    MOZ_ASSERT(refCnt > 0x7fffdead);
#endif
  MOZ_ASSERT(refCnt > 0);
  if (refCnt > 0) {
    if (0 == --refCnt) {
#ifdef DEBUG
      refCnt = 0x7fffdead;
#else
      refCnt = 1;
#endif
      delete static_cast<LibHandle*>(this);
    }
  }
}

} /* namespace detail */
} /* namespace mozilla */

/**
 * Class handling libraries loaded by the system linker
 */
class SystemElf: public LibHandle
{
public:
  /**
   * Returns a new SystemElf for the given path. The given flags are passed
   * to dlopen().
   */
  static mozilla::TemporaryRef<LibHandle> Load(const char *path, int flags);

  /**
   * Inherited from LibHandle
   */
  virtual ~SystemElf();
  virtual void *GetSymbolPtr(const char *symbol) const;
  virtual bool Contains(void *addr) const { return false; /* UNIMPLEMENTED */ }

protected:
  virtual Mappable *GetMappable() const;

  /**
   * Returns whether the handle is a SystemElf or not. (short of a better way
   * to do this without RTTI)
   */
  friend class ElfLoader;
  virtual bool IsSystemElf() const { return true; }

  /**
   * Remove the reference to the system linker handle. This avoids dlclose()
   * being called when the instance is destroyed.
   */
  void Forget()
  {
    dlhandle = NULL;
  }

private:
  /**
   * Private constructor
   */
  SystemElf(const char *path, void *handle)
  : LibHandle(path), dlhandle(handle) { }

  /* Handle as returned by system dlopen() */
  void *dlhandle;
};

/**
 * The ElfLoader registers its own SIGSEGV handler to handle segmentation
 * faults within the address space of the loaded libraries. It however
 * allows a handler to be set for faults in other places, and redispatches
 * to the handler set through signal() or sigaction().
 */
class SEGVHandler
{
public:
  bool hasRegisteredHandler() {
    return registeredHandler;
  }

protected:
  SEGVHandler();
  ~SEGVHandler();

private:
  static int __wrap_sigaction(int signum, const struct sigaction *act,
                              struct sigaction *oldact);

  /**
   * SIGSEGV handler registered with __wrap_signal or __wrap_sigaction.
   */
  struct sigaction action;
  
  /**
   * ElfLoader SIGSEGV handler.
   */
  static void handler(int signum, siginfo_t *info, void *context);

  /**
   * Size of the alternative stack. The printf family requires more than 8KB
   * of stack, and our signal handler may print a few things.
   */
  static const size_t stackSize = 12 * 1024;

  /**
   * Alternative stack information used before initialization.
   */
  stack_t oldStack;

  /**
   * Pointer to an alternative stack for signals. Only set if oldStack is
   * not set or not big enough.
   */
  MappedPtr stackPtr;

  bool registeredHandler;
};

/**
 * Elf Loader class in charge of loading and bookkeeping libraries.
 */
class ElfLoader: public SEGVHandler
{
public:
  /**
   * The Elf Loader instance
   */
  static ElfLoader Singleton;

  /**
   * Loads the given library with the given flags. Equivalent to dlopen()
   * The extra "parent" argument optionally gives the handle of the library
   * requesting the given library to be loaded. The loader may look in the
   * directory containing that parent library for the library to load.
   */
  mozilla::TemporaryRef<LibHandle> Load(const char *path, int flags,
                                        LibHandle *parent = NULL);

  /**
   * Returns the handle of the library containing the given address in
   * its virtual address space, i.e. the library handle for which
   * LibHandle::Contains returns true. Its purpose is to allow to
   * implement dladdr().
   */
  mozilla::TemporaryRef<LibHandle> GetHandleByPtr(void *addr);

  /**
   * Returns a Mappable object for the path. Paths in the form
   *   /foo/bar/baz/archive!/directory/lib.so
   * try to load the directory/lib.so in /foo/bar/baz/archive, provided
   * that file is a Zip archive.
   */
  static Mappable *GetMappableFromPath(const char *path);

protected:
  /**
   * Registers the given handle. This method is meant to be called by
   * LibHandle subclass creators.
   */
  void Register(LibHandle *handle);

  /**
   * Forget about the given handle. This method is meant to be called by
   * LibHandle subclass destructors.
   */
  void Forget(LibHandle *handle);

  /* Last error. Used for dlerror() */
  friend class SystemElf;
  friend const char *__wrap_dlerror(void);
  friend void *__wrap_dlsym(void *handle, const char *symbol);
  friend int __wrap_dlclose(void *handle);
  const char *lastError;

private:
  ~ElfLoader();

  /* Bookkeeping */
  typedef std::vector<LibHandle *> LibHandleList;
  LibHandleList handles;

protected:
  friend class CustomElf;
  /**
   * Show some stats about Mappables in CustomElfs. The when argument is to
   * be used by the caller to give an identifier of the when the stats call
   * is made.
   */
  static void stats(const char *when);

  /* Definition of static destructors as to be used for C++ ABI compatibility */
  typedef void (*Destructor)(void *object);

  /**
   * C++ ABI makes static initializers register destructors through a specific
   * atexit interface. On glibc/linux systems, the dso_handle is a pointer
   * within a given library. On bionic/android systems, it is an undefined
   * symbol. Making sense of the value is not really important, and all that
   * is really important is that it is different for each loaded library, so
   * that they can be discriminated when shutting down. For convenience, on
   * systems where the dso handle is a symbol, that symbol is resolved to
   * point at corresponding CustomElf.
   *
   * Destructors are registered with __*_atexit with an associated object to
   * be passed as argument when it is called.
   *
   * When __cxa_finalize is called, destructors registered for the given
   * DSO handle are called in the reverse order they were registered.
   */
#ifdef __ARM_EABI__
  static int __wrap_aeabi_atexit(void *that, Destructor destructor,
                                 void *dso_handle);
#else
  static int __wrap_cxa_atexit(Destructor destructor, void *that,
                               void *dso_handle);
#endif

  static void __wrap_cxa_finalize(void *dso_handle);

  /**
   * Registered destructor. Keeps track of the destructor function pointer,
   * associated object to call it with, and DSO handle.
   */
  class DestructorCaller {
  public:
    DestructorCaller(Destructor destructor, void *object, void *dso_handle)
    : destructor(destructor), object(object), dso_handle(dso_handle) { }

    /**
     * Call the destructor function with the associated object.
     * Call only once, see CustomElf::~CustomElf.
     */
    void Call();

    /**
     * Returns whether the destructor is associated to the given DSO handle
     */
    bool IsForHandle(void *handle) const
    {
      return handle == dso_handle;
    }

  private:
    Destructor destructor;
    void *object;
    void *dso_handle;
  };

private:
  /* Keep track of all registered destructors */
  std::vector<DestructorCaller> destructors;

  /* Forward declaration, see further below */
  class DebuggerHelper;
public:
  /* Loaded object descriptor for the debugger interface below*/
  struct link_map {
    /* Base address of the loaded object. */
    const void *l_addr;
    /* File name */
    const char *l_name;
    /* Address of the PT_DYNAMIC segment. */
    const void *l_ld;

  private:
    friend class ElfLoader::DebuggerHelper;
    /* Double linked list of loaded objects. */
    link_map *l_next, *l_prev;
  };

private:
  /* Data structure used by the linker to give details about shared objects it
   * loaded to debuggers. This is normally defined in link.h, but Android
   * headers lack this file. */
  struct r_debug {
    /* Version number of the protocol. */
    int r_version;

    /* Head of the linked list of loaded objects. */
    link_map *r_map;

    /* Function to be called when updates to the linked list of loaded objects
     * are going to occur. The function is to be called before and after
     * changes. */
    void (*r_brk)(void);

    /* Indicates to the debugger what state the linked list of loaded objects
     * is in when the function above is called. */
    enum {
      RT_CONSISTENT, /* Changes are complete */
      RT_ADD,        /* Beginning to add a new object */
      RT_DELETE      /* Beginning to remove an object */
    } r_state;
  };

  /* Helper class used to integrate libraries loaded by this linker in
   * r_debug */
  class DebuggerHelper
  {
  public:
    DebuggerHelper();

    operator bool()
    {
      return dbg;
    }

    /* Make the debugger aware of a new loaded object */
    void Add(link_map *map);

    /* Make the debugger aware of the unloading of an object */
    void Remove(link_map *map);

    /* Iterates over all link_maps */
    class iterator
    {
    public:
      const link_map *operator ->() const
      {
        return item;
      }

      const link_map &operator ++()
      {
        item = item->l_next;
        return *item;
      }

      bool operator<(const iterator &other) const
      {
        if (other.item == NULL)
          return item ? true : false;
        MOZ_NOT_REACHED("DebuggerHelper::iterator::operator< called with something else than DebuggerHelper::end()");
      }
    protected:
      friend class DebuggerHelper;
      iterator(const link_map *item): item(item) { }

    private:
      const link_map *item;
    };

    iterator begin() const
    {
      return iterator(dbg ? dbg->r_map : NULL);
    }

    iterator end() const
    {
      return iterator(NULL);
    }

  private:
    r_debug *dbg;
    link_map *firstAdded;
  };
  friend int __wrap_dl_iterate_phdr(dl_phdr_cb callback, void *data);
  DebuggerHelper dbg;
};

#endif /* ElfLoader_h */
