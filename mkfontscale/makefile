TTYAPP = mkfontscale

DEFINES += _BSD_SOURCE

INCLUDES += $(MHMAKECONF)\freetype\include\freetype $(MHMAKECONF)\freetype\include

INCLUDELIBFILES = \
 $(MHMAKECONF)\zlib\$(OBJDIR)\zlib1.lib \
 $(MHMAKECONF)\libXfont2\$(OBJDIR)\libXfont2.lib \
 $(MHMAKECONF)\libfontenc\src\$(OBJDIR)\libfontenc.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

CSRCS = constlist.c \
	hash.c \
	ident.c \
	list.c \
	mkfontscale.c

LINKLIBS += $(FREETYPELIB)

all: $(OBJDIR)/freetype.dll $(OBJDIR)/zlib1.dll

$(OBJDIR)/freetype.dll: $(FREETYPELIB:lib=dll)
	cp $< $@

$(OBJDIR)/zlib1.dll: $(MHMAKECONF)\zlib\$(OBJDIR)\zlib1.dll
	cp $< $@

