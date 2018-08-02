srcdir=.
builddir=.
top_srcdir=../..
top_builddir=../..

include Makefile.sources

libmesa_la_SOURCES = \
	$(MESA_FILES) \
	$(PROGRAM_FILES) \
	$(PROGRAM_NIR_FILES) \
	$(MESA_ASM_FILES_FOR_ARCH)

DEFINES = SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC

PACKAGE_VERSION=\"$(strip $(shell cat $(top_srcdir)/VERSION))\"
DEFINES += PACKAGE_VERSION=$(PACKAGE_VERSION)

LIBRARY = libmesa

CSRCS := $(notdir $(subst /,$/,$(libmesa_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

INCLUDE_DIRS:=$(INCLUDE_DIRS:-I%=%)

INCLUDES=$(MHMAKECONF)/include $(INCLUDE_DIRS) ../mapi/glapi

vpath %.c main:math:vbo:tnl:swrast:swrast_setup:drivers/common:program

vpath %.cpp main:program

load_makefile -f makefile.srcs NORELDBG=1
load_makefile -f ../mapi/glapi/gen/makefile.srcs NORELDBG=1

