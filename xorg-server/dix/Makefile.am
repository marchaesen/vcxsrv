noinst_LTLIBRARIES = libdix.la libmain.la

AM_CPPFLAGS = -I$(top_srcdir)/include
AM_CFLAGS = $(DIX_CFLAGS)

libmain_la_SOURCES =    \
	stubmain.c

libdix_la_SOURCES = 	\
	atom.c		\
	colormap.c	\
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

EXTRA_DIST = buildatoms BuiltInAtoms Xserver.d Xserver-dtrace.h.in

# Install list of protocol names
miscconfigdir = $(SERVER_MISC_CONFIG_PATH)
dist_miscconfig_DATA = protocol.txt

if XSERVER_DTRACE
# Generate dtrace header file for C sources to include
BUILT_SOURCES = Xserver-dtrace.h

Xserver-dtrace.h: $(srcdir)/Xserver.d
	$(AM_V_GEN)$(DTRACE) -C -h -o $@ -s $(srcdir)/Xserver.d \
		|| cp Xserver-dtrace.h.in $@

endif

if SPECIAL_DTRACE_OBJECTS
# Generate dtrace object code for probes in libdix
dtrace-dix.o: $(top_srcdir)/dix/Xserver.d $(am_libdix_la_OBJECTS)
	$(AM_V_GEN)$(DTRACE) -G -C -o $@ -s $(top_srcdir)/dix/Xserver.d $(am_libdix_la_OBJECTS:%.lo=.libs/%.o)

noinst_PROGRAMS = dix.O

dix_O_SOURCES =
dix.O: dtrace-dix.o $(am_libdix_la_OBJECTS)
	$(AM_V_GEN)ld -r -o $@ $(am_libdix_la_OBJECTS:%.lo=.libs/%.o)
endif

CLEANFILES = Xserver-dtrace.h
