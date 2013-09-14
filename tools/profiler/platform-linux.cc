// Copyright (c) 2006-2011 The Chromium Authors. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the
//    distribution.
//  * Neither the name of Google, Inc. nor the names of its contributors
//    may be used to endorse or promote products derived from this
//    software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
// OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
// AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
// OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.

/*
# vim: sw=2
*/
#include <stdio.h>

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sched.h>
#ifdef ANDROID
#include <android/log.h>
#else
#define __android_log_print(a, ...)
#endif
// Ubuntu Dapper requires memory pages to be marked as
// executable. Otherwise, OS raises an exception when executing code
// in that page.
#include <sys/types.h>  // mmap & munmap
#include <sys/mman.h>   // mmap & munmap
#include <sys/stat.h>   // open
#include <fcntl.h>      // open
#include <unistd.h>     // sysconf
#ifdef __GLIBC__
#include <execinfo.h>   // backtrace, backtrace_symbols
#endif  // def __GLIBC__
#include <strings.h>    // index
#include <errno.h>
#include <stdarg.h>
#include "platform.h"
#include "GeckoProfilerImpl.h"
#include "mozilla/Mutex.h"
#include "ProfileEntry.h"
#include "nsThreadUtils.h"
#include "TableTicker.h"
#include "UnwinderThread2.h"

#include <string.h>
#include <stdio.h>
#include <list>

#define SIGNAL_SAVE_PROFILE SIGUSR2

#if defined(__GLIBC__)
// glibc doesn't implement gettid(2).
#include <sys/syscall.h>
pid_t gettid()
{
  return (pid_t) syscall(SYS_gettid);
}
#endif

#if !defined(ANDROID)
// Keep track of when any of our threads calls fork(), so we can
// temporarily disable signal delivery during the fork() call.  Not
// doing so appears to cause a kind of race, in which signals keep
// getting delivered to the thread doing fork(), which keeps causing
// it to fail and be restarted; hence forward progress is delayed a
// great deal.  A side effect of this is to permanently disable
// sampling in the child process.  See bug 837390.

// Unfortunately this is only doable on non-Android, since Bionic
// doesn't have pthread_atfork.

// This records the current state at the time we paused it.
static bool was_paused = false;

// In the parent, just before the fork, record the pausedness state,
// and then pause.
static void paf_prepare(void) {
  if (Sampler::GetActiveSampler()) {
    was_paused = Sampler::GetActiveSampler()->IsPaused();
    Sampler::GetActiveSampler()->SetPaused(true);
  } else {
    was_paused = false;
  }
}

// In the parent, just after the fork, return pausedness to the
// pre-fork state.
static void paf_parent(void) {
  if (Sampler::GetActiveSampler())
    Sampler::GetActiveSampler()->SetPaused(was_paused);
}

// Set up the fork handlers.  This is called just once, at the first
// call to SenderEntry.
static void* setup_atfork() {
  pthread_atfork(paf_prepare, paf_parent, NULL);
  return NULL;
}
#endif /* !defined(ANDROID) */

#ifdef ANDROID
#include "android-signal-defs.h"
#endif

struct SamplerRegistry {
  static void AddActiveSampler(Sampler *sampler) {
    ASSERT(!SamplerRegistry::sampler);
    SamplerRegistry::sampler = sampler;
  }
  static void RemoveActiveSampler(Sampler *sampler) {
    SamplerRegistry::sampler = NULL;
  }
  static Sampler *sampler;
};

Sampler *SamplerRegistry::sampler = NULL;

static ThreadProfile* sCurrentThreadProfile = NULL;

static void ProfilerSaveSignalHandler(int signal, siginfo_t* info, void* context) {
  Sampler::GetActiveSampler()->RequestSave();
}

#ifdef ANDROID
#define V8_HOST_ARCH_ARM 1
#define SYS_gettid __NR_gettid
#define SYS_tgkill __NR_tgkill
#else
#define V8_HOST_ARCH_X64 1
#endif
static void ProfilerSignalHandler(int signal, siginfo_t* info, void* context) {
  if (!Sampler::GetActiveSampler())
    return;

  TickSample sample_obj;
  TickSample* sample = &sample_obj;
  sample->context = context;

#ifdef ENABLE_SPS_LEAF_DATA
  // If profiling, we extract the current pc and sp.
  if (Sampler::GetActiveSampler()->IsProfiling()) {
    // Extracting the sample from the context is extremely machine dependent.
    ucontext_t* ucontext = reinterpret_cast<ucontext_t*>(context);
    mcontext_t& mcontext = ucontext->uc_mcontext;
#if V8_HOST_ARCH_IA32
    sample->pc = reinterpret_cast<Address>(mcontext.gregs[REG_EIP]);
    sample->sp = reinterpret_cast<Address>(mcontext.gregs[REG_ESP]);
    sample->fp = reinterpret_cast<Address>(mcontext.gregs[REG_EBP]);
#elif V8_HOST_ARCH_X64
    sample->pc = reinterpret_cast<Address>(mcontext.gregs[REG_RIP]);
    sample->sp = reinterpret_cast<Address>(mcontext.gregs[REG_RSP]);
    sample->fp = reinterpret_cast<Address>(mcontext.gregs[REG_RBP]);
#elif V8_HOST_ARCH_ARM
// An undefined macro evaluates to 0, so this applies to Android's Bionic also.
#if !defined(ANDROID) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ <= 3))
    sample->pc = reinterpret_cast<Address>(mcontext.gregs[R15]);
    sample->sp = reinterpret_cast<Address>(mcontext.gregs[R13]);
    sample->fp = reinterpret_cast<Address>(mcontext.gregs[R11]);
#ifdef ENABLE_ARM_LR_SAVING
    sample->lr = reinterpret_cast<Address>(mcontext.gregs[R14]);
#endif
#else
    sample->pc = reinterpret_cast<Address>(mcontext.arm_pc);
    sample->sp = reinterpret_cast<Address>(mcontext.arm_sp);
    sample->fp = reinterpret_cast<Address>(mcontext.arm_fp);
#ifdef ENABLE_ARM_LR_SAVING
    sample->lr = reinterpret_cast<Address>(mcontext.arm_lr);
#endif
#endif
#elif V8_HOST_ARCH_MIPS
    // Implement this on MIPS.
    UNIMPLEMENTED();
#endif
  }
#endif
  sample->threadProfile = sCurrentThreadProfile;
  sample->timestamp = mozilla::TimeStamp::Now();

  Sampler::GetActiveSampler()->Tick(sample);

  sCurrentThreadProfile = NULL;
}

int tgkill(pid_t tgid, pid_t tid, int signalno) {
  return syscall(SYS_tgkill, tgid, tid, signalno);
}

class PlatformData : public Malloced {
 public:
  PlatformData()
  {}
};

/* static */ PlatformData*
Sampler::AllocPlatformData(int aThreadId)
{
  return new PlatformData;
}

/* static */ void
Sampler::FreePlatformData(PlatformData* aData)
{
  delete aData;
}

static void* SignalSender(void* arg) {
# if defined(ANDROID)
  // pthread_atfork isn't available on Android.
  void* initialize_atfork = NULL;
# else
  // This call is done just once, at the first call to SenderEntry.
  // It returns NULL.
  static void* initialize_atfork = setup_atfork();
# endif

  int vm_tgid_ = getpid();

  while (SamplerRegistry::sampler->IsActive()) {
    SamplerRegistry::sampler->HandleSaveRequest();

    if (!SamplerRegistry::sampler->IsPaused()) {
      mozilla::MutexAutoLock lock(*Sampler::sRegisteredThreadsMutex);
      std::vector<ThreadInfo*> threads =
        SamplerRegistry::sampler->GetRegisteredThreads();

      for (uint32_t i = 0; i < threads.size(); i++) {
        ThreadInfo* info = threads[i];

        // This will be null if we're not interested in profiling this thread.
        if (!info->Profile())
          continue;

        // We use sCurrentThreadProfile the ThreadProfile for the
        // thread we're profiling to the signal handler
        sCurrentThreadProfile = info->Profile();

        int threadId = info->ThreadId();

        if (tgkill(vm_tgid_, threadId, SIGPROF) != 0) {
          printf_stderr("profiler failed to signal tid=%d\n", threadId);
#ifdef DEBUG
          abort();
#endif
          continue;
        }

        // Wait for the signal handler to run before moving on to the next one
        while (sCurrentThreadProfile)
          sched_yield();
      }
    }

    // Convert ms to us and subtract 100 us to compensate delays
    // occuring during signal delivery.
    // TODO measure and confirm this.
    const useconds_t interval =
      SamplerRegistry::sampler->interval() * 1000 - 100;
    //int result = usleep(interval);
    usleep(interval);
  }
  return initialize_atfork; // which is guaranteed to be NULL
}

Sampler::Sampler(int interval, bool profiling, int entrySize)
    : interval_(interval),
      profiling_(profiling),
      paused_(false),
      active_(false),
      entrySize_(entrySize) {
}

Sampler::~Sampler() {
  ASSERT(!signal_sender_launched_);
}


void Sampler::Start() {
  LOG("Sampler started");

  SamplerRegistry::AddActiveSampler(this);

  // Request profiling signals.
  LOG("Request signal");
  struct sigaction sa;
  sa.sa_sigaction = ProfilerSignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_SIGINFO;
  if (sigaction(SIGPROF, &sa, &old_sigprof_signal_handler_) != 0) {
    LOG("Error installing signal");
    return;
  }

  // Request save profile signals
  struct sigaction sa2;
  sa2.sa_sigaction = ProfilerSaveSignalHandler;
  sigemptyset(&sa2.sa_mask);
  sa2.sa_flags = SA_RESTART | SA_SIGINFO;
  if (sigaction(SIGNAL_SAVE_PROFILE, &sa2, &old_sigsave_signal_handler_) != 0) {
    LOG("Error installing start signal");
    return;
  }
  LOG("Signal installed");
  signal_handler_installed_ = true;

  // Start a thread that sends SIGPROF signal to VM thread.
  // Sending the signal ourselves instead of relying on itimer provides
  // much better accuracy.
  SetActive(true);
  if (pthread_create(
        &signal_sender_thread_, NULL, SignalSender, NULL) == 0) {
    signal_sender_launched_ = true;
  }
  LOG("Profiler thread started");
}


void Sampler::Stop() {
  SetActive(false);

  // Wait for signal sender termination (it will exit after setting
  // active_ to false).
  if (signal_sender_launched_) {
    pthread_join(signal_sender_thread_, NULL);
    signal_sender_launched_ = false;
  }

  SamplerRegistry::RemoveActiveSampler(this);

  // Restore old signal handler
  if (signal_handler_installed_) {
    sigaction(SIGNAL_SAVE_PROFILE, &old_sigsave_signal_handler_, 0);
    sigaction(SIGPROF, &old_sigprof_signal_handler_, 0);
    signal_handler_installed_ = false;
  }
}

bool Sampler::RegisterCurrentThread(const char* aName,
                                    PseudoStack* aPseudoStack,
                                    bool aIsMainThread, void* stackTop)
{
  if (!Sampler::sRegisteredThreadsMutex)
    return false;

  mozilla::MutexAutoLock lock(*Sampler::sRegisteredThreadsMutex);

  ThreadInfo* info = new ThreadInfo(aName, gettid(),
    aIsMainThread, aPseudoStack);

  if (sActiveSampler) {
    sActiveSampler->RegisterThread(info);
  }

  sRegisteredThreads->push_back(info);

  uwt__register_thread_for_profiling(stackTop);
  return true;
}

void Sampler::UnregisterCurrentThread()
{
  if (!Sampler::sRegisteredThreadsMutex)
    return;

  mozilla::MutexAutoLock lock(*Sampler::sRegisteredThreadsMutex);

  int id = gettid();

  for (uint32_t i = 0; i < sRegisteredThreads->size(); i++) {
    ThreadInfo* info = sRegisteredThreads->at(i);
    if (info->ThreadId() == id) {
      delete info;
      sRegisteredThreads->erase(sRegisteredThreads->begin() + i);
      break;
    }
  }

  uwt__unregister_thread_for_profiling();
}

#ifdef ANDROID
static struct sigaction old_sigstart_signal_handler;
const int SIGSTART = SIGUSR1;

static void StartSignalHandler(int signal, siginfo_t* info, void* context) {
  profiler_start(PROFILE_DEFAULT_ENTRY, PROFILE_DEFAULT_INTERVAL,
                 PROFILE_DEFAULT_FEATURES, PROFILE_DEFAULT_FEATURE_COUNT,
                 NULL, 0);
}

void OS::RegisterStartHandler()
{
  LOG("Registering start signal");
  struct sigaction sa;
  sa.sa_sigaction = StartSignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_SIGINFO;
  if (sigaction(SIGSTART, &sa, &old_sigstart_signal_handler) != 0) {
    LOG("Error installing signal");
  }
}
#endif

