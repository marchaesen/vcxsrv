SUBDIRS = man

bin_PROGRAMS = Xvfb
noinst_LIBRARIES = libfbcmap.a

AM_CFLAGS = -DHAVE_DIX_CONFIG_H \
            $(XVFBMODULES_CFLAGS) \
	    $(DIX_CFLAGS)

SRCS =	InitInput.c \
	InitOutput.c \
	$(top_srcdir)/Xext/dpmsstubs.c \
	$(top_srcdir)/Xi/stubs.c \
	$(top_srcdir)/mi/miinitext.c

libfbcmap_a_CFLAGS = $(AM_CFLAGS)
libfbcmap_a_SOURCES = $(top_srcdir)/fb/fbcmap_mi.c

Xvfb_SOURCES = $(SRCS)

XVFB_LIBS = \
        @XVFB_LIBS@ \
	libfbcmap.a \
	$(MAIN_LIB) \
	$(XSERVER_LIBS)

Xvfb_LDADD = $(XVFB_LIBS) $(XVFB_SYS_LIBS) $(XSERVER_SYS_LIBS)
Xvfb_DEPENDENCIES = $(XVFB_LIBS)
Xvfb_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)

relink:
	$(AM_V_at)rm -f Xvfb$(EXEEXT) && $(MAKE) Xvfb$(EXEEXT)
