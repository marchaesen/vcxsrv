SHAREDLIB = libXext

INCLUDES := ..\include $(INCLUDES)

INCLUDELIBFILES =  \
 $(MHMAKECONF)\libX11\$(OBJDIR)\libX11.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)


CSRCS = \
	DPMS.c \
	MITMisc.c \
	XAppgroup.c \
	XEVI.c \
	XLbx.c \
	XMultibuf.c \
	XSecurity.c \
	XShape.c \
	XShm.c \
	XSync.c \
	XTestExt1.c \
	Xcup.c \
	Xdbe.c \
        Xge.c \
	extutil.c \
	globals.c

