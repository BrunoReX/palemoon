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
 * The Original Code is Mozilla Android code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Michael Wu <mwu@mozilla.com>
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

/*
 * This custom library loading code is only meant to be called
 * during initialization. As a result, it takes no special 
 * precautions to be threadsafe. Any of the library loading functions
 * like mozload should not be available to other code.
 */

#include <jni.h>
#include <android/log.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/limits.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>
#ifdef MOZ_OLD_LINKER
#include <linux/ashmem.h>
#include <endian.h>
#endif
#include "dlfcn.h"
#include "APKOpen.h"
#include <sys/time.h>
#include <sys/resource.h>
#include "Zip.h"
#include "sqlite3.h"
#include "SQLiteBridge.h"
#ifndef MOZ_OLD_LINKER
#include "ElfLoader.h"
#endif

/* Android headers don't define RUSAGE_THREAD */
#ifndef RUSAGE_THREAD
#define RUSAGE_THREAD 1
#endif

enum StartupEvent {
#define mozilla_StartupTimeline_Event(ev, z) ev,
#include "StartupTimeline.h"
#undef mozilla_StartupTimeline_Event
};

static uint64_t *sStartupTimeline;

void StartupTimeline_Record(StartupEvent ev, struct timeval *tm)
{
  sStartupTimeline[ev] = (((uint64_t)tm->tv_sec * 1000000LL) + (uint64_t)tm->tv_usec);
}

static struct mapping_info * lib_mapping = NULL;

NS_EXPORT const struct mapping_info *
getLibraryMapping()
{
  return lib_mapping;
}

#ifdef MOZ_OLD_LINKER
static int
createAshmem(size_t bytes, const char *name)
{
  int fd = open("/" ASHMEM_NAME_DEF, O_RDWR, 0600);
  if (fd < 0)
    return -1;

  char buf[ASHMEM_NAME_LEN];

  strlcpy(buf, name, sizeof(buf));
  /*ret = */ioctl(fd, ASHMEM_SET_NAME, buf);

  if (!ioctl(fd, ASHMEM_SET_SIZE, bytes))
    return fd;

  close(fd);
  return -1;
}
#endif

#define SHELL_WRAPPER0(name) \
typedef void (*name ## _t)(JNIEnv *, jclass); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT void JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc) \
{ \
  f_ ## name(jenv, jc); \
}

#define SHELL_WRAPPER0_WITH_RETURN(name, return_type) \
typedef return_type (*name ## _t)(JNIEnv *, jclass); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT return_type JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc) \
{ \
  return f_ ## name(jenv, jc); \
}

#define SHELL_WRAPPER1(name,type1) \
typedef void (*name ## _t)(JNIEnv *, jclass, type1 one); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT void JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one) \
{ \
  f_ ## name(jenv, jc, one); \
}

#define SHELL_WRAPPER1_WITH_RETURN(name, return_type, type1) \
typedef return_type (*name ## _t)(JNIEnv *, jclass, type1 one); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT return_type JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one) \
{ \
  return f_ ## name(jenv, jc, one); \
}

#define SHELL_WRAPPER2(name,type1,type2) \
typedef void (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT void JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two) \
{ \
  f_ ## name(jenv, jc, one, two); \
}

#define SHELL_WRAPPER2_WITH_RETURN(name, return_type, type1, type2) \
typedef return_type (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT return_type JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two) \
{ \
  return f_ ## name(jenv, jc, one, two); \
}

#define SHELL_WRAPPER3(name,type1,type2,type3) \
typedef void (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT void JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three) \
{ \
  f_ ## name(jenv, jc, one, two, three); \
}

#define SHELL_WRAPPER3_WITH_RETURN(name, return_type, type1, type2, type3) \
typedef return_type (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT return_type JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three) \
{ \
  return f_ ## name(jenv, jc, one, two, three); \
}

#define SHELL_WRAPPER4(name,type1,type2,type3,type4) \
typedef void (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three, type4 four); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT void JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three, type4 four) \
{ \
  f_ ## name(jenv, jc, one, two, three, four); \
}

#define SHELL_WRAPPER4_WITH_RETURN(name, return_type, type1, type2, type3, type4) \
typedef return_type (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three, type4 four); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT return_type JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three, type4 four) \
{ \
  return f_ ## name(jenv, jc, one, two, three, four); \
}

#define SHELL_WRAPPER5(name,type1,type2,type3,type4,type5) \
typedef void (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three, type4 four, type5 five); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT void JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three, type4 four, type5 five) \
{ \
  f_ ## name(jenv, jc, one, two, three, four, five); \
}

#define SHELL_WRAPPER5_WITH_RETURN(name, return_type, type1, type2, type3, type4, type5) \
typedef return_type (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three, type4 four, type5 five); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT return_type JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three, type4 four, type5 five) \
{ \
  return f_ ## name(jenv, jc, one, two, three, four, five); \
}

#define SHELL_WRAPPER6(name,type1,type2,type3,type4,type5,type6) \
typedef void (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT void JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six) \
{ \
  f_ ## name(jenv, jc, one, two, three, four, five, six); \
}

#define SHELL_WRAPPER6_WITH_RETURN(name, return_type, type1, type2, type3, type4, type5, type6) \
typedef return_type (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT return_type JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six) \
{ \
  return f_ ## name(jenv, jc, one, two, three, four, five, six); \
}

#define SHELL_WRAPPER7(name,type1,type2,type3,type4,type5,type6,type7) \
typedef void (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six, type7 seven); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT void JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six, type7 seven) \
{ \
  f_ ## name(jenv, jc, one, two, three, four, five, six, seven); \
}

#define SHELL_WRAPPER7_WITH_RETURN(name, return_type, type1, type2, type3, type4, type5, type6, type7) \
typedef return_type (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six, type7 seven); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT return_type JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six, type7 seven) \
{ \
  return f_ ## name(jenv, jc, one, two, three, four, five, six, seven); \
}

#define SHELL_WRAPPER8(name,type1,type2,type3,type4,type5,type6,type7,type8) \
typedef void (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six, type7 seven, type8 eight); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT void JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six, type7 seven, type8 eight) \
{ \
  f_ ## name(jenv, jc, one, two, three, four, five, six, seven, eight); \
}

#define SHELL_WRAPPER8_WITH_RETURN(name, return_type, type1, type2, type3, type4, type5, type6, type7, type8) \
typedef return_type (*name ## _t)(JNIEnv *, jclass, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six, type7 seven, type8 eight); \
static name ## _t f_ ## name; \
extern "C" NS_EXPORT return_type JNICALL \
Java_org_mozilla_gecko_GeckoAppShell_ ## name(JNIEnv *jenv, jclass jc, type1 one, type2 two, type3 three, type4 four, type5 five, type6 six, type7 seven, type8 eight) \
{ \
  return f_ ## name(jenv, jc, one, two, three, four, five, six, seven, eight); \
}

SHELL_WRAPPER0(nativeInit)
SHELL_WRAPPER1(nativeRun, jstring)
SHELL_WRAPPER1(notifyGeckoOfEvent, jobject)
SHELL_WRAPPER0(processNextNativeEvent)
SHELL_WRAPPER1(setSurfaceView, jobject)
SHELL_WRAPPER1(setSoftwareLayerClient, jobject)
SHELL_WRAPPER0(onResume)
SHELL_WRAPPER0(onLowMemory)
SHELL_WRAPPER3(callObserver, jstring, jstring, jstring)
SHELL_WRAPPER1(removeObserver, jstring)
SHELL_WRAPPER1(onChangeNetworkLinkStatus, jstring)
SHELL_WRAPPER1(reportJavaCrash, jstring)
SHELL_WRAPPER0(executeNextRunnable)
SHELL_WRAPPER1(cameraCallbackBridge, jbyteArray)
SHELL_WRAPPER3(notifyBatteryChange, jdouble, jboolean, jdouble)
SHELL_WRAPPER3(notifySmsReceived, jstring, jstring, jlong)
SHELL_WRAPPER0(bindWidgetTexture)
SHELL_WRAPPER3_WITH_RETURN(saveMessageInSentbox, jint, jstring, jstring, jlong)
SHELL_WRAPPER6(notifySmsSent, jint, jstring, jstring, jlong, jint, jlong)
SHELL_WRAPPER4(notifySmsDelivered, jint, jstring, jstring, jlong)
SHELL_WRAPPER3(notifySmsSendFailed, jint, jint, jlong)
SHELL_WRAPPER7(notifyGetSms, jint, jstring, jstring, jstring, jlong, jint, jlong)
SHELL_WRAPPER3(notifyGetSmsFailed, jint, jint, jlong)
SHELL_WRAPPER3(notifySmsDeleted, jboolean, jint, jlong)
SHELL_WRAPPER3(notifySmsDeleteFailed, jint, jint, jlong)
SHELL_WRAPPER2(notifyNoMessageInList, jint, jlong)
SHELL_WRAPPER8(notifyListCreated, jint, jint, jstring, jstring, jstring, jlong, jint, jlong)
SHELL_WRAPPER7(notifyGotNextMessage, jint, jstring, jstring, jstring, jlong, jint, jlong)
SHELL_WRAPPER3(notifyReadingMessageListFailed, jint, jint, jlong)

static void * xul_handle = NULL;
static void * sqlite_handle = NULL;
#ifdef MOZ_OLD_LINKER
static time_t apk_mtime = 0;
#ifdef DEBUG
extern "C" int extractLibs = 1;
#else
extern "C" int extractLibs = 0;
#endif

static void
extractFile(const char * path, Zip::Stream &s)
{
  uint32_t size = s.GetUncompressedSize();

  struct stat status;
  if (!stat(path, &status) &&
      status.st_size == size &&
      apk_mtime < status.st_mtime)
    return;

  int fd = open(path, O_CREAT | O_NOATIME | O_TRUNC | O_RDWR, S_IRWXU);
  if (fd == -1) {
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Couldn't open %s to decompress library", path);
    return;
  }

  if (ftruncate(fd, size) == -1) {
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Couldn't ftruncate %s to decompress library", path);
    close(fd);
    return;
  }

  void * buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, 0);
  if (buf == (void *)-1) {
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Couldn't mmap decompression buffer");
    close(fd);
    return;
  }

  z_stream strm = {
    next_in: (Bytef *)s.GetBuffer(),
    avail_in: s.GetSize(),
    total_in: 0,

    next_out: (Bytef *)buf,
    avail_out: size,
    total_out: 0
  };

  int ret;
  ret = inflateInit2(&strm, -MAX_WBITS);
  if (ret != Z_OK)
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "inflateInit failed: %s", strm.msg);

  if (inflate(&strm, Z_FINISH) != Z_STREAM_END)
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "inflate failed: %s", strm.msg);

  if (strm.total_out != size)
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "extracted %d, expected %d!", strm.total_out, size);

  ret = inflateEnd(&strm);
  if (ret != Z_OK)
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "inflateEnd failed: %s", strm.msg);

  close(fd);
#ifdef ANDROID_ARM_LINKER
  /* We just extracted data that is going to be executed in the future.
   * We thus need to ensure Instruction and Data cache coherency. */
  cacheflush((unsigned) buf, (unsigned) buf + size, 0);
#endif
  munmap(buf, size);
}
#endif

static void
extractLib(Zip::Stream &s, void * dest)
{
  z_stream strm = {
    next_in: (Bytef *)s.GetBuffer(),
    avail_in: s.GetSize(),
    total_in: 0,

    next_out: (Bytef *)dest,
    avail_out: s.GetUncompressedSize(),
    total_out: 0
  };

  int ret;
  ret = inflateInit2(&strm, -MAX_WBITS);
  if (ret != Z_OK)
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "inflateInit failed: %s", strm.msg);

  ret = inflate(&strm, Z_SYNC_FLUSH);
  if (ret != Z_STREAM_END)
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "inflate failed: %s", strm.msg);

  ret = inflateEnd(&strm);
  if (ret != Z_OK)
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "inflateEnd failed: %s", strm.msg);

  if (strm.total_out != s.GetUncompressedSize())
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "File not fully uncompressed! %d / %d", strm.total_out, s.GetUncompressedSize());
}

static int cache_count = 0;
static struct lib_cache_info *cache_mapping = NULL;

NS_EXPORT const struct lib_cache_info *
getLibraryCache()
{
  return cache_mapping;
}

#ifdef MOZ_OLD_LINKER
static void
ensureLibCache()
{
  if (!cache_mapping)
    cache_mapping = (struct lib_cache_info *)calloc(MAX_LIB_CACHE_ENTRIES,
                                                    sizeof(*cache_mapping));
}

static void
fillLibCache(const char *buf)
{
  ensureLibCache();

  char * str = strdup(buf);
  if (!str)
    return;

  char * saveptr;
  char * nextstr = str;
  do {
    struct lib_cache_info *info = &cache_mapping[cache_count];

    char * name = strtok_r(nextstr, ":", &saveptr);
    if (!name)
      break;
    nextstr = NULL;

    char * fd_str = strtok_r(NULL, ";", &saveptr);
    if (!fd_str)
      break;

    long int fd = strtol(fd_str, NULL, 10);
    if (fd == LONG_MIN || fd == LONG_MAX)
      break;
    strncpy(info->name, name, MAX_LIB_CACHE_NAME_LEN - 1);
    info->fd = fd;
  } while (cache_count++ < MAX_LIB_CACHE_ENTRIES);
  free(str);
}

static int
lookupLibCacheFd(const char *libName)
{
  if (!cache_mapping)
    return -1;

  int count = cache_count;
  while (count--) {
    struct lib_cache_info *info = &cache_mapping[count];
    if (!strcmp(libName, info->name))
      return info->fd;
  }
  return -1;
}

static void
addLibCacheFd(const char *libName, int fd, uint32_t lib_size = 0, void* buffer = NULL)
{
  ensureLibCache();

  struct lib_cache_info *info = &cache_mapping[cache_count++];
  strncpy(info->name, libName, MAX_LIB_CACHE_NAME_LEN - 1);
  info->fd = fd;
  info->lib_size = lib_size;
  info->buffer = buffer;
}

static void * mozload(const char * path, Zip *zip)
{
#ifdef DEBUG
  struct timeval t0, t1;
  gettimeofday(&t0, 0);
#endif

  void *handle;
  Zip::Stream s;
  if (!zip->GetStream(path, &s))
    return NULL;

  if (extractLibs) {
    char fullpath[PATH_MAX];
    snprintf(fullpath, PATH_MAX, "%s/%s", getenv("MOZ_LINKER_CACHE"), path);
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "resolved %s to %s", path, fullpath);
    extractFile(fullpath, s);
    handle = __wrap_dlopen(fullpath, RTLD_LAZY);
    if (!handle)
      __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Couldn't load %s because %s", fullpath, __wrap_dlerror());
#ifdef DEBUG
    gettimeofday(&t1, 0);
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "%s: spent %d", path,
                        (((long long)t1.tv_sec * 1000000LL) +
                          (long long)t1.tv_usec) -
                        (((long long)t0.tv_sec * 1000000LL) +
                          (long long)t0.tv_usec));
#endif
    return handle;
  }

  bool skipLibCache = false;
  int fd;
  void * buf = NULL;
  uint32_t lib_size = s.GetUncompressedSize();
  int cache_fd = 0;
  if (s.GetType() == Zip::Stream::DEFLATE) {
    cache_fd = lookupLibCacheFd(path);
    fd = cache_fd;
    if (fd < 0)
      fd = createAshmem(lib_size, path);
#ifdef DEBUG
    else
      __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Loading %s from cache", path);
#endif
    if (fd < 0) {
      __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Couldn't open " ASHMEM_NAME_DEF ", Error %d, %s, bailing out", errno, strerror(errno));
      return NULL;
    }
    buf = mmap(NULL, lib_size,
               PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, 0);
    if (buf == (void *)-1) {
      __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Couldn't mmap decompression buffer");
      close(fd);
      return NULL;
    }

    if (cache_fd < 0) {
      extractLib(s, buf);
#ifdef ANDROID_ARM_LINKER
      /* We just extracted data that is going to be executed in the future.
       * We thus need to ensure Instruction and Data cache coherency. */
      cacheflush((unsigned) buf, (unsigned) buf + s.GetUncompressedSize(), 0);
#endif
      addLibCacheFd(path, fd, lib_size, buf);
    }

    // preload libxul, to avoid slowly demand-paging it
    if (!strcmp(path, "libxul.so"))
      madvise(buf, s.GetUncompressedSize(), MADV_WILLNEED);
  }

#ifdef DEBUG
  __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Loading %s with len %d (0x%08x)", path, lib_size, lib_size);
#endif

  handle = moz_mapped_dlopen(path, RTLD_LAZY, fd, buf,
                             lib_size, 0);
  if (!handle)
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Couldn't load %s because %s", path, __wrap_dlerror());

  if (buf)
    munmap(buf, lib_size);

#ifdef DEBUG
  gettimeofday(&t1, 0);
  __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "%s: spent %d", path, (((long long)t1.tv_sec * 1000000LL) + (long long)t1.tv_usec) -
               (((long long)t0.tv_sec * 1000000LL) + (long long)t0.tv_usec));
#endif

  return handle;
}
#endif

#ifdef MOZ_CRASHREPORTER
static void *
extractBuf(const char * path, Zip *zip)
{
  Zip::Stream s;
  if (!zip->GetStream(path, &s))
    return NULL;

  void * buf = malloc(s.GetUncompressedSize());
  if (buf == (void *)-1) {
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Couldn't alloc decompression buffer for %s", path);
    return NULL;
  }
  if (s.GetType() == Zip::Stream::DEFLATE)
    extractLib(s, buf);
  else
    memcpy(buf, s.GetBuffer(), s.GetUncompressedSize());

  return buf;
}
#endif

static int mapping_count = 0;
static char *file_ids = NULL;

#define MAX_MAPPING_INFO 32

extern "C" void
report_mapping(char *name, void *base, uint32_t len, uint32_t offset)
{
  if (!file_ids || mapping_count >= MAX_MAPPING_INFO)
    return;

  struct mapping_info *info = &lib_mapping[mapping_count++];
  info->name = strdup(name);
  info->base = (uintptr_t)base;
  info->len = len;
  info->offset = offset;

  char * entry = strstr(file_ids, name);
  if (entry)
    info->file_id = strndup(entry + strlen(name) + 1, 32);
}

#ifdef MOZ_OLD_LINKER
extern "C" void simple_linker_init(void);
#endif

static void
loadGeckoLibs(const char *apkName)
{
  chdir(getenv("GRE_HOME"));

#ifdef MOZ_OLD_LINKER
  struct stat status;
  if (!stat(apkName, &status))
    apk_mtime = status.st_mtime;
#endif

  struct timeval t0, t1;
  gettimeofday(&t0, 0);
  struct rusage usage1;
  getrusage(RUSAGE_THREAD, &usage1);
  
  Zip *zip = new Zip(apkName);

#ifdef MOZ_CRASHREPORTER
  file_ids = (char *)extractBuf("lib.id", zip);
#endif

#ifndef MOZ_OLD_LINKER
  char *file = new char[strlen(apkName) + sizeof("!/libxpcom.so")];
  sprintf(file, "%s!/libxpcom.so", apkName);
  __wrap_dlopen(file, RTLD_GLOBAL | RTLD_LAZY);
  // libxul.so is pulled from libxpcom.so, so we don't need to give the full path
  xul_handle = __wrap_dlopen("libxul.so", RTLD_GLOBAL | RTLD_LAZY);
  delete[] file;
#else
#define MOZLOAD(name) mozload("lib" name ".so", zip)
  MOZLOAD("mozalloc");
  MOZLOAD("nspr4");
  MOZLOAD("plc4");
  MOZLOAD("plds4");
  MOZLOAD("nssutil3");
  MOZLOAD("nss3");
  MOZLOAD("ssl3");
  MOZLOAD("smime3");
  xul_handle = MOZLOAD("xul");
  MOZLOAD("xpcom");
  MOZLOAD("nssckbi");
  MOZLOAD("freebl3");
  MOZLOAD("softokn3");
#undef MOZLOAD
#endif

  delete zip;

#ifdef MOZ_CRASHREPORTER
  free(file_ids);
  file_ids = NULL;
#endif

  if (!xul_handle)
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Couldn't get a handle to libxul!");

#define GETFUNC(name) f_ ## name = (name ## _t) __wrap_dlsym(xul_handle, "Java_org_mozilla_gecko_GeckoAppShell_" #name)
  GETFUNC(nativeInit);
  GETFUNC(nativeRun);
  GETFUNC(notifyGeckoOfEvent);
  GETFUNC(processNextNativeEvent);
  GETFUNC(setSurfaceView);
  GETFUNC(setSoftwareLayerClient);
  GETFUNC(onResume);
  GETFUNC(onLowMemory);
  GETFUNC(callObserver);
  GETFUNC(removeObserver);
  GETFUNC(onChangeNetworkLinkStatus);
  GETFUNC(reportJavaCrash);
  GETFUNC(executeNextRunnable);
  GETFUNC(cameraCallbackBridge);
  GETFUNC(notifyBatteryChange);
  GETFUNC(notifySmsReceived);
  GETFUNC(bindWidgetTexture);
  GETFUNC(saveMessageInSentbox);
  GETFUNC(notifySmsSent);
  GETFUNC(notifySmsDelivered);
  GETFUNC(notifySmsSendFailed);
  GETFUNC(notifyGetSms);
  GETFUNC(notifyGetSmsFailed);
  GETFUNC(notifySmsDeleted);
  GETFUNC(notifySmsDeleteFailed);
  GETFUNC(notifyNoMessageInList);
  GETFUNC(notifyListCreated);
  GETFUNC(notifyGotNextMessage);
  GETFUNC(notifyReadingMessageListFailed);
#undef GETFUNC
  sStartupTimeline = (uint64_t *)__wrap_dlsym(xul_handle, "_ZN7mozilla15StartupTimeline16sStartupTimelineE");
  gettimeofday(&t1, 0);
  struct rusage usage2;
  getrusage(RUSAGE_THREAD, &usage2);
  __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Loaded libs in %dms total, %dms user, %dms system, %d faults",
                      (t1.tv_sec - t0.tv_sec)*1000 + (t1.tv_usec - t0.tv_usec)/1000, 
                      (usage2.ru_utime.tv_sec - usage1.ru_utime.tv_sec)*1000 + (usage2.ru_utime.tv_usec - usage1.ru_utime.tv_usec)/1000,
                      (usage2.ru_stime.tv_sec - usage1.ru_stime.tv_sec)*1000 + (usage2.ru_stime.tv_usec - usage1.ru_stime.tv_usec)/1000,
                      usage2.ru_majflt-usage1.ru_majflt);

  StartupTimeline_Record(LINKER_INITIALIZED, &t0);
  StartupTimeline_Record(LIBRARIES_LOADED, &t1);
}

static void loadSQLiteLibs(const char *apkName)
{
  chdir(getenv("GRE_HOME"));

#ifdef MOZ_OLD_LINKER
  simple_linker_init();

  struct stat status;
  if (!stat(apkName, &status))
    apk_mtime = status.st_mtime;
#endif

  Zip *zip = new Zip(apkName);
  lib_mapping = (struct mapping_info *)calloc(MAX_MAPPING_INFO, sizeof(*lib_mapping));

#ifdef MOZ_CRASHREPORTER
  file_ids = (char *)extractBuf("lib.id", zip);
#endif

#ifndef MOZ_OLD_LINKER
  char *file = new char[strlen(apkName) + sizeof("!/libmozsqlite3.so")];
  sprintf(file, "%s!/libmozsqlite3.so", apkName);
  sqlite_handle = __wrap_dlopen(file, RTLD_GLOBAL | RTLD_LAZY);
  delete [] file;
#else
#define MOZLOAD(name) mozload("lib" name ".so", zip)
  sqlite_handle = MOZLOAD("mozsqlite3");
#undef MOZLOAD
#endif

  delete zip;

#ifdef MOZ_CRASHREPORTER
  free(file_ids);
  file_ids = NULL;
#endif

  if (!sqlite_handle)
    __android_log_print(ANDROID_LOG_ERROR, "GeckoLibLoad", "Couldn't get a handle to libmozsqlite3!");

  setup_sqlite_functions(sqlite_handle);
}

extern "C" NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_loadGeckoLibsNative(JNIEnv *jenv, jclass jGeckoAppShellClass, jstring jApkName)
{
  const char* str;
  // XXX: java doesn't give us true UTF8, we should figure out something
  // better to do here
  str = jenv->GetStringUTFChars(jApkName, NULL);
  if (str == NULL)
    return;

  loadGeckoLibs(str);
  jenv->ReleaseStringUTFChars(jApkName, str);
}

extern "C" NS_EXPORT void JNICALL
Java_org_mozilla_gecko_GeckoAppShell_loadSQLiteLibsNative(JNIEnv *jenv, jclass jGeckoAppShellClass, jstring jApkName, jboolean jShouldExtract) {
  if (jShouldExtract) {
#ifdef MOZ_OLD_LINKER
    extractLibs = 1;
#else
    putenv("MOZ_LINKER_EXTRACT=1");
#endif
  }

  const char* str;
  // XXX: java doesn't give us true UTF8, we should figure out something
  // better to do here
  str = jenv->GetStringUTFChars(jApkName, NULL);
  if (str == NULL)
    return;

  loadSQLiteLibs(str);
  jenv->ReleaseStringUTFChars(jApkName, str);
}

typedef int GeckoProcessType;
typedef int nsresult;

extern "C" NS_EXPORT int
ChildProcessInit(int argc, char* argv[])
{
  int i;
  for (i = 0; i < (argc - 1); i++) {
    if (strcmp(argv[i], "-greomni"))
      continue;

    i = i + 1;
    break;
  }

#ifdef MOZ_OLD_LINKER
  fillLibCache(argv[argc - 1]);
#endif
  loadSQLiteLibs(argv[i]);
  loadGeckoLibs(argv[i]);

  // don't pass the last arg - it's only recognized by the lib cache
  argc--;

  typedef GeckoProcessType (*XRE_StringToChildProcessType_t)(char*);
  typedef nsresult (*XRE_InitChildProcess_t)(int, char**, GeckoProcessType);
  XRE_StringToChildProcessType_t fXRE_StringToChildProcessType =
    (XRE_StringToChildProcessType_t)__wrap_dlsym(xul_handle, "XRE_StringToChildProcessType");
  XRE_InitChildProcess_t fXRE_InitChildProcess =
    (XRE_InitChildProcess_t)__wrap_dlsym(xul_handle, "XRE_InitChildProcess");

  GeckoProcessType proctype = fXRE_StringToChildProcessType(argv[--argc]);

  nsresult rv = fXRE_InitChildProcess(argc, argv, proctype);
  if (rv != 0)
    return 1;

  return 0;
}

