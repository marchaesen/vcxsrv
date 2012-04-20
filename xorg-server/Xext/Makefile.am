# libXext.la:        includes all extensions and should be linked into Xvfb,
#                    Xnest, Xdmx and Xprt
# libXextbuiltin.la: includes those extensions that are built directly into
#                    Xorg by default
# libXextmodule.la:  includes those extensions that are built into a module
#                    that Xorg loads
if XORG
noinst_LTLIBRARIES = libXext.la libXextbuiltin.la libXextmodule.la
else
noinst_LTLIBRARIES = libXext.la
endif

INCLUDES = -I$(top_srcdir)/hw/xfree86/dixmods/extmod

AM_CFLAGS = $(DIX_CFLAGS)

if XORG
sdk_HEADERS = xvdix.h xvmcext.h geext.h geint.h shmint.h syncsdk.h
endif

# Sources always included in libXextbuiltin.la & libXext.la
BUILTIN_SRCS =			\
	bigreq.c		\
        geext.c			\
	shape.c			\
	sleepuntil.c		\
	sleepuntil.h		\
	sync.c			\
	syncsdk.h		\
	syncsrv.h		\
	xcmisc.c		\
	xtest.c

# Sources always included in libXextmodule.la & libXext.la. That's right, zero.
MODULE_SRCS =
MODULE_LIBS =

# Optional sources included if extension enabled by configure.ac rules

# MIT Shared Memory extension
MITSHM_SRCS = shm.c shmint.h
if MITSHM
BUILTIN_SRCS += $(MITSHM_SRCS)
endif

# XVideo extension
XV_SRCS = xvmain.c xvdisp.c xvmc.c xvdix.h xvmcext.h xvdisp.h
if XV
MODULE_SRCS  += $(XV_SRCS)
endif

# XResource extension: lets clients get data about per-client resource usage
RES_SRCS = hashtable.c xres.c
if RES
MODULE_SRCS  += $(RES_SRCS)
endif

# MIT ScreenSaver extension
SCREENSAVER_SRCS = saver.c
if SCREENSAVER
MODULE_SRCS  += $(SCREENSAVER_SRCS)
endif

# Xinerama extension: making multiple video devices act as one virtual screen
XINERAMA_SRCS = panoramiX.c panoramiX.h panoramiXh.h panoramiXsrv.h panoramiXprocs.c panoramiXSwap.c 
if XINERAMA
BUILTIN_SRCS += $(XINERAMA_SRCS)
if XORG
sdk_HEADERS += panoramiXsrv.h panoramiX.h
endif
endif

# X-ACE extension: provides hooks for building security policy extensions
# like XC-Security, X-SELinux & XTSol
XACE_SRCS = xace.c xace.h xacestr.h
if XACE
BUILTIN_SRCS += $(XACE_SRCS)
if XORG
sdk_HEADERS += xace.h xacestr.h
endif
endif

# SELinux extension: provides SELinux policy support for X objects
# requires X-ACE extension
XSELINUX_SRCS = xselinux_ext.c xselinux_hooks.c xselinux_label.c xselinux.h xselinuxint.h
if XSELINUX
MODULE_SRCS += $(XSELINUX_SRCS)
MODULE_LIBS += $(SELINUX_LIBS)
endif

# Security extension: multi-level security to protect clients from each other
XCSECURITY_SRCS = security.c securitysrv.h
if XCSECURITY   
BUILTIN_SRCS += $(XCSECURITY_SRCS)
endif

# XF86 Big Font extension
BIGFONT_SRCS = xf86bigfont.c xf86bigfontsrv.h
if XF86BIGFONT
BUILTIN_SRCS += $(BIGFONT_SRCS)
endif

# DPMS extension
DPMS_SRCS = dpms.c dpmsproc.h
if DPMSExtension
MODULE_SRCS += $(DPMS_SRCS)
endif

# Now take all of the above, mix well, bake for 10 minutes and get libXext*.la

libXext_la_SOURCES =		$(BUILTIN_SRCS) $(MODULE_SRCS)
libXext_la_LIBADD =		$(MODULE_LIBS)

if XORG
libXextbuiltin_la_SOURCES =	$(BUILTIN_SRCS)

libXextmodule_la_SOURCES =	$(MODULE_SRCS)
libXextmodule_la_LIBADD =	$(MODULE_LIBS)
endif

EXTRA_DIST = \
	$(MITSHM_SRCS) \
	$(XV_SRCS) \
	$(RES_SRCS) \
	$(SCREENSAVER_SRCS) \
	$(XACE_SRCS) \
	$(XCSECURITY_SRCS) \
	$(XSELINUX_SRCS) \
	$(XINERAMA_SRCS) \
	$(BIGFONT_SRCS) \
	$(DPMS_SRCS) \
        $(GE_SRCS)

