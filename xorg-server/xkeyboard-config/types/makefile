ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

THISDIR=types

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\$(THISDIR)

dist_xkbdata_types_DATA = \
	basic cancel caps complete default.in extra iso9995 level5 mousekeys nokia numpad pc README

DATA_FILES=$(dist_xkbdata_types_DATA:%=$(DESTDIR)\%)

include ..\xkbrules.mak
