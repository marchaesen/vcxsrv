WINAPP = xcalc

DEFINES += XT_NO_SM IEEE

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
 $(MHMAKECONF)\libXpm\src\$(OBJDIR)\libXpm.lib \
 $(MHMAKECONF)\libxau\$(OBJDIR)\libXau.lib \
 $(MHMAKECONF)\libXaw\src\$(OBJDIR)\libXaw.lib \
 $(MHMAKECONF)\libXt\src\$(OBJDIR)\libXt.lib \
 $(MHMAKECONF)\libXext\src\$(OBJDIR)\libXext.lib \
 $(MHMAKECONF)\libXmu\src\$(OBJDIR)\libXmu.lib \
 $(MHMAKECONF)\libX11\src\$(OBJDIR)\libX11.lib
LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2d.lib
else
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2.lib
endif

CSRCS =	\
        actions.c \
        math.c \
        xcalc.c
