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
 $(MHMAKECONF)\libXau\$(OBJDIR)\libXau.lib \
 $(MHMAKECONF)\libXdmcp\$(OBJDIR)\libXdmcp.lib \
 $(MHMAKECONF)\libXfont\src\util\$(OBJDIR)\libutil.lib \
 $(MHMAKECONF)\libXfont\src\fc\$(OBJDIR)\libfc.lib \
 $(MHMAKECONF)\libXfont\src\fontfile\$(OBJDIR)\libfontfile.lib \
 $(MHMAKECONF)\libXfont\src\builtins\$(OBJDIR)\libbuiltins.lib \
 $(MHMAKECONF)\libXfont\src\bitmap\$(OBJDIR)\libbitmap.lib \
 $(MHMAKECONF)\pixman\pixman\$(OBJDIR)\libpixman-1.lib \
 $(MHMAKECONF)\libX11\modules\im\ximcp\$(OBJDIR)\libximcp.lib \
 $(MHMAKECONF)\libX11\src\xlibi18n\$(OBJDIR)\libi18n.lib \
 $(MHMAKECONF)\libX11\src\$(OBJDIR)\libx11.lib \
 $(MHMAKECONF)\libX11\src\xcms\$(OBJDIR)\libxcms.lib \
 $(MHMAKECONF)\libxcb\src\$(OBJDIR)\libxcb.lib \
 $(MHMAKECONF)\libX11\src\xkb\$(OBJDIR)\libxkb.lib \
 $(MHMAKECONF)\libX11\modules\om\generic\$(OBJDIR)\libxomGeneric.lib \
 $(MHMAKECONF)\libX11\modules\lc\utf8\$(OBJDIR)\libxlcUTF8Load.lib \
 $(MHMAKECONF)\libX11\modules\lc\def\$(OBJDIR)\libxlcDef.lib \
 $(MHMAKECONF)\libX11\modules\lc\gen\$(OBJDIR)\liblcGenConvLoad.lib \
 $(MHMAKECONF)\zlib\$(OBJDIR)\libz.lib \
 $(MHMAKECONF)\libX11\modules\lc\xlocale\$(OBJDIR)\libxlocale.lib \
 $(MHMAKECONF)\libfontenc\src\$(OBJDIR)\libfontenc.lib \
 $(MHMAKECONF)\libXfont\src\freetype\$(OBJDIR)\libft.lib \
 $(MHMAKECONF)\libXfont\src\stubs\$(OBJDIR)\libstubs.lib \
 $(MHMAKECONF)\libXinerama\src\$(OBJDIR)\libXinerama.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=$(MAKESERVER) DEBUG=$(DEBUG);)

OBJS = dix\$(OBJDIR)\main.obj

ifeq ($(DEBUG),1)
TTYAPP=vcxsrv_dbg
APP:=$(TTYAPP)

LINKLIBS += $(MHMAKECONF)\openssl\out32_d\libeay32.lib \
            $(MHMAKECONF)\freetype\lib\freetype2410MT_D.lib \
            $(MHMAKECONF)\pthreads\pthreadVC2d.lib

$(OBJDIR)\$(TTYAPP).exe: $(LINKLIBS)

else
WINAPP=vcxsrv
APP:=$(WINAPP)

LINKLIBS += $(MHMAKECONF)\openssl\out32\libeay32.lib \
            $(MHMAKECONF)\freetype\lib\freetype2410MT.lib \
            $(MHMAKECONF)\pthreads\pthreadVC2.lib

endif

RCINCLUDES += include hw\xwin $(MHMAKECONF)\include $(MHMAKECONF)

XWin.rc: hw\xwin\XWin.rc
	copy $< $@

RESOURCES = XWin.rc

$(OBJDIR)\$(APP).exe: $(LINKLIBS)

XErrorDB: ..\libX11\src\XErrorDB
	copy $< $@

%.exe: $(OBJDIR)\%.exe
	copy $< $@

load_makefile hw\xwin\xlaunch\makefile MAKESERVER=0 DEBUG=0
xlaunch.exe: hw\xwin\xlaunch\obj\release\xlaunch.exe
	copy $< $@

load_makefile ..\xkbcomp\makefile MAKESERVER=0 DEBUG=0
xkbcomp.exe: ..\xkbcomp\obj\release\xkbcomp.exe
	copy $< $@

%: ..\apps\xcalc\app-defaults\%
	copy $< $@

load_makefile ..\apps\xcalc\makefile MAKESERVER=0 DEBUG=0
xcalc.exe: ..\apps\xcalc\obj\release\xcalc.exe XCalc XCalc-color
	copy $< $@

%: ..\apps\xclock\app-defaults\%
	copy $< $@

load_makefile ..\apps\xclock\makefile MAKESERVER=0 DEBUG=0
xclock.exe: ..\apps\xclock\obj\release\xclock.exe XClock XClock-color
	copy $< $@

load_makefile ..\apps\xwininfo\makefile MAKESERVER=0 DEBUG=0
xwininfo.exe: ..\apps\xwininfo\obj\release\xwininfo.exe
	copy $< $@

load_makefile ..\apps\xhost\makefile MAKESERVER=0 DEBUG=0
xhost.exe: ..\apps\xhost\obj\release\xhost.exe
	copy $< $@

load_makefile ..\apps\xauth\makefile MAKESERVER=0 DEBUG=0
xauth.exe: ..\apps\xauth\obj\release\xauth.exe
	copy $< $@

load_makefile ..\tools\plink\makefile MAKESERVER=0 DEBUG=0
plink.exe: ..\tools\plink\obj\release\plink.exe
	copy $< $@

load_makefile ..\libX11\nls\makefile MAKESERVER=0 DEBUG=0
load_makefile fonts.src\makefile MAKESERVER=0 DEBUG=0
load_makefile xkeyboard-config\makefile MAKESERVER=0 DEBUG=0

all: $(APP).exe xlaunch.exe xkbcomp.exe protocol.txt XErrorDB xhost.exe xauth.exe \
     ..\libX11\nls\all fonts.src\all xkeyboard-config\all plink.exe xclock.exe xcalc.exe \
     xwininfo.exe swrast_dri_dbg.dll swrast_dri.dll dxtn.dll dxtn_dbg.dll \
     swrastwgl_dri.dll swrastwgl_dri_dbg.dll


load_makefile ..\mesalib\windows\VC8\mesa\makefile MAKESERVER=0 DEBUG=0
swrast_dri.dll: ..\mesalib\windows\VC8\mesa\Release\swrast_dri.dll
	copy $< $@

swrast_dri_dbg.dll: ..\mesalib\windows\VC8\mesa\Debug\swrast_dri.dll
	copy $< $@

load_makefile hw\xwin\swrastwgl_dri\makefile MAKESERVER=0 DEBUG=0
swrastwgl_dri.dll: hw\xwin\swrastwgl_dri\obj\release\swrastwgl_dri.dll
	copy $< $@

load_makefile hw\xwin\swrastwgl_dri\makefile MAKESERVER=0 DEBUG=1
swrastwgl_dri_dbg.dll: hw\xwin\swrastwgl_dri\obj\debug\swrastwgl_dri.dll
	copy $< $@


load_makefile ..\dxtn\makefile MAKESERVER=0 DEBUG=0
dxtn.dll: ..\dxtn\obj\release\dxtn.dll
	copy $< $@

load_makefile ..\dxtn\makefile MAKESERVER=0 DEBUG=1
dxtn_dbg.dll: ..\dxtn\obj\debug\dxtn.dll
	copy $< $@

mesaopengl32.dll: ..\mesalib\lib\mesaopengl32.dll
	copy $< $@

protocol.txt: dix\protocol.txt
	copy $< $@


