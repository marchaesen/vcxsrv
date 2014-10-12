/*
 * Copyright 2001-2004 Red Hat Inc., Durham, North Carolina.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation on the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT.  IN NO EVENT SHALL RED HAT AND/OR THEIR SUPPLIERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Authors:
 *   Kevin E. Martin <kem@redhat.com>
 *
 */

/** \file
 * This file provides support for GC operations. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxsync.h"
#include "dmxgc.h"
#include "dmxgcops.h"
#include "dmxwindow.h"
#include "dmxpixmap.h"

#include "mi.h"
#include "gcstruct.h"
#include "pixmapstr.h"
#include "dixfontstr.h"

#ifdef PANORAMIX
#include "panoramiXsrv.h"
#endif

#define DMX_GCOPS_SET_DRAWABLE(_pDraw, _draw)				\
do {									\
    if ((_pDraw)->type == DRAWABLE_WINDOW) {				\
	dmxWinPrivPtr  pWinPriv =					\
	    DMX_GET_WINDOW_PRIV((WindowPtr)(_pDraw));			\
	(_draw) = (Drawable)pWinPriv->window;				\
    } else {								\
	dmxPixPrivPtr  pPixPriv =					\
	    DMX_GET_PIXMAP_PRIV((PixmapPtr)(_pDraw));			\
	(_draw) = (Drawable)pPixPriv->pixmap;				\
    }									\
} while (0)

#define DMX_GCOPS_OFFSCREEN(_pDraw)					\
    (!dmxScreens[(_pDraw)->pScreen->myNum].beDisplay ||			\
     (dmxOffScreenOpt &&						\
      (_pDraw)->type == DRAWABLE_WINDOW &&				\
      (DMX_GET_WINDOW_PRIV((WindowPtr)(_pDraw))->offscreen ||		\
       !DMX_GET_WINDOW_PRIV((WindowPtr)(_pDraw))->window)))

/** Fill spans -- this function should never be called. */
void
dmxFillSpans(DrawablePtr pDrawable, GCPtr pGC,
             int nInit, DDXPointPtr pptInit, int *pwidthInit, int fSorted)
{
    /* Error -- this should never happen! */
}

/** Set spans -- this function should never be called. */
void
dmxSetSpans(DrawablePtr pDrawable, GCPtr pGC,
            char *psrc, DDXPointPtr ppt, int *pwidth, int nspans, int fSorted)
{
    /* Error -- this should never happen! */
}

/** Transfer \a pBits image to back-end server associated with \a
 *  pDrawable's screen.  If primitive subdivision optimization is
 *  enabled, then only transfer the sections of \a pBits that are
 *  visible (i.e., not-clipped) to the back-end server. */
void
dmxPutImage(DrawablePtr pDrawable, GCPtr pGC,
            int depth, int x, int y, int w, int h,
            int leftPad, int format, char *pBits)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    XImage *img;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    img = XCreateImage(dmxScreen->beDisplay,
                       dmxScreen->beVisuals[dmxScreen->beDefVisualIndex].visual,
                       depth, format, leftPad, pBits, w, h,
                       BitmapPad(dmxScreen->beDisplay),
                       (format == ZPixmap) ?
                       PixmapBytePad(w, depth) : BitmapBytePad(w + leftPad));

    if (img) {
        Drawable draw;

        DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

        if (dmxSubdividePrimitives && pGC->pCompositeClip) {
            RegionPtr pSubImages;
            RegionPtr pClip;
            BoxRec box;
            BoxPtr pBox;
            int nBox;

            box.x1 = x;
            box.y1 = y;
            box.x2 = x + w;
            box.y2 = y + h;
            pSubImages = RegionCreate(&box, 1);

            pClip = RegionCreate(NullBox, 1);
            RegionCopy(pClip, pGC->pCompositeClip);
            RegionTranslate(pClip, -pDrawable->x, -pDrawable->y);
            RegionIntersect(pSubImages, pSubImages, pClip);

            nBox = RegionNumRects(pSubImages);
            pBox = RegionRects(pSubImages);

            while (nBox--) {
                XPutImage(dmxScreen->beDisplay, draw, pGCPriv->gc, img,
                          pBox->x1 - box.x1,
                          pBox->y1 - box.y1,
                          pBox->x1,
                          pBox->y1, pBox->x2 - pBox->x1, pBox->y2 - pBox->y1);
                pBox++;
            }
            RegionDestroy(pClip);
            RegionDestroy(pSubImages);
        }
        else {
            XPutImage(dmxScreen->beDisplay, draw, pGCPriv->gc,
                      img, 0, 0, x, y, w, h);
        }
        XFree(img);             /* Use XFree instead of XDestroyImage
                                 * because pBits is passed in from the
                                 * caller. */

        dmxSync(dmxScreen, FALSE);
    }
    else {
        /* Error -- this should not happen! */
    }
}

/** Copy area from \a pSrc drawable to \a pDst drawable on the back-end
 *  server associated with \a pSrc drawable's screen.  If the offscreen
 *  optimization is enabled, only copy when both \a pSrc and \a pDst are
 *  at least partially visible. */
RegionPtr
dmxCopyArea(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
            int srcx, int srcy, int w, int h, int dstx, int dsty)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pSrc->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable srcDraw, dstDraw;

    if (DMX_GCOPS_OFFSCREEN(pSrc) || DMX_GCOPS_OFFSCREEN(pDst))
        return miHandleExposures(pSrc, pDst, pGC, srcx, srcy, w, h,
                                 dstx, dsty);

    DMX_GCOPS_SET_DRAWABLE(pSrc, srcDraw);
    DMX_GCOPS_SET_DRAWABLE(pDst, dstDraw);

    XCopyArea(dmxScreen->beDisplay, srcDraw, dstDraw, pGCPriv->gc,
              srcx, srcy, w, h, dstx, dsty);
    dmxSync(dmxScreen, FALSE);

    return miHandleExposures(pSrc, pDst, pGC, srcx, srcy, w, h, dstx, dsty);
}

/** Copy plane number \a bitPlane from \a pSrc drawable to \a pDst
 *  drawable on the back-end server associated with \a pSrc drawable's
 *  screen.  If the offscreen optimization is enabled, only copy when
 *  both \a pSrc and \a pDst are at least partially visible. */
RegionPtr
dmxCopyPlane(DrawablePtr pSrc, DrawablePtr pDst, GCPtr pGC,
             int srcx, int srcy, int width, int height,
             int dstx, int dsty, unsigned long bitPlane)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pSrc->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable srcDraw, dstDraw;

    if (DMX_GCOPS_OFFSCREEN(pSrc) || DMX_GCOPS_OFFSCREEN(pDst))
        return miHandleExposures(pSrc, pDst, pGC, srcx, srcy, width, height,
                                 dstx, dsty);

    DMX_GCOPS_SET_DRAWABLE(pSrc, srcDraw);
    DMX_GCOPS_SET_DRAWABLE(pDst, dstDraw);

    XCopyPlane(dmxScreen->beDisplay, srcDraw, dstDraw, pGCPriv->gc,
               srcx, srcy, width, height, dstx, dsty, bitPlane);
    dmxSync(dmxScreen, FALSE);

    return miHandleExposures(pSrc, pDst, pGC, srcx, srcy, width, height,
                             dstx, dsty);
}

/** Render list of points, \a pptInit in \a pDrawable on the back-end
 *  server associated with \a pDrawable's screen.  If the offscreen
 *  optimization is enabled, only draw when \a pDrawable is at least
 *  partially visible. */
void
dmxPolyPoint(DrawablePtr pDrawable, GCPtr pGC,
             int mode, int npt, DDXPointPtr pptInit)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable draw;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

    XDrawPoints(dmxScreen->beDisplay, draw, pGCPriv->gc,
                (XPoint *) pptInit, npt, mode);
    dmxSync(dmxScreen, FALSE);
}

/** Render list of connected lines, \a pptInit in \a pDrawable on the
 *  back-end server associated with \a pDrawable's screen.  If the
 *  offscreen optimization is enabled, only draw when \a pDrawable is at
 *  least partially visible. */
void
dmxPolylines(DrawablePtr pDrawable, GCPtr pGC,
             int mode, int npt, DDXPointPtr pptInit)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable draw;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

    XDrawLines(dmxScreen->beDisplay, draw, pGCPriv->gc,
               (XPoint *) pptInit, npt, mode);
    dmxSync(dmxScreen, FALSE);
}

/** Render list of disjoint segments, \a pSegs in \a pDrawable on the
 *  back-end server associated with \a pDrawable's screen.  If the
 *  offscreen optimization is enabled, only draw when \a pDrawable is at
 *  least partially visible. */
void
dmxPolySegment(DrawablePtr pDrawable, GCPtr pGC, int nseg, xSegment * pSegs)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable draw;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

    XDrawSegments(dmxScreen->beDisplay, draw, pGCPriv->gc,
                  (XSegment *) pSegs, nseg);
    dmxSync(dmxScreen, FALSE);
}

/** Render list of rectangle outlines, \a pRects in \a pDrawable on the
 *  back-end server associated with \a pDrawable's screen.  If the
 *  offscreen optimization is enabled, only draw when \a pDrawable is at
 *  least partially visible. */
void
dmxPolyRectangle(DrawablePtr pDrawable, GCPtr pGC,
                 int nrects, xRectangle *pRects)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable draw;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

    XDrawRectangles(dmxScreen->beDisplay, draw, pGCPriv->gc,
                    (XRectangle *) pRects, nrects);

    dmxSync(dmxScreen, FALSE);
}

/** Render list of arc outlines, \a parcs in \a pDrawable on the
 *  back-end server associated with \a pDrawable's screen.  If the
 *  offscreen optimization is enabled, only draw when \a pDrawable is at
 *  least partially visible. */
void
dmxPolyArc(DrawablePtr pDrawable, GCPtr pGC, int narcs, xArc * parcs)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable draw;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

    XDrawArcs(dmxScreen->beDisplay, draw, pGCPriv->gc, (XArc *) parcs, narcs);
    dmxSync(dmxScreen, FALSE);
}

/** Render a filled polygons in \a pDrawable on the back-end server
 *  associated with \a pDrawable's screen.  If the offscreen
 *  optimization is enabled, only draw when \a pDrawable is at least
 *  partially visible. */
void
dmxFillPolygon(DrawablePtr pDrawable, GCPtr pGC,
               int shape, int mode, int count, DDXPointPtr pPts)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable draw;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

    XFillPolygon(dmxScreen->beDisplay, draw, pGCPriv->gc,
                 (XPoint *) pPts, count, shape, mode);
    dmxSync(dmxScreen, FALSE);
}

/** Render list of filled rectangles, \a prectInit in \a pDrawable on
 *  the back-end server associated with \a pDrawable's screen.  If the
 *  offscreen optimization is enabled, only draw when \a pDrawable is at
 *  least partially visible. */
void
dmxPolyFillRect(DrawablePtr pDrawable, GCPtr pGC,
                int nrectFill, xRectangle *prectInit)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable draw;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

    XFillRectangles(dmxScreen->beDisplay, draw, pGCPriv->gc,
                    (XRectangle *) prectInit, nrectFill);
    dmxSync(dmxScreen, FALSE);
}

/** Render list of filled arcs, \a parcs in \a pDrawable on the back-end
 *  server associated with \a pDrawable's screen.  If the offscreen
 *  optimization is enabled, only draw when \a pDrawable is at least
 *  partially visible. */
void
dmxPolyFillArc(DrawablePtr pDrawable, GCPtr pGC, int narcs, xArc * parcs)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable draw;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

    XFillArcs(dmxScreen->beDisplay, draw, pGCPriv->gc, (XArc *) parcs, narcs);
    dmxSync(dmxScreen, FALSE);
}

/** Render string of 8-bit \a chars (foreground only) in \a pDrawable on
 *  the back-end server associated with \a pDrawable's screen.  If the
 *  offscreen optimization is enabled, only draw when \a pDrawable is at
 *  least partially visible. */
int
dmxPolyText8(DrawablePtr pDrawable, GCPtr pGC,
             int x, int y, int count, char *chars)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    unsigned long n, i;
    int w;
    CharInfoPtr charinfo[255];
    Drawable draw;

    GetGlyphs(pGC->font, (unsigned long) count, (unsigned char *) chars,
              Linear8Bit, &n, charinfo);

    /* Calculate text width */
    w = 0;
    for (i = 0; i < n; i++)
        w += charinfo[i]->metrics.characterWidth;

    if (n != 0 && !DMX_GCOPS_OFFSCREEN(pDrawable)) {
        DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

        XDrawString(dmxScreen->beDisplay, draw, pGCPriv->gc,
                    x, y, chars, count);
        dmxSync(dmxScreen, FALSE);
    }

    return x + w;
}

/** Render string of 16-bit \a chars (foreground only) in \a pDrawable
 *  on the back-end server associated with \a pDrawable's screen.  If
 *  the offscreen optimization is enabled, only draw when \a pDrawable
 *  is at least partially visible. */
int
dmxPolyText16(DrawablePtr pDrawable, GCPtr pGC,
              int x, int y, int count, unsigned short *chars)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    unsigned long n, i;
    int w;
    CharInfoPtr charinfo[255];
    Drawable draw;

    GetGlyphs(pGC->font, (unsigned long) count, (unsigned char *) chars,
              (FONTLASTROW(pGC->font) == 0) ? Linear16Bit : TwoD16Bit,
              &n, charinfo);

    /* Calculate text width */
    w = 0;
    for (i = 0; i < n; i++)
        w += charinfo[i]->metrics.characterWidth;

    if (n != 0 && !DMX_GCOPS_OFFSCREEN(pDrawable)) {
        DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

        XDrawString16(dmxScreen->beDisplay, draw, pGCPriv->gc,
                      x, y, (XChar2b *) chars, count);
        dmxSync(dmxScreen, FALSE);
    }

    return x + w;
}

/** Render string of 8-bit \a chars (both foreground and background) in
 *  \a pDrawable on the back-end server associated with \a pDrawable's
 *  screen.  If the offscreen optimization is enabled, only draw when \a
 *  pDrawable is at least partially visible. */
void
dmxImageText8(DrawablePtr pDrawable, GCPtr pGC,
              int x, int y, int count, char *chars)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable draw;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

    XDrawImageString(dmxScreen->beDisplay, draw, pGCPriv->gc,
                     x, y, chars, count);
    dmxSync(dmxScreen, FALSE);
}

/** Render string of 16-bit \a chars (both foreground and background) in
 *  \a pDrawable on the back-end server associated with \a pDrawable's
 *  screen.  If the offscreen optimization is enabled, only draw when \a
 *  pDrawable is at least partially visible. */
void
dmxImageText16(DrawablePtr pDrawable, GCPtr pGC,
               int x, int y, int count, unsigned short *chars)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Drawable draw;

    if (DMX_GCOPS_OFFSCREEN(pDrawable))
        return;

    DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);

    XDrawImageString16(dmxScreen->beDisplay, draw, pGCPriv->gc,
                       x, y, (XChar2b *) chars, count);
    dmxSync(dmxScreen, FALSE);
}

/** Image Glyph Blt -- this function should never be called. */
void
dmxImageGlyphBlt(DrawablePtr pDrawable, GCPtr pGC,
                 int x, int y, unsigned int nglyph,
                 CharInfoPtr * ppci, void *pglyphBase)
{
    /* Error -- this should never happen! */
}

/** Poly Glyph Blt -- this function should never be called. */
void
dmxPolyGlyphBlt(DrawablePtr pDrawable, GCPtr pGC,
                int x, int y, unsigned int nglyph,
                CharInfoPtr * ppci, void *pglyphBase)
{
    /* Error -- this should never happen! */
}

/** Push Pixels -- this function should never be called. */
void
dmxPushPixels(GCPtr pGC, PixmapPtr pBitMap, DrawablePtr pDst,
              int w, int h, int x, int y)
{
    /* Error -- this should never happen! */
}

/**********************************************************************
 * Miscellaneous drawing commands
 */

/** When Xinerama is active, the client pixmaps are always obtained from
 * screen 0.  When screen 0 is detached, the pixmaps must be obtained
 * from any other screen that is not detached.  Usually, this is screen
 * 1. */
static DMXScreenInfo *
dmxFindAlternatePixmap(DrawablePtr pDrawable, XID *draw)
{
#ifdef PANORAMIX
    PanoramiXRes *pXinPix;
    int i;
    DMXScreenInfo *dmxScreen;

    if (noPanoramiXExtension)
        return NULL;
    if (pDrawable->type != DRAWABLE_PIXMAP)
        return NULL;

    if (Success != dixLookupResourceByType((void **) &pXinPix,
                                           pDrawable->id, XRT_PIXMAP,
                                           NullClient, DixUnknownAccess))
        return NULL;

    FOR_NSCREENS_FORWARD_SKIP(i) {
        dmxScreen = &dmxScreens[i];
        if (dmxScreen->beDisplay) {
            PixmapPtr pSrc;
            dmxPixPrivPtr pSrcPriv;

            dixLookupResourceByType((void **) &pSrc, pXinPix->info[i].id,
                                    RT_PIXMAP, NullClient, DixUnknownAccess);
            pSrcPriv = DMX_GET_PIXMAP_PRIV(pSrc);
            if (pSrcPriv->pixmap) {
                *draw = pSrcPriv->pixmap;
                return dmxScreen;
            }
        }
    }
#endif
    return NULL;
}

/** Get an image from the back-end server associated with \a pDrawable's
 *  screen.  If \a pDrawable is a window, it must be viewable to get an
 *  image from it.  If it is not viewable, then get the image from the
 *  first ancestor of \a pDrawable that is viewable.  If no viewable
 *  ancestor is found, then simply return without getting an image.  */
void
dmxGetImage(DrawablePtr pDrawable, int sx, int sy, int w, int h,
            unsigned int format, unsigned long planeMask, char *pdstLine)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pDrawable->pScreen->myNum];
    XImage *img;
    Drawable draw;

    /* Cannot get image from unviewable window */
    if (pDrawable->type == DRAWABLE_WINDOW) {
        WindowPtr pWindow = (WindowPtr) pDrawable;

        if (!pWindow->viewable) {
            while (!pWindow->viewable && pWindow->parent) {
                sx += pWindow->origin.x - wBorderWidth(pWindow);
                sx += pWindow->origin.y - wBorderWidth(pWindow);
                pWindow = pWindow->parent;
            }
            if (!pWindow->viewable) {
                return;
            }
        }
        DMX_GCOPS_SET_DRAWABLE(&pWindow->drawable, draw);
        if (DMX_GCOPS_OFFSCREEN(&pWindow->drawable))
            return;
    }
    else {
        DMX_GCOPS_SET_DRAWABLE(pDrawable, draw);
        if (DMX_GCOPS_OFFSCREEN(pDrawable)) {
            /* Try to find the pixmap on a non-detached Xinerama screen */
            dmxScreen = dmxFindAlternatePixmap(pDrawable, &draw);
            if (!dmxScreen)
                return;
        }
    }

    img = XGetImage(dmxScreen->beDisplay, draw,
                    sx, sy, w, h, planeMask, format);
    if (img) {
        int len = img->bytes_per_line * img->height;

        memmove(pdstLine, img->data, len);
        XDestroyImage(img);
    }

    dmxSync(dmxScreen, FALSE);
}

/** Get Spans -- this function should never be called. */
void
dmxGetSpans(DrawablePtr pDrawable, int wMax,
            DDXPointPtr ppt, int *pwidth, int nspans, char *pdstStart)
{
    /* Error -- this should never happen! */
}
