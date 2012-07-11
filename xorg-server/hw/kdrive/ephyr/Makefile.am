SUBDIRS = man

INCLUDES = 			\
	@KDRIVE_INCS@		\
	@KDRIVE_CFLAGS@		\
	@XEPHYR_INCS@		\
	@XEPHYR_CFLAGS@		\
	-I$(top_srcdir)		\
	-I$(top_srcdir)/exa

if XV
LIBXEPHYR_HOSTXV=libxephyr-hostxv.la
endif

if DRI
LIBXEPHYR_HOSTDRI=libxephyr-hostdri.la
endif

noinst_LTLIBRARIES = libxephyr-hostx.la $(LIBXEPHYR_HOSTXV) $(LIBXEPHYR_HOSTDRI) libxephyr.la

bin_PROGRAMS = Xephyr

HOSTX_SRCS =			\
	hostx.c			\
	hostx.h

HOSTVIDEO_SRCS =		\
	ephyrvideo.c		\
	ephyrhostvideo.c	\
	ephyrhostvideo.h

HOSTDRI_SRCS =			\
	ephyrdriext.c		\
	ephyrdriext.h		\
	ephyrdri.c		\
	ephyrdri.h		\
	XF86dri.c		\
	xf86dri.h		\
	ephyrglxext.c		\
	ephyrglxext.h		\
	ephyrhostglx.c		\
	ephyrhostglx.h

XEPHYR_SRCS =			\
	ephyr.c			\
	ephyr.h			\
	ephyrlog.h		\
	ephyr_draw.c		\
	os.c

libxephyr_hostx_la_SOURCES = $(HOSTX_SRCS)

if XV
libxephyr_hostxv_la_SOURCES = $(HOSTVIDEO_SRCS)
endif

if DRI
libxephyr_hostdri_la_SOURCES = $(HOSTDRI_SRCS)
endif

libxephyr_la_SOURCES = $(XEPHYR_SRCS)

Xephyr_SOURCES = \
	ephyrinit.c

Xephyr_LDADD = 						\
	libxephyr.la					\
	libxephyr-hostx.la				\
	$(LIBXEPHYR_HOSTXV)				\
	$(LIBXEPHYR_HOSTDRI)				\
	$(top_builddir)/exa/libexa.la			\
	@KDRIVE_LIBS@					\
	@XEPHYR_LIBS@

Xephyr_DEPENDENCIES =	\
	libxephyr.la					\
	libxephyr-hostx.la				\
	$(LIBXEPHYR_HOSTXV)				\
	$(LIBXEPHYR_HOSTDRI)				\
	@KDRIVE_LOCAL_LIBS@

Xephyr_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)

relink:
	$(AM_V_at)rm -f $(bin_PROGRAMS) && $(MAKE) $(bin_PROGRAMS)

EXTRA_DIST = \
	$(HOSTVIDEO_SRCS) \
	$(HOSTDRI_SRCS)
