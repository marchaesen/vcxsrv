dnl Process this file with autoconf to produce a configure script.

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([libxcb],[1.12],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xcb],
        [libxcb])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([xcb.pc.in])
AC_CONFIG_HEADERS([src/config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_PATH_PYTHON([2.6])

# Set common system defines for POSIX extensions, such as _GNU_SOURCE
# Must be called before any macros that run the compiler (like AC_PROG_LIBTOOL)
# to avoid autoconf errors.
AC_USE_SYSTEM_EXTENSIONS

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT([win32-dll])

# Require xorg-macros minimum of 1.18 - Initial version
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.18 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.18)
XORG_DEFAULT_OPTIONS
XORG_ENABLE_DEVEL_DOCS
XORG_WITH_DOXYGEN

# The dot drawing tool is checked by XORG_WITH_DOXYGEN, needed for xcb manual
if test x"$HAVE_DOT" = xno; then
    AC_MSG_WARN([dot not found - doxygen targets will be skipped])
fi

PKG_CHECK_MODULES(CHECK, [check >= 0.9.4], [HAVE_CHECK=yes], [HAVE_CHECK=no])
AM_CONDITIONAL(HAVE_CHECK, test x$HAVE_CHECK = xyes)

XSLTPROC=no
HTML_CHECK_RESULT=no
if test x"$HAVE_CHECK" = xyes; then
        AC_PATH_PROG(XSLTPROC, xsltproc, no)
        if test x"$XSLTPROC" != xno; then
                HTML_CHECK_RESULT=yes
        fi
fi
AC_SUBST(HTML_CHECK_RESULT)

# Checks for pkg-config packages
PKG_CHECK_MODULES(XCBPROTO, xcb-proto >= 1.12)
NEEDED="pthread-stubs xau >= 0.99.2"
PKG_CHECK_MODULES(NEEDED, $NEEDED)

have_xdmcp="no"
PKG_CHECK_MODULES(XDMCP, xdmcp,
        AC_CHECK_LIB(Xdmcp, XdmcpWrap,
                [
                AC_DEFINE(HASXDMAUTH,1,[Has Wraphelp.c needed for XDM AUTH protocols])
                NEEDED="$NEEDED xdmcp"
                have_xdmcp="yes"
                ],
                [
                XDMCP_CFLAGS=
                XDMCP_LIBS=
                ], [$XDMCP_LIBS]),
        [AC_MSG_RESULT(no)])

AC_SUBST(NEEDED)

# Find the xcb-proto protocol descriptions
AC_MSG_CHECKING(XCBPROTO_XCBINCLUDEDIR)
XCBPROTO_XCBINCLUDEDIR=`$PKG_CONFIG --variable=xcbincludedir xcb-proto`
AC_MSG_RESULT($XCBPROTO_XCBINCLUDEDIR)
AC_SUBST(XCBPROTO_XCBINCLUDEDIR)

# Find the xcb-proto version
XCBPROTO_VERSION=`$PKG_CONFIG --modversion xcb-proto`
AC_SUBST(XCBPROTO_VERSION)

# Find the xcbgen Python package
AC_MSG_CHECKING(XCBPROTO_XCBPYTHONDIR)
XCBPROTO_XCBPYTHONDIR=`$PKG_CONFIG --variable=pythondir xcb-proto`
AC_MSG_RESULT($XCBPROTO_XCBPYTHONDIR)
AC_SUBST(XCBPROTO_XCBPYTHONDIR)

AC_HEADER_STDC
AC_SEARCH_LIBS(getaddrinfo, socket)
AC_SEARCH_LIBS(connect, socket)

# Find support for sending a message from a socket
AC_SEARCH_LIBS(sendmsg, socket, [have_sendmsg="yes"], [have_sendmsg="no"])

# XPG4v2/UNIX95 added msg_control - check to see if we need to define
# _XOPEN_SOURCE to get it (such as on Solaris)
AC_CHECK_MEMBER([struct msghdr.msg_control], [], [],
                 [
AC_INCLUDES_DEFAULT
#include <sys/socket.h>
                 ])
# First try for Solaris in C99 compliant mode, which requires XPG6/UNIX03
if test "x$ac_cv_member_struct_msghdr_msg_control" = xno; then
    unset ac_cv_member_struct_msghdr_msg_control
    AC_MSG_NOTICE([trying again with _XOPEN_SOURCE=600])
    AC_CHECK_MEMBER([struct msghdr.msg_control],
                    [AC_DEFINE([_XOPEN_SOURCE], [600],
                       [Defined if needed to expose struct msghdr.msg_control])
                    ], [], [
#define _XOPEN_SOURCE 600
AC_INCLUDES_DEFAULT
#include <sys/socket.h>
                     ])
fi
# If that didn't work, fall back to XPG5/UNIX98 with C89
if test "x$ac_cv_member_struct_msghdr_msg_control" = xno; then
    unset ac_cv_member_struct_msghdr_msg_control
    AC_MSG_NOTICE([trying again with _XOPEN_SOURCE=500])
    AC_CHECK_MEMBER([struct msghdr.msg_control],
                    [AC_DEFINE([_XOPEN_SOURCE], [500],
                       [Defined if needed to expose struct msghdr.msg_control])
                    ], [have_sendmsg="no"], [
#define _XOPEN_SOURCE 500
AC_INCLUDES_DEFAULT
#include <sys/socket.h>
                     ])
fi

case x$have_sendmsg in
xyes)
        AC_DEFINE([HAVE_SENDMSG],1,[Define if your platform supports sendmsg])
        ;;
esac

have_win32="no"
lt_enable_auto_import=""
case $host_os in
mingw*)
        have_win32="yes"
        lt_enable_auto_import="-Wl,--enable-auto-import"
        ;;
linux*)
        AC_DEFINE([HAVE_ABSTRACT_SOCKETS], 1, [Define if your platform supports abstract sockets])
        ;;
esac

AC_SUBST(lt_enable_auto_import)
AM_CONDITIONAL([XCB_HAVE_WIN32], [test "x${have_win32}" = "xyes"])

dnl define buffer queue size
AC_ARG_WITH([queue-size],
            AS_HELP_STRING([--with-queue-size=SIZE],
            [Set the XCB buffer queue size (default is 16384)]),
            [xcb_queue_buffer_size="$withval"],
            [xcb_queue_buffer_size=16384])
AC_DEFINE_UNQUOTED(XCB_QUEUE_BUFFER_SIZE, [$xcb_queue_buffer_size],
                   [XCB buffer queue size])

dnl check for the sockaddr_un.sun_len member
AC_CHECK_MEMBER([struct sockaddr_un.sun_len],
                [AC_DEFINE(HAVE_SOCKADDR_SUN_LEN,1,[Have the sockaddr_un.sun_len member.])],
                [],
                [ #include <sys/types.h>
                  #include <sys/un.h>
                ])

dnl check for support for Solaris Trusted Extensions
AC_CHECK_HEADERS([tsol/label.h])
AC_CHECK_FUNCS([is_system_labeled])

dnl check for IOV_MAX, and fall back to UIO_MAXIOV on BSDish systems
AC_CHECK_DECL([IOV_MAX], [],
              [AC_CHECK_DECL([UIO_MAXIOV], [AC_DEFINE([IOV_MAX], [UIO_MAXIOV])],
                                           [AC_DEFINE([IOV_MAX], [16], [Define if not provided by <limits.h>])],
                                           [[#include <sys/uio.h>]])],
              [[#include <limits.h>]])

xcbincludedir='${includedir}/xcb'
AC_SUBST(xcbincludedir)

XCB_CHECK_VISIBILITY()

AC_CHECK_FUNC(getaddrinfo, [AC_DEFINE(HAVE_GETADDRINFO, 1, [getaddrinfo() function is available])], )

case $host_os in
        # darwin through Snow Leopard has poll() but can't be used to poll character devices.
        darwin@<:@789@:>@*|darwin10*) ;;
        darwin*)
                _ac_xorg_macosx_version_min=""
                if echo $CPPFLAGS $CFLAGS | grep -q mmacosx-version-min ; then
                        _ac_xorg_macosx_version_min=`echo $CPPFLAGS $CFLAGS | sed 's/^.*-mmacosx-version-min=\(@<:@^ @:>@*\).*$/\1/'`
                else
                        _ac_xorg_macosx_version_min=$MACOSX_DEPLOYMENT_TARGET
                fi
                case $_ac_xorg_macosx_version_min in
                        10.@<:@0123456@:>@|10.@<:@0123456@:>@.*) ;;
                        *)
                                AC_CHECK_FUNC(poll, [AC_DEFINE(USE_POLL, 1, [poll() function is available])], )
                        ;;
                esac
                unset _ac_xorg_macosx_version_min
                ;;
        *)
                AC_CHECK_FUNC(poll, [AC_DEFINE(USE_POLL, 1, [poll() function is available])], )
        ;;
esac

dnl Link with winsock for socket functions on MinGW
case $host_os in
        *mingw*)
                AC_CHECK_LIB([ws2_32],[main])
                ;;
        *)
                ;;
esac

XCB_EXTENSION(Composite, yes)
XCB_EXTENSION(Damage, yes)
XCB_EXTENSION(DPMS, yes)
XCB_EXTENSION(DRI2, yes)
XCB_EXTENSION(DRI3, $have_sendmsg)
XCB_EXTENSION(GLX, yes)
XCB_EXTENSION(Present, yes)
XCB_EXTENSION(RandR, yes)
XCB_EXTENSION(Record, yes)
XCB_EXTENSION(Render, yes)
XCB_EXTENSION(Resource, yes)
XCB_EXTENSION(Screensaver, yes)
XCB_EXTENSION(Shape, yes)
XCB_EXTENSION(Shm, yes)
XCB_EXTENSION(Sync, yes)
XCB_EXTENSION(Xevie, no)
XCB_EXTENSION(XFixes, yes)
XCB_EXTENSION(XFree86-DRI, yes)
XCB_EXTENSION(Xinerama, yes)
XCB_EXTENSION(XInput, no)
XCB_EXTENSION(XKB, yes)
XCB_EXTENSION(Xprint, no)
XCB_EXTENSION(SELinux, no)
XCB_EXTENSION(XTest, yes)
XCB_EXTENSION(Xv, yes)
XCB_EXTENSION(XvMC, yes)

AC_ARG_WITH(launchd, AS_HELP_STRING([--with-launchd], [Build with support for Apple's launchd (default: auto)]), [LAUNCHD=$withval], [LAUNCHD=auto])
if test "x$LAUNCHD" = xauto; then
        unset LAUNCHD
        AC_CHECK_PROG(LAUNCHD, [launchd], [yes], [no], [$PATH$PATH_SEPARATOR/sbin])
fi

if test "x$LAUNCHD" = xyes ; then
        AC_DEFINE(HAVE_LAUNCHD, 1, [launchd support available])
fi

AC_ARG_WITH(serverside-support, AS_HELP_STRING([--with-serverside-support], [Build with support for server-side usage of xcb. This is still EXPERIMENTAL! ABI/API may change! (default: no)]), [XCB_SERVERSIDE_SUPPORT=$withval], [XCB_SERVERSIDE_SUPPORT=no])

AM_CONDITIONAL(XCB_SERVERSIDE_SUPPORT, test "x$XCB_SERVERSIDE_SUPPORT" = "xyes")

AC_CONFIG_FILES([
Makefile
doc/Makefile
man/Makefile
src/Makefile
tests/Makefile
])

AC_CONFIG_FILES([
xcb.pc
xcb-composite.pc
xcb-damage.pc
xcb-dpms.pc
xcb-dri2.pc
xcb-dri3.pc
xcb-glx.pc
xcb-present.pc
xcb-randr.pc
xcb-record.pc
xcb-render.pc
xcb-res.pc
xcb-screensaver.pc
xcb-shape.pc
xcb-shm.pc
xcb-sync.pc
xcb-xevie.pc
xcb-xf86dri.pc
xcb-xfixes.pc
xcb-xinerama.pc
xcb-xinput.pc
xcb-xkb.pc
xcb-xprint.pc
xcb-xselinux.pc
xcb-xtest.pc
xcb-xv.pc
xcb-xvmc.pc
])

AC_CONFIG_FILES([
doc/xcb.doxygen
])

AC_OUTPUT

dnl Configuration output

echo ""
echo "  Package: ${PACKAGE_NAME} ${PACKAGE_VERSION}"
echo ""
echo "  Configuration"
echo "    XDM support.........: ${have_xdmcp}"
echo "    sendmsg fd passing..: ${have_sendmsg}"
echo "    Build unit tests....: ${HAVE_CHECK}"
echo "      with html results.: ${HTML_CHECK_RESULT}"
echo "    XCB buffer size.....: ${xcb_queue_buffer_size}"
echo ""
echo "  X11 extensions"
echo "    Composite...........: ${BUILD_COMPOSITE}"
echo "    Damage..............: ${BUILD_DAMAGE}"
echo "    Dpms................: ${BUILD_DPMS}"
echo "    Dri2................: ${BUILD_DRI2}"
echo "    Dri3................: ${BUILD_DRI3}"
echo "    Glx.................: ${BUILD_GLX}"
echo "    Randr...............: ${BUILD_RANDR}"
echo "    Record..............: ${BUILD_RECORD}"
echo "    Render..............: ${BUILD_RENDER}"
echo "    Resource............: ${BUILD_RESOURCE}"
echo "    Screensaver.........: ${BUILD_SCREENSAVER}"
echo "    selinux.............: ${BUILD_SELINUX}"
echo "    Shape...............: ${BUILD_SHAPE}"
echo "    Shm.................: ${BUILD_SHM}"
echo "    Sync................: ${BUILD_SYNC}"
echo "    Xevie...............: ${BUILD_XEVIE}"
echo "    Xfixes..............: ${BUILD_XFIXES}"
echo "    Xfree86-dri.........: ${BUILD_XFREE86_DRI}"
echo "    xinerama............: ${BUILD_XINERAMA}"
echo "    xinput..............: ${BUILD_XINPUT}"
echo "    xkb.................: ${BUILD_XKB}"
echo "    xprint..............: ${BUILD_XPRINT}"
echo "    xtest...............: ${BUILD_XTEST}"
echo "    xv..................: ${BUILD_XV}"
echo "    xvmc................: ${BUILD_XVMC}"
echo ""
echo "  Used CFLAGS:"
echo "    CPPFLAGS............: ${CPPFLAGS}"
echo "    CFLAGS..............: ${CFLAGS}"
echo ""
echo "  Installation:"
echo "    Prefix..............: ${prefix}"
echo ""
