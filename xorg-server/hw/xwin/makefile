LIBRARY = libXWin

XWIN_MULTIWINDOW=1
XWIN_CLIPBOARD=1
XWIN_MULTIWINDOWEXTWM=0  # currently not working correctly
XWIN_MULTIWINDOWINTWM=0  # currently not working correctly
XWIN_RANDR=1
XWIN_NATIVEGDI=0         # currently not working correctly
XWIN_PRIMARYFB=0         # currently not working correctly
XWIN_GLX_WINDOWS=1       # everything is in the glx makefile

if XWIN_CLIPBOARD
SRCS_CLIPBOARD = \
	winclipboardinit.c \
	winclipboardwrappers.c
DEFS_CLIPBOARD = -DXWIN_CLIPBOARD
INCLUDES_CLIPBOARD = winclipboard
endif

if XWIN_GLX_WINDOWS
GLX_DIR = glx
DEFS_GLX_WINDOWS = -DXWIN_GLX_WINDOWS
XWIN_GLX_LIBS = $(top_builddir)/hw/xwin/glx/libXwinGLX.la
XWIN_GLX_LINK_FLAGS = -lopengl32
endif

if XWIN_MULTIWINDOW
SRCS_MULTIWINDOW = \
	winmultiwindowshape.c \
	winmultiwindowwindow.c \
	winmultiwindowwm.c \
	winmultiwindowwndproc.c \
	winSetAppUserModelID.c
DEFS_MULTIWINDOW = -DXWIN_MULTIWINDOW
endif

if XWIN_MULTIWINDOWEXTWM
SRCS_MULTIWINDOWEXTWM = \
	winwin32rootless.c \
	winwin32rootlesswindow.c \
	winwin32rootlesswndproc.c \
	winwindowswm.c
DEFS_MULTIWINDOWEXTWM = -DXWIN_MULTIWINDOWEXTWM
MULTIWINDOWEXTWM_LIBS = $(top_builddir)/miext/rootless/librootless.la
endif

if XWIN_NATIVEGDI
SRCS_NATIVEGDI = \
	winclip.c \
	winfillsp.c \
	winfont.c \
	wingc.c \
	wingetsp.c \
	winnativegdi.c \
	winpixmap.c \
	winpolyline.c \
	winrop.c \
	winsetsp.c
DEFS_NATIVEGDI = -DXWIN_NATIVEGDI
endif

if XWIN_PRIMARYFB
SRCS_PRIMARYFB = \
	winpfbdd.c
DEFS_PRIMARYFB = -DXWIN_PRIMARYFB
endif

if XWIN_RANDR
SRCS_RANDR = \
	winrandr.c
DEFS_RANDR = -DXWIN_RANDR
endif

if XWIN_XV
SRCS_XV = \
	winvideo.c
DEFS_XV = -DXWIN_XV
endif

SRCS =	InitInput.c \
	InitOutput.c \
	winallpriv.c \
	winauth.c \
	winblock.c \
	wincmap.c \
	winconfig.c \
	wincreatewnd.c \
	wincursor.c \
	windialogs.c \
	winengine.c \
	winerror.c \
	winglobals.c \
	winkeybd.c \
	winkeyhook.c \
	winmisc.c \
	winmonitors.c \
	winmouse.c \
	winmsg.c \
	winmsgwindow.c \
	winmultiwindowclass.c \
	winmultiwindowicons.c \
	winprefs.c \
	winprefsyacc.y \
	winprefslex.l \
	winprocarg.c \
	winscrinit.c \
	winshaddd.c \
	winshadddnl.c \
	winshadgdi.c \
	wintaskbar.c \
	wintrayicon.c \
	winvalargs.c \
	winwakeup.c \
	winwindow.c \
	winwndproc.c \
	ddraw.h \
	winclipboard.h \
	winconfig.h \
	win.h \
	winkeybd.h \
	winkeynames.h \
	winlayouts.h \
	winmessages.h \
	winmsg.h \
	winms.h \
	winmultiwindowclass.h \
	winprefs.h \
	winresource.h \
	winwindow.h \
	windisplay.c \
	XWin.rc \
	$(SRCS_CLIPBOARD) \
	$(SRCS_MULTIWINDOW) \
	$(SRCS_MULTIWINDOWEXTWM) \
	$(SRCS_NATIVEGDI) \
	$(SRCS_PRIMARYFB) \
	$(SRCS_RANDR) \
	$(SRCS_XV) \
	xdmcphostselect.c

CSRCS=$(filter %.c,$(SRCS)) $(filter %.y,$(SRCS)) $(filter %.l,$(SRCS))
CSRCS := $(CSRCS:%.y=%.c)
CSRCS := $(CSRCS:%.l=%.c)

 DEFS = $(DEFS_CLIPBOARD) \
	$(DEFS_GLX_WINDOWS) \
	$(DEFS_MULTIWINDOW) \
	$(DEFS_MULTIWINDOWEXTWM) \
	$(DEFS_NATIVEGDI) \
	$(DEFS_PRIMARYFB) \
	$(DEFS_RANDR) \
	$(DEFS_XV)

DEFINES += $(DEFS:-D%=%) HAVE_XWIN_CONFIG_H PTW32_STATIC_LIB RELOCATE_PROJECTROOT

INCLUDES += $(INCLUDES_CLIPBOARD) ..\.. ..\..\miext\rootless

INCLUDES += $(OBJDIR)

$(OBJDIR)\winprefslex.c $(OBJDIR)\winprefsyacc.c $(OBJDIR)\winprefsyacc.h: winprefslex.l winprefsyacc.y
	doflexbison.bat $(subst \,/,$(OBJDIR))

