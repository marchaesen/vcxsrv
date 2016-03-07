if ENABLE_UNIT_TESTS
SUBDIRS= .
noinst_PROGRAMS = list string
if XORG
# Tests that require at least some DDX functions in order to fully link
# For now, requires xf86 ddx, could be adjusted to use another
SUBDIRS += xi1 xi2
noinst_PROGRAMS += xkb input xtest misc fixes xfree86 os signal-logging touch
if RES
noinst_PROGRAMS += hashtabletest
endif
endif
check_LTLIBRARIES = libxservertest.la

TESTS=$(noinst_PROGRAMS)
TESTS_ENVIRONMENT = $(XORG_MALLOC_DEBUG_ENV)

AM_CFLAGS = $(DIX_CFLAGS) @XORG_CFLAGS@
AM_CPPFLAGS = $(XORG_INCS)
if XORG
AM_CPPFLAGS += -I$(top_srcdir)/hw/xfree86/parser \
	-I$(top_srcdir)/hw/xfree86/ddc \
	-I$(top_srcdir)/hw/xfree86/i2c -I$(top_srcdir)/hw/xfree86/modes \
	-I$(top_srcdir)/hw/xfree86/ramdac -I$(top_srcdir)/hw/xfree86/dri \
	-I$(top_srcdir)/hw/xfree86/dri2 -I$(top_srcdir)/dri3
endif
TEST_LDADD=libxservertest.la $(XORG_SYS_LIBS) $(XSERVER_SYS_LIBS) $(GLX_SYS_LIBS)

if SPECIAL_DTRACE_OBJECTS
TEST_LDADD += $(OS_LIB) $(DIX_LIB)
endif

xkb_LDADD=$(TEST_LDADD)
input_LDADD=$(TEST_LDADD)
xtest_LDADD=$(TEST_LDADD)
misc_LDADD=$(TEST_LDADD)
fixes_LDADD=$(TEST_LDADD)
xfree86_LDADD=$(TEST_LDADD)
touch_LDADD=$(TEST_LDADD)
signal_logging_LDADD=$(TEST_LDADD)
hashtabletest_LDADD=$(TEST_LDADD)
os_LDADD=$(TEST_LDADD)

libxservertest_la_LIBADD = $(XSERVER_LIBS)
if XORG

nodist_libxservertest_la_SOURCES = sdksyms.c
libxservertest_la_LIBADD += \
            $(top_builddir)/hw/xfree86/loader/libloader.la \
            $(top_builddir)/hw/xfree86/os-support/libxorgos.la \
            $(top_builddir)/hw/xfree86/common/libcommon.la \
            $(top_builddir)/hw/xfree86/parser/libxf86config.la \
            $(top_builddir)/hw/xfree86/dixmods/libdixmods.la \
            $(top_builddir)/hw/xfree86/modes/libxf86modes.la \
            $(top_builddir)/hw/xfree86/ramdac/libramdac.la \
            $(top_builddir)/hw/xfree86/ddc/libddc.la \
            $(top_builddir)/hw/xfree86/i2c/libi2c.la \
            $(top_builddir)/hw/xfree86/dixmods/libxorgxkb.la \
            $(top_builddir)/Xext/libXvidmode.la \
            @XORG_LIBS@

BUILT_SOURCES = sdksyms.c
CLEANFILES = sdksyms.c

sdksyms.c: $(top_builddir)/hw/xfree86/sdksyms.c
	$(AM_V_GEN)$(LN_S) $(top_builddir)/hw/xfree86/sdksyms.c

if DRI
libxservertest_la_LIBADD += $(top_builddir)/hw/xfree86/dri/libdri.la
endif

if DRI2
libxservertest_la_LIBADD += $(top_builddir)/hw/xfree86/dri2/libdri2.la
endif

if DRI3
libxservertest_la_LIBADD += $(top_builddir)/dri3/libdri3.la
endif

else
nodist_libxservertest_la_SOURCES = \
            ddxstubs.c \
            $(top_srcdir)/mi/miinitext.c

libxservertest_la_LIBADD += \
            $(top_builddir)/damageext/libdamageext.la \
            $(top_builddir)/fb/libfb.la \
            $(top_builddir)/fb/libwfb.la \
            $(top_builddir)/miext/damage/libdamage.la \
            $(top_builddir)/miext/sync/libsync.la \
            $(top_builddir)/randr/librandr.la \
            $(top_builddir)/render/librender.la \
            $(top_builddir)/Xext/libXext.la \
            $(top_builddir)/Xext/libXextdpmsstubs.la \
            $(top_builddir)/Xext/libXvidmode.la \
            $(top_builddir)/Xi/libXi.la \
            $(top_builddir)/Xi/libXistubs.la \
            $(top_builddir)/xfixes/libxfixes.la \
            $(top_builddir)/xkb/libxkb.la \
            $(top_builddir)/xkb/libxkbstubs.la

if COMPOSITE
libxservertest_la_LIBADD += \
            $(top_builddir)/composite/libcomposite.la
endif

if DBE
libxservertest_la_LIBADD += \
            $(top_builddir)/dbe/libdbe.la
endif

if GLX
libxservertest_la_LIBADD += \
            $(top_builddir)/glx/libglx.la
endif

if RECORD
libxservertest_la_LIBADD += \
            $(top_builddir)/record/librecord.la
endif

if DRI3
libxservertest_la_LIBADD += \
            $(top_builddir)/dri3/libdri3.la
endif

if XQUARTZ
libxservertest_la_LIBADD += \
            $(top_builddir)/miext/rootless/librootless.la
TEST_LDADD += -lXplugin
endif

if XWIN_MULTIWINDOWEXTWM
libxservertest_la_LIBADD += \
            $(top_builddir)/miext/rootless/librootless.la
endif
endif

libxservertest_la_DEPENDENCIES = $(libxservertest_la_LIBADD)
endif

EXTRA_DIST = ddxstubs.c

