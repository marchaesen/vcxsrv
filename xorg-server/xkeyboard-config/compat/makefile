ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

THISDIR=compat

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\$(THISDIR)

dist_xkbdata_compat_DATA = \
accessx basic caps complete \
iso9995 \
japan ledcaps ledcompose \
lednum ledscroll level5 \
misc mousekeys \
olpc pc pc98 xfree86 \
xtest README

DATA_FILES=$(dist_xkbdata_compat_DATA:%=$(DESTDIR)\%)

include ..\xkbrules.mak
