noinst_LTLIBRARIES = libdixmods.la libxorgxkb.la

if GLX
GLXMODS = libglx.la
endif

module_LTLIBRARIES = libfb.la \
                     libwfb.la \
                     libshadow.la

extsmoduledir = $(moduledir)/extensions
extsmodule_LTLIBRARIES = $(GLXMODS)

AM_CFLAGS = @XORG_CFLAGS@ @DIX_CFLAGS@
AM_CPPFLAGS = @XORG_INCS@ \
           -I$(top_srcdir)/hw/xfree86/loader \
           -I$(top_srcdir)/miext/shadow \
           -I$(top_srcdir)/glx

libfb_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
libfb_la_LIBADD = $(top_builddir)/fb/libfb.la
libfb_la_SOURCES = fbmodule.c
libfb_la_CFLAGS = $(AM_CFLAGS)

libwfb_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
libwfb_la_LIBADD = $(top_builddir)/fb/libwfb.la
libwfb_la_SOURCES = fbmodule.c
libwfb_la_CFLAGS = $(AM_CFLAGS) -DFB_ACCESS_WRAPPER

libglx_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
libglx_la_LIBADD = $(top_builddir)/glx/libglx.la $(GLX_SYS_LIBS)
if AIGLX_DRI_LOADER
libglx_la_LIBADD += $(top_builddir)/glx/libglxdri.la
if NO_UNDEFINED
libglx_la_LIBADD += $(LIBDRM_LIBS) $(PIXMAN_LIBS)
endif
endif
libglx_la_SOURCES = glxmodule.c

libshadow_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
libshadow_la_LIBADD = $(top_builddir)/miext/shadow/libshadow.la
if NO_UNDEFINED
libshadow_la_LIBADD += libfb.la
endif
libshadow_la_SOURCES = shmodule.c

libdixmods_la_SOURCES = $(top_srcdir)/mi/miinitext.c
libdixmods_la_CFLAGS = $(AM_CFLAGS)

libxorgxkb_la_SOURCES = xkbVT.c xkbPrivate.c xkbKillSrv.c
