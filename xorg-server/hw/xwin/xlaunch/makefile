INCLUDELIBFILES = window\$(OBJDIR)\window.lib \
                  $(MHMAKECONF)\libX11\$(OBJDIR)\libX11.lib \
                  $(MHMAKECONF)\libxcb\src\$(OBJDIR)\libxcb.lib \
                  $(MHMAKECONF)\libXau\$(OBJDIR)\libXau.lib

CSRCS=config.cc main.cc

INCLUDES += $(MHMAKECONF)\libxml2\include

WINAPP = xlaunch

RESOURCES = resources.rc

$(OBJDIR)\%.res : resources\%.rc $(wildcard resources\*.rc) 
	$(RC) $(RCFLAGS) $(RCDEFINES:%=-d "%") $(RCINCLUDES:%=-i %) -Fo$(relpath $@) $<

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

LINKLIBS += $(MHMAKECONF)\libxml2\lib\libxml2.lib

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2d.lib
else
LINKLIBS += $(MHMAKECONF)\pthreads\pthreadVC2.lib
endif

