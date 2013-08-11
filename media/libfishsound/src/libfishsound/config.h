/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Do not build decoding support */
#define FS_DECODE 1

/* Do not build encoding support */
#define FS_ENCODE 0

/* Define to build experimental code */
/* #undef FS_EXPERIMENTAL */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have libFLAC */
#define HAVE_FLAC 0

/* Define to 1 if you have libFLAC 1.1.2 */
/* #undef HAVE_FLAC_1_1_2 */

/* Define to 1 if you have libFLAC 1.1.3 */
/* #undef HAVE_FLAC_1_1_3 */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define if have libsndfile */
/* #undef HAVE_LIBSNDFILE1 */

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define if have liboggz */
/* #undef HAVE_OGGZ */

/* Define to 1 if you have libspeex */
#define HAVE_SPEEX 0

/* Define to 1 if you have libspeex 1.1.x */
/* #undef HAVE_SPEEX_1_1 */

/* Define to 1 if speex_lib_get_mode() exists in libspeex */
/* #undef HAVE_SPEEX_LIB_GET_MODE */

/* Define to 1 if you have the <stdint.h> header file. */
/* #define HAVE_STDINT_H 1 */

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if the system has the type `uintptr_t'. */
/* #define HAVE_UINTPTR_T 1 */

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have libvorbis */
#define HAVE_VORBIS 1

/* Define to 1 if you have libvorbisenc */
#define HAVE_VORBISENC 0

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* Name of package */
#define PACKAGE "libfishsound"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT ""

/* Define to the full name of this package. */
#define PACKAGE_NAME ""

/* Define to the full name and version of this package. */
#define PACKAGE_STRING ""

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME ""

/* Define to the version of this package. */
#define PACKAGE_VERSION ""

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Version number of package */
#define VERSION "0.9.2"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */
#undef FS_ENCODE
#define FS_ENCODE 0
#undef HAVE_FLAC
#define HAVE_FLAC 0
#undef HAVE_OGGZ
#define HAVE_OGGZ 1
#undef HAVE_SPEEX
#define HAVE_SPEEX 0
#undef HAVE_VORBIS
#define HAVE_VORBIS 1
#undef HAVE_VORBISENC
#define HAVE_VORBISENC 0
#undef DEBUG

#include "prcpucfg.h"
#ifdef IS_BIG_ENDIAN
#define WORDS_BIGENDIAN
#endif
