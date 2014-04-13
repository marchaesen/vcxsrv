noinst_LTLIBRARIES = libXquartz.la
AM_CFLAGS = $(DIX_CFLAGS)
AM_OBJCFLAGS = $(DIX_CFLAGS)
AM_CPPFLAGS = \
	-DBUILD_DATE=\"$(BUILD_DATE)\" \
	-DXSERVER_VERSION=\"$(VERSION)\" \
	-DINXQUARTZ \
	-DUSE_NEW_CLUT \
	-DXFree86Server \
	-I$(top_srcdir)/miext/rootless \
	-I$(top_builddir)/pseudoramiX \
	-DX11LIBDIR=\"$(libdir)\"

if GLX
GL_DIR = GL
endif

SUBDIRS = bundle . $(GL_DIR) xpr pbproxy mach-startup man

DIST_SUBDIRS = bundle . GL xpr pbproxy mach-startup man

libXquartz_la_SOURCES = \
	$(top_srcdir)/mi/miinitext.c \
	X11Application.m \
	X11Controller.m \
	applewm.c \
	darwin.c \
	darwinEvents.c \
	darwinXinput.c \
	keysym2ucs.c \
	quartz.c \
	quartzCocoa.m \
	quartzKeyboard.c \
	quartzStartup.c \
	quartzRandR.c \
	console_redirect.c

libXquartz_la_LIBADD = $(top_builddir)/pseudoramiX/libPseudoramiX.la

EXTRA_DIST = \
	X11Application.h \
	X11Controller.h \
	applewmExt.h \
	darwin.h \
	darwinfb.h \
	darwinEvents.h \
	keysym2ucs.h \
	quartz.h \
	quartzCommon.h \
	quartzKeyboard.h \
	quartzRandR.h \
	sanitizedCarbon.h \
	sanitizedCocoa.h \
	console_redirect.h
