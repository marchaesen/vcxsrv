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
 present\$(OBJDIR)\libpresent.lib

INCLUDENOSERVLIBFILES =  \
 $(MHMAKECONF)\libX11\$(NOSERVOBJDIR)\libX11.lib \
 $(MHMAKECONF)\libxcb\src\$(NOSERVOBJDIR)\libxcb.lib \
 $(MHMAKECONF)\pixman\pixman\$(NOSERVOBJDIR)\libpixman-1.lib \
 $(MHMAKECONF)\libXdmcp\$(NOSERVOBJDIR)\libXdmcp.lib \
 $(MHMAKECONF)\libXau\$(NOSERVOBJDIR)\libXau.lib \
 $(MHMAKECONF)\libXfont\src\util\$(NOSERVOBJDIR)\libutil.lib \
 $(MHMAKECONF)\libXfont\src\fc\$(NOSERVOBJDIR)\libfc.lib \
 $(MHMAKECONF)\libXfont\src\fontfile\$(NOSERVOBJDIR)\libfontfile.lib \
 $(MHMAKECONF)\libXfont\src\builtins\$(NOSERVOBJDIR)\libbuiltins.lib \
 $(MHMAKECONF)\libXfont\src\bitmap\$(NOSERVOBJDIR)\libbitmap.lib \
 $(MHMAKECONF)\libXfont\src\freetype\$(NOSERVOBJDIR)\libft.lib \
 $(MHMAKECONF)\libXfont\src\stubs\$(NOSERVOBJDIR)\libstubs.lib \
 $(MHMAKECONF)\libfontenc\src\$(NOSERVOBJDIR)\libfontenc.lib \
 $(MHMAKECONF)\libXinerama\src\$(NOSERVOBJDIR)\libXinerama.lib \
 $(MHMAKECONF)\zlib\$(NOSERVOBJDIR)\zlib1.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))
NOSERVLIBDIRS=$(dir $(INCLUDENOSERVLIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=$(MAKESERVER) DEBUG=$(DEBUG);)
load_makefile $(NOSERVLIBDIRS:%$(NOSERVOBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

OBJS = dix\$(OBJDIR)\main.obj

LINKLIBS += $(PTHREADLIB) $(FREETYPELIB) $(OPENSSLLIB)

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

load_makefile fonts.src\makefile MAKESERVER=0 DEBUG=0
load_makefile xkeyboard-config\makefile MAKESERVER=0 DEBUG=0

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

load_makefile ..\mesalib\windows\VC8\mesa\makefile MAKESERVER=0 DEBUG=$(DEBUG)
ifdef IS64
TARGETDIR=x64
else
TARGETDIR=Win32
endif

ifeq ($(DEBUG),1)
all: ..\mesalib\windows\VC8\mesa\$(TARGETDIR)\Debug\swrast_dri.dll
else
all: ..\mesalib\windows\VC8\mesa\$(TARGETDIR)\Release\swrast_dri.dll
endif

all: fonts.src\all xkeyboard-config\all

