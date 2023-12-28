lib_LTLIBRARIES = libXmu.la libXmuu.la

BITMAP_DEFINES = -DBITMAPDIR=\"$(includedir)/X11/bitmaps\"

AM_CPPFLAGS = \
	-I${top_srcdir}/include \
	-I${top_srcdir}/include/X11/Xmu \
	$(BITMAP_DEFINES) -D_CONST_X_STRING

AM_CFLAGS = $(CWARNFLAGS) $(XMU_CFLAGS)

libXmu_la_LDFLAGS = -version-number 6:2:0 -no-undefined
libXmuu_la_LDFLAGS = -version-number 1:0:0 -no-undefined

libXmu_la_LIBADD = $(LTLIBOBJS) $(XMU_LIBS)
libXmuu_la_LIBADD = $(XMUU_LIBS)

libXmuu_la_SOURCES = \
	ClientWin.c \
	CursorName.c \
	DefErrMsg.c \
	GetHost.c \
	Lower.c

libXmu_la_SOURCES = \
	$(libXmuu_la_SOURCES) \
	Xmuint.h \
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


if LINT
ALL_LINT_FLAGS=$(LINT_FLAGS) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) \
                $(AM_CPPFLAGS) $(CPPFLAGS)

lint:
	$(LINT) $(ALL_LINT_FLAGS) $(libXmu_la_SOURCES) $(XEXT_LIBS)
endif LINT

if MAKE_LINT_LIB
lintlibdir = $(libdir)

lintlib_DATA = $(LINTLIB) $(LINTLIBUU)

$(LINTLIB): $(libXmu_la_SOURCES)
	$(LINT) -y -oXmu -x $(ALL_LINT_FLAGS) $(libXmu_la_SOURCES)

$(LINTLIBUU): $(libXmuu_la_SOURCES)
	$(LINT) -y -oXmuu -x $(ALL_LINT_FLAGS) $(libXmuu_la_SOURCES)
endif MAKE_LINT_LIB
