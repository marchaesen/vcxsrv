SHAREDLIB = libXmu

INCLUDES := ..\include ..\include\X11\Xmu $(INCLUDES)

DEFINES += XT_NO_SM BITMAPDIR="""bitmaps"""

INCLUDELIBFILES =  \
 $(MHMAKECONF)\libX11\$(OBJDIR)\libX11.lib \
 $(MHMAKECONF)\libXext\src\$(OBJDIR)\libXext.lib \
 $(MHMAKECONF)\libXt\src\$(OBJDIR)\libXt.lib

LIBDIRS=$(dir $(INCLUDELIBFILES))

load_makefile $(LIBDIRS:%$(OBJDIR)\=%makefile MAKESERVER=0 DEBUG=$(DEBUG);)


CSRCS = \
	ClientWin.c \
	CursorName.c \
	DefErrMsg.c \
	GetHost.c \
	Lower.c \
	AllCmap.c \
	Atoms.c \
	Clip.c \
	CloseHook.c \
	CmapAlloc.c \
	CrCmap.c \
	CrPixFBit.c \
	CvtCache.c \
	CvtStdSel.c \
	DelCmap.c \
	DisplayQue.c \
	Distinct.c \
	DrawLogo.c \
	DrRndRect.c \
	EditresCom.c \
	ExtAgent.c \
	FToCback.c \
	GrayPixmap.c \
	Initer.c \
	LocBitmap.c \
	Lookup.c \
	LookupCmap.c \
	RdBitF.c \
	ScrOfWin.c \
	ShapeWidg.c \
	StdCmap.c \
	StrToBS.c \
	StrToBmap.c \
	StrToCurs.c \
	StrToGrav.c \
	StrToJust.c \
	StrToLong.c \
	StrToOrnt.c \
	StrToShap.c \
	StrToWidg.c \
	UpdMapHint.c \
	VisCmap.c \
	WidgetNode.c \
	Xct.c

