noinst_LTLIBRARIES = libdixmods.la libxorgxkb.la

SUBDIRS = extmod

if GLX
GLXMODS = libglx.la
endif

if DBE
DBEMOD = libdbe.la
endif

if RECORD
RECORDMOD = librecord.la
endif

module_LTLIBRARIES = libfb.la \
                     libwfb.la \
                     libshadow.la

extsmoduledir = $(moduledir)/extensions
extsmodule_LTLIBRARIES = $(RECORDMOD) \
			 $(DBEMOD) \
                         $(GLXMODS)

AM_CFLAGS = @XORG_CFLAGS@ @DIX_CFLAGS@
INCLUDES = @XORG_INCS@ \
           -I$(top_srcdir)/dbe \
           -I$(top_srcdir)/hw/xfree86/loader \
           -I$(top_srcdir)/miext/shadow \
           -I$(top_srcdir)/glx

libdbe_la_LDFLAGS = -module -avoid-version
libdbe_la_LIBADD = $(top_builddir)/dbe/libdbe.la
libdbe_la_SOURCES = dbemodule.c

libfb_la_LDFLAGS = -module -avoid-version
libfb_la_LIBADD = $(top_builddir)/fb/libfb.la
libfb_la_SOURCES = $(top_builddir)/fb/fbcmap_mi.c fbmodule.c
libfb_la_CFLAGS = $(AM_CFLAGS)

libwfb_la_LDFLAGS = -module -avoid-version
libwfb_la_LIBADD = $(top_builddir)/fb/libwfb.la
libwfb_la_SOURCES = $(top_builddir)/fb/fbcmap_mi.c fbmodule.c
libwfb_la_CFLAGS = $(AM_CFLAGS) -DFB_ACCESS_WRAPPER

libglx_la_LDFLAGS = -module -avoid-version
if AIGLX_DRI_LOADER
GLXDRI_LIBRARY = $(top_builddir)/glx/libglxdri.la
endif
libglx_la_LIBADD = \
	$(top_builddir)/glx/libglx.la \
	$(GLXDRI_LIBRARY)
libglx_la_SOURCES = glxmodule.c

librecord_la_LDFLAGS = -module -avoid-version
librecord_la_LIBADD = $(top_builddir)/record/librecord.la
librecord_la_SOURCES = recordmod.c

libshadow_la_LDFLAGS = -module -avoid-version
libshadow_la_LIBADD = $(top_builddir)/miext/shadow/libshadow.la
libshadow_la_SOURCES = shmodule.c

libdixmods_la_SOURCES = $(top_srcdir)/mi/miinitext.c
libdixmods_la_CFLAGS = -DXFree86LOADER $(AM_CFLAGS)

libxorgxkb_la_SOURCES = xkbVT.c xkbPrivate.c xkbKillSrv.c
