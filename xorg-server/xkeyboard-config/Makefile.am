AUTOMAKE_OPTIONS = foreign

SUBDIRS = compat geometry keycodes po rules symbols types docs man

pkgconfigdir = $(datadir)/pkgconfig
pkgconfig_DATA = xkeyboard-config.pc

mesonfiles = meson.build \
	     meson_options.txt \
	     po/meson.build \
	     rules/meson.build \
	     rules/compat/meson.build \
	     $(NULL)

EXTRA_DIST=config.rpath COPYING README \
       autogen.sh \
       xkeyboard-config.pc.in \
       $(mesonfiles)

ACLOCAL_AMFLAGS = -I m4

# we don't care about runtime dependencies during distcheck
AM_DISTCHECK_CONFIGURE_FLAGS=--disable-runtime-deps

sync-po:
	rsync -Lrtvz translationproject.org::tp/latest/xkeyboard-config/  po
