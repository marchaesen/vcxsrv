AM_CPPFLAGS=-DBUNDLE_ID_PREFIX=\"$(BUNDLE_ID_PREFIX)\"

AM_CFLAGS=$(XPBPROXY_CFLAGS)
AM_OBJCFLAGS=$(XPBPROXY_CFLAGS)

noinst_LTLIBRARIES = libxpbproxy.la
libxpbproxy_la_SOURCES = \
	trick_autotools.c \
	main.m \
	x-input.m \
	x-selection.m

libxpbproxy_la_LDFLAGS=$(XPBPROXY_LIBS)

if STANDALONE_XPBPROXY

bin_PROGRAMS = xpbproxy
xpbproxy_SOURCES = app-main.m
xpbproxy_LDADD = libxpbproxy.la
xpbproxy_LDFLAGS = -Wl,-framework,Cocoa

AM_CPPFLAGS += -DSTANDALONE_XPBPROXY

endif

EXTRA_DIST = \
	pbproxy.h \
	x-selection.h
