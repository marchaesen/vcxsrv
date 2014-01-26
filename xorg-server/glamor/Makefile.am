lib_LTLIBRARIES = libglamor.la

if GLAMOR_GLES2
libglamor_la_LIBADD = $(GLESV2_LIBS)
else
libglamor_la_LIBADD = $(GL_LIBS)
endif

AM_CFLAGS = $(CWARNFLAGS) $(XORG_CFLAGS) $(LIBDRM_CFLAGS)

libglamor_la_LDFLAGS = -version-info 0:0:0

libglamor_la_SOURCES = \
	compat-api.h \
	compiler.h \
	glamor.c \
	glamor_copyarea.c \
	glamor_copywindow.c \
	glamor_core.c \
	glamor_debug.h \
	glamor_gl_dispatch.h \
	glamor_fill.c \
	glamor_fillspans.c \
	glamor_getspans.c \
	glamor_glext.h \
	glamor_glyphs.c \
	glamor_polyfillrect.c \
	glamor_polylines.c \
	glamor_putimage.c \
	glamor_setspans.c \
	glamor_render.c \
	glamor_gradient.c \
	glamor_trapezoid.c \
	glamor_tile.c \
	glamor_triangles.c\
	glamor_addtraps.c\
	glamor_getimage.c\
	glamor_copyplane.c\
	glamor_glyphblt.c\
	glamor_polyops.c\
	glamor_priv.h\
	glamor_pixmap.c\
	glamor_largepixmap.c\
	glamor_picture.c\
	glamor_window.c\
	glamor_gl_dispatch.c\
	glamor_fbo.c\
	glamor_compositerects.c\
	glamor_xv.c\
	glamor_utils.h\
	glamor.h\
	glapi.h

sdk_HEADERS = glamor.h

if EGL
LIBGLAMOREGL = libglamoregl.la
module_LTLIBRARIES = $(LIBGLAMOREGL)
libglamoregl_la_DEPENDENCIES = libglamor.la
libglamoregl_la_LDFLAGS = -avoid-version -module
libglamoregl_la_LIBADD = $(EGL_LIBS) $(GLX_SYS_LIBS) $(GBM_LIBS) libglamor.la
libglamoregl_la_SOURCES = glamor_eglmodule.c glamor_egl.c
libglamoregl_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(GLX_DEFINES) \
	$(LIBDRM_CFLAGS) \
	$(EGL_CFLAGS) \
	$(GBM_CFLAGS)
endif


