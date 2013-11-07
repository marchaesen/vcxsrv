noinst_LTLIBRARIES = libpresent.la
AM_CFLAGS = \
	-DHAVE_XORG_CONFIG_H \
	@DIX_CFLAGS@ @XORG_CFLAGS@

libpresent_la_SOURCES = \
	present.h \
	present.c \
	present_event.c \
	present_fake.c \
	present_fence.c \
	present_notify.c \
	present_priv.h \
	present_request.c \
	present_screen.c

sdk_HEADERS = present.h presentext.h
