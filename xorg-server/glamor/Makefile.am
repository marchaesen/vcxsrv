noinst_LTLIBRARIES = libglamor.la libglamor_egl_stubs.la

libglamor_la_LIBADD = $(GLAMOR_LIBS)

AM_CFLAGS = $(CWARNFLAGS) $(DIX_CFLAGS) $(GLAMOR_CFLAGS)

libglamor_la_SOURCES = \
	glamor.c \
	glamor_context.h \
	glamor_copyarea.c \
	glamor_copywindow.c \
	glamor_core.c \
	glamor_debug.h \
	glamor_fill.c \
	glamor_fillspans.c \
	glamor_getspans.c \
	glamor_glx.c \
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
	glamor_fbo.c\
	glamor_compositerects.c\
	glamor_xv.c\
	glamor_utils.h\
	glamor.h

libglamor_egl_stubs_la_SOURCES = glamor_egl_stubs.c

sdk_HEADERS = glamor.h
