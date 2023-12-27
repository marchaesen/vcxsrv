libmandir = $(LIB_MAN_DIR)

libman_PRE =				\
	Xft.man				\
	$(Xft_sources)

Xft_sources = \
	$(Xft_shadows:=.man)

Xft_shadows =				\
	XftCharExists			\
	XftCharFontSpecRender		\
	XftCharIndex			\
	XftCharSpecRender		\
	XftColorAllocName		\
	XftColorAllocValue		\
	XftColorFree			\
	XftDefaultHasRender		\
	XftDefaultSet			\
	XftDefaultSubstitute		\
	XftDrawChange			\
	XftDrawCharFontSpec		\
	XftDrawCharSpec			\
	XftDrawColormap			\
	XftDrawCreate			\
	XftDrawCreateAlpha		\
	XftDrawCreateBitmap		\
	XftDrawDestroy			\
	XftDrawDisplay			\
	XftDrawDrawable			\
	XftDrawGlyphFontSpec		\
	XftDrawGlyphSpec		\
	XftDrawGlyphs			\
	XftDrawPicture			\
	XftDrawRect			\
	XftDrawSetClip			\
	XftDrawSetClipRectangles	\
	XftDrawSetSubwindowMode		\
	XftDrawSrcPicture		\
	XftDrawString16			\
	XftDrawString32			\
	XftDrawString8			\
	XftDrawStringUtf16		\
	XftDrawStringUtf8		\
	XftDrawVisual			\
	XftFontCheckGlyph		\
	XftFontClose			\
	XftFontCopy			\
	XftFontInfoCreate		\
	XftFontInfoDestroy		\
	XftFontInfoEqual		\
	XftFontInfoHash			\
	XftFontLoadGlyphs		\
	XftFontMatch			\
	XftFontOpen			\
	XftFontOpenInfo			\
	XftFontOpenName			\
	XftFontOpenPattern		\
	XftFontOpenXlfd			\
	XftFontUnloadGlyphs		\
	XftGetVersion			\
	XftGlyphExtents			\
	XftGlyphFontSpecRender		\
	XftGlyphRender			\
	XftGlyphSpecRender		\
	XftInit				\
	XftInitFtLibrary		\
	XftListFonts			\
	XftLockFace			\
	XftNameParse			\
	XftNameUnparse			\
	XftTextExtents16		\
	XftTextExtents32		\
	XftTextExtents8			\
	XftTextExtentsUtf16		\
	XftTextExtentsUtf8		\
	XftTextRender16			\
	XftTextRender16BE		\
	XftTextRender16LE		\
	XftTextRender32			\
	XftTextRender32BE		\
	XftTextRender32LE		\
	XftTextRender8			\
	XftTextRenderUtf16		\
	XftTextRenderUtf8		\
	XftUnlockFace			\
	XftXlfdParse

libman_DATA = $(libman_PRE:man=$(LIB_MAN_SUFFIX))

EXTRA_DIST = $(libman_PRE)
CLEANFILES = $(libman_DATA) $(Xft_sources)

SUFFIXES = .$(LIB_MAN_SUFFIX) .man

$(Xft_sources) :
	$(AM_V_GEN)echo ".so man__libmansuffix__/Xft.__libmansuffix__" > $@

# String replacements in MAN_SUBSTS now come from xorg-macros.m4 via configure
.man.$(LIB_MAN_SUFFIX):
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@
