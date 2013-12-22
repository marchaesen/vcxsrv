/*
 * Copyright © 2000 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "xftint.h"
#include FT_OUTLINE_H
#include FT_LCD_FILTER_H

#include FT_SYNTHESIS_H

/*
 * Validate the memory info for a font
 */

static void
_XftFontValidateMemory (Display *dpy, XftFont *public)
{
    XftFontInt	    *font = (XftFontInt *) public;
    unsigned long   glyph_memory;
    FT_UInt	    glyphindex;
    XftGlyph	    *xftg;

    glyph_memory = 0;
    for (glyphindex = 0; glyphindex < font->num_glyphs; glyphindex++)
    {
	xftg = font->glyphs[glyphindex];
	if (xftg)
	{
	    glyph_memory += xftg->glyph_memory;
	}
    }
    if (glyph_memory != font->glyph_memory)
	printf ("Font glyph cache incorrect has %ld bytes, should have %ld\n",
		font->glyph_memory, glyph_memory);
}

/* we sometimes need to convert the glyph bitmap in a FT_GlyphSlot
 * into a different format. For example, we want to convert a
 * FT_PIXEL_MODE_LCD or FT_PIXEL_MODE_LCD_V bitmap into a 32-bit
 * ARGB or ABGR bitmap.
 *
 * this function prepares a target descriptor for this operation.
 *
 * input :: target bitmap descriptor. The function will set its
 *          'width', 'rows' and 'pitch' fields, and only these
 *
 * slot  :: the glyph slot containing the source bitmap. this
 *          function assumes that slot->format == FT_GLYPH_FORMAT_BITMAP
 *
 * mode  :: the requested final rendering mode. supported values are
 *          MONO, NORMAL (i.e. gray), LCD and LCD_V
 *
 * the function returns the size in bytes of the corresponding buffer,
 * it's up to the caller to allocate the corresponding memory block
 * before calling _fill_xrender_bitmap
 *
 * it also returns -1 in case of error (e.g. incompatible arguments,
 * like trying to convert a gray bitmap into a monochrome one)
 */
static int
_compute_xrender_bitmap_size( FT_Bitmap*	target,
			      FT_GlyphSlot	slot,
			      FT_Render_Mode	mode )
{
    FT_Bitmap*	ftbit;
    int		width, height, pitch;

    if ( slot->format != FT_GLYPH_FORMAT_BITMAP )
	return -1;

    // compute the size of the final bitmap
    ftbit = &slot->bitmap;

    width = ftbit->width;
    height = ftbit->rows;
    pitch = (width+3) & ~3;

    switch ( ftbit->pixel_mode )
    {
    case FT_PIXEL_MODE_MONO:
	if ( mode == FT_RENDER_MODE_MONO )
	{
	    pitch = (((width+31) & ~31) >> 3);
	    break;
	}
	/* fall-through */

    case FT_PIXEL_MODE_GRAY:
	if ( mode == FT_RENDER_MODE_LCD ||
	     mode == FT_RENDER_MODE_LCD_V )
	{
	    /* each pixel is replicated into a 32-bit ARGB value */
	    pitch = width*4;
	}
	break;

    case FT_PIXEL_MODE_LCD:
	if ( mode != FT_RENDER_MODE_LCD )
	    return -1;

	/* horz pixel triplets are packed into 32-bit ARGB values */
	width /= 3;
	pitch = width*4;
	break;

    case FT_PIXEL_MODE_LCD_V:
	if ( mode != FT_RENDER_MODE_LCD_V )
	    return -1;

	/* vert pixel triplets are packed into 32-bit ARGB values */
	height /= 3;
	pitch = width*4;
	break;

    default:  /* unsupported source format */
	return -1;
    }

    target->width = width;
    target->rows = height;
    target->pitch = pitch;
    target->buffer = NULL;

    return pitch * height;
}

/* this functions converts the glyph bitmap found in a FT_GlyphSlot
 * into a different format (see _compute_xrender_bitmap_size)
 *
 * you should call this function after _compute_xrender_bitmap_size
 *
 * target :: target bitmap descriptor. Note that its 'buffer' pointer
 *           must point to memory allocated by the caller
 *
 * slot   :: the glyph slot containing the source bitmap
 *
 * mode   :: the requested final rendering mode
 *
 * bgr    :: boolean, set if BGR or VBGR pixel ordering is needed
 */
static void
_fill_xrender_bitmap( FT_Bitmap*	target,
		      FT_GlyphSlot	slot,
		      FT_Render_Mode	mode,
		      int		bgr )
{
    FT_Bitmap*   ftbit = &slot->bitmap;

    {
	unsigned char*	srcLine	= ftbit->buffer;
        unsigned char*	dstLine	= target->buffer;
        int		src_pitch = ftbit->pitch;
        int		width = target->width;
        int		height = target->rows;
        int		pitch = target->pitch;
        int		subpixel;
        int		h;

        subpixel = ( mode == FT_RENDER_MODE_LCD ||
		     mode == FT_RENDER_MODE_LCD_V );

	if ( src_pitch < 0 )
	    srcLine -= src_pitch*(ftbit->rows-1);

	switch ( ftbit->pixel_mode )
	{
	case FT_PIXEL_MODE_MONO:
	    if ( subpixel )  /* convert mono to ARGB32 values */
	    {
		for ( h = height; h > 0; h--, srcLine += src_pitch, dstLine += pitch )
		{
		    int x;

		    for ( x = 0; x < width; x++ )
		    {
			if ( srcLine[(x >> 3)] & (0x80 >> (x & 7)) )
			    ((unsigned int*)dstLine)[x] = 0xffffffffU;
		    }
		}
	    }
	    else if ( mode == FT_RENDER_MODE_NORMAL )  /* convert mono to 8-bit gray */
	    {
		for ( h = height; h > 0; h--, srcLine += src_pitch, dstLine += pitch )
		{
		    int x;

		    for ( x = 0; x < width; x++ )
		    {
			if ( srcLine[(x >> 3)] & (0x80 >> (x & 7)) )
			    dstLine[x] = 0xff;
		    }
		}
	    }
	    else  /* copy mono to mono */
	    {
		int bytes = (width+7) >> 3;

		for ( h = height; h > 0; h--, srcLine += src_pitch, dstLine += pitch )
		    memcpy( dstLine, srcLine, bytes );
	    }
	    break;

	case FT_PIXEL_MODE_GRAY:
	    if ( subpixel )  /* convert gray to ARGB32 values */
	    {
		for ( h = height; h > 0; h--, srcLine += src_pitch, dstLine += pitch )
		{
		    int		   x;
		    unsigned int*  dst = (unsigned int*)dstLine;

		    for ( x = 0; x < width; x++ )
		    {
			unsigned int pix = srcLine[x];

			pix |= (pix << 8);
			pix |= (pix << 16);

			dst[x] = pix;
		    }
		}
	    }
	    else  /* copy gray into gray */
	    {
		for ( h = height; h > 0; h--, srcLine += src_pitch, dstLine += pitch )
		    memcpy( dstLine, srcLine, width );
	    }
	    break;

	case FT_PIXEL_MODE_LCD:
	    if ( !bgr )
	    {
		/* convert horizontal RGB into ARGB32 */
		for ( h = height; h > 0; h--, srcLine += src_pitch, dstLine += pitch )
		{
		    int		   x;
		    unsigned char* src = srcLine;
		    unsigned int*  dst = (unsigned int*)dstLine;

		    for ( x = 0; x < width; x++, src += 3 )
		    {
			unsigned int pix;

			pix = ((unsigned int)src[0] << 16) |
			      ((unsigned int)src[1] <<  8) |
			      ((unsigned int)src[2]      ) |
			      ((unsigned int)src[1] << 24) ;

			dst[x] = pix;
		    }
		}
	    }
	    else
	    {
		/* convert horizontal BGR into ARGB32 */
		for ( h = height; h > 0; h--, srcLine += src_pitch, dstLine += pitch )
		{
		    int		   x;
		    unsigned char* src = srcLine;
		    unsigned int*  dst = (unsigned int*)dstLine;

		    for ( x = 0; x < width; x++, src += 3 )
		    {
			unsigned int pix;

			pix = ((unsigned int)src[2] << 16) |
			      ((unsigned int)src[1] <<  8) |
			      ((unsigned int)src[0]      ) |
			      ((unsigned int)src[1] << 24) ;

			dst[x] = pix;
		    }
		}
	    }
	    break;

	default:  /* FT_PIXEL_MODE_LCD_V */
	    /* convert vertical RGB into ARGB32 */
	    if ( !bgr )
	    {
		for ( h = height; h > 0; h--, srcLine += 3*src_pitch, dstLine += pitch )
		{
		    int		   x;
		    unsigned char* src = srcLine;
		    unsigned int*  dst = (unsigned int*)dstLine;

		    for ( x = 0; x < width; x++, src += 1 )
		    {
			unsigned int  pix;

			pix = ((unsigned int)src[0]           << 16) |
			      ((unsigned int)src[src_pitch]   <<  8) |
			      ((unsigned int)src[src_pitch*2]      ) |
			      ((unsigned int)src[src_pitch]   << 24) ;

			dst[x] = pix;
		    }
		}
	    }
	    else
	    {
	    for ( h = height; h > 0; h--, srcLine += 3*src_pitch, dstLine += pitch )
		{
		    int		   x;
		    unsigned char* src = srcLine;
		    unsigned int*  dst = (unsigned int*)dstLine;

		    for ( x = 0; x < width; x++, src += 1 )
		    {
			unsigned int  pix;

			pix = ((unsigned int)src[src_pitch*2] << 16) |
			      ((unsigned int)src[src_pitch]   <<  8) |
			      ((unsigned int)src[0]                ) |
			      ((unsigned int)src[src_pitch]   << 24) ;

			dst[x] = pix;
		    }
		}
	    }
	}
    }
}

_X_EXPORT void
XftFontLoadGlyphs (Display	    *dpy,
		   XftFont	    *pub,
		   FcBool	    need_bitmaps,
		   _Xconst FT_UInt  *glyphs,
		   int		    nglyph)
{
    XftDisplayInfo  *info = _XftDisplayInfoGet (dpy, True);
    XftFontInt	    *font = (XftFontInt *) pub;
    FT_Error	    error;
    FT_UInt	    glyphindex;
    FT_GlyphSlot    glyphslot;
    XftGlyph	    *xftg;
    Glyph	    glyph;
    unsigned char   bufLocal[4096];
    unsigned char   *bufBitmap = bufLocal;
    int		    bufSize = sizeof (bufLocal);
    int		    size;
    int		    width;
    int		    height;
    int		    left, right, top, bottom;
    FT_Bitmap*	    ftbit;
    FT_Bitmap	    local;
    FT_Vector	    vector;
    FT_Face	    face;
    FT_Render_Mode  mode = FT_RENDER_MODE_MONO;

    if (!info)
	return;

    face = XftLockFace (&font->public);

    if (!face)
	return;

    if (font->info.antialias)
    {
	switch (font->info.rgba) {
	case FC_RGBA_RGB:
	case FC_RGBA_BGR:
	    mode = FT_RENDER_MODE_LCD;
	    break;
	case FC_RGBA_VRGB:
	case FC_RGBA_VBGR:
	    mode = FT_RENDER_MODE_LCD_V;
	    break;
	default:
	    mode = FT_RENDER_MODE_NORMAL;
	}
    }

    while (nglyph--)
    {
	glyphindex = *glyphs++;
	xftg = font->glyphs[glyphindex];
	if (!xftg)
	    continue;

	if (XftDebug() & XFT_DBG_CACHE)
	    _XftFontValidateMemory (dpy, pub);
	/*
	 * Check to see if this glyph has just been loaded,
	 * this happens when drawing the same glyph twice
	 * in a single string
	 */
	if (xftg->glyph_memory)
	    continue;

	FT_Library_SetLcdFilter( _XftFTlibrary, font->info.lcd_filter);

	error = FT_Load_Glyph (face, glyphindex, font->info.load_flags);
	if (error)
	{
	    /*
	     * If anti-aliasing or transforming glyphs and
	     * no outline version exists, fallback to the
	     * bitmap and let things look bad instead of
	     * missing the glyph
	     */
	    if (font->info.load_flags & FT_LOAD_NO_BITMAP)
		error = FT_Load_Glyph (face, glyphindex,
				       font->info.load_flags & ~FT_LOAD_NO_BITMAP);
	    if (error)
		continue;
	}

#define FLOOR(x)    ((x) & -64)
#define CEIL(x)	    (((x)+63) & -64)
#define TRUNC(x)    ((x) >> 6)
#define ROUND(x)    (((x)+32) & -64)

	glyphslot = face->glyph;

	/*
	 * Embolden if required
	 */
	if (font->info.embolden) FT_GlyphSlot_Embolden(glyphslot);

	/*
	 * Compute glyph metrics from FreeType information
	 */
	if(font->info.transform && glyphslot->format != FT_GLYPH_FORMAT_BITMAP)
	{
	    /*
	     * calculate the true width by transforming all four corners.
	     */
	    int xc, yc;
	    left = right = top = bottom = 0;
	    for(xc = 0; xc <= 1; xc ++) {
		for(yc = 0; yc <= 1; yc++) {
		    vector.x = glyphslot->metrics.horiBearingX + xc * glyphslot->metrics.width;
		    vector.y = glyphslot->metrics.horiBearingY - yc * glyphslot->metrics.height;
		    FT_Vector_Transform(&vector, &font->info.matrix);
		    if (XftDebug() & XFT_DBG_GLYPH)
			printf("Trans %d %d: %d %d\n", (int) xc, (int) yc,
			       (int) vector.x, (int) vector.y);
		    if(xc == 0 && yc == 0) {
			left = right = vector.x;
			top = bottom = vector.y;
		    } else {
			if(left > vector.x) left = vector.x;
			if(right < vector.x) right = vector.x;
			if(bottom > vector.y) bottom = vector.y;
			if(top < vector.y) top = vector.y;
		    }

		}
	    }
	    left = FLOOR(left);
	    right = CEIL(right);
	    bottom = FLOOR(bottom);
	    top = CEIL(top);

	} else {
	    left  = FLOOR( glyphslot->metrics.horiBearingX );
	    right = CEIL( glyphslot->metrics.horiBearingX + glyphslot->metrics.width );

	    top    = CEIL( glyphslot->metrics.horiBearingY );
	    bottom = FLOOR( glyphslot->metrics.horiBearingY - glyphslot->metrics.height );
	}

	width = TRUNC(right - left);
	height = TRUNC( top - bottom );

	/*
	 * Clip charcell glyphs to the bounding box
	 * XXX transformed?
	 */
	if (font->info.spacing >= FC_CHARCELL && !font->info.transform)
	{
	    if (font->info.load_flags & FT_LOAD_VERTICAL_LAYOUT)
	    {
		if (TRUNC(bottom) > font->public.max_advance_width)
		{
		    int adjust;

		    adjust = bottom - (font->public.max_advance_width << 6);
		    if (adjust > top)
			adjust = top;
		    top -= adjust;
		    bottom -= adjust;
		    height = font->public.max_advance_width;
		}
	    }
	    else
	    {
		if (TRUNC(right) > font->public.max_advance_width)
		{
		    int adjust;

		    adjust = right - (font->public.max_advance_width << 6);
		    if (adjust > left)
			adjust = left;
		    left -= adjust;
		    right -= adjust;
		    width = font->public.max_advance_width;
		}
	    }
	}

	if ( glyphslot->format != FT_GLYPH_FORMAT_BITMAP )
	{
	    error = FT_Render_Glyph( face->glyph, mode );
	    if (error)
		continue;
	}

	FT_Library_SetLcdFilter( _XftFTlibrary, FT_LCD_FILTER_NONE );

	if (font->info.spacing >= FC_MONO)
	{
	    if (font->info.transform)
	    {
		if (font->info.load_flags & FT_LOAD_VERTICAL_LAYOUT)
		{
		    vector.x = 0;
		    vector.y = -face->size->metrics.max_advance;
		}
		else
		{
		    vector.x = face->size->metrics.max_advance;
		    vector.y = 0;
		}
		FT_Vector_Transform (&vector, &font->info.matrix);
		xftg->metrics.xOff = vector.x >> 6;
		xftg->metrics.yOff = -(vector.y >> 6);
	    }
	    else
	    {
		if (font->info.load_flags & FT_LOAD_VERTICAL_LAYOUT)
		{
		    xftg->metrics.xOff = 0;
		    xftg->metrics.yOff = -font->public.max_advance_width;
		}
		else
		{
		    xftg->metrics.xOff = font->public.max_advance_width;
		    xftg->metrics.yOff = 0;
		}
	    }
	}
	else
	{
	    xftg->metrics.xOff = TRUNC(ROUND(glyphslot->advance.x));
	    xftg->metrics.yOff = -TRUNC(ROUND(glyphslot->advance.y));
	}

	// compute the size of the final bitmap
	ftbit = &glyphslot->bitmap;

	width = ftbit->width;
	height = ftbit->rows;

	if (XftDebug() & XFT_DBG_GLYPH)
	{
	    printf ("glyph %d:\n", (int) glyphindex);
	    printf (" xywh (%d %d %d %d), trans (%d %d %d %d) wh (%d %d)\n",
		    (int) glyphslot->metrics.horiBearingX,
		    (int) glyphslot->metrics.horiBearingY,
		    (int) glyphslot->metrics.width,
		    (int) glyphslot->metrics.height,
		    left, right, top, bottom,
		    width, height);
	    if (XftDebug() & XFT_DBG_GLYPHV)
	    {
		int		x, y;
		unsigned char	*line;

		line = ftbit->buffer;
		if (ftbit->pitch < 0)
		    line -= ftbit->pitch*(height-1);

		for (y = 0; y < height; y++)
		{
		    if (font->info.antialias)
		    {
			static const char    den[] = { " .:;=+*#" };
			for (x = 0; x < width; x++)
			    printf ("%c", den[line[x] >> 5]);
		    }
		    else
		    {
			for (x = 0; x < width * 8; x++)
			{
			    printf ("%c", line[x>>3] & (1 << (x & 7)) ? '#' : ' ');
			}
		    }
		    printf ("|\n");
		    line += ftbit->pitch;
		}
		printf ("\n");
	    }
	}

	size = _compute_xrender_bitmap_size( &local, glyphslot, mode );
	if ( size < 0 )
	    continue;

	xftg->metrics.width  = local.width;
	xftg->metrics.height = local.rows;
	xftg->metrics.x      = - glyphslot->bitmap_left;
	xftg->metrics.y      =   glyphslot->bitmap_top;

	/*
	 * If the glyph is relatively large (> 1% of server memory),
	 * don't send it until necessary.
	 */
	if (!need_bitmaps && size > info->max_glyph_memory / 100)
	    continue;

	/*
	 * Make sure there is enough buffer space for the glyph.
	 */
	if (size > bufSize)
	{
	    if (bufBitmap != bufLocal)
		free (bufBitmap);
	    bufBitmap = (unsigned char *) malloc (size);
	    if (!bufBitmap)
		continue;
	    bufSize = size;
	}
	memset (bufBitmap, 0, size);

	local.buffer = bufBitmap;

	_fill_xrender_bitmap( &local, glyphslot, mode,
			      (font->info.rgba == FC_RGBA_BGR ||
			       font->info.rgba == FC_RGBA_VBGR ) );

	/*
	 * Copy or convert into local buffer.
	 */

	/*
	 * Use the glyph index as the wire encoding; it
	 * might be more efficient for some locales to map
	 * these by first usage to smaller values, but that
	 * would require persistently storing the map when
	 * glyphs were freed.
	 */
	glyph = (Glyph) glyphindex;

	xftg->glyph_memory = size + sizeof (XftGlyph);
	if (font->format)
	{
	    if (!font->glyphset)
		font->glyphset = XRenderCreateGlyphSet (dpy, font->format);
	    if ( mode == FT_RENDER_MODE_MONO )
	    {
		/* swap bits in each byte */
		if (BitmapBitOrder (dpy) != MSBFirst)
		{
		    unsigned char   *line = (unsigned char*)bufBitmap;
		    int		    i = size;

		    while (i--)
		    {
			int c = *line;
			c = ((c << 1) & 0xaa) | ((c >> 1) & 0x55);
			c = ((c << 2) & 0xcc) | ((c >> 2) & 0x33);
			c = ((c << 4) & 0xf0) | ((c >> 4) & 0x0f);
			*line++ = c;
		    }
		}
	    }
	    else if ( mode != FT_RENDER_MODE_NORMAL )
	    {
		/* invert ARGB <=> BGRA */
		if (ImageByteOrder (dpy) != XftNativeByteOrder ())
		    XftSwapCARD32 ((CARD32 *) bufBitmap, size >> 2);
	    }
	    XRenderAddGlyphs (dpy, font->glyphset, &glyph,
			      &xftg->metrics, 1,
			      (char *) bufBitmap, size);
	}
	else
	{
	    if (size)
	    {
		xftg->bitmap = malloc (size);
		if (xftg->bitmap)
		    memcpy (xftg->bitmap, bufBitmap, size);
	    }
	    else
		xftg->bitmap = NULL;
	}

	font->glyph_memory += xftg->glyph_memory;
	info->glyph_memory += xftg->glyph_memory;
	if (XftDebug() & XFT_DBG_CACHE)
	    _XftFontValidateMemory (dpy, pub);
	if (XftDebug() & XFT_DBG_CACHEV)
	    printf ("Caching glyph 0x%x size %ld\n", glyphindex,
		    xftg->glyph_memory);
    }
    if (bufBitmap != bufLocal)
	free (bufBitmap);
    XftUnlockFace (&font->public);
}

_X_EXPORT void
XftFontUnloadGlyphs (Display		*dpy,
		     XftFont		*pub,
		     _Xconst FT_UInt	*glyphs,
		     int		nglyph)
{
    XftDisplayInfo  *info = _XftDisplayInfoGet (dpy, False);
    XftFontInt	    *font = (XftFontInt *) pub;
    XftGlyph	    *xftg;
    FT_UInt	    glyphindex;
    Glyph	    glyphBuf[1024];
    int		    nused;

    nused = 0;
    while (nglyph--)
    {
	glyphindex = *glyphs++;
	xftg = font->glyphs[glyphindex];
	if (!xftg)
	    continue;
	if (xftg->glyph_memory)
	{
	    if (font->format)
	    {
		if (font->glyphset)
		{
		    glyphBuf[nused++] = (Glyph) glyphindex;
		    if (nused == sizeof (glyphBuf) / sizeof (glyphBuf[0]))
		    {
			XRenderFreeGlyphs (dpy, font->glyphset, glyphBuf, nused);
			nused = 0;
		    }
		}
	    }
	    else
	    {
		if (xftg->bitmap)
		    free (xftg->bitmap);
	    }
	    font->glyph_memory -= xftg->glyph_memory;
	    if (info)
		info->glyph_memory -= xftg->glyph_memory;
	}
	free (xftg);
	XftMemFree (XFT_MEM_GLYPH, sizeof (XftGlyph));
	font->glyphs[glyphindex] = NULL;
    }
    if (font->glyphset && nused)
	XRenderFreeGlyphs (dpy, font->glyphset, glyphBuf, nused);
}

_X_EXPORT FcBool
XftFontCheckGlyph (Display	*dpy,
		   XftFont	*pub,
		   FcBool	need_bitmaps,
		   FT_UInt	glyph,
		   FT_UInt	*missing,
		   int		*nmissing)
{
    XftFontInt	    *font = (XftFontInt *) pub;
    XftGlyph	    *xftg;
    int		    n;

    if (glyph >= font->num_glyphs)
	return FcFalse;
    xftg = font->glyphs[glyph];
    if (!xftg || (need_bitmaps && !xftg->glyph_memory))
    {
	if (!xftg)
	{
	    xftg = (XftGlyph *) malloc (sizeof (XftGlyph));
	    if (!xftg)
		return FcFalse;
	    XftMemAlloc (XFT_MEM_GLYPH, sizeof (XftGlyph));
	    xftg->bitmap = NULL;
	    xftg->glyph_memory = 0;
	    font->glyphs[glyph] = xftg;
	}
	n = *nmissing;
	missing[n++] = glyph;
	if (n == XFT_NMISSING)
	{
	    XftFontLoadGlyphs (dpy, pub, need_bitmaps, missing, n);
	    n = 0;
	}
	*nmissing = n;
	return FcTrue;
    }
    else
	return FcFalse;
}

_X_EXPORT FcBool
XftCharExists (Display	    *dpy,
	       XftFont	    *pub,
	       FcChar32    ucs4)
{
    if (pub->charset)
	return FcCharSetHasChar (pub->charset, ucs4);
    return FcFalse;
}

#define Missing	    ((FT_UInt) ~0)

_X_EXPORT FT_UInt
XftCharIndex (Display	    *dpy,
	      XftFont	    *pub,
	      FcChar32	    ucs4)
{
    XftFontInt	*font = (XftFontInt *) pub;
    FcChar32	ent, offset;
    FT_Face	face;

    if (!font->hash_value)
	return 0;

    ent = ucs4 % font->hash_value;
    offset = 0;
    while (font->hash_table[ent].ucs4 != ucs4)
    {
	if (font->hash_table[ent].ucs4 == (FcChar32) ~0)
	{
	    if (!XftCharExists (dpy, pub, ucs4))
		return 0;
	    face  = XftLockFace (pub);
	    if (!face)
		return 0;
	    font->hash_table[ent].ucs4 = ucs4;
	    font->hash_table[ent].glyph = FcFreeTypeCharIndex (face, ucs4);
	    XftUnlockFace (pub);
	    break;
	}
	if (!offset)
	{
	    offset = ucs4 % font->rehash_value;
	    if (!offset)
		offset = 1;
	}
	ent = ent + offset;
	if (ent >= font->hash_value)
	    ent -= font->hash_value;
    }
    return font->hash_table[ent].glyph;
}

/*
 * Pick a random glyph from the font and remove it from the cache
 */
_X_HIDDEN void
_XftFontUncacheGlyph (Display *dpy, XftFont *pub)
{
    XftFontInt	    *font = (XftFontInt *) pub;
    unsigned long   glyph_memory;
    FT_UInt	    glyphindex;
    XftGlyph	    *xftg;

    if (!font->glyph_memory)
	return;
    if (font->use_free_glyphs)
    {
	glyph_memory = rand() % font->glyph_memory;
    }
    else
    {
	if (font->glyphset)
	{
	    XRenderFreeGlyphSet (dpy, font->glyphset);
	    font->glyphset = 0;
	}
	glyph_memory = 0;
    }

    if (XftDebug() & XFT_DBG_CACHE)
	_XftFontValidateMemory (dpy, pub);
    for (glyphindex = 0; glyphindex < font->num_glyphs; glyphindex++)
    {
	xftg = font->glyphs[glyphindex];
	if (xftg)
	{
	    if (xftg->glyph_memory > glyph_memory)
	    {
		if (XftDebug() & XFT_DBG_CACHEV)
		    printf ("Uncaching glyph 0x%x size %ld\n",
			    glyphindex, xftg->glyph_memory);
		XftFontUnloadGlyphs (dpy, pub, &glyphindex, 1);
		if (!font->use_free_glyphs)
		    continue;
		break;
	    }
	    glyph_memory -= xftg->glyph_memory;
	}
    }
    if (XftDebug() & XFT_DBG_CACHE)
	_XftFontValidateMemory (dpy, pub);
}

_X_HIDDEN void
_XftFontManageMemory (Display *dpy, XftFont *pub)
{
    XftFontInt	*font = (XftFontInt *) pub;

    if (font->max_glyph_memory)
    {
	if (XftDebug() & XFT_DBG_CACHE)
	{
	    if (font->glyph_memory > font->max_glyph_memory)
		printf ("Reduce memory for font 0x%lx from %ld to %ld\n",
			font->glyphset ? font->glyphset : (unsigned long) font,
			font->glyph_memory, font->max_glyph_memory);
	}
	while (font->glyph_memory > font->max_glyph_memory)
	    _XftFontUncacheGlyph (dpy, pub);
    }
    _XftDisplayManageMemory (dpy);
}
