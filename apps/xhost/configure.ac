dnl Process this file with autoconf to create configure.

# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([xhost], [1.0.9],
        [https://gitlab.freedesktop.org/xorg/app/xhost/-/issues], [xhost])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-xz])

# Set common system defines for POSIX extensions, such as _GNU_SOURCE
# Must be called before any macros that run the compiler (like
# XORG_DEFAULT_OPTIONS) to avoid autoconf errors.
AC_USE_SYSTEM_EXTENSIONS

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
	  [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

# Checks for pkg-config packages
# - Both CFLAGS & LIBS needed:
PKG_CHECK_MODULES(XHOST, [xproto >= 7.0.22 x11 xmuu])
# - Only CFLAGS needed, not libs:
PKG_CHECK_MODULES(XAU, xau)

# Transport selection macro from xtrans.m4
XTRANS_CONNECTION_FLAGS

# Secure RPC detection macro from xtrans.m4
XTRANS_SECURE_RPC_FLAGS

# Link with winsock if mingw target
case $host_os in
 *mingw*)
  AC_CHECK_LIB([ws2_32],[main])
  ;;
 *)
  ;;
esac

# Checks for library functions.
AC_CHECK_FUNCS([inet_aton])

# Internationalization & localization support
AC_SEARCH_LIBS([gettext], [intl], [USE_GETTEXT="yes"], [USE_GETTEXT="no"])
AC_MSG_CHECKING([where to install localized messages])
AC_ARG_WITH([localedir], AS_HELP_STRING([--with-localedir=<path>],
	[Path to install message files in (default: datadir/locale)]),
	[LOCALEDIR=${withval}], [LOCALEDIR=${datadir}/locale])
if test "x$LOCALEDIR" = "xno" -o "x$USE_GETTEXT" = "xno" ; then
	AC_MSG_RESULT([nowhere])
	USE_GETTEXT="no"
else
	AC_SUBST([LOCALEDIR])
	AC_MSG_RESULT([$LOCALEDIR])
fi

if test "x$USE_GETTEXT" = "xyes" ; then
	AC_DEFINE([USE_GETTEXT], 1,
		  [Define to 1 if you want to use the gettext() function.])
fi
AM_CONDITIONAL(USE_GETTEXT, test "x$USE_GETTEXT" = "xyes")


AC_CONFIG_FILES([
	Makefile
	man/Makefile])
AC_OUTPUT
