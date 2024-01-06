ifeq ($(MAKESERVER),1)
$(error Please do not specify MAKESERVER=1)
endif

THISDIR=compat

DESTDIR=$(MHMAKECONF)\xorg-server\xkbdata\$(THISDIR)

dist_xkbdata_compat_DATA = \
 basic complete japan ledcompose ledscroll Makefile mousekeys pc README xtest\
 accessx caps iso9995 ledcaps lednum level5 misc olpc pc98 xfree86

DATA_FILES=$(dist_xkbdata_compat_DATA:%=$(DESTDIR)\%)

include ..\xkbrules.mak
