
# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([libXaw], [1.0.15],
        [https://gitlab.freedesktop.org/xorg/lib/libxaw/-/issues], [libXaw])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-xz])

# Initialize libtool
LT_INIT

# Require xorg-macros minimum of 1.12 for DocBook external references
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.12 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.12)
XORG_DEFAULT_OPTIONS
XORG_ENABLE_SPECS
XORG_WITH_XMLTO(0.0.22)
XORG_WITH_FOP
XORG_WITH_XSLTPROC
XORG_CHECK_SGML_DOCTOOLS(1.8)

# Some compilers do not support per target -c and -o flags
AM_PROG_CC_C_O

# Checks for programs.
AC_PROG_AWK

# Need to call this explicitly since the first call to PKG_CHECK_MODULES
# is in an if statement, and later calls would break if it's skipped.
PKG_PROG_PKG_CONFIG

#
# fix libtool to set SONAME to libXaw.so.$major
#
AC_CONFIG_COMMANDS([libtool_hack], [
	cp -f libtool libtool_
	test -z "$SED" && SED=sed
	$SED '1,/^soname_spec/{
/^soname_spec/i\
# X.Org hack to match monolithic Xaw SONAME\
xorglibxawname="libXaw"
/^soname_spec/s/libname/xorglibxawname/
}' libtool_ > libtool
	rm -f libtool_
])

# OSX/Win32 rules are different.
platform_win32=no
platform_darwin=no
LIBEXT=so
case $host_os in
    cygwin*|mingw*)
	LIBEXT=dll.a
	platform_win32=yes
	;;
    darwin*)
	LIBEXT=dylib
	platform_darwin=yes
	;;
esac
AC_SUBST(LIBEXT)
AM_CONDITIONAL(PLATFORM_WIN32, test "x$platform_win32" = "xyes")
AM_CONDITIONAL(PLATFORM_DARWIN, test "x$platform_darwin" = "xyes")

# Whether to build Xaw6

AC_ARG_ENABLE(xaw6, AS_HELP_STRING([--disable-xaw6],
				[Disable building of libXaw.so.6]),
			[build_v6=$enableval], [build_v6=yes])

if test "x$build_v6" = xyes; then
   PKG_CHECK_MODULES(XAW6, xproto x11 xext xextproto xt xmu)
fi


# Whether to build Xaw7

AC_ARG_ENABLE(xaw7, AS_HELP_STRING([--disable-xaw7],
				[Disable building of libXaw.so.7]),
			[build_v7=$enableval], [build_v7=yes])

if test "x$build_v7" = xyes; then
   PKG_CHECK_MODULES(XAW7, xproto x11 xext xextproto xt xmu xpm)
fi


AM_CONDITIONAL(BUILD_XAW6, [test x$build_v6 = xyes])
AM_CONDITIONAL(BUILD_XAW7, [test x$build_v7 = xyes])

# Checks for header files.
AC_CHECK_HEADERS([wctype.h wchar.h widec.h])

# Checks for functions
AC_CHECK_FUNCS([iswalnum getpagesize])

# Link with winsock if mingw target
case $host_os in
	*mingw*)
		AC_CHECK_LIB([ws2_32],[main])
	;;
	*)
	;;
esac

AC_MSG_CHECKING(if C const-support is wanted)
AC_ARG_ENABLE(const, AS_HELP_STRING([--disable-const], [Disable const-support]),
	USE_CONST="$enableval", USE_CONST="yes")
AC_MSG_RESULT($USE_CONST)
if test "x$USE_CONST" = "xyes" ; then
	AC_DEFINE(_CONST_X_STRING, 1, [Define to 1 to use standard C const feature.])
fi

AC_CONFIG_FILES([Makefile
		include/Makefile
		man/Makefile
		specs/Makefile
		specs/libXaw.ent
		src/Makefile])

if test "x$build_v6" = xyes; then
   AC_CONFIG_FILES(xaw6.pc)
fi

if test "x$build_v7" = xyes; then
   AC_CONFIG_FILES(xaw7.pc)
fi

AC_OUTPUT
