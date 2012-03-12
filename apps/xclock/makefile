WINAPP = xclock

DEFINES += XT_NO_SM XRENDER

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
 $(MHMAKECONF)\libxkbfile\src\$(OBJDIR)\libxkbfile.lib \
 $(MHMAKECONF)\libXpm\src\$(OBJDIR)\libXpm.lib \
 $(MHMAKECONF)\libxau\$(OBJDIR)\libXau.lib \
 $(MHMAKECONF)\libXaw\src\$(OBJDIR)\libXaw.lib \
 $(MHMAKECONF)\libXt\src\$(OBJDIR)\libXt.lib \
 $(MHMAKECONF)\libXext\src\$(OBJDIR)\libXext.lib \
 $(MHMAKECONF)\libXmu\src\$(OBJDIR)\libXmu.lib \
 $(MHMAKECONF)\libX11\src\$(OBJDIR)\libX11.lib \
 $(MHMAKECONF)\libXft\src\$(OBJDIR)\libXft.lib \
 $(MHMAKECONF)\libXrender\src\$(OBJDIR)\libXrender.lib \
 $(MHMAKECONF)\fontconfig\src\$(OBJDIR)\libfontconfig.lib \
 $(MHMAKECONF)\libXfont\src\util\$(OBJDIR)\libutil.lib
 
INCLUDES += $(MHMAKECONF)\libXft\include $(MHMAKECONF)\freetype\include $(MHMAKECONF)\fontconfig

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2d.lib \
            $(MHMAKECONF)\freetype\lib\freetype249MT_D.lib
else
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2.lib \
            $(MHMAKECONF)\freetype\lib\freetype249MT.lib
endif

LINKLIBS += $(MHMAKECONF)\libxml2\lib\libxml2.lib

CSRCS =	\
        Clock.c \
        xclock.c

