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

PACKAGE_VERSION:=\"$(strip $(shell cat $(top_srcdir)/VERSION))\"
DEFINES += PACKAGE_VERSION=$(PACKAGE_VERSION)

LIBRARY = libmesa

CSRCS := $(notdir $(subst /,$/,$(libmesa_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

INCLUDE_DIRS:=$(INCLUDE_DIRS:-I%=%)

INCLUDES=$(MHMAKECONF)/include $(INCLUDE_DIRS) ../mapi/glapi ../$(OBJDIR)

vpath %.c main:math:vbo:tnl:swrast:swrast_setup:drivers/common:program

vpath %.cpp main:program

load_makefile IS64=0 NORELDBG=1 MAKESERVER=0 makefile.srcs
load_makefile IS64=0 NORELDBG=1 ../mapi/glapi/gen/makefile.srcs

