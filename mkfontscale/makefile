TTYAPP = mkfontscale

DEFINES += _BSD_SOURCE

INCLUDES += $(MHMAKECONF)\freetype\include

INCLUDELIBFILES = \
 $(MHMAKECONF)\zlib\$(OBJDIR)\zlib1.lib \
 $(MHMAKECONF)\libXfont\src\fontfile\$(OBJDIR)\libfontfile.lib \
 $(MHMAKECONF)\libXfont\src\util\$(OBJDIR)\libutil.lib \
 $(MHMAKECONF)\libfontenc\src\$(OBJDIR)\libfontenc.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

CSRCS = hash.c \
	ident.c \
	list.c \
	mkfontscale.c

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\freetype\lib\freetype2501MT_D.lib
else
LINKLIBS += $(MHMAKECONF)\freetype\lib\freetype2501MT.lib
endif


