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
#include <stdint.h>

#define NUM_LOCAL	1024
#define NUM_ELT_LOCAL	128

/*
 * Dispatch glyph drawing to the correct XRenderCompositeString function
 */
static void
_XftCompositeString (Display           *dpy,
                     int               op,
                     Picture           src,
                     Picture           dst,
                     XRenderPictFormat *format,
                     GlyphSet          glyphset,
                     int               srcx,
                     int               srcy,
                     int               dstx,
                     int               dsty,
                     int               charwidth,
                     unsigned int      *chars,
                     int               nchars)
{
    if (nchars == 0)
        return;

    switch (charwidth) {
    case 1:
    default:
	XRenderCompositeString8 (dpy, op,
				 src, dst, format, glyphset,
				 srcx, srcy, dstx, dsty, (char*)chars, nchars);
	break;
    case 2:
	XRenderCompositeString16(dpy, op,
				 src, dst, format, glyphset,
				 srcx, srcy, dstx, dsty, (unsigned short*)chars, nchars);
	break;
    case 4:
	XRenderCompositeString32(dpy, op,
				 src, dst, format, glyphset,
				 srcx, srcy, dstx, dsty, (unsigned int*)chars, nchars);
	break;
    }
}

/*
 * Use the Render extension to draw the glyphs
 */

_X_EXPORT void
XftGlyphRender (Display		*dpy,
		int		op,
		Picture		src,
		XftFont		*pub,
		Picture		dst,
		int		srcx,
		int		srcy,
		int		x,
		int		y,
		_Xconst FT_UInt	*glyphs,
		int		nglyphs)
{
    XftFontInt	    *font = (XftFontInt *) pub;
    int		    i, j;
    FT_UInt	    missing[XFT_NMISSING];
    int		    nmissing;
    FT_UInt	    g, max;
    int		    width;
    int		    dstx, dsty;
    Glyph	    wire;
    XftGlyph*       glyph;
    char	    *char8;
    unsigned short  *char16;
    unsigned int    *char32;
    unsigned int    char_local[NUM_LOCAL];
    unsigned int    *chars;
    FcBool	    glyphs_loaded;
    size_t          size;
    size_t          needed;

    if (!font->format)
	return;

    /*
     * Load missing glyphs
     */
    nmissing = 0;
    max = 0;
    glyphs_loaded = FcFalse;
    for (i = 0; i < nglyphs; i++)
    {
	g = glyphs[i];
	if (g > max)
	    max = g;
	if (XftFontCheckGlyph (dpy, pub, FcTrue, g, missing, &nmissing))
	    glyphs_loaded = FcTrue;
    }
    if (nmissing)
	XftFontLoadGlyphs (dpy, pub, FcTrue, missing, nmissing);

    if (!font->glyphset)
	goto bail1;
    if (max < 0x100)
    {
	width = 1;
	size = sizeof (char);
    }
    else if (max < 0x10000)
    {
	width = 2;
	size = sizeof (unsigned short);
    }
    else
    {
	width = 4;
	size = sizeof (unsigned int);
    }
    chars = char_local;
    if ((size_t)nglyphs > SIZE_MAX / size)
	goto bail1;
    needed = (size_t)nglyphs * size;
    if (needed > sizeof (char_local))
    {
	chars = malloc (needed);
	if (!chars)
	    goto bail1;
    }
    dstx = x;
    dsty = y;
    char8 = (char *) chars;
    char16 = (unsigned short *) chars;
    char32 = (unsigned int *) chars;
    for (i = 0, j = 0; i < nglyphs; i++)
    {
	wire = (Glyph) glyphs[i];
	if (wire >= (Glyph) font->num_glyphs || !font->glyphs[wire])
	    wire = 0;
        glyph = font->glyphs[wire];
	if (glyph == NULL)
	    continue;
	if (glyph->picture)
	{
	    _XftCompositeString(dpy, op, src, dst, font->format, font->glyphset, srcx, srcy, x, y, width, chars, j);
	    XRenderComposite(dpy, PictOpOver, glyph->picture, None, dst, 0, 0, 0, 0, dstx - glyph->metrics.x, dsty - glyph->metrics.y, glyph->metrics.width, glyph->metrics.height);
	    dstx += glyph->metrics.xOff;
	    dsty += glyph->metrics.yOff;
	    x = dstx;
	    y = dsty;
	    j = 0;
	}
	else
	{
	    switch (width) {
	    case 1: char8[j] = (char) wire; break;
	    case 2: char16[j] = (unsigned short) wire; break;
	    case 4: char32[j] = (unsigned int) wire; break;
	    }
	    dstx += glyph->metrics.xOff;
	    dsty += glyph->metrics.yOff;
	    ++j;
	}
    }
    _XftCompositeString(dpy, op, src, dst, font->format, font->glyphset,
                        srcx, srcy, x, y, width, chars, j);
    if (chars != char_local)
	free (chars);
bail1:
    if (glyphs_loaded)
	_XftFontManageMemory (dpy, pub);
}

/*
 * Dispatch glyph drawing to the correct XRenderCompositeText function
 */
static void
_XftCompositeText (Display           *dpy,
                   int               op,
                   Picture           src,
                   Picture           dst,
                   XRenderPictFormat *format,
                   int               srcx,
                   int               srcy,
                   int               dstx,
                   int               dsty,
                   int               eltwidth,
                   XGlyphElt8        *elts,
                   int               nelt)
{
    if (nelt == 0)
        return;

    switch (eltwidth) {
    case 1:
    default:
	XRenderCompositeText8 (dpy, op,
			       src, dst, format,
			       srcx, srcy, dstx, dsty,
	                       (XGlyphElt8*)elts, nelt);
	break;
    case 2:
	XRenderCompositeText16(dpy, op,
			       src, dst, format,
			       srcx, srcy, dstx, dsty,
	                       (XGlyphElt16*)elts, nelt);
	break;
    case 4:
	XRenderCompositeText32(dpy, op,
			       src, dst, format,
			       srcx, srcy, dstx, dsty,
	                       (XGlyphElt32*)elts, nelt);
	break;
    }
}

_X_EXPORT void
XftGlyphSpecRender (Display		    *dpy,
		    int			    op,
		    Picture		    src,
		    XftFont		    *pub,
		    Picture		    dst,
		    int			    srcx,
		    int			    srcy,
		    _Xconst XftGlyphSpec    *glyphs,
		    int			    nglyphs)
{
    XftFontInt	    *font = (XftFontInt *) pub;
    int		    i, j;
    FT_UInt	    missing[XFT_NMISSING];
    int		    nmissing;
    int		    n;
    FT_UInt	    g;
    XftGlyph	    *glyph;
    FT_UInt	    max;
    int		    size, width;
    char	    *char8;
    unsigned short  *char16;
    unsigned int    *char32;
    unsigned int    char_local[NUM_LOCAL];
    unsigned int    *chars;
    XGlyphElt8	    *elts;
    XGlyphElt8	    elts_local[NUM_ELT_LOCAL];
    FcBool	    glyphs_loaded;
    int		    nelt;
    int		    x, y;

    if (!font->format)
	return;
    if (!nglyphs)
	return;

    /*
     * Load missing glyphs
     */
    max = 0;
    nmissing = 0;
    glyphs_loaded = FcFalse;
    g = glyphs[0].glyph;
    for (i = 0; i < nglyphs; i++)
    {
	g = glyphs[i].glyph;
	if (g > max)
	    max = g;
	if (XftFontCheckGlyph (dpy, pub, FcTrue, g, missing, &nmissing))
	    glyphs_loaded = FcTrue;
    }
    if (nmissing)
	XftFontLoadGlyphs (dpy, pub, FcTrue, missing, nmissing);

    if (!font->glyphset)
	goto bail1;

    /*
     * See what encoding size is needed
     */
    if (max < 0x100)
    {
	size = sizeof (char);
	width = 1;
    }
    else if (max < 0x10000)
    {
	size = sizeof (unsigned short);
	width = 2;
    }
    else
    {
	size = sizeof (unsigned int);
	width = 4;
    }
    chars = char_local;
    if (nglyphs * size > NUM_LOCAL)
    {
	chars = malloc ((size_t)(nglyphs * size));
	if (!chars)
	    goto bail1;
    }
    char8 = (char *) chars;
    char16 = (unsigned short *) chars;
    char32 = (unsigned int *) chars;

    /*
     * Compute the number of glyph elts needed
     */
    nelt = 1;
    for (i = 0; i < nglyphs; i++)
    {
	g = glyphs[i].glyph;
	/* Substitute default for non-existent glyphs */
	if (g >= font->num_glyphs || !font->glyphs[g])
	    g = 0;
	if (font->glyphs[g])
	     break;
    }
    if (i == nglyphs)
	goto bail2;
    glyph = font->glyphs[g];
    x = glyphs[i].x + glyph->metrics.xOff;
    y = glyphs[i].y + glyph->metrics.yOff;
    while (++i < nglyphs)
    {
	g = glyphs[i].glyph;
	/* Substitute default for non-existent glyphs */
	if (g >= font->num_glyphs || !font->glyphs[g])
	    g = 0;
	/*
	 * check to see if the glyph is placed where it would
	 * fall using the normal spacing and if it would render
	 * as a XRender glyph
	 */
	if ((glyph = font->glyphs[g]) && !glyph->picture)
	{
	    if (x != glyphs[i].x || y != glyphs[i].y)
	    {
		x = glyphs[i].x;
		y = glyphs[i].y;
		++nelt;
	    }
	    x += glyph->metrics.xOff;
	    y += glyph->metrics.yOff;
	}
    }

    elts = elts_local;
    if (nelt > NUM_ELT_LOCAL)
    {
	elts = AllocGlyphElt8Array (nelt);
	if (!elts)
	    goto bail2;
    }

    /*
     * Generate the list of glyph elts or render color glyphs
     */
    nelt = 0;
    x = y = 0;
    n = 0;
    j = 0;
    for (i = 0; i < nglyphs; i++)
    {
	g = glyphs[i].glyph;
	/* Substitute default for non-existent glyphs */
	if (g >= font->num_glyphs || !font->glyphs[g])
	    g = 0;
	if ((glyph = font->glyphs[g]))
	{
	    if (glyph->picture)
	    {
                XRenderComposite(dpy, PictOpOver, glyph->picture, None,
                                 dst, 0, 0, 0, 0,
                                 glyphs[i].x - glyph->metrics.x,
                                 glyphs[i].y - glyph->metrics.y,
                                 glyph->metrics.width,
                                 glyph->metrics.height);
                continue;
	    }
	    if (!i || x != glyphs[i].x || y != glyphs[i].y)
	    {
		if (n)
		{
		    elts[nelt].nchars = n;
		    nelt++;
		}
		elts[nelt].glyphset = font->glyphset;
		elts[nelt].chars = char8 + size * j;
		elts[nelt].xOff = glyphs[i].x - x;
		elts[nelt].yOff = glyphs[i].y - y;
		x = glyphs[i].x;
		y = glyphs[i].y;
		n = 0;
	    }
	    switch (width) {
	    case 1: char8[j] = (char) g; break;
	    case 2: char16[j] = (unsigned short) g; break;
	    case 4: char32[j] = (unsigned int) g; break;
	    }
	    x += glyph->metrics.xOff;
	    y += glyph->metrics.yOff;
	    j++;
	    n++;
	}
    }
    if (n)
    {
	elts[nelt].nchars = n;
	nelt++;
    }
    _XftCompositeText(dpy, op, src, dst, font->format,
		      srcx, srcy, glyphs[0].x, glyphs[0].y,
		      width, elts, nelt);

    if (elts != elts_local)
	free (elts);
bail2:
    if (chars != char_local)
	free (chars);
bail1:
    if (glyphs_loaded)
	_XftFontManageMemory (dpy, pub);
}

_X_EXPORT void
XftCharSpecRender (Display		*dpy,
		   int			op,
		   Picture		src,
		   XftFont		*pub,
		   Picture		dst,
		   int			srcx,
		   int			srcy,
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
	glyphs[i].glyph = XftCharIndex(dpy, pub, chars[i].ucs4);
	glyphs[i].x = chars[i].x;
	glyphs[i].y = chars[i].y;
    }

    XftGlyphSpecRender (dpy, op, src, pub, dst, srcx, srcy, glyphs, len);

    if (glyphs != glyphs_local)
	free (glyphs);
}

/*
 * Choose which format to draw text in when drawing with fonts
 * of different formats.  The trick is that ARGB formats aren't
 * compatible with A formats as PictOpAdd does the wrong thing, so
 * fall back to an A format when presented with an ARGB and A format
 */

#define XftIsARGBFormat(a)  ((a)->depth == 32)

static XRenderPictFormat *
XftPreferFormat (Display *dpy, XRenderPictFormat *a, XRenderPictFormat *b)
{
    XRenderPictFormat	*prefer = NULL;

    if (a == b)
	prefer = a;
    else if (XftIsARGBFormat(a) != XftIsARGBFormat(b))
	prefer = XRenderFindStandardFormat (dpy, PictStandardA8);
    else if (a->depth > b->depth)
	prefer = a;
    else
	prefer = b;
    return prefer;
}

_X_EXPORT void
XftGlyphFontSpecRender (Display			    *dpy,
			int			    op,
			Picture			    src,
			Picture			    dst,
			int			    srcx,
			int			    srcy,
			_Xconst XftGlyphFontSpec    *glyphs,
			int			    nglyphs)
{
    int		    i, j;
    XftFont	    *prevPublic;
    XftFontInt	    *firstFont;
    XRenderPictFormat	*format;
    FT_UInt	    missing[XFT_NMISSING];
    int		    nmissing;
    int		    n;
    FT_UInt	    g;
    XftGlyph	    *glyph;
    FT_UInt	    max;
    int		    size, width;
    char	    *char8;
    unsigned short  *char16;
    unsigned int    *char32;
    unsigned int    char_local[NUM_LOCAL];
    unsigned int    *chars;
    XGlyphElt8	    *elts;
    XGlyphElt8	    elts_local[NUM_ELT_LOCAL];
    FcBool	    glyphs_loaded;
    int		    nelt;
    int		    x, y;

    if (!nglyphs)
	return;

    /*
     * Load missing glyphs.  Have to load them
     * one at a time in case the font changes
     */
    max = 0;
    glyphs_loaded = FcFalse;
    g = glyphs[0].glyph;
    for (i = 0; i < nglyphs; i++)
    {
	XftFont	    *pub = glyphs[i].font;
	XftFontInt  *font = (XftFontInt *) pub;
	g = glyphs[i].glyph;
	if (g > max)
	    max = g;
	nmissing = 0;
	if (XftFontCheckGlyph (dpy, pub, FcTrue, g, missing, &nmissing))
	    glyphs_loaded = FcTrue;
	if (nmissing)
	    XftFontLoadGlyphs (dpy, pub, FcTrue, missing, nmissing);
	if (!font->format)
	    goto bail1;
	if (!font->glyphset)
	    goto bail1;
    }

    /*
     * See what encoding size is needed
     */
    if (max < 0x100)
    {
	size = sizeof (char);
	width = 1;
    }
    else if (max < 0x10000)
    {
	size = sizeof (unsigned short);
	width = 2;
    }
    else
    {
	size = sizeof (unsigned int);
	width = 4;
    }
    chars = char_local;
    if (nglyphs * size > NUM_LOCAL)
    {
	chars = malloc ((size_t)(nglyphs * size));
	if (!chars)
	    goto bail1;
    }
    char8 = (char *) chars;
    char16 = (unsigned short *) chars;
    char32 = (unsigned int *) chars;

    /*
     * Compute the number of glyph elts needed
     */
    nelt = 1;
    firstFont = NULL;
    for (i = 0; i < nglyphs; i++)
    {
	XftFont	    *pub = glyphs[i].font;
	XftFontInt  *font = (XftFontInt *) pub;
	g = glyphs[i].glyph;
	/* Substitute default for non-existent glyphs */
	if (g >= font->num_glyphs || !font->glyphs[g])
	    g = 0;
	if (font->glyphs[g])
	{
	    firstFont = font;
	    break;
	}
    }
    if (i == nglyphs || !firstFont)
	goto bail2;
    glyph = firstFont->glyphs[g];
    format = firstFont->format;
    x = glyphs[i].x + glyph->metrics.xOff;
    y = glyphs[i].y + glyph->metrics.yOff;
    prevPublic = NULL;
    while (++i < nglyphs)
    {
	XftFont	    *pub = glyphs[i].font;
	XftFontInt  *font = (XftFontInt *) pub;
	g = glyphs[i].glyph;
	/* Substitute default for non-existent glyphs */
	if (g >= font->num_glyphs || !font->glyphs[g])
	    g = 0;
	/*
	 * check to see if the glyph is placed where it would
	 * fall using the normal spacing
	 */
	if ((glyph = font->glyphs[g]) && !glyph->picture)
	{
	    if (pub != prevPublic || x != glyphs[i].x || y != glyphs[i].y)
	    {
		prevPublic = pub;
		if (font->format != format)
		    format = XftPreferFormat (dpy, font->format, format);
		x = glyphs[i].x;
		y = glyphs[i].y;
		++nelt;
	    }
	    x += glyph->metrics.xOff;
	    y += glyph->metrics.yOff;
	}
    }

    elts = elts_local;
    if (nelt > NUM_ELT_LOCAL)
    {
	elts = AllocGlyphElt8Array (nelt);
	if (!elts)
	    goto bail2;
    }

    /*
     * Generate the list of glyph elts and render color glyphs
     */
    nelt = 0;
    x = y = 0;
    n = 0;
    j = 0;
    prevPublic = NULL;
    for (i = 0; i < nglyphs; i++)
    {
	XftFont	    *pub = glyphs[i].font;
	XftFontInt  *font = (XftFontInt *) pub;

	g = glyphs[i].glyph;
	/* Substitute default for non-existent glyphs */
	if (g >= font->num_glyphs || !font->glyphs[g])
	    g = 0;
	if ((glyph = font->glyphs[g]))
	{
	    if (glyph->picture)
	    {
                XRenderComposite(dpy, PictOpOver, glyph->picture, None,
                                 dst, 0, 0, 0, 0,
                                 glyphs[i].x - glyph->metrics.x,
                                 glyphs[i].y - glyph->metrics.y,
                                 glyph->metrics.width,
                                 glyph->metrics.height);
                continue;
	    }
	    if (!i || pub != prevPublic || x != glyphs[i].x || y != glyphs[i].y)
	    {
		if (n)
		{
		    elts[nelt].nchars = n;
		    nelt++;
		}
		elts[nelt].glyphset = font->glyphset;
		elts[nelt].chars = char8 + size * j;
		elts[nelt].xOff = glyphs[i].x - x;
		elts[nelt].yOff = glyphs[i].y - y;
		prevPublic = pub;
		x = glyphs[i].x;
		y = glyphs[i].y;
		n = 0;
	    }
	    switch (width) {
	    case 1: char8[j] = (char) g; break;
	    case 2: char16[j] = (unsigned short) g; break;
	    case 4: char32[j] = (unsigned int) g; break;
	    }
	    x += glyph->metrics.xOff;
	    y += glyph->metrics.yOff;
	    j++;
	    n++;
	}
    }
    if (n)
    {
	elts[nelt].nchars = n;
	nelt++;
    }
    _XftCompositeText(dpy, op, src, dst, format,
		      srcx, srcy, glyphs[0].x, glyphs[0].y,
		      width, elts, nelt);

    if (elts != elts_local)
	free (elts);
bail2:
    if (chars != char_local)
	free (chars);
bail1:
    if (glyphs_loaded)
	for (i = 0; i < nglyphs; i++)
	    _XftFontManageMemory (dpy, glyphs[i].font);
}

_X_EXPORT void
XftCharFontSpecRender (Display			*dpy,
		       int			op,
		       Picture			src,
		       Picture			dst,
		       int			srcx,
		       int			srcy,
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
	glyphs[i].glyph = XftCharIndex(dpy, glyphs[i].font, chars[i].ucs4);
	glyphs[i].x = chars[i].x;
	glyphs[i].y = chars[i].y;
    }

    XftGlyphFontSpecRender (dpy, op, src, dst, srcx, srcy, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftTextRender8 (Display		*dpy,
		int		op,
		Picture		src,
		XftFont		*pub,
		Picture		dst,
		int		srcx,
		int		srcy,
		int		x,
		int		y,
		_Xconst FcChar8	*string,
		int		len)
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
	glyphs[i] = XftCharIndex (dpy, pub, string[i]);
    XftGlyphRender (dpy, op, src, pub, dst,
		     srcx, srcy, x, y, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftTextRender16 (Display	    *dpy,
		 int		    op,
		 Picture	    src,
		 XftFont	    *pub,
		 Picture	    dst,
		 int		    srcx,
		 int		    srcy,
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
	glyphs[i] = XftCharIndex (dpy, pub, string[i]);
    XftGlyphRender (dpy, op, src, pub, dst,
		     srcx, srcy, x, y, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftTextRender16BE (Display	    *dpy,
		   int		    op,
		   Picture	    src,
		   XftFont	    *pub,
		   Picture	    dst,
		   int		    srcx,
		   int		    srcy,
		   int		    x,
		   int		    y,
		   _Xconst FcChar8  *string,
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
	glyphs[i] = XftCharIndex (dpy, pub,
				  (FcChar32)((string[i*2]<<8) | string[i*2+1]));
    XftGlyphRender (dpy, op, src, pub, dst,
		     srcx, srcy, x, y, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftTextRender16LE (Display	    *dpy,
		   int		    op,
		   Picture	    src,
		   XftFont	    *pub,
		   Picture	    dst,
		   int		    srcx,
		   int		    srcy,
		   int		    x,
		   int		    y,
		   _Xconst FcChar8  *string,
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
	glyphs[i] = XftCharIndex (dpy, pub,
				  (FcChar32)(string[i*2] | (string[i*2+1]<<8)));
    XftGlyphRender (dpy, op, src, pub, dst,
		     srcx, srcy, x, y, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftTextRender32 (Display	    *dpy,
		 int		    op,
		 Picture	    src,
		 XftFont	    *pub,
		 Picture	    dst,
		 int		    srcx,
		 int		    srcy,
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
	glyphs[i] = XftCharIndex (dpy, pub, string[i]);
    XftGlyphRender (dpy, op, src, pub, dst,
		     srcx, srcy, x, y, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftTextRender32BE (Display	    *dpy,
		   int		    op,
		   Picture	    src,
		   XftFont	    *pub,
		   Picture	    dst,
		   int		    srcx,
		   int		    srcy,
		   int		    x,
		   int		    y,
		   _Xconst FcChar8  *string,
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
	glyphs[i] = XftCharIndex (dpy, pub,
				  (FcChar32)((string[i*4] << 24) |
					     (string[i*4+1] << 16) |
					     (string[i*4+2] << 8) |
					     (string[i*4+3])));
    XftGlyphRender (dpy, op, src, pub, dst,
		     srcx, srcy, x, y, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftTextRender32LE (Display	    *dpy,
		   int		    op,
		   Picture	    src,
		   XftFont	    *pub,
		   Picture	    dst,
		   int		    srcx,
		   int		    srcy,
		   int		    x,
		   int		    y,
		   _Xconst FcChar8  *string,
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
	glyphs[i] = XftCharIndex (dpy, pub,
				  (FcChar32)((string[i*4]) |
					     (string[i*4+1] << 8) |
					     (string[i*4+2] << 16) |
					     (string[i*4+3] << 24)));
    XftGlyphRender (dpy, op, src, pub, dst,
		     srcx, srcy, x, y, glyphs, len);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftTextRenderUtf8 (Display	    *dpy,
		   int		    op,
		   Picture	    src,
		   XftFont	    *pub,
		   Picture	    dst,
		   int		    srcx,
		   int		    srcy,
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
	glyphs[i++] = XftCharIndex (dpy, pub, ucs4);
	string += l;
	len -= l;
    }
    XftGlyphRender (dpy, op, src, pub, dst,
		     srcx, srcy, x, y, glyphs, i);
    if (glyphs != glyphs_local)
	free (glyphs);
}

_X_EXPORT void
XftTextRenderUtf16 (Display	    *dpy,
		    int		    op _X_UNUSED,
		    Picture	    src,
		    XftFont	    *pub,
		    Picture	    dst,
		    int		    srcx,
		    int		    srcy,
		    int		    x,
		    int		    y,
		    _Xconst FcChar8 *string,
		    FcEndian	    endian,
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
	glyphs[i++] = XftCharIndex (dpy, pub, ucs4);
	string += l;
	len -= l;
    }
    XftGlyphRender (dpy, PictOpOver, src, pub, dst,
		     srcx, srcy, x, y, glyphs, i);
    if (glyphs != glyphs_local)
	free (glyphs);
}
