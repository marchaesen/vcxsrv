/*
 * Copyright © 2022 Thomas E. Dickey
 * Copyright © 2000 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the above copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The above copyright holders make no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * THE ABOVE LISTED COPYRIGHT HOLDER(S) DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "xftint.h"

_X_HIDDEN FT_Library  _XftFTlibrary;

#define FT_Matrix_Equal(a,b)	((a)->xx == (b)->xx && \
				 (a)->yy == (b)->yy && \
				 (a)->xy == (b)->xy && \
				 (a)->yx == (b)->yx)
/*
 * List of all open files (each face in a file is managed separately)
 */

static XftFtFile *_XftFtFiles;
static int XftMaxFreeTypeFiles = 5;

static XftFtFile *
_XftGetFile (const FcChar8 *file, int id)
{
    XftFtFile	*f;

    if (!XftInitFtLibrary ())
	return NULL;

    for (f = _XftFtFiles; f; f = f->next)
    {
	if (!strcmp (f->file, (const char *) file) && f->id == id)
	{
	    ++f->ref;
	    if (XftDebug () & XFT_DBG_REF)
		printf ("FontFile %s/%d matches existing (%d)\n",
			file, id, f->ref);
	    return f;
	}
    }
    f = malloc (sizeof (XftFtFile) + strlen ((const char *) file) + 1);
    if (!f)
	return NULL;

    XftMemAlloc (XFT_MEM_FILE, sizeof (XftFtFile) + strlen ((const char *) file) + 1);
    if (XftDebug () & XFT_DBG_REF)
    	printf ("FontFile %s/%d matches new\n",
		file, id);
    f->next = _XftFtFiles;
    _XftFtFiles = f;

    f->ref = 1;

    f->file = (char *) (f+1);
    strcpy (f->file, (const char *) file);
    f->id = id;

    f->lock = 0;
    f->face = NULL;
    f->xsize = 0;
    f->ysize = 0;
    f->matrix.xx = f->matrix.xy = f->matrix.yx = f->matrix.yy = 0;
    return f;
}

static XftFtFile *
_XftGetFaceFile (FT_Face face)
{
    XftFtFile	*f;

    f = malloc (sizeof (XftFtFile));
    if (!f)
	return NULL;
    XftMemAlloc (XFT_MEM_FILE, sizeof (XftFtFile));
    f->next = NULL;

    f->ref = 1;

    f->file = NULL;
    f->id = 0;
    f->lock = 0;
    f->face = face;
    f->xsize = 0;
    f->ysize = 0;
    f->matrix.xx = f->matrix.xy = f->matrix.yx = f->matrix.yy = 0;
    return f;
}

static int
_XftNumFiles (void)
{
    XftFtFile	*f;
    int		count = 0;
    for (f = _XftFtFiles; f; f = f->next)
	if (f->face && !f->lock)
	    ++count;
    return count;
}

static XftFtFile *
_XftNthFile (int n)
{
    XftFtFile	*f;
    int		count = 0;
    for (f = _XftFtFiles; f; f = f->next)
	if (f->face && !f->lock)
	    if (count++ == n)
		break;
    return f;
}

static void
_XftUncacheFiles (void)
{
    int		n;
    XftFtFile	*f;
    while ((n = _XftNumFiles ()) > XftMaxFreeTypeFiles)
    {
	f = _XftNthFile (n ? (rand () % n) : 0);
	if (f)
	{
	    if (XftDebug() & XFT_DBG_REF)
		printf ("Discard file %s/%d from cache\n",
			f->file, f->id);
	    FT_Done_Face (f->face);
	    f->face = NULL;
	}
    }
}

static FT_Face
_XftLockFile (XftFtFile *f)
{
    ++f->lock;
    if (!f->face)
    {
	if (XftDebug() & XFT_DBG_REF)
	    printf ("Loading file %s/%d\n", f->file, f->id);
	if (FT_New_Face (_XftFTlibrary, f->file, f->id, &f->face))
	    --f->lock;

	f->xsize = 0;
	f->ysize = 0;
	f->matrix.xx = f->matrix.xy = f->matrix.yx = f->matrix.yy = 0;
	_XftUncacheFiles ();
    }
    return f->face;
}

static void
_XftLockError (const char *reason)
{
    fprintf (stderr, "Xft: locking error %s\n", reason);
}

static void
_XftUnlockFile (XftFtFile *f)
{
    if (--f->lock < 0)
	_XftLockError ("too many file unlocks");
}

#define X_SIZE(face,i) ((face)->available_sizes[i].x_ppem)
#define Y_SIZE(face,i) ((face)->available_sizes[i].y_ppem)

_X_HIDDEN FcBool
_XftSetFace (XftFtFile *f, FT_F26Dot6 xsize, FT_F26Dot6 ysize, FT_Matrix *matrix)
{
    FT_Face face = f->face;

    if (f->xsize != xsize || f->ysize != ysize)
    {
	if (XftDebug() & XFT_DBG_GLYPH)
	    printf ("Set face size to %dx%d (%dx%d)\n",
		    (int) (xsize >> 6), (int) (ysize >> 6), (int) xsize, (int) ysize);
	/*
	 * Bitmap only faces must match exactly, so find the closest
	 * one (height dominant search)
	 */
	if (!(face->face_flags & FT_FACE_FLAG_SCALABLE))
	{
	    int		i, best = 0;

#define xft_abs(a)	((a) < 0 ? -(a) : (a))
#define dist(a,b)	(xft_abs((a)-(b)))

	    for (i = 1; i < face->num_fixed_sizes; i++)
	    {
		if (dist (ysize, Y_SIZE(face,i)) <
		    dist (ysize, Y_SIZE(face, best)) ||
		    (dist (ysize, Y_SIZE(face, i)) ==
		     dist (ysize, Y_SIZE(face, best)) &&
		     dist (xsize, X_SIZE(face, i)) <
		     dist (xsize, X_SIZE(face, best))))
		{
		    best = i;
		}
	    }
	    /*
	     * Freetype 2.1.7 and earlier used width/height
	     * for matching sizes in the BDF and PCF loaders.
	     * This has been fixed for 2.1.8.  Because BDF and PCF
	     * files have but a single strike per file, we can
	     * simply try both sizes.
	     */
	    if (FT_Set_Char_Size (face, face->available_sizes[best].x_ppem,
				  face->available_sizes[best].y_ppem, 0, 0) != 0
		&&
		FT_Set_Char_Size (face, face->available_sizes[best].width << 6,
				  face->available_sizes[best].height << 6,
				  0, 0) != 0)
	    {
		return False;
	    }
	}
	else
    	{
	    if (FT_Set_Char_Size (face, xsize, ysize, 0, 0))
	    {
		return False;
	    }
	}
	f->xsize = xsize;
	f->ysize = ysize;
    }
    if (!FT_Matrix_Equal (&f->matrix, matrix))
    {
	if (XftDebug() & XFT_DBG_GLYPH)
	    printf ("Set face matrix to (%g,%g,%g,%g)\n",
		    (double) matrix->xx / 0x10000,
		    (double) matrix->xy / 0x10000,
		    (double) matrix->yx / 0x10000,
		    (double) matrix->yy / 0x10000);
	FT_Set_Transform (face, matrix, NULL);
	f->matrix = *matrix;
    }
    return True;
}

static void
_XftReleaseFile (XftFtFile *f)
{
    XftFtFile	**prev;

    if (--f->ref != 0)
        return;
    if (f->lock)
	_XftLockError ("Attempt to close locked file");
    if (f->file)
    {
	for (prev = &_XftFtFiles; *prev; prev = &(*prev)->next)
	{
	    if (*prev == f)
	    {
		*prev = f->next;
		break;
	    }
	}
	if (f->face)
	    FT_Done_Face (f->face);
    }
    XftMemFree (XFT_MEM_FILE,
		(sizeof (XftFtFile) + (f->file ? strlen (f->file) + 1 : 0)));
    free (f);
}

/*
 * Find a prime larger than the minimum reasonable hash size
 */

static FcChar32
_XftSqrt (FcChar32 a)
{
    FcChar32	    l, h, m;

    l = 2;
    h = a/2;
    while ((h-l) > 1)
    {
	m = (h+l) >> 1;
	if (m * m < a)
	    l = m;
	else
	    h = m;
    }
    return h;
}

static FcBool
_XftIsPrime (FcChar32 i)
{
    FcChar32	l, t;

    if (i < 2)
	return FcFalse;
    if ((i & 1) == 0)
    {
	if (i == 2)
	    return FcTrue;
	return FcFalse;
    }
    l = _XftSqrt (i) + 1;
    for (t = 3; t <= l; t += 2)
	if (i % t == 0)
	    return FcFalse;
    return FcTrue;
}

static FcChar32
_XftHashSize (FcChar32 num_unicode)
{
    /* at least 31.25 % extra space */
    FcChar32	hash = num_unicode + (num_unicode >> 2) + (num_unicode >> 4);

    if ((hash & 1) == 0)
	hash++;
    while (!_XftIsPrime (hash))
	hash += 2;
    return hash;
}

_X_EXPORT FT_Face
XftLockFace (XftFont *public)
{
    XftFontInt	*font = (XftFontInt *) public;
    XftFontInfo	*fi = &font->info;
    FT_Face	face;

    face = _XftLockFile (fi->file);
    /*
     * Make sure the face is usable at the requested size
     */
    if (face && !_XftSetFace (fi->file, fi->xsize, fi->ysize, &fi->matrix))
    {
	_XftUnlockFile (fi->file);
	face = NULL;
    }
    return face;
}

_X_EXPORT void
XftUnlockFace (XftFont *public)
{
    XftFontInt	*font = (XftFontInt *) public;
    _XftUnlockFile (font->info.file);
}

static FcBool
XftFontInfoFill (Display *dpy, _Xconst FcPattern *pattern, XftFontInfo *fi)
{
    XftDisplayInfo  *info = _XftDisplayInfoGet (dpy, True);
    FcChar8	    *filename;
    int		    id, mid;
    double	    dsize;
    double	    aspect;
    FcMatrix	    *font_matrix, fm1;
    FcBool	    hinting, vertical_layout, autohint, global_advance;
    int             hint_style;
    FcChar32	    hash, *hashp;
    FT_Face	    face;
    int		    nhash;
    FcBool	    bitmap;

    if (!info)
	return FcFalse;

    /*
     * Initialize the whole XftFontInfo so that padding doesn't interfere with
     * hash or XftFontInfoEqual().
     */

    memset (fi, '\0', sizeof (*fi));

    /*
     * Find the associated file
     */
    switch (FcPatternGetString (pattern, FC_FILE, 0, &filename)) {
    case FcResultNoMatch:
	filename = NULL;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail0;
    }

    switch (FcPatternGetInteger (pattern, FC_INDEX, 0, &id)) {
    case FcResultNoMatch:
	id = 0;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail0;
    }

    if (filename)
	fi->file = _XftGetFile (filename, id);
    else if (FcPatternGetFTFace (pattern, FC_FT_FACE, 0, &face) == FcResultMatch
	     && face)
	fi->file = _XftGetFaceFile (face);
    if (!fi->file)
        goto bail0;

    /*
     * Compute pixel size
     */
    if (FcPatternGetDouble (pattern, FC_PIXEL_SIZE, 0, &dsize) != FcResultMatch)
	goto bail1;

    if (FcPatternGetDouble (pattern, FC_ASPECT, 0, &aspect) != FcResultMatch)
	aspect = 1.0;

    fi->ysize = (FT_F26Dot6) (dsize * 64.0);
    fi->xsize = (FT_F26Dot6) (dsize * aspect * 64.0);

    if (XftDebug() & XFT_DBG_OPEN)
	printf ("XftFontInfoFill: %s: %d (%g pixels)\n",
		(filename ? filename : (const FcChar8 *) "<none>"), id, dsize);
    /*
     * Get antialias value
     */
    switch (FcPatternGetBool (pattern, FC_ANTIALIAS, 0, &fi->antialias)) {
    case FcResultNoMatch:
	fi->antialias = True;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    /*
     * Get rgba value
     */
    switch (FcPatternGetInteger (pattern, FC_RGBA, 0, &fi->rgba)) {
    case FcResultNoMatch:
	fi->rgba = FC_RGBA_UNKNOWN;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    /*
     * Get lcd_filter value
     */
    switch (FcPatternGetInteger (pattern, FC_LCD_FILTER, 0, &fi->lcd_filter)) {
    case FcResultNoMatch:
	fi->lcd_filter = FC_LCD_DEFAULT;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    /*
     * Get matrix and transform values
     */
    switch (FcPatternGetMatrix (pattern, FC_MATRIX, 0, &font_matrix)) {
    case FcResultNoMatch:
	fi->matrix.xx = fi->matrix.yy = 0x10000;
	fi->matrix.xy = fi->matrix.yx = 0;
	break;
    case FcResultMatch:
	fi->matrix.xx = (FT_Fixed)(0x10000L * font_matrix->xx);
	fi->matrix.yy = (FT_Fixed)(0x10000L * font_matrix->yy);
	fi->matrix.xy = (FT_Fixed)(0x10000L * font_matrix->xy);
	fi->matrix.yx = (FT_Fixed)(0x10000L * font_matrix->yx);
	break;
    default:
	goto bail1;
    }

    mid = 1;
    while (FcPatternGetMatrix (pattern, FC_MATRIX, mid, &font_matrix) == FcResultMatch) {
	FcMatrixInit(&fm1);
#define PreScale(value) ((double) (value) / (double) 0x10000L)
	fm1.xx = PreScale(fi->matrix.xx);
	fm1.yy = PreScale(fi->matrix.yy);
	fm1.xy = PreScale(fi->matrix.xy);
	fm1.yx = PreScale(fi->matrix.yx);
	FcMatrixMultiply(&fm1, font_matrix, &fm1);
	fi->matrix.xx = (FT_Fixed)(0x10000L * fm1.xx);
	fi->matrix.yy = (FT_Fixed)(0x10000L * fm1.yy);
	fi->matrix.xy = (FT_Fixed)(0x10000L * fm1.xy);
	fi->matrix.yx = (FT_Fixed)(0x10000L * fm1.yx);
	mid++;
    }

    fi->transform = (fi->matrix.xx != 0x10000 || fi->matrix.xy != 0 ||
		     fi->matrix.yx != 0 || fi->matrix.yy != 0x10000);

    /*
     * Get render value, set to false if no Render extension present
     */
    if (info->hasRender)
    {
	switch (FcPatternGetBool (pattern, XFT_RENDER, 0, &fi->render)) {
	case FcResultTypeMismatch:
	    /*
	     * Fontconfig no longer supports xft's custom values in
	     * text patterns, so any name specifying render:true or
	     * render:false will have an invalid type in the resulting
	     * pattern. Just ignore that case so that the app doesn't
	     * just fail
	     */
	    /* fall through ... */
	case FcResultNoMatch:
	    fi->render = info->hasRender;
	    break;
	case FcResultMatch:
	    break;
	default:
	    goto bail1;
	}
    }
    else
	fi->render = FcFalse;

    /*
     * Compute glyph load flags
     */
    fi->load_flags = FT_LOAD_DEFAULT | FT_LOAD_COLOR;

#ifndef XFT_EMBEDDED_BITMAP
#define XFT_EMBEDDED_BITMAP "embeddedbitmap"
#endif

    switch (FcPatternGetBool (pattern, XFT_EMBEDDED_BITMAP, 0, &bitmap)) {
    case FcResultNoMatch:
	bitmap = FcFalse;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    /* disable bitmaps when anti-aliasing or transforming glyphs */
    if ((!bitmap && fi->antialias) || fi->transform)
	fi->load_flags |= FT_LOAD_NO_BITMAP;

    /* disable hinting if requested */
    switch (FcPatternGetBool (pattern, FC_HINTING, 0, &hinting)) {
    case FcResultNoMatch:
	hinting = FcTrue;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    switch (FcPatternGetBool (pattern, FC_EMBOLDEN, 0, &fi->embolden)) {
    case FcResultNoMatch:
	fi->embolden = FcFalse;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    switch (FcPatternGetInteger (pattern, FC_HINT_STYLE, 0, &hint_style)) {
    case FcResultNoMatch:
	hint_style = FC_HINT_FULL;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    if (!hinting
	|| hint_style == FC_HINT_NONE
	)
    {
	fi->load_flags |= FT_LOAD_NO_HINTING;
    }

    /* Figure out the load target, which modifies the hinting
     * behavior of FreeType based on the intended use of the glyphs.
     */
    if (fi->antialias)
    {
	if (FC_HINT_NONE < hint_style && hint_style < FC_HINT_FULL)
	{
	    fi->load_flags |= FT_LOAD_TARGET_LIGHT;
	}
	else
	{
	    /* autohinter will snap stems to integer widths, when
	     * the LCD targets are used.
	     */
	    switch (fi->rgba) {
	    case FC_RGBA_RGB:
	    case FC_RGBA_BGR:
		fi->load_flags |= FT_LOAD_TARGET_LCD;
		break;
	    case FC_RGBA_VRGB:
	    case FC_RGBA_VBGR:
		fi->load_flags |= FT_LOAD_TARGET_LCD_V;
		break;
	    }
	}
    }
    else
	fi->load_flags |= FT_LOAD_TARGET_MONO;

    /* set vertical layout if requested */
    switch (FcPatternGetBool (pattern, FC_VERTICAL_LAYOUT, 0, &vertical_layout)) {
    case FcResultNoMatch:
	vertical_layout = FcFalse;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    if (vertical_layout)
	fi->load_flags |= FT_LOAD_VERTICAL_LAYOUT;

    /* force autohinting if requested */
    switch (FcPatternGetBool (pattern, FC_AUTOHINT, 0, &autohint)) {
    case FcResultNoMatch:
	autohint = FcFalse;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    if (autohint)
	fi->load_flags |= FT_LOAD_FORCE_AUTOHINT;

    /* disable global advance width (for broken DynaLab TT CJK fonts) */
    switch (FcPatternGetBool (pattern, FC_GLOBAL_ADVANCE, 0, &global_advance)) {
    case FcResultNoMatch:
	global_advance = FcTrue;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    if (!global_advance)
	fi->load_flags |= FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH;

    /*
     * Get requested spacing value
     */
    switch (FcPatternGetInteger (pattern, FC_SPACING, 0, &fi->spacing)) {
    case FcResultNoMatch:
	fi->spacing = FC_PROPORTIONAL;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }

    /*
     * Check for minspace
     */

    switch (FcPatternGetBool (pattern, FC_MINSPACE, 0, &fi->minspace)) {
    case FcResultNoMatch:
	fi->minspace = FcFalse;
	break;
    case FcResultMatch:
	break;
    default:
	goto bail1;
    }
    /*
     * Check for fixed pixel spacing
     */
    switch (FcPatternGetInteger (pattern, FC_CHAR_WIDTH, 0, &fi->char_width)) {
    case FcResultNoMatch:
	fi->char_width = 0;
	break;
    case FcResultMatch:
	if (fi->char_width)
	    fi->spacing = FC_MONO;
	break;
    default:
	goto bail1;
    }

    /*
     * Step over hash value in the structure
     */
    hash = 0;
    hashp = (FcChar32 *) fi + 1;
    nhash = (sizeof (XftFontInfo) / sizeof (FcChar32)) - 1;

    while (nhash--)
	hash += *hashp++;
    fi->hash = hash;

    /*
     * All done
     */
    return FcTrue;

bail1:
    _XftReleaseFile (fi->file);
    fi->file = NULL;
bail0:
    return FcFalse;
}

static void
XftFontInfoEmpty (Display *dpy _X_UNUSED, XftFontInfo *fi)
{
    if (fi->file)
	_XftReleaseFile (fi->file);
}

XftFontInfo *
XftFontInfoCreate (Display *dpy, _Xconst FcPattern *pattern)
{
    XftFontInfo	*fi = malloc (sizeof (XftFontInfo));

    if (!fi)
	return NULL;

    if (!XftFontInfoFill (dpy, pattern, fi))
    {
	free (fi);
	fi = NULL;
    }
    XftMemAlloc (XFT_MEM_FONT, sizeof (XftFontInfo));
    return fi;
}

_X_EXPORT void
XftFontInfoDestroy (Display *dpy, XftFontInfo *fi)
{
    XftFontInfoEmpty (dpy, fi);
    XftMemFree (XFT_MEM_FONT, sizeof (XftFontInfo));
    free (fi);
}

_X_EXPORT FcChar32
XftFontInfoHash (_Xconst XftFontInfo *fi)
{
    return fi->hash;
}

_X_EXPORT FcBool
XftFontInfoEqual (_Xconst XftFontInfo *a, _Xconst XftFontInfo *b)
{
    return memcmp ((const void *) a, (const void *) b, sizeof (XftFontInfo)) == 0;
}

_X_EXPORT XftFont *
XftFontOpenInfo (Display	*dpy,
		 FcPattern	*pattern,
		 XftFontInfo	*fi)
{
    XftDisplayInfo	*info = _XftDisplayInfoGet (dpy, True);
    FT_Face		face;
    XftFont		**bucket;
    XftFontInt		*font;
    XRenderPictFormat	*format;
    FcCharSet		*charset;
    FcChar32		num_unicode;
    FcChar32		hash_value;
    FcChar32		rehash_value;
    FcBool		antialias;
    FcBool		color;
    int			max_glyph_memory;
    size_t		alloc_size;
    int			ascent, descent, height;
    int			i;
    FT_UInt		num_glyphs;

    if (!info)
	return NULL;
    /*
     * Find a matching previously opened font
     */
    bucket = &info->fontHash[fi->hash % XFT_NUM_FONT_HASH];
    for (font = (XftFontInt *) *bucket; font; font = (XftFontInt *) font->hash_next)
	if (XftFontInfoEqual (&font->info, fi))
	{
	    if (!font->ref++)
		--info->num_unref_fonts;
	    FcPatternDestroy (pattern);
	    return &font->public;
	}

    /*
     * No existing font, create another.
     */

    if (XftDebug () & XFT_DBG_CACHE)
	printf ("New font %s/%d size %dx%d\n",
		fi->file->file, fi->file->id,
		(int) fi->xsize >> 6, (int) fi->ysize >> 6);

    if (FcPatternGetInteger (pattern, XFT_MAX_GLYPH_MEMORY, 0,
			     &max_glyph_memory) != FcResultMatch)
	max_glyph_memory = XFT_FONT_MAX_GLYPH_MEMORY;

    face = _XftLockFile (fi->file);
    if (!face)
	goto bail0;

    if (!_XftSetFace (fi->file, fi->xsize, fi->ysize, &fi->matrix))
	goto bail1;

    /*
     * Get the set of Unicode codepoints covered by the font.
     * If the incoming pattern doesn't provide this data, go
     * off and compute it.  Yes, this is expensive, but it's
     * required to map Unicode to glyph indices.
     */
    if (FcPatternGetCharSet (pattern, FC_CHARSET, 0, &charset) == FcResultMatch)
	charset = FcCharSetCopy (charset);
    else
	charset = FcFreeTypeCharSet (face, FcConfigGetBlanks (NULL));

    antialias = fi->antialias;
    if (!(face->face_flags & FT_FACE_FLAG_SCALABLE))
	antialias = FcFalse;

#ifdef FT_HAS_SVG
    color = (FT_HAS_COLOR(face) && !FT_HAS_SVG(face)) ? FcTrue : FcFalse;
#else
    color = FT_HAS_COLOR(face) ? FcTrue : FcFalse;
#endif

    /*
     * Find the appropriate picture format
     */
    if (fi->render)
    {
	if (color)
	{
	    format = XRenderFindStandardFormat (dpy, PictStandardARGB32);
	}
	else if (antialias)
	{
	    switch (fi->rgba) {
	    case FC_RGBA_RGB:
	    case FC_RGBA_BGR:
	    case FC_RGBA_VRGB:
	    case FC_RGBA_VBGR:
		format = XRenderFindStandardFormat (dpy, PictStandardARGB32);
		break;
	    default:
		format = XRenderFindStandardFormat (dpy, PictStandardA8);
		break;
	    }
	}
	else
	{
	    format = XRenderFindStandardFormat (dpy, PictStandardA1);
	}

	if (!format)
	    goto bail2;
    }
    else
	format = NULL;

    if (charset)
    {
	num_unicode = FcCharSetCount (charset);
	hash_value = _XftHashSize (num_unicode);
	rehash_value = hash_value - 2;
    }
    else
    {
	hash_value = 0;
	rehash_value = 0;
    }

    /*
     * Sometimes the glyphs are numbered 1..n, other times 0..n-1,
     * accept either numbering scheme by making room in the table
     */
    num_glyphs = (FT_UInt)face->num_glyphs + 1;
    alloc_size = (sizeof (XftFontInt) +
		  (size_t)num_glyphs * sizeof (XftGlyph *) +
		  hash_value * sizeof (XftUcsHash));
    font = malloc (alloc_size);

    if (!font)
	goto bail2;

    XftMemAlloc (XFT_MEM_FONT, alloc_size);

    /*
     * Public fields
     */
    if (fi->transform)
    {
	FT_Vector	vector;

	vector.x = 0;
	vector.y = face->size->metrics.descender;
	FT_Vector_Transform (&vector, &fi->matrix);
	descent = (int)(-(vector.y >> 6));

	vector.x = 0;
	vector.y = face->size->metrics.ascender;
	FT_Vector_Transform (&vector, &fi->matrix);
	ascent = (int)(vector.y >> 6);

	if (fi->minspace)
	    height = ascent + descent;
	else
	{
	    vector.x = 0;
	    vector.y = face->size->metrics.height;
	    FT_Vector_Transform (&vector, &fi->matrix);
	    height = (int)(vector.y >> 6);
	}
    }
    else
    {
	descent = -(int)(face->size->metrics.descender >> 6);
	ascent = (int)(face->size->metrics.ascender >> 6);
	if (fi->minspace)
	    height = ascent + descent;
	else
	    height = (int)(face->size->metrics.height >> 6);
    }
    font->public.ascent = ascent;
    font->public.descent = descent;
    font->public.height = height;

    if (fi->char_width)
	font->public.max_advance_width = fi->char_width;
    else
    {
	if (fi->transform)
	{
	    FT_Vector	vector;
	    vector.x = face->size->metrics.max_advance;
	    vector.y = 0;
	    FT_Vector_Transform (&vector, &fi->matrix);
	    font->public.max_advance_width = (int)(vector.x >> 6);
	}
	else
	    font->public.max_advance_width = (int)(face->size->metrics.max_advance >> 6);
    }
    font->public.charset = charset;
    font->public.pattern = pattern;

    /*
     * Management fields
     */
    font->ref = 1;

    font->next = info->fonts;
    info->fonts = &font->public;

    font->hash_next = *bucket;
    *bucket = &font->public;

    /*
     * Copy the info over
     */
    font->info = *fi;
    /*
     * reset the antialias field.  It can't
     * be set correctly until the font is opened,
     * which doesn't happen in XftFontInfoFill
     */
    font->info.antialias = antialias;

    /*
     * Set color value, which is only known once the
     * font was loaded
     */
    font->info.color = color;

    /*
     * bump XftFile reference count
     */
    font->info.file->ref++;

    /*
     * Per glyph information
     */
    font->glyphs = (XftGlyph **) (font + 1);
    memset (font->glyphs, '\0', (size_t)num_glyphs * sizeof (XftGlyph *));
    font->num_glyphs = num_glyphs;
    /*
     * Memory-usage tracking
     */
    font->newest = FT_UINT_MAX;
    font->total_inuse = 0;
    /*
     * Unicode hash table information
     */
    font->hash_table = (XftUcsHash *) (font->glyphs + font->num_glyphs);
    for (i = 0; (FcChar32) i < hash_value; i++)
    {
	font->hash_table[i].ucs4 = ((FcChar32) ~0);
	font->hash_table[i].glyph = 0;
    }
    font->hash_value = (int)hash_value;
    font->rehash_value = (int)rehash_value;
    /*
     * X specific fields
     */
    font->glyphset = 0;
    font->format = format;

    /*
     * Glyph memory management fields
     */
    font->glyph_memory     = 0;
    font->max_glyph_memory = (unsigned long)max_glyph_memory;
    font->track_mem_usage  = info->track_mem_usage;
    font->use_free_glyphs  = info->use_free_glyphs;
    font->sizeof_glyph     = (font->track_mem_usage
    			      ? sizeof(XftGlyphUsage)
			      : sizeof(XftGlyph));

    _XftUnlockFile (fi->file);

    return &font->public;

bail2:
    FcCharSetDestroy (charset);
bail1:
    _XftUnlockFile (fi->file);
bail0:
    return NULL;
}

_X_EXPORT XftFont *
XftFontOpenPattern (Display *dpy, FcPattern *pattern)
{
    XftFontInfo	    info;
    XftFont	    *font;

    if (!XftFontInfoFill (dpy, pattern, &info))
	return NULL;

    font = XftFontOpenInfo (dpy, pattern, &info);
    XftFontInfoEmpty (dpy, &info);
    return font;
}

_X_EXPORT XftFont *
XftFontCopy (Display *dpy _X_UNUSED, XftFont *public)
{
    XftFontInt	    *font = (XftFontInt *) public;

    font->ref++;
    return public;
}

static void
XftFontDestroy (Display *dpy, XftFont *public)
{
    XftDisplayInfo  *info = _XftDisplayInfoGet (dpy, False);
    XftFontInt	    *font = (XftFontInt *) public;
    FT_UInt	    i;

    /* note reduction in memory use */
    if (info)
	info->glyph_memory -= font->glyph_memory;
    /* Clean up the info */
    XftFontInfoEmpty (dpy, &font->info);
    /* Free the glyphset */
    if (font->glyphset)
	XRenderFreeGlyphSet (dpy, font->glyphset);
    /* Free the glyphs */
    for (i = 0; i < font->num_glyphs; i++)
    {
	XftGlyph	*xftg = font->glyphs[i];
	if (xftg)
	{
	    if (xftg->bitmap)
		free (xftg->bitmap);
	    free (xftg);
	}
    }

    /* Free the pattern and the charset */
    FcPatternDestroy (font->public.pattern);
    FcCharSetDestroy (font->public.charset);

    /* Finally, free the font structure */
    XftMemFree (XFT_MEM_FONT, (sizeof (XftFontInt) +
		(size_t)font->num_glyphs * sizeof (XftGlyph *) +
		(size_t)font->hash_value * sizeof (XftUcsHash)));
    free (font);
}

static XftFont *
XftFontFindNthUnref (XftDisplayInfo *info, int n)
{
    XftFont	*public;
    XftFontInt	*font;

    for (public = info->fonts; public; public = font->next)
    {
	font = (XftFontInt*) public;
	if (!font->ref && !n--)
	    break;
    }
    return public;
}

_X_HIDDEN void
XftFontManageMemory (Display *dpy)
{
    XftDisplayInfo  *info = _XftDisplayInfoGet (dpy, False);
    XftFont	    **prev;
    XftFont	    *public;
    XftFontInt	    *font;

    if (!info)
	return;
    while (info->num_unref_fonts > info->max_unref_fonts)
    {
	public = XftFontFindNthUnref (info, rand() % info->num_unref_fonts);
	font = (XftFontInt *) public;

	if (XftDebug () & XFT_DBG_CACHE)
	    printf ("freeing unreferenced font %s/%d size %dx%d\n",
		    font->info.file->file, font->info.file->id,
		    (int) font->info.xsize >> 6, (int) font->info.ysize >> 6);

	/* Unhook from display list */
	for (prev = &info->fonts; *prev; prev = &(*(XftFontInt **) prev)->next)
	{
	    if (*prev == public)
	    {
		*prev = font->next;
		break;
	    }
	}
	/* Unhook from hash list */
	for (prev = &info->fontHash[font->info.hash % XFT_NUM_FONT_HASH];
	     *prev;
	     prev = &(*(XftFontInt **) prev)->hash_next)
	{
	    if (*prev == public)
	    {
		*prev = font->hash_next;
		break;
	    }
	}
	/* Destroy the font */
	XftFontDestroy (dpy, public);
	--info->num_unref_fonts;
    }
}

_X_EXPORT void
XftFontClose (Display *dpy, XftFont *public)
{
    XftDisplayInfo  *info = _XftDisplayInfoGet (dpy, False);
    XftFontInt	    *font = (XftFontInt *) public;

    if (--font->ref != 0)
	return;

    if (info)
    {
	++info->num_unref_fonts;
	XftFontManageMemory (dpy);
    }
    else
    {
	XftFontDestroy (dpy, public);
    }
}

_X_EXPORT FcBool
XftInitFtLibrary (void)
{
    if (_XftFTlibrary)
	return FcTrue;
    if (FT_Init_FreeType (&_XftFTlibrary))
	return FcFalse;
    return FcTrue;
}
