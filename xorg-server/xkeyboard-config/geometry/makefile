SUBDIRS = digital_vndr  sgi_vndr

ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

THISDIR=geometry

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\$(THISDIR)

load_makefile $(SUBDIRS:%=%\makefile MAKESERVER=0 DEBUG=$(DEBUG);)

include ..\xkbrules.mak
