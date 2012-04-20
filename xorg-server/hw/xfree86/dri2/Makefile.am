libdri2_la_LTLIBRARIES = libdri2.la
libdri2_la_CFLAGS = \
	-DHAVE_XORG_CONFIG_H \
	@DRI_CFLAGS@ \
	@DIX_CFLAGS@ @XORG_CFLAGS@ @DRI2PROTO_CFLAGS@ @LIBDRM_CFLAGS@ \
	-I$(top_srcdir)/hw/xfree86/common \
	-I$(top_srcdir)/hw/xfree86/os-support/bus

libdri2_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
libdri2_la_LIBADD = @LIBDRM_LIBS@
libdri2_ladir = $(moduledir)/extensions
libdri2_la_SOURCES = \
	dri2.c \
	dri2.h \
	dri2ext.c

sdk_HEADERS = dri2.h
