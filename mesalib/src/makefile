SHAREDLIB = swrast_dri

top_srcdir:=..

#SUBDIRS = . gtest util mapi/glapi/gen mapi

$(OBJDIR)/git_sha1.h:
	@echo "updating $@"
	@python $(top_srcdir)/bin/git_sha1_gen.py --output $@

ifdef HAVE_OPENGL
gldir = $(includedir)/GL
gl_HEADERS = \
  $(top_srcdir)/include/GL/gl.h \
  $(top_srcdir)/include/GL/glext.h \
  $(top_srcdir)/include/GL/glcorearb.h \
  $(top_srcdir)/include/GL/gl_mangle.h
endif

ifdef HAVE_GLX
glxdir = $(includedir)/GL
glx_HEADERS = \
  $(top_srcdir)/include/GL/glx.h \
  $(top_srcdir)/include/GL/glxext.h \
  $(top_srcdir)/include/GL/glx_mangle.h
endif

ifdef HAVE_COMMON_OSMESA
osmesadir = $(includedir)/GL
osmesa_HEADERS = $(top_srcdir)/include/GL/osmesa.h
endif

# include only conditionally ?
INCLUDELIBFILES = compiler\$(OBJDIR)\libcompiler.lib
INCLUDELIBFILES += mesa\$(OBJDIR)\libmesa.lib
INCLUDELIBFILES += util\$(OBJDIR)\libutil.lib
INCLUDELIBFILES += mapi\$(OBJDIR)\libmapi.lib
INCLUDELIBFILES += mesa\drivers\dri\swrast\$(OBJDIR)\libswrast_dri.lib
INCLUDELIBFILES += mesa\drivers\dri\common\$(OBJDIR)\libdricommon.lib
INCLUDELIBFILES += $(MHMAKECONF)\expat\lib\$(OBJDIR)\libexpat.lib
INCLUDELIBFILES += $(MHMAKECONF)\libregex\$(OBJDIR)\libregex.lib

INCLUDESERVLIBFILES =  $(MHMAKECONF)\xorg-server\$(SERVOBJDIR)\vcxsrv.lib

#SUBDIRS += loader

ifdef HAVE_DRI_GLX
#SUBDIRS += glx
endif

INCLUDES += \
  $(MHMAKECONF)/include \
	$(top_srcdir)/include/ \
	$(top_srcdir)/src/mapi/ \
	$(top_srcdir)/src/mesa/ \
	$(top_srcdir)/src/gallium/include \
	$(top_srcdir)/src/gallium/auxiliary

libglsl_util_la_SOURCES = \
	mesa/main/extensions_table.c \
	mesa/main/imports.c \
	mesa/program/prog_parameter.c \
	mesa/program/symbol_table.c \

#DEFINES=\
#_LIB\
#_DLL\
#_GDI32_\
#BUILD_GL32\
#WIN32_THREADS\
#MESA_MINWARN\
#_CRT_SECURE_NO_DEPRECATE\
#_MBCS

DEFINES = SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC

CSRCS := $(notdir $(subst /,$/,$(libglsl_util_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

vpath %.c mesa\main:mesa\program

LIBDIRS=$(dir $(INCLUDELIBFILES))
SERVLIBDIRS=$(dir $(INCLUDESERVLIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)
load_makefile $(SERVLIBDIRS:%$(SERVOBJDIR)\=%makefile MAKESERVER=1 DEBUG=$(DEBUG);)

