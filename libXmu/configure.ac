
# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([libXmu], [1.1.2],
	[https://bugs.freedesktop.org/enter_bug.cgi?product=xorg], [libXmu])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])

# Initialize libtool
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

# Obtain compiler/linker options for depedencies
PKG_CHECK_MODULES(XMU, xt xext x11 xextproto)
PKG_CHECK_MODULES(XMUU, x11)

# CvtStdSel.c needs to know which network transports to build hostname
# conversion routines for
XTRANS_CONNECTION_FLAGS

# Allow checking code with lint, sparse, etc.
XORG_WITH_LINT
XORG_LINT_LIBRARY([Xmu])
LINTLIBUU=`echo $LINTLIB | sed s/Xmu/Xmuu/`
AC_SUBST(LINTLIBUU)

AC_CONFIG_FILES([Makefile
		doc/Makefile
		include/Makefile
		src/Makefile
		xmu.pc
		xmuu.pc])
AC_OUTPUT
