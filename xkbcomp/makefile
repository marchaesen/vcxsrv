INCLUDELIBFILES = $(MHMAKECONF)\libx11\src\$(OBJDIR)\libx11.lib \
                  $(MHMAKECONF)\libx11\src\xkb\$(OBJDIR)\libxkb.lib \
                  $(MHMAKECONF)\libx11\src\xlibi18n\$(OBJDIR)\libi18n.lib \
                  $(MHMAKECONF)\libx11\modules\lc\xlocale\$(OBJDIR)\libxlocale.lib \
                  $(MHMAKECONF)\libx11\modules\lc\utf8\$(OBJDIR)\libxlcUTF8Load.lib \
                  $(MHMAKECONF)\libx11\modules\lc\def\$(OBJDIR)\libxlcDef.lib \
                  $(MHMAKECONF)\libx11\modules\om\generic\$(OBJDIR)\libxomGeneric.lib \
                  $(MHMAKECONF)\libx11\modules\lc\gen\$(OBJDIR)\liblcGenConvLoad.lib \
                  $(MHMAKECONF)\libx11\modules\im\ximcp\$(OBJDIR)\libximcp.lib \
                  $(MHMAKECONF)\libxcb\src\$(OBJDIR)\libxcb.lib \
                  $(MHMAKECONF)\libxau\$(OBJDIR)\libxau.lib \
                  $(MHMAKECONF)\libxkbfile\src\$(OBJDIR)\libxkbfile.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=$(MAKESERVER) DEBUG=$(DEBUG);)

ifeq $(DEBUG),1
TTYAPP = xkbcomp
else
WINAPP = xkbcomp
endif

DEFINES += DFLT_XKB_CONFIG_ROOT="\".\""  PACKAGE_VERSION="\"1.2.3\""

INCLUDES += $(OBJDIR)

CSRCS = action.c \
        alias.c \
        compat.c \
        expr.c \
        geometry.c \
        indicators.c \
        keycodes.c \
        keymap.c \
        keytypes.c \
        listing.c \
        misc.c \
        parseutils.c \
        symbols.c \
        utils.c \
        vmod.c \
        xkbcomp.c \
        xkbparse.c \
        xkbpath.c \
        xkbscan.c

LINKLIBS += $(MHMAKECONF)\openssl\out32\libeay32.lib

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\freetype\lib\freetype249MT_D.lib \
            $(MHMAKECONF)\pthreads\pthreadVC2d.lib
else
LINKLIBS += $(MHMAKECONF)\freetype\lib\freetype249MT.lib \
            $(MHMAKECONF)\pthreads\pthreadVC2.lib
endif

$(OBJDIR)\xkbparse.c $(OBJDIR)\xkbparse.h: xkbparse.y
	bison.bat -d -o$(OBJDIR)\xkbparse.c $<
