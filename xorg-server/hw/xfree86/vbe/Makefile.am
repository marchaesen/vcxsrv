module_LTLIBRARIES = libvbe.la
libvbe_la_LDFLAGS = -module -avoid-version $(LD_NO_UNDEFINED_FLAG)
libvbe_la_SOURCES = vbe.c vbeModes.c vbe_module.c
if NO_UNDEFINED
libvbe_la_LIBADD = ../int10/libint10.la
endif

sdk_HEADERS = vbe.h vbeModes.h

AM_CFLAGS = $(DIX_CFLAGS) $(XORG_CFLAGS)

AM_CPPFLAGS = $(XORG_INCS) -I$(srcdir)/../ddc -I$(srcdir)/../i2c \
           -I$(srcdir)/../modes -I$(srcdir)/../parser \
           -I$(srcdir)/../int10
