gldir = $(includedir)/GL
gl_HEADERS = \
	include/GL/glxint.h \
	include/GL/glxmd.h \
	include/GL/glxproto.h \
	include/GL/glxtokens.h

glinternaldir = $(includedir)/GL/internal
glinternal_HEADERS = \
	include/GL/internal/glcore.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = glproto.pc

EXTRA_DIST = meson.build

MAINTAINERCLEANFILES = ChangeLog INSTALL

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL
