srcdir=.
top_srcdir=../..

INCLDUES = \
  $(top_srcdir)/include					\
	$(top_srcdir)/src					\
	$(top_builddir)/src/mapi				\
	$(top_srcdir)/src/mapi				\
	$(top_builddir)/src/mapi/glapi			\
	$(top_srcdir)/src/mapi/glapi

include Makefile.sources

PYTHON_GEN = python

glapi_gen_mapi_deps := \
	mapi_abi.py \
	$(wildcard $(top_srcdir)/src/mapi/glapi/gen/*.xml) \
	$(wildcard $(top_srcdir)/src/mapi/glapi/gen/*.py)

shared_glapi_libglapi_la_SOURCES = $(MAPI_GLAPI_FILES) shared-glapi/glapi_mapi_tmp.h

#DEFINES = MAPI_MODE_GLAPI MAPI_ABI_HEADER=\"shared-glapi/glapi_mapi_tmp.h\"

shared-glapi/glapi_mapi_tmp.h : glapi/gen/gl_and_es_API.xml $(glapi_gen_mapi_deps)
	$(PYTHON_GEN) $(srcdir)/mapi_abi.py --printer shared-glapi \
		$(srcdir)/glapi/gen/gl_and_es_API.xml > $@

INCLUDES = $(top_srcdir)/src/mesa

ifdef HAVE_APPLEDRI
glapi_libglapi_la_SOURCES += glapi/glapi_gentable.c
endif
ifdef HAVE_WINDOWSDRI
glapi_libglapi_la_SOURCES += glapi/glapi_gentable.c
endif

DEFINES = MAPI_MODE_UTIL
glapi_libglapi_la_SOURCES += \
	glapi/glapi_nop.c

ifdef HAVE_OPENGL_ES1
TESTS += es1api/ABI-check

BUILT_SOURCES += es1api/glapi_mapi_tmp.h

GLES_includedir = $(includedir)/GLES
GLES_include_HEADERS = \
	$(top_srcdir)/include/GLES/egl.h \
	$(top_srcdir)/include/GLES/gl.h \
	$(top_srcdir)/include/GLES/glext.h \
	$(top_srcdir)/include/GLES/glplatform.h

es1api_libGLESv1_CM_la_SOURCES = entry.c es1api/glapi_mapi_tmp.h
es1api_libGLESv1_CM_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(VISIBILITY_CFLAGS)
es1api_libGLESv1_CM_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DMAPI_MODE_BRIDGE \
	-DMAPI_ABI_HEADER=\"es1api/glapi_mapi_tmp.h\"
endif

es1api/glapi_mapi_tmp.h: glapi/gen/gl_and_es_API.xml $(glapi_gen_mapi_deps)
	$(MKDIR_GEN)
	$(PYTHON_GEN) $(srcdir)/mapi_abi.py --printer es1api \
		$(srcdir)/glapi/gen/gl_and_es_API.xml > $@

ifdef HAVE_OPENGL_ES2
BUILT_SOURCES += es2api/glapi_mapi_tmp.h

GLES2_includedir = $(includedir)/GLES2
GLES2_include_HEADERS = \
	$(top_srcdir)/include/GLES2/gl2.h \
	$(top_srcdir)/include/GLES2/gl2ext.h \
	$(top_srcdir)/include/GLES2/gl2platform.h
GLES3_includedir = $(includedir)/GLES3
GLES3_include_HEADERS = \
	$(top_srcdir)/include/GLES3/gl3.h \
	$(top_srcdir)/include/GLES3/gl31.h \
	$(top_srcdir)/include/GLES3/gl32.h \
	$(top_srcdir)/include/GLES3/gl3ext.h \
	$(top_srcdir)/include/GLES3/gl3platform.h

lib_LTLIBRARIES += es2api/libGLESv2.la

es2api_libGLESv2_la_SOURCES = entry.c es2api/glapi_mapi_tmp.h
es2api_libGLESv2_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(VISIBILITY_CFLAGS)
es2api_libGLESv2_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DMAPI_MODE_BRIDGE \
	-DMAPI_ABI_HEADER=\"es2api/glapi_mapi_tmp.h\"
es2api_libGLESv2_la_LIBADD = $(GLESv2_LIB_DEPS)
es2api_libGLESv2_la_LDFLAGS = \
	-no-undefined \
	-version-number 2 \
	$(GC_SECTIONS) \
	$(LD_NO_UNDEFINED)

es2api_libGLESv2_la_LIBADD += shared-glapi/libglapi.la
endif

es2api/glapi_mapi_tmp.h: glapi/gen/gl_and_es_API.xml $(glapi_gen_mapi_deps)
	$(MKDIR_GEN)
	$(PYTHON_GEN) $(srcdir)/mapi_abi.py --printer es2api \
		$(srcdir)/glapi/gen/gl_and_es_API.xml > $@

ifdef NEED_KHRPLATFORM
khrdir = $(includedir)/KHR
khr_HEADERS = $(top_srcdir)/include/KHR/khrplatform.h
endif

DEFINES += SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC

LIBRARY = libmapi

INCLUDES += . .. $(top_srcdir)/include

CSRCS := $(notdir $(subst /,$/,$(glapi_libglapi_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

vpath %.c glapi

