lib_LTLIBRARIES=libXext.la

AM_CPPFLAGS=\
          -I$(top_srcdir)/include \
          -I$(top_srcdir)/include/X11/extensions\
          $(XEXT_CFLAGS) $(MALLOC_ZERO_CFLAGS)

AM_CFLAGS=$(CWARNFLAGS)

libXext_la_LDFLAGS = -version-number $(XEXT_SOREV) -no-undefined

libXext_la_LIBADD = $(XEXT_LIBS)

libXext_la_SOURCES = \
	DPMS.c \
	MITMisc.c \
	XAppgroup.c \
	XEVI.c \
	XLbx.c \
	XMultibuf.c \
	XSecurity.c \
	XShape.c \
	XShm.c \
	XSync.c \
	XTestExt1.c \
	Xcup.c \
	Xdbe.c \
        Xge.c \
	extutil.c \
	globals.c

libXextincludedir = $(includedir)/X11/extensions
libXextinclude_HEADERS = $(top_srcdir)/include/X11/extensions/dpms.h \
			 $(top_srcdir)/include/X11/extensions/extutil.h \
			 $(top_srcdir)/include/X11/extensions/MITMisc.h \
			 $(top_srcdir)/include/X11/extensions/multibuf.h \
			 $(top_srcdir)/include/X11/extensions/security.h \
			 $(top_srcdir)/include/X11/extensions/shape.h \
			 $(top_srcdir)/include/X11/extensions/sync.h \
			 $(top_srcdir)/include/X11/extensions/Xag.h \
			 $(top_srcdir)/include/X11/extensions/Xcup.h \
			 $(top_srcdir)/include/X11/extensions/Xdbe.h \
			 $(top_srcdir)/include/X11/extensions/XEVI.h \
			 $(top_srcdir)/include/X11/extensions/Xext.h \
			 $(top_srcdir)/include/X11/extensions/Xge.h \
			 $(top_srcdir)/include/X11/extensions/XLbx.h \
			 $(top_srcdir)/include/X11/extensions/XShm.h \
			 $(top_srcdir)/include/X11/extensions/xtestext1.h

if LINT
ALL_LINT_FLAGS=$(LINT_FLAGS) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) \
                $(AM_CPPFLAGS) $(CPPFLAGS)

lint:
	$(LINT) $(ALL_LINT_FLAGS) $(libXext_la_SOURCES) $(XEXT_LIBS)
endif LINT

if MAKE_LINT_LIB
lintlibdir = $(libdir)

lintlib_DATA = $(LINTLIB)

$(LINTLIB): $(libXext_la_SOURCES)
	$(LINT) -y -oXext -x $(ALL_LINT_FLAGS) $(libXext_la_SOURCES)
endif MAKE_LINT_LIB
