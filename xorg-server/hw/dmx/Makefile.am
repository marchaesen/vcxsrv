
SUBDIRS = input config examples doc doxygen man
bin_PROGRAMS = Xdmx

if XINERAMA
PANORAMIX_SRCS = $(top_srcdir)/Xext/panoramiX.c
endif

if GLX
SUBDIRS += glxProxy
GLX_LIBS = glxProxy/libglxproxy.a
GLX_SRCS = $(PANORAMIX_SRCS) dmx_glxvisuals.c dmx_glxvisuals.h
GLX_INCS = -I$(top_srcdir)/hw/xfree86/dixmods/extmod
GLX_DEFS = @GL_CFLAGS@
endif

AM_CFLAGS = \
      -DHAVE_DMX_CONFIG_H \
      $(DIX_CFLAGS) \
      $(GLX_INCS) \
      $(GLX_DEFS) \
      $(DMX_CFLAGS) \
      @DMXMODULES_CFLAGS@

Xdmx_SOURCES = dmx.c \
               dmxcb.c \
               dmxcb.h \
               dmxclient.h \
               dmxcmap.c \
               dmxcmap.h \
               dmx-config.h \
               dmxcursor.c \
               dmxcursor.h \
               dmxdpms.c \
               dmxdpms.h \
               dmxextension.c \
               dmxextension.h \
               dmxfont.c \
               dmxfont.h \
               dmxgc.c \
               dmxgc.h \
               dmxgcops.c \
               dmxgcops.h \
               dmx.h \
               dmxinit.c \
               dmxinit.h \
               dmxinput.c \
               dmxinput.h \
               dmxlog.c \
               dmxlog.h \
               dmxpict.c \
               dmxpict.h \
               dmxpixmap.c \
               dmxpixmap.h \
               dmxprop.c \
               dmxprop.h \
               dmxscrinit.c \
               dmxscrinit.h \
               dmxstat.c \
               dmxstat.h \
               dmxsync.c \
               dmxsync.h \
               dmxvisual.c \
               dmxvisual.h \
               dmxwindow.c \
               dmxwindow.h \
               $(top_srcdir)/mi/miinitext.c \
               $(GLX_SRCS) 


#if COMPOSITE
#Xdmx_SOURCES += fakecw.c
#endif

XDMX_LIBS = \
	$(GLX_LIBS) \
	@XDMX_LIBS@ \
        input/libdmxinput.a \
        config/libdmxconfig.a

Xdmx_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)
Xdmx_DEPENDENCIES= $(XDMX_LIBS)
Xdmx_LDADD = $(XDMX_LIBS) $(XDMX_SYS_LIBS) $(XSERVER_SYS_LIBS)

relink:
	$(AM_V_at)rm -f Xdmx$(EXEEXT) && $(MAKE) Xdmx$(EXEEXT)
