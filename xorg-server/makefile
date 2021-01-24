ifneq ($(MAKESERVER),1)
$(error Please specify MAKESERVER=1 on the command line or as environment variable)
endif

INCLUDELIBFILES =  \
 composite\$(OBJDIR)\libcomposite.lib \
 config\$(OBJDIR)\libconfig.lib \
 damageext\$(OBJDIR)\libdamageext.lib \
 dbe\$(OBJDIR)\libdbe.lib \
 dix\$(OBJDIR)\libdix.lib \
 fb\$(OBJDIR)\libfb.lib \
 glx\$(OBJDIR)\libglx.lib \
 hw\xwin\$(OBJDIR)\libXWin.lib \
 hw\xwin\glx\$(OBJDIR)\libwinglx.lib \
 hw\xwin\winclipboard\$(OBJDIR)\libXWinclipboard.lib \
 mi\$(OBJDIR)\libmi.lib \
 miext\damage\$(OBJDIR)\libdamage.lib \
 miext\shadow\$(OBJDIR)\libshadow.lib \
 miext\sync\$(OBJDIR)\libsync.lib \
 miext\rootless\$(OBJDIR)\librootless.lib \
 os\$(OBJDIR)\libos.lib \
 randr\$(OBJDIR)\librandr.lib \
 record\$(OBJDIR)\librecord.lib \
 Xext\$(OBJDIR)\libxext.lib \
 xfixes\$(OBJDIR)\libxfixes.lib \
 Xi\$(OBJDIR)\libXi.lib \
 xkb\$(OBJDIR)\libxkb.lib \
 render\$(OBJDIR)\librender.lib \
 present\$(OBJDIR)\libpresent.lib \
 pseudoramiX\$(OBJDIR)\libPseudoramiX.lib

INCLUDENOSERVLIBFILES =  \
 $(MHMAKECONF)\libX11\$(NOSERVOBJDIR)\libX11.lib \
 $(MHMAKECONF)\libxcb\src\$(NOSERVOBJDIR)\libxcb.lib \
 $(MHMAKECONF)\pixman\pixman\$(NOSERVOBJDIR)\libpixman-1.lib \
 $(MHMAKECONF)\libXdmcp\$(NOSERVOBJDIR)\libXdmcp.lib \
 $(MHMAKECONF)\libXau\$(NOSERVOBJDIR)\libXau.lib \
 $(MHMAKECONF)\libXfixes\src\$(NOSERVOBJDIR)\libXfixes.lib \
 $(MHMAKECONF)\libXfont2\$(NOSERVOBJDIR)\libXfont2.lib \
 $(MHMAKECONF)\libfontenc\src\$(NOSERVOBJDIR)\libfontenc.lib \
 $(MHMAKECONF)\libXinerama\src\$(NOSERVOBJDIR)\libXinerama.lib \
 $(MHMAKECONF)\zlib\$(NOSERVOBJDIR)\zlib1.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))
NOSERVLIBDIRS=$(dir $(INCLUDENOSERVLIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=$(MAKESERVER) DEBUG=$(DEBUG);)
load_makefile $(NOSERVLIBDIRS:%$(NOSERVOBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

OBJS = dix\$(OBJDIR)\main.obj

LINKLIBS += $(PTHREADLIB) $(FREETYPELIB) $(OPENSSLLIB) opengl32.lib dwmapi.lib

ifeq ($(DEBUG),1)
TTYAPP=vcxsrv
APP:=$(TTYAPP)
else
WINAPP=vcxsrv
APP:=$(WINAPP)
endif

RCINCLUDES += include hw\xwin $(MHMAKECONF)\include $(MHMAKECONF)

XWin.rc: hw\xwin\XWin.rc
	copy $< $@

RESOURCES = XWin.rc

$(OBJDIR)\$(APP).exe: $(LINKLIBS)

load_makefile fonts.src\makefile MAKESERVER=0 DEBUG=$(DEBUG)
load_makefile xkeyboard-config\makefile MAKESERVER=0 DEBUG=$(DEBUG)

EXTRASTOBUILD =  \
 hw\xwin\xlaunch\$(NOSERVOBJDIR)\xlaunch.exe \
 ..\xkbcomp\$(NOSERVOBJDIR)\xkbcomp.exe \
 ..\apps\xcalc\$(NOSERVOBJDIR)\xcalc.exe \
 ..\apps\xclock\$(NOSERVOBJDIR)\xclock.exe \
 ..\apps\xwininfo\$(NOSERVOBJDIR)\xwininfo.exe \
 ..\apps\xhost\$(NOSERVOBJDIR)\xhost.exe \
 ..\apps\xrdb\$(NOSERVOBJDIR)\xrdb.exe \
 ..\apps\xauth\$(NOSERVOBJDIR)\xauth.exe \
 ..\tools\plink\$(NOSERVOBJDIR)\plink.exe \
 hw\xwin\swrastwgl_dri\$(NOSERVOBJDIR)\swrastwgl_dri.dll \
 ..\dxtn\$(NOSERVOBJDIR)\dxtn.dll

EXTRASTOBUILDDIRS=$(dir $(EXTRASTOBUILD))

load_makefile $(EXTRASTOBUILDDIRS:%$(NOSERVOBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)
all: $(EXTRASTOBUILD)

load_makefile $(MHMAKECONF)\mesalib\src\makefile MAKESERVER=0 DEBUG=$(DEBUG)
all: $(MHMAKECONF)\mesalib\src\$(NOSERVOBJDIR)\swrast_dri.lib $(MHMAKECONF)\mesalib\src\$(NOSERVOBJDIR)\swrast_dri.dll

all: fonts.src\all xkeyboard-config\all

