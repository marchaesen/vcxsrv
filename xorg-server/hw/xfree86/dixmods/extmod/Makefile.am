sdk_HEADERS = dgaproc.h

extsmoduledir = $(moduledir)/extensions
extsmodule_LTLIBRARIES = libextmod.la

if DGA
DGA_SRCS = xf86dga2.c dgaproc.h
endif

if XV
XV_SRCS = xvmod.c xvmodproc.h
endif

if XF86VIDMODE
XF86VMODE_SRCS = xf86vmode.c
endif

AM_CFLAGS = @DIX_CFLAGS@ @XORG_CFLAGS@
INCLUDES = @XORG_INCS@ \
           -I$(top_srcdir)/dbe \
           -I$(top_srcdir)/hw/xfree86/loader \
           -I$(top_srcdir)/miext/shadow

libextmod_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
libextmod_la_SOURCES = modinit.c \
                       modinit.h \
                       $(DGA_SRCS) \
                       $(XF86VMODE_SRCS) \
                       $(XV_SRCS)
libextmod_la_LIBADD = $(top_builddir)/Xext/libXextmodule.la
