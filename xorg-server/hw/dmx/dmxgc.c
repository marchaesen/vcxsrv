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
 * This file provides support for GCs. */

#ifdef HAVE_DMX_CONFIG_H
#include <dmx-config.h>
#endif

#include "dmx.h"
#include "dmxsync.h"
#include "dmxgc.h"
#include "dmxgcops.h"
#include "dmxpixmap.h"
#include "dmxfont.h"

#include "gcstruct.h"
#include "pixmapstr.h"
#include "migc.h"

static const GCFuncs dmxGCFuncs = {
    dmxValidateGC,
    dmxChangeGC,
    dmxCopyGC,
    dmxDestroyGC,
    dmxChangeClip,
    dmxDestroyClip,
    dmxCopyClip,
};

static const GCOps dmxGCOps = {
    dmxFillSpans,
    dmxSetSpans,
    dmxPutImage,
    dmxCopyArea,
    dmxCopyPlane,
    dmxPolyPoint,
    dmxPolylines,
    dmxPolySegment,
    dmxPolyRectangle,
    dmxPolyArc,
    dmxFillPolygon,
    dmxPolyFillRect,
    dmxPolyFillArc,
    dmxPolyText8,
    dmxPolyText16,
    dmxImageText8,
    dmxImageText16,
    dmxImageGlyphBlt,
    dmxPolyGlyphBlt,
    dmxPushPixels
};

/** Initialize the GC on \a pScreen */
Bool
dmxInitGC(ScreenPtr pScreen)
{
    if (!dixRegisterPrivateKey
        (&dmxGCPrivateKeyRec, PRIVATE_GC, sizeof(dmxGCPrivRec)))
        return FALSE;
    return TRUE;
}

/** Create the GC on the back-end server. */
void
dmxBECreateGC(ScreenPtr pScreen, GCPtr pGC)
{
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    int i;

    for (i = 0; i < dmxScreen->beNumPixmapFormats; i++) {
        if (pGC->depth == dmxScreen->bePixmapFormats[i].depth) {
            unsigned long mask;
            XGCValues gcvals;

            mask = GCGraphicsExposures;
            gcvals.graphics_exposures = FALSE;

            /* Create GC in the back-end servers */
            pGCPriv->gc = XCreateGC(dmxScreen->beDisplay,
                                    dmxScreen->scrnDefDrawables[i],
                                    mask, &gcvals);
            break;
        }
    }
}

/** Create a graphics context on the back-end server associated /a pGC's
 *  screen. */
Bool
dmxCreateGC(GCPtr pGC)
{
    ScreenPtr pScreen = pGC->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    Bool ret;

    DMX_UNWRAP(CreateGC, dmxScreen, pScreen);
    if ((ret = pScreen->CreateGC(pGC))) {
        /* Save the old funcs */
        pGCPriv->funcs = pGC->funcs;
        pGCPriv->ops = NULL;

        pGC->funcs = &dmxGCFuncs;

        if (dmxScreen->beDisplay) {
            dmxBECreateGC(pScreen, pGC);
        }
        else {
            pGCPriv->gc = NULL;
        }

        /* Check for "magic special case"
         * 1. see CreateGC in dix/gc.c for more info
         * 2. see dmxChangeGC for more info
         */
        pGCPriv->msc = (!pGC->tileIsPixel && !pGC->tile.pixmap);
    }
    DMX_WRAP(CreateGC, dmxCreateGC, dmxScreen, pScreen);

    return ret;
}

/** Validate a graphics context, \a pGC, locally in the DMX server and
 *  recompute the composite clip, if necessary. */
void
dmxValidateGC(GCPtr pGC, unsigned long changes, DrawablePtr pDrawable)
{
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);

    DMX_GC_FUNC_PROLOGUE(pGC);
#if 0
    pGC->funcs->ValidateGC(pGC, changes, pDrawable);
#endif

    if (pDrawable->type == DRAWABLE_WINDOW ||
        pDrawable->type == DRAWABLE_PIXMAP) {
        /* Save the old ops, since we're about to change the ops in the
         * epilogue.
         */
        pGCPriv->ops = pGC->ops;
    }
    else {
        pGCPriv->ops = NULL;
    }

    /* If the client clip is different or moved OR the subwindowMode has
     * changed OR the window's clip has changed since the last
     * validation, then we need to recompute the composite clip.
     */
    if ((changes & (GCClipXOrigin |
                    GCClipYOrigin |
                    GCClipMask |
                    GCSubwindowMode)) ||
        (pDrawable->serialNumber !=
         (pGC->serialNumber & DRAWABLE_SERIAL_BITS))) {
        miComputeCompositeClip(pGC, pDrawable);
    }

    DMX_GC_FUNC_EPILOGUE(pGC);
}

/** Set the values in the graphics context on the back-end server
 *  associated with \a pGC's screen. */
void
dmxChangeGC(GCPtr pGC, unsigned long mask)
{
    ScreenPtr pScreen = pGC->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    XGCValues v;

    DMX_GC_FUNC_PROLOGUE(pGC);
#if 0
    pGC->funcs->ChangeGC(pGC, mask);
#endif

    /* Handle "magic special case" from CreateGC */
    if (pGCPriv->msc) {
        /* The "magic special case" is used to handle the case where a
         * foreground pixel is set when the GC is created so that a
         * "pseudo default-tile" can be created and used in case the
         * fillstyle was set to FillTiled.  This specific case is tested
         * in xtest (XCreateGC test #3).  What has happened in dix by
         * the time it reaches here is (1) the pGC->tile.pixel has been
         * set to pGC->fgPixel and pGC->tileIsPixel is set, (2) if a
         * tile has also been set, then pGC->tileIsPixel is unset and
         * pGC->tile.pixmap is initialized; else, the default tile is
         * created and pGC->tileIsPixel is unset and pGC->tile.pixmap is
         * initialized to the "pseudo default-tile".  In either case,
         * pGC->tile.pixmap is set; however, in the "magic special case"
         * the mask is not updated to allow us to detect that we should
         * initialize the GCTile in the back-end server.  Thus, we catch
         * this case in dmxCreateGC and add GCTile to the mask here.
         * Are there any cases that I've missed?
         */

        /* Make sure that the tile.pixmap is set, just in case the user
         * set GCTile in the mask but forgot to set vals.pixmap
         */
        if (pGC->tile.pixmap)
            mask |= GCTile;

        /* This only happens once when the GC is created */
        pGCPriv->msc = FALSE;
    }

    /* Update back-end server's gc */
    if (mask & GCFunction)
        v.function = pGC->alu;
    if (mask & GCPlaneMask)
        v.plane_mask = pGC->planemask;
    if (mask & GCForeground)
        v.foreground = pGC->fgPixel;
    if (mask & GCBackground)
        v.background = pGC->bgPixel;
    if (mask & GCLineWidth)
        v.line_width = pGC->lineWidth;
    if (mask & GCLineStyle)
        v.line_style = pGC->lineStyle;
    if (mask & GCCapStyle)
        v.cap_style = pGC->capStyle;
    if (mask & GCJoinStyle)
        v.join_style = pGC->joinStyle;
    if (mask & GCFillStyle)
        v.fill_style = pGC->fillStyle;
    if (mask & GCFillRule)
        v.fill_rule = pGC->fillRule;
    if (mask & GCTile) {
        if (pGC->tileIsPixel) {
            mask &= ~GCTile;
        }
        else {
            dmxPixPrivPtr pPixPriv = DMX_GET_PIXMAP_PRIV(pGC->tile.pixmap);

            v.tile = (Drawable) pPixPriv->pixmap;
        }
    }
    if (mask & GCStipple) {
        dmxPixPrivPtr pPixPriv = DMX_GET_PIXMAP_PRIV(pGC->stipple);

        v.stipple = (Drawable) pPixPriv->pixmap;
    }
    if (mask & GCTileStipXOrigin)
        v.ts_x_origin = pGC->patOrg.x;
    if (mask & GCTileStipYOrigin)
        v.ts_y_origin = pGC->patOrg.y;
    if (mask & GCFont) {
        if (dmxScreen->beDisplay) {
            dmxFontPrivPtr pFontPriv;

            pFontPriv = FontGetPrivate(pGC->font, dmxFontPrivateIndex);
            v.font = pFontPriv->font[pScreen->myNum]->fid;
        }
        else {
            mask &= ~GCFont;
        }
    }
    if (mask & GCSubwindowMode)
        v.subwindow_mode = pGC->subWindowMode;

    /* Graphics exposures are not needed on the back-ends since they can
       be generated on the front-end thereby saving bandwidth. */
    if (mask & GCGraphicsExposures)
        mask &= ~GCGraphicsExposures;

    if (mask & GCClipXOrigin)
        v.clip_x_origin = pGC->clipOrg.x;
    if (mask & GCClipYOrigin)
        v.clip_y_origin = pGC->clipOrg.y;
    if (mask & GCClipMask)
        mask &= ~GCClipMask;    /* See ChangeClip */
    if (mask & GCDashOffset)
        v.dash_offset = pGC->dashOffset;
    if (mask & GCDashList) {
        mask &= ~GCDashList;
        if (dmxScreen->beDisplay)
            XSetDashes(dmxScreen->beDisplay, pGCPriv->gc,
                       pGC->dashOffset, (char *) pGC->dash, pGC->numInDashList);
    }
    if (mask & GCArcMode)
        v.arc_mode = pGC->arcMode;

    if (mask && dmxScreen->beDisplay) {
        XChangeGC(dmxScreen->beDisplay, pGCPriv->gc, mask, &v);
        dmxSync(dmxScreen, FALSE);
    }

    DMX_GC_FUNC_EPILOGUE(pGC);
}

/** Copy \a pGCSrc to \a pGCDst on the back-end server associated with
 *  \a pGCSrc's screen. */
void
dmxCopyGC(GCPtr pGCSrc, unsigned long changes, GCPtr pGCDst)
{
    ScreenPtr pScreen = pGCSrc->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxGCPrivPtr pGCSrcPriv = DMX_GET_GC_PRIV(pGCSrc);
    dmxGCPrivPtr pGCDstPriv = DMX_GET_GC_PRIV(pGCDst);

    DMX_GC_FUNC_PROLOGUE(pGCDst);
    pGCDst->funcs->CopyGC(pGCSrc, changes, pGCDst);

    /* Copy the GC on the back-end server */
    if (dmxScreen->beDisplay)
        XCopyGC(dmxScreen->beDisplay, pGCSrcPriv->gc, changes, pGCDstPriv->gc);

    DMX_GC_FUNC_EPILOGUE(pGCDst);
}

/** Free the \a pGC on the back-end server. */
Bool
dmxBEFreeGC(GCPtr pGC)
{
    ScreenPtr pScreen = pGC->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);

    if (pGCPriv->gc) {
        XFreeGC(dmxScreen->beDisplay, pGCPriv->gc);
        pGCPriv->gc = NULL;
        return TRUE;
    }

    return FALSE;
}

/** Destroy the graphics context, \a pGC and free the corresponding GC
 *  on the back-end server. */
void
dmxDestroyGC(GCPtr pGC)
{
    ScreenPtr pScreen = pGC->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];

    DMX_GC_FUNC_PROLOGUE(pGC);

    /* Free the GC on the back-end server */
    if (dmxScreen->beDisplay)
        dmxBEFreeGC(pGC);

    pGC->funcs->DestroyGC(pGC);
    DMX_GC_FUNC_EPILOGUE(pGC);
}

/** Change the clip rects for a GC. */
void
dmxChangeClip(GCPtr pGC, int type, void *pvalue, int nrects)
{
    ScreenPtr pScreen = pGC->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);
    XRectangle *pRects;
    BoxPtr pBox;
    int i, nRects;

    DMX_GC_FUNC_PROLOGUE(pGC);
    pGC->funcs->ChangeClip(pGC, type, pvalue, nrects);

    /* Set the client clip on the back-end server */
    if (!pGC->clientClip) {
        if (dmxScreen->beDisplay)
            XSetClipMask(dmxScreen->beDisplay, pGCPriv->gc, None);
    } else {
        if (dmxScreen->beDisplay) {
            nRects = RegionNumRects((RegionPtr) pGC->clientClip);
            pRects = xallocarray(nRects, sizeof(*pRects));
            pBox = RegionRects((RegionPtr) pGC->clientClip);

            for (i = 0; i < nRects; i++) {
                pRects[i].x = pBox[i].x1;
                pRects[i].y = pBox[i].y1;
                pRects[i].width = pBox[i].x2 - pBox[i].x1;
                pRects[i].height = pBox[i].y2 - pBox[i].y1;
            }

            XSetClipRectangles(dmxScreen->beDisplay, pGCPriv->gc,
                               pGC->clipOrg.x, pGC->clipOrg.y,
                               pRects, nRects, Unsorted);

            free(pRects);
        }
    }

    DMX_GC_FUNC_EPILOGUE(pGC);
}

/** Destroy a GC's clip rects. */
void
dmxDestroyClip(GCPtr pGC)
{
    ScreenPtr pScreen = pGC->pScreen;
    DMXScreenInfo *dmxScreen = &dmxScreens[pScreen->myNum];
    dmxGCPrivPtr pGCPriv = DMX_GET_GC_PRIV(pGC);

    DMX_GC_FUNC_PROLOGUE(pGC);
    pGC->funcs->DestroyClip(pGC);

    /* Set the client clip on the back-end server to None */
    if (dmxScreen->beDisplay)
        XSetClipMask(dmxScreen->beDisplay, pGCPriv->gc, None);

    DMX_GC_FUNC_EPILOGUE(pGC);
}

/** Copy a GC's clip rects. */
void
dmxCopyClip(GCPtr pGCDst, GCPtr pGCSrc)
{
    DMX_GC_FUNC_PROLOGUE(pGCDst);
    pGCDst->funcs->CopyClip(pGCDst, pGCSrc);
    DMX_GC_FUNC_EPILOGUE(pGCDst);
}
