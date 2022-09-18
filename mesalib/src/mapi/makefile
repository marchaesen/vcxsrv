srcdir=.
top_srcdir=../..

INCLDUES = \
  $(MHMAKECONF)/include \
  $(top_srcdir)/include					\
	$(top_srcdir)/src					\
	$(top_builddir)/src/mapi				\
	$(top_srcdir)/src/mapi				\
	$(top_builddir)/src/mapi/glapi			\
	$(top_srcdir)/src/mapi/glapi

include Makefile.sources

DEFINES = MAPI_MODE_UTIL

glapi_libglapi_la_SOURCES += \
	glapi/glapi_nop.c

DEFINES = WIN32 SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC __STDC_LIMIT_MACROS HAVE_PIPE_LOADER_DRI GALLIUM_SOFTPIPE GALLIUM_STATIC_TARGETS PIPE_SEARCH_DIR=\".\" HAVE_STRUCT_TIMESPEC __STDC_NO_THREADS__

LIBRARY = libmapi

INCLUDES += . .. glapi/gen $(top_srcdir)/include

CSRCS := $(notdir $(subst /,$/,$(glapi_libglapi_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

vpath %.c glapi

load_makefile IS64=0 NORELDBG=1 $(srcdir)/glapi/gen/makefile.srcs

