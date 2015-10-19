/* src/config.h.in.  Generated from configure.ac by autoheader.  */

/* Define to 1 if `struct sockaddr_in' has a `sin_len' member */
/* #undef BSD44SOCKETS */

/* Include compose table cache support */
#define COMPOSECACHE 1

/* Has getresuid() & getresgid() functions */
/* #undef HASGETRESUID */

/* Has issetugid() function */
/* #undef HASSETUGID */

/* Has shm*() functions */
//MH#define HAS_SHM 1

/* Define to 1 if you have the `authdes_create' function. */
/* #undef HAVE_AUTHDES_CREATE */

/* Define to 1 if you have the `authdes_seccreate' function. */
/* #undef HAVE_AUTHDES_SECCREATE */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the <endian.h> header file. */
#define HAVE_ENDIAN_H 1

/* Use dlopen to load shared libraries */
#define HAVE_DLOPEN 1

/* Define to 1 if you have the <dl.h> header file. */
/* #undef HAVE_DL_H */

/* Define to 1 if you have the `getpagesize' function. */
#define HAVE_GETPAGESIZE 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* launchd support available */
/* #undef HAVE_LAUNCHD */

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `poll' function. */
#define HAVE_POLL 1

/* Define to 1 if you have a working `mmap' system call. */
#define HAVE_MMAP 1

/* Use shl_load to load shared libraries */
/* #undef HAVE_SHL_LOAD */

/* Define to 1 if the system has the type `socklen_t'. */
#define HAVE_SOCKLEN_T 0

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/poll.h> header file. */
#define HAVE_SYS_POLL_H 1

/* Define to 1 if you have the `strtol' function. */
#define HAVE_STRTOL 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Support IPv6 for TCP connections */
/* #undef IPv6 */

/* Support dynamically loaded font modules */
#define LOADABLEFONTS 1

/* Support os-specific local connections */
/* #undef LOCALCONN */

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* Disable XLOCALEDIR environment variable */
#define NO_XLOCALEDIR 1

/* Name of package */
#define PACKAGE "libX11"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "https://bugs.freedesktop.org/enter_bug.cgi?product=xorg"

/* Define to the full name of this package. */
#define PACKAGE_NAME "libX11"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "libX11 1.1.5"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "libX11"

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.1.5"

/* Major version of this package */
#define PACKAGE_VERSION_MAJOR 1

/* Minor version of this package */
#define PACKAGE_VERSION_MINOR 1

/* Patch version of this package */
#define PACKAGE_VERSION_PATCHLEVEL 5

/* Define as the return type of signal handlers (`int' or `void'). */
/* #undef RETSIGTYPE */

/* Support Secure RPC ("SUN-DES-1") authentication for X11 clients */
/* #undef SECURE_RPC */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Support TCP socket connections */
#define TCPCONN 1

/* launchd support available */
/* #undef TRANS_REOPEN */

/* Support UNIX socket connections */
#define UNIXCONN 1

/* Split some i18n functions into loadable modules */
/* #undef USE_DYNAMIC_LC */

/* Use the X cursor library to load cursors */
#define USE_DYNAMIC_XCURSOR 1

/* poll() function is available */
#define USE_POLL 1

/* Use XCB for low-level protocol implementation */
#define USE_XCB 1

/* Version number of package */
#define VERSION "1.1.5"

/* Support bdf format bitmap font files */
#define XFONT_BDFFORMAT 1

/* Location of libX11 data */
#define X11_DATADIR "/usr/share/X11"

/* Location of libX11 library data */
#define X11_LIBDIR "/usr/lib/X11"

/* Include support for XCMS */
#define XCMS 1

/* Location of error message database */
#define XERRORDB "XErrorDB"

/* Enable XF86BIGFONT extension */
/* #undef XF86BIGFONT */

/* Use XKB */
#define XKB 1

/* Location of keysym database */
#define XKEYSYMDB "XKeysymDB"

/* support for X Locales */
#define XLOCALE 1

/* Location of libX11 locale data */
#define XLOCALEDATADIR "locale"

/* Location of libX11 locale data */
#define XLOCALEDIR "locale"

/* Location of libX11 locale libraries */
#define XLOCALELIBDIR "locale"

/* Whether libX11 is compiled with thread support */
#define XTHREADS /**/

/* Whether libX11 needs to use MT safe API's */
#define XUSE_MTSAFE_API /**/

/* Enable GNU and other extensions to the C environment for glibc */
/* #undef _GNU_SOURCE */

/* Support bitmap font files */
#define XFONT_BITMAP 1

/* Support built-in fonts */
#define XFONT_BUILTINS 1

/* Support the X Font Services Protocol */
#define XFONT_FC 1

/* Support fonts in files */
#define XFONT_FONTFILE 1

/* Support FreeType rasterizer for nearly all font file formats */
#define XFONT_FREETYPE 1

/* Support pcf format bitmap font files */
#define XFONT_PCFFORMAT 1

/* Support snf format bitmap font files */
#define XFONT_SNFFORMAT 1

/* Support Speedo font files */
#define XFONT_SPEEDO 1

/* Support IBM Type 1 rasterizer for Type1 font files */
#define XFONT_TYPE1 1

/* Support bzip2 for bitmap fonts */
/* #undef X_BZIP2_FONT_COMPRESSION */

/* Support gzip for bitmap fonts */
#define X_GZIP_FONT_COMPRESSION 1

#define HAVE_STRUCT_TIMESPEC

#define INCL_WINSOCK_API_TYPEDEFS 1
#include <X11/Xwinsock.h>
#include <X11/Xwindows.h>

#include <stdint.h>

