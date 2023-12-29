
# Initialize Autoconf
AC_PREREQ([2.60])
AC_INIT([libXpm], [3.5.17],
        [https://gitlab.freedesktop.org/xorg/lib/libxpm/-/issues/], [libXpm])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-xz])

# Initialize libtool
LT_INIT

# Require X.Org macros 1.16 or later for XORG_MEMORY_CHECK_FLAGS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.16 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.16)
XORG_DEFAULT_OPTIONS

# Checks for library functions
AC_CHECK_FUNCS([strlcat])
AC_CHECK_FUNC([fork],[], AC_DEFINE(NO_ZPIPE))

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

dnl Helper macro to find absolute path to program and add a #define for it
AC_DEFUN([XPM_PATH_PROG],[
AC_PATH_PROG([$1], [$2], [])
AS_IF([test "x$$1" = "x"], [$3],
      [AC_DEFINE_UNQUOTED([$1], ["$$1"], [Path to $2])])
]) dnl End of AC_DEFUN([XPM_PATH_PROG]...

# Optional feature: When a filename ending in .Z or .gz is requested,
# open a pipe to a newly forked compress/uncompress/gzip command to
# handle it.
AC_MSG_CHECKING([whether to handle compressed pixmaps])
case $host_os in
        *mingw*)        zpipe_default="no" ;;
        *)              zpipe_default="yes" ;;
esac
AC_ARG_ENABLE(open-zfile,
        AS_HELP_STRING([--enable-open-zfile],
                        [Search for files with .Z & .gz extensions automatically @<:@default=auto@:>@]),
              [OPEN_ZFILE=$enableval], [OPEN_ZFILE=yes])
AC_MSG_RESULT([$OPEN_ZFILE])
AM_CONDITIONAL(COMPRESSED_PIXMAPS, test "x$OPEN_ZFILE" = "xyes")
if test x$OPEN_ZFILE = xno ; then
        AC_DEFINE(NO_ZPIPE, 1, [Define to 1 to disable decompression via pipes])
else
        # gzip is absolutely required for the compressed file handling code
        XPM_PATH_PROG([XPM_PATH_GZIP], [gzip],
            [AC_MSG_ERROR([gzip not found, set XPM_PATH_GZIP or use --disable-open-zfile])])

        # if compress is not found, we disable writing to .Z files,
        # but leave the rest of the compression code active
        XPM_PATH_PROG([XPM_PATH_COMPRESS], [compress],
            [AC_MSG_WARN([compress not found, disabling writing of .Z files])])
        # if uncompress is not found, we use gzip to read .Z files
        XPM_PATH_PROG([XPM_PATH_UNCOMPRESS], [uncompress])

        AC_CHECK_FUNCS([closefrom close_range], [break])
fi

# Optional feature: When ___.xpm is requested, also look for ___.xpm.Z & .gz
# Replaces ZFILEDEF = -DSTAT_ZFILE in old Imakefile
AC_MSG_CHECKING([whether to search for compressed pixmaps])
AC_ARG_ENABLE(stat-zfile,
        AS_HELP_STRING([--enable-stat-zfile],
                        [Search for files with .Z & .gz extensions automatically @<:@default=auto@:>@]),
              [STAT_ZFILE=$enableval], [STAT_ZFILE=$OPEN_ZFILE])
AC_MSG_RESULT([$STAT_ZFILE])
if test x$STAT_ZFILE = xyes ; then
        AC_DEFINE(STAT_ZFILE, 1, [Define to 1 to automatically look for files with .Z & .gz extensions])
fi

# --enable-unit-tests
AC_REQUIRE_AUX_FILE([tap-driver.sh])
XORG_ENABLE_UNIT_TESTS
XORG_WITH_GLIB([2.46])
if test "x$have_glib" = x"yes"; then
       AC_CHECK_FUNCS([g_pattern_spec_match_string])
fi
XORG_MEMORY_CHECK_FLAGS

AC_CONFIG_FILES([Makefile
                 doc/Makefile
                 include/Makefile
                 man/Makefile
                 src/Makefile
                 sxpm/Makefile
                 cxpm/Makefile
                 test/Makefile
                 xpm.pc])
AC_OUTPUT
