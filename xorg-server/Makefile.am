AUTOMAKE_OPTIONS=nostdinc

# Required for automake < 1.14
ACLOCAL_AMFLAGS = -I m4

if COMPOSITE
COMPOSITE_DIR=composite
endif

if GLX
GLX_DIR=glx
endif

if DBE
DBE_DIR=dbe
endif

if RECORD
RECORD_DIR=record
endif

if DRI3
DRI3_DIR=dri3
endif

if PRESENT
PRESENT_DIR=present
endif

if PSEUDORAMIX
PSEUDORAMIX_DIR=pseudoramiX
endif

if GLAMOR
GLAMOR_DIR=glamor
endif

SUBDIRS = \
	doc \
	man \
	include \
	dix  \
	fb \
	mi \
	Xext \
	miext \
	os \
	randr \
	render  \
	Xi \
	xkb \
	$(PSEUDORAMIX_DIR) \
	$(DBE_DIR) \
	$(RECORD_DIR) \
	xfixes \
	damageext \
	$(COMPOSITE_DIR) \
	$(GLX_DIR) \
	$(PRESENT_DIR) \
	$(DRI3_DIR) \
	exa \
	$(GLAMOR_DIR) \
	config \
	hw \
	test

if XORG
aclocaldir = $(datadir)/aclocal
aclocal_DATA = xorg-server.m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = xorg-server.pc
endif

EXTRA_DIST = xorg-server.pc.in xorg-server.m4 autogen.sh

DISTCHECK_CONFIGURE_FLAGS=\
	--with-xkb-path=$(XKB_BASE_DIRECTORY) \
	--with-xkb-bin-directory=$(XKB_BIN_DIRECTORY) \
	--with-xkb-output='$${datadir}/X11/xkb/compiled'

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL

DIST_SUBDIRS = \
	doc \
	man \
	include \
	dix  \
	fb \
	mi \
	Xext \
	miext \
	os \
	pseudoramiX \
	randr \
	render  \
	Xi \
	xkb \
	dbe \
	record \
	xfixes \
	damageext \
	composite \
	glx \
	exa \
	glamor \
	config \
	dri3 \
	present \
	hw \
	test

# gross hack
relink: all
	$(AM_V_at)$(MAKE) -C hw relink

install-headers: Makefile
	+find . -name Makefile | while read m; do			\
		if grep -q install-sdkHEADERS $$m; then			\
			(cd `dirname "$$m"` && make install-sdkHEADERS)	\
		fi							\
	done
