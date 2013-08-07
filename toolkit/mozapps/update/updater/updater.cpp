/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Application Update.
 *
 * The Initial Developer of the Original Code is
 * Benjamin Smedberg <benjamin@smedbergs.us>
 *
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Darin Fisher <darin@meer.net>
 *  Robert Strong <robert.bugzilla@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/**
 *  Manifest Format
 *  ---------------
 *
 *  contents = 1*( line )
 *  line     = method LWS *( param LWS ) CRLF
 *  CRLF     = "\r\n"
 *  LWS      = 1*( " " | "\t" )
 *
 *  Available methods for the different manifest files:
 *
 *  update.manifest
 *  ---------------
 *  method   = "add" | "add-if" | "patch" | "patch-if" | "remove"
 *
 *  updatev2.manifest
 *  -----------------
 *  method   = "add" | "add-if" | "patch" | "patch-if" | "remove" |
 *             "rmdir" | "rmrfdir" | type
 *
 *  'type' is the update type (e.g. complete or partial) and when present MUST
 *  be the first entry in the update manifest. The type is used to support
 *  downgrades by causing the actions defined in precomplete to be performed.
 *
 *  precomplete
 *  -----------
 *  method   = "remove" | "rmdir"
 */
#include "bspatch.h"
#include "progressui.h"
#include "archivereader.h"
#include "readstrings.h"
#include "errors.h"
#include "bzlib.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>

#include "updatelogging.h"

// Amount of the progress bar to use in each of the 3 update stages,
// should total 100.0.
#define PROGRESS_PREPARE_SIZE 20.0f
#define PROGRESS_EXECUTE_SIZE 75.0f
#define PROGRESS_FINISH_SIZE   5.0f

#if defined(XP_MACOSX)
// These functions are defined in launchchild_osx.mm
void LaunchChild(int argc, char **argv);
void LaunchMacPostProcess(const char* aAppExe);
#endif

#ifndef _O_BINARY
# define _O_BINARY 0
#endif

#ifndef NULL
# define NULL (0)
#endif

#ifndef SSIZE_MAX
# define SSIZE_MAX LONG_MAX
#endif

// We want to use execv to invoke the callback executable on platforms where
// we were launched using execv.  See nsUpdateDriver.cpp.
#if defined(XP_UNIX) && !defined(XP_MACOSX)
#define USE_EXECV
#endif

#ifdef XP_WIN
#include "updatehelper.h"

// Closes the handle if valid and if the updater is elevated returns with the
// return code specified. This prevents multiple launches of the callback
// application by preventing the elevated process from launching the callback.
#define EXIT_WHEN_ELEVATED(path, handle, retCode) \
  { \
      if (handle != INVALID_HANDLE_VALUE) { \
        CloseHandle(handle); \
      } \
      if (_waccess(path, F_OK) == 0 && NS_tremove(path) != 0) { \
        return retCode; \
      } \
  }
#endif

//-----------------------------------------------------------------------------

// This variable lives in libbz2.  It's declared in bzlib_private.h, so we just
// declare it here to avoid including that entire header file.
#if (__GNUC__ >= 4) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)
extern "C"  __attribute__((visibility("default"))) unsigned int BZ2_crc32Table[256];
#elif defined(__SUNPRO_C) || defined(__SUNPRO_CC)
extern "C" __global unsigned int BZ2_crc32Table[256];
#else
extern "C" unsigned int BZ2_crc32Table[256];
#endif

static unsigned int
crc32(const unsigned char *buf, unsigned int len)
{
  unsigned int crc = 0xffffffffL;

  const unsigned char *end = buf + len;
  for (; buf != end; ++buf)
    crc = (crc << 8) ^ BZ2_crc32Table[(crc >> 24) ^ *buf];

  crc = ~crc;
  return crc;
}

//-----------------------------------------------------------------------------

// A simple stack based container for a FILE struct that closes the
// file descriptor from its destructor.
class AutoFile
{
public:
  AutoFile(FILE* file = NULL)
    : mFile(file) {
  }

  ~AutoFile() {
    if (mFile != NULL)
      fclose(mFile);
  }

  AutoFile &operator=(FILE* file) {
    if (mFile != 0)
      fclose(mFile);
    mFile = file;
    return *this;
  }

  operator FILE*() {
    return mFile;
  }

  FILE* get() {
    return mFile;
  }

private:
  FILE* mFile;
};

struct MARChannelStringTable {
  MARChannelStringTable() 
  {
    MARChannelID[0] = '\0';
  }

  char MARChannelID[MAX_TEXT_LEN];
};

//-----------------------------------------------------------------------------

typedef void (* ThreadFunc)(void *param);

#ifdef XP_WIN
#include <process.h>

class Thread
{
public:
  int Run(ThreadFunc func, void *param)
  {
    mThreadFunc = func;
    mThreadParam = param;

    unsigned int threadID;

    mThread = (HANDLE) _beginthreadex(NULL, 0, ThreadMain, this, 0, &threadID);

    return mThread ? 0 : -1;
  }
  int Join()
  {
    WaitForSingleObject(mThread, INFINITE);
    CloseHandle(mThread);
    return 0;
  }
private:
  static unsigned __stdcall ThreadMain(void *p)
  {
    Thread *self = (Thread *) p;
    self->mThreadFunc(self->mThreadParam);
    return 0;
  }
  HANDLE     mThread;
  ThreadFunc mThreadFunc;
  void      *mThreadParam;
};

#elif defined(XP_UNIX)
#include <pthread.h>

class Thread
{
public:
  int Run(ThreadFunc func, void *param)
  {
    return pthread_create(&thr, NULL, (void* (*)(void *)) func, param);
  }
  int Join()
  {
    void *result;
    return pthread_join(thr, &result);
  }
private:
  pthread_t thr;
};

#elif defined(XP_OS2)

class Thread
{
public:
  int Run(ThreadFunc func, void *param)
  {
    mThreadFunc = func;
    mThreadParam = param;

    mThread = _beginthread(ThreadMain, NULL, 16384, (void *)this);

    return mThread ? 0 : -1;
  }
  int Join()
  {
    int status;
    waitpid(mThread, &status, 0);
    return 0;
  }
private:
  static void ThreadMain(void *p)
  {
    Thread *self = (Thread *) p;
    self->mThreadFunc(self->mThreadParam);
  }
  int        mThread;
  ThreadFunc mThreadFunc;
  void      *mThreadParam;
};

#else
#error "Unsupported platform"
#endif

//-----------------------------------------------------------------------------

static NS_tchar* gSourcePath;
static ArchiveReader gArchiveReader;
static bool gSucceeded = false;

#ifdef XP_WIN
// The current working directory specified in the command line.
static NS_tchar* gDestPath;
static NS_tchar gCallbackRelPath[MAXPATHLEN];
static NS_tchar gCallbackBackupPath[MAXPATHLEN];
#endif

static const NS_tchar kWhitespace[] = NS_T(" \t");
static const NS_tchar kNL[] = NS_T("\r\n");
static const NS_tchar kQuote[] = NS_T("\"");

static inline size_t
mmin(size_t a, size_t b)
{
  return (a > b) ? b : a;
}

static NS_tchar*
mstrtok(const NS_tchar *delims, NS_tchar **str)
{
  if (!*str || !**str)
    return NULL;

  // skip leading "whitespace"
  NS_tchar *ret = *str;
  const NS_tchar *d;
  do {
    for (d = delims; *d != NS_T('\0'); ++d) {
      if (*ret == *d) {
        ++ret;
        break;
      }
    }
  } while (*d);

  if (!*ret) {
    *str = ret;
    return NULL;
  }

  NS_tchar *i = ret;
  do {
    for (d = delims; *d != NS_T('\0'); ++d) {
      if (*i == *d) {
        *i = NS_T('\0');
        *str = ++i;
        return ret;
      }
    }
    ++i;
  } while (*i);

  *str = NULL;
  return ret;
}

#ifdef XP_WIN
/**
 * Coverts a relative update path to a full path for Windows.
 *
 * @param  relpath
 *         The relative path to convert to a full path.
 * @return valid filesystem full path or NULL memory allocation fails.
 */
static NS_tchar*
get_full_path(const NS_tchar *relpath)
{
  size_t lendestpath = NS_tstrlen(gDestPath);
  size_t lenrelpath = NS_tstrlen(relpath);
  NS_tchar *s = (NS_tchar *) malloc((lendestpath + lenrelpath + 1) * sizeof(NS_tchar));
  if (!s)
    return NULL;

  NS_tchar *c = s;

  NS_tstrcpy(c, gDestPath);
  c += lendestpath;
  NS_tstrcat(c, relpath);
  c += lenrelpath;
  *c = NS_T('\0');
  c++;
  return s;
}
#endif

/**
 * Gets the platform specific path and performs simple checks to the path. If
 * the path checks don't pass NULL will be returned.
 *
 * @param  line
 *         The line from the manifest that contains the path.
 * @param  isdir
 *         Whether the path is a directory path. Defaults to false.
 * @return valid filesystem path or NULL if the path checks fail.
 */
static NS_tchar*
get_valid_path(NS_tchar **line, bool isdir = false)
{
  NS_tchar *path = mstrtok(kQuote, line);
  if (!path) {
    LOG(("get_valid_path: unable to determine path: " LOG_S "\n", line));
    return NULL;
  }

  // All paths must be relative from the current working directory
  if (path[0] == NS_T('/')) {
    LOG(("get_valid_path: path must be relative: " LOG_S "\n", path));
    return NULL;
  }

#ifdef XP_WIN
  // All paths must be relative from the current working directory
  if (path[0] == NS_T('\\') || path[1] == NS_T(':')) {
    LOG(("get_valid_path: path must be relative: " LOG_S "\n", path));
    return NULL;
  }
#endif

  if (isdir) {
    // Directory paths must have a trailing forward slash.
    if (path[NS_tstrlen(path) - 1] != NS_T('/')) {
      LOG(("get_valid_path: directory paths must have a trailing forward " \
           "slash: " LOG_S "\n", path));
      return NULL;
    }

    // Remove the trailing forward slash because stat on Windows will return
    // ENOENT if the path has a trailing slash.
    path[NS_tstrlen(path) - 1] = NS_T('\0');
  }

  // Don't allow relative paths that resolve to a parent directory.
  if (NS_tstrstr(path, NS_T("..")) != NULL) {
    LOG(("get_valid_path: paths must not contain '..': " LOG_S "\n", path));
    return NULL;
  }

  return path;
}

static NS_tchar*
get_quoted_path(const NS_tchar *path)
{
  size_t lenQuote = NS_tstrlen(kQuote);
  size_t lenPath = NS_tstrlen(path);
  size_t len = lenQuote + lenPath + lenQuote + 1;

  NS_tchar *s = (NS_tchar *) malloc(len * sizeof(NS_tchar));
  if (!s)
    return NULL;

  NS_tchar *c = s;
  NS_tstrcpy(c, kQuote);
  c += lenQuote;
  NS_tstrcat(c, path);
  c += lenPath;
  NS_tstrcat(c, kQuote);
  c += lenQuote;
  *c = NS_T('\0');
  c++;
  return s;
}

static void ensure_write_permissions(const NS_tchar *path)
{
#ifdef XP_WIN
  (void) _wchmod(path, _S_IREAD | _S_IWRITE);
#else
  struct stat fs;
  if (!stat(path, &fs) && !(fs.st_mode & S_IWUSR)) {
    (void)chmod(path, fs.st_mode | S_IWUSR);
  }
#endif
}

static int ensure_remove(const NS_tchar *path)
{
  ensure_write_permissions(path);
  int rv = NS_tremove(path);
  if (rv)
    LOG(("ensure_remove: failed to remove file: " LOG_S ", rv: %d, err: %d\n",
         path, rv, errno));
  return rv;
}

static FILE* ensure_open(const NS_tchar *path, const NS_tchar *flags, unsigned int options)
{
  ensure_write_permissions(path);
  FILE* f = NS_tfopen(path, flags);
  if (NS_tchmod(path, options) != 0) {
    if (f != NULL) {
      fclose(f);
    }
    return NULL;
  }
  struct stat ss;
  if (NS_tstat(path, &ss) != 0 || ss.st_mode != options) {
    if (f != NULL) {
      fclose(f);
    }
    return NULL;
  }
  return f;
}

// Ensure that the directory containing this file exists.
static int ensure_parent_dir(const NS_tchar *path)
{
  int rv = OK;

  NS_tchar *slash = (NS_tchar *) NS_tstrrchr(path, NS_T('/'));
  if (slash) {
    *slash = NS_T('\0');
    rv = ensure_parent_dir(path);
    if (rv == OK) {
      rv = NS_tmkdir(path, 0755);
      // If the directory already exists, then ignore the error.
      if (rv < 0 && errno != EEXIST) {
        LOG(("ensure_parent_dir: failed to create directory: " LOG_S ", " \
             "err: %d\n", path, errno));
        rv = WRITE_ERROR;
      } else {
        rv = OK;
      }
    }
    *slash = NS_T('/');
  }
  return rv;
}

// Renames the specified file to the new file specified. If the destination file
// exists it is removed.
static int rename_file(const NS_tchar *spath, const NS_tchar *dpath)
{
  int rv = ensure_parent_dir(dpath);
  if (rv)
    return rv;

  struct stat spathInfo;
  rv = NS_tstat(spath, &spathInfo);
  if (rv) {
    LOG(("rename_file: failed to read file status info: " LOG_S ", " \
         "err: %d\n", spath, errno));
    return READ_ERROR;
  }

  if (!S_ISREG(spathInfo.st_mode)) {
    LOG(("rename_file: path present, but not a file: " LOG_S ", err: %d\n",
         spath, errno));
    return UNEXPECTED_ERROR;
  }

  if (!NS_taccess(dpath, F_OK)) {
    if (ensure_remove(dpath)) {
      LOG(("rename_file: destination file exists and could not be " \
           "removed: " LOG_S "\n", dpath));
      return WRITE_ERROR;
    }
  }

  if (NS_trename(spath, dpath) != 0) {
    LOG(("rename_file: failed to rename file - src: " LOG_S ", " \
         "dst:" LOG_S ", err: %d\n", spath, dpath, errno));
    return WRITE_ERROR;
  }

  return OK;
}

//-----------------------------------------------------------------------------

// Create a backup of the specified file by renaming it.
static int backup_create(const NS_tchar *path)
{
  NS_tchar backup[MAXPATHLEN];
  NS_tsnprintf(backup, sizeof(backup)/sizeof(backup[0]),
               NS_T("%s" BACKUP_EXT), path);

  return rename_file(path, backup);
}

// Rename the backup of the specified file that was created by renaming it back
// to the original file.
static int backup_restore(const NS_tchar *path)
{
  NS_tchar backup[MAXPATHLEN];
  NS_tsnprintf(backup, sizeof(backup)/sizeof(backup[0]),
               NS_T("%s" BACKUP_EXT), path);

  if (NS_taccess(backup, F_OK)) {
    LOG(("backup_restore: backup file doesn't exist: " LOG_S "\n", backup));
    return OK;
  }

  return rename_file(backup, path);
}

// Discard the backup of the specified file that was created by renaming it.
static int backup_discard(const NS_tchar *path)
{
  NS_tchar backup[MAXPATHLEN];
  NS_tsnprintf(backup, sizeof(backup)/sizeof(backup[0]),
               NS_T("%s" BACKUP_EXT), path);

  // Nothing to discard
  if (NS_taccess(backup, F_OK)) {
    return OK;
  }

  int rv = ensure_remove(backup);
#if defined(XP_WIN)
  if (rv) {
    LOG(("backup_discard: unable to remove: " LOG_S "\n", backup));
    NS_tchar path[MAXPATHLEN];
    GetTempFileNameW(DELETE_DIR, L"moz", 0, path);
    if (rename_file(backup, path)) {
      LOG(("backup_discard: failed to rename file:" LOG_S ", dst:" LOG_S "\n",
           backup, path));
      return WRITE_ERROR;
    }
    // The MoveFileEx call to remove the file on OS reboot will fail if the
    // process doesn't have write access to the HKEY_LOCAL_MACHINE registry key
    // but this is ok since the installer / uninstaller will delete the
    // directory containing the file along with its contents after an update is
    // applied, on reinstall, and on uninstall.
    if (MoveFileEx(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) {
      LOG(("backup_discard: file renamed and will be removed on OS " \
           "reboot: " LOG_S "\n", path));
    } else {
      LOG(("backup_discard: failed to schedule OS reboot removal of " \
           "file: " LOG_S "\n", path));
    }
  }
#else
  if (rv)
    return WRITE_ERROR;
#endif

  return OK;
}

// Helper function for post-processing a temporary backup.
static void backup_finish(const NS_tchar *path, int status)
{
  if (status == OK)
    backup_discard(path);
  else
    backup_restore(path);
}

//-----------------------------------------------------------------------------

static int DoUpdate();

class Action
{
public:
  Action() : mProgressCost(1), mNext(NULL) { }
  virtual ~Action() { }

  virtual int Parse(NS_tchar *line) = 0;

  // Do any preprocessing to ensure that the action can be performed.  Execute
  // will be called if this Action and all others return OK from this method.
  virtual int Prepare() = 0;

  // Perform the operation.  Return OK to indicate success.  After all actions
  // have been executed, Finish will be called.  A requirement of Execute is
  // that its operation be reversable from Finish.
  virtual int Execute() = 0;

  // Finish is called after execution of all actions.  If status is OK, then
  // all actions were successfully executed.  Otherwise, some action failed.
  virtual void Finish(int status) = 0;

  int mProgressCost;
private:
  Action* mNext;

  friend class ActionList;
};

class RemoveFile : public Action
{
public:
  RemoveFile() : mFile(NULL), mSkip(0) { }

  int Parse(NS_tchar *line);
  int Prepare();
  int Execute();
  void Finish(int status);

private:
  const NS_tchar *mFile;
  int mSkip;
};

int
RemoveFile::Parse(NS_tchar *line)
{
  // format "<deadfile>"

  mFile = get_valid_path(&line);
  if (!mFile)
    return PARSE_ERROR;

  return OK;
}

int
RemoveFile::Prepare()
{
  // Skip the file if it already doesn't exist.
  int rv = NS_taccess(mFile, F_OK);
  if (rv) {
    mSkip = 1;
    mProgressCost = 0;
    return OK;
  }

  LOG(("PREPARE REMOVEFILE " LOG_S "\n", mFile));

  // Make sure that we're actually a file...
  struct stat fileInfo;
  rv = NS_tstat(mFile, &fileInfo);
  if (rv) {
    LOG(("failed to read file status info: " LOG_S ", err: %d\n", mFile,
         errno));
    return READ_ERROR;
  }

  if (!S_ISREG(fileInfo.st_mode)) {
    LOG(("path present, but not a file: " LOG_S "\n", mFile));
    return UNEXPECTED_ERROR;
  }

  NS_tchar *slash = (NS_tchar *) NS_tstrrchr(mFile, NS_T('/'));
  if (slash) {
    *slash = NS_T('\0');
    rv = NS_taccess(mFile, W_OK);
    *slash = NS_T('/');
  } else {
    rv = NS_taccess(NS_T("."), W_OK);
  }

  if (rv) {
    LOG(("access failed: %d\n", errno));
    return WRITE_ERROR;
  }

  return OK;
}

int
RemoveFile::Execute()
{
  if (mSkip)
    return OK;

  LOG(("EXECUTE REMOVEFILE " LOG_S "\n", mFile));

  // The file is checked for existence here and in Prepare since it might have
  // been removed by a separate instruction: bug 311099.
  int rv = NS_taccess(mFile, F_OK);
  if (rv) {
    LOG(("file cannot be removed because it does not exist; skipping\n"));
    mSkip = 1;
    return OK;
  }

  // Rename the old file. It will be removed in Finish.
  rv = backup_create(mFile);
  if (rv) {
    LOG(("backup_create failed: %d\n", rv));
    return rv;
  }

  return OK;
}

void
RemoveFile::Finish(int status)
{
  if (mSkip)
    return;

  LOG(("FINISH REMOVEFILE " LOG_S "\n", mFile));

  backup_finish(mFile, status);
}

class RemoveDir : public Action
{
public:
  RemoveDir() : mDir(NULL), mSkip(0) { }

  virtual int Parse(NS_tchar *line);
  virtual int Prepare(); // check that the source dir exists
  virtual int Execute();
  virtual void Finish(int status);

private:
  const NS_tchar *mDir;
  int mSkip;
};

int
RemoveDir::Parse(NS_tchar *line)
{
  // format "<deaddir>/"

  mDir = get_valid_path(&line, true);
  if (!mDir)
    return PARSE_ERROR;

  return OK;
}

int
RemoveDir::Prepare()
{
  // We expect the directory to exist if we are to remove it.
  int rv = NS_taccess(mDir, F_OK);
  if (rv) {
    mSkip = 1;
    mProgressCost = 0;
    return OK;
  }

  LOG(("PREPARE REMOVEDIR " LOG_S "/\n", mDir));

  // Make sure that we're actually a dir.
  struct stat dirInfo;
  rv = NS_tstat(mDir, &dirInfo);
  if (rv) {
    LOG(("failed to read directory status info: " LOG_S ", err: %d\n", mDir,
         errno));
    return READ_ERROR;
  }

  if (!S_ISDIR(dirInfo.st_mode)) {
    LOG(("path present, but not a directory: " LOG_S "\n", mDir));
    return UNEXPECTED_ERROR;
  }

  rv = NS_taccess(mDir, W_OK);
  if (rv) {
    LOG(("access failed: %d, %d\n", rv, errno));
    return WRITE_ERROR;
  }

  return OK;
}

int
RemoveDir::Execute()
{
  if (mSkip)
    return OK;

  LOG(("EXECUTE REMOVEDIR " LOG_S "/\n", mDir));

  // The directory is checked for existence at every step since it might have
  // been removed by a separate instruction: bug 311099.
  int rv = NS_taccess(mDir, F_OK);
  if (rv) {
    LOG(("directory no longer exists; skipping\n"));
    mSkip = 1;
  }

  return OK;
}

void
RemoveDir::Finish(int status)
{
  if (mSkip || status != OK)
    return;

  LOG(("FINISH REMOVEDIR " LOG_S "/\n", mDir));

  // The directory is checked for existence at every step since it might have
  // been removed by a separate instruction: bug 311099.
  int rv = NS_taccess(mDir, F_OK);
  if (rv) {
    LOG(("directory no longer exists; skipping\n"));
    return;
  }


  if (status == OK) {
    if (NS_trmdir(mDir)) {
      LOG(("non-fatal error removing directory: " LOG_S "/, rv: %d, err: %d\n",
           mDir, rv, errno));
    }
  }
}

class AddFile : public Action
{
public:
  AddFile() : mFile(NULL)
            , mAdded(false)
            { }

  virtual int Parse(NS_tchar *line);
  virtual int Prepare();
  virtual int Execute();
  virtual void Finish(int status);

private:
  const NS_tchar *mFile;
  bool mAdded;
};

int
AddFile::Parse(NS_tchar *line)
{
  // format "<newfile>"

  mFile = get_valid_path(&line);
  if (!mFile)
    return PARSE_ERROR;

  return OK;
}

int
AddFile::Prepare()
{
  LOG(("PREPARE ADD " LOG_S "\n", mFile));

  return OK;
}

int
AddFile::Execute()
{
  LOG(("EXECUTE ADD " LOG_S "\n", mFile));

  int rv;

  // First make sure that we can actually get rid of any existing file.
  rv = NS_taccess(mFile, F_OK);
  if (rv == 0) {
    rv = backup_create(mFile);
    if (rv)
      return rv;
  } else {
    rv = ensure_parent_dir(mFile);
    if (rv)
      return rv;
  }

#ifdef XP_WIN
  char sourcefile[MAXPATHLEN];
  if (!WideCharToMultiByte(CP_UTF8, 0, mFile, -1, sourcefile, MAXPATHLEN,
                           NULL, NULL)) {
    LOG(("error converting wchar to utf8: %d\n", GetLastError()));
    return STRING_CONVERSION_ERROR;
  }

  rv = gArchiveReader.ExtractFile(sourcefile, mFile);
#else
  rv = gArchiveReader.ExtractFile(mFile, mFile);
#endif
  if (!rv) {
    mAdded = true;
  }
  return rv;
}

void
AddFile::Finish(int status)
{
  LOG(("FINISH ADD " LOG_S "\n", mFile));
  // When there is an update failure and a file has been added it is removed
  // here since there might not be a backup to replace it.
  if (status && mAdded)
    NS_tremove(mFile);
  backup_finish(mFile, status);
}

class PatchFile : public Action
{
public:
  PatchFile() : mPatchIndex(-1), buf(NULL) { }

  virtual ~PatchFile();

  virtual int Parse(NS_tchar *line);
  virtual int Prepare(); // should check for patch file and for checksum here
  virtual int Execute();
  virtual void Finish(int status);

private:
  int LoadSourceFile(FILE* ofile);

  static int sPatchIndex;

  const NS_tchar *mPatchFile;
  const NS_tchar *mFile;
  int mPatchIndex;
  MBSPatchHeader header;
  unsigned char *buf;
  NS_tchar spath[MAXPATHLEN];
};

int PatchFile::sPatchIndex = 0;

PatchFile::~PatchFile()
{
  // delete the temporary patch file
  if (spath[0])
    NS_tremove(spath);

  if (buf)
    free(buf);
}

int
PatchFile::LoadSourceFile(FILE* ofile)
{
  struct stat os;
  int rv = fstat(fileno((FILE *)ofile), &os);
  if (rv) {
    LOG(("LoadSourceFile: unable to stat destination file: " LOG_S ", " \
         "err: %d\n", mFile, errno));
    return READ_ERROR;
  }

  if (PRUint32(os.st_size) != header.slen) {
    LOG(("LoadSourceFile: destination file size %d does not match expected size %d\n",
         PRUint32(os.st_size), header.slen));
    return UNEXPECTED_ERROR;
  }

  buf = (unsigned char *) malloc(header.slen);
  if (!buf)
    return UPDATER_MEM_ERROR;

  size_t r = header.slen;
  unsigned char *rb = buf;
  while (r) {
    const size_t count = mmin(SSIZE_MAX, r);
    size_t c = fread(rb, 1, count, ofile);
    if (c != count) {
      LOG(("LoadSourceFile: error reading destination file: " LOG_S "\n",
           mFile));
      return READ_ERROR;
    }

    r -= c;
    rb += c;
  }

  // Verify that the contents of the source file correspond to what we expect.

  unsigned int crc = crc32(buf, header.slen);

  if (crc != header.scrc32) {
    LOG(("LoadSourceFile: destination file crc %d does not match expected " \
         "crc %d\n", crc, header.scrc32));
    return CRC_ERROR;
  }

  return OK;
}

int
PatchFile::Parse(NS_tchar *line)
{
  // format "<patchfile>" "<filetopatch>"

  // Get the path to the patch file inside of the mar
  mPatchFile = mstrtok(kQuote, &line);
  if (!mPatchFile)
    return PARSE_ERROR;

  // consume whitespace between args
  NS_tchar *q = mstrtok(kQuote, &line);
  if (!q)
    return PARSE_ERROR;

  mFile = get_valid_path(&line);
  if (!mFile)
    return PARSE_ERROR;

  return OK;
}

int
PatchFile::Prepare()
{
  LOG(("PREPARE PATCH " LOG_S "\n", mFile));

  // extract the patch to a temporary file
  mPatchIndex = sPatchIndex++;

  NS_tsnprintf(spath, sizeof(spath)/sizeof(spath[0]),
               NS_T("%s/%d.patch"), gSourcePath, mPatchIndex);

  NS_tremove(spath);

  FILE *fp = NS_tfopen(spath, NS_T("wb"));
  if (!fp)
    return WRITE_ERROR;

#ifdef XP_WIN
  char sourcefile[MAXPATHLEN];
  if (!WideCharToMultiByte(CP_UTF8, 0, mPatchFile, -1, sourcefile, MAXPATHLEN,
                           NULL, NULL)) {
    LOG(("error converting wchar to utf8: %d\n", GetLastError()));
    return STRING_CONVERSION_ERROR;
  }

  int rv = gArchiveReader.ExtractFileToStream(sourcefile, fp);
#else
  int rv = gArchiveReader.ExtractFileToStream(mPatchFile, fp);
#endif
  fclose(fp);
  return rv;
}

int
PatchFile::Execute()
{
  LOG(("EXECUTE PATCH " LOG_S "\n", mFile));

  AutoFile pfile = NS_tfopen(spath, NS_T("rb"));
  if (pfile == NULL)
    return READ_ERROR;

  int rv = MBS_ReadHeader(pfile, &header);
  if (rv)
    return rv;

  FILE *origfile = NULL;
#ifdef XP_WIN
  if (NS_tstrcmp(mFile, gCallbackRelPath) == 0) {
    // Read from the copy of the callback when patching since the callback can't
    // be opened for reading to prevent the application from being launched.
    origfile = NS_tfopen(gCallbackBackupPath, NS_T("rb"));
  } else {
    origfile = NS_tfopen(mFile, NS_T("rb"));
  }
#else
  origfile = NS_tfopen(mFile, NS_T("rb"));
#endif

  if (!origfile) {
    LOG(("unable to open destination file: " LOG_S ", err: %d\n", mFile,
         errno));
    return READ_ERROR;
  }

  rv = LoadSourceFile(origfile);
  fclose(origfile);
  if (rv) {
    LOG(("LoadSourceFile failed\n"));
    return rv;
  }

  // Rename the destination file if it exists before proceeding so it can be
  // used to restore the file to its original state if there is an error.
  struct stat ss;
  rv = NS_tstat(mFile, &ss);
  if (rv) {
    LOG(("failed to read file status info: " LOG_S ", err: %d\n", mFile,
         errno));
    return READ_ERROR;
  }

  rv = backup_create(mFile);
  if (rv)
    return rv;

#if defined(HAVE_POSIX_FALLOCATE)
  AutoFile ofile = ensure_open(mFile, NS_T("wb+"), ss.st_mode);
  posix_fallocate(fileno((FILE *)ofile), 0, header.dlen);
#elif defined(XP_WIN)
  bool shouldTruncate = true;
  // Creating the file, setting the size, and then closing the file handle
  // lessens fragmentation more than any other method tested. Other methods that
  // have been tested are:
  // 1. _chsize / _chsize_s reduced fragmentation but though not completely.
  // 2. _get_osfhandle and then setting the size reduced fragmentation though
  //    not completely. There are also reports of _get_osfhandle failing on
  //    mingw.
  HANDLE hfile = CreateFileW(mFile,
                             GENERIC_WRITE,
                             0,
                             NULL,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);

  if (hfile != INVALID_HANDLE_VALUE) {
    if (SetFilePointer(hfile, header.dlen, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
        SetEndOfFile(hfile) != 0) {
      shouldTruncate = false;
    }
    CloseHandle(hfile);
  }

  AutoFile ofile = ensure_open(mFile, shouldTruncate ? NS_T("wb+") : NS_T("rb+"), ss.st_mode);
#elif defined(XP_MACOSX)
  AutoFile ofile = ensure_open(mFile, NS_T("wb+"), ss.st_mode);
  // Modified code from FileUtils.cpp
  fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, header.dlen};
  // Try to get a continous chunk of disk space
  rv = fcntl(fileno((FILE *)ofile), F_PREALLOCATE, &store);
  if (rv == -1) {
    // OK, perhaps we are too fragmented, allocate non-continuous
    store.fst_flags = F_ALLOCATEALL;
    rv = fcntl(fileno((FILE *)ofile), F_PREALLOCATE, &store);
  }

  if (rv != -1) {
    ftruncate(fileno((FILE *)ofile), header.dlen);
  }
#else
  AutoFile ofile = ensure_open(mFile, NS_T("wb+"), ss.st_mode);
#endif

  if (ofile == NULL) {
    LOG(("unable to create new file: " LOG_S ", err: %d\n", mFile, errno));
    return WRITE_ERROR;
  }

#ifdef XP_WIN
  if (!shouldTruncate) {
    fseek(ofile, 0, SEEK_SET);
  }
#endif

  rv = MBS_ApplyPatch(&header, pfile, buf, ofile);

  // Go ahead and do a bit of cleanup now to minimize runtime overhead.
  // Set pfile to NULL to make AutoFile close the file so it can be deleted on
  // Windows.
  pfile = NULL;
  NS_tremove(spath);
  spath[0] = NS_T('\0');
  free(buf);
  buf = NULL;

  return rv;
}

void
PatchFile::Finish(int status)
{
  LOG(("FINISH PATCH " LOG_S "\n", mFile));

  backup_finish(mFile, status);
}

class AddIfFile : public AddFile
{
public:
  AddIfFile() : mTestFile(NULL) { }

  virtual int Parse(NS_tchar *line);
  virtual int Prepare();
  virtual int Execute();
  virtual void Finish(int status);

protected:
  const NS_tchar *mTestFile;
};

int
AddIfFile::Parse(NS_tchar *line)
{
  // format "<testfile>" "<newfile>"

  mTestFile = get_valid_path(&line);
  if (!mTestFile)
    return PARSE_ERROR;

  // consume whitespace between args
  NS_tchar *q = mstrtok(kQuote, &line);
  if (!q)
    return PARSE_ERROR;

  return AddFile::Parse(line);
}

int
AddIfFile::Prepare()
{
  // If the test file does not exist, then skip this action.
  if (NS_taccess(mTestFile, F_OK)) {
    mTestFile = NULL;
    return OK;
  }

  return AddFile::Prepare();
}

int
AddIfFile::Execute()
{
  if (!mTestFile)
    return OK;

  return AddFile::Execute();
}

void
AddIfFile::Finish(int status)
{
  if (!mTestFile)
    return;

  AddFile::Finish(status);
}

class PatchIfFile : public PatchFile
{
public:
  PatchIfFile() : mTestFile(NULL) { }

  virtual int Parse(NS_tchar *line);
  virtual int Prepare(); // should check for patch file and for checksum here
  virtual int Execute();
  virtual void Finish(int status);

private:
  const NS_tchar *mTestFile;
};

int
PatchIfFile::Parse(NS_tchar *line)
{
  // format "<testfile>" "<patchfile>" "<filetopatch>"

  mTestFile = get_valid_path(&line);
  if (!mTestFile)
    return PARSE_ERROR;

  // consume whitespace between args
  NS_tchar *q = mstrtok(kQuote, &line);
  if (!q)
    return PARSE_ERROR;

  return PatchFile::Parse(line);
}

int
PatchIfFile::Prepare()
{
  // If the test file does not exist, then skip this action.
  if (NS_taccess(mTestFile, F_OK)) {
    mTestFile = NULL;
    return OK;
  }

  return PatchFile::Prepare();
}

int
PatchIfFile::Execute()
{
  if (!mTestFile)
    return OK;

  return PatchFile::Execute();
}

void
PatchIfFile::Finish(int status)
{
  if (!mTestFile)
    return;

  PatchFile::Finish(status);
}

//-----------------------------------------------------------------------------

#ifdef XP_WIN
#include "nsWindowsRestart.cpp"
#include "uachelper.h"
#include "pathhash.h"
#endif

static void
LaunchCallbackApp(const NS_tchar *workingDir, 
                  int argc, 
                  NS_tchar **argv, 
                  bool usingService)
{
  putenv(const_cast<char*>("NO_EM_RESTART="));
  putenv(const_cast<char*>("MOZ_LAUNCHED_CHILD=1"));

  // Run from the specified working directory (see bug 312360). This is not
  // necessary on Windows CE since the application that launches the updater
  // passes the working directory as an --environ: command line argument.
  if (NS_tchdir(workingDir) != 0) {
    LOG(("Warning: chdir failed\n"));
  }

#if defined(USE_EXECV)
  execv(argv[0], argv);
#elif defined(XP_MACOSX)
  LaunchChild(argc, argv);
#elif defined(XP_WIN)
  // Do not allow the callback to run when running an update through the
  // service as session 0.  The unelevated updater.exe will do the launching.
  if (!usingService) {
    WinLaunchChild(argv[0], argc, argv, NULL);
  }
#else
# warning "Need implementaton of LaunchCallbackApp"
#endif
}

static void
WriteStatusFile(int status)
{
  // This is how we communicate our completion status to the main application.

  NS_tchar filename[MAXPATHLEN];
  NS_tsnprintf(filename, sizeof(filename)/sizeof(filename[0]),
               NS_T("%s/update.status"), gSourcePath);

  AutoFile file = NS_tfopen(filename, NS_T("wb+"));
  if (file == NULL)
    return;

  const char *text;

  char buf[32];
  if (status == OK) {
    text = "succeeded\n";
  } else {
    snprintf(buf, sizeof(buf)/sizeof(buf[0]), "failed: %d\n", status);
    text = buf;
  }
  fwrite(text, strlen(text), 1, file);
}

static bool
WriteStatusApplying()
{
  NS_tchar filename[MAXPATHLEN];
  NS_tsnprintf(filename, sizeof(filename)/sizeof(filename[0]),
               NS_T("%s/update.status"), gSourcePath);

  AutoFile file = NS_tfopen(filename, NS_T("wb+"));
  if (file == NULL)
    return false;

  static const char kApplying[] = "applying";
  if (fwrite(kApplying, strlen(kApplying), 1, file) != 1)
    return false;

  return true;
}

#ifdef MOZ_MAINTENANCE_SERVICE
/* 
 * Read the update.status file and sets isPendingService to true if
 * the status is set to pending-service.
 *
 * @param  isPendingService Out parameter for specifying if the status
 *         is set to pending-service or not.
 * @return true if the information was retrieved and it is pending
 *         or pending-service.
*/
static bool
IsUpdateStatusPending(bool &isPendingService)
{
  bool isPending = false;
  isPendingService = false;
  NS_tchar filename[MAXPATHLEN];
  NS_tsnprintf(filename, sizeof(filename)/sizeof(filename[0]),
               NS_T("%s/update.status"), gSourcePath);

  AutoFile file = NS_tfopen(filename, NS_T("rb"));
  if (file == NULL)
    return false;

  char buf[32] = { 0 };
  fread(buf, sizeof(buf), 1, file);

  const char kPending[] = "pending";
  const char kPendingService[] = "pending-service";
  isPending = strncmp(buf, kPending, 
                      sizeof(kPending) - 1) == 0;

  isPendingService = strncmp(buf, kPendingService, 
                             sizeof(kPendingService) - 1) == 0;
  return isPending;
}
#endif

#ifdef XP_WIN
/* 
 * Read the update.status file and sets isSuccess to true if
 * the status is set to succeeded.
 *
 * @param  isSucceeded Out parameter for specifying if the status
 *         is set to succeeded or not.
 * @return true if the information was retrieved and it is succeeded.
*/
static bool
IsUpdateStatusSucceeded(bool &isSucceeded)
{
  isSucceeded = false;
  NS_tchar filename[MAXPATHLEN];
  NS_tsnprintf(filename, sizeof(filename)/sizeof(filename[0]),
               NS_T("%s/update.status"), gSourcePath);

  AutoFile file = NS_tfopen(filename, NS_T("rb"));
  if (file == NULL)
    return false;

  char buf[32] = { 0 };
  fread(buf, sizeof(buf), 1, file);

  const char kSucceeded[] = "succeeded";
  isSucceeded = strncmp(buf, kSucceeded, 
                        sizeof(kSucceeded) - 1) == 0;
  return true;
}

static void 
WaitForServiceFinishThread(void *param)
{
  // We wait at most 10 minutes, we already waited 5 seconds previously
  // before deciding to show this UI.
  WaitForServiceStop(SVC_NAME, 595);
  LOG(("calling QuitProgressUI\n"));
  QuitProgressUI();
}
#endif

/**
 * This function reads in the ACCEPTED_MAR_CHANNEL_IDS from update-settings.ini
 *
 * @param path    The path to the ini file that is to be read
 * @param results A pointer to the location to store the read strings
 * @return OK on success
 */
static int
ReadMARChannelIDs(const NS_tchar *path, MARChannelStringTable *results)
{
  const unsigned int kNumStrings = 1;
  const char *kUpdaterKeys = "ACCEPTED_MAR_CHANNEL_IDS\0";
  char updater_strings[kNumStrings][MAX_TEXT_LEN];

  int result = ReadStrings(path, kUpdaterKeys, kNumStrings,
                           updater_strings, "Settings");

  strncpy(results->MARChannelID, updater_strings[0], MAX_TEXT_LEN - 1);
  results->MARChannelID[MAX_TEXT_LEN - 1] = 0;

  return result;
}

struct UpdateThreadData 
{
  UpdateThreadData(bool performMARChecks) :
    mPerformMARChecks(performMARChecks)
  {
  }

  bool mPerformMARChecks;
};

static void
UpdateThreadFunc(void *param)
{
  UpdateThreadData *threadData = reinterpret_cast<UpdateThreadData*>(param);
  bool performMARChecks = threadData && threadData->mPerformMARChecks;
  delete threadData;
  
  // open ZIP archive and process...
  int rv;
  NS_tchar dataFile[MAXPATHLEN];
  NS_tsnprintf(dataFile, sizeof(dataFile)/sizeof(dataFile[0]),
               NS_T("%s/update.mar"), gSourcePath);

  rv = gArchiveReader.Open(dataFile);

  if (performMARChecks) {
#ifdef MOZ_VERIFY_MAR_SIGNATURE
    if (rv == OK) {
      rv = gArchiveReader.VerifySignature();
    }

    if (rv == OK) {
      NS_tchar updateSettingsPath[MAX_TEXT_LEN];
      NS_tsnprintf(updateSettingsPath, 
                   sizeof(updateSettingsPath) / sizeof(updateSettingsPath[0]),
                   NS_T("%supdate-settings.ini"), gDestPath);
      MARChannelStringTable MARStrings;
      if (ReadMARChannelIDs(updateSettingsPath, &MARStrings) != OK) {
        // If we can't read from update-settings.ini then we shouldn't impose
        // a MAR restriction.  Some installations won't even include this file.
        MARStrings.MARChannelID[0] = '\0';
      }

      rv = gArchiveReader.VerifyProductInformation(MARStrings.MARChannelID,
                                                   MOZ_APP_VERSION);
    }
#endif
  }

  if (rv == OK) {
    rv = DoUpdate();
    gArchiveReader.Close();
  }

  if (rv) {
    LOG(("failed: %d\n", rv));
  }
  else {
#ifdef XP_MACOSX
    // If the update was successful we need to update the timestamp
    // on the top-level Mac OS X bundle directory so that Mac OS X's
    // Launch Services picks up any major changes. Here we assume that
    // the current working directory is the top-level bundle directory.
    char* cwd = getcwd(NULL, 0);
    if (cwd) {
      if (utimes(cwd, NULL) != 0) {
        LOG(("Couldn't set access/modification time on application bundle.\n"));
      }
      free(cwd);
    }
    else {
      LOG(("Couldn't get current working directory for setting "
           "access/modification time on application bundle.\n"));
    }
#endif

    LOG(("succeeded\n"));
  }
  WriteStatusFile(rv);

  LOG(("calling QuitProgressUI\n"));
  QuitProgressUI();
}

int NS_main(int argc, NS_tchar **argv)
{
  InitProgressUI(&argc, &argv);

  // To process an update the updater command line must at a minimum have the
  // directory path containing the updater.mar file to process as the first argument
  // and the directory to apply the update to as the second argument. When the
  // updater is launched by another process the PID of the parent process should be
  // provided in the optional third argument and the updater will wait on the parent
  // process to exit if the value is non-zero and the process is present. This is
  // necessary due to not being able to update files that are in use on Windows. The
  // optional fourth argument is the callback's working directory and the optional
  // fifth argument is the callback path. The callback is the application to launch
  // after  updating and it will be launched when these arguments are provided
  // whether the update was successful or not. All remaining arguments are optional
  // and are passed to the callback when it is launched.
  if (argc < 3) {
    fprintf(stderr, "Usage: updater update-dir apply-to-dir [wait-pid [callback-working-dir callback-path args...]]\n");
    return 1;
  }

  // Change current directory to the directory where we need to apply the update.
  if (NS_tchdir(argv[2]) != 0) {
    return 1;
  }

  // The directory containing the update information.
  gSourcePath = argv[1];

#ifdef XP_WIN
  bool useService = false;
  bool testOnlyFallbackKeyExists = false;
  bool noServiceFallback = getenv("MOZ_NO_SERVICE_FALLBACK") != NULL;
  putenv(const_cast<char*>("MOZ_NO_SERVICE_FALLBACK="));

  // We never want the service to be used unless we build with
  // the maintenance service.
#ifdef MOZ_MAINTENANCE_SERVICE
  IsUpdateStatusPending(useService);
  // Our tests run with a different apply directory for each test.
  // We use this registry key on our test slaves to store the 
  // allowed name/issuers.
  testOnlyFallbackKeyExists = DoesFallbackKeyExist();
#endif
#endif

  if (!WriteStatusApplying()) {
    LOG(("failed setting status to 'applying'\n"));
    return 1;
  }

#ifdef XP_WIN
  // Remove everything except close window from the context menu
  {
    HKEY hkApp;
    RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications",
                    0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL,
                    &hkApp, NULL);
    RegCloseKey(hkApp);
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Classes\\Applications\\updater.exe",
                        0, NULL, REG_OPTION_VOLATILE, KEY_SET_VALUE, NULL,
                        &hkApp, NULL) == ERROR_SUCCESS) {
      RegSetValueExW(hkApp, L"IsHostApp", 0, REG_NONE, 0, 0);
      RegSetValueExW(hkApp, L"NoOpenWith", 0, REG_NONE, 0, 0);
      RegSetValueExW(hkApp, L"NoStartPage", 0, REG_NONE, 0, 0);
      RegCloseKey(hkApp);
    }
  }
#endif

  // If there is a PID specified and it is not '0' then wait for the process to exit.
  if (argc > 3) {
#ifdef XP_WIN
    __int64 pid = _wtoi64(argv[3]);
    if (pid != 0) {
      HANDLE parent = OpenProcess(SYNCHRONIZE, false, (DWORD) pid);
      // May return NULL if the parent process has already gone away.
      // Otherwise, wait for the parent process to exit before starting the
      // update.
      if (parent) {
        DWORD result = WaitForSingleObject(parent, 5000);
        CloseHandle(parent);
        if (result != WAIT_OBJECT_0)
          return 1;
      }
    }
#else
    int pid = atoi(argv[3]);
    if (pid != 0)
      waitpid(pid, NULL, 0);
#endif
  }

  // The callback is the remaining arguments starting at callbackIndex.
  // The argument specified by callbackIndex is the callback executable and the
  // argument prior to callbackIndex is the working directory.
  const int callbackIndex = 5;

  bool usingService = false;
#if defined(XP_WIN)
  usingService = getenv("MOZ_USING_SERVICE") != NULL;
  putenv(const_cast<char*>("MOZ_USING_SERVICE="));
  // lastFallbackError keeps track of the last error for the service not being 
  // used, in case of an error when fallback is not enabled we write the 
  // error to the update.status file. 
  // When fallback is disabled (MOZ_NO_SERVICE_FALLBACK does not exist) then
  // we will instead fallback to not using the service and display a UAC prompt.
  int lastFallbackError = FALLBACKKEY_UNKNOWN_ERROR;

  // Launch a second instance of the updater with the runas verb on Windows
  // when write access is denied to the installation directory.
  HANDLE updateLockFileHandle;
  NS_tchar elevatedLockFilePath[MAXPATHLEN];
  if (argc > callbackIndex && !usingService) {
    NS_tchar updateLockFilePath[MAXPATHLEN];
    NS_tsnprintf(updateLockFilePath,
                 sizeof(updateLockFilePath)/sizeof(updateLockFilePath[0]),
                 NS_T("%s.update_in_progress.lock"), argv[callbackIndex]);

    // The update_in_progress.lock file should only exist during an update. In
    // case it exists attempt to remove it and exit if that fails to prevent
    // simultaneous updates occurring.
    if (!_waccess(updateLockFilePath, F_OK) &&
        NS_tremove(updateLockFilePath) != 0) {
      fprintf(stderr, "Update already in progress! Exiting\n");
      return 1;
    }

    updateLockFileHandle = CreateFileW(updateLockFilePath,
                                       GENERIC_READ | GENERIC_WRITE,
                                       0,
                                       NULL,
                                       OPEN_ALWAYS,
                                       FILE_FLAG_DELETE_ON_CLOSE,
                                       NULL);

    NS_tsnprintf(elevatedLockFilePath,
                 sizeof(elevatedLockFilePath)/sizeof(elevatedLockFilePath[0]),
                 NS_T("%s/update_elevated.lock"), argv[1]);


    // Even if a file has no sharing access, you can still get its attributes
    bool startedFromUnelevatedUpdater =
      GetFileAttributesW(elevatedLockFilePath) != INVALID_FILE_ATTRIBUTES;
    
    // If we're running from the service, then we were started with the same
    // token as the service so the permissions are already dropped.  If we're
    // running from an elevated updater that was started from an unelevated 
    // updater, then we drop the permissions here. We do not drop the 
    // permissions on the originally called updater because we use its token
    // to start the callback application.
    if(startedFromUnelevatedUpdater) {
      // Disable every privilege we don't need. Processes started using
      // CreateProcess will use the same token as this process.
      UACHelper::DisablePrivileges(NULL);
    }

    if (updateLockFileHandle == INVALID_HANDLE_VALUE || 
        (useService && testOnlyFallbackKeyExists && noServiceFallback)) {
      if (!_waccess(elevatedLockFilePath, F_OK) &&
          NS_tremove(elevatedLockFilePath) != 0) {
        fprintf(stderr, "Update already elevated! Exiting\n");
        return 1;
      }

      HANDLE elevatedFileHandle;
      elevatedFileHandle = CreateFileW(elevatedLockFilePath,
                                       GENERIC_READ | GENERIC_WRITE,
                                       0,
                                       NULL,
                                       OPEN_ALWAYS,
                                       FILE_FLAG_DELETE_ON_CLOSE,
                                       NULL);

      if (elevatedFileHandle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Unable to create elevated lock file! Exiting\n");
        return 1;
      }

      PRUnichar *cmdLine = MakeCommandLine(argc - 1, argv + 1);
      if (!cmdLine) {
        CloseHandle(elevatedFileHandle);
        return 1;
      }

      // Make sure the service registry entries for the instsallation path
      // are available.  If not don't use the service.
      if (useService) {
        WCHAR maintenanceServiceKey[MAX_PATH + 1];
        if (CalculateRegistryPathFromFilePath(argv[2], maintenanceServiceKey)) {
          HKEY baseKey;
          if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
                            maintenanceServiceKey, 0, 
                            KEY_READ | KEY_WOW64_64KEY, 
                            &baseKey) == ERROR_SUCCESS) {
            RegCloseKey(baseKey);
          } else {
            useService = testOnlyFallbackKeyExists;
            if (!useService) {
              lastFallbackError = FALLBACKKEY_NOKEY_ERROR;
            }
          }
        } else {
          useService = false;
          lastFallbackError = FALLBACKKEY_REGPATH_ERROR;
        }
      }

      // Originally we used to write "pending" to update.status before
      // launching the service command.  This is no longer needed now
      // since the service command is launched from updater.exe.  If anything
      // fails in between, we can fall back to using the normal update process
      // on our own.

      // If we still want to use the service try to launch the service 
      // comamnd for the update.
      if (useService) {
        // If the update couldn't be started, then set useService to false so
        // we do the update the old way.
        DWORD ret = LaunchServiceSoftwareUpdateCommand(argc, (LPCWSTR *)argv);
        useService = (ret == ERROR_SUCCESS);
        // If the command was launched then wait for the service to be done.
        if (useService) {
          // We need to call this separately instead of allowing ShowProgressUI
          // to initialize the strings because the service will move the
          // ini file out of the way when running updater.
          bool showProgressUI = !InitProgressUIStrings();

          // Wait for the service to stop for 5 seconds.  If the service
          // has still not stopped then show an indeterminate progress bar.
          DWORD lastState = WaitForServiceStop(SVC_NAME, 5);
          if (lastState != SERVICE_STOPPED) {
            Thread t1;
            if (t1.Run(WaitForServiceFinishThread, NULL) == 0 && 
                showProgressUI) {
              ShowProgressUI(true, false);
            }
            t1.Join();
          }

          lastState = WaitForServiceStop(SVC_NAME, 1);
          if (lastState != SERVICE_STOPPED) {
            // If the service doesn't stop after 10 minutes there is
            // something seriously wrong.
            lastFallbackError = FALLBACKKEY_SERVICE_NO_STOP_ERROR;
            useService = false;
          }
        } else {
          lastFallbackError = FALLBACKKEY_LAUNCH_ERROR;
        }
      }

      // If we started the service command, and it finished, check the
      // update.status file to make sure it succeeded, and if it did
      // we need to manually start the PostUpdate process from the
      // current user's session of this unelevated updater.exe the
      // current process is running as.
      if (useService) {
        bool updateStatusSucceeded = false;
        if (IsUpdateStatusSucceeded(updateStatusSucceeded) && 
            updateStatusSucceeded) {
          if (!LaunchWinPostProcess(argv[2], gSourcePath, false, NULL)) {
            fprintf(stderr, "The post update process which runs as the user"
                    " for service update could not be launched.");
          }
        }
      }

      // If we didn't want to use the service at all, or if an update was 
      // already happening, or launching the service command failed, then 
      // launch the elevated updater.exe as we do without the service.
      // We don't launch the elevated updater in the case that we did have
      // write access all along because in that case the only reason we're
      // using the service is because we are testing. 
      if (!useService && !noServiceFallback && 
          updateLockFileHandle == INVALID_HANDLE_VALUE) {
        SHELLEXECUTEINFO sinfo;
        memset(&sinfo, 0, sizeof(SHELLEXECUTEINFO));
        sinfo.cbSize       = sizeof(SHELLEXECUTEINFO);
        sinfo.fMask        = SEE_MASK_FLAG_NO_UI |
                             SEE_MASK_FLAG_DDEWAIT |
                             SEE_MASK_NOCLOSEPROCESS;
        sinfo.hwnd         = NULL;
        sinfo.lpFile       = argv[0];
        sinfo.lpParameters = cmdLine;
        sinfo.lpVerb       = L"runas";
        sinfo.nShow        = SW_SHOWNORMAL;

        bool result = ShellExecuteEx(&sinfo);
        free(cmdLine);

        if (result) {
          WaitForSingleObject(sinfo.hProcess, INFINITE);
          CloseHandle(sinfo.hProcess);
        } else {
          WriteStatusFile(ELEVATION_CANCELED);
        }
      }

      // We are already contained in a condition where argc > callbackIndex
      LaunchCallbackApp(argv[4], argc - callbackIndex, 
                        argv + callbackIndex, usingService);

      CloseHandle(elevatedFileHandle);

      if (!useService && !noServiceFallback &&
          INVALID_HANDLE_VALUE == updateLockFileHandle) {
        // We didn't use the service and we did run the elevated updater.exe.
        // The elevated updater.exe is responsible for writing out the
        // update.status file.
        return 0;
      } else if(useService) {
        // The service command was launched.  The service is responsible for 
        // writing out the update.status file.
        if (updateLockFileHandle != INVALID_HANDLE_VALUE) {
          CloseHandle(updateLockFileHandle);
        }
        return 0;
      } else {
        // Otherwise the service command was not launched at all.
        // We are only reaching this code path because we had write access
        // all along to the directory and a fallback key existed, and we
        // have fallback disabled (MOZ_NO_SERVICE_FALLBACK env var exists).
        // We only currently use this env var from XPCShell tests.
        CloseHandle(updateLockFileHandle);
        WriteStatusFile(lastFallbackError);
        return 0;
      }
    }
  }
#endif

  LogInit(gSourcePath, NS_T("update.log"));
  LOG(("SOURCE DIRECTORY " LOG_S "\n", argv[1]));
  LOG(("DESTINATION DIRECTORY " LOG_S "\n", argv[2]));

#ifdef XP_WIN
  // Allocate enough space for the length of the path an optional additional
  // trailing slash and null termination.
  NS_tchar *destpath = (NS_tchar *) malloc((NS_tstrlen(argv[2]) + 2) * sizeof(NS_tchar));
  if (!destpath)
    return 1;

  NS_tchar *c = destpath;
  NS_tstrcpy(c, argv[2]);
  c += NS_tstrlen(argv[2]);
  if (argv[2][NS_tstrlen(argv[2]) - 1] != NS_T('/') &&
      argv[2][NS_tstrlen(argv[2]) - 1] != NS_T('\\')) {
    NS_tstrcat(c, NS_T("/"));
    c += NS_tstrlen(NS_T("/"));
  }
  *c = NS_T('\0');
  c++;

  gDestPath = destpath;

  HANDLE callbackFile = INVALID_HANDLE_VALUE;
  if (argc > callbackIndex) {
    // If the callback executable is specified it must exist for a successful
    // update.  It is important we null out the whole buffer here because later
    // we make the assumption that the callback application is inside the
    // apply-to dir.  If we don't have a fully null'ed out buffer it can lead
    // to stack corruption which causes crashes and other problems.
    NS_tchar callbackLongPath[MAXPATHLEN];
    ZeroMemory(callbackLongPath, sizeof(callbackLongPath));
    if (!GetLongPathNameW(argv[callbackIndex], callbackLongPath,
                          sizeof(callbackLongPath)/sizeof(callbackLongPath[0]))) {
      LOG(("NS_main: unable to find callback file: " LOG_S "\n", argv[callbackIndex]));
      LogFinish();
      WriteStatusFile(WRITE_ERROR);
      EXIT_WHEN_ELEVATED(elevatedLockFilePath, updateLockFileHandle, 1);
      LaunchCallbackApp(argv[4], 
                        argc - callbackIndex, 
                        argv + callbackIndex, 
                        usingService);
      return 1;
    }

    NS_tchar applyDirLongPath[MAXPATHLEN];
    if (!GetLongPathNameW(argv[2], applyDirLongPath,
                          sizeof(applyDirLongPath)/sizeof(applyDirLongPath[0]))) {
      LOG(("NS_main: unable to find apply to dir: " LOG_S "\n", argv[2]));
      LogFinish();
      WriteStatusFile(WRITE_ERROR);
      EXIT_WHEN_ELEVATED(elevatedLockFilePath, updateLockFileHandle, 1);
      LaunchCallbackApp(argv[4], 
                        argc - callbackIndex, 
                        argv + callbackIndex, 
                        usingService);
      return 1;
    }

    int len = NS_tstrlen(applyDirLongPath);
    NS_tchar *s = callbackLongPath;
    NS_tchar *d = gCallbackRelPath;
    // advance to the apply to directory and advance past the trailing backslash
    // if present.
    s += len;
    if (*s == NS_T('\\'))
      ++s;

    // Copy the string and replace backslashes with forward slashes along the
    // way.
    do {
      if (*s == NS_T('\\'))
        *d = NS_T('/');
      else
        *d = *s;
      ++s;
      ++d;
    } while (*s);
    *d = NS_T('\0');
    ++d;

    // Make a copy of the callback executable so it can be read when patching.
    NS_tsnprintf(gCallbackBackupPath,
                 sizeof(gCallbackBackupPath)/sizeof(gCallbackBackupPath[0]),
                 NS_T("%s" CALLBACK_BACKUP_EXT), argv[callbackIndex]);
    NS_tremove(gCallbackBackupPath);
    CopyFileW(argv[callbackIndex], gCallbackBackupPath, false);

    // Since the process may be signaled as exited by WaitForSingleObject before
    // the release of the executable image try to lock the main executable file
    // multiple times before giving up.
    const int max_retries = 10;
    int retries = 1;
    do {
      // By opening a file handle without FILE_SHARE_READ to the callback
      // executable, the OS will prevent launching the process while it is
      // being updated.
      callbackFile = CreateFileW(argv[callbackIndex],
                                 DELETE | GENERIC_WRITE,
                                 // allow delete, rename, and write
                                 FILE_SHARE_DELETE | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, 0, NULL);
      if (callbackFile != INVALID_HANDLE_VALUE)
        break;

      DWORD lastError = GetLastError();
      LOG(("NS_main: callback app open attempt %d failed. " \
           "File: " LOG_S ". Last error: %d\n", retries, 
           argv[callbackIndex], lastError));

      Sleep(100);
    } while (++retries <= max_retries);

    // CreateFileW will fail if the callback executable is already in use. Since
    // it isn't possible to update write the status file and return.
    if (callbackFile == INVALID_HANDLE_VALUE) {
      LOG(("NS_main: file in use - failed to exclusively open executable " \
           "file: " LOG_S "\n", argv[callbackIndex]));
      LogFinish();
      WriteStatusFile(WRITE_ERROR);
      NS_tremove(gCallbackBackupPath);
      EXIT_WHEN_ELEVATED(elevatedLockFilePath, updateLockFileHandle, 1);
      LaunchCallbackApp(argv[4], 
                        argc - callbackIndex, 
                        argv + callbackIndex,
                        usingService);
      return 1;
    }
  }

  // The directory to move files that are in use to on Windows. This directory
  // will be deleted after the update is finished or on OS reboot using
  // MoveFileEx if it contains files that are in use.
  if (NS_taccess(DELETE_DIR, F_OK)) {
    NS_tmkdir(DELETE_DIR, 0755);
  }
#endif /* XP_WIN */

  // Run update process on a background thread.  ShowProgressUI may return
  // before QuitProgressUI has been called, so wait for UpdateThreadFunc to
  // terminate.
  Thread t;
  if (t.Run(UpdateThreadFunc, new UpdateThreadData(usingService)) == 0) {
    ShowProgressUI();
  }
  t.Join();

#ifdef XP_WIN
  if (argc > callbackIndex) {
    CloseHandle(callbackFile);
    // Remove the copy of the callback executable.
    NS_tremove(gCallbackBackupPath);
  }

  if (_wrmdir(DELETE_DIR)) {
    LOG(("NS_main: unable to remove directory: " LOG_S ", err: %d\n",
         DELETE_DIR, errno));
    // The directory probably couldn't be removed due to it containing files
    // that are in use and will be removed on OS reboot. The call to remove the
    // directory on OS reboot is done after the calls to remove the files so the
    // files are removed first on OS reboot since the directory must be empty
    // for the directory removal to be successful. The MoveFileEx call to remove
    // the directory on OS reboot will fail if the process doesn't have write
    // access to the HKEY_LOCAL_MACHINE registry key but this is ok since the
    // installer / uninstaller will delete the directory along with its contents
    // after an update is applied, on reinstall, and on uninstall.
    if (MoveFileEx(DELETE_DIR, NULL, MOVEFILE_DELAY_UNTIL_REBOOT)) {
      LOG(("NS_main: directory will be removed on OS reboot: " LOG_S "\n",
           DELETE_DIR));
    } else {
      LOG(("NS_main: failed to schedule OS reboot removal of " \
           "directory: " LOG_S "\n", DELETE_DIR));
    }
  }
#endif /* XP_WIN */

  LogFinish();

  if (argc > callbackIndex) {
#if defined(XP_WIN)
    if (gSucceeded) {
      // The service update will only be executed if it is already installed.
      // For first time installs of the service, the install will happen from
      // the PostUpdate process. We do the service update process here 
      // because it's possible we are updating with updater.exe without the 
      // service if the service failed to apply the update. We want to update
      // the service to a newer version in that case. If we are not running
      // through the service, then MOZ_USING_SERVICE will not exist.
      if (!usingService) {
        if (!LaunchWinPostProcess(argv[2], gSourcePath, false, NULL)) {
          LOG(("NS_main: The post update process could not be launched.\n"));
        }
        StartServiceUpdate(argc, argv);
      }
    }
    EXIT_WHEN_ELEVATED(elevatedLockFilePath, updateLockFileHandle, 0);
#endif /* XP_WIN */
#ifdef XP_MACOSX
    if (gSucceeded) {
      LaunchMacPostProcess(argv[callbackIndex]);
    }
#endif /* XP_MACOSX */
    LaunchCallbackApp(argv[4], 
                      argc - callbackIndex, 
                      argv + callbackIndex, 
                      usingService);
  }

  return 0;
}

class ActionList
{
public:
  ActionList() : mFirst(NULL), mLast(NULL), mCount(0) { }
  ~ActionList();

  void Append(Action* action);
  int Prepare();
  int Execute();
  void Finish(int status);

private:
  Action *mFirst;
  Action *mLast;
  int     mCount;
};

ActionList::~ActionList()
{
  Action* a = mFirst;
  while (a) {
    Action *b = a;
    a = a->mNext;
    delete b;
  }
}

void
ActionList::Append(Action *action)
{
  if (mLast)
    mLast->mNext = action;
  else
    mFirst = action;

  mLast = action;
  mCount++;
}

int
ActionList::Prepare()
{
  // If the action list is empty then we should fail in order to signal that
  // something has gone wrong. Otherwise we report success when nothing is
  // actually done. See bug 327140.
  if (mCount == 0) {
    LOG(("empty action list\n"));
    return UNEXPECTED_ERROR;
  }

  Action *a = mFirst;
  int i = 0;
  while (a) {
    int rv = a->Prepare();
    if (rv)
      return rv;

    float percent = float(++i) / float(mCount);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE * percent);

    a = a->mNext;
  }

  return OK;
}

int
ActionList::Execute()
{
  int currentProgress = 0, maxProgress = 0;
  Action *a = mFirst;
  while (a) {
    maxProgress += a->mProgressCost;
    a = a->mNext;
  }

  a = mFirst;
  while (a) {
    int rv = a->Execute();
    if (rv) {
      LOG(("### execution failed\n"));
      return rv;
    }

    currentProgress += a->mProgressCost;
    float percent = float(currentProgress) / float(maxProgress);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE +
                     PROGRESS_EXECUTE_SIZE * percent);

    a = a->mNext;
  }

  return OK;
}

void
ActionList::Finish(int status)
{
  Action *a = mFirst;
  int i = 0;
  while (a) {
    a->Finish(status);

    float percent = float(++i) / float(mCount);
    UpdateProgressUI(PROGRESS_PREPARE_SIZE +
                     PROGRESS_EXECUTE_SIZE +
                     PROGRESS_FINISH_SIZE * percent);

    a = a->mNext;
  }

  if (status == OK)
    gSucceeded = true;
}


#ifdef XP_WIN
int add_dir_entries(const NS_tchar *dirpath, ActionList *list)
{
  int rv = OK;
  WIN32_FIND_DATAW finddata;
  HANDLE hFindFile;
  NS_tchar searchspec[MAXPATHLEN];
  NS_tchar foundpath[MAXPATHLEN];

  NS_tsnprintf(searchspec, sizeof(searchspec)/sizeof(searchspec[0]),
               NS_T("%s*"), dirpath);
  const NS_tchar *pszSpec = get_full_path(searchspec);

  hFindFile = FindFirstFileW(pszSpec, &finddata);
  if (hFindFile != INVALID_HANDLE_VALUE) {
    do {
      // Don't process the current or parent directory.
      if (NS_tstrcmp(finddata.cFileName, NS_T(".")) == 0 ||
          NS_tstrcmp(finddata.cFileName, NS_T("..")) == 0)
        continue;

      NS_tsnprintf(foundpath, sizeof(foundpath)/sizeof(foundpath[0]),
                   NS_T("%s%s"), dirpath, finddata.cFileName);
      if (finddata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        NS_tsnprintf(foundpath, sizeof(foundpath)/sizeof(foundpath[0]),
                     NS_T("%s/"), foundpath);
        // Recurse into the directory.
        rv = add_dir_entries(foundpath, list);
        if (rv) {
          LOG(("add_dir_entries error: " LOG_S ", err: %d\n", foundpath, rv));
          return rv;
        }
      } else {
        // Add the file to be removed to the ActionList.
        NS_tchar *quotedpath = get_quoted_path(foundpath);
        if (!quotedpath)
          return PARSE_ERROR;

        Action *action = new RemoveFile();
        rv = action->Parse(quotedpath);
        if (rv) {
          LOG(("add_dir_entries Parse error on recurse: " LOG_S ", err: %d\n", quotedpath, rv));
          return rv;
        }

        list->Append(action);
      }
    } while (FindNextFileW(hFindFile, &finddata) != 0);

    FindClose(hFindFile);
    {
      // Add the directory to be removed to the ActionList.
      NS_tchar *quotedpath = get_quoted_path(dirpath);
      if (!quotedpath)
        return PARSE_ERROR;

      Action *action = new RemoveDir();
      rv = action->Parse(quotedpath);
      if (rv)
        LOG(("add_dir_entries Parse error on close: " LOG_S ", err: %d\n", quotedpath, rv));
      else
        list->Append(action);
    }
  }

  return rv;
}

#else

int add_dir_entries(const NS_tchar *dirpath, ActionList *list)
{
  int rv = OK;
  FTS *ftsdir;
  FTSENT *ftsdirEntry;
  NS_tchar searchpath[MAXPATHLEN];

  NS_tsnprintf(searchpath, sizeof(searchpath)/sizeof(searchpath[0]), NS_T("%s"),
               dirpath);
  // Remove the trailing slash so the paths don't contain double slashes. The
  // existence of the slash has already been checked in DoUpdate.
  searchpath[NS_tstrlen(searchpath) - 1] = NS_T('\0');
  char* const pathargv[] = {searchpath, NULL};

  // FTS_NOCHDIR is used so relative paths from the destination directory are
  // returned.
  if (!(ftsdir = fts_open(pathargv,
                          FTS_PHYSICAL | FTS_NOSTAT | FTS_XDEV | FTS_NOCHDIR,
                          NULL)))
    return UNEXPECTED_ERROR;

  while ((ftsdirEntry = fts_read(ftsdir)) != NULL) {
    NS_tchar foundpath[MAXPATHLEN];
    NS_tchar *quotedpath;
    Action *action = NULL;

    switch (ftsdirEntry->fts_info) {
      // Filesystem objects that shouldn't be in the application's directories
      case FTS_SL:
      case FTS_SLNONE:
      case FTS_DEFAULT:
        LOG(("add_dir_entries: found a non-standard file: " LOG_S "\n",
             ftsdirEntry->fts_path));
        // Fall through and try to remove as a file

      // Files
      case FTS_F:
      case FTS_NSOK:
        // Add the file to be removed to the ActionList.
        NS_tsnprintf(foundpath, sizeof(foundpath)/sizeof(foundpath[0]),
                     NS_T("%s"), ftsdirEntry->fts_accpath);
        quotedpath = get_quoted_path(foundpath);
        if (!quotedpath) {
          rv = UPDATER_QUOTED_PATH_MEM_ERROR;
          break;
        }
        action = new RemoveFile();
        rv = action->Parse(quotedpath);
        if (!rv)
          list->Append(action);
        break;

      // Directories
      case FTS_DP:
        rv = OK;
        // Add the directory to be removed to the ActionList.
        NS_tsnprintf(foundpath, sizeof(foundpath)/sizeof(foundpath[0]),
                     NS_T("%s/"), ftsdirEntry->fts_accpath);
        quotedpath = get_quoted_path(foundpath);
        if (!quotedpath) {
          rv = UPDATER_QUOTED_PATH_MEM_ERROR;
          break;
        }

        action = new RemoveDir();
        rv = action->Parse(quotedpath);
        if (!rv)
          list->Append(action);
        break;

      // Errors
      case FTS_DNR:
      case FTS_NS:
        // ENOENT is an acceptable error for FTS_DNR and FTS_NS and means that
        // we're racing with ourselves. Though strange, the entry will be
        // removed anyway.
        if (ENOENT == ftsdirEntry->fts_errno) {
          rv = OK;
          break;
        }
        // Fall through

      case FTS_ERR:
        rv = UNEXPECTED_ERROR;
        LOG(("add_dir_entries: fts_read() error: " LOG_S ", err: %d\n",
             ftsdirEntry->fts_path, ftsdirEntry->fts_errno));
        break;

      case FTS_DC:
        rv = UNEXPECTED_ERROR;
        LOG(("add_dir_entries: fts_read() returned FT_DC: " LOG_S "\n",
             ftsdirEntry->fts_path));
        break;

      default:
        // FTS_D is ignored and FTS_DP is used instead (post-order).
        rv = OK;
        break;
    }

    if (rv != OK)
      break;
  }

  fts_close(ftsdir);

  return rv;
}
#endif

static NS_tchar*
GetManifestContents(const NS_tchar *manifest)
{
  AutoFile mfile = NS_tfopen(manifest, NS_T("rb"));
  if (mfile == NULL) {
    LOG(("GetManifestContents: error opening manifest file: " LOG_S "\n", manifest));
    return NULL;
  }

  struct stat ms;
  int rv = fstat(fileno((FILE *)mfile), &ms);
  if (rv) {
    LOG(("GetManifestContents: error stating manifest file: " LOG_S "\n", manifest));
    return NULL;
  }

  char *mbuf = (char *) malloc(ms.st_size + 1);
  if (!mbuf)
    return NULL;

  size_t r = ms.st_size;
  char *rb = mbuf;
  while (r) {
    const size_t count = mmin(SSIZE_MAX, r);
    size_t c = fread(rb, 1, count, mfile);
    if (c != count) {
      LOG(("GetManifestContents: error reading manifest file: " LOG_S "\n", manifest));
      return NULL;
    }

    r -= c;
    rb += c;
  }
  mbuf[ms.st_size] = '\0';
  rb = mbuf;

#ifndef XP_WIN
  return rb;
#else
  NS_tchar *wrb = (NS_tchar *) malloc((ms.st_size + 1) * sizeof(NS_tchar));
  if (!wrb)
    return NULL;

  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, rb, -1, wrb,
                           ms.st_size + 1)) {
    LOG(("GetManifestContents: error converting utf8 to utf16le: %d\n", GetLastError()));
    free(mbuf);
    free(wrb);
    return NULL;
  }
  free(mbuf);

  return wrb;
#endif
}

int AddPreCompleteActions(ActionList *list)
{
  NS_tchar *rb = GetManifestContents(NS_T("precomplete"));
  if (rb == NULL) {
    LOG(("AddPreCompleteActions: error getting contents of precomplete " \
         "manifest\n"));
    // Applications aren't required to have a precomplete manifest yet.
    return OK;
  }

  int rv;
  NS_tchar *line;
  while((line = mstrtok(kNL, &rb)) != 0) {
    // skip comments
    if (*line == NS_T('#'))
      continue;

    NS_tchar *token = mstrtok(kWhitespace, &line);
    if (!token) {
      LOG(("AddPreCompleteActions: token not found in manifest\n"));
      return PARSE_ERROR;
    }

    Action *action = NULL;
    if (NS_tstrcmp(token, NS_T("remove")) == 0) { // rm file
      action = new RemoveFile();
    }
    else if (NS_tstrcmp(token, NS_T("remove-cc")) == 0) { // no longer supported
      continue;
    }
    else if (NS_tstrcmp(token, NS_T("rmdir")) == 0) { // rmdir if  empty
      action = new RemoveDir();
    }
    else {
      LOG(("AddPreCompleteActions: unknown token: " LOG_S "\n", token));
      return PARSE_ERROR;
    }

    if (!action)
      return BAD_ACTION_ERROR;

    rv = action->Parse(line);
    if (rv)
      return rv;

    list->Append(action);
  }

  return OK;
}

int DoUpdate()
{
  NS_tchar manifest[MAXPATHLEN];
  NS_tsnprintf(manifest, sizeof(manifest)/sizeof(manifest[0]),
               NS_T("%s/update.manifest"), gSourcePath);

  // extract the manifest
  int rv = gArchiveReader.ExtractFile("updatev2.manifest", manifest);
  if (rv) {
    rv = gArchiveReader.ExtractFile("update.manifest", manifest);
    if (rv) {
      LOG(("DoUpdate: error extracting manifest file\n"));
      return rv;
    }
  }

  NS_tchar *rb = GetManifestContents(manifest);
  if (rb == NULL) {
    LOG(("DoUpdate: error opening manifest file: " LOG_S "\n", manifest));
    return READ_ERROR;
  }


  ActionList list;
  NS_tchar *line;
  bool isFirstAction = true;

  while((line = mstrtok(kNL, &rb)) != 0) {
    // skip comments
    if (*line == NS_T('#'))
      continue;

    NS_tchar *token = mstrtok(kWhitespace, &line);
    if (!token) {
      LOG(("DoUpdate: token not found in manifest\n"));
      return PARSE_ERROR;
    }

    if (isFirstAction && NS_tstrcmp(token, NS_T("type")) == 0) {
      const NS_tchar *type = mstrtok(kQuote, &line);
      LOG(("UPDATE TYPE " LOG_S "\n", type));
      if (NS_tstrcmp(type, NS_T("complete")) == 0) {
        rv = AddPreCompleteActions(&list);
        if (rv)
          return rv;
      }
      isFirstAction = false;
      continue;
    }

    isFirstAction = false;

    Action *action = NULL;
    if (NS_tstrcmp(token, NS_T("remove")) == 0) { // rm file
      action = new RemoveFile();
    }
    else if (NS_tstrcmp(token, NS_T("rmdir")) == 0) { // rmdir if  empty
      action = new RemoveDir();
    }
    else if (NS_tstrcmp(token, NS_T("rmrfdir")) == 0) { // rmdir recursive
      const NS_tchar *reldirpath = mstrtok(kQuote, &line);
      if (!reldirpath)
        return PARSE_ERROR;

      if (reldirpath[NS_tstrlen(reldirpath) - 1] != NS_T('/'))
        return PARSE_ERROR;

      rv = add_dir_entries(reldirpath, &list);
      if (rv)
        return rv;

      continue;
    }
    else if (NS_tstrcmp(token, NS_T("add")) == 0) {
      action = new AddFile();
    }
    else if (NS_tstrcmp(token, NS_T("patch")) == 0) {
      action = new PatchFile();
    }
    else if (NS_tstrcmp(token, NS_T("add-if")) == 0) { // Add if exists
      action = new AddIfFile();
    }
    else if (NS_tstrcmp(token, NS_T("patch-if")) == 0) { // Patch if exists
      action = new PatchIfFile();
    }
    else if (NS_tstrcmp(token, NS_T("add-cc")) == 0) { // no longer supported
      continue;
    }
    else {
      LOG(("DoUpdate: unknown token: " LOG_S "\n", token));
      return PARSE_ERROR;
    }

    if (!action)
      return BAD_ACTION_ERROR;

    rv = action->Parse(line);
    if (rv)
      return rv;

    list.Append(action);
  }

  rv = list.Prepare();
  if (rv)
    return rv;

  rv = list.Execute();

  list.Finish(rv);
  return rv;
}
