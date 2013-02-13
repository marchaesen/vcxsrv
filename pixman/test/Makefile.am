include $(top_srcdir)/test/Makefile.sources

AM_CFLAGS = $(OPENMP_CFLAGS)
AM_LDFLAGS = $(OPENMP_CFLAGS) $(TESTPROGS_EXTRA_LDFLAGS)
LDADD = libutils.la $(top_builddir)/pixman/libpixman-1.la -lm  $(PNG_LIBS)
AM_CPPFLAGS = -I$(top_srcdir)/pixman -I$(top_builddir)/pixman $(PNG_CFLAGS)

libutils_la_SOURCES = $(libutils_sources) $(libutils_headers)

noinst_LTLIBRARIES = libutils.la
noinst_PROGRAMS = $(TESTPROGRAMS) $(OTHERPROGRAMS)

TESTS = $(TESTPROGRAMS)
