include Makefile.sources

top_srcdir=../..
srcdir=.

INCLUDES += \
	$(MHMAKECONF)/include  \
	$(top_srcdir)/include \
	$(top_srcdir)/src \
	$(top_srcdir)/src/mapi \
	$(top_srcdir)/src/mesa/ \
	$(top_srcdir)/src/compiler\
	$(top_srcdir)/src/compiler/glsl\
	$(top_srcdir)/src/compiler/spirv\
	$(top_srcdir)/src/compiler/nir \
	$(top_srcdir)/src/gallium/include \
	$(top_srcdir)/src/gallium/auxiliary \
	$(top_srcdir)/src/gtest/include \

libcompiler_la_SOURCES = $(LIBCOMPILER_FILES)

glsl_libglcpp_la_SOURCES =				\
	$(LIBGLCPP_FILES) \
  $(SPIRV_FILES) \
	$(SPIRV_GENERATED_FILES) \
  $(NIR_FILES) \
	$(NIR_GENERATED_FILES)

glsl_libglsl_la_SOURCES =				\
	$(LIBGLSL_GENERATED_FILES)			\
	$(LIBGLSL_FILES)				\
	$(LIBGLSL_SHADER_CACHE_FILES)

DEFINES = WIN32 SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC __STDC_LIMIT_MACROS HAVE_PIPE_LOADER_DRI GALLIUM_SOFTPIPE GALLIUM_STATIC_TARGETS PIPE_SEARCH_DIR=\".\" HAVE_STRUCT_TIMESPEC __STDC_NO_THREADS__

LIBRARY = libcompiler

CSRCS := $(notdir $(subst /,$/,$(libcompiler_la_SOURCES)))
CSRCS += $(notdir $(subst /,$/,$(glsl_libglsl_la_SOURCES)))
CSRCS += $(notdir $(subst /,$/,$(glsl_libglcpp_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)
CSRCS := $(CSRCS:%.hpp=)

vpath %.c glsl:spirv:nir:glsl/glcpp
vpath %.cpp glsl

load_makefile IS64=0 NORELDBG=1 makefile.srcs

