ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

THISDIR=keycodes

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\$(THISDIR)

SUBDIRS = digital_vndr sgi_vndr

dist_xkbdata_keycodes_DATA = \
aliases \
amiga \
ataritt \
empty \
evdev \
fujitsu \
hp \
ibm \
jolla \
macintosh \
olpc \
sony \
sun \
xfree86 \
xfree98 \
README

DATA_FILES=$(dist_xkbdata_keycodes_DATA:%=$(DESTDIR)\%)

load_makefile $(SUBDIRS:%=%\makefile MAKESERVER=0 DEBUG=$(DEBUG);)

extrastuff: $(SUBDIRS:%=%\all)

include ..\xkbrules.mak
