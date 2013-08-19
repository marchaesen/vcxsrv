noinst_LTLIBRARIES = libramdac.la

libramdac_la_SOURCES = xf86RamDac.c xf86RamDacCmap.c \
                      xf86Cursor.c xf86HWCurs.c IBM.c BT.c TI.c

sdk_HEADERS = BT.h IBM.h TI.h xf86Cursor.h xf86RamDac.h

EXTRA_DIST = BTPriv.h IBMPriv.h TIPriv.h xf86CursorPriv.h xf86RamDacPriv.h \
	CURSOR.NOTES

AM_CFLAGS = $(DIX_CFLAGS) $(XORG_CFLAGS)
AM_CPPFLAGS = $(XORG_INCS)
