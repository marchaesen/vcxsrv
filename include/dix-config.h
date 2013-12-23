/* dix-config.h.in: not at all generated.                      -*- c -*- */

#ifndef _DIX_CONFIG_H_
#define _DIX_CONFIG_H_

#define _X_UNUSED

#define GLYPHPADBYTES 4

/* Use XCB for low-level protocol implementation */
#define USE_XCB 1

/* Support BigRequests extension */
#define BIGREQS 1

/* Builder address */
#define BUILDERADDR "marha@users.sourceforge.net"

/* Operating System Name */
#define OSNAME "Win32"

/* Operating System Vendor */
#define OSVENDOR "Microsoft"

/* Builder string */
#define BUILDERSTRING ""

/* Default font path */
#define COMPILEDDEFAULTFONTPATH "fonts/misc/,fonts/TTF/,fonts/OTF,fonts/Type1/,fonts/100dpi/,fonts/75dpi/,fonts/cyrillic/,fonts/Speedo/,fonts/terminus-font/,built-ins"

/* Miscellaneous server configuration files path */
#define SERVER_MISC_CONFIG_PATH "."

/* Support Composite Extension */
#define COMPOSITE 1

/* Support Damage extension */
#define DAMAGE 1

/* Use OsVendorVErrorF */
#define DDXOSVERRORF 1

/* Use ddxBeforeReset */
#define DDXBEFORERESET 1

/* Build DPMS extension */
#define DPMSExtension 1

/* Build DRI3 extension */
/* #undef DRI3 */

/* Build GLX extension */
#define GLXEXT

/* Build GLX DRI loader */
#undef GLX_DRI

/* Path to DRI drivers */
#define DRI_DRIVER_PATH ""

/* Support XDM-AUTH*-1 */
#define HASXDMAUTH 1

/* Support SHM */
#undef HAS_SHM

/* Define to 1 if you have <alloca.h> and it should be used (not on Ultrix).
   */
#define HAVE_ALLOCA_H 1

/* Define to 1 if you have the <asm/mtrr.h> header file. */
#undef HAVE_ASM_MTRR_H

/* Has backtrace support */
#undef HAVE_BACKTRACE

/* Has libunwind support */
/* #undef HAVE_LIBUNWIND */

/* Define to 1 if you have the <byteswap.h> header file. */
#define HAVE_BYTESWAP_H 1

/* Define to 1 if you have the `cbrt' function. */
#undef HAVE_CBRT

/* Define to 1 if you have the <dbm.h> header file. */
#undef HAVE_DBM_H

/* Define to 1 if you have the <dirent.h> header file, and it defines `DIR'.
   */
#define HAVE_DIRENT_H 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Have execinfo.h */
#undef HAVE_EXECINFO_H

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the `ffs' function. */
#undef HAVE_FFS

/* Define to 1 if you have the `getdtablesize' function. */
#define HAVE_GETDTABLESIZE 1

/* Define to 1 if you have the `getifaddrs' function. */
#undef HAVE_GETIFADDRS

/* Define to 1 if you have the `getpeereid' function. */
#undef HAVE_GETPEEREID

/* Define to 1 if you have the `getpeerucred' function. */
#undef HAVE_GETPEERUCRED

/* Define to 1 if you have the `getzoneid' function. */
#undef HAVE_GETZONEID

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Have Quartz */
#undef XQUARTZ

/* Support application updating through sparkle. */
#undef XQUARTZ_SPARKLE

/* Prefix to use for bundle identifiers */
#undef BUNDLE_ID_PREFIX

/* Build a standalone xpbproxy */
#undef STANDALONE_XPBPROXY

/* Define to 1 if you have the `m' library (-lm). */
#define HAVE_LIBM 1

/* Define to 1 if you have the libdispatch (GCD) available */
#undef HAVE_LIBDISPATCH

/* Define to 1 if you have the <linux/agpgart.h> header file. */
#undef HAVE_LINUX_AGPGART_H

/* Define to 1 if you have the <linux/apm_bios.h> header file. */
#undef HAVE_LINUX_APM_BIOS_H

/* Define to 1 if you have the <linux/fb.h> header file. */
#undef HAVE_LINUX_FB_H

/* Define to 1 if you have the `mmap' function. */
#undef HAVE_MMAP

/* Define to 1 if you have the <ndbm.h> header file. */
#undef HAVE_NDBM_H

/* Define to 1 if you have the <ndir.h> header file, and it defines `DIR'. */
#undef HAVE_NDIR_H

/* Define to 1 if you have the <rpcsvc/dbm.h> header file. */
#undef HAVE_RPCSVC_DBM_H

/* Define to use libc SHA1 functions */
#undef HAVE_SHA1_IN_LIBC

/* Define to use CommonCrypto SHA1 functions */
#undef HAVE_SHA1_IN_COMMONCRYPTO

/* Define to use CryptoAPI SHA1 functions */
#undef HAVE_SHA1_IN_CRYPTOAPI

/* Define to use libmd SHA1 functions */
#undef HAVE_SHA1_IN_LIBMD

/* Define to use libgcrypt SHA1 functions */
#undef HAVE_SHA1_IN_LIBGCRYPT

/* Define to use libnettle SHA1 functions */
#undef HAVE_SHA1_IN_LIBNETTLE

/* Define to use libsha1 for SHA1 */
#undef HAVE_SHA1_IN_LIBSHA1

/* Define to 1 if you have the `shmctl64' function. */
#undef HAVE_SHMCTL64

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strcasecmp' function. */
#define HAVE_STRCASECMP 1

/* Define to 1 if you have the `strcasestr' function. */
#undef HAVE_STRCASESTR

/* Define to 1 if you have the `strncasecmp' function. */
#define HAVE_STRNCASECMP 1

/* Define to 1 if you have the `strlcat' function. */
#undef HAVE_STRLCAT

/* Define to 1 if you have the `strlcpy' function. */
#undef HAVE_STRLCPY

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strndup' function. */
#undef HAVE_STRNDUP

/* Define to 1 if SYSV IPC is available */
#undef HAVE_SYSV_IPC

/* Define to 1 if you have the <sys/agpio.h> header file. */
#undef HAVE_SYS_AGPIO_H

/* Define to 1 if you have the <sys/dir.h> header file, and it defines `DIR'.
   */
#undef HAVE_SYS_DIR_H

/* Define to 1 if you have the <sys/ndir.h> header file, and it defines `DIR'.
   */
#undef HAVE_SYS_NDIR_H

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/utsname.h> header file. */
#undef HAVE_SYS_UTSNAME_H

/* Define to 1 if you have the <tslib.h> header file. */
#undef HAVE_TSLIB_H

/* Define to 1 if you have the <unistd.h> header file. */
#undef HAVE_UNISTD_H

/* Define to 1 if you have the <fnmatch.h> header file. */
#undef HAVE_FNMATCH_H

/* Have /dev/urandom */
#undef HAVE_URANDOM

/* Define to 1 if you have the `vasprintf' function. */
#undef HAVE_VASPRINTF

/* Support IPv6 for TCP connections */
#define IPv6 1

/* Support os-specific local connections */
#undef LOCALCONN

/* Support MIT-SHM Extension */
#undef MITSHM

/* Disable some debugging code */
#define NDEBUG 1

/* Enable some debugging code */
#undef DEBUG

/* Name of package */
#define PACKAGE "xorg-server"

/* Internal define for Xinerama */
#define PANORAMIX 1

/* Support Present extension */
#define PRESENT 1

/* Overall prefix */
#define PROJECTROOT "."

/* Support RANDR extension */
#define RANDR 1

/* Support Record extension */
#define XRECORD 1

/* Support RENDER extension */
#define RENDER 1

/* Support X resource extension */
#define RES 1

/* Support client ID tracking in X resource extension */
#define CLIENTIDS 1

/* Support MIT-SCREEN-SAVER extension */
#define SCREENSAVER 1

/* Support Secure RPC ("SUN-DES-1") authentication for X11 clients */
#undef SECURE_RPC

/* Support SHAPE extension */
#define SHAPE 1

/* Define to 1 on systems derived from System V Release 4 */
#undef SVR4

/* Support TCP socket connections */
#define TCPCONN 1

/* Enable touchscreen support */
#undef TOUCHSCREEN

/* Support tslib touchscreen abstraction library */
#undef TSLIB

/* Support UNIX socket connections */
#undef UNIXCONN

/* Define to use byteswap macros from <sys/endian.h> */
#undef USE_SYS_ENDIAN_H

/* unaligned word accesses behave as expected */
#undef WORKING_UNALIGNED_INT

/* Build X string registry */
#define XREGISTRY 1

/* Build X-ACE extension */
#define XACE 1

/* Build SELinux extension */
#undef XSELINUX

/* Support XCMisc extension */
#define XCMISC 1

/* Build Security extension */
#define XCSECURITY 1

/* Support Xdmcp */
#define XDMCP 1

/* Build XFree86 BigFont extension */
#define XF86BIGFONT 1

/* Support XFree86 Video Mode extension */
#undef XF86VIDMODE

/* Support XFixes extension */
#define XFIXES 1

/* Build XDGA support */
#undef XFreeXDGA

/* Support Xinerama extension */
#define XINERAMA 1

/* Support X Input extension */
#define XINPUT 1

/* Build XKB */
#define XKB 1

/* Vendor release */
#undef XORG_RELEASE

/* Current Xorg version */
#define XORG_VERSION_CURRENT (((1) * 10000000) + ((14) * 100000) + ((5) * 1000) + 0)

/* Xorg release date */
#define XORG_DATE "10 Sept 2009"

/* Build Xv Extension */
#undef XvExtension

/* Build XvMC Extension */
#undef XvMCExtension

/* Support XSync extension */
#define XSYNC 1

/* Support XTest extension */
#define XTEST 1

/* Support Xv extension */
#undef XV

/* Support DRI extension */
#undef XF86DRI

/* Build DRI2 extension */
#undef DRI2

/* Build DBE support */
#define DBE 1

/* Vendor name */
#define XVENDORNAME "The VcXsrv Project"

/* Endian order */
#ifndef X_BYTE_ORDER

#define _X_BYTE_ORDER X_LITTLE_ENDIAN
/* Deal with multiple architecture compiles on Mac OS X */
#ifndef __APPLE_CC__
#define X_BYTE_ORDER _X_BYTE_ORDER
#else
#ifdef __BIG_ENDIAN__
#define X_BYTE_ORDER X_BIG_ENDIAN
#else
#define X_BYTE_ORDER X_LITTLE_ENDIAN
#endif
#endif
#endif

/* Enable GNU and other extensions to the C environment for GLIBC */
#undef _GNU_SOURCE

/* Define to empty if `const' does not conform to ANSI C. */
#undef const

/* Define to `int' if <sys/types.h> does not define. */
#undef pid_t

/* Build Rootless code */
#define ROOTLESS 1

/* Define to 1 if unsigned long is 64 bits. */
#undef _XSERVER64

/* System is BSD-like */
#undef CSRG_BASED

/* Define to 1 if `struct sockaddr_in' has a `sin_len' member */
#undef BSD44SOCKETS

/* Support D-Bus */
#undef HAVE_DBUS

/* Use libudev for input hotplug */
#undef CONFIG_UDEV

/* Use libudev for kms enumeration */
#undef CONFIG_UDEV_KMS

/* Use udev_monitor_filter_add_match_tag() */
#undef HAVE_UDEV_MONITOR_FILTER_ADD_MATCH_TAG

/* Use udev_enumerate_add_match_tag() */
#undef HAVE_UDEV_ENUMERATE_ADD_MATCH_TAG

/* Use D-Bus for input hotplug */
#undef CONFIG_NEED_DBUS

/* Support the D-Bus hotplug API */
#undef CONFIG_DBUS_API

/* Support HAL for hotplug */
#undef CONFIG_HAL

/* Have a monotonic clock from clock_gettime() */
#undef MONOTONIC_CLOCK

/* Define to 1 if the DTrace Xserver provider probes should be built in */
/* #undef XSERVER_DTRACE */

/* Define to 16-bit byteswap macro */
#undef bswap_16

/* Define to 32-bit byteswap macro */
#undef bswap_32

/* Define to 64-bit byteswap macro */
#undef bswap_64

/* Define to 1 if typeof works with your compiler. */
#undef HAVE_TYPEOF

/* Define to __typeof__ if your compiler spells it that way. */
#undef typeof

/* The compiler supported TLS storage class, prefering initial-exec if tls_model is supported */
#undef TLS

/* Correctly set _XSERVER64 for OSX fat binaries */
#ifdef __APPLE__
#include "dix-config-apple-verbatim.h"
#endif

/* Have support for X shared memory fence library (xshmfence) */
/* #undef HAVE_XSHMFENCE */

#undef HAVE_AVC_NETLINK_ACQUIRE_FD

#include <X11/Xwinsock.h>
#include <X11/Xwindows.h>
#if NTDDI_VERSION < NTDDI_VISTA
int inet_pton(int af, const char *src, void *dst);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt);
#endif
#include <assert.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#undef MINSHORT
#undef MAXSHORT

#define MINSHORT -32768
#define MAXSHORT 32767

#endif /* _DIX_CONFIG_H_ */
