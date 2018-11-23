
dnl  Copyright 2005 Red Hat, Inc.
dnl
dnl  Permission to use, copy, modify, distribute, and sell this software and its
dnl  documentation for any purpose is hereby granted without fee, provided that
dnl  the above copyright notice appear in all copies and that both that
dnl  copyright notice and this permission notice appear in supporting
dnl  documentation, and that the name of Red Hat not be used in
dnl  advertising or publicity pertaining to distribution of the software without
dnl  specific, written prior permission.  Red Hat makes no
dnl  representations about the suitability of this software for any purpose.  It
dnl  is provided "as is" without express or implied warranty.
dnl
dnl  RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
dnl  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
dnl  EVENT SHALL RED HAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
dnl  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
dnl  DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
dnl  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
dnl  PERFORMANCE OF THIS SOFTWARE.
dnl
dnl Process this file with autoconf to create configure.

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([xwininfo], [1.1.4],
	[https://gitlab.freedesktop.org/xorg/app/xwininfo/issues], [xwininfo])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_USE_SYSTEM_EXTENSIONS

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_ICONV

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
	  [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

# Checks for library functions
AC_CHECK_FUNCS([strlcat])

# Use POSIX strnlen or the implementation supplied in this module
AC_FUNC_STRNLEN
if test "x$ac_cv_func_strnlen_working" = xyes; then
  AC_DEFINE(HAVE_STRNLEN, 1, [Define to 1 if you have a working strnlen function.])
fi

# Define a configure option for using the XCB ICCCM helper functions
AC_MSG_CHECKING([whether to use xcb-icccm library])
AC_ARG_WITH([xcb-icccm],
	    [AS_HELP_STRING([--with-xcb-icccm],
			    [use xcb-icccm (default: no)])],
            [], [with_xcb_icccm=no])
AC_MSG_RESULT([$with_xcb_icccm])
if test "x$with_xcb_icccm" != xno ; then
	AC_DEFINE([USE_XCB_ICCCM], 1,
		  [Define to 1 to call xcb-icccm library functions instead of local replacements])
	xcb_icccm_pc='xcb-icccm >= 0.3.8'
fi

# Obtain compiler/linker options for xwininfo dependencies
PKG_CHECK_MODULES(XWININFO, [xcb >= 1.6] xcb-shape ${xcb_icccm_pc})

# Even when using xcb, xproto is still required for Xfuncproto.h
# and libX11 headers for cursorfont.h
PKG_CHECK_MODULES(XLIB, x11 [xproto >= 7.0.25])
XWININFO_CFLAGS="${XWININFO_CFLAGS} ${XLIB_CFLAGS}"
XWININFO_LIBS="${XWININFO_LIBS} ${LIBICONV}"

AC_CONFIG_FILES([
	Makefile
	man/Makefile])
AC_OUTPUT
