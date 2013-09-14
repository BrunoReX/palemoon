/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/nsMemoryInfoDumper.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/FileUtils.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/unused.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentChild.h"
#include "nsIConsoleService.h"
#include "nsICycleCollectorListener.h"
#include "nsDirectoryServiceDefs.h"
#include "nsGZFileWriter.h"
#include "nsJSEnvironment.h"
#include "nsPrintfCString.h"

#ifdef XP_WIN
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

#ifdef XP_LINUX
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#ifdef ANDROID
#include "android/log.h"
#endif

#ifdef LOG
#undef LOG
#endif

#ifdef ANDROID
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Gecko:MemoryInfoDumper", ## __VA_ARGS__)
#else
#define LOG(...)
#endif

using namespace mozilla;
using namespace mozilla::dom;

namespace {

class DumpMemoryInfoToTempDirRunnable : public nsRunnable
{
public:
  DumpMemoryInfoToTempDirRunnable(const nsAString& aIdentifier,
                                  bool aMinimizeMemoryUsage,
                                  bool aDumpChildProcesses)
      : mIdentifier(aIdentifier)
      , mMinimizeMemoryUsage(aMinimizeMemoryUsage)
      , mDumpChildProcesses(aDumpChildProcesses)
  {}

  NS_IMETHOD Run()
  {
    nsCOMPtr<nsIMemoryInfoDumper> dumper = do_GetService("@mozilla.org/memory-info-dumper;1");
    dumper->DumpMemoryInfoToTempDir(mIdentifier, mMinimizeMemoryUsage,
                                    mDumpChildProcesses);
    return NS_OK;
  }

private:
  const nsString mIdentifier;
  const bool mMinimizeMemoryUsage;
  const bool mDumpChildProcesses;
};

class GCAndCCLogDumpRunnable : public nsRunnable
{
public:
  GCAndCCLogDumpRunnable(const nsAString& aIdentifier,
                         bool aDumpChildProcesses)
    : mIdentifier(aIdentifier)
    , mDumpChildProcesses(aDumpChildProcesses)
  {}

  NS_IMETHOD Run()
  {
    nsCOMPtr<nsIMemoryInfoDumper> dumper = do_GetService("@mozilla.org/memory-info-dumper;1");
    dumper->DumpGCAndCCLogsToFile(
      mIdentifier, mDumpChildProcesses);
    return NS_OK;
  }

private:
  const nsString mIdentifier;
  const bool mDumpChildProcesses;
};

} // anonymous namespace

#ifdef XP_LINUX // {
namespace {

/*
 * The following code supports dumping about:memory upon receiving a signal.
 *
 * We listen for the following signals:
 *
 *  - SIGRTMIN:     Dump our memory reporters (and those of our child
 *                  processes),
 *  - SIGRTMIN + 1: Dump our memory reporters (and those of our child
 *                  processes) after minimizing memory usage, and
 *  - SIGRTMIN + 2: Dump the GC and CC logs in this and our child processes.
 *
 * When we receive one of these signals, we write the signal number to a pipe.
 * The IO thread then notices that the pipe has been written to, and kicks off
 * the appropriate task on the main thread.
 *
 * This scheme is similar to using signalfd(), except it's portable and it
 * doesn't require the use of sigprocmask, which is problematic because it
 * masks signals received by child processes.
 *
 * In theory, we could use Chromium's MessageLoopForIO::CatchSignal() for this.
 * But that uses libevent, which does not handle the realtime signals (bug
 * 794074).
 */

// It turns out that at least on some systems, SIGRTMIN is not a compile-time
// constant, so these have to be set at runtime.
static int sDumpAboutMemorySignum;         // SIGRTMIN
static int sDumpAboutMemoryAfterMMUSignum; // SIGRTMIN + 1
static int sGCAndCCDumpSignum;             // SIGRTMIN + 2

// This is the write-end of a pipe that we use to notice when a
// dump-about-memory signal occurs.
static int sDumpAboutMemoryPipeWriteFd = -1;

void
DumpAboutMemorySignalHandler(int aSignum)
{
  // This is a signal handler, so everything in here needs to be
  // async-signal-safe.  Be careful!

  if (sDumpAboutMemoryPipeWriteFd != -1) {
    uint8_t signum = static_cast<int>(aSignum);
    write(sDumpAboutMemoryPipeWriteFd, &signum, sizeof(signum));
  }
}

/**
 * Abstract base class for something which watches an fd and takes action when
 * we can read from it without blocking.
 */
class FdWatcher : public MessageLoopForIO::Watcher
                , public nsIObserver
{
protected:
  MessageLoopForIO::FileDescriptorWatcher mReadWatcher;
  int mFd;

public:
  FdWatcher()
    : mFd(-1)
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

  virtual ~FdWatcher()
  {
    // StopWatching should have run.
    MOZ_ASSERT(mFd == -1);
  }

  /**
   * Open the fd to watch.  If we encounter an error, return -1.
   */
  virtual int OpenFd() = 0;

  /**
   * Called when you can read() from the fd without blocking.  Note that this
   * function is also called when you're at eof (read() returns 0 in this case).
   */
  virtual void OnFileCanReadWithoutBlocking(int aFd) = 0;
  virtual void OnFileCanWriteWithoutBlocking(int Afd) {};

  NS_DECL_ISUPPORTS

  /**
   * Initialize this object.  This should be called right after the object is
   * constructed.  (This would go in the constructor, except we interact with
   * XPCOM, which we can't do from a constructor because our refcount is 0 at
   * that point.)
   */
  void Init()
  {
    MOZ_ASSERT(NS_IsMainThread());

    nsCOMPtr<nsIObserverService> os = services::GetObserverService();
    os->AddObserver(this, "xpcom-shutdown", /* ownsWeak = */ false);

    XRE_GetIOMessageLoop()->PostTask(
        FROM_HERE,
        NewRunnableMethod(this, &FdWatcher::StartWatching));
  }

  // Implementations may call this function multiple times if they ensure that
  // it's safe to call OpenFd() multiple times and they call StopWatching()
  // first.
  virtual void StartWatching()
  {
    MOZ_ASSERT(XRE_GetIOMessageLoop() == MessageLoopForIO::current());
    MOZ_ASSERT(mFd == -1);

    mFd = OpenFd();
    if (mFd == -1) {
      LOG("FdWatcher: OpenFd failed.");
      return;
    }

    MessageLoopForIO::current()->WatchFileDescriptor(
      mFd, /* persistent = */ true,
      MessageLoopForIO::WATCH_READ,
      &mReadWatcher, this);
  }

  // Since implementations can call StartWatching() multiple times, they can of
  // course call StopWatching() multiple times.
  virtual void StopWatching()
  {
    MOZ_ASSERT(XRE_GetIOMessageLoop() == MessageLoopForIO::current());

    mReadWatcher.StopWatchingFileDescriptor();
    if (mFd != -1) {
      close(mFd);
      mFd = -1;
    }
  }

  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const PRUnichar* aData)
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(!strcmp(aTopic, "xpcom-shutdown"));

    XRE_GetIOMessageLoop()->PostTask(
        FROM_HERE,
        NewRunnableMethod(this, &FdWatcher::StopWatching));

    return NS_OK;
  }
};

NS_IMPL_THREADSAFE_ISUPPORTS1(FdWatcher, nsIObserver);

class SignalPipeWatcher : public FdWatcher
{
public:
  static void Create()
  {
    nsRefPtr<SignalPipeWatcher> sw = new SignalPipeWatcher();
    sw->Init();
  }

  virtual ~SignalPipeWatcher()
  {
    MOZ_ASSERT(sDumpAboutMemoryPipeWriteFd == -1);
  }

  virtual int OpenFd()
  {
    MOZ_ASSERT(XRE_GetIOMessageLoop() == MessageLoopForIO::current());

    sDumpAboutMemorySignum = SIGRTMIN;
    sDumpAboutMemoryAfterMMUSignum = SIGRTMIN + 1;
    sGCAndCCDumpSignum = SIGRTMIN + 2;

    // Create a pipe.  When we receive a signal in our signal handler, we'll
    // write the signum to the write-end of this pipe.
    int pipeFds[2];
    if (pipe(pipeFds)) {
      LOG("SignalPipeWatcher failed to create pipe.");
      return -1;
    }

    // Close this pipe on calls to exec().
    fcntl(pipeFds[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipeFds[1], F_SETFD, FD_CLOEXEC);

    int readFd = pipeFds[0];
    sDumpAboutMemoryPipeWriteFd = pipeFds[1];

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_handler = DumpAboutMemorySignalHandler;

    if (sigaction(sDumpAboutMemorySignum, &action, nullptr)) {
      LOG("SignalPipeWatcher failed to register about:memory "
          "dump signal handler.");
    }
    if (sigaction(sDumpAboutMemoryAfterMMUSignum, &action, nullptr)) {
      LOG("SignalPipeWatcher failed to register about:memory "
          "dump after MMU signal handler.");
    }
    if (sigaction(sGCAndCCDumpSignum, &action, nullptr)) {
      LOG("Failed to register GC+CC dump signal handler.");
    }

    return readFd;
  }

  virtual void StopWatching()
  {
    MOZ_ASSERT(XRE_GetIOMessageLoop() == MessageLoopForIO::current());

    // Close sDumpAboutMemoryPipeWriteFd /after/ setting the fd to -1.
    // Otherwise we have the (admittedly far-fetched) race where we
    //
    //  1) close sDumpAboutMemoryPipeWriteFd
    //  2) open a new fd with the same number as sDumpAboutMemoryPipeWriteFd
    //     had.
    //  3) receive a signal, then write to the fd.
    int pipeWriteFd = sDumpAboutMemoryPipeWriteFd;
    PR_ATOMIC_SET(&sDumpAboutMemoryPipeWriteFd, -1);
    close(pipeWriteFd);

    FdWatcher::StopWatching();
  }

  virtual void OnFileCanReadWithoutBlocking(int aFd)
  {
    MOZ_ASSERT(XRE_GetIOMessageLoop() == MessageLoopForIO::current());

    uint8_t signum;
    ssize_t numReceived = read(aFd, &signum, sizeof(signum));
    if (numReceived != sizeof(signum)) {
      LOG("Error reading from buffer in "
          "SignalPipeWatcher::OnFileCanReadWithoutBlocking.");
      return;
    }

    if (signum == sDumpAboutMemorySignum ||
        signum == sDumpAboutMemoryAfterMMUSignum) {
      // Dump our memory reports (but run this on the main thread!).
      bool doMMUFirst = signum == sDumpAboutMemoryAfterMMUSignum;
      nsRefPtr<DumpMemoryInfoToTempDirRunnable> runnable =
        new DumpMemoryInfoToTempDirRunnable(/* identifier = */ EmptyString(),
                                            doMMUFirst,
                                            /* dumpChildProcesses = */ true);
      NS_DispatchToMainThread(runnable);
    }
    else if (signum == sGCAndCCDumpSignum) {
      // Dump GC and CC logs (from the main thread).
      nsRefPtr<GCAndCCLogDumpRunnable> runnable =
        new GCAndCCLogDumpRunnable(
            /* identifier = */ EmptyString(),
            /* dumpChildProcesses = */ true);
      NS_DispatchToMainThread(runnable);
    }
    else {
      LOG("SignalPipeWatcher got unexpected signum.");
    }
  }
};

class FifoWatcher : public FdWatcher
{
public:
  static void MaybeCreate()
  {
    MOZ_ASSERT(NS_IsMainThread());

    if (XRE_GetProcessType() != GeckoProcessType_Default) {
      // We want this to be main-process only, since two processes can't listen
      // to the same fifo.
      return;
    }

    if (!Preferences::GetBool("memory_info_dumper.watch_fifo.enabled", false)) {
      LOG("Fifo watcher disabled via pref.");
      return;
    }

    // The FifoWatcher is held alive by the observer service.
    nsRefPtr<FifoWatcher> fw = new FifoWatcher();
    fw->Init();
  }

  virtual int OpenFd()
  {
    // If the memory_info_dumper.directory pref is specified, put the fifo
    // there.  Otherwise, put it into the system's tmp directory.

    nsCOMPtr<nsIFile> file;
    nsAutoCString dirPath;
    nsresult rv = Preferences::GetCString(
      "memory_info_dumper.watch_fifo.directory", &dirPath);

    if (NS_SUCCEEDED(rv)) {
      rv = XRE_GetFileFromPath(dirPath.get(), getter_AddRefs(file));
      if (NS_FAILED(rv)) {
        LOG("FifoWatcher failed to open file \"%s\"", dirPath.get());
        return -1;
      }
    } else {
      rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(file));
      NS_ENSURE_SUCCESS(rv, -1);
    }

    rv = file->AppendNative(NS_LITERAL_CSTRING("debug_info_trigger"));
    NS_ENSURE_SUCCESS(rv, -1);

    nsAutoCString path;
    rv = file->GetNativePath(path);
    NS_ENSURE_SUCCESS(rv, -1);

    // unlink might fail because the file doesn't exist, or for other reasons.
    // But we don't care it fails; any problems will be detected later, when we
    // try to mkfifo or open the file.
    if (unlink(path.get())) {
      LOG("FifoWatcher::OpenFifo unlink failed; errno=%d.  "
          "Continuing despite error.", errno);
    }

    if (mkfifo(path.get(), 0766)) {
      LOG("FifoWatcher::OpenFifo mkfifo failed; errno=%d", errno);
      return -1;
    }

#ifdef ANDROID
    // Android runs with a umask, so we need to chmod our fifo to make it
    // world-writable.
    chmod(path.get(), 0666);
#endif

    int fd;
    do {
      // The fifo will block until someone else has written to it.  In
      // particular, open() will block until someone else has opened it for
      // writing!  We want open() to succeed and read() to block, so we open
      // with NONBLOCK and then fcntl that away.
      fd = open(path.get(), O_RDONLY | O_NONBLOCK);
    } while (fd == -1 && errno == EINTR);

    if (fd == -1) {
      LOG("FifoWatcher::OpenFifo open failed; errno=%d", errno);
      return -1;
    }

    // Make fd blocking now that we've opened it.
    if (fcntl(fd, F_SETFL, 0)) {
      close(fd);
      return -1;
    }

    return fd;
  }

  virtual void OnFileCanReadWithoutBlocking(int aFd)
  {
    MOZ_ASSERT(XRE_GetIOMessageLoop() == MessageLoopForIO::current());

    char buf[1024];
    int nread;
    do {
      // sizeof(buf) - 1 to leave space for the null-terminator.
      nread = read(aFd, buf, sizeof(buf));
    } while(nread == -1 && errno == EINTR);

    if (nread == -1) {
      // We want to avoid getting into a situation where
      // OnFileCanReadWithoutBlocking is called in an infinite loop, so when
      // something goes wrong, stop watching the fifo altogether.
      LOG("FifoWatcher hit an error (%d) and is quitting.", errno);
      StopWatching();
      return;
    }

    if (nread == 0) {
      // If we get EOF, that means that the other side closed the fifo.  We need
      // to close and re-open the fifo; if we don't,
      // OnFileCanWriteWithoutBlocking will be called in an infinite loop.

      LOG("FifoWatcher closing and re-opening fifo.");
      StopWatching();
      StartWatching();
      return;
    }

    nsAutoCString inputStr;
    inputStr.Append(buf, nread);

    // Trimming whitespace is important because if you do
    //   |echo "foo" >> debug_info_trigger|,
    // it'll actually write "foo\n" to the fifo.
    inputStr.Trim("\b\t\r\n");

    bool doMemoryReport = inputStr == NS_LITERAL_CSTRING("memory report");
    bool doMMUMemoryReport = inputStr == NS_LITERAL_CSTRING("minimize memory report");
    bool doGCCCDump = inputStr == NS_LITERAL_CSTRING("gc log");

    if (doMemoryReport || doMMUMemoryReport) {
      LOG("FifoWatcher dispatching memory report runnable.");
      nsRefPtr<DumpMemoryInfoToTempDirRunnable> runnable =
        new DumpMemoryInfoToTempDirRunnable(/* identifier = */ EmptyString(),
                                            doMMUMemoryReport,
                                            /* dumpChildProcesses = */ true);
      NS_DispatchToMainThread(runnable);
    } else if (doGCCCDump) {
      LOG("FifoWatcher dispatching GC/CC log runnable.");
      nsRefPtr<GCAndCCLogDumpRunnable> runnable =
        new GCAndCCLogDumpRunnable(
            /* identifier = */ EmptyString(),
            /* dumpChildProcesses = */ true);
      NS_DispatchToMainThread(runnable);
    } else {
      LOG("Got unexpected value from fifo; ignoring it.");
    }
  }
};

} // anonymous namespace
#endif // XP_LINUX }

NS_IMPL_ISUPPORTS1(nsMemoryInfoDumper, nsIMemoryInfoDumper)

nsMemoryInfoDumper::nsMemoryInfoDumper()
{
}

nsMemoryInfoDumper::~nsMemoryInfoDumper()
{
}

/* static */ void
nsMemoryInfoDumper::Initialize()
{
#ifdef XP_LINUX
  SignalPipeWatcher::Create();
  FifoWatcher::MaybeCreate();
#endif
}

static void
EnsureNonEmptyIdentifier(nsAString& aIdentifier)
{
  if (!aIdentifier.IsEmpty()) {
    return;
  }

  // If the identifier is empty, set it to the number of whole seconds since the
  // epoch.  This identifier will appear in the files that this process
  // generates and also the files generated by this process's children, allowing
  // us to identify which files are from the same memory report request.
  aIdentifier.AppendInt(static_cast<int64_t>(PR_Now()) / 1000000);
}

NS_IMETHODIMP
nsMemoryInfoDumper::DumpGCAndCCLogsToFile(
  const nsAString& aIdentifier,
  bool aDumpChildProcesses)
{
  nsString identifier(aIdentifier);
  EnsureNonEmptyIdentifier(identifier);

  if (aDumpChildProcesses) {
    nsTArray<ContentParent*> children;
    ContentParent::GetAll(children);
    for (uint32_t i = 0; i < children.Length(); i++) {
      unused << children[i]->SendDumpGCAndCCLogsToFile(
          identifier, aDumpChildProcesses);
    }
  }

  nsCOMPtr<nsICycleCollectorListener> logger =
    do_CreateInstance("@mozilla.org/cycle-collector-logger;1");
  logger->SetFilenameIdentifier(identifier);

  nsJSContext::CycleCollectNow(logger);
  return NS_OK;
}

namespace mozilla {

#define DUMP(o, s) \
  do { \
    nsresult rv = (o)->Write(s); \
    NS_ENSURE_SUCCESS(rv, rv); \
  } while (0)

static nsresult
DumpReport(nsIGZFileWriter *aWriter, bool aIsFirst,
  const nsACString &aProcess, const nsACString &aPath, int32_t aKind,
  int32_t aUnits, int64_t aAmount, const nsACString &aDescription)
{
  DUMP(aWriter, aIsFirst ? "[" : ",");

  // We only want to dump reports for this process.  If |aProcess| is
  // non-NULL that means we've received it from another process in response
  // to a "child-memory-reporter-request" event;  ignore such reports.
  if (!aProcess.IsEmpty()) {
    return NS_OK;
  }

  // Generate the process identifier, which is of the form "$PROCESS_NAME
  // (pid $PID)", or just "(pid $PID)" if we don't have a process name.  If
  // we're the main process, we let $PROCESS_NAME be "Main Process".
  nsAutoCString processId;
  if (XRE_GetProcessType() == GeckoProcessType_Default) {
    // We're the main process.
    processId.AssignLiteral("Main Process ");
  } else if (ContentChild *cc = ContentChild::GetSingleton()) {
    // Try to get the process name from ContentChild.
    nsAutoString processName;
    cc->GetProcessName(processName);
    processId.Assign(NS_ConvertUTF16toUTF8(processName));
    if (!processId.IsEmpty()) {
      processId.AppendLiteral(" ");
    }
  }

  // Add the PID to the identifier.
  unsigned pid = getpid();
  processId.Append(nsPrintfCString("(pid %u)", pid));

  DUMP(aWriter, "\n    {\"process\": \"");
  DUMP(aWriter, processId);

  DUMP(aWriter, "\", \"path\": \"");
  nsCString path(aPath);
  path.ReplaceSubstring("\\", "\\\\");    /* <backslash> --> \\ */
  path.ReplaceSubstring("\"", "\\\"");    // " --> \"
  DUMP(aWriter, path);

  DUMP(aWriter, "\", \"kind\": ");
  DUMP(aWriter, nsPrintfCString("%d", aKind));

  DUMP(aWriter, ", \"units\": ");
  DUMP(aWriter, nsPrintfCString("%d", aUnits));

  DUMP(aWriter, ", \"amount\": ");
  DUMP(aWriter, nsPrintfCString("%lld", aAmount));

  nsCString description(aDescription);
  description.ReplaceSubstring("\\", "\\\\");    /* <backslash> --> \\ */
  description.ReplaceSubstring("\"", "\\\"");    // " --> \"
  description.ReplaceSubstring("\n", "\\n");     // <newline> --> \n
  DUMP(aWriter, ", \"description\": \"");
  DUMP(aWriter, description);
  DUMP(aWriter, "\"}");

  return NS_OK;
}

class DumpMultiReporterCallback MOZ_FINAL : public nsIMemoryMultiReporterCallback
{
  public:
    NS_DECL_ISUPPORTS

      NS_IMETHOD Callback(const nsACString &aProcess, const nsACString &aPath,
          int32_t aKind, int32_t aUnits, int64_t aAmount,
          const nsACString &aDescription,
          nsISupports *aData)
      {
        nsCOMPtr<nsIGZFileWriter> writer = do_QueryInterface(aData);
        NS_ENSURE_TRUE(writer, NS_ERROR_FAILURE);

        // The |isFirst = false| assumes that at least one single reporter is
        // present and so will have been processed in
        // DumpProcessMemoryReportsToGZFileWriter() below.
        return DumpReport(writer, /* isFirst = */ false, aProcess, aPath,
            aKind, aUnits, aAmount, aDescription);
        return NS_OK;
      }
};

NS_IMPL_ISUPPORTS1(
    DumpMultiReporterCallback
    , nsIMemoryMultiReporterCallback
    )

} // namespace mozilla

static void
MakeFilename(const char *aPrefix, const nsAString &aIdentifier,
             const char *aSuffix, nsACString &aResult)
{
  aResult = nsPrintfCString("%s-%s-%d.%s",
                            aPrefix,
                            NS_ConvertUTF16toUTF8(aIdentifier).get(),
                            getpid(), aSuffix);
}

/* static */ nsresult
nsMemoryInfoDumper::OpenTempFile(const nsACString &aFilename, nsIFile* *aFile)
{
#ifdef ANDROID
  // For Android, first try the downloads directory which is world-readable
  // rather than the temp directory which is not.
  if (!*aFile) {
    char *env = PR_GetEnv("DOWNLOADS_DIRECTORY");
    if (env) {
      NS_NewNativeLocalFile(nsCString(env), /* followLinks = */ true, aFile);
    }
  }
#endif
  nsresult rv;
  if (!*aFile) {
    rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, aFile);
    NS_ENSURE_SUCCESS(rv, rv);
  }

#ifdef ANDROID
  // /data/local/tmp is a true tmp directory; anyone can create a file there,
  // but only the user which created the file can remove it.  We want non-root
  // users to be able to remove these files, so we write them into a
  // subdirectory of the temp directory and chmod 777 that directory.
  rv = (*aFile)->AppendNative(NS_LITERAL_CSTRING("memory-reports"));
  NS_ENSURE_SUCCESS(rv, rv);

  // It's OK if this fails; that probably just means that the directory already
  // exists.
  (*aFile)->Create(nsIFile::DIRECTORY_TYPE, 0777);

  nsAutoCString dirPath;
  rv = (*aFile)->GetNativePath(dirPath);
  NS_ENSURE_SUCCESS(rv, rv);

  while (chmod(dirPath.get(), 0777) == -1 && errno == EINTR) {}
#endif

  nsCOMPtr<nsIFile> file(*aFile);

  rv = file->AppendNative(aFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = file->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0666);
  NS_ENSURE_SUCCESS(rv, rv);

#ifdef ANDROID
    // Make this file world-read/writable; the permissions passed to the
    // CreateUnique call above are not sufficient on Android, which runs with a
    // umask.
    nsAutoCString path;
    rv = file->GetNativePath(path);
    NS_ENSURE_SUCCESS(rv, rv);

    while (chmod(path.get(), 0666) == -1 && errno == EINTR) {}
#endif

  return NS_OK;
}

#ifdef MOZ_DMD
struct DMDWriteState
{
  static const size_t kBufSize = 4096;
  char mBuf[kBufSize];
  nsRefPtr<nsGZFileWriter> mGZWriter;

  DMDWriteState(nsGZFileWriter *aGZWriter)
    : mGZWriter(aGZWriter)
  {}
};

static void
DMDWrite(void* aState, const char* aFmt, va_list ap)
{
  DMDWriteState *state = (DMDWriteState*)aState;
  vsnprintf(state->mBuf, state->kBufSize, aFmt, ap);
  unused << state->mGZWriter->Write(state->mBuf);
}
#endif

static nsresult
DumpProcessMemoryReportsToGZFileWriter(nsIGZFileWriter *aWriter)
{
  nsresult rv;

  // Increment this number if the format changes.
  //
  // This is the first write to the file, and it causes |aWriter| to allocate
  // over 200 KiB of memory.
  //
  DUMP(aWriter, "{\n  \"version\": 1,\n");

  DUMP(aWriter, "  \"hasMozMallocUsableSize\": ");

  nsCOMPtr<nsIMemoryReporterManager> mgr =
    do_GetService("@mozilla.org/memory-reporter-manager;1");
  NS_ENSURE_STATE(mgr);

  DUMP(aWriter, mgr->GetHasMozMallocUsableSize() ? "true" : "false");
  DUMP(aWriter, ",\n");
  DUMP(aWriter, "  \"reports\": ");

  // Process single reporters.
  bool isFirst = true;
  bool more;
  nsCOMPtr<nsISimpleEnumerator> e;
  mgr->EnumerateReporters(getter_AddRefs(e));
  while (NS_SUCCEEDED(e->HasMoreElements(&more)) && more) {
    nsCOMPtr<nsIMemoryReporter> r;
    e->GetNext(getter_AddRefs(r));

    nsCString process;
    rv = r->GetProcess(process);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCString path;
    rv = r->GetPath(path);
    NS_ENSURE_SUCCESS(rv, rv);

    int32_t kind;
    rv = r->GetKind(&kind);
    NS_ENSURE_SUCCESS(rv, rv);

    int32_t units;
    rv = r->GetUnits(&units);
    NS_ENSURE_SUCCESS(rv, rv);

    int64_t amount;
    rv = r->GetAmount(&amount);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCString description;
    rv = r->GetDescription(description);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = DumpReport(aWriter, isFirst, process, path, kind, units, amount,
                    description);
    NS_ENSURE_SUCCESS(rv, rv);

    isFirst = false;
  }

  // Process multi-reporters.
  nsCOMPtr<nsISimpleEnumerator> e2;
  mgr->EnumerateMultiReporters(getter_AddRefs(e2));
  nsRefPtr<DumpMultiReporterCallback> cb = new DumpMultiReporterCallback();
  while (NS_SUCCEEDED(e2->HasMoreElements(&more)) && more) {
    nsCOMPtr<nsIMemoryMultiReporter> r;
    e2->GetNext(getter_AddRefs(r));
    r->CollectReports(cb, aWriter);
  }

  DUMP(aWriter, "\n  ]\n}\n");

  return NS_OK;
}

nsresult
DumpProcessMemoryInfoToTempDir(const nsAString& aIdentifier)
{
  MOZ_ASSERT(!aIdentifier.IsEmpty());

#ifdef MOZ_DMD
  // Clear DMD's reportedness state before running the memory reporters, to
  // avoid spurious twice-reported warnings.
  dmd::ClearReports();
#endif

  // Open a new file named something like
  //
  //   incomplete-memory-report-<identifier>-<pid>.json.gz
  //
  // in NS_OS_TEMP_DIR for writing.  When we're finished writing the report,
  // we'll rename this file and get rid of the "incomplete-" prefix.
  //
  // We do this because we don't want scripts which poll the filesystem
  // looking for memory report dumps to grab a file before we're finished
  // writing to it.

  // Note that |mrFilename| is missing the "incomplete-" prefix; we'll tack
  // that on in a moment.
  nsCString mrFilename;
  MakeFilename("memory-report", aIdentifier, "json.gz", mrFilename);

  nsCOMPtr<nsIFile> mrTmpFile;
  nsresult rv;
  rv = nsMemoryInfoDumper::OpenTempFile(NS_LITERAL_CSTRING("incomplete-") +
                                        mrFilename,
                                        getter_AddRefs(mrTmpFile));
  NS_ENSURE_SUCCESS(rv, rv);

  nsRefPtr<nsGZFileWriter> mrWriter = new nsGZFileWriter();
  rv = mrWriter->Init(mrTmpFile);
  NS_ENSURE_SUCCESS(rv, rv);

  // Dump the memory reports to the file.
  DumpProcessMemoryReportsToGZFileWriter(mrWriter);

#ifdef MOZ_DMD
  // Create a filename like dmd-<identifier>-<pid>.txt.gz, which will be used
  // if DMD is enabled.
  nsCString dmdFilename;
  MakeFilename("dmd", aIdentifier, "txt.gz", dmdFilename);

  // Open a new DMD file named |dmdFilename| in NS_OS_TEMP_DIR for writing,
  // and dump DMD output to it.  This must occur after the memory reporters
  // have been run (above), but before the memory-reports file has been
  // renamed (so scripts can detect the DMD file, if present).

  nsCOMPtr<nsIFile> dmdFile;
  rv = nsMemoryInfoDumper::OpenTempFile(dmdFilename, getter_AddRefs(dmdFile));
  NS_ENSURE_SUCCESS(rv, rv);

  nsRefPtr<nsGZFileWriter> dmdWriter = new nsGZFileWriter();
  rv = dmdWriter->Init(dmdFile);
  NS_ENSURE_SUCCESS(rv, rv);

  // Dump DMD output to the file.

  DMDWriteState state(dmdWriter);
  dmd::Writer w(DMDWrite, &state);
  dmd::Dump(w);

  rv = dmdWriter->Finish();
  NS_ENSURE_SUCCESS(rv, rv);
#endif  // MOZ_DMD

  // The call to Finish() deallocates the memory allocated by mrWriter's first
  // DUMP() call (within DumpProcessMemoryReportsToGZFileWriter()).  Because
  // that memory was live while the memory reporters ran and thus measured by
  // them -- by "heap-allocated" if nothing else -- we want DMD to see it as
  // well.  So we deliberately don't call Finish() until after DMD finishes.
  rv = mrWriter->Finish();
  NS_ENSURE_SUCCESS(rv, rv);

  // Rename the memory reports file, now that we're done writing all the files.
  // Its final name is "memory-report<-identifier>-<pid>.json.gz".

  nsCOMPtr<nsIFile> mrFinalFile;
  rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR, getter_AddRefs(mrFinalFile));
  NS_ENSURE_SUCCESS(rv, rv);

#ifdef ANDROID
  rv = mrFinalFile->AppendNative(NS_LITERAL_CSTRING("memory-reports"));
  NS_ENSURE_SUCCESS(rv, rv);
#endif

  rv = mrFinalFile->AppendNative(mrFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mrFinalFile->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0600);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString mrActualFinalFilename;
  rv = mrFinalFile->GetLeafName(mrActualFinalFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mrTmpFile->MoveTo(/* directory */ nullptr, mrActualFinalFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  // Write a message to the console.

  nsCOMPtr<nsIConsoleService> cs =
    do_GetService(NS_CONSOLESERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString path;
  mrTmpFile->GetPath(path);
  NS_ENSURE_SUCCESS(rv, rv);

  nsString msg = NS_LITERAL_STRING(
    "nsIMemoryInfoDumper dumped reports to ");
  msg.Append(path);
  return cs->LogStringMessage(msg.get());
}

NS_IMETHODIMP
nsMemoryInfoDumper::DumpMemoryInfoToTempDir(const nsAString& aIdentifier,
                                            bool aMinimizeMemoryUsage,
                                            bool aDumpChildProcesses)
{
  nsString identifier(aIdentifier);
  EnsureNonEmptyIdentifier(identifier);

  // Kick off memory report dumps in our child processes, if applicable.  We
  // do this before doing our own report because writing a report may be I/O
  // bound, in which case we want to busy the CPU with other reports while we
  // work on our own.
  if (aDumpChildProcesses) {
    nsTArray<ContentParent*> children;
    ContentParent::GetAll(children);
    for (uint32_t i = 0; i < children.Length(); i++) {
      unused << children[i]->SendDumpMemoryInfoToTempDir(
          identifier, aMinimizeMemoryUsage, aDumpChildProcesses);
    }
  }

  if (aMinimizeMemoryUsage) {
    // Minimize memory usage, then run DumpMemoryInfoToTempDir again.
    nsRefPtr<DumpMemoryInfoToTempDirRunnable> callback =
      new DumpMemoryInfoToTempDirRunnable(identifier,
                                          /* minimizeMemoryUsage = */ false,
                                          /* dumpChildProcesses = */ false);
    nsCOMPtr<nsIMemoryReporterManager> mgr =
      do_GetService("@mozilla.org/memory-reporter-manager;1");
    NS_ENSURE_TRUE(mgr, NS_ERROR_FAILURE);
    nsCOMPtr<nsICancelableRunnable> runnable;
    mgr->MinimizeMemoryUsage(callback, getter_AddRefs(runnable));
    return NS_OK;
  }

  return DumpProcessMemoryInfoToTempDir(identifier);
}

NS_IMETHODIMP
nsMemoryInfoDumper::DumpMemoryReportsToNamedFile(const nsAString& aFilename)
{
  MOZ_ASSERT(!aFilename.IsEmpty());

  // Create the file.

  nsCOMPtr<nsIFile> mrFile;
  nsresult rv = NS_NewLocalFile(aFilename, false, getter_AddRefs(mrFile));
  NS_ENSURE_SUCCESS(rv, rv);

  mrFile->InitWithPath(aFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  bool exists;
  rv = mrFile->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!exists) {
    rv = mrFile->Create(nsIFile::NORMAL_FILE_TYPE, 0644);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Write the memory reports to the file.

  nsRefPtr<nsGZFileWriter> mrWriter = new nsGZFileWriter();
  rv = mrWriter->Init(mrFile);
  NS_ENSURE_SUCCESS(rv, rv);

  DumpProcessMemoryReportsToGZFileWriter(mrWriter);

  rv = mrWriter->Finish();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

#undef DUMP
