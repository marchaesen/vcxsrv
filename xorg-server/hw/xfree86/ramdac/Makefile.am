noinst_LTLIBRARIES = libramdac.la

libramdac_la_SOURCES = xf86RamDac.c xf86RamDacCmap.c \
                      xf86Cursor.c xf86HWCurs.c IBM.c BT.c TI.c \
		      xf86BitOrder.c

sdk_HEADERS = BT.h IBM.h TI.h xf86Cursor.h xf86RamDac.h

DISTCLEANFILES = xf86BitOrder.c
EXTRA_DIST = BTPriv.h IBMPriv.h TIPriv.h xf86CursorPriv.h xf86RamDacPriv.h \
	CURSOR.NOTES

AM_CFLAGS = -DXAAReverseBitOrder=xf86ReverseBitOrder -DRAMDAC_MODULE \
            $(DIX_CFLAGS) $(XORG_CFLAGS)
INCLUDES = $(XORG_INCS)

xf86BitOrder.c:
	$(AM_V_GEN)echo "#define XAAReverseBitOrder xf86ReverseBitOrder" > $@
	$(AM_V_GEN)echo "#include \"$(srcdir)/../xaa/xaaBitOrder.c\"" >> $@
