ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\keycodes\digital_vndr

dist_keycodes_DATA = lk pc

DATA_FILES=$(dist_keycodes_DATA:%=$(DESTDIR)\%)

include ..\..\xkbrules.mak
