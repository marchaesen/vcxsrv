AM_CFLAGS = $(CWARNFLAGS) $(RENDER_CFLAGS) $(MALLOC_ZERO_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/include/X11/extensions

lib_LTLIBRARIES = libXrender.la

libXrender_la_SOURCES = AddTrap.c \
                        Color.c \
                        Composite.c \
                        Cursor.c \
                        FillRect.c \
                        FillRects.c \
                        Filter.c \
                        Glyph.c \
                        Picture.c \
                        Poly.c \
                        Trap.c \
                        Tri.c \
                        Xrender.c \
                        Xrenderint.h

libXrender_la_LIBADD = $(RENDER_LIBS)

# -version-number requires libtool >= 1.5
libXrender_la_LDFLAGS = -version-number 1:3:0 -no-undefined

libXrenderincludedir = $(includedir)/X11/extensions
libXrenderinclude_HEADERS = $(top_srcdir)/include/X11/extensions/Xrender.h
