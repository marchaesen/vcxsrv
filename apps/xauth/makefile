TTYAPP = xauth

INCLUDELIBFILES = \
 ..\common\$(OBJDIR)\common.lib \
 $(MHMAKECONF)\libxcb\src\$(OBJDIR)\libxcb.lib \
 $(MHMAKECONF)\libXau\$(OBJDIR)\libXau.lib \
 $(MHMAKECONF)\libXmu\src\$(OBJDIR)\libXmu.lib \
 $(MHMAKECONF)\libXext\src\$(OBJDIR)\libXext.lib \
 $(MHMAKECONF)\libX11\$(OBJDIR)\libX11.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

LINKLIBS += $(PTHREADLIB)

INCLUDES += $(MHMAKECONF)\include\xcb

CSRCS =	\
        gethost.c \
        parsedpy.c \
        process.c \
        xauth.c

