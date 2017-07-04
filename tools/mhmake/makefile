ifdef VS2008
MHMAKESLNFILE=mhmake.sln
BUILDCMD=vcbuild $(MHMAKESLNFILE)
else
MHMAKESLNFILE=mhmakevc10.sln
BUILDCMD=devenv.com $(MHMAKESLNFILE) /build
endif

ifeq ($(IS64),1)
SUBCONF=|x64
SUBCONFDIR=64
else
SUBCONF=|Win32
endif
.PHONY: all mhmake_dbg mhmake cleanthis

all: debug$(SUBCONFDIR)\mhmake_dbg.exe release$(SUBCONFDIR)\mhmake.exe

DEPS=$(wildcard src\*)

debug$(SUBCONFDIR)\mhmake_dbg.exe: $(DEPS)
	$(BUILDCMD) "Debug$(SUBCONF)" 

release$(SUBCONFDIR)\mhmake.exe: $(DEPS)
	$(BUILDCMD) "Release$(SUBCONF)" 

clean: cleanthis

cleanthis:
	del -e Debug
	del -e Release
