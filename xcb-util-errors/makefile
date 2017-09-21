XCBPROTO_XCBINCLUDEDIR=../libxcb/xcb-proto/src

lib_LTLIBRARIES = libxcb-errors.la

xcbinclude_HEADERS = src/xcb_errors.h

libxcb_errors_la_SOURCES = src/xcb_errors.c $(OBJDIR)/extensions.c
pkgconfig_DATA = xcb-errors.pc

$(OBJDIR)/extensions.c: src/extensions.py
	python src/extensions.py $@ ${XCBPROTO_XCBINCLUDEDIR}/*.xml

LIBRARY = $(lib_LTLIBRARIES:.la=)

CSRCS := $(notdir $(subst /,$/,$(libxcb_errors_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

INCLUDES += src

vpath %.c src

