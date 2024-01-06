ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\symbols\sun_vndr

dist_symbols_DATA = ara be br ca ch cz de dk ee es fi fr gb gr it jp kr lt lv nl no pl pt ro ru se sk solaris tr tw ua us

DATA_FILES=$(dist_symbols_DATA:%=$(DESTDIR)\%)

include ..\..\xkbrules.mak
