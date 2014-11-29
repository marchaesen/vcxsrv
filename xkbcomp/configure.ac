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
AC_INIT([xkbcomp], [1.3.0],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg], [xkbcomp])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

# If both the C file and YACC are missing, the package cannot be build.
AC_PROG_YACC
AC_PATH_PROG([YACC_INST], $YACC)
if test ! -f "$srcdir/xkbparse.c"; then
   if test -z "$YACC_INST"; then
      AC_MSG_ERROR([yacc not found - unable to compile xkbparse.y])
   fi
fi

AC_CHECK_FUNCS([strdup strcasecmp])

REQUIRED_MODULES="x11 xkbfile xproto >= 7.0.17"

# Checks for pkg-config packages
PKG_CHECK_MODULES(XKBCOMP, [$REQUIRED_MODULES])
AC_SUBST(REQUIRED_MODULES)

AC_ARG_WITH([xkb_config_root],
    [AS_HELP_STRING([--with-xkb-config-root=<paths>],
        [Set default XKB config root (default: ${datadir}/X11/xkb)])],
    [XKBCONFIGROOT="$withval"],
    [XKBCONFIGROOT='${datadir}/X11/xkb'])
AC_SUBST([XKBCONFIGROOT])


AC_CONFIG_FILES([
	Makefile
	man/Makefile
	xkbcomp.pc])
AC_OUTPUT
