ACLOCAL_AMFLAGS = -I m4

MAINTAINERCLEANFILES = ChangeLog INSTALL Makefile.in

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL

EXTRA_DIST = autogen.sh xcb-errors.pc.in src/errors.h src/extensions.py

lib_LTLIBRARIES = libxcb-errors.la

xcbinclude_HEADERS = src/xcb_errors.h

AM_CFLAGS = $(CWARNFLAGS)

libxcb_errors_la_SOURCES = src/xcb_errors.c
nodist_libxcb_errors_la_SOURCES = src/extensions.c
libxcb_errors_la_CPPFLAGS = $(XCB_CFLAGS) $(XPROTO_CFLAGS) -I$(srcdir)/src
libxcb_errors_la_LIBADD = $(XCB_LIBS) $(XPROTO_LIBS)
libxcb_errors_la_LDFLAGS = -version-info 0:0:0 -no-undefined -export-symbols-regex '^xcb_errors_'

pkgconfig_DATA = xcb-errors.pc

TESTS = tests/test
check_PROGRAMS = tests/test
tests_test_SOURCE = tests/test.c
tests_test_CPPFLAGS = -I$(srcdir)/src/ $(XCB_CFLAGS)
tests_test_LDADD = libxcb-errors.la $(XCB_LIBS)

CLEANFILES = src/extensions.c

src/extensions.c: src/extensions.py
	$(AM_V_GEN)$(PYTHON) $(srcdir)/src/extensions.py $@.tmp ${XCBPROTO_XCBINCLUDEDIR}/*.xml && \
	mv $@.tmp $@
