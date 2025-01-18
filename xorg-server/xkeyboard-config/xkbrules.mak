DIRFILE=$(THISDIR:%=$(DESTDIR)\..\%.dir)

.PHONY: destdir
destdir: $(DESTDIR)

all: $(DIRFILE)

ifneq ($(DIRFILE),)
#bdftopcf is dependent on libX11.dll, so we need to add the directory of the libX11 dll to the path env variable
PATH:=$(relpath $(MHMAKECONF)\libxcb\src\$(OBJDIR))\;$(relpath $(MHMAKECONF)\libX11\$(OBJDIR))\;$(relpath $(MHMAKECONF)\libXau\$(OBJDIR))\;$(PATH)
export PATH

load_makefile ..\..\..\xkbcomp\makefile MAKESERVER=0 DEBUG=$(DEBUG)

$(DIRFILE): ..\..\..\xkbcomp\$(NOSERVOBJDIR)\xkbcomp.exe
	-del -e $@
	cd $(DESTDIR) & ..\..\..\xkbcomp\$(NOSERVOBJDIR)\xkbcomp.exe -lfhlpR -o $(relpath $@) *
endif
