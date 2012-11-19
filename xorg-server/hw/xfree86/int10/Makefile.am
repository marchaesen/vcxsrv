module_LTLIBRARIES = libint10.la

sdk_HEADERS = xf86int10.h

EXTRA_CFLAGS =

libint10_la_LDFLAGS = -avoid-version
libint10_la_LIBADD = $(PCIACCESS_LIBS)

COMMON_SOURCES = \
	helper_exec.c \
	helper_mem.c \
	xf86int10.c \
	xf86int10module.c

if I386_VIDEO
I386_VIDEO_CFLAGS = -D_PC
endif

if INT10_VM86
AM_CFLAGS = $(I386_VIDEO_CFLAGS) -D_VM86_LINUX $(DIX_CFLAGS) $(XORG_CFLAGS) $(EXTRA_CFLAGS)
INCLUDES = $(XORG_INCS)
libint10_la_SOURCES = \
	$(COMMON_SOURCES) \
	$(srcdir)/../os-support/linux/int10/vm86/linux_vm86.c \
	$(srcdir)/../os-support/linux/int10/linux.c
endif

if INT10_X86EMU
AM_CFLAGS = $(I386_VIDEO_CFLAGS) -D_X86EMU -DNO_SYS_HEADERS \
           $(XORG_CFLAGS) $(EXTRA_CFLAGS) $(DIX_CFLAGS)
INCLUDES = $(XORG_INCS) -I$(srcdir)/../x86emu
libint10_la_SOURCES = \
	$(COMMON_SOURCES) \
	xf86x86emu.c \
	generic.c \
	x86emu.c
endif

if INT10_STUB
AM_CFLAGS = $(I386_VIDEO_CFLAGS) -D_VM86_LINUX $(DIX_CFLAGS) $(XORG_CFLAGS) $(EXTRA_CFLAGS)
INCLUDES = $(XORG_INCS)
libint10_la_SOURCES = stub.c xf86int10module.c
endif

EXTRA_DIST = xf86x86emu.h INT10.HOWTO
