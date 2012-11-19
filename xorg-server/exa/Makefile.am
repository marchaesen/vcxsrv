noinst_LTLIBRARIES = libexa.la

# Override these since EXA doesn't need them and the needed files aren't
# built (in hw/xfree86/os-support/solaris) until after EXA is built
SOLARIS_ASM_CFLAGS=""

if XORG
sdk_HEADERS = exa.h
endif

INCLUDES = \
	$(XORG_INCS) \
	-I$(srcdir)/../miext/cw

AM_CFLAGS = $(XORG_CFLAGS) $(DIX_CFLAGS)

libexa_la_SOURCES = \
	exa.c \
	exa.h \
	exa_classic.c \
	exa_migration_classic.c \
	exa_driver.c \
	exa_mixed.c \
	exa_migration_mixed.c \
	exa_accel.c \
	exa_glyphs.c \
	exa_offscreen.c \
	exa_render.c \
	exa_priv.h \
	exa_unaccel.c
