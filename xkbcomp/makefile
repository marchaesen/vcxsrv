INCLUDELIBFILES = $(MHMAKECONF)\libX11\$(OBJDIR)\libX11.lib \
                  $(MHMAKECONF)\libxcb\src\$(OBJDIR)\libxcb.lib \
                  $(MHMAKECONF)\libXau\$(OBJDIR)\libXau.lib \
                  $(MHMAKECONF)\libxkbfile\src\$(OBJDIR)\libxkbfile.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)

ifeq $(DEBUG),1
TTYAPP = xkbcomp
else
WINAPP = xkbcomp
endif

DEFINES += DFLT_XKB_CONFIG_ROOT="\".\""  PACKAGE_VERSION="\"1.2.3\""

INCLUDES += $(OBJDIR)

CSRCS = action.c \
        alias.c \
        compat.c \
        expr.c \
        geometry.c \
        indicators.c \
        keycodes.c \
        keymap.c \
        keytypes.c \
        listing.c \
        misc.c \
        parseutils.c \
        symbols.c \
        utils.c \
        vmod.c \
        xkbcomp.c \
        xkbparse.c \
        xkbpath.c \
        xkbscan.c

LINKLIBS += $(MHMAKECONF)\openssl\out32\libeay32.lib

ifeq ($(DEBUG),1)
LINKLIBS += $(MHMAKECONF)\freetype\lib\freetype2410MT_D.lib \
            $(MHMAKECONF)\pthreads\pthreadVC2d.lib
else
LINKLIBS += $(MHMAKECONF)\freetype\lib\freetype2410MT.lib \
            $(MHMAKECONF)\pthreads\pthreadVC2.lib
endif

$(OBJDIR)\xkbparse.c $(OBJDIR)\xkbparse.h: xkbparse.y
	bison.bat -d -o$(OBJDIR)\xkbparse.c $<
