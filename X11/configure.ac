AC_PREREQ([2.60])
AC_INIT([Xproto], [7.0.27],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg])
AM_INIT_AUTOMAKE([foreign dist-bzip2])

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

AC_CONFIG_HEADERS([do-not-use-config.h])
AC_CONFIG_HEADERS([Xfuncproto.h])

AC_CANONICAL_HOST

m4_ifdef([AS_HELP_STRING], , [m4_define([AS_HELP_STRING], m4_defn([AC_HELP_STRING]))])

# Check for enable/disable options
AC_ARG_ENABLE(function-prototypes,
	AS_HELP_STRING([--enable-function-prototypes],
		       [force function prototypes (default: auto)]),
	[FUNCPROTO=$enableval], [FUNCPROTO=auto])
AC_ARG_ENABLE(varargs-prototypes,
	AS_HELP_STRING([--enable-varargs-prototypes],
		       [varargs prototypes are supported (default: auto)]),
	[VAPROTO=$enableval], [VAPROTO=auto])
AC_ARG_ENABLE(const-prototypes,
	AS_HELP_STRING([--enable-const-prototypes],
		       [const is supported (default: auto)]),
	[CONSTPROTO=$enableval], [CONSTPROTO=auto])
AC_ARG_ENABLE(nested-prototypes,
	AS_HELP_STRING([--enable-nested-prototypes],
		       [nested prototypes are supported (default: auto)]),
	[NESTEDPROTO=$enableval], [NESTEDPROTO=auto])
AC_ARG_ENABLE(wide-prototypes,
	AS_HELP_STRING([--enable-wide-prototypes],
		       [widen function prototypes (default: auto)]),
	[WIDEPROTO=$enableval], [WIDEPROTO=auto])


AC_CHECK_HEADERS([sys/select.h sys/param.h sys/types.h sys/time.h])

# Handle Xpoll.h.in
# Avoid determining fds_bits on WIN32 hosts (not including cygwin)
case $host_os in
	mingw*)		fds_bits_found=true;;
	*)		fds_bits_found=false;;
esac

if test "x$fds_bits_found" = xfalse ; then
   AC_CHECK_MEMBER(fd_set.fds_bits, 
			   [
			       fds_bits_found=plain
			       USE_FDS_BITS="fds_bits"
			   ],,
			   [
                               #ifdef HAVE_SYS_PARAM_H
                               #include <sys/param.h>
                               #endif
                               #ifdef HAVE_SYS_TYPES_H
                               #include <sys/types.h>
                               #endif
                               #ifdef HAVE_SYS_TIME_H
                               #include <sys/time.h>
                               #endif
                               #ifdef HAVE_SYS_SELECT_H
			       #include <sys/select.h>
                               #endif
			   ])
fi

if test "x$fds_bits_found" = xfalse ; then
   AC_CHECK_MEMBER(fd_set.__fds_bits,
			[
			    fds_bits_found=underscores
			    USE_FDS_BITS="__fds_bits"
			],,
			[
                               #ifdef HAVE_SYS_PARAM_H
                               #include <sys/param.h>
                               #endif
                               #ifdef HAVE_SYS_TYPES_H
                               #include <sys/types.h>
                               #endif
                               #ifdef HAVE_SYS_TIME_H
                               #include <sys/time.h>
                               #endif
                               #ifdef HAVE_SYS_SELECT_H
			       #include <sys/select.h>
                               #endif
			])
fi

if test "x$fds_bits_found" = xfalse ; then
    AC_MSG_ERROR([Could not determine how to access the fds_bits or equivalent
                  structure in fd_set on your platform.])
fi

AC_SUBST([USE_FDS_BITS])


# Handle Xfuncproto.h
AC_MSG_CHECKING([whether const prototypes are supported])
if test "x$CONSTPROTO" = xauto; then
	case $host_os in
		linux*)		CONSTPROTO=yes ;;
		freebsd*)	CONSTPROTO=yes ;;
		netbsd*)	CONSTPROTO=yes ;;
		openbsd*)	CONSTPROTO=yes ;;
		solaris*)	CONSTPROTO=yes ;;
		*)		CONSTPROTO=no ;;
	esac
fi
AC_MSG_RESULT([$CONSTPROTO])

AC_MSG_CHECKING([whether varargs prototypes are supported])
if test "x$VAPROTO" = xauto; then
	case $host_os in
		linux*)		VAPROTO=yes ;;
		freebsd*)	VAPROTO=yes ;;
		netbsd*)	VAPROTO=yes ;;
		openbsd*)	VAPROTO=yes ;;
		solaris*)	VAPROTO=yes ;;
		*)		VAPROTO=no ;;
	esac
fi
AC_MSG_RESULT([$VAPROTO])

AC_MSG_CHECKING([whether nested prototypes are supported])
if test "x$NESTEDPROTO" = xauto; then
	case $host_os in
		linux*)		NESTEDPROTO=yes ;;
		freebsd*)	NESTEDPROTO=yes ;;
		netbsd*)	NESTEDPROTO=yes ;;
		openbsd*)	NESTEDPROTO=yes ;;
		solaris*)	NESTEDPROTO=yes ;;
		*)		NESTEDPROTO=no ;;
	esac
fi
AC_MSG_RESULT([$NESTEDPROTO])

AC_MSG_CHECKING([whether to force function prototypes])
if test "x$FUNCPROTO" = xauto; then
	case $host_os in
	    linux*)	FUNCPROTO=yes ;;
	    freebsd*)	FUNCPROTO=yes ;;
	    netbsd*)	FUNCPROTO=yes ;;
	    openbsd*)	FUNCPROTO=yes ;;
	    *)
		if test "x$VAPROTO" = xyes && test "x$NESTEDPROTO" = xyes; then
			FUNCPROTO=yes
		else
			FUNCPROTO=no
		fi
		;;
	esac
fi
AC_MSG_RESULT([$FUNCPROTO])

# Logic for this was taken from Imake.tmpl
if test "x$FUNCPROTO" = xyes && test "x$VAPROTO" = xyes && test "x$NESTEDPROTO" = xyes && test "x$CONSTPROTO" = xyes; then
		AC_DEFINE(FUNCPROTO, [15], [FUNCPROTO])
else
  if test "x$FUNCPROTO" = xyes && test "x$VAPROTO" = xyes && test "x$NESTEDPROTO" = xyes; then
		AC_DEFINE(FUNCPROTO, [11], [FUNCPROTO])
  else
    if test "x$FUNCPROTO" = xyes && test "x$NESTEDPROTO" = xyes; then
		AC_DEFINE(FUNCPROTO, [9], [FUNCPROTO])
    else
      if test "x$FUNCPROTO" = xyes && test "x$VAPROTO" = xyes && test "x$CONSTPROTO" = xyes; then
		AC_DEFINE(FUNCPROTO, [7], [FUNCPROTO])
      else
        if test "x$FUNCPROTO" = xyes && test "x$CONSTPROTO" = xyes; then
		AC_DEFINE(FUNCPROTO, [5], [FUNCPROTO])
        else
          if test "x$FUNCPROTO" = xyes && test "x$VAPROTO" = xyes; then
		AC_DEFINE(FUNCPROTO, [3], [FUNCPROTO])
          else
            if test "x$FUNCPROTO" = xyes; then
		AC_DEFINE(FUNCPROTO, [], [FUNCPROTO])
            fi
          fi
        fi
      fi
    fi
  fi
fi

AC_MSG_CHECKING([whether to widen function prototypes])
if test "x$WIDEPROTO" = xauto; then
	case $host_os in
		linux*)		WIDEPROTO=no ;;
		freebsd*)	WIDEPROTO=no ;;
		netbsd*)	WIDEPROTO=no ;;
		openbsd*)	WIDEPROTO=no ;;
		*)		WIDEPROTO=yes ;;
	esac
fi
AC_MSG_RESULT([$WIDEPROTO])

if test "x$WIDEPROTO" = xno; then
	AC_DEFINE(NARROWPROTO, [], [Narrow prototypes])
fi


AC_CONFIG_FILES([Makefile
           specs/Makefile
           specs/SIAddresses/Makefile
           Xpoll.h
           xproto.pc])
AC_OUTPUT
