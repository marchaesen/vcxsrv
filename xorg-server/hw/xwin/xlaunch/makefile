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

LINKLIBS += $(LIBXMLLIB) $(PTHREADLIB)

