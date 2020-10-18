SUBDIRS = include src doc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = xmu.pc xmuu.pc

MAINTAINERCLEANFILES = ChangeLog INSTALL

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL

if LINT
# Check source code with tools like lint & sparse
lint:
	(cd src && $(MAKE) $(MFLAGS) lint)
endif LINT
