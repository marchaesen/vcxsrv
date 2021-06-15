LIBRARY = librbug

top_srcdir=../../../..

DEFINES = WIN32 SWRAST_DRI_EXPORT INSERVER _USE_MATH_DEFINES __STDC_CONSTANT_MACROS __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS XML_STATIC __STDC_LIMIT_MACROS HAVE_PIPE_LOADER_DRI GALLIUM_SOFTPIPE GALLIUM_STATIC_TARGETS PIPE_SEARCH_DIR=\".\"

INCLUDES += $(MHMAKECONF)/include ../../auxiliary ../../auxiliary/driver_rbug ../../include ../../.. $(top_srcdir)/include ../../frontends/dri \
  ../../../mesa/drivers/dri/common ../../../mesa ../../drivers ../../../mapi ../../../compiler/nir ../../winsys

CSRCS := rbug_context.c rbug_shader.c rbug_texture.c rbug_connection.c rbug_core.c rbug_demarshal.c

