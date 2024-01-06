ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\keycodes\sgi_vndr

dist_keycodes_DATA = indigo indy iris

DATA_FILES=$(dist_keycodes_DATA:%=$(DESTDIR)\%)

include ..\..\xkbrules.mak
