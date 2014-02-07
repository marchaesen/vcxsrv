/*
 * Copyright © 2009 Intel Corporation
 * Copyright © 1998 Keith Packard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Zhigang Gong <zhigang.gong@gmail.com>
 *
 */

#include "glamor_priv.h"

static Bool
_glamor_image_glyph_blt(DrawablePtr pDrawable, GCPtr pGC,
                        int x, int y, unsigned int nglyph,
                        CharInfoPtr *ppci, void *pglyphBase, Bool fallback)
{
    if (!fallback && glamor_ddx_fallback_check_pixmap(pDrawable)
        && glamor_ddx_fallback_check_gc(pGC))
        return FALSE;

    miImageGlyphBlt(pDrawable, pGC, x, y, nglyph, ppci, pglyphBase);
    return TRUE;
}

void
glamor_image_glyph_blt(DrawablePtr pDrawable, GCPtr pGC,
                       int x, int y, unsigned int nglyph,
                       CharInfoPtr *ppci, void *pglyphBase)
{
    _glamor_image_glyph_blt(pDrawable, pGC, x, y, nglyph, ppci, pglyphBase,
                            TRUE);
}

Bool
glamor_image_glyph_blt_nf(DrawablePtr pDrawable, GCPtr pGC,
                          int x, int y, unsigned int nglyph,
                          CharInfoPtr *ppci, void *pglyphBase)
{
    return _glamor_image_glyph_blt(pDrawable, pGC, x, y, nglyph, ppci,
                                   pglyphBase, FALSE);
}

static Bool
_glamor_poly_glyph_blt(DrawablePtr pDrawable, GCPtr pGC,
                       int x, int y, unsigned int nglyph,
                       CharInfoPtr *ppci, void *pglyphBase, Bool fallback)
{
    if (!fallback && glamor_ddx_fallback_check_pixmap(pDrawable)
        && glamor_ddx_fallback_check_gc(pGC))
        return FALSE;

    miPolyGlyphBlt(pDrawable, pGC, x, y, nglyph, ppci, pglyphBase);
    return TRUE;
}

void
glamor_poly_glyph_blt(DrawablePtr pDrawable, GCPtr pGC,
                      int x, int y, unsigned int nglyph,
                      CharInfoPtr *ppci, void *pglyphBase)
{
    _glamor_poly_glyph_blt(pDrawable, pGC, x, y, nglyph, ppci, pglyphBase,
                           TRUE);
}

Bool
glamor_poly_glyph_blt_nf(DrawablePtr pDrawable, GCPtr pGC,
                         int x, int y, unsigned int nglyph,
                         CharInfoPtr *ppci, void *pglyphBase)
{
    return _glamor_poly_glyph_blt(pDrawable, pGC, x, y, nglyph, ppci,
                                  pglyphBase, FALSE);
}

static Bool
_glamor_push_pixels(GCPtr pGC, PixmapPtr pBitmap,
                    DrawablePtr pDrawable, int w, int h, int x, int y,
                    Bool fallback)
{
    if (!fallback && glamor_ddx_fallback_check_pixmap(pDrawable)
        && glamor_ddx_fallback_check_pixmap(&pBitmap->drawable)
        && glamor_ddx_fallback_check_gc(pGC))
        return FALSE;

    miPushPixels(pGC, pBitmap, pDrawable, w, h, x, y);
    return TRUE;
}

void
glamor_push_pixels(GCPtr pGC, PixmapPtr pBitmap,
                   DrawablePtr pDrawable, int w, int h, int x, int y)
{
    _glamor_push_pixels(pGC, pBitmap, pDrawable, w, h, x, y, TRUE);
}

Bool
glamor_push_pixels_nf(GCPtr pGC, PixmapPtr pBitmap,
                      DrawablePtr pDrawable, int w, int h, int x, int y)
{
    return _glamor_push_pixels(pGC, pBitmap, pDrawable, w, h, x, y, FALSE);
}
