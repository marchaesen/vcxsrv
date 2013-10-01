
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

AC_PREREQ([2.60])
AC_INIT([xclock],
	[1.0.7],
	[https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
	[xclock])
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_MAINTAINER_MODE

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

AC_CONFIG_HEADERS([config.h])



AC_CHECK_FUNCS([strlcpy getpid])

AM_ICONV

dnl Required dependencies
PKG_CHECK_MODULES(XCLOCK, [xaw7 xmu x11 xproto >= 7.0.17])
XCLOCK_LIBS="$XCLOCK_LIBS $LIBICONV"

dnl Optional dependencies
AC_ARG_WITH(xft, AS_HELP_STRING([--with-xft],[Use Xft2 and Xrender for rendering (Default is YES)]),use_xft="$withval",use_xft="try")
if test x$use_xft != xno ; then
	PKG_CHECK_MODULES(XFT, [xrender xft])
	AC_SEARCH_LIBS(sqrt, [m])
	XCLOCK_CFLAGS="$XCLOCK_CFLAGS $XFT_CFLAGS"
	XCLOCK_LIBS="$XCLOCK_LIBS $XFT_LIBS"
	AC_DEFINE([XRENDER],1,[Define to use X Render Extension])
	AC_DEFINE([XFREE86_FT2],1,[Define to use Xft2 library])
fi

AC_ARG_WITH(xkb, AS_HELP_STRING([--with-xkb],[Use XKB to sound bells (Default is YES)]),use_xkb="$withval",use_xkb="try")
if test x$use_xkb != xno ; then
	PKG_CHECK_MODULES(XKB, xkbfile)
	XCLOCK_CFLAGS="$XCLOCK_CFLAGS $XKB_CFLAGS"
	XCLOCK_LIBS="$XCLOCK_LIBS $XKB_LIBS"
	AC_DEFINE([XKB],1,[Define to use XkbStdBell])
fi


PKG_CHECK_MODULES(APPDEFS, xt)
xt_appdefaultdir=`$PKG_CONFIG --variable=appdefaultdir xt`
AC_ARG_WITH(appdefaultdir,
	AS_HELP_STRING([--with-appdefaultdir=<pathname>],
	  [specify directory for app-defaults files (default is autodetected)]),
	[appdefaultdir="$withval"], [appdefaultdir="${xt_appdefaultdir}"])
AC_SUBST(appdefaultdir)

AC_CONFIG_FILES([
	Makefile
	man/Makefile])
AC_OUTPUT
