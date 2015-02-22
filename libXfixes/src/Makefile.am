lib_LTLIBRARIES = libXfixes.la

libXfixes_la_SOURCES = 	\
	Cursor.c	\
	Region.c	\
	SaveSet.c	\
	Selection.c	\
	Xfixes.c	\
	Xfixesint.h

libXfixes_la_LIBADD = @FIXESEXT_LIBS@
AM_CFLAGS = $(CWARNFLAGS) @FIXESEXT_CFLAGS@

INCLUDES = -I$(top_srcdir)/include/X11/extensions

libXfixes_la_LDFLAGS = -version-number 3:1:0 -no-undefined

libXfixesincludedir = $(includedir)/X11/extensions
libXfixesinclude_HEADERS = $(top_srcdir)/include/X11/extensions/Xfixes.h 

