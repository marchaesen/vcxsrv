# $XdotOrg: app/xhost/configure.ac,v 1.10 2005/12/15 00:24:07 kem Exp $

AC_PREREQ([2.60])
AC_INIT([xhost], [1.0.4],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg], [xhost])
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_MAINTAINER_MODE

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
	  [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)

AM_CONFIG_HEADER(config.h)

AC_TYPE_SIGNAL

XORG_DEFAULT_OPTIONS

# Checks for pkg-config packages
PKG_CHECK_MODULES(XHOST, x11 xmuu xau)

# Transport selection macro from xtrans.m4
XTRANS_CONNECTION_FLAGS

# Secure RPC detection macro from xtrans.m4
XTRANS_SECURE_RPC_FLAGS

AC_OUTPUT([Makefile])
