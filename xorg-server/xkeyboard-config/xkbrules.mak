DIRFILE=$(THISDIR:%=$(DESTDIR)\..\%.dir)

.PHONY: destdir
destdir: $(DESTDIR)

all: destdir $(DATA_FILES) $(DIRFILE)

$(DESTDIR)\default: default.in
	copy $< $@

$(DESTDIR)\%: %
	copy $< $@

ifneq ($(DIRFILE),)
.PHONY: extrastuff

#bdftopcf is dependent on libX11.dll, so we need to add the directory of the libX11 dll to the path env variable
PATH:=$(relpath $(MHMAKECONF)\libxcb\src\$(OBJDIR))\;$(relpath $(MHMAKECONF)\libX11\$(OBJDIR))\;$(relpath $(MHMAKECONF)\libXau\$(OBJDIR))\;$(PATH)
export PATH

load_makefile ..\..\..\xkbcomp\makefile MAKESERVER=0 DEBUG=0

$(DIRFILE): extrastuff $(DATA_FILES) ..\..\..\xkbcomp\obj$(OBJDIREXTRA)\release\xkbcomp.exe
	-del -e $@
	cd $(DESTDIR) & ..\..\..\xkbcomp\obj$(OBJDIREXTRA)\release\xkbcomp.exe -lfhlpR -o $(relpath $@) *
endif
