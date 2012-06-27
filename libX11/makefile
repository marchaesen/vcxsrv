SHAREDLIB=libX11

INCLUDELIBFILES =  \
 modules\im\ximcp\$(OBJDIR)\libximcp.lib \
 src\xlibi18n\$(OBJDIR)\libi18n.lib \
 src\$(OBJDIR)\libx11.lib \
 src\xcms\$(OBJDIR)\libxcms.lib \
 src\xkb\$(OBJDIR)\libxkb.lib \
 modules\om\generic\$(OBJDIR)\libxomGeneric.lib \
 modules\lc\utf8\$(OBJDIR)\libxlcUTF8Load.lib \
 modules\lc\def\$(OBJDIR)\libxlcDef.lib \
 modules\lc\gen\$(OBJDIR)\liblcGenConvLoad.lib \
 modules\lc\xlocale\$(OBJDIR)\libxlocale.lib \
 $(MHMAKECONF)\libXau\$(OBJDIR)\libXau.lib \
 $(MHMAKECONF)\libxcb\src\$(OBJDIR)\libxcb.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2d.lib
else
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2.lib
endif

load_makefile nls\makefile MAKESERVER=0 DEBUG=0

$(OBJDIR)\libX11.lib : nls\all
