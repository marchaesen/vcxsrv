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
#include FT_OUTLINE_H
#include FT_LCD_FILTER_H

#include FT_SYNTHESIS_H

#include FT_GLYPH_H

typedef double m3x3[3][3];

static void
m3x3_uniform(m3x3 m)
{
    m[0][0] = m[1][1] = m[2][2] = 1.0;
    m[0][1] = m[1][0] = m[0][2] = m[1][2] = m[2][0] = m[2][1] = 0;
}

static void
m3x3_transform(FT_Vector *v, m3x3 m)
{
    double x, y;

    x = (double)v->x;
    y = (double)v->y;
    v->x = (FT_Pos)(x * m[0][0] + y * m[0][1] + m[0][2] + 0.5);
    v->y = (FT_Pos)(x * m[1][0] + y * m[1][1] + m[1][2] + 0.5);
}

static void
m3x3_invert(m3x3 m, m3x3 mi)
{
    double det;

    det  = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]);
    det -= m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]);
    det += m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    det  = 1.0 / det;
    mi[0][0] = det * (m[1][1] * m[2][2] - m[1][2] * m[2][1]);
    mi[1][0] = det * (m[1][2] * m[2][0] - m[1][0] * m[2][2]);
    mi[2][0] = det * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    mi[0][1] = det * (m[0][2] * m[2][1] - m[0][1] * m[2][2]);
    mi[1][1] = det * (m[0][0] * m[2][2] - m[0][2] * m[2][0]);
    mi[2][1] = det * (m[0][1] * m[2][0] - m[0][0] * m[2][1]);
    mi[0][2] = det * (m[0][1] * m[1][2] - m[0][2] * m[1][1]);
    mi[1][2] = det * (m[0][2] * m[1][0] - m[0][0] * m[1][2]);
    mi[2][2] = det * (m[0][0] * m[1][1] - m[0][1] * m[1][0]);
}

/*
 * Validate the memory info for a font
 */

static void
_XftFontValidateMemory (Display *dpy _X_UNUSED, XftFont *public)
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
	printf ("Font glyph cache incorrect has %lu bytes, should have %lu\n",
		font->glyph_memory, glyph_memory);
}

/*
 * Validate the glyph usage-links for a font.
 */
static void
_XftValidateGlyphUsage(XftFontInt *font)
{
    if (font->newest != FT_UINT_MAX) {
	FT_UInt forward;
	FT_UInt reverse;
	FT_UInt next;
	XftGlyphUsage *x1st = (XftGlyphUsage *) font->glyphs[font->newest];
	XftGlyphUsage *xuse = x1st;
	for (forward = 1,
	     next = x1st->newer;
	     xuse != NULL &&
	     next != font->newest;
	     next = xuse->newer) {
	    if (next >= font->num_glyphs) {
		printf("Xft: out of range; %d\n", next);
		break;
	    }
	    if (++forward > font->total_inuse) {
		printf("Xft: too many in-use glyphs (%d vs %d)\n",
		       forward, font->total_inuse);
		if (forward > font->total_inuse + 10)
		    break;
	    }
	    xuse = (XftGlyphUsage *) font->glyphs[next];
	}
	if (forward < font->total_inuse) {
	    printf("Xft: too few in-use glyphs (%u vs %d)\n",
		   forward, font->total_inuse);
	}
	for (reverse = 1,
	     next = x1st->older;
	     xuse != NULL &&
	     next != font->newest;
	     next = xuse->older) {
	    if (next >= font->num_glyphs) {
		printf("Xft out of range; %d\n", next);
		break;
	    }
	    if (++reverse > font->total_inuse) {
		printf("Xft: too many in-use glyphs (%d vs %d)\n",
		       reverse, font->total_inuse);
		if (reverse > font->total_inuse + 10)
		    break;
	    }
	    xuse = (XftGlyphUsage *) font->glyphs[next];
	}
	if (reverse < font->total_inuse) {
	    printf("Xft: too few in-use glyphs (%u vs %d)\n",
		   reverse, font->total_inuse);
	}
	if (forward != reverse) {
	    printf("Xft: forward %d vs reverse %d\n",
		   forward, reverse);
	    exit(1);
	}
    }
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
			      FT_Render_Mode	mode,
			      FT_Matrix*	matrix,
			      m3x3		m )
{
    FT_Bitmap*	ftbit;
    int		width, height, pitch;

    if ( slot->format != FT_GLYPH_FORMAT_BITMAP )
	return -1;

    /* compute the size of the final bitmap */
    ftbit = &slot->bitmap;

    width = (int)ftbit->width;
    height = (int)ftbit->rows;

    if ( matrix && mode == FT_RENDER_MODE_NORMAL )
    {
	FT_Matrix mirror, inverse;
	FT_Vector vector;
	int xc, yc;
	int left, right, top, bottom;

	left = right = top = bottom = 0;
	for (xc = 0; xc <= 1; xc++) {
	    for (yc = 0; yc <= 1; yc++) {
		vector.x = xc * width;
		vector.y = yc * height;
		FT_Vector_Transform(&vector, matrix);
		if (xc == 0 && yc == 0) {
		    left = right = (int)vector.x;
		    top = bottom = (int)vector.y;
		} else {
		    if (left   > vector.x) left   = (int)vector.x;
		    if (right  < vector.x) right  = (int)vector.x;
		    if (bottom > vector.y) bottom = (int)vector.y;
		    if (top    < vector.y) top    = (int)vector.y;
		}
	    }
	}
	width = (int)(right - left);
	height = (int)(top - bottom);

	mirror.xx = + 0x10000;
	mirror.yy = - 0x10000;
	mirror.xy = mirror.yx = 0;
	inverse = *matrix;
	FT_Matrix_Multiply(&mirror, &inverse);
	FT_Matrix_Invert(&inverse);
	FT_Matrix_Multiply(&mirror, &inverse);

	vector.x = vector.y = 0;
	FT_Vector_Transform(&vector, &inverse);
	left = (int)vector.x;
	bottom = (int)vector.y;
	vector.x = width;
	vector.y = height;
	FT_Vector_Transform(&vector, &inverse);
	right = (int)vector.x;
	top = (int)vector.y;
	left = (right - left) - (int)ftbit->width;
	bottom = (top - bottom) - (int)ftbit->rows;

	m[0][0] = (double)inverse.xx / 0x10000;
	m[0][1] = (double)inverse.xy / 0x10000;
	m[1][0] = (double)inverse.yx / 0x10000;
	m[1][1] = (double)inverse.yy / 0x10000;
	m[0][2] = (double)-left / 2;
	m[1][2] = (double)-bottom / 2;
	m[2][0] = m[2][1] = 0.0;
	m[2][2] = 1.0;
    }
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

    case FT_PIXEL_MODE_BGRA:
	pitch = width * 4;
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

    target->width = (unsigned)width;
    target->rows = (unsigned)height;
    target->pitch = pitch;
    target->buffer = NULL;

    return pitch * height;
}

/* this functions converts the glyph bitmap found in a FT_GlyphSlot
 * into a different format while scaling by applying the given matrix
 * (see _compute_xrender_bitmap_size)
 *
 * you should call this function after _compute_xrender_bitmap_size
 *
 * target :: target bitmap descriptor. Note that its 'buffer' pointer
 *           must point to memory allocated by the caller
 *
 * source :: the source bitmap descriptor
 *
 * matrix :: the scaling matrix to apply
 */
static void
_scaled_fill_xrender_bitmap( FT_Bitmap*	target,
			     FT_Bitmap* source,
			     m3x3 m )
{
    unsigned char*	src_buf	  = source->buffer;
    unsigned char*	dst_line  = target->buffer;
    int			src_pitch = source->pitch;
    int			width     = (int) target->width;
    int			height    = (int) target->rows;
    int			pitch     = target->pitch;
    int			i, x, y;
    FT_Vector		vector, vector0;
    int			sampling_width;
    int			sampling_height;
    int			sample_count;

    if ( src_pitch < 0 )
	src_buf -= ((unsigned) src_pitch * (source->rows - 1));

    /* compute how many source pixels a target pixel spans */
    vector.x = 1;
    vector.y = 1;
    m3x3_transform(&vector, m);
    vector0.x = 0;
    vector0.y = 0;
    m3x3_transform(&vector0, m);
    sampling_width = (int) ((vector.x - vector0.x) / 2);
    sampling_height = (int) ((vector.y - vector0.y) / 2);
    if (sampling_width < 0) sampling_width = -sampling_width;
    if (sampling_height < 0) sampling_height = -sampling_height;
    sample_count = (2 * sampling_width + 1) * (2 * sampling_height + 1);

    for	( y = height; y > 0; y--, dst_line += pitch )
    {
	for ( x	= 0; x < width; x++ )
	{
	    unsigned char* src;

	    /* compute target pixel location in source space */
	    vector.x = x;
	    vector.y = height - y;
	    m3x3_transform(&vector, m);

	    if (source->pixel_mode == FT_PIXEL_MODE_BGRA)
	    {
		if (vector.x < -sampling_width
		 || vector.x > (source->width + (unsigned) sampling_width))
		    continue;
		if (vector.y < -sampling_height
		 || vector.y > (source->rows + (unsigned) sampling_height))
		    continue;
	    }
	    else
	    {
		if (vector.x < 0 || vector.x >= source->width)
		    continue;
		if (vector.y < 0 || vector.y >= source->rows)
		    continue;
	    }

	    switch ( source->pixel_mode )
	    {
	    case FT_PIXEL_MODE_MONO: /* convert mono to 8-bit gray, scale using nearest pixel */
		src = src_buf + (vector.y * src_pitch);
		if ( src[(vector.x >> 3)] & (0x80 >> (vector.x & 7)) )
		    dst_line[x] = 0xff;
		break;

	    case FT_PIXEL_MODE_GRAY: /* scale using nearest pixel */
		src = src_buf + (vector.y * src_pitch);
		dst_line[x] = src[vector.x];
		break;

	    case FT_PIXEL_MODE_BGRA: /* scale by averaging all relevant source pixels, keep BGRA format */
	    {
		int sample_x, sample_y;
		int bgra[4] = { 0, 0, 0, 0 };

		for (sample_y = - sampling_height; sample_y < sampling_height + 1; ++sample_y)
		{
		    int src_y = (int) (vector.y + sample_y);

		    if (src_y < 0 || (FT_Pos) src_y >= source->rows)
			continue;
		    src = src_buf + (src_y * src_pitch);
		    for (sample_x = - sampling_width; sample_x < sampling_width + 1; ++sample_x)
		    {
			int src_x = (int) (vector.x + sample_x);

			if (src_x < 0 || (FT_Pos) src_x >= source->width)
			    continue;
			for (i = 0; i < 4; ++i)
			    bgra[i] += src[src_x * 4 + i];
		    }
		}

		for (i = 0; i < 4; ++i)
		    dst_line[4 * x + i] = (unsigned char) (bgra[i] / sample_count);
		break;
	    }
	    }
	}
    }
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
	int		width = (int)target->width;
	int		height = (int)target->rows;
	int		pitch = target->pitch;
	int		subpixel;
	int		h;

	subpixel = ( mode == FT_RENDER_MODE_LCD ||
		     mode == FT_RENDER_MODE_LCD_V );

	if ( src_pitch < 0 )
	    srcLine -= ((unsigned)src_pitch * (ftbit->rows-1));

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
		    memcpy( dstLine, srcLine, (size_t)bytes );
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
		    memcpy( dstLine, srcLine, (size_t)width );
	    }
	    break;

	case FT_PIXEL_MODE_BGRA: /* Preserve BGRA format */
	    for ( h = height; h > 0; h--, srcLine += src_pitch, dstLine += pitch )
		memcpy( dstLine, srcLine, (size_t) width * 4 );
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
    m3x3	    m;
    FT_Face	    face;
    FT_Render_Mode  mode = FT_RENDER_MODE_MONO;
    FcBool	    transform;
    FcBool	    glyph_transform;

    if (!info)
	return;

    face = XftLockFace (&font->public);

    if (!face)
	return;

    if (font->info.color)
	mode = FT_RENDER_MODE_NORMAL;
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

    transform = font->info.transform && mode != FT_RENDER_MODE_MONO;

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
	if (transform)
	{
	    /*
	     * calculate the true width by transforming all four corners.
	     */
	    int xc, yc;
	    left = right = top = bottom = 0;
	    for (xc = 0; xc <= 1; xc++) {
		for (yc = 0; yc <= 1; yc++) {
		    vector.x = glyphslot->metrics.horiBearingX + xc * glyphslot->metrics.width;
		    vector.y = glyphslot->metrics.horiBearingY - yc * glyphslot->metrics.height;
		    FT_Vector_Transform(&vector, &font->info.matrix);
		    if (XftDebug() & XFT_DBG_GLYPH)
			printf("Trans %d %d: %d %d\n", (int) xc, (int) yc,
			       (int) vector.x, (int) vector.y);
		    if (xc == 0 && yc == 0) {
			left = right = (int)vector.x;
			top = bottom = (int)vector.y;
		    } else {
			if (left   > vector.x) left   = (int)vector.x;
			if (right  < vector.x) right  = (int)vector.x;
			if (bottom > vector.y) bottom = (int)vector.y;
			if (top	   < vector.y) top    = (int)vector.y;
		    }

		}
	    }
	    left   = (int)FLOOR(left);
	    right  = (int)CEIL(right);
	    bottom = (int)FLOOR(bottom);
	    top	   = (int)CEIL(top);

	} else {
	    left   = (int)FLOOR( glyphslot->metrics.horiBearingX );
	    right  = (int)CEIL( glyphslot->metrics.horiBearingX + glyphslot->metrics.width );

	    top    = (int)CEIL( glyphslot->metrics.horiBearingY );
	    bottom = (int)FLOOR( glyphslot->metrics.horiBearingY - glyphslot->metrics.height );
	}

	/*
	 * Clip charcell glyphs to the bounding box
	 * XXX transformed?
	 */
	if (font->info.spacing >= FC_CHARCELL && !transform)
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
		}
	    }
	}

	glyph_transform = transform;
	if ( glyphslot->format != FT_GLYPH_FORMAT_BITMAP )
	{
	    error = FT_Render_Glyph( face->glyph, mode );
	    if (error)
		continue;
	    glyph_transform = False;
	}

	FT_Library_SetLcdFilter( _XftFTlibrary, FT_LCD_FILTER_NONE );

	if (font->info.spacing >= FC_MONO)
	{
	    if (transform)
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
		FT_Vector_Transform(&vector, &font->info.matrix);
		xftg->metrics.xOff = (short)(TRUNC(ROUND(vector.x)));
		xftg->metrics.yOff = (short)(-TRUNC(ROUND(vector.y)));
	    }
	    else
	    {
		short maximum_x = (short)(font->public.max_advance_width);
		short maximum_y = (short)(-font->public.max_advance_width);
		short trimmed_x = (short)(TRUNC(ROUND(glyphslot->advance.x)));
		short trimmed_y = (short)(-TRUNC(ROUND(glyphslot->advance.y)));
		if (font->info.load_flags & FT_LOAD_VERTICAL_LAYOUT)
		{
		    xftg->metrics.xOff = 0;
		    xftg->metrics.yOff = min(maximum_y,trimmed_y);
		}
		else
		{
		    xftg->metrics.xOff = min(maximum_x,trimmed_x);
		    xftg->metrics.yOff = 0;
		}
	    }
	}
	else
	{
	    xftg->metrics.xOff = (short)(TRUNC(ROUND(glyphslot->advance.x)));
	    xftg->metrics.yOff = (short)(-TRUNC(ROUND(glyphslot->advance.y)));
	}

	/* compute the size of the final bitmap */
	ftbit = &glyphslot->bitmap;

	width = (int)ftbit->width;
	height = (int)ftbit->rows;

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
			    printf ("%c", (line[x>>3] & (1 << (x & 7))) ? '#' : ' ');
			}
		    }
		    printf ("|\n");
		    line += ftbit->pitch;
		}
		printf ("\n");
	    }
	}

	m3x3_uniform(m);
	size = _compute_xrender_bitmap_size( &local, glyphslot, mode, glyph_transform ? &font->info.matrix : NULL, m );
	if ( size < 0 )
	    continue;

	xftg->metrics.width  = (unsigned short)local.width;
	xftg->metrics.height = (unsigned short)local.rows;
	if (glyph_transform)
	{
	    m3x3 mi;

	    m3x3_invert(m, mi);
	    vector.x = - glyphslot->bitmap_left;
	    vector.y =   glyphslot->bitmap_top;
	    m3x3_transform(&vector, mi);
	    xftg->metrics.x = (short)vector.x;
	    xftg->metrics.y = (short)vector.y;
	}
	else
	{
	    xftg->metrics.x = (short)(- glyphslot->bitmap_left);
	    xftg->metrics.y = (short)(  glyphslot->bitmap_top);
	}

	/*
	 * If the glyph is relatively large (> 1% of server memory),
	 * don't send it until necessary.
	 */
	if (!need_bitmaps && ((unsigned long) size > (info->max_glyph_memory / 100)))
	    continue;

	/*
	 * Make sure there is enough buffer space for the glyph.
	 */
	if (size > bufSize)
	{
	    if (bufBitmap != bufLocal)
		free (bufBitmap);
	    bufBitmap = (unsigned char *) malloc ((size_t)size);
	    if (!bufBitmap)
		continue;
	    bufSize = size;
	}
	memset (bufBitmap, 0, (size_t)size);

	local.buffer = bufBitmap;

	if (mode == FT_RENDER_MODE_NORMAL && glyph_transform)
	    _scaled_fill_xrender_bitmap(&local, &glyphslot->bitmap, m);
	else
	    _fill_xrender_bitmap( &local, glyphslot, mode,
				 (font->info.rgba == FC_RGBA_BGR ||
				  font->info.rgba == FC_RGBA_VBGR) );

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

	if (xftg->picture)
	{
	    XRenderFreePicture(dpy, xftg->picture);
	    xftg->picture = 0;
	}
	xftg->glyph_memory = (size_t)size + font->sizeof_glyph;
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
			*line++ = (unsigned char)c;
		    }
		}
	    }
	    else if (glyphslot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA || mode != FT_RENDER_MODE_NORMAL)
	    {
		/* invert ARGB <=> BGRA */
		if (ImageByteOrder (dpy) != XftNativeByteOrder ())
		    XftSwapCARD32 ((CARD32 *) bufBitmap, size >> 2);
	    }

	    if (glyphslot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA)
	    {
		Pixmap pixmap = XCreatePixmap(dpy, DefaultRootWindow(dpy), local.width, local.rows, 32);
		GC gc = XCreateGC(dpy, pixmap, 0, NULL);
		XImage image = {
		    (int) local.width, (int) local.rows, 0, ZPixmap, (char *)bufBitmap,
		    dpy->byte_order, dpy->bitmap_unit, dpy->bitmap_bit_order, 32,
		    32, (int) (local.width * 4 - (unsigned) local.pitch), 32,
		    0, 0, 0, NULL, { NULL }
		};

		XInitImage(&image);
		XPutImage(dpy, pixmap, gc, &image, 0, 0, 0, 0, local.width, local.rows);
		xftg->picture = XRenderCreatePicture(dpy, pixmap, font->format, 0, NULL);

		XFreeGC(dpy, gc);
		XFreePixmap(dpy, pixmap);
		/*
		 * Record 256 times higher memory pressure for unrotated
		 * pictures, and maximum for rotated pictures.
		 */
		if (font->info.matrix.xy || font->info.matrix.yx)
		    xftg->glyph_memory += font->max_glyph_memory - (unsigned long) size;
		else
		    xftg->glyph_memory += (size_t)size * 255;
	    }
	    else
		XRenderAddGlyphs (dpy, font->glyphset, &glyph,
				  &xftg->metrics, 1,
				  (char *) bufBitmap, size);
	}
	else
	{
	    if (size)
	    {
		xftg->bitmap = malloc ((size_t)size);
		if (xftg->bitmap)
		    memcpy (xftg->bitmap, bufBitmap, (size_t)size);
	    }
	    else
		xftg->bitmap = NULL;
	}

	font->glyph_memory += xftg->glyph_memory;
	info->glyph_memory += xftg->glyph_memory;
	if (XftDebug() & XFT_DBG_CACHE)
	    _XftFontValidateMemory (dpy, pub);
	if (XftDebug() & XFT_DBG_CACHEV)
	    printf ("Caching glyph 0x%x size %lu\n", glyphindex,
		    xftg->glyph_memory);

	if (font->track_mem_usage) {
	    XftGlyphUsage *xuse = (XftGlyphUsage *) xftg;

	    if (font->newest == FT_UINT_MAX) {
		xuse->older = glyphindex;
	        xuse->newer = glyphindex;
		if (XftDebug() & XFT_DBG_USAGE)
		    printf("alloc %p -> %d: %p USE %d.%d\n",
			    (void *) font, glyphindex,
			    (void *) xuse, xuse->older, xuse->newer);
	    } else {
		XftGlyphUsage *xnew;
		XftGlyphUsage *xold;

		assert(font->glyphs[font->newest] != NULL);
		xnew = (XftGlyphUsage *) font->glyphs[font->newest];

		assert(font->glyphs[xnew->newer] != NULL);
		xold = (XftGlyphUsage *) font->glyphs[xnew->newer];

		xuse->older = font->newest;
		xuse->newer = xnew->newer;
		xnew->newer = glyphindex;
		xold->older = glyphindex;
		if (XftDebug() & XFT_DBG_USAGE)
		    printf("alloc %p -> %d: %p USE %d.%d, %p NEW %d.%d %p OLD %d.%d\n",
			    (void *) font, glyphindex,
			    (void *) xuse, xuse->older, xuse->newer,
			    (void *) xnew, xnew->older, xnew->newer,
			    (void *) xold, xold->older, xold->newer);
	    }

	    font->newest = glyphindex;
	    font->total_inuse++;
	    if (XftDebug() & XFT_DBG_USAGE)
		_XftValidateGlyphUsage(font);
	}
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
	    if (XftDebug() & XFT_DBG_CACHEV)
		printf ("Uncaching glyph 0x%x size %lu\n",
			glyphindex, xftg->glyph_memory);
	    if (font->format)
	    {
		if (xftg->picture)
		    XRenderFreePicture(dpy, xftg->picture);
		else if (font->glyphset)
		{
		    glyphBuf[nused++] = (Glyph) glyphindex;
		    if (nused == sizeof (glyphBuf) / sizeof (glyphBuf[0]))
		    {
			XRenderFreeGlyphs (dpy, font->glyphset, glyphBuf, nused);
			nused = 0;
		    }
		}
	    }
	    else if (xftg->bitmap)
		free (xftg->bitmap);
	    font->glyph_memory -= xftg->glyph_memory;
	    if (info)
		info->glyph_memory -= xftg->glyph_memory;
	}

	if (font->track_mem_usage) {
	    XftGlyphUsage *xuse = (XftGlyphUsage *) xftg;
	    XftGlyphUsage *xtmp;

	    if (XftDebug() & XFT_DBG_USAGE)
		printf("free %p -> %p USE %d.%d\n",
		       (void *) font, (void *) xuse, xuse->older, xuse->newer);

	    if (xuse->older != FT_UINT_MAX) {
		xtmp = (XftGlyphUsage *) font->glyphs[xuse->older];
		if (xtmp != NULL) {
		    /* update link around to oldest glyph */
		    xtmp->newer = xuse->newer;
		}
		if (font->newest == glyphindex) {
		    if (font->newest == xuse->older)
			font->newest = FT_UINT_MAX;
		    else
			font->newest = xuse->older;
		}
	    }
	    if (xuse->newer != FT_UINT_MAX) {
		xtmp = (XftGlyphUsage *) font->glyphs[xuse->newer];
		if (xtmp != NULL) {
		    /* update link around to newest glyph */
		    xtmp->older = xuse->older;
		}
	    }
	    if (font->total_inuse) {
		font->total_inuse--;
	    } else {
		fprintf (stderr, "Xft: glyph count error\n");
	    }
	    if (XftDebug() & XFT_DBG_USAGE)
		_XftValidateGlyphUsage(font);
	}

	free (xftg);
	XftMemFree (XFT_MEM_GLYPH, font->sizeof_glyph);
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
	    xftg = malloc (font->sizeof_glyph);
	    if (!xftg)
		return FcFalse;
	    XftMemAlloc (XFT_MEM_GLYPH, font->sizeof_glyph);

	    xftg->bitmap = NULL;
	    xftg->glyph_memory = 0;
	    xftg->picture = 0;
	    font->glyphs[glyph] = xftg;

	    if (font->track_mem_usage) {
		XftGlyphUsage *xuse = (XftGlyphUsage *) xftg;
		xuse->older = FT_UINT_MAX;
		xuse->newer = FT_UINT_MAX;
	    }
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

    /*
     * Make unloading faster by moving newly-referenced glyphs to the front
     * of the list, leaving the less-used glyphs on the end.
     *
     * If the glyph is zero, the older/newer data may not have been set.
     */
    if (glyph != 0
     && font->track_mem_usage
     && font->total_inuse > 10
     && font->newest != FT_UINT_MAX
     && font->newest != glyph)
    {
	XftGlyphUsage *xuse = (XftGlyphUsage *) xftg;
	XftGlyphUsage *xtmp = (XftGlyphUsage *) font->glyphs[font->newest];
	XftGlyphUsage *xold;
	XftGlyphUsage *xnew;

	/* delink */
	xold = (XftGlyphUsage *) font->glyphs[xuse->older];
	xnew = (XftGlyphUsage *) font->glyphs[xuse->newer];
	assert(xold != NULL);
	assert(xnew != NULL);
	xold->newer = xuse->newer;
	xnew->older = xuse->older;

	/* relink */
	xnew = (XftGlyphUsage *) font->glyphs[xtmp->newer];
	assert(xnew != NULL);
	xnew->older = glyph;
	xuse->older = font->newest;
	xuse->newer = xtmp->newer;
	xtmp->newer = glyph;

	font->newest = glyph;
    }
    return FcFalse;
}

_X_EXPORT FcBool
XftCharExists (Display	    *dpy _X_UNUSED,
	       XftFont	    *pub,
	       FcChar32	     ucs4)
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

    ent = ucs4 % (FcChar32)font->hash_value;
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
	    offset = ucs4 % (FcChar32)font->rehash_value;
	    if (!offset)
		offset = 1;
	}
	ent = ent + offset;
	if (ent >= (FcChar32)font->hash_value)
	    ent -= (FcChar32)font->hash_value;
    }
    return font->hash_table[ent].glyph;
}

/*
 * Remove glyph(s) from the font to reduce memory-usage.
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

    if (XftDebug() & XFT_DBG_CACHE)
	_XftFontValidateMemory (dpy, pub);

    if (font->track_mem_usage)
    {
	/*
	 * Remove the oldest glyph from the font.
	 */
	if (font->newest != FT_UINT_MAX) {
	    XftGlyphUsage *xuse = (XftGlyphUsage *) font->glyphs[font->newest];
	    if ((glyphindex = xuse->newer) != FT_UINT_MAX)
		XftFontUnloadGlyphs (dpy, pub, &glyphindex, 1);
	}
    }
    else if (font->use_free_glyphs)
    {
	/*
	 * Pick a random glyph from the font and remove it from the cache
	 */
	glyph_memory = ((unsigned long)rand() % font->glyph_memory);
	for (glyphindex = 0; glyphindex < font->num_glyphs; glyphindex++)
	{
	    xftg = font->glyphs[glyphindex];
	    if (xftg)
	    {
		if (xftg->glyph_memory > glyph_memory)
		{
		    XftFontUnloadGlyphs (dpy, pub, &glyphindex, 1);
		    break;
		}
		glyph_memory -= xftg->glyph_memory;
	    }
	}
    }
    else
    {
	/*
	 * Free all glyphs, since they are part of a set.
	 */
	if (font->glyphset)
	{
	    XRenderFreeGlyphSet (dpy, font->glyphset);
	    font->glyphset = 0;
	}
	for (glyphindex = 0; glyphindex < font->num_glyphs; glyphindex++)
	{
	    xftg = font->glyphs[glyphindex];
	    if (xftg)
	    {
		if (xftg->glyph_memory > 0)
		{
		    XftFontUnloadGlyphs (dpy, pub, &glyphindex, 1);
		}
	    }
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
		printf ("Reduce memory for font 0x%lx from %lu to %lu\n",
			font->glyphset ? font->glyphset : (unsigned long) font,
			font->glyph_memory, font->max_glyph_memory);
	}
	while (font->glyph_memory > font->max_glyph_memory)
	    _XftFontUncacheGlyph (dpy, pub);
    }
    _XftDisplayManageMemory (dpy);
}
