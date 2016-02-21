AC_INIT(xkeyboard-config, 2.17)
AC_CONFIG_SRCDIR(rules/base.xml.in)
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_MAINTAINER_MODE

# Require X.Org macros 1.12 or later for XORG_WITH_XSLTPROC
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.12 or later before
          running autoconf/autogen])])
XORG_MACROS_VERSION(1.12)
XORG_MANPAGE_SECTIONS
XORG_WITH_XSLTPROC
AC_PROG_SED

AC_SUBST(VERSION)

AC_ARG_WITH( xkb_base,
             [AS_HELP_STRING([--with-xkb-base=DIR],[XKB base path @<:@DATADIR/X11/xkb@:>@])],
             xkb_base="$withval", 
             xkb_base="${datadir}/X11/xkb" )

AC_ARG_WITH( xkb_rules_symlink,
             [  --with-xkb-rules-symlink=NAME1(,NAME2)*        create symlink(s) to "old style" rules files (xfree86 and/or xorg)],
             xkb_rules_symlink="$withval" )

AC_ARG_ENABLE( compat_rules,
               [  --enable-compat-rules      create compatibility rules],
               enable_compat_rules="$enableval",
               enable_compat_rules="yes" )

# xkeyboard-config does not have build-time dependencies. However, it does
# have run-time dependencies and keyboard layouts may not work without the
# right libX11 or xproto installed.
# By default, we enable these run-time dependencies as build-time
# dependencies so that those building on their local machines are warned
# that the resulting build may not work.
AC_ARG_ENABLE( runtime-deps,
               [ --enable-runtime-deps    use run-time dependencies as build-time dependencies],
               enable_runtime_deps="$enableval",
               enable_runtime_deps="yes")

if test "x$enable_runtime_deps" = "xyes"; then
    PKG_CHECK_MODULES(DEPS, [xproto >= 7.0.20] [x11 >= 1.4.3], [have_deps=yes], [have_deps=no])
    if test "x$have_deps" = "xno" ; then
        AC_MSG_WARN([
                 Required dependencies not found. These dependencies are
                 run-time dependencies only and not required for building.
                 Skip this check with --disable-runtime-deps.

                 Installing this version of xkeyboard-config on a host
                 without the required dependencies may result in unusable
                 keyboard layouts.
                 ])

        AC_MSG_ERROR([$DEPS_PKG_ERRORS])
    fi
fi

AM_CONDITIONAL(CREATE_RULES_SYMLINK, test "x$xkb_rules_symlink" != "x")

AM_CONDITIONAL(USE_COMPAT_RULES, test "x$enable_compat_rules" = "xyes")

# ****
# i18n
# ****
AC_PROG_INTLTOOL(0.30)

AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])

GETTEXT_PACKAGE=xkeyboard-config
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Gettext package])

ALL_LINGUAS="af az bg ca crh cs da de el en_GB eo es fi fr gl hr hu id it ja ka ko ky lt nb nl pl pt_BR ro ru rw sk sl sq sr sv tr uk vi zh_CN zh_TW"

AC_SUBST(xkb_base)
AC_SUBST(xkb_rules_symlink)

AC_OUTPUT([ po/Makefile.in
Makefile
compat/Makefile
geometry/Makefile
geometry/digital_vndr/Makefile
geometry/sgi_vndr/Makefile
keycodes/Makefile
keycodes/digital_vndr/Makefile
keycodes/sgi_vndr/Makefile
rules/Makefile
rules/bin/Makefile
rules/compat/Makefile
symbols/Makefile
symbols/digital_vndr/Makefile
symbols/fujitsu_vndr/Makefile
symbols/hp_vndr/Makefile
symbols/jolla_vndr/Makefile
symbols/macintosh_vndr/Makefile
symbols/nec_vndr/Makefile
symbols/nokia_vndr/Makefile
symbols/sgi_vndr/Makefile
symbols/sharp_vndr/Makefile
symbols/sony_vndr/Makefile
symbols/sun_vndr/Makefile
symbols/xfree68_vndr/Makefile
types/Makefile
xkeyboard-config.pc
docs/Makefile
man/Makefile
])

echo '***********************************************************'
echo "  $PACKAGE_NAME is configured with the following parameters:"
echo "    XKB base directory: $xkb_base"
if test -z "$xkb_rules_symlink" ; then
  echo "    Symbolic link(s) to legacy rules are not created"
else
  echo "    Symbolic link(s) to legacy rules: $xkb_rules_symlink"
fi

if test "$enable_compat_rules" = "yes" ; then
  echo "    Compatibility rules are included"
else
  echo "    Compatibility rules are not included"
fi
echo '***********************************************************'

