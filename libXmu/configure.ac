
# Initialize Autoconf
AC_PREREQ([2.70])
AC_INIT([libXmu], [1.2.1],
	[https://gitlab.freedesktop.org/xorg/lib/libxmu/-/issues/], [libXmu])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])

# Set common system defines for POSIX extensions, such as _GNU_SOURCE
# Must be called before any macros that run the compiler (like LT_INIT)
# to avoid autoconf errors.
AC_USE_SYSTEM_EXTENSIONS

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-xz])

# Initialize libtool
LT_INIT

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

# Obtain compiler/linker options for dependencies
PKG_CHECK_MODULES(XMU, [xt >= 1.1 xext x11 xextproto])
PKG_CHECK_MODULES(XMUU, x11)

# Checks for library functions.
AC_CHECK_FUNCS([uname])
AC_REPLACE_FUNCS([reallocarray])

# Allow checking code with lint, sparse, etc.
XORG_WITH_LINT
XORG_LINT_LIBRARY([Xmu])
LINTLIBUU=`echo $LINTLIB | sed s/Xmu/Xmuu/`
AC_SUBST(LINTLIBUU)

# --enable-unit-tests
AC_REQUIRE_AUX_FILE([tap-driver.sh])
XORG_ENABLE_UNIT_TESTS
XORG_WITH_GLIB([2.46])
XORG_MEMORY_CHECK_FLAGS
if test "x$enable_unit_tests" != "xno" ; then
       AC_CHECK_FUNCS([malloc_usable_size])
       AC_CHECK_HEADERS([malloc.h])
fi

AC_CONFIG_FILES([Makefile
		doc/Makefile
		include/Makefile
		src/Makefile
		test/Makefile
		xmu.pc
		xmuu.pc])
AC_OUTPUT
