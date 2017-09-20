ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

THISDIR=geometry

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\$(THISDIR)

dist_xkbdata_geometry_DATA = \
amiga ataritt chicony \
dell everex fujitsu \
hhk hp keytronic kinesis \
macintosh microsoft nec nokia \
northgate pc sanwa sony thinkpad \
sun teck steelseries typematrix winbook README

DATA_FILES=$(dist_xkbdata_geometry_DATA:%=$(DESTDIR)\%)

SUBDIRS = digital_vndr  sgi_vndr

load_makefile $(SUBDIRS:%=%\makefile MAKESERVER=0 DEBUG=$(DEBUG);)

extrastuff: $(SUBDIRS:%=%\all)

include ..\xkbrules.mak
