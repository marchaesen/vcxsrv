
libXfont2_la_SOURCES =			\
	src/stubs/atom.c		\
	src/stubs/libxfontstubs.c	\
	src/util/fontaccel.c		\
	src/util/fontnames.c		\
	src/util/fontutil.c		\
	src/util/fontxlfd.c		\
	src/util/format.c		\
	src/util/miscutil.c		\
	src/util/patcache.c		\
	src/util/private.c		\
	src/util/utilbitmap.c

libXfont2_la_SOURCES +=			\
	src/fontfile/bitsource.c	\
	src/fontfile/bufio.c		\
	src/fontfile/decompress.c	\
	src/fontfile/defaults.c		\
	src/fontfile/dirfile.c		\
	src/fontfile/fileio.c		\
	src/fontfile/filewr.c		\
	src/fontfile/fontdir.c		\
	src/fontfile/fontencc.c		\
	src/fontfile/fontfile.c		\
	src/fontfile/fontscale.c	\
	src/fontfile/gunzip.c		\
	src/fontfile/register.c		\
	src/fontfile/renderers.c	\
	src/fontfile/catalogue.c

#libXfont2_la_SOURCES += src/fontfile/bunzip2.c

libXfont2_la_SOURCES +=			\
	src/FreeType/ft.h		\
	src/FreeType/ftfuncs.h		\
	src/FreeType/xttcap.h		\
	src/FreeType/ftenc.c		\
	src/FreeType/ftfuncs.c		\
	src/FreeType/fttools.c		\
	src/FreeType/xttcap.c

libXfont2_la_SOURCES +=			\
	src/bitmap/bitmap.c		\
	src/bitmap/bitmapfunc.c		\
	src/bitmap/bitmaputil.c		\
	src/bitmap/bitscale.c		\
	src/bitmap/fontink.c

libXfont2_la_SOURCES +=			\
	src/bitmap/bdfread.c		\
	src/bitmap/bdfutils.c

libXfont2_la_SOURCES += src/bitmap/pcfread.c

libXfont2_la_SOURCES += src/bitmap/pcfwrite.c

libXfont2_la_SOURCES +=			\
	src/bitmap/snfread.c		\
	src/bitmap/snfstr.h

libXfont2_la_SOURCES +=			\
	src/builtins/builtin.h		\
	src/builtins/dir.c		\
	src/builtins/file.c		\
	src/builtins/fonts.c		\
	src/builtins/fpe.c		\
	src/builtins/render.c

libXfont2_la_SOURCES +=			\
	src/fc/fsconvert.c		\
	src/fc/fserve.c			\
	src/fc/fserve.h			\
	src/fc/fservestr.h		\
	src/fc/fsio.c			\
	src/fc/fsio.h			\
	src/fc/fslibos.h		\
	src/fc/fstrans.c

LIBRARY = libXfont2

CSRCS := $(notdir $(subst /,$/,$(libXfont2_la_SOURCES)))
CSRCS := $(CSRCS:%.h=)

INCLUDES += . ./include ./include/X11/fonts
INCLUDES += $(MHMAKECONF)\freetype\include\freetype  $(MHMAKECONF)\freetype\include

DEFINES += strcasecmp=_stricmp

vpath %.c src\stubs:src\util:src\fontfile:src\FreeType:src\bitmap:src\builtins:src\fc



