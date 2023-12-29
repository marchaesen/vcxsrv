SUBDIRS = man
bin_PROGRAMS = xhost

xhost_SOURCES = xhost.c

AM_CFLAGS = $(CWARNFLAGS) $(XHOST_CFLAGS) $(XAU_CFLAGS)
xhost_LDADD = $(XHOST_LIBS)

MAINTAINERCLEANFILES = ChangeLog INSTALL

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL

EXTRA_DIST = README.md

if USE_GETTEXT
AM_CPPFLAGS = -DLOCALEDIR='"$(LOCALEDIR)"'

noinst_DATA = xhost.po

xhost.po: $(xhost_SOURCES:%=$(srcdir)/%)
	$(AM_V_GEN)xgettext -d xhost -n $(xhost_SOURCES:%=$(srcdir)/%)

CLEANFILES = xhost.po
endif
