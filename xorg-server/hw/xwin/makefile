LIBRARY = libXWin

XWIN_GLX_WINDOWS=1       # everything is in the glx makefile

if XWIN_GLX_WINDOWS
GLX_DIR = glx
DEFS_GLX_WINDOWS = -DXWIN_GLX_WINDOWS
XWIN_GLX_LIBS = $(top_builddir)/hw/xwin/glx/libXwinGLX.la
XWIN_GLX_LINK_FLAGS = -lopengl32
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
	winos.c \
	winprefs.c \
	winprefsyacc.y \
	winprefslex.l \
	winprocarg.c \
	winscrinit.c \
	winshadddnl.c \
	winshadgdi.c \
	wintaskbar.c \
	wintrayicon.c \
	winvalargs.c \
	winwakeup.c \
	winwindow.c \
	winwndproc.c \
	ddraw.h \
	winconfig.h \
	win.h \
	winglobals.h \
	winkeybd.h \
	winkeynames.h \
	winlayouts.h \
	winmessages.h \
	winmonitors.h \
	winmsg.h \
	winms.h \
	winmultiwindowclass.h \
	winmultiwindowicons.h \
	winprefs.h \
	winresource.h \
	winwindow.h \
	windisplay.c \
	windisplay.h \
	XWin.rc \
	winclipboardinit.c \
	winclipboardwrappers.c \
	winmultiwindowshape.c \
	winmultiwindowwindow.c \
	winmultiwindowwm.c \
	winmultiwindowwndproc.c \
	winSetAppUserModelID.c \
	winrandr.c \
	$(SRCS_XV) \
	xdmcphostselect.c

CSRCS=$(filter %.c,$(SRCS)) $(filter %.y,$(SRCS)) $(filter %.l,$(SRCS))
CSRCS := $(CSRCS:%.y=%.c)
CSRCS := $(CSRCS:%.l=%.c)

 DEFS = $(DEFS_GLX_WINDOWS) \
	$(DEFS_XV)

DEFINES += $(DEFS:-D%=%) HAVE_XWIN_CONFIG_H PTW32_STATIC_LIB RELOCATE_PROJECTROOT READ_FONTDIRS PIXMAN_API=

INCLUDES += winclipboard ..\.. ..\..\dix ..\..\miext\rootless

INCLUDES += $(OBJDIR)

$(OBJDIR)\winprefslex.c $(OBJDIR)\winprefsyacc.c $(OBJDIR)\winprefsyacc.h: winprefslex.l winprefsyacc.y
	doflexbison.bat $(subst \,/,$(OBJDIR))

