#
#  Copyright © 2019 Thomas Dickey
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
AC_PREREQ([2.70])
AC_INIT([libXt], [1.3.0],
	[https://gitlab.freedesktop.org/xorg/lib/libxt/-/issues/], [libXt])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Set common system defines for POSIX extensions, such as _GNU_SOURCE
# Must be called before any macros that run the compiler (like AC_PROG_LIBTOOL)
# to avoid autoconf errors.
AC_USE_SYSTEM_EXTENSIONS

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-xz])

# Initialize libtool
LT_INIT

# Require X.Org macros 1.16 or later for XORG_MEMORY_CHECK_FLAGS
m4_ifndef([XORG_MACROS_VERSION],
	  [m4_fatal([must install xorg-macros 1.16 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.16)
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

# Checks for library functions.
AC_CHECK_FUNCS([reallocarray])

# Obtain compiler/linker options for dependencies
PKG_CHECK_MODULES(XT, sm ice x11 xproto kbproto)

# Set-up variables to use build machine compiler when cross-compiling
if test x"$CC_FOR_BUILD" = x; then
	if test x"$cross_compiling" = xyes; then
		AC_CHECK_PROGS(CC_FOR_BUILD, gcc cc)
	else
		CC_FOR_BUILD="$CC"
	fi
fi
if test x"$cross_compiling" != xyes; then
       : ${CFLAGS_FOR_BUILD=${CFLAGS}}
       : ${LDFLAGS_FOR_BUILD=${LDFLAGS}}
fi
AC_SUBST([CC_FOR_BUILD])
DEFAULT_CPPFLAGS_FOR_BUILD="${CPPFLAGS}"
AC_CHECK_FUNC(asprintf,
  [DEFAULT_CPPFLAGS_FOR_BUILD="${DEFAULT_CPPFLAGS_FOR_BUILD} -DHAVE_ASPRINTF -D_GNU_SOURCE"])
CPPFLAGS_FOR_BUILD=${CPPFLAGS_FOR_BUILD-${DEFAULT_CPPFLAGS_FOR_BUILD}}
AC_SUBST(CPPFLAGS_FOR_BUILD)
DEFAULT_CFLAGS_FOR_BUILD="${CFLAGS} ${CWARNFLAGS}"
AC_SUBST(CFLAGS_FOR_BUILD)
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
		AC_CHECK_LIB([ws2_32],[main])
	;;
	*)
	;;
esac

# Options
AC_MSG_CHECKING(if XKB support is wanted)
AC_ARG_ENABLE(xkb, AS_HELP_STRING([--disable-xkb], [Disable XKB support]),
	XKB="$enableval", XKB="yes")
AC_MSG_RESULT($XKB)
if test "x$XKB" = "xyes" ; then
	AC_DEFINE(XKB, 1, [Define to 1 to use XKB for keysym resolution.])
fi

AC_MSG_CHECKING(if C const-support is wanted)
AC_ARG_ENABLE(const, AS_HELP_STRING([--disable-const], [Disable const-support]),
	USE_CONST="$enableval", USE_CONST="yes")
AC_MSG_RESULT($USE_CONST)
if test "x$USE_CONST" = "xyes" ; then
	AC_DEFINE(_CONST_X_STRING, 1, [Define to 1 to use standard C const feature.])
fi

AC_MSG_CHECKING(if geo-tattler traces are wanted)
AC_ARG_ENABLE(geo-tattler, AS_HELP_STRING([--enable-geo-tattler], [Enable geo-tattler traces]),
	GEO_TATTLER="$enableval", GEO_TATTLER="no")
AC_MSG_RESULT($GEO_TATTLER)
if test "x$GEO_TATTLER" = "xyes" ; then
	AC_DEFINE(XT_GEO_TATTLER, 1, [Define to 1 to enable geo-tattler traces.])
fi

# --enable-unit-tests
XORG_ENABLE_UNIT_TESTS
XORG_WITH_GLIB([2.40])
XORG_MEMORY_CHECK_FLAGS
if test "x$enable_unit_tests" != "xno" ; then
	AC_CHECK_FUNCS([malloc_usable_size])
	AC_CHECK_HEADERS([malloc.h])
fi

# Replaces XFileSearchPathDefault from Imake configs
XFILESEARCHPATHDEFAULT='$(sysconfdir)/X11/%L/%T/%N%C%S:$(sysconfdir)/X11/%l/%T/%N%C%S:$(sysconfdir)/X11/%T/%N%C%S:$(sysconfdir)/X11/%L/%T/%N%S:$(sysconfdir)/X11/%l/%T/%N%S:$(sysconfdir)/X11/%T/%N%S:$(datadir)/X11/%L/%T/%N%C%S:$(datadir)/X11/%l/%T/%N%C%S:$(datadir)/X11/%T/%N%C%S:$(datadir)/X11/%L/%T/%N%S:$(datadir)/X11/%l/%T/%N%S:$(datadir)/X11/%T/%N%S:$(libdir)/X11/%L/%T/%N%C%S:$(libdir)/X11/%l/%T/%N%C%S:$(libdir)/X11/%T/%N%C%S:$(libdir)/X11/%L/%T/%N%S:$(libdir)/X11/%l/%T/%N%S:$(libdir)/X11/%T/%N%S'

AC_MSG_CHECKING(for X files search path)
AC_ARG_WITH(xfile-search-path,
	AS_HELP_STRING([--with-xfile-search-path=<path>],
		       [Set path to search for app-defaults and other files]),
	[XFILESEARCHPATHDEFAULT="$withval"], [])
AC_MSG_RESULT(${XFILESEARCHPATHDEFAULT:-none})
AC_SUBST([XFILESEARCHPATHDEFAULT])

AC_MSG_CHECKING(for X app-defaults directory)
AC_ARG_WITH(appdefaultdir,
	AS_HELP_STRING([--with-appdefaultdir=<path>],
		       [Set app-default directory (default: ${datadir}/X11/app-defaults)]),
	[appdefaultdir="$withval"],
	[appdefaultdir=${datadir}/X11/app-defaults])
AC_MSG_RESULT($appdefaultdir)
AX_DEFINE_DIR([appdefaultdir], [appdefaultdir], [app default data directory])

# Replacement for Imake ToolkitStringsABIOptions, controls string tables
# generated by util/string.list in StringDefs.h & Shell.h
AC_MSG_CHECKING(for ABI options)
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
AC_MSG_RESULT(${STRINGSABIOPTIONS:-none})
AC_SUBST(STRINGSABIOPTIONS)

ERRORDBDIR=${datadir}/X11
AC_SUBST(ERRORDBDIR)

AC_CONFIG_FILES([Makefile
		src/Makefile
		util/Makefile
		include/Makefile
		man/Makefile
		specs/Makefile
		specs/libXt.ent
		test/Makefile
		xt.pc])
AC_OUTPUT
