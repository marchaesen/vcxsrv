DEFINES += BAD_ARPAINET

TTYAPP = xhost

INCLUDELIBFILES = \
 ..\common\$(OBJDIR)\common.lib \
 $(MHMAKECONF)\libxcb\src\$(OBJDIR)\libxcb.lib \
 $(MHMAKECONF)\libxau\$(OBJDIR)\libXau.lib \
 $(MHMAKECONF)\libXmu\src\$(OBJDIR)\libXmu.lib \
 $(MHMAKECONF)\libX11\$(OBJDIR)\libX11.lib \

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2d.lib
else
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2.lib
endif

CSRCS = xhost.c
