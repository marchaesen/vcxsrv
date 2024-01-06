if ENABLE_UNIT_TESTS
if HAVE_GLIB
check_PROGRAMS = Alloc Converters Event

TESTS=$(check_PROGRAMS)

AM_CFLAGS = $(CWARNFLAGS) $(XT_CFLAGS) $(GLIB_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include \
    -I$(top_builddir)/include/X11
LDADD= $(top_builddir)/src/libXt.la $(GLIB_LIBS)

TESTS_ENVIRONMENT = $(MALLOC_DEBUG_ENV)

endif HAVE_GLIB
endif ENABLE_UNIT_TESTS
