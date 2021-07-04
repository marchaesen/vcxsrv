SUBDIRS = man

noinst_LIBRARIES = libdmxconfig.a

LIBSRCS = parser.y \
          scanner.l \
          dmxparse.c \
          dmxparse.h \
          dmxprint.c \
          dmxprint.h \
          dmxcompat.c \
          dmxcompat.h \
          dmxconfig.c \
          dmxconfig.h

parser.h: parser.c
scanner.c: scanner.l parser.h

BUILT_SOURCES = parser.c parser.h scanner.c

MAINTAINERCLEANFILES = $(BUILT_SOURCES)

libdmxconfig_a_SOURCES = $(LIBSRCS)
libdmxconfig_a_SOURCES += $(top_srcdir)/os/strlcpy.c

if GLX
GLX_DEFS = @GL_CFLAGS@
endif	    

AM_YFLAGS = -d
AM_CFLAGS = \
	$(DIX_CFLAGS) \
	-I$(top_srcdir)/hw/dmx \
	-DHAVE_DMX_CONFIG_H \
	-DDMX_LOG_STANDALONE \
	$(GLX_DEFS) \
	@DMXMODULES_CFLAGS@

bin_PROGRAMS = xdmxconfig vdltodmx dmxtodmx

xdmxconfig_SOURCES = \
	xdmxconfig.c \
	$(top_srcdir)/hw/dmx/dmxlog.c \
	Canvas.c \
	Canvas.h \
	CanvasP.h
xdmxconfig_LDADD = libdmxconfig.a @XDMXCONFIG_DEP_LIBS@
xdmxconfig_CFLAGS = $(AM_CFLAGS) @XDMXCONFIG_DEP_CFLAGS@

vdltodmx_SOURCES = vdltodmx.c
vdltodmx_LDADD = libdmxconfig.a

dmxtodmx_SOURCES = dmxtodmx.c
dmxtodmx_LDADD = libdmxconfig.a

EXTRA_DIST = \
	test-a.in test-a.out \
	test-b.in test-b.out \
	test-c.in test-c.out \
	test-d.in test-d.out \
	test-e.in test-e.out \
	test-f.in test-f.out \
	test-g.in test-g.out \
	test-h.in test-h.out \
	test-i.in test-i.out \
	test-j.in test-j.out \
	test-k.in test-k.out \
	test-l.in test-l.out
