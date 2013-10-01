AM_CPPFLAGS = -I${top_srcdir}/include

AM_CFLAGS = $(XFONT_CFLAGS) $(OS_CFLAGS) $(CWARNFLAGS)

noinst_LTLIBRARIES = libfontfile.la

libfontfile_la_LIBADD = 	\
	$(Z_LIBS)

libfontfile_la_SOURCES = 	\
	bitsource.c		\
	bufio.c			\
	decompress.c		\
	defaults.c		\
	dirfile.c		\
	fileio.c		\
	filewr.c		\
	fontdir.c		\
	fontencc.c		\
	fontfile.c		\
	fontscale.c		\
	gunzip.c		\
	register.c		\
	renderers.c		\
	catalogue.c

if X_BZIP2_FONT_COMPRESSION
libfontfile_la_SOURCES += bunzip2.c
endif
