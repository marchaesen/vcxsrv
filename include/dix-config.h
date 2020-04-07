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
#define COMPILEDDEFAULTFONTPATH "./fonts/misc/,./fonts/TTF/,./fonts/OTF,./fonts/Type1/,./fonts/100dpi/,./fonts/75dpi/,./fonts/cyrillic/,./fonts/Speedo/,./fonts/terminus-font/,built-ins"

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
#undef DRI3

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

/* Has backtrace support */
#undef HAVE_BACKTRACE

/* Has libunwind support */
#undef HAVE_LIBUNWIND

/* Define to 1 if you have the `cbrt' function. */
#undef HAVE_CBRT

/* Define to 1 if you have the declaration of `program_invocation_short_name', and
   to 0 if you don't. */
#undef HAVE_DECL_PROGRAM_INVOCATION_SHORT_NAME

/* Define to 1 if you have the <dirent.h> header file, and it defines `DIR'.
   */
#define HAVE_DIRENT_H 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Have execinfo.h */
#undef HAVE_EXECINFO_H

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the `getdtablesize' function. */
#define HAVE_GETDTABLESIZE 1

/* Define to 1 if you have the `getifaddrs' function. */
#undef HAVE_GETIFADDRS

/* Define to 1 if you have the `getpeereid' function. */
#undef HAVE_GETPEEREID

/* Define to 1 if you have the `getpeerucred' function. */
#undef HAVE_GETPEERUCRED

/* Define to 1 if you have the `getprogname' function. */
#undef HAVE_GETPROGNAME

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

/* Define to 1 if you have the `bsd' library (-lbsd). */
#undef HAVE_LIBBSD

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

/* Define to 1 if you have the `mkostemp' function. */
#undef HAVE_MKOSTEMP

/* Define to 1 if you have the `mmap' function. */
#undef HAVE_MMAP

/* Define to 1 if you have the function pthread_setname_np(const char*) */
#undef HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID

/* Define to 1 if you have the function pthread_setname_np(pthread_t, const char*) */
#undef HAVE_PTHREAD_SETNAME_NP_WITH_TID

/* Define to 1 if you have the <ndir.h> header file, and it defines `DIR'. */
#undef HAVE_NDIR_H

/* Define to 1 if you have the `reallocarray' function. */
#undef HAVE_REALLOCARRAY

/* Define to 1 if you have the `arc4random_buf' function. */
#undef HAVE_ARC4RANDOM_BUF

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

/* Define to 1 if libsystemd-daemon is available */
#undef HAVE_SYSTEMD_DAEMON

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

/* Define to 1 if you have the `timingsafe_memcmp' function. */
#undef HAVE_TIMINGSAFE_MEMCMP

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

/* Where to install Xorg.bin and Xorg.wrap */
#undef SUID_WRAPPER_DIR

/* Define to 1 on systems derived from System V Release 4 */
#undef SVR4

/* sysconfdir */
#undef SYSCONFDIR

/* Support TCP socket connections */
#define TCPCONN 1

/* Support UNIX socket connections */
#undef UNIXCONN

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
#define XORG_VERSION_CURRENT (((1) * 10000000) + ((20) * 100000) + ((8) * 1000) + 1)

/* Xorg release date */
#define XORG_DATE "7 April 2020"

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

/* Number of bits in a file offset, on hosts where this is settable. */
#undef _FILE_OFFSET_BITS

/* Enable GNU and other extensions to the C environment for GLIBC */
#undef _GNU_SOURCE

/* Define for large files, on AIX-style hosts. */
#undef _LARGE_FILES

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

/* Enable D-Bus core */
#undef NEED_DBUS

/* Support HAL for hotplug */
#undef CONFIG_HAL

/* Enable systemd-logind integration */
#undef SYSTEMD_LOGIND

/* Have a monotonic clock from clock_gettime() */
#undef MONOTONIC_CLOCK

/* Define to 1 if the DTrace Xserver provider probes should be built in */
/*#define XSERVER_DTRACE*/

/* Define to 1 if typeof works with your compiler. */
#undef HAVE_TYPEOF

/* Define to __typeof__ if your compiler spells it that way. */
#undef typeof

/* Correctly set _XSERVER64 for OSX fat binaries */
#ifdef __APPLE__
#include "dix-config-apple-verbatim.h"
#endif

/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# undef __EXTENSIONS__
#endif

/* Defined if needed to expose struct msghdr.msg_control */
#undef _XOPEN_SOURCE

/* Have support for X shared memory fence library (xshmfence) */
#undef HAVE_XSHMFENCE

/* Use XTrans FD passing support */
#undef XTRANS_SEND_FDS

/* Wrap SIGBUS to catch MIT-SHM faults */
#undef BUSFAULT

/* Directory for shared memory temp files */
#undef SHMDIR

/* Don't let Xdefs.h define 'pointer' */
#undef _XTYPEDEF_POINTER

/* Don't let XIproto define 'Pointer' */
#undef _XITYPEDEF_POINTER

/* Ask fontsproto to make font path element names const */
#define FONT_PATH_ELEMENT_NAME_CONST    1

/* Build GLAMOR */
#undef GLAMOR

/* Build glamor's GBM-based EGL support */
#undef GLAMOR_HAS_GBM

/* Build glamor/gbm has linear support */
#undef GLAMOR_HAS_GBM_LINEAR

/* GBM has modifiers support */
#undef GBM_BO_WITH_MODIFIERS

/* Glamor can use eglQueryDmaBuf* functions */
#undef GLAMOR_HAS_EGL_QUERY_DMABUF

/* byte order */
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

/* Listen on TCP socket */
#define LISTEN_TCP 1

/* Listen on Unix socket */
#undef LISTEN_UNIX

/* Listen on local socket */
#undef LISTEN_LOCAL

/* Define if no local socket credentials interface exists */
#define NO_LOCAL_CLIENT_CRED 1

/* Have setitimer support */
#define HAVE_SETITIMER 1

/* Have posix_fallocate() */
#undef HAVE_POSIX_FALLOCATE

/* Use input thread */
#undef INPUTTHREAD

/* Have poll() */
#undef HAVE_POLL

/* Have epoll_create1() */
#undef HAVE_EPOLL_CREATE1

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

#define HAVE_STRUCT_TIMESPEC

#endif /* _DIX_CONFIG_H_ */
