noinst_LTLIBRARIES = liblinux.la

if LINUX_ALPHA
noinst_LTLIBRARIES += liblinuxev56.la

liblinuxev56_la_CFLAGS = $(AM_CFLAGS) -mcpu=ev56

liblinuxev56_la_SOURCES = lnx_ev56.c
endif

if LNXACPI
ACPI_SRCS = lnx_acpi.c lnx_apm.c
XORG_CFLAGS += -DHAVE_ACPI
endif

if LNXAPM
APM_SRCS = lnx_apm.c
XORG_CFLAGS += -DHAVE_APM
endif

if SYSTEMD_LOGIND
LOGIND_SRCS = systemd-logind.c
XORG_CFLAGS += $(DBUS_CFLAGS)
endif

liblinux_la_SOURCES = linux.h lnx_init.c lnx_video.c \
                     lnx_agp.c lnx_kmod.c lnx_bell.c lnx_platform.c \
		     $(srcdir)/../shared/VTsw_usl.c \
		     $(srcdir)/../shared/posix_tty.c \
                     $(srcdir)/../shared/vidmem.c \
		     $(srcdir)/../shared/sigio.c \
                     $(ACPI_SRCS) \
                     $(APM_SRCS) \
                     $(LOGIND_SRCS)

AM_CFLAGS = -DUSESTDRES -DHAVE_SYSV_IPC $(DIX_CFLAGS) $(XORG_CFLAGS) $(PLATFORM_DEFINES)

AM_CPPFLAGS = $(XORG_INCS) $(PLATFORM_INCLUDES) $(LIBDRM_CFLAGS)

if LINUX_ALPHA
liblinux_la_LIBADD = liblinuxev56.la
endif
