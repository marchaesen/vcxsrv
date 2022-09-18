top_srcdir=../../../..

INCLUDES += \
	$(MHMAKECONF)/include  \
	$(top_srcdir)/include \
	$(top_srcdir)/src \
	$(top_srcdir)/src/mapi \
	$(top_srcdir)/src/mesa/ \
	$(top_srcdir)/src/compiler/glsl/glcpp\
	$(top_srcdir)/src/compiler/spirv\
	$(top_srcdir)/src/compiler/nir \
	$(top_srcdir)/src/gallium/include \
	$(top_srcdir)/src/gallium/auxiliary \
	$(top_srcdir)/src/gtest/include \

glsl_libglcpp_la_SOURCES =				\
	glcpp-lex.c				\
	glcpp-parse.c			\
	glcpp-parse.h

DEFINES = WIN32 SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC __STDC_LIMIT_MACROS HAVE_PIPE_LOADER_DRI GALLIUM_SOFTPIPE GALLIUM_STATIC_TARGETS PIPE_SEARCH_DIR=\".\" HAVE_STRUCT_TIMESPEC __STDC_NO_THREADS__

LIBRARY = libcompilerglcpp

CSRCS = $(glsl_libglcpp_la_SOURCES)
CSRCS := $(CSRCS:%.h=)
CSRCS := $(CSRCS:%.hpp=)

