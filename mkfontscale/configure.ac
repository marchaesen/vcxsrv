dnl  Copyright 2005 Red Hat, Inc.
dnl
dnl  Permission to use, copy, modify, distribute, and sell this software
dnl  and its documentation for any purpose is hereby granted without fee,
dnl  provided that the above copyright notice appear in all copies and
dnl  that both that copyright notice and this permission notice appear in
dnl  supporting documentation, and that the name of Red Hat not be used in
dnl  advertising or publicity pertaining to distribution of the software
dnl  without specific, written prior permission.  Red Hat makes no
dnl  representations about the suitability of this software for any
dnl  purpose.  It is provided "as is" without express or implied warranty.
dnl
dnl  RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
dnl  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
dnl  NO EVENT SHALL RED HAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
dnl  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
dnl  OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
dnl  OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
dnl  USE OR PERFORMANCE OF THIS SOFTWARE.
dnl
dnl Process this file with autoconf to create configure.

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([mkfontscale], [1.1.2],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
        [mkfontscale])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_USE_SYSTEM_EXTENSIONS

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
	  [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

AC_CHECK_LIB(z, gzopen, [],
	[AC_MSG_ERROR([zlib is required, but was not found.])])

AC_ARG_WITH(bzip2,
	AS_HELP_STRING([--with-bzip2],
			[Support bzip2 compressed bitmap fonts]),
	[], [with_bzip2=no])
if test "x$with_bzip2" = xyes; then
	AC_CHECK_LIB(bz2, BZ2_bzopen, [],
		AC_MSG_ERROR([*** libbz2 is required for bzip2 support]))
	AC_DEFINE(X_BZIP2_FONT_COMPRESSION,1,[Support bzip2 for bitmap fonts])
fi

# Checks for pkg-config packages
PKG_CHECK_MODULES(MKFONTSCALE, fontenc freetype2)
PKG_CHECK_MODULES(X11, [xproto >= 7.0.25])

dnl Allow checking code with lint, sparse, etc.
XORG_WITH_LINT

AC_CONFIG_FILES([
	Makefile
	man/Makefile])
AC_OUTPUT
