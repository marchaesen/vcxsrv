dnl  Copyright 2005 Red Hat, Inc.
dnl
dnl  Permission to use, copy, modify, distribute, and sell this software
dnl  and its documentation for any purpose is hereby granted without
dnl  fee, provided that the above copyright notice appear in all copies
dnl  and that both that copyright notice and this permission notice
dnl  appear in supporting documentation, and that the name of Red Hat
dnl  not be used in advertising or publicity pertaining to distribution
dnl  of the software without specific, written prior permission.  Red
dnl  Hat makes no representations about the suitability of this software
dnl  for any purpose.  It is provided "as is" without express or implied
dnl  warranty.
dnl
dnl  RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
dnl  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
dnl  NO EVENT SHALL RED HAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
dnl  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
dnl  OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
dnl  NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
dnl  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
dnl
dnl Process this file with autoconf to create configure.

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([font-util], [1.3.1],
	[https://bugs.freedesktop.org/enter_bug.cgi?product=xorg], [font-util])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
	  [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

AC_MSG_CHECKING([for root directory for font files])
fontrootdir='${datadir}/fonts/X11'
AC_ARG_WITH(fontrootdir, [AS_HELP_STRING([--with-fontrootdir=DIR],
	[Path to parent of font subdirectories [DATADIR/fonts/X11]])],
	[fontrootdir="${withval}"])
AC_SUBST(fontrootdir)
AC_MSG_RESULT([${fontrootdir}])

mapdir='${fontrootdir}/util'
AC_ARG_WITH(mapdir, [AS_HELP_STRING([--with-mapdir=DIR],
	[Path to install font maps [FONTROOTDIR/util]])],
	[mapdir="${withval}"])
MAPDIR="$mapdir"
AC_SUBST(MAPDIR)

AC_CONFIG_FILES([Makefile
                 man/Makefile
                 fontutil.m4
                 fontutil.pc])

AC_OUTPUT
