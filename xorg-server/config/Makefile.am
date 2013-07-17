AM_CFLAGS = $(DIX_CFLAGS)

noinst_LTLIBRARIES = libconfig.la
libconfig_la_SOURCES = config.c config-backends.h

if CONFIG_UDEV

AM_CFLAGS += $(UDEV_CFLAGS)
libconfig_la_SOURCES += udev.c
libconfig_la_LIBADD = $(UDEV_LIBS)

if XORG
xorgconfddir = $(datadir)/X11/$(XF86CONFIGDIR)
xorgconfd_DATA = 10-evdev.conf 10-quirks.conf
endif

else

if CONFIG_NEED_DBUS
AM_CFLAGS += $(DBUS_CFLAGS)
libconfig_la_SOURCES += dbus-core.c
libconfig_la_LIBADD = $(DBUS_LIBS)

if CONFIG_DBUS_API
dbusconfigdir = $(sysconfdir)/dbus-1/system.d
dbusconfig_DATA = xorg-server.conf

libconfig_la_SOURCES += dbus.c
endif

if CONFIG_HAL
AM_CFLAGS += $(HAL_CFLAGS)
libconfig_la_SOURCES += hal.c
libconfig_la_LIBADD += $(HAL_LIBS)
endif

else

if CONFIG_WSCONS
libconfig_la_SOURCES += wscons.c
endif # CONFIG_WSCONS

endif # CONFIG_NEED_DBUS

endif # !CONFIG_UDEV

EXTRA_DIST = xorg-server.conf x11-input.fdi 10-evdev.conf non-seat0.conf.multi-seat fdi2iclass.py 10-quirks.conf
