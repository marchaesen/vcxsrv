/*
 * Copyright © 1998 Keith Packard
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

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdlib.h>

#include "fb.h"

const GCFuncs fbGCFuncs = {
    fbValidateGC,
    miChangeGC,
    miCopyGC,
    miDestroyGC,
    miChangeClip,
    miDestroyClip,
    miCopyClip,
};

const GCOps fbGCOps = {
    fbFillSpans,
    fbSetSpans,
    fbPutImage,
    fbCopyArea,
    fbCopyPlane,
    fbPolyPoint,
    fbPolyLine,
    fbPolySegment,
    fbPolyRectangle,
    fbPolyArc,
    miFillPolygon,
    fbPolyFillRect,
    fbPolyFillArc,
    miPolyText8,
    miPolyText16,
    miImageText8,
    miImageText16,
    fbImageGlyphBlt,
    fbPolyGlyphBlt,
    fbPushPixels
};

Bool
fbCreateGC(GCPtr pGC)
{
    pGC->ops = (GCOps *) &fbGCOps;
    pGC->funcs = (GCFuncs *) &fbGCFuncs;

    /* fb wants to translate before scan conversion */
    pGC->miTranslate = 1;
    pGC->fExpose = 1;

    fbGetGCPrivate(pGC)->bpp = BitsPerPixel(pGC->depth);
    return TRUE;
}

/*
 * Pad pixmap to FB_UNIT bits wide
 */
void
fbPadPixmap(PixmapPtr pPixmap)
{
    int width;
    FbBits *bits;
    FbBits b;
    FbBits mask;
    int height;
    int w;
    int stride;
    int bpp;
    _X_UNUSED int xOff, yOff;

    fbGetDrawable(&pPixmap->drawable, bits, stride, bpp, xOff, yOff);

    width = pPixmap->drawable.width * pPixmap->drawable.bitsPerPixel;
    height = pPixmap->drawable.height;
    mask = FbBitsMask(0, width);
    while (height--) {
        b = READ(bits) & mask;
        w = width;
        while (w < FB_UNIT) {
            b = b | FbScrRight(b, w);
            w <<= 1;
        }
        WRITE(bits, b);
        bits += stride;
    }

    fbFinishAccess(&pPixmap->drawable);
}

/*
 * Verify that 'bits' repeats every 'len' bits
 */
static Bool
fbBitsRepeat(FbBits bits, int len, int width)
{
    FbBits mask = FbBitsMask(0, len);
    FbBits orig = bits & mask;
    int i;

    if (width > FB_UNIT)
        width = FB_UNIT;
    for (i = 0; i < width / len; i++) {
        if ((bits & mask) != orig)
            return FALSE;
        bits = FbScrLeft(bits, len);
    }
    return TRUE;
}

/*
 * Check whether an entire bitmap line is a repetition of
 * the first 'len' bits
 */
static Bool
fbLineRepeat(FbBits * bits, int len, int width)
{
    FbBits first = bits[0];

    if (!fbBitsRepeat(first, len, width))
        return FALSE;
    width = (width + FB_UNIT - 1) >> FB_SHIFT;
    bits++;
    while (--width)
        if (READ(bits) != first)
            return FALSE;
    return TRUE;
}

/*
 * The even stipple code wants the first FB_UNIT/bpp bits on
 * each scanline to represent the entire stipple
 */
static Bool
fbCanEvenStipple(PixmapPtr pStipple, int bpp)
{
    int len = FB_UNIT / bpp;
    FbBits *bits;
    int stride;
    int stip_bpp;
    _X_UNUSED int stipXoff, stipYoff;
    int h;

    /* can't even stipple 24bpp drawables */
    if ((bpp & (bpp - 1)) != 0)
        return FALSE;
    /* make sure the stipple width is a multiple of the even stipple width */
    if (pStipple->drawable.width % len != 0)
        return FALSE;
    fbGetDrawable(&pStipple->drawable, bits, stride, stip_bpp, stipXoff,
                  stipYoff);
    h = pStipple->drawable.height;
    /* check to see that the stipple repeats horizontally */
    while (h--) {
        if (!fbLineRepeat(bits, len, pStipple->drawable.width)) {
            fbFinishAccess(&pStipple->drawable);
            return FALSE;
        }
        bits += stride;
    }
    fbFinishAccess(&pStipple->drawable);
    return TRUE;
}

void
fbValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr pDrawable)
{
    FbGCPrivPtr pPriv = fbGetGCPrivate(pGC);
    FbBits mask;

    /*
     * if the client clip is different or moved OR the subwindowMode has
     * changed OR the window's clip has changed since the last validation
     * we need to recompute the composite clip 
     */

    if ((changes &
         (GCClipXOrigin | GCClipYOrigin | GCClipMask | GCSubwindowMode)) ||
        (pDrawable->serialNumber != (pGC->serialNumber & DRAWABLE_SERIAL_BITS))
        ) {
        miComputeCompositeClip(pGC, pDrawable);
    }

    if (pPriv->bpp != pDrawable->bitsPerPixel) {
        changes |= GCStipple | GCForeground | GCBackground | GCPlaneMask;
        pPriv->bpp = pDrawable->bitsPerPixel;
    }
    if ((changes & GCTile) && fbGetRotatedPixmap(pGC)) {
        (*pGC->pScreen->DestroyPixmap) (fbGetRotatedPixmap(pGC));
        fbGetRotatedPixmap(pGC) = 0;
    }

    if (pGC->fillStyle == FillTiled) {
        PixmapPtr pOldTile, pNewTile;

        pOldTile = pGC->tile.pixmap;
        if (pOldTile->drawable.bitsPerPixel != pDrawable->bitsPerPixel) {
            pNewTile = fbGetRotatedPixmap(pGC);
            if (!pNewTile ||
                pNewTile->drawable.bitsPerPixel != pDrawable->bitsPerPixel) {
                if (pNewTile)
                    (*pGC->pScreen->DestroyPixmap) (pNewTile);
                pNewTile =
                    fb24_32ReformatTile(pOldTile, pDrawable->bitsPerPixel);
            }
            if (pNewTile) {
                fbGetRotatedPixmap(pGC) = pOldTile;
                pGC->tile.pixmap = pNewTile;
                changes |= GCTile;
            }
        }
    }
    if (changes & GCTile) {
        if (!pGC->tileIsPixel &&
            FbEvenTile(pGC->tile.pixmap->drawable.width *
                       pDrawable->bitsPerPixel))
            fbPadPixmap(pGC->tile.pixmap);
    }
    if (changes & GCStipple) {
        pPriv->evenStipple = FALSE;

        if (pGC->stipple) {

            /* can we do an even stipple ?? */
            if (FbEvenStip(pGC->stipple->drawable.width,
                           pDrawable->bitsPerPixel) &&
                (fbCanEvenStipple(pGC->stipple, pDrawable->bitsPerPixel)))
                pPriv->evenStipple = TRUE;

            if (pGC->stipple->drawable.width * pDrawable->bitsPerPixel <
                FB_UNIT)
                fbPadPixmap(pGC->stipple);
        }
    }
    /*
     * Recompute reduced rop values
     */
    if (changes & (GCForeground | GCBackground | GCPlaneMask | GCFunction)) {
        int s;
        FbBits depthMask;

        mask = FbFullMask(pDrawable->bitsPerPixel);
        depthMask = FbFullMask(pDrawable->depth);

        pPriv->fg = pGC->fgPixel & mask;
        pPriv->bg = pGC->bgPixel & mask;

        if ((pGC->planemask & depthMask) == depthMask)
            pPriv->pm = mask;
        else
            pPriv->pm = pGC->planemask & mask;

        s = pDrawable->bitsPerPixel;
        while (s < FB_UNIT) {
            pPriv->fg |= pPriv->fg << s;
            pPriv->bg |= pPriv->bg << s;
            pPriv->pm |= pPriv->pm << s;
            s <<= 1;
        }
        pPriv->and = fbAnd(pGC->alu, pPriv->fg, pPriv->pm);
        pPriv->xor = fbXor(pGC->alu, pPriv->fg, pPriv->pm);
        pPriv->bgand = fbAnd(pGC->alu, pPriv->bg, pPriv->pm);
        pPriv->bgxor = fbXor(pGC->alu, pPriv->bg, pPriv->pm);
    }
    if (changes & GCDashList) {
        unsigned short n = pGC->numInDashList;
        unsigned char *dash = pGC->dash;
        unsigned int dashLength = 0;

        while (n--)
            dashLength += (unsigned int) *dash++;
        pPriv->dashLength = dashLength;
    }
}
