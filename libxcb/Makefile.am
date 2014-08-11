ACLOCAL_AMFLAGS=-I m4

SUBDIRS=src tests doc man

pkgconfigdir = $(libdir)/pkgconfig

pkgconfig_DATA = xcb.pc

if BUILD_COMPOSITE
pkgconfig_DATA += xcb-composite.pc
endif
if BUILD_DAMAGE
pkgconfig_DATA += xcb-damage.pc
endif
if BUILD_DPMS
pkgconfig_DATA += xcb-dpms.pc
endif
if BUILD_DRI2
pkgconfig_DATA += xcb-dri2.pc
endif
if BUILD_DRI3
pkgconfig_DATA += xcb-dri3.pc
endif
if BUILD_GLX
pkgconfig_DATA += xcb-glx.pc
endif
if BUILD_PRESENT
pkgconfig_DATA += xcb-present.pc
endif
if BUILD_RANDR
pkgconfig_DATA += xcb-randr.pc
endif
if BUILD_RECORD
pkgconfig_DATA += xcb-record.pc
endif
if BUILD_RENDER
pkgconfig_DATA += xcb-render.pc
endif
if BUILD_RESOURCE
pkgconfig_DATA += xcb-res.pc
endif
if BUILD_SCREENSAVER
pkgconfig_DATA += xcb-screensaver.pc
endif
if BUILD_SHAPE
pkgconfig_DATA += xcb-shape.pc
endif
if BUILD_SHM
pkgconfig_DATA += xcb-shm.pc
endif
if BUILD_SYNC
pkgconfig_DATA += xcb-sync.pc
endif
if BUILD_XEVIE
pkgconfig_DATA += xcb-xevie.pc
endif
if BUILD_XFREE86_DRI
pkgconfig_DATA += xcb-xf86dri.pc
endif
if BUILD_XFIXES
pkgconfig_DATA += xcb-xfixes.pc
endif
if BUILD_XINERAMA
pkgconfig_DATA += xcb-xinerama.pc
endif
if BUILD_XINPUT
pkgconfig_DATA += xcb-xinput.pc
endif
if BUILD_XKB
pkgconfig_DATA += xcb-xkb.pc
endif
if BUILD_XPRINT
pkgconfig_DATA += xcb-xprint.pc
endif
if BUILD_SELINUX
pkgconfig_DATA += xcb-xselinux.pc
endif
if BUILD_XTEST
pkgconfig_DATA += xcb-xtest.pc
endif
if BUILD_XV
pkgconfig_DATA += xcb-xv.pc
endif
if BUILD_XVMC
pkgconfig_DATA += xcb-xvmc.pc
endif


AM_TESTS_ENVIRONMENT = \
	AM_SRCDIR=${srcdir}

TESTS=check-pc-requires

EXTRA_DIST = \
tools/README \
tools/api_conv.pl \
tools/constants \
autogen.sh \
$(TESTS)

MAINTAINERCLEANFILES = ChangeLog INSTALL

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL
