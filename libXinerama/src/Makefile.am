AM_CFLAGS = -I$(top_srcdir)/include \
            -I$(top_srcdir)/include/X11 \
            -I$(top_srcdir)/include/X11/extensions \
            $(XINERAMA_CFLAGS) \
            $(MALLOC_ZERO_CFLAGS) \
            $(CWARNFLAGS)

lib_LTLIBRARIES = libXinerama.la

libXinerama_la_SOURCES = \
         Xinerama.c

libXinerama_la_LIBADD = $(XINERAMA_LIBS)

libXinerama_la_LDFLAGS = -version-number 1:0:0 -no-undefined

libXineramaincludedir = $(includedir)/X11/extensions
libXineramainclude_HEADERS = $(top_srcdir)/include/X11/extensions/Xinerama.h \
                             $(top_srcdir)/include/X11/extensions/panoramiXext.h

if LINT
ALL_LINT_FLAGS=$(LINT_FLAGS) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) \
		$(AM_CPPFLAGS) $(CPPFLAGS)

lint:
	$(LINT) $(ALL_LINT_FLAGS) $(libXinerama_la_SOURCES) $(XINERAMA_LIBS)
endif LINT
