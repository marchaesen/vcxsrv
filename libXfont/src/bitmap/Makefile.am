AM_CPPFLAGS =			\
	-I${top_srcdir}/include

AM_CFLAGS = $(XFONT_CFLAGS) $(OS_CFLAGS) $(CWARNFLAGS)

noinst_LTLIBRARIES = libbitmap.la

libbitmap_la_SOURCES = 		\
	bitmap.c		\
	bitmapfunc.c		\
	bitmaputil.c		\
	bitscale.c		\
	fontink.c

if XFONT_BDFFORMAT
libbitmap_la_SOURCES += bdfread.c bdfutils.c
endif

if XFONT_PCF_OR_BUILTIN
libbitmap_la_SOURCES += pcfread.c
endif

if XFONT_PCFFORMAT
libbitmap_la_SOURCES += pcfwrite.c
endif

if XFONT_SNFFORMAT
libbitmap_la_SOURCES += snfread.c snfstr.h
endif
