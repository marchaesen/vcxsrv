srcdir=.
top_srcdir=../..

include Makefile.sources

INCLUDES = \
  $(MHMAKECONF)/include \
  $(top_srcdir)/include \
	$(top_srcdir)/src \
	$(top_srcdir)/src/mapi \
	$(top_srcdir)/src/mesa \
	$(top_srcdir)/src/gallium/include \
	$(top_srcdir)/src/gallium/auxiliary \
	$(top_srcdir)/src/util \

libmesautil_la_SOURCES = \
	$(MESA_UTIL_FILES) \
	$(MESA_UTIL_GENERATED_FILES)

libxmlconfig_la_SOURCES = $(XMLCONFIG_FILES)

libxmlconfig_la_CFLAGS = \
	$(DEFINES) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src \
	-DSYSCONFDIR=\"$(sysconfdir)\" \
	$(VISIBILITY_CFLAGS) \
	$(EXPAT_CFLAGS)

BUILT_SOURCES = $(MESA_UTIL_GENERATED_FILES)

PYTHON_GEN = python

DEFINES = WIN32 SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC

INCLUDES += $(MHMAKECONF)/include $(MHMAKECONF) $(MHMAKECONF)/expat/lib $(MHMAKECONF)/libregex/include

LIBRARY = libutil

CSRCS := $(notdir $(subst /,$/,$(libmesautil_la_SOURCES)))
CSRCS += $(notdir $(subst /,$/,$(libxmlconfig_la_SOURCES)))

CSRCS := $(CSRCS:%.h=)

vpath %.c sha1:format

load_makefile IS64=0 NORELDBG=1 makefile.srcs
