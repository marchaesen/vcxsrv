TTYAPP = xwininfo

INCLUDELIBFILES = \
 $(MHMAKECONF)\libX11\src\xkb\$(OBJDIR)\libxkb.lib \
 $(MHMAKECONF)\libX11\src\xcms\$(OBJDIR)\libxcms.lib \
 $(MHMAKECONF)\libX11\src\xlibi18n\$(OBJDIR)\libi18n.lib \
 $(MHMAKECONF)\libX11\modules\lc\xlocale\$(OBJDIR)\libxlocale.lib \
 $(MHMAKECONF)\libX11\modules\lc\utf8\$(OBJDIR)\libxlcUTF8Load.lib \
 $(MHMAKECONF)\libX11\modules\lc\def\$(OBJDIR)\libxlcDef.lib \
 $(MHMAKECONF)\libX11\modules\om\generic\$(OBJDIR)\libxomGeneric.lib \
 $(MHMAKECONF)\libX11\modules\lc\gen\$(OBJDIR)\liblcGenConvLoad.lib \
 $(MHMAKECONF)\libX11\modules\im\ximcp\$(OBJDIR)\libximcp.lib \
 $(MHMAKECONF)\libxcb\src\$(OBJDIR)\libxcb.lib \
 $(MHMAKECONF)\libxau\$(OBJDIR)\libXau.lib \
 $(MHMAKECONF)\libXext\src\$(OBJDIR)\libXext.lib \
 $(MHMAKECONF)\libX11\src\$(OBJDIR)\libx11.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2d.lib
else
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2.lib
endif

CSRCS = \
        clientwin.c \
        dsimple.c \
        xwininfo.c

