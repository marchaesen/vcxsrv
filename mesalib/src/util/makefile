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

DEFINES = WIN32 SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC __STDC_LIMIT_MACROS HAVE_PIPE_LOADER_DRI GALLIUM_SOFTPIPE GALLIUM_STATIC_TARGETS PIPE_SEARCH_DIR=\".\" HAVE_STRUCT_TIMESPEC __STDC_NO_THREADS__
ifeq ($(IS64),0)
DEFINES +=BLAKE3_NO_SSE2 BLAKE3_NO_SSE41 BLAKE3_NO_AVX2 BLAKE3_NO_AVX512
endif

INCLUDES += $(MHMAKECONF)/include $(MHMAKECONF) $(MHMAKECONF)/expat/lib $(MHMAKECONF)/libregex/include

LIBRARY = libutil

CSRCS := $(notdir $(subst /,$/,$(libmesautil_la_SOURCES)))
CSRCS += $(notdir $(subst /,$/,$(libxmlconfig_la_SOURCES)))

CSRCS := $(CSRCS:%.h=)

vpath %.c sha1:format:blake3:../gallium/auxiliary/indices:.:../gallium/auxiliary/util

vpath %.masm blake3

load_makefile IS64=0 NORELDBG=1 makefile.srcs
