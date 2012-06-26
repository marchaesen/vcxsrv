LIBRARY = libXt

INCLUDES := .. ../include ../include/X11 $(INCLUDES)

DEFINES += XT_NO_SM ERRORDB="""XtErrorDB"""

CSRCS = \
	ActionHook.c \
	Alloc.c \
	ArgList.c \
	Callback.c \
	ClickTime.c \
	Composite.c \
	Constraint.c \
	Convert.c \
	Converters.c \
	Core.c \
	Create.c \
	Destroy.c \
	Display.c \
	Error.c \
	Event.c \
	EventUtil.c \
	Functions.c \
	GCManager.c \
	Geometry.c \
	GetActKey.c \
	GetResList.c \
	GetValues.c \
	HookObj.c \
	Hooks.c \
	Initialize.c \
	Intrinsic.c \
	Keyboard.c \
	Manage.c \
	NextEvent.c \
	Object.c \
	PassivGrab.c \
	Pointer.c \
	Popup.c \
	PopupCB.c \
	RectObj.c \
	ResConfig.c \
	Resources.c \
	Selection.c \
	SetSens.c \
	SetValues.c \
	SetWMCW.c \
	Shell.c \
	TMaction.c \
	TMgrab.c \
	TMkey.c \
	TMparse.c \
	TMprint.c \
	TMstate.c \
	Threads.c \
	VarCreate.c \
	VarGet.c \
	Varargs.c \
	Vendor.c \
	StringDefs.c

BUILT_SOURCE = StringDefs.c 
BUILT_HEADER = ../include/X11/StringDefs.h ../include/X11/Shell.h
BUILT_FILES = $(BUILT_SOURCE) $(BUILT_HEADER)

STRING_LIST = ../util/string.list

$(BUILT_HEADER): $(BUILT_SOURCE)

load_makefile ../util/makefile MAKESERVER=0 DEBUG=$(DEBUG)

$(BUILT_SOURCE) $(BUILT_HEADER) : ../util/$(OBJDIR)/makestrs.exe $(STRING_LIST) ../util/StrDefs.ht ../util/StrDefs.ct ../util/Shell.ht
	../util/$(OBJDIR)/makestrs -i .. $(STRINGSABIOPTIONS) < $(STRING_LIST) > StringDefs.c
	move StringDefs.h ..\include\X11
	move Shell.h ..\include\X11
