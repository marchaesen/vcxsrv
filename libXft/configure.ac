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
#
# This is the package version number, not the shared library
# version.  This version number will be substituted into Xft.h
# Please bump the minor library number at each release as well.
#
AC_INIT([libXft], [2.3.2],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg], [libXft])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])

# Initialize libtool
AC_PROG_LIBTOOL

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

# Set library version for Xft.h from package version set in AC_INIT
# copied from PACKAGE_VERSION_* settings in XORG_VERSION
AC_CONFIG_HEADERS([include/X11/Xft/Xft.h])
AC_DEFINE_UNQUOTED([XFT_MAJOR],
                [`echo $PACKAGE_VERSION | cut -d . -f 1`],
                [Major version of Xft])
AC_DEFINE_UNQUOTED([XFT_MINOR],
                [`echo $PACKAGE_VERSION | cut -d . -f 2 | cut -d - -f 1`],
                [Minor version of Xft])
AC_DEFINE_UNQUOTED([XFT_REVISION],
                [`echo $PACKAGE_VERSION | cut -d . -f 3 | cut -d - -f 1`],
                [Micro revision of Xft])

XFT_LT_VERSION=`echo $PACKAGE_VERSION | tr '.' ':'`
AC_SUBST([XFT_LT_VERSION])

#
# Check for Xrender
#
PKG_CHECK_MODULES(XRENDER, xrender >= 0.8.2 x11)

# Check freetype configuration
PKG_CHECK_MODULES(FREETYPE, freetype2 >= 2.1.6)

# Check fontconfig configuration
PKG_CHECK_MODULES(FONTCONFIG, fontconfig >= 2.5.92)

AC_SUBST(XRENDER_CFLAGS)
AC_SUBST(XRENDER_LIBS)
AC_SUBST(FREETYPE_CFLAGS)
AC_SUBST(FREETYPE_LIBS)
AC_SUBST(FONTCONFIG_CFLAGS)
AC_SUBST(FONTCONFIG_LIBS)

if test "$VERSION" = "" ; then
       VERSION=$PACKAGE_VERSION;
fi

AC_CONFIG_FILES([Makefile
		xft.pc
		src/Makefile
		man/Makefile])
AC_OUTPUT
