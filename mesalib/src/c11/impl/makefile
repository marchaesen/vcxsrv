srcdir=.
top_srcdir=../../..

INCLUDES = \
  $(MHMAKECONF)/include \
  $(top_srcdir)/include \
	$(top_srcdir)/src \
	$(top_srcdir)/src/mapi \
	$(top_srcdir)/src/mesa \
	$(top_srcdir)/src/gallium/include \
	$(top_srcdir)/src/gallium/auxiliary \
	$(top_srcdir)/src/util \

SOURCES = \
	threads_win32.c

BUILT_SOURCES = $(MESA_UTIL_GENERATED_FILES)

DEFINES = WIN32 SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC __STDC_LIMIT_MACROS HAVE_PIPE_LOADER_DRI GALLIUM_SOFTPIPE GALLIUM_STATIC_TARGETS PIPE_SEARCH_DIR=\".\" HAVE_STRUCT_TIMESPEC __STDC_NO_THREADS__

INCLUDES += $(MHMAKECONF)/include $(MHMAKECONF) $(MHMAKECONF)/expat/lib $(MHMAKECONF)/libregex/include

LIBRARY = libc11

CSRCS := $(notdir $(subst /,$/,$(SOURCES)))

CSRCS := $(CSRCS:%.h=)
