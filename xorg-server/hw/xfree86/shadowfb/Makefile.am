module_LTLIBRARIES = libshadowfb.la
libshadowfb_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
libshadowfb_la_SOURCES = sfbmodule.c shadow.c
libshadowfb_la_LIBADD = $(PIXMAN_LIBS)

sdk_HEADERS = shadowfb.h

AM_CPPFLAGS = $(XORG_INCS)

AM_CFLAGS = $(DIX_CFLAGS) $(XORG_CFLAGS)
