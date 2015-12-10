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
AC_INIT([libXt], [1.1.5],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg], [libXt])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])

# Initialize libtool
AC_PROG_LIBTOOL

# Require X.Org macros 1.13 or later for XORG_ENABLE_UNIT_TESTS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.13 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.13)
XORG_DEFAULT_OPTIONS
XORG_CHECK_MALLOC_ZERO
XORG_ENABLE_SPECS
XORG_WITH_XMLTO(0.0.20)
XORG_WITH_FOP
XORG_WITH_XSLTPROC
XORG_CHECK_SGML_DOCTOOLS(1.01)
XORG_PROG_RAWCPP
XORG_WITH_PERL


# Checks for header files.
AC_CHECK_HEADER([alloca.h], AC_DEFINE(INCLUDE_ALLOCA_H, 1, [Define to 1 if Xalloca.h should include <alloca.h>]))

# Obtain compiler/linker options for depedencies
PKG_CHECK_MODULES(XT, sm ice x11 xproto kbproto)

# Set-up variables to use build machine compiler when cross-compiling
if test x"$CC_FOR_BUILD" = x; then
	if test x"$cross_compiling" = xyes; then
		AC_CHECK_PROGS(CC_FOR_BUILD, gcc cc)
	else
	        CC_FOR_BUILD="$CC"
	fi
fi
AC_SUBST([CC_FOR_BUILD])
DEFAULT_CPPFLAGS_FOR_BUILD="${CPPFLAGS}"
AC_CHECK_FUNC(asprintf,
  [DEFAULT_CPPFLAGS_FOR_BUILD="${DEFAULT_CPPFLAGS_FOR_BUILD} -DHAVE_ASPRINTF -D_GNU_SOURCE"])
CPPFLAGS_FOR_BUILD=${CPPFLAGS_FOR_BUILD-${DEFAULT_CPPFLAGS_FOR_BUILD}}
AC_SUBST(CPPFLAGS_FOR_BUILD)
DEFAULT_CFLAGS_FOR_BUILD="${CFLAGS} ${CWARNFLAGS}"
CFLAGS_FOR_BUILD=${CFLAGS_FOR_BUILD-${DEFAULT_CFLAGS_FOR_BUILD}}
AC_SUBST(CFLAGS_FOR_BUILD)
LDFLAGS_FOR_BUILD=${LDFLAGS_FOR_BUILD-${LDFLAGS}}
AC_SUBST(LDFLAGS_FOR_BUILD)

# Map function checks to old Imake #defines
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

# Link with winsock if mingw target
case $host_os in
	*mingw*)
		AC_HAVE_LIBRARY(ws2_32)
	;;
	*)
	;;
esac

# Options
AC_ARG_ENABLE(xkb, AS_HELP_STRING([--disable-xkb], [Disable XKB support]),
	XKB="$enableval", XKB="yes")
if test "x$XKB" = "xyes" ; then
	AC_DEFINE(XKB, 1, [Define to 1 to use XKB for keysym resolution.])
fi

# --enable-unit-tests
XORG_ENABLE_UNIT_TESTS
XORG_WITH_GLIB([2.16])

# Memory checking support
case $host_os in
     solaris*)
        AC_CHECK_LIB([umem], [umem_alloc],
            [MALLOC_DEBUG_ENV='LD_PRELOAD=libumem.so UMEM_DEBUG=default'])
        ;;
     *-gnu*) # GNU libc - Value is used as a single byte bit pattern,
        # both directly and inverted, so should not be 0 or 255.
        MALLOC_DEBUG_ENV='MALLOC_PERTURB_=15'
        ;;
     *bsd*|darwin*)
        MALLOC_DEBUG_ENV='MallocPreScribble=1 MallocScribble=1'
        ;;
esac
AC_SUBST([MALLOC_DEBUG_ENV])

# Replaces XFileSearchPathDefault from Imake configs
XFILESEARCHPATHDEFAULT='$(sysconfdir)/X11/%L/%T/%N%C%S:$(sysconfdir)/X11/%l/%T/%N%C%S:$(sysconfdir)/X11/%T/%N%C%S:$(sysconfdir)/X11/%L/%T/%N%S:$(sysconfdir)/X11/%l/%T/%N%S:$(sysconfdir)/X11/%T/%N%S:$(datadir)/X11/%L/%T/%N%C%S:$(datadir)/X11/%l/%T/%N%C%S:$(datadir)/X11/%T/%N%C%S:$(datadir)/X11/%L/%T/%N%S:$(datadir)/X11/%l/%T/%N%S:$(datadir)/X11/%T/%N%S:$(libdir)/X11/%L/%T/%N%C%S:$(libdir)/X11/%l/%T/%N%C%S:$(libdir)/X11/%T/%N%C%S:$(libdir)/X11/%L/%T/%N%S:$(libdir)/X11/%l/%T/%N%S:$(libdir)/X11/%T/%N%S'

AC_ARG_WITH(xfile-search-path,
	AS_HELP_STRING([--with-xfile-search-path=<path>],
		       [Set path to search for app-defaults and other files]),
	[XFILESEARCHPATHDEFAULT="$withval"], [])
AC_SUBST([XFILESEARCHPATHDEFAULT])

AC_ARG_WITH(appdefaultdir,
	AS_HELP_STRING([--with-appdefaultdir=<path>],
		       [Set app-default directory (default: ${datadir}/X11/app-defaults)]),
	[appdefaultdir="$withval"],
	[appdefaultdir=${datadir}/X11/app-defaults])
AX_DEFINE_DIR([appdefaultdir], [appdefaultdir], [app default data directory])

# Replacement for Imake ToolkitStringsABIOptions, controls string tables
# generated by util/string.list in StringDefs.h & Shell.h
case $host_os in
	solaris*)
		# Solaris uses -intelabi even on SPARC
  		STRINGSABIOPTIONS="-intelabi -solarisabinames"
		;;
	sco* | svr4*)
		case $host_cpu in
		    i*86) STRINGSABIOPTIONS="-intelabi" ;;
		       *) STRINGSABIOPTIONS="" ;;
		esac
		;;
esac
AC_SUBST(STRINGSABIOPTIONS)

ERRORDBDIR=${datadir}/X11
AC_SUBST(ERRORDBDIR)

case $host_os in
    darwin*)
        OS_CFLAGS="-Wl,-flat_namespace"
	;;
    *)
        OS_CFLAGS=
        ;;
esac

XT_CFLAGS="$XT_CFLAGS $OS_CFLAGS"

AC_CONFIG_FILES([Makefile
		src/Makefile
		util/Makefile
		include/Makefile
		man/Makefile
		specs/Makefile
		test/Makefile
		xt.pc])
AC_OUTPUT
