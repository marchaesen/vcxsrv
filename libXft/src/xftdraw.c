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

/*
 * Ok, this is a pain.  To share source pictures across multiple destinations,
 * the screen for each drawable must be discovered.
 */

static int
_XftDrawScreen (Display *dpy, Drawable drawable, Visual *visual)
{
    int		    s;
    Window	    root;
    int		    x, y;
    unsigned int    width, height, borderWidth, depth;
    /* Special case the most common environment */
    if (ScreenCount (dpy) == 1)
	return 0;
    /*
     * If we've got a visual, look for the screen that points at it.
     * This requires no round trip.
     */
    if (visual)
    {
	for (s = 0; s < ScreenCount (dpy); s++)
	{
	    XVisualInfo	template, *ret;
	    int		nret;

	    template.visualid = visual->visualid;
	    template.screen = s;
	    ret = XGetVisualInfo (dpy, VisualIDMask|VisualScreenMask,
				  &template, &nret);
	    if (ret)
	    {
		XFree (ret);
		return s;
	    }
	}
    }
    /*
     * Otherwise, as the server for the drawable geometry and find
     * the screen from the root window.
     * This takes a round trip.
     */
    if (XGetGeometry (dpy, drawable, &root, &x, &y, &width, &height,
		      &borderWidth, &depth))
    {
	for (s = 0; s < ScreenCount (dpy); s++)
	{
	    if (RootWindow (dpy, s) == root)
		return s;
	}
    }
    /*
     * Make a guess -- it's probably wrong, but then the app probably
     * handed us a bogus drawable in this case
     */
    return 0;
}

_X_HIDDEN unsigned int
XftDrawDepth (XftDraw *draw)
{
    if (!draw->depth)
    {
	Window		    root;
	int		    x, y;
	unsigned int	    width, height, borderWidth, depth;
	if (XGetGeometry (draw->dpy, draw->drawable,
			  &root, &x, &y, &width, &height,
			  &borderWidth, &depth))
	    draw->depth = depth;
    }
    return draw->depth;
}

_X_HIDDEN unsigned int
XftDrawBitsPerPixel (XftDraw	*draw)
{
    if (!draw->bits_per_pixel)
    {
	XPixmapFormatValues *formats;
	int		    nformats;
	unsigned int	    depth;

	if ((depth = XftDrawDepth (draw)) &&
	    (formats = XListPixmapFormats (draw->dpy, &nformats)))
	{
	    int	i;

	    for (i = 0; i < nformats; i++)
	    {
		if ((unsigned) formats[i].depth == depth)
		{
		    draw->bits_per_pixel = (unsigned)formats[i].bits_per_pixel;
		    break;
		}
	    }
	    XFree (formats);
	}
    }
    return draw->bits_per_pixel;
}

_X_EXPORT XftDraw *
XftDrawCreate (Display   *dpy,
	       Drawable  drawable,
	       Visual    *visual,
	       Colormap  colormap)
{
    XftDraw	*draw;

    draw = malloc (sizeof (XftDraw));
    if (!draw)
	return NULL;

    draw->dpy = dpy;
    draw->drawable = drawable;
    draw->screen = _XftDrawScreen (dpy, drawable, visual);
    draw->depth = 0;		/* don't find out unless we need to know */
    draw->bits_per_pixel = 0;	/* don't find out unless we need to know */
    draw->visual = visual;
    draw->colormap = colormap;
    draw->render.pict = 0;
    draw->core.gc = NULL;
    draw->core.use_pixmap = 0;
    draw->clip_type = XftClipTypeNone;
    draw->subwindow_mode = ClipByChildren;
    XftMemAlloc (XFT_MEM_DRAW, sizeof (XftDraw));
    return draw;
}

_X_EXPORT XftDraw *
XftDrawCreateBitmap (Display	*dpy,
		     Pixmap	bitmap)
{
    XftDraw	*draw;

    draw = malloc (sizeof (XftDraw));
    if (!draw)
	return NULL;
    draw->dpy = dpy;
    draw->drawable = (Drawable) bitmap;
    draw->screen = _XftDrawScreen (dpy, bitmap, NULL);
    draw->depth = 1;
    draw->bits_per_pixel = 1;
    draw->visual = NULL;
    draw->colormap = 0;
    draw->render.pict = 0;
    draw->core.gc = NULL;
    draw->core.use_pixmap = 0;
    draw->clip_type = XftClipTypeNone;
    draw->subwindow_mode = ClipByChildren;
    XftMemAlloc (XFT_MEM_DRAW, sizeof (XftDraw));
    return draw;
}

_X_EXPORT XftDraw *
XftDrawCreateAlpha (Display *dpy,
		    Pixmap  pixmap,
		    int	    depth)
{
    XftDraw	*draw;

    draw = malloc (sizeof (XftDraw));
    if (!draw)
	return NULL;
    draw->dpy = dpy;
    draw->drawable = (Drawable) pixmap;
    draw->screen = _XftDrawScreen (dpy, pixmap, NULL);
    draw->depth = (unsigned)depth;
    draw->bits_per_pixel = 0;	/* don't find out until we need it */
    draw->visual = NULL;
    draw->colormap = 0;
    draw->render.pict = 0;
    draw->core.gc = NULL;
    draw->core.use_pixmap = 0;
    draw->clip_type = XftClipTypeNone;
    draw->subwindow_mode = ClipByChildren;
    XftMemAlloc (XFT_MEM_DRAW, sizeof (XftDraw));
    return draw;
}

static XRenderPictFormat *
_XftDrawFormat (XftDraw	*draw)
{
    XftDisplayInfo  *info = _XftDisplayInfoGet (draw->dpy, True);

    if (!info || !info->hasRender)
	return NULL;

    if (draw->visual == NULL)
    {
	XRenderPictFormat   pf;

	pf.type = PictTypeDirect;
	pf.depth = (int)XftDrawDepth (draw);
	pf.direct.alpha = 0;
	pf.direct.alphaMask = (short)((1 << pf.depth) - 1);
	return XRenderFindFormat (draw->dpy,
				  (PictFormatType|
				   PictFormatDepth|
				   PictFormatAlpha|
				   PictFormatAlphaMask),
				  &pf,
				  0);
    }
    else
	return XRenderFindVisualFormat (draw->dpy, draw->visual);
}

_X_EXPORT void
XftDrawChange (XftDraw	*draw,
	       Drawable	drawable)
{
    draw->drawable = drawable;
    if (draw->render.pict)
    {
	XRenderFreePicture (draw->dpy, draw->render.pict);
	draw->render.pict = 0;
    }
    if (draw->core.gc)
    {
	XFreeGC (draw->dpy, draw->core.gc);
	draw->core.gc = NULL;
    }
}

_X_EXPORT Display *
XftDrawDisplay (XftDraw *draw)
{
    return draw->dpy;
}

_X_EXPORT Drawable
XftDrawDrawable (XftDraw *draw)
{
    return draw->drawable;
}

_X_EXPORT Colormap
XftDrawColormap (XftDraw *draw)
{
    return draw->colormap;
}

_X_EXPORT Visual *
XftDrawVisual (XftDraw *draw)
{
    return draw->visual;
}

_X_EXPORT void
XftDrawDestroy (XftDraw	*draw)
{
    if (draw->render.pict)
	XRenderFreePicture (draw->dpy, draw->render.pict);
    if (draw->core.gc)
	XFreeGC (draw->dpy, draw->core.gc);
    switch (draw->clip_type) {
    case XftClipTypeRegion:
	XDestroyRegion (draw->clip.region);
	break;
    case XftClipTypeRectangles:
	free (draw->clip.rect);
	break;
    case XftClipTypeNone:
	break;
    }
    XftMemFree (XFT_MEM_DRAW, sizeof (XftDraw));
    free (draw);
}

_X_EXPORT Picture
XftDrawSrcPicture (XftDraw *draw, _Xconst XftColor *color)
{
    Display	    *dpy = draw->dpy;
    XftDisplayInfo  *info = _XftDisplayInfoGet (dpy, True);
    int		    i;
    XftColor	    bitmapColor;

    if (!info || !info->solidFormat)
	return 0;

    /*
     * Monochrome targets require special handling; the PictOp controls
     * the color, and the color must be opaque
     */
    if (!draw->visual && draw->depth == 1)
    {
	bitmapColor.color.alpha = 0xffff;
	bitmapColor.color.red   = 0xffff;
	bitmapColor.color.green = 0xffff;
	bitmapColor.color.blue  = 0xffff;
	color = &bitmapColor;
    }

    /*
     * See if there's one already available
     */
    for (i = 0; i < XFT_NUM_SOLID_COLOR; i++)
    {
	if (info->colors[i].pict &&
	    info->colors[i].screen == draw->screen &&
	    !memcmp ((const void *) &color->color,
		     (const void *) &info->colors[i].color,
		     sizeof (XRenderColor)))
	    return info->colors[i].pict;
    }
    /*
     * Pick one to replace at random
     */
    i = (unsigned int) rand () % XFT_NUM_SOLID_COLOR;

    if (info->hasSolid) {
	/*
	 * Free any existing entry
	 */
	if (info->colors[i].pict)
	    XRenderFreePicture (dpy, info->colors[i].pict);
	/*
	 * Create picture
	 */
	info->colors[i].pict = XRenderCreateSolidFill (draw->dpy, &color->color);
    } else {
	if (info->colors[i].screen != draw->screen && info->colors[i].pict)
	{
	    XRenderFreePicture (dpy, info->colors[i].pict);
	    info->colors[i].pict = 0;
	}
	/*
	 * Create picture if necessary
	 */
	if (!info->colors[i].pict)
	{
	    Pixmap			    pix;
	    XRenderPictureAttributes    pa;

	    pix = XCreatePixmap (dpy, RootWindow (dpy, draw->screen), 1, 1,
				 (unsigned)info->solidFormat->depth);
	    pa.repeat = True;
	    info->colors[i].pict = XRenderCreatePicture (draw->dpy,
							 pix,
							 info->solidFormat,
							 CPRepeat, &pa);
	    XFreePixmap (dpy, pix);
	}
	/*
	 * Set to the new color
	 */
	info->colors[i].color = color->color;
	info->colors[i].screen = draw->screen;
	XRenderFillRectangle (dpy, PictOpSrc,
			      info->colors[i].pict,
			      &color->color, 0, 0, 1, 1);
    }
    info->colors[i].color = color->color;
    info->colors[i].screen = draw->screen;

    return info->colors[i].pict;
}

static int
_XftDrawOp (_Xconst XftDraw *draw, _Xconst XftColor *color)
{
    if (draw->visual || draw->depth != 1)
	return PictOpOver;
    if (color->color.alpha >= 0x8000)
	return PictOpOver;
    return PictOpOutReverse;
}

static FcBool
_XftDrawRenderPrepare (XftDraw	*draw)
{
    if (!draw->render.pict)
    {
	XRenderPictFormat	    *format;
	XRenderPictureAttributes    pa;
	unsigned long		    mask = 0;

	format = _XftDrawFormat (draw);
	if (!format)
	    return FcFalse;

	if (draw->subwindow_mode == IncludeInferiors)
	{
	    pa.subwindow_mode = IncludeInferiors;
	    mask |= CPSubwindowMode;
	}
	draw->render.pict = XRenderCreatePicture (draw->dpy, draw->drawable,
						  format, mask, &pa);
	if (!draw->render.pict)
	    return FcFalse;
	switch (draw->clip_type) {
	case XftClipTypeRegion:
	    XRenderSetPictureClipRegion (draw->dpy, draw->render.pict,
					 draw->clip.region);
	    break;
	case XftClipTypeRectangles:
	    XRenderSetPictureClipRectangles (draw->dpy, draw->render.pict,
					     draw->clip.rect->xOrigin,
					     draw->clip.rect->yOrigin,
					     XftClipRects(draw->clip.rect),
					     draw->clip.rect->n);
	    break;
	case XftClipTypeNone:
	    break;
	}
    }
    return FcTrue;
}

static FcBool
_XftDrawCorePrepare (XftDraw *draw, _Xconst XftColor *color)
{
    if (!draw->core.gc)
    {
	XGCValues	gcv;
	unsigned long	mask = 0;
	if (draw->subwindow_mode == IncludeInferiors)
	{
	    gcv.subwindow_mode = IncludeInferiors;
	    mask |= GCSubwindowMode;
	}
	draw->core.gc = XCreateGC (draw->dpy, draw->drawable, mask, &gcv);
	if (!draw->core.gc)
	    return FcFalse;
	switch (draw->clip_type) {
	case XftClipTypeRegion:
	    XSetRegion (draw->dpy, draw->core.gc, draw->clip.region);
	    break;
	case XftClipTypeRectangles:
	    XSetClipRectangles (draw->dpy, draw->core.gc,
				draw->clip.rect->xOrigin,
				draw->clip.rect->yOrigin,
				XftClipRects (draw->clip.rect),
				draw->clip.rect->n,
				Unsorted);
	    break;
	case XftClipTypeNone:
	    break;
	}
    }
    XSetForeground (draw->dpy, draw->core.gc, color->pixel);
    return FcTrue;
}

_X_EXPORT Picture
XftDrawPicture (XftDraw *draw)
{
    if (!_XftDrawRenderPrepare (draw))
	return 0;
    return draw->render.pict;
}

#define NUM_LOCAL   1024

_X_EXPORT void
XftDrawGlyphs (XftDraw		*draw,
	       _Xconst XftColor	*color,
	       XftFont		*pub,
	       int		x,
	       int		y,
	       _Xconst FT_UInt	*glyphs,
	       int		nglyphs)
{
    XftFontInt	*font = (XftFontInt *) pub;

    if (font->format)
    {
	Picture	    src;

	if (_XftDrawRenderPrepare (draw) &&
	    (src = XftDrawSrcPicture (draw, color)))
	    XftGlyphRender (draw->dpy, _XftDrawOp (draw, color),
			     src, pub, draw->render.pict,
			     0, 0, x, y, glyphs, nglyphs);
    }
    else
    {
	if (_XftDrawCorePrepare (draw, color))
	    XftGlyphCore (draw, color, pub, x, y, glyphs, nglyphs);
    }
}

_X_EXPORT void
XftDrawString8 (XftDraw		    *draw,
		_Xconst XftColor    *color,
		XftFont		    *pub,
		int		    x,
		int		    y,
		_Xconst FcChar8	    *string,
		int		    len)
{
    FT_UInt	    *glyphs, glyphs_local[NUM_LOCAL];
    int		    i;

    if (XftDebug () & XFT_DBG_DRAW)
	printf ("DrawString \"%*.*s\"\n", len, len, string);

    if (len <= NUM_LOCAL)
	glyphs = glyphs_local;
    else
    {
	glyphs = AllocUIntArray (len);
	if (!glyphs)
	    return;
    }
    for (i = 0; i < len; i++)
	glyphs[i] = XftCharIndex (draw->dpy, pub, string[i]);
    XftDrawGlyphs (draw, color, pub, x, y, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftDrawString16 (XftDraw	    *draw,
		 _Xconst XftColor   *color,
		 XftFont	    *pub,
		 int		    x,
		 int		    y,
		 _Xconst FcChar16   *string,
		 int		    len)
{
    FT_UInt	    *glyphs, glyphs_local[NUM_LOCAL];
    int		    i;

    if (len <= 0)
	return;

    if (len <= NUM_LOCAL)
	glyphs = glyphs_local;
    else
    {
	glyphs = AllocUIntArray (len);
	if (!glyphs)
	    return;
    }
    for (i = 0; i < len; i++)
	glyphs[i] = XftCharIndex (draw->dpy, pub, string[i]);

    XftDrawGlyphs (draw, color, pub, x, y, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftDrawString32 (XftDraw	    *draw,
		 _Xconst XftColor   *color,
		 XftFont	    *pub,
		 int		    x,
		 int		    y,
		 _Xconst FcChar32   *string,
		 int		    len)
{
    FT_UInt	    *glyphs, glyphs_local[NUM_LOCAL];
    int		    i;

    if (len <= 0)
	return;

    if (len <= NUM_LOCAL)
	glyphs = glyphs_local;
    else
    {
	glyphs = AllocUIntArray (len);
	if (!glyphs)
	    return;
    }
    for (i = 0; i < len; i++)
	glyphs[i] = XftCharIndex (draw->dpy, pub, string[i]);

    XftDrawGlyphs (draw, color, pub, x, y, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftDrawStringUtf8 (XftDraw	    *draw,
		   _Xconst XftColor *color,
		   XftFont	    *pub,
		   int		    x,
		   int		    y,
		   _Xconst FcChar8  *string,
		   int		    len)
{
    FT_UInt	    *glyphs, *glyphs_new, glyphs_local[NUM_LOCAL];
    FcChar32	    ucs4;
    int		    i;
    int		    l;
    int		    size;

    if (len <= 0)
	return;

    i = 0;
    glyphs = glyphs_local;
    size = NUM_LOCAL;
    while (len && (l = FcUtf8ToUcs4 (string, &ucs4, len)) > 0)
    {
	if (i == size)
	{
	    glyphs_new = AllocUIntArray (size * 2);
	    if (!glyphs_new)
	    {
		if (glyphs != glyphs_local)
		    free (glyphs);
		return;
	    }
	    memcpy (glyphs_new, glyphs, (size_t)size * sizeof (FT_UInt));
	    size *= 2;
	    if (glyphs != glyphs_local)
		free (glyphs);
	    glyphs = glyphs_new;
	}
	glyphs[i++] = XftCharIndex (draw->dpy, pub, ucs4);
	string += l;
	len -= l;
    }
    XftDrawGlyphs (draw, color, pub, x, y, glyphs, i);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftDrawStringUtf16 (XftDraw		*draw,
		    _Xconst XftColor	*color,
		    XftFont		*pub,
		    int			x,
		    int			y,
		    _Xconst FcChar8	*string,
		    FcEndian		endian,
		    int			len)
{
    FT_UInt	    *glyphs, *glyphs_new, glyphs_local[NUM_LOCAL];
    FcChar32	    ucs4;
    int		    i;
    int		    l;
    int		    size;

    if (len <= 0)
	return;

    i = 0;
    glyphs = glyphs_local;
    size = NUM_LOCAL;
    while (len && (l = FcUtf16ToUcs4 (string, endian, &ucs4, len)) > 0)
    {
	if (i == size)
	{
	    glyphs_new = AllocUIntArray (size * 2);
	    if (!glyphs_new)
	    {
		if (glyphs != glyphs_local)
		    free (glyphs);
		return;
	    }
	    memcpy (glyphs_new, glyphs, (size_t)size * sizeof (FT_UInt));
	    size *= 2;
	    if (glyphs != glyphs_local)
		free (glyphs);
	    glyphs = glyphs_new;
	}
	glyphs[i++] = XftCharIndex (draw->dpy, pub, ucs4);
	string += l;
	len -= l;
    }
    XftDrawGlyphs (draw, color, pub, x, y, glyphs, i);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftDrawGlyphSpec (XftDraw		*draw,
		  _Xconst XftColor	*color,
		  XftFont		*pub,
		  _Xconst XftGlyphSpec	*glyphs,
		  int			len)
{
    XftFontInt	*font = (XftFontInt *) pub;

    if (font->format)
    {
	Picture	src;

	if (_XftDrawRenderPrepare (draw) &&
	    (src = XftDrawSrcPicture (draw, color)))
	{
	    XftGlyphSpecRender (draw->dpy, _XftDrawOp (draw, color),
				src, pub, draw->render.pict,
				0, 0, glyphs, len);
	}
    }
    else
    {
	if (_XftDrawCorePrepare (draw, color))
	    XftGlyphSpecCore (draw, color, pub, glyphs, len);
    }
}

_X_EXPORT void
XftDrawGlyphFontSpec (XftDraw			*draw,
		      _Xconst XftColor		*color,
		      _Xconst XftGlyphFontSpec	*glyphs,
		      int			len)
{
    int		i;
    int		start;

    i = 0;
    while (i < len)
    {
	start = i;
	if (((XftFontInt *) glyphs[i].font)->format)
	{
	    Picture	src;
	    while (i < len && ((XftFontInt *) glyphs[i].font)->format)
		i++;
	    if (_XftDrawRenderPrepare (draw) &&
		(src = XftDrawSrcPicture (draw, color)))
	    {
		XftGlyphFontSpecRender (draw->dpy, _XftDrawOp (draw, color),
					src, draw->render.pict,
					0, 0, glyphs + start , i - start);
	    }
	}
	else
	{
	    while (i < len && !((XftFontInt *) glyphs[i].font)->format)
		i++;
	    if (_XftDrawCorePrepare (draw, color))
		XftGlyphFontSpecCore (draw, color, glyphs + start, i - start);
	}
    }
}

_X_EXPORT void
XftDrawCharSpec (XftDraw		*draw,
		 _Xconst XftColor	*color,
		 XftFont		*pub,
		 _Xconst XftCharSpec	*chars,
		 int			len)
{
    XftGlyphSpec    *glyphs, glyphs_local[NUM_LOCAL];
    int		    i;

    if (len <= 0)
	return;

    if (len <= NUM_LOCAL)
	glyphs = glyphs_local;
    else
    {
	glyphs = AllocGlyphSpecArray (len);
	if (!glyphs)
	    return;
    }
    for (i = 0; i < len; i++)
    {
	glyphs[i].glyph = XftCharIndex(draw->dpy, pub, chars[i].ucs4);
	glyphs[i].x = chars[i].x;
	glyphs[i].y = chars[i].y;
    }

    XftDrawGlyphSpec (draw, color, pub, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftDrawCharFontSpec (XftDraw			*draw,
		     _Xconst XftColor		*color,
		     _Xconst XftCharFontSpec	*chars,
		     int			len)
{
    XftGlyphFontSpec	*glyphs, glyphs_local[NUM_LOCAL];
    int			i;

    if (len <= 0)
	return;

    if (len <= NUM_LOCAL)
	glyphs = glyphs_local;
    else
    {
	glyphs = AllocGlyphFontSpecArray (len);
	if (!glyphs)
	    return;
    }
    for (i = 0; i < len; i++)
    {
	glyphs[i].font = chars[i].font;
	glyphs[i].glyph = XftCharIndex(draw->dpy, glyphs[i].font, chars[i].ucs4);
	glyphs[i].x = chars[i].x;
	glyphs[i].y = chars[i].y;
    }

    XftDrawGlyphFontSpec (draw, color, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftDrawRect (XftDraw		*draw,
	     _Xconst XftColor	*color,
	     int		x,
	     int		y,
	     unsigned int	width,
	     unsigned int	height)
{
    if (_XftDrawRenderPrepare (draw))
    {
	XRenderFillRectangle (draw->dpy, PictOpSrc, draw->render.pict,
			      &color->color, x, y, width, height);
    }
    else if (_XftDrawCorePrepare (draw, color))
    {
	/* note: not XftRectCore() */
	XSetForeground (draw->dpy, draw->core.gc, color->pixel);
	XFillRectangle (draw->dpy, draw->drawable, draw->core.gc,
			x, y, width, height);
    }
}

_X_EXPORT Bool
XftDrawSetClip (XftDraw	*draw,
		Region	r)
{
    Region			n = NULL;

    /*
     * Check for quick exits
     */
    if (!r && draw->clip_type == XftClipTypeNone)
	return True;

    if (r &&
	draw->clip_type == XftClipTypeRegion &&
	XEqualRegion (r, draw->clip.region))
    {
	return True;
    }

    /*
     * Duplicate the region so future changes can be short circuited
     */
    if (r)
    {
	n = XCreateRegion ();
	if (n)
	{
	    if (!XUnionRegion (n, r, n))
	    {
		XDestroyRegion (n);
		return False;
	    }
	}
    }

    /*
     * Destroy existing clip
     */
    switch (draw->clip_type) {
    case XftClipTypeRegion:
	XDestroyRegion (draw->clip.region);
	break;
    case XftClipTypeRectangles:
	free (draw->clip.rect);
	break;
    case XftClipTypeNone:
	break;
    }

    /*
     * Set the clip
     */
    if (n)
    {
	draw->clip_type = XftClipTypeRegion;
	draw->clip.region = n;
    }
    else
    {
	draw->clip_type = XftClipTypeNone;
    }
    /*
     * Apply new clip to existing objects
     */
    if (draw->render.pict)
    {
	if (n)
	    XRenderSetPictureClipRegion (draw->dpy, draw->render.pict, n);
	else
	{
	    XRenderPictureAttributes	pa;
	    pa.clip_mask = None;
	    XRenderChangePicture (draw->dpy, draw->render.pict,
				  CPClipMask, &pa);
	}
    }
    if (draw->core.gc)
    {
	if (n)
	    XSetRegion (draw->dpy, draw->core.gc, draw->clip.region);
	else
	    XSetClipMask (draw->dpy, draw->core.gc, None);
    }
    return True;
}

_X_EXPORT Bool
XftDrawSetClipRectangles (XftDraw		*draw,
			  int			xOrigin,
			  int			yOrigin,
			  _Xconst XRectangle	*rects,
			  int			n)
{
    XftClipRect	*new = NULL;

    /*
     * Check for quick exit
     */
    if (draw->clip_type == XftClipTypeRectangles &&
	draw->clip.rect->n == n &&
	(n == 0 || (draw->clip.rect->xOrigin == xOrigin &&
		    draw->clip.rect->yOrigin == yOrigin)) &&
	!memcmp (XftClipRects (draw->clip.rect), rects, (size_t)n * sizeof (XRectangle)))
    {
	return True;
    }

    /*
     * Duplicate the region so future changes can be short circuited
     */
    new = malloc (sizeof (XftClipRect) + (size_t)n * sizeof (XRectangle));
    if (!new)
	return False;

    new->n = n;
    new->xOrigin = xOrigin;
    new->yOrigin = yOrigin;
    memcpy (XftClipRects (new), rects, (size_t)n * sizeof (XRectangle));

    /*
     * Destroy existing clip
     */
    switch (draw->clip_type) {
    case XftClipTypeRegion:
	XDestroyRegion (draw->clip.region);
	break;
    case XftClipTypeRectangles:
	free (draw->clip.rect);
	break;
    case XftClipTypeNone:
	break;
    }

    /*
     * Set the clip
     */
    draw->clip_type = XftClipTypeRectangles;
    draw->clip.rect = new;
    /*
     * Apply new clip to existing objects
     */
    if (draw->render.pict)
    {
	XRenderSetPictureClipRectangles (draw->dpy, draw->render.pict,
					 new->xOrigin,
					 new->yOrigin,
					 XftClipRects(new),
					 new->n);
    }
    if (draw->core.gc)
    {
	XSetClipRectangles (draw->dpy, draw->core.gc,
			    new->xOrigin,
			    new->yOrigin,
			    XftClipRects (new),
			    new->n,
			    Unsorted);
    }
    return True;
}

_X_EXPORT void
XftDrawSetSubwindowMode (XftDraw *draw, int mode)
{
    if (mode == draw->subwindow_mode)
	return;
    draw->subwindow_mode = mode;
    if (draw->render.pict)
    {
	XRenderPictureAttributes    pa;

	pa.subwindow_mode = mode;
	XRenderChangePicture (draw->dpy, draw->render.pict,
			      CPSubwindowMode, &pa);
    }
    if (draw->core.gc)
	XSetSubwindowMode (draw->dpy, draw->core.gc, mode);
}
