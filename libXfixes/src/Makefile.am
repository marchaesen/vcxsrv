lib_LTLIBRARIES = libXfixes.la

libXfixes_la_SOURCES = 	\
	Cursor.c	\
	Disconnect.c	\
	Region.c	\
	SaveSet.c	\
	Selection.c	\
	Xfixes.c	\
	Xfixesint.h

AM_CFLAGS = $(CWARNFLAGS) @FIXESEXT_CFLAGS@
AM_CPPFLAGS = -I$(top_srcdir)/include/X11/extensions

libXfixes_la_LIBADD = @FIXESEXT_LIBS@
libXfixes_la_LDFLAGS = -version-number 3:1:0 -no-undefined

libXfixesincludedir = $(includedir)/X11/extensions
libXfixesinclude_HEADERS = $(top_srcdir)/include/X11/extensions/Xfixes.h

