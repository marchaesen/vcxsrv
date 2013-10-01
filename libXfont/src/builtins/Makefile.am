AM_CPPFLAGS =			\
	-I${top_srcdir}/include \
	-I${top_srcdir}/src/bitmap

AM_CFLAGS = $(XFONT_CFLAGS) $(OS_CFLAGS) $(CWARNFLAGS)

noinst_LTLIBRARIES = libbuiltins.la

libbuiltins_la_SOURCES = 	\
	builtin.h		\
	dir.c			\
	file.c			\
	fonts.c			\
	fpe.c			\
	render.c

EXTRA_DIST = buildfont
