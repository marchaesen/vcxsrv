ifeq ($(DEBUG),1)
DEFINES += FONTDEBUG XSERVER_DTRACE
endif

INCLUDES += ..\composite

LIBRARY=libdix

libdix_la_SOURCES = 	\
	atom.c		\
	colormap.c	\
	cursor.c	\
	devices.c	\
	dispatch.c	\
	dispatch.h	\
	dixfonts.c	\
	dixutils.c	\
	enterleave.c	\
	enterleave.h	\
	events.c	\
	eventconvert.c  \
	extension.c	\
	ffs.c		\
	gc.c		\
	getevents.c	\
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

CSRCS = $(filter %.c,$(libdix_la_SOURCES))
