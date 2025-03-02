ifeq ($(DEBUG),1)
DEFINES += FONTDEBUG XSERVER_DTRACE
endif
DEFINES += PIXMAN_API=

INCLUDES += ..\composite ..\miext\sync ..

LIBRARY=libdix
libmain_la_SOURCES =    \
	stubmain.c

libdix_la_SOURCES = 	\
	display.c		\
	atom.c		\
	colormap.c	\
	color.c	\
	cursor.c	\
	devices.c	\
	dispatch.c	\
	dispatch.h	\
	dixfonts.c	\
	main.c		\
	dixutils.c	\
	enterleave.c	\
	enterleave.h	\
	events.c	\
	eventconvert.c  \
	extension.c	\
	gc.c		\
	getevents.c	\
	gestures.c	\
	globals.c	\
	glyphcurs.c	\
	grabs.c		\
	initatoms.c	\
	inpututils.c	\
	pixmap.c	\
	privates.c	\
	property.c	\
	ptrveloc.c	\
	region.c	\
	registry.c	\
	resource.c	\
	selection.c	\
	swaprep.c	\
	swapreq.c	\
	tables.c	\
	touch.c		\
	window.c

initatoms.c:
	wsl ./generate-atoms BuiltInAtoms $(notdir $@)

CSRCS = $(filter %.c,$(libdix_la_SOURCES)) $(filter %.c,$(libmain_la_SOURCES))
