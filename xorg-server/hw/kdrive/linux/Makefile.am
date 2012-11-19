INCLUDES = 					\
	@KDRIVE_INCS@				\
	@KDRIVE_CFLAGS@

AM_CFLAGS = -DHAVE_DIX_CONFIG_H

noinst_LTLIBRARIES = liblinux.la

liblinux_la_SOURCES =

liblinux_la_SOURCES += linux.c

if KDRIVE_KBD
liblinux_la_SOURCES += keyboard.c
endif

if KDRIVE_EVDEV
liblinux_la_SOURCES += evdev.c
endif

if KDRIVE_MOUSE
liblinux_la_SOURCES += mouse.c ms.c ps2.c
endif

if TSLIB
liblinux_la_SOURCES += tslib.c
endif
