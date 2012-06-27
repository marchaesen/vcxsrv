WINAPP = xclock

DEFINES += XT_NO_SM XRENDER

INCLUDELIBFILES = \
 $(MHMAKECONF)\libxcb\src\$(OBJDIR)\libxcb.lib \
 $(MHMAKECONF)\libxkbfile\src\$(OBJDIR)\libxkbfile.lib \
 $(MHMAKECONF)\libXpm\src\$(OBJDIR)\libXpm.lib \
 $(MHMAKECONF)\libXau\$(OBJDIR)\libXau.lib \
 $(MHMAKECONF)\libXaw\src\$(OBJDIR)\libXaw.lib \
 $(MHMAKECONF)\libXt\src\$(OBJDIR)\libXt.lib \
 $(MHMAKECONF)\libXext\src\$(OBJDIR)\libXext.lib \
 $(MHMAKECONF)\libXmu\src\$(OBJDIR)\libXmu.lib \
 $(MHMAKECONF)\libX11\$(OBJDIR)\libX11.lib \
 $(MHMAKECONF)\libXft\src\$(OBJDIR)\libXft.lib \
 $(MHMAKECONF)\libXrender\src\$(OBJDIR)\libXrender.lib \
 $(MHMAKECONF)\fontconfig\src\$(OBJDIR)\libfontconfig.lib \
 $(MHMAKECONF)\libXfont\src\util\$(OBJDIR)\libutil.lib

INCLUDES += $(MHMAKECONF)\libXft\include $(MHMAKECONF)\freetype\include $(MHMAKECONF)\fontconfig

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2d.lib \
            $(MHMAKECONF)\freetype\lib\freetype2410MT_D.lib
else
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2.lib \
            $(MHMAKECONF)\freetype\lib\freetype2410MT.lib
endif

LINKLIBS += $(MHMAKECONF)\libxml2\lib\libxml2.lib

CSRCS =	\
        Clock.c \
        xclock.c

