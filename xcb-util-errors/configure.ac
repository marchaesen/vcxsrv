dnl XCB_UTIL_M4_WITH_INCLUDE_PATH requires Autoconf >= 2.62
AC_PREREQ(2.62)
AC_INIT([xcb-util-errors], [1.0.1],
        [https://gitlab.freedesktop.org/xorg/lib/libxcb-errors/-/issues])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_MACRO_DIR([m4])

# Set common system defines for POSIX extensions, such as _GNU_SOURCE
# Must be called before any macros that run the compiler (like AC_PROG_LIBTOOL)
# to avoid autoconf errors.
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([foreign dist-xz subdir-objects])

AC_TYPE_SSIZE_T

XCB_UTIL_M4_WITH_INCLUDE_PATH
XCB_UTIL_COMMON([1.4], [1.6])

AM_PATH_PYTHON

# Checks for xcb-proto's XML files, inspired from libxcb's configure.ac
PKG_CHECK_MODULES(XCBPROTO, xcb-proto)
AC_MSG_CHECKING(XCBPROTO_XCBINCLUDEDIR)
XCBPROTO_XCBINCLUDEDIR=`$PKG_CONFIG --variable=xcbincludedir xcb-proto`
AC_MSG_RESULT($XCBPROTO_XCBINCLUDEDIR)
AC_SUBST(XCBPROTO_XCBINCLUDEDIR)

# Find the xcb-proto version
XCBPROTO_VERSION=`$PKG_CONFIG --modversion xcb-proto`
AC_SUBST(XCBPROTO_VERSION)

AC_CONFIG_FILES([Makefile
	xcb-errors.pc
	])
AC_OUTPUT
