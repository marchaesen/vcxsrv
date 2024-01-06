SUBDIRS = digital_vndr  sgi_vndr

ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

THISDIR=geometry

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\$(THISDIR)

geom_DATA = \
 ataritt dell fujitsu hp kinesis nec northgate README sony sun thinkpad winbook\
 amiga chicony everex hhk keytronic macintosh microsoft nokia pc sanwa steelseries teck typematrix

DATA_FILES=$(geom_DATA:%=$(DESTDIR)\%)

load_makefile $(SUBDIRS:%=%\makefile MAKESERVER=0 DEBUG=$(DEBUG);)

extrastuff: $(SUBDIRS:%=%\all)

include ..\xkbrules.mak
