DEFINES += HAVE_XKB_CONFIG_H PIXMAN_API=

INCLUDES += ..

DDX_SRCS = \
        ddxBeep.c \
        ddxCtrls.c \
        ddxLEDs.c \
        ddxLoad.c

DIX_SRCS = \
        xkb.c \
        xkbUtils.c \
        xkbEvents.c \
        xkbAccessX.c \
        xkbSwap.c \
        xkbLEDs.c \
        xkbInit.c \
        xkbActions.c \
        xkbPrKeyEv.c

# this should be replaced by a common library or something, ideally -d
XKBFILE_SRCS = \
        maprules.c \
        xkmread.c \
        xkbtext.c \
        xkbfmisc.c \
        xkbout.c

X11_SRCS = \
        XKBMisc.c \
        XKBAlloc.c \
        XKBGAlloc.c \
        XKBMAlloc.c

# ends up unused...
# XI_SRCS = xkbPrOtherEv.c

CSRCS = $(DDX_SRCS) $(DIX_SRCS) $(XI_SRCS) $(XKBFILE_SRCS) \
                    $(X11_SRCS) ddxVT.c ddxPrivate.c ddxKillSrv.c

LIBRARY=libxkb

