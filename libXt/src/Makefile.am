lib_LTLIBRARIES = libXt.la

BUILT_SOURCES = $(BUILT_FILES)
CLEANFILES = $(BUILT_SOURCES)

SRCH_DEFINES = -DXFILESEARCHPATHDEFAULT=\"$(XFILESEARCHPATHDEFAULT)\"
ERRORDB_DEFINES = -DERRORDB=\"$(ERRORDBDIR)/XtErrorDB\"

AM_CFLAGS = $(CWARNFLAGS) $(XT_CFLAGS) $(SRCH_DEFINES) $(ERRORDB_DEFINES) \
	$(XTMALLOC_ZERO_CFLAGS) -DLIBXT_COMPILATION

AM_CPPFLAGS = -I$(top_srcdir)/include/ -I$(top_srcdir)/include/X11 \
	      -I$(top_builddir)/include -I$(top_builddir)/include/X11

nodist_libXt_la_SOURCES = StringDefs.c

libXt_la_SOURCES = \
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
	sharedlib.c

DISTCLEANFILES = StringDefs.c

libXt_la_LIBADD = @XT_LIBS@

libXt_la_LDFLAGS = -version-number 6:0:0 -no-undefined

#
# The util directory contains a program that builds some of the sources.
# The input to this program is given in files. Unfortunately those input
# files refers to things like
#
# 	util/foo
#
# so the generating program must be run from the top directory.
#

BUILT_SOURCE = StringDefs.c
BUILT_HEADER = $(top_builddir)/include/X11/StringDefs.h $(top_builddir)/include/X11/Shell.h
BUILT_FILES = $(BUILT_SOURCE) $(BUILT_HEADER)

STRING_LIST = $(top_srcdir)/util/string.list

all-local: $(BUILT_FILES)

$(BUILT_HEADER): $(BUILT_SOURCE)

$(BUILT_SOURCE):
	$(AM_V_GEN)$(top_builddir)/util/makestrs -i $(top_srcdir) $(STRINGSABIOPTIONS) < $(STRING_LIST) > StringDefs.c
	$(AM_V_at)$(MKDIR_P) $(top_builddir)/src
	$(AM_V_at)$(MKDIR_P) $(top_builddir)/include/X11
	$(AM_V_at)cp StringDefs.h $(top_builddir)/include/X11
	$(AM_V_at)cp Shell.h $(top_builddir)/include/X11
	$(AM_V_at)rm StringDefs.h Shell.h
