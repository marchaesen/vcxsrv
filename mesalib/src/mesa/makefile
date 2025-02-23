srcdir=.
builddir=.
top_srcdir=../..
top_builddir=../..

include Makefile.sources

libmesa_la_SOURCES = \
	$(MESA_FILES) \
	$(PROGRAM_FILES) \
	$(PROGRAM_NIR_FILES) \
	$(MESA_ASM_FILES_FOR_ARCH) \
	$(PROGRAM_NIR_FILES_REMOVED) \
        $(STATETRACKER_FILES)

DEFINES = WIN32 SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC __STDC_LIMIT_MACROS HAVE_PIPE_LOADER_DRI GALLIUM_SOFTPIPE GALLIUM_STATIC_TARGETS PIPE_SEARCH_DIR=\".\" HAVE_STRUCT_TIMESPEC __STDC_NO_THREADS__ HAVE_OPENGL=1 HAVE_OPENGL_ES_2=0 HAVE_OPENGL_ES_1=0

PACKAGE_VERSION:=\"$(strip $(shell cat $(top_srcdir)/VERSION))\"
DEFINES += PACKAGE_VERSION=$(PACKAGE_VERSION)

LIBRARY = libmesa

CSRCS := $(notdir $(subst /,$/,$(libmesa_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

INCLUDE_DIRS:=$(INCLUDE_DIRS:-I%=%)

INCLUDES=$(MHMAKECONF)/include $(INCLUDE_DIRS) ../mapi/glapi ../util ../$(OBJDIR) ../compiler

vpath %.c main:math:vbo:tnl:swrast:swrast_setup:drivers/common:program:state_tracker

vpath %.cpp main:program:state_tracker

load_makefile IS64=0 NORELDBG=1 MAKESERVER=0 makefile.srcs
load_makefile IS64=0 NORELDBG=1 ../mapi/glapi/gen/makefile.srcs

