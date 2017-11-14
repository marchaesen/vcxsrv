TTYAPP = bdftopcf

INCLUDELIBFILES = \
 $(MHMAKECONF)\apps\common\$(OBJDIR)\common.lib \
 $(MHMAKECONF)\zlib\$(OBJDIR)\zlib1.lib \
 $(MHMAKECONF)\libXfont2\$(OBJDIR)\libXfont2.lib \
 $(MHMAKECONF)\libfontenc\src\$(OBJDIR)\libfontenc.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

CSRCS = \
	atom.c \
	bdfread.c \
	bdfutils.c \
	bitmap.c \
	bitmaputil.c \
	bufio.c \
	defaults.c \
	fileio.c \
	filewr.c \
	fontaccel.c \
	fontink.c \
	pcfwrite.c \
	private.c \
	utilbitmap.c \
	bdftopcf.c

LINKLIBS += $(FREETYPELIB)

