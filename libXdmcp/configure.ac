#
#  Copyright © 2003 Keith Packard, Noah Levitt
#
#  Permission to use, copy, modify, distribute, and sell this software and its
#  documentation for any purpose is hereby granted without fee, provided that
#  the above copyright notice appear in all copies and that both that
#  copyright notice and this permission notice appear in supporting
#  documentation, and that the name of Keith Packard not be used in
#  advertising or publicity pertaining to distribution of the software without
#  specific, written prior permission.  Keith Packard makes no
#  representations about the suitability of this software for any purpose.  It
#  is provided "as is" without express or implied warranty.
#
#  KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
#  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
#  EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
#  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
#  DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
#  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
#  PERFORMANCE OF THIS SOFTWARE.
#

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([libXdmcp], [1.1.1],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg], [libXdmcp])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_MAINTAINER_MODE

# Initialize libtool
AC_LIBTOOL_WIN32_DLL
AC_PROG_LIBTOOL

# Require xorg-macros minimum of 1.12 for DocBook external references
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.12 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.12)
XORG_DEFAULT_OPTIONS
XORG_ENABLE_DOCS
XORG_WITH_XMLTO(0.0.22)
XORG_WITH_FOP
XORG_WITH_XSLTPROC
XORG_CHECK_SGML_DOCTOOLS(1.8)

# Checks for programs.
AC_PROG_LN_S

# Checks for libraries.
AC_SEARCH_LIBS([recvfrom],[socket])

# Checks for library functions.
AC_CHECK_FUNCS([srand48 lrand48])

# Obtain compiler/linker options for depedencies
PKG_CHECK_MODULES(XDMCP, xproto)

if test -f ${srcdir}/Wraphelp.c; then
	AC_DEFINE(HASXDMAUTH,1,[Has Wraphelp.c needed for XDM AUTH protocols])
	HASXDMAUTH=yes
else
	HASXDMAUTH=no
fi

AM_CONDITIONAL(HASXDMAUTH,test x$HASXDMAUTH = xyes)

# Allow checking code with lint, sparse, etc.
XORG_WITH_LINT
XORG_LINT_LIBRARY([Xdmcp])

AC_CONFIG_FILES([Makefile
		doc/Makefile
		xdmcp.pc])
AC_OUTPUT
