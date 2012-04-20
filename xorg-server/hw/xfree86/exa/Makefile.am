SUBDIRS = man

module_LTLIBRARIES = libexa.la

libexa_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)

INCLUDES = \
	$(XORG_INCS) \
	-I$(srcdir)/../../../exa \
	-I$(srcdir)/../../../miext/cw

AM_CFLAGS = $(DIX_CFLAGS) $(XORG_CFLAGS)

libexa_la_SOURCES = \
	examodule.c

libexa_la_LIBADD = \
	../../../exa/libexa.la $(PIXMAN_LIBS)
