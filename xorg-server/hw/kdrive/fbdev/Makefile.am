AM_CPPFLAGS = 					\
	@KDRIVE_INCS@				\
	@KDRIVE_CFLAGS@

noinst_LTLIBRARIES = libfbdev.la

libfbdev_la_SOURCES =	\
	fbdev.c		\
	fbdev.h

if KDRIVEFBDEV
bin_PROGRAMS = Xfbdev

Xfbdev_SOURCES = \
	fbinit.c

Xfbdev_LDADD = 						\
	libfbdev.la					\
	@KDRIVE_MAIN_LIB@				\
	@KDRIVE_LIBS@

Xfbdev_DEPENDENCIES =	\
	libfbdev.la					\
	$(KDRIVE_PURE_LIBS)

Xfbdev_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)

relink:
	$(AM_V_at)rm -f $(bin_PROGRAMS) && $(MAKE) $(bin_PROGRAMS)
endif
