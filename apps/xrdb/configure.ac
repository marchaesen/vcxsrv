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
AC_INIT([xrdb], [1.0.9],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg], [xrdb])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_USE_SYSTEM_EXTENSIONS

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_MAINTAINER_MODE

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
	  [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

AC_CHECK_FUNCS([mkstemp asprintf])

# Find MAXHOSTNAMELEN definition
# Common hidey holes:
#  BSD & Linux - <sys/param.h>
#  Solaris - <netdb.h>

AC_CHECK_DECL([MAXHOSTNAMELEN],[FOUND_MAXHOSTNAMELEN=yes])

if test x$FOUND_MAXHOSTNAMELEN != xyes ; then
	AC_MSG_CHECKING([for header that defines MAXHOSTNAMELEN])

	FOUND_MAXHOSTNAMELEN='not found'

	AC_COMPILE_IFELSE(
	    AC_LANG_PROGRAM([#include <sys/param.h>],
			    [int h = MAXHOSTNAMELEN;]),
	    [FOUND_MAXHOSTNAMELEN='sys/param.h'
	     AC_DEFINE(NEED_SYS_PARAM_H,1,
	       [Define to 1 if you need <sys/param.h> to define MAXHOSTNAMELEN])])

	AC_COMPILE_IFELSE(
	    AC_LANG_PROGRAM([#include <netdb.h>],
			    [int h = MAXHOSTNAMELEN;]),
	    [FOUND_MAXHOSTNAMELEN='netdb.h'
	     AC_DEFINE(NEED_NETDB_H,1,
	       [Define to 1 if you need <netdb.h> to define MAXHOSTNAMELEN])])

	AC_MSG_RESULT([$FOUND_MAXHOSTNAMELEN])
fi

# xrdb needs to know where to find cpp at runtime - previously set as CppCmd
# in Imake config files for each OS
AC_ARG_WITH([cpp], 
	AS_HELP_STRING([--with-cpp=path],
		       [comma-separated list of paths to cpp command for xrdb to use at runtime]), 
	[AC_MSG_CHECKING(for cpp)
	CPP_PATH=$withval
	AC_MSG_RESULT(--with-cpp specified $CPP_PATH)], 
	[AC_PATH_PROG([CPP_PATH],[cpp], [cpp], 
 [$PATH:/bin:/usr/bin:/usr/lib:/usr/libexec:/usr/ccs/lib:/usr/ccs/lbin:/lib])])
CPP_PATH=`echo ${CPP_PATH} | sed 's/,/\\",\\"/g'`
AC_DEFINE_UNQUOTED([CPP], "$CPP_PATH", [Path to CPP program])

# Checks for pkg-config packages
PKG_CHECK_MODULES(XRDB, xmuu x11)

AC_CONFIG_FILES([
	Makefile
	man/Makefile])
AC_OUTPUT
