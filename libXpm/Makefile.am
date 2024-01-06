# Daniel Stone disowns all copyright on this file; no warranty is given as to its
# suitability or otherwise.

SUBDIRS = doc include man src sxpm cxpm test

ACLOCAL_AMFLAGS = -I m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = xpm.pc

EXTRA_DIST = COPYRIGHT NEWS.old README.md

MAINTAINERCLEANFILES = ChangeLog INSTALL

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL
