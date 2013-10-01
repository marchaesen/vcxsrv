
# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([libXpm], [3.5.11],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg], [libXpm])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])

# Initialize libtool
AC_PROG_LIBTOOL

# Require X.Org macros 1.8 or later for MAN_SUBSTS set by XORG_MANPAGE_SECTIONS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.8 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

# Checks for library functions
AC_CHECK_FUNCS([strlcat])

# Obtain compiler/linker options for dependencies
PKG_CHECK_MODULES(XPM, xproto x11)
PKG_CHECK_MODULES(SXPM, [x11 xt xext xextproto xproto >= 7.0.17],
                  [build_sxpm=true], [build_sxpm=false])
AM_CONDITIONAL(BUILD_SXPM, test x$build_sxpm = xtrue)

# Internationalization & localization support
AC_SEARCH_LIBS([gettext], [intl], [USE_GETTEXT="yes"], [USE_GETTEXT="no"])
AC_MSG_CHECKING([where to install localized messages])
AC_ARG_WITH([localedir], AS_HELP_STRING([--with-localedir=<path>],
	[Path to install message files in (default: datadir/locale)]),
	[LOCALEDIR=${withval}], [LOCALEDIR=${datadir}/locale])
AX_DEFINE_DIR([LOCALEDIR], [LOCALEDIR], [Location of translated messages])
if test "x$LOCALEDIR" = "xno" -o "x$USE_GETTEXT" = "xno" ; then
	AC_MSG_RESULT([nowhere])
	USE_GETTEXT="no"
else
	AC_MSG_RESULT([$LOCALEDIR])
fi

if test "x$USE_GETTEXT" = "xyes" ; then
	AC_DEFINE([USE_GETTEXT], 1,
		  [Define to 1 if you want to use the gettext() function.])
fi
AM_CONDITIONAL(USE_GETTEXT, test "x$USE_GETTEXT" = "xyes")

# Optional feature: When ___.xpm is requested, also look for ___.xpm.Z & .gz
# Replaces ZFILEDEF = -DSTAT_ZFILE in old Imakefile
AC_ARG_ENABLE(stat-zfile,
	AS_HELP_STRING([--enable-stat-zfile],
			[Search for files with .Z & .gz extensions automatically @<:@default=yes@:>@]),
              [STAT_ZFILE=$enableval], [STAT_ZFILE=yes])
if test x$STAT_ZFILE = xyes ; then
	AC_DEFINE(STAT_ZFILE, 1, [Define to 1 to automatically look for files with .Z & .gz extensions])
fi


case $host_os in
	*mingw*)
                AC_DEFINE(NO_ZPIPE, 1, [Define to 1 to disable decompression via pipes])
	;;
	*)
	;;
esac

AC_CONFIG_FILES([Makefile
                 doc/Makefile
                 include/Makefile
                 man/Makefile
                 src/Makefile
                 sxpm/Makefile
                 cxpm/Makefile
                 xpm.pc])
AC_OUTPUT
