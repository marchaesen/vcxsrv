AM_CPPFLAGS = 					\
	@KDRIVE_INCS@				\
	@KDRIVE_CFLAGS@

noinst_LTLIBRARIES = libfake.la

bin_PROGRAMS = Xfake

libfake_la_SOURCES =	\
	fake.c		\
	kbd.c		\
	os.c		\
	mouse.c		\
	fake.h

Xfake_SOURCES = \
	fakeinit.c

Xfake_LDADD = 						\
	libfake.la					\
	@KDRIVE_MAIN_LIB@				\
	@KDRIVE_LIBS@

Xfake_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)

Xfake_DEPENDENCIES =	\
	libfake.la					\
	@KDRIVE_LOCAL_LIBS@

relink:
	$(AM_V_at)rm -f $(bin_PROGRAMS) && $(MAKE) $(bin_PROGRAMS)
