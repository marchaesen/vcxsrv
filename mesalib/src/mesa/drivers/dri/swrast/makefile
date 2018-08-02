top_srcdir=../../../../..
top_builddir=../../../../..

include Makefile.sources

INCLUDES = \
  $(MHMAKECONF)/include \
	$(top_srcdir)/include \
	$(top_srcdir)/src/ \
	$(top_srcdir)/src/mapi \
	$(top_srcdir)/src/mesa/ \
	$(top_srcdir)/src/gallium/include \
	$(top_srcdir)/src/gallium/auxiliary \
	$(top_builddir)/src/util \
	$(top_srcdir)/src/mesa/drivers/dri/common \

libswrast_dri_la_SOURCES = $(SWRAST_C_FILES)

DEFINES = SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC

LIBRARY = libswrast_dri

CSRCS := $(notdir $(subst /,$/,$(libswrast_dri_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

#vpath %.c glapi
