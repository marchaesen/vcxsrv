top_srcdir=../../../../..
top_builddir=../../../../..

include Makefile.sources

libdricommon_la_SOURCES = $(DRI_COMMON_FILES)

libmegadriver_stub_la_SOURCES = $(megadriver_stub_FILES)

DEFINES = SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC

PACKAGE_VERSION:=\"$(strip $(shell cat $(top_srcdir)/VERSION))\"
DEFINES += PACKAGE_VERSION=$(PACKAGE_VERSION)

mesa_dri_common_INCLUDES+=src src/gallium/auxiliary src/gallium/include

INCLUDES = $(MHMAKECONF)/include $(mesa_dri_common_INCLUDES:%=$(top_srcdir)/%) $(MHMAKECONF)/include

LIBRARY = libdricommon

CSRCS := $(notdir $(subst /,$/,$(libdricommon_la_SOURCES)))
CSRCS += $(notdir $(subst /,$/,$(megadriver_stub_FILES)))
CSRCS := $(CSRCS:%.h=)

load_makefile IS64=0 NORELDBG=1 $(top_srcdir)/src/util/xmlpool/makefile.srcs

