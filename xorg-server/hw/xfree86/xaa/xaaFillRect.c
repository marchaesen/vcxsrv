
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "pixmapstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"

static void XAARenderSolidRects(GCPtr, int, BoxPtr, int, int);
static void XAARenderColor8x8Rects(GCPtr, int, BoxPtr, int, int);
static void XAARenderMono8x8Rects(GCPtr, int, BoxPtr, int, int);
static void XAARenderColorExpandRects(GCPtr, int, BoxPtr, int, int);
static void XAARenderCacheExpandRects(GCPtr, int, BoxPtr, int, int);
static void XAARenderCacheBltRects(GCPtr, int, BoxPtr, int, int);
static void XAARenderImageWriteRects(GCPtr, int, BoxPtr, int, int);
static void XAARenderPixmapCopyRects(GCPtr, int, BoxPtr, int, int);

void
XAAPolyFillRect(DrawablePtr pDraw, GCPtr pGC, int nrectFill,    /* number of rectangles to fill */
                xRectangle *prectInit   /* Pointer to first rectangle to fill */
    )
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int xorg = pDraw->x;
    int yorg = pDraw->y;
    int type = 0;
    ClipAndRenderRectsFunc function;

    if ((nrectFill <= 0) || !pGC->planemask)
        return;

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    switch (pGC->fillStyle) {
    case FillSolid:
        type = DO_SOLID;
        break;
    case FillStippled:
        type = (*infoRec->StippledFillChooser) (pGC);
        break;
    case FillOpaqueStippled:
        if ((pGC->fgPixel == pGC->bgPixel) && infoRec->FillSolidRects &&
            CHECK_PLANEMASK(pGC, infoRec->FillSolidRectsFlags) &&
            CHECK_ROP(pGC, infoRec->FillSolidRectsFlags) &&
            CHECK_ROPSRC(pGC, infoRec->FillSolidRectsFlags) &&
            CHECK_FG(pGC, infoRec->FillSolidRectsFlags))
            type = DO_SOLID;
        else
            type = (*infoRec->OpaqueStippledFillChooser) (pGC);
        break;
    case FillTiled:
        type = (*infoRec->TiledFillChooser) (pGC);
        break;
    }

    switch (type) {
    case DO_SOLID:
        function = XAARenderSolidRects;
        break;
    case DO_COLOR_8x8:
        function = XAARenderColor8x8Rects;
        break;
    case DO_MONO_8x8:
        function = XAARenderMono8x8Rects;
        break;
    case DO_CACHE_BLT:
        function = XAARenderCacheBltRects;
        break;
    case DO_COLOR_EXPAND:
        function = XAARenderColorExpandRects;
        break;
    case DO_CACHE_EXPAND:
        function = XAARenderCacheExpandRects;
        break;
    case DO_IMAGE_WRITE:
        function = XAARenderImageWriteRects;
        break;
    case DO_PIXMAP_COPY:
        function = XAARenderPixmapCopyRects;
        break;
    default:
        (*XAAFallbackOps.PolyFillRect) (pDraw, pGC, nrectFill, prectInit);
        return;
    }

    if (xorg | yorg) {
        int n = nrectFill;
        xRectangle *prect = prectInit;

        while (n--) {
            prect->x += xorg;
            prect->y += yorg;
            prect++;
        }
    }

    XAAClipAndRenderRects(pGC, function, nrectFill, prectInit, xorg, yorg);
}

        /*********************\
	|     Solid Rects     |
	\*********************/

static void
XAARenderSolidRects(GCPtr pGC,
                    int nboxes, BoxPtr pClipBoxes, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    (*infoRec->FillSolidRects) (infoRec->pScrn,
                                pGC->fgPixel, pGC->alu, pGC->planemask, nboxes,
                                pClipBoxes);
}

        /************************\
	|     Mono 8x8 Rects     |
	\************************/

static void
XAARenderMono8x8Rects(GCPtr pGC,
                      int nboxes, BoxPtr pClipBoxes, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    XAAPixmapPtr pPriv;
    int fg, bg;

    switch (pGC->fillStyle) {
    case FillStippled:
        pPriv = XAA_GET_PIXMAP_PRIVATE(pGC->stipple);
        fg = pGC->fgPixel;
        bg = -1;
        break;
    case FillOpaqueStippled:
        pPriv = XAA_GET_PIXMAP_PRIVATE(pGC->stipple);
        fg = pGC->fgPixel;
        bg = pGC->bgPixel;
        break;
    case FillTiled:
        pPriv = XAA_GET_PIXMAP_PRIVATE(pGC->tile.pixmap);
        fg = pPriv->fg;
        bg = pPriv->bg;
        break;
    default:                   /* Muffle compiler */
        pPriv = NULL;           /* Kaboom */
        fg = -1;
        bg = -1;
        break;
    }

    (*infoRec->FillMono8x8PatternRects) (infoRec->pScrn,
                                         fg, bg, pGC->alu, pGC->planemask,
                                         nboxes, pClipBoxes, pPriv->pattern0,
                                         pPriv->pattern1,
                                         (xorg + pGC->patOrg.x),
                                         (yorg + pGC->patOrg.y));
}

        /*************************\
	|     Color 8x8 Rects     |
	\*************************/

static void
XAARenderColor8x8Rects(GCPtr pGC,
                       int nboxes, BoxPtr pClipBoxes, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    XAACacheInfoPtr pCache;
    PixmapPtr pPix;
    int fg, bg;

    switch (pGC->fillStyle) {
    case FillStippled:
        pPix = pGC->stipple;
        fg = pGC->fgPixel;
        bg = -1;
        break;
    case FillOpaqueStippled:
        pPix = pGC->stipple;
        fg = pGC->fgPixel;
        bg = pGC->bgPixel;
        break;
    case FillTiled:
        pPix = pGC->tile.pixmap;
        fg = -1;
        bg = -1;
        break;
    default:                   /* Muffle compiler */
        pPix = NULL;
        fg = -1;
        bg = -1;
        break;
    }

    pCache = (*infoRec->CacheColor8x8Pattern) (infoRec->pScrn, pPix, fg, bg);
    (*infoRec->FillColor8x8PatternRects) (infoRec->pScrn,
                                          pGC->alu, pGC->planemask, nboxes,
                                          pClipBoxes, (xorg + pGC->patOrg.x),
                                          (yorg + pGC->patOrg.y), pCache);
}

        /****************************\
	|     Color Expand Rects     |
	\****************************/

static void
XAARenderColorExpandRects(GCPtr pGC,
                          int nboxes, BoxPtr pClipBoxes, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int fg, bg;

    switch (pGC->fillStyle) {
    case FillStippled:
        fg = pGC->fgPixel;
        bg = -1;
        break;
    case FillOpaqueStippled:
        fg = pGC->fgPixel;
        bg = pGC->bgPixel;
        break;
    default:                   /* Muffle compiler */
        fg = -1;
        bg = -1;
        break;
    }

    (*infoRec->FillColorExpandRects) (infoRec->pScrn, fg, bg,
                                      pGC->alu, pGC->planemask, nboxes,
                                      pClipBoxes, (xorg + pGC->patOrg.x),
                                      (yorg + pGC->patOrg.y), pGC->stipple);
}

        /*************************\
	|     Cache Blt Rects     |
	\*************************/

static void
XAARenderCacheBltRects(GCPtr pGC,
                       int nboxes, BoxPtr pClipBoxes, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    XAACacheInfoPtr pCache;

    switch (pGC->fillStyle) {
    case FillStippled:
        pCache = (*infoRec->CacheStipple) (infoRec->pScrn, pGC->stipple,
                                           pGC->fgPixel, -1);
        break;
    case FillOpaqueStippled:
        pCache = (*infoRec->CacheStipple) (infoRec->pScrn, pGC->stipple,
                                           pGC->fgPixel, pGC->bgPixel);
        break;
    case FillTiled:
        pCache = (*infoRec->CacheTile) (infoRec->pScrn, pGC->tile.pixmap);
        break;
    default:                   /* Muffle compiler */
        pCache = NULL;
        break;
    }

    (*infoRec->FillCacheBltRects) (infoRec->pScrn, pGC->alu,
                                   pGC->planemask, nboxes, pClipBoxes,
                                   (xorg + pGC->patOrg.x),
                                   (yorg + pGC->patOrg.y), pCache);
}

        /****************************\
	|     Cache Expand Rects     |
	\****************************/

static void
XAARenderCacheExpandRects(GCPtr pGC,
                          int nboxes, BoxPtr pClipBoxes, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int fg, bg;

    switch (pGC->fillStyle) {
    case FillStippled:
        fg = pGC->fgPixel;
        bg = -1;
        break;
    case FillOpaqueStippled:
        fg = pGC->fgPixel;
        bg = pGC->bgPixel;
        break;
    default:                   /* Muffle compiler */
        fg = -1;
        bg = -1;
        break;
    }

    (*infoRec->FillCacheExpandRects) (infoRec->pScrn, fg, bg,
                                      pGC->alu, pGC->planemask, nboxes,
                                      pClipBoxes, (xorg + pGC->patOrg.x),
                                      (yorg + pGC->patOrg.y), pGC->stipple);
}

        /***************************\
	|     Image Write Rects     |
	\***************************/

static void
XAARenderImageWriteRects(GCPtr pGC,
                         int nboxes, BoxPtr pClipBoxes, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    (*infoRec->FillImageWriteRects) (infoRec->pScrn, pGC->alu,
                                     pGC->planemask, nboxes, pClipBoxes,
                                     (xorg + pGC->patOrg.x),
                                     (yorg + pGC->patOrg.y), pGC->tile.pixmap);
}

        /***************************\
	|     Pixmap Copy Rects     |
	\***************************/

static void
XAARenderPixmapCopyRects(GCPtr pGC,
                         int nboxes, BoxPtr pClipBoxes, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    XAACacheInfoPtr pCache = &(infoRec->ScratchCacheInfoRec);
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pGC->tile.pixmap);

    pCache->x = pPriv->offscreenArea->box.x1;
    pCache->y = pPriv->offscreenArea->box.y1;
    pCache->w = pCache->orig_w = pPriv->offscreenArea->box.x2 - pCache->x;
    pCache->h = pCache->orig_h = pPriv->offscreenArea->box.y2 - pCache->y;
    pCache->trans_color = -1;

    (*infoRec->FillCacheBltRects) (infoRec->pScrn, pGC->alu,
                                   pGC->planemask, nboxes, pClipBoxes,
                                   (xorg + pGC->patOrg.x),
                                   (yorg + pGC->patOrg.y), pCache);
}

        /************\
	|   Solid    |
	\************/

void
XAAFillSolidRects(ScrnInfoPtr pScrn, int fg, int rop, unsigned int planemask, int nBox, /* number of rectangles to fill */
                  BoxPtr pBox   /* Pointer to first rectangle to fill */
    )
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);

    (*infoRec->SetupForSolidFill) (pScrn, fg, rop, planemask);
    while (nBox--) {
        (*infoRec->SubsequentSolidFillRect) (pScrn, pBox->x1, pBox->y1,
                                             pBox->x2 - pBox->x1,
                                             pBox->y2 - pBox->y1);
        pBox++;
    }
    SET_SYNC_FLAG(infoRec);
}

        /*********************\
	|  8x8 Mono Patterns  |
	\*********************/

void
XAAFillMono8x8PatternRectsScreenOrigin(ScrnInfoPtr pScrn,
                                       int fg, int bg, int rop,
                                       unsigned int planemask,
                                       int nBox,
                                       BoxPtr pBox,
                                       int pattern0, int pattern1,
                                       int xorigin, int yorigin)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int patx = pattern0, paty = pattern1;
    int xorg = (-xorigin) & 0x07;
    int yorg = (-yorigin) & 0x07;

    if (infoRec->Mono8x8PatternFillFlags & HARDWARE_PATTERN_PROGRAMMED_BITS) {
        if (!(infoRec->Mono8x8PatternFillFlags &
              HARDWARE_PATTERN_PROGRAMMED_ORIGIN)) {
            XAARotateMonoPattern(&patx, &paty, xorg, yorg,
                                 (infoRec->Mono8x8PatternFillFlags &
                                  BIT_ORDER_IN_BYTE_MSBFIRST));
            xorg = patx;
            yorg = paty;
        }
    }
    else {
        XAACacheInfoPtr pCache =
            (*infoRec->CacheMono8x8Pattern) (pScrn, pattern0, pattern1);
        patx = pCache->x;
        paty = pCache->y;
        if (!(infoRec->Mono8x8PatternFillFlags &
              HARDWARE_PATTERN_PROGRAMMED_ORIGIN)) {
            int slot = (yorg << 3) + xorg;

            patx += pCache->offsets[slot].x;
            paty += pCache->offsets[slot].y;
            xorg = patx;
            yorg = paty;
        }
    }

    (*infoRec->SetupForMono8x8PatternFill) (pScrn, patx, paty,
                                            fg, bg, rop, planemask);

    while (nBox--) {
        (*infoRec->SubsequentMono8x8PatternFillRect) (pScrn,
                                                      xorg, yorg, pBox->x1,
                                                      pBox->y1,
                                                      pBox->x2 - pBox->x1,
                                                      pBox->y2 - pBox->y1);
        pBox++;
    }
    SET_SYNC_FLAG(infoRec);
}

void
XAAFillMono8x8PatternRects(ScrnInfoPtr pScrn,
                           int fg, int bg, int rop,
                           unsigned int planemask,
                           int nBox,
                           BoxPtr pBox,
                           int pattern0, int pattern1, int xorigin, int yorigin)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int patx = pattern0, paty = pattern1;
    int xorg, yorg;
    XAACacheInfoPtr pCache = NULL;

    if (!(infoRec->Mono8x8PatternFillFlags & HARDWARE_PATTERN_PROGRAMMED_BITS)) {
        pCache = (*infoRec->CacheMono8x8Pattern) (pScrn, pattern0, pattern1);
        patx = pCache->x;
        paty = pCache->y;
    }

    (*infoRec->SetupForMono8x8PatternFill) (pScrn, patx, paty,
                                            fg, bg, rop, planemask);

    while (nBox--) {
        xorg = (pBox->x1 - xorigin) & 0x07;
        yorg = (pBox->y1 - yorigin) & 0x07;

        if (!(infoRec->Mono8x8PatternFillFlags &
              HARDWARE_PATTERN_PROGRAMMED_ORIGIN)) {
            if (infoRec->Mono8x8PatternFillFlags &
                HARDWARE_PATTERN_PROGRAMMED_BITS) {
                patx = pattern0;
                paty = pattern1;
                XAARotateMonoPattern(&patx, &paty, xorg, yorg,
                                     (infoRec->Mono8x8PatternFillFlags &
                                      BIT_ORDER_IN_BYTE_MSBFIRST));
                xorg = patx;
                yorg = paty;
            }
            else {
                int slot = (yorg << 3) + xorg;

                xorg = patx + pCache->offsets[slot].x;
                yorg = paty + pCache->offsets[slot].y;
            }
        }

        (*infoRec->SubsequentMono8x8PatternFillRect) (pScrn,
                                                      xorg, yorg, pBox->x1,
                                                      pBox->y1,
                                                      pBox->x2 - pBox->x1,
                                                      pBox->y2 - pBox->y1);
        pBox++;
    }

    SET_SYNC_FLAG(infoRec);
}

        /**********************\
	|  8x8 Color Patterns  |
	\**********************/

void
XAAFillColor8x8PatternRectsScreenOrigin(ScrnInfoPtr pScrn,
                                        int rop,
                                        unsigned int planemask,
                                        int nBox,
                                        BoxPtr pBox,
                                        int xorigin, int yorigin,
                                        XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int patx = pCache->x, paty = pCache->y;
    int xorg = (-xorigin) & 0x07;
    int yorg = (-yorigin) & 0x07;

    if (!(infoRec->Color8x8PatternFillFlags &
          HARDWARE_PATTERN_PROGRAMMED_ORIGIN)) {
        int slot = (yorg << 3) + xorg;

        paty += pCache->offsets[slot].y;
        patx += pCache->offsets[slot].x;
        xorg = patx;
        yorg = paty;
    }

    (*infoRec->SetupForColor8x8PatternFill) (pScrn, patx, paty,
                                             rop, planemask,
                                             pCache->trans_color);

    while (nBox--) {
        (*infoRec->SubsequentColor8x8PatternFillRect) (pScrn,
                                                       xorg, yorg, pBox->x1,
                                                       pBox->y1,
                                                       pBox->x2 - pBox->x1,
                                                       pBox->y2 - pBox->y1);
        pBox++;
    }
    SET_SYNC_FLAG(infoRec);
}

void
XAAFillColor8x8PatternRects(ScrnInfoPtr pScrn,
                            int rop,
                            unsigned int planemask,
                            int nBox,
                            BoxPtr pBox,
                            int xorigin, int yorigin, XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int xorg, yorg;

    (*infoRec->SetupForColor8x8PatternFill) (pScrn, pCache->x, pCache->y,
                                             rop, planemask,
                                             pCache->trans_color);

    while (nBox--) {
        xorg = (pBox->x1 - xorigin) & 0x07;
        yorg = (pBox->y1 - yorigin) & 0x07;

        if (!(infoRec->Color8x8PatternFillFlags &
              HARDWARE_PATTERN_PROGRAMMED_ORIGIN)) {
            int slot = (yorg << 3) + xorg;

            yorg = pCache->y + pCache->offsets[slot].y;
            xorg = pCache->x + pCache->offsets[slot].x;
        }

        (*infoRec->SubsequentColor8x8PatternFillRect) (pScrn,
                                                       xorg, yorg, pBox->x1,
                                                       pBox->y1,
                                                       pBox->x2 - pBox->x1,
                                                       pBox->y2 - pBox->y1);
        pBox++;
    }

    SET_SYNC_FLAG(infoRec);
}

        /***************\
	|  Cache Blits  |
	\***************/

void
XAAFillCacheBltRects(ScrnInfoPtr pScrn,
                     int rop,
                     unsigned int planemask,
                     int nBox,
                     BoxPtr pBox, int xorg, int yorg, XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int x, y, phaseY, phaseX, skipleft, height, width, w, blit_w, blit_h;

    (*infoRec->SetupForScreenToScreenCopy) (pScrn, 1, 1, rop, planemask,
                                            pCache->trans_color);

    while (nBox--) {
        y = pBox->y1;
        phaseY = (y - yorg) % pCache->orig_h;
        if (phaseY < 0)
            phaseY += pCache->orig_h;
        phaseX = (pBox->x1 - xorg) % pCache->orig_w;
        if (phaseX < 0)
            phaseX += pCache->orig_w;
        height = pBox->y2 - y;
        width = pBox->x2 - pBox->x1;

#if 0
        if (rop == GXcopy) {
            while (1) {
                w = width;
                skipleft = phaseX;
                x = pBox->x1;
                blit_h = pCache->h - phaseY;
                if (blit_h > height)
                    blit_h = height;

                while (1) {
                    blit_w = pCache->w - skipleft;
                    if (blit_w > w)
                        blit_w = w;
                    (*infoRec->SubsequentScreenToScreenCopy) (pScrn,
                                                              pCache->x +
                                                              skipleft,
                                                              pCache->y +
                                                              phaseY, x, y,
                                                              blit_w, blit_h);
                    w -= blit_w;
                    if (!w)
                        break;
                    x += blit_w;
                    skipleft = (skipleft + blit_w) % pCache->orig_w;
                    if (blit_w >= pCache->orig_w)
                        break;
                }

                /* Expand horizontally */
                if (w) {
                    skipleft -= phaseX;
                    if (skipleft < 0)
                        skipleft += pCache->orig_w;
                    blit_w = x - pBox->x1 - skipleft;
                    while (w) {
                        if (blit_w > w)
                            blit_w = w;
                        (*infoRec->SubsequentScreenToScreenCopy) (pScrn,
                                                                  pBox->x1 +
                                                                  skipleft, y,
                                                                  x, y, blit_w,
                                                                  blit_h);
                        w -= blit_w;
                        x += blit_w;
                        blit_w <<= 1;
                    }
                }

                height -= blit_h;
                if (!height)
                    break;
                y += blit_h;
                phaseY = (phaseY + blit_h) % pCache->orig_h;
                if (blit_h >= pCache->orig_h)
                    break;
            }

            /* Expand vertically */
            if (height) {
                blit_w = pBox->x2 - pBox->x1;
                phaseY -= (pBox->y1 - yorg) % pCache->orig_h;
                if (phaseY < 0)
                    phaseY += pCache->orig_h;
                blit_h = y - pBox->y1 - phaseY;
                while (height) {
                    if (blit_h > height)
                        blit_h = height;
                    (*infoRec->SubsequentScreenToScreenCopy) (pScrn, pBox->x1,
                                                              pBox->y1 + phaseY,
                                                              pBox->x1, y,
                                                              blit_w, blit_h);
                    height -= blit_h;
                    y += blit_h;
                    blit_h <<= 1;
                }
            }
        }
        else
#endif
        {
            while (1) {
                w = width;
                skipleft = phaseX;
                x = pBox->x1;
                blit_h = pCache->h - phaseY;
                if (blit_h > height)
                    blit_h = height;

                while (1) {
                    blit_w = pCache->w - skipleft;
                    if (blit_w > w)
                        blit_w = w;
                    (*infoRec->SubsequentScreenToScreenCopy) (pScrn,
                                                              pCache->x +
                                                              skipleft,
                                                              pCache->y +
                                                              phaseY, x, y,
                                                              blit_w, blit_h);
                    w -= blit_w;
                    if (!w)
                        break;
                    x += blit_w;
                    skipleft = (skipleft + blit_w) % pCache->orig_w;
                }
                height -= blit_h;
                if (!height)
                    break;
                y += blit_h;
                phaseY = (phaseY + blit_h) % pCache->orig_h;
            }
        }
        pBox++;
    }

    SET_SYNC_FLAG(infoRec);
}

        /*******************\
	|  Cache Expansion  |
	\*******************/

void
XAAFillCacheExpandRects(ScrnInfoPtr pScrn,
                        int fg, int bg, int rop,
                        unsigned int planemask,
                        int nBox,
                        BoxPtr pBox, int xorg, int yorg, PixmapPtr pPix)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int x, y, phaseY, phaseX, skipleft, height, width, w, blit_w, blit_h;
    int cacheWidth;
    XAACacheInfoPtr pCache;

    pCache = (*infoRec->CacheMonoStipple) (pScrn, pPix);

    cacheWidth = (pCache->w * pScrn->bitsPerPixel) /
        infoRec->CacheColorExpandDensity;

    (*infoRec->SetupForScreenToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                       planemask);

    while (nBox--) {
        y = pBox->y1;
        phaseY = (y - yorg) % pCache->orig_h;
        if (phaseY < 0)
            phaseY += pCache->orig_h;
        phaseX = (pBox->x1 - xorg) % pCache->orig_w;
        if (phaseX < 0)
            phaseX += pCache->orig_w;
        height = pBox->y2 - y;
        width = pBox->x2 - pBox->x1;

        while (1) {
            w = width;
            skipleft = phaseX;
            x = pBox->x1;
            blit_h = pCache->h - phaseY;
            if (blit_h > height)
                blit_h = height;

            while (1) {
                blit_w = cacheWidth - skipleft;
                if (blit_w > w)
                    blit_w = w;
                (*infoRec->SubsequentScreenToScreenColorExpandFill) (pScrn, x,
                                                                     y, blit_w,
                                                                     blit_h,
                                                                     pCache->x,
                                                                     pCache->y +
                                                                     phaseY,
                                                                     skipleft);
                w -= blit_w;
                if (!w)
                    break;
                x += blit_w;
                skipleft = (skipleft + blit_w) % pCache->orig_w;
            }
            height -= blit_h;
            if (!height)
                break;
            y += blit_h;
            phaseY = (phaseY + blit_h) % pCache->orig_h;
        }
        pBox++;
    }

    SET_SYNC_FLAG(infoRec);
}

        /******************\
	|   Image Writes   |
	\******************/

/*  This requires all LEFT_EDGE clipping.  You get too many problems
    with reading past the edge of the pattern otherwise */

static void
WriteColumn(ScrnInfoPtr pScrn,
            unsigned char *pSrc,
            int x, int y, int w, int h,
            int xoff, int yoff, int pHeight, int srcwidth, int Bpp)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    unsigned char *src;
    Bool PlusOne = FALSE;
    int skipleft, dwords;

    pSrc += (Bpp * xoff);

    if ((skipleft = (long) pSrc & 0x03L)) {
        if (Bpp == 3)
            skipleft = 4 - skipleft;
        else
            skipleft /= Bpp;

        x -= skipleft;
        w += skipleft;

        if (Bpp == 3)
            pSrc -= 3 * skipleft;
        else                    /* is this Alpha friendly ? */
            pSrc = (unsigned char *) ((long) pSrc & ~0x03L);
    }

    src = pSrc + (yoff * srcwidth);

    dwords = bytes_to_int32(w * Bpp);

    if ((infoRec->ImageWriteFlags & CPU_TRANSFER_PAD_QWORD) &&
        ((dwords * h) & 0x01)) {
        PlusOne = TRUE;
    }

    (*infoRec->SubsequentImageWriteRect) (pScrn, x, y, w, h, skipleft);

    if (dwords > infoRec->ImageWriteRange) {
        while (h--) {
            XAAMoveDWORDS_FixedBase((CARD32 *) infoRec->ImageWriteBase,
                                    (CARD32 *) src, dwords);
            src += srcwidth;
            yoff++;
            if (yoff >= pHeight) {
                yoff = 0;
                src = pSrc;
            }
        }
    }
    else {
        if (srcwidth == (dwords << 2)) {
            int maxLines = infoRec->ImageWriteRange / dwords;
            int step;

            while (h) {
                step = pHeight - yoff;
                if (step > maxLines)
                    step = maxLines;
                if (step > h)
                    step = h;

                XAAMoveDWORDS((CARD32 *) infoRec->ImageWriteBase,
                              (CARD32 *) src, dwords * step);

                src += (srcwidth * step);
                yoff += step;
                if (yoff >= pHeight) {
                    yoff = 0;
                    src = pSrc;
                }
                h -= step;
            }
        }
        else {
            while (h--) {
                XAAMoveDWORDS((CARD32 *) infoRec->ImageWriteBase,
                              (CARD32 *) src, dwords);
                src += srcwidth;
                yoff++;
                if (yoff >= pHeight) {
                    yoff = 0;
                    src = pSrc;
                }
            }
        }
    }

    if (PlusOne) {
        CARD32 *base = (CARD32 *) infoRec->ImageWriteBase;

        *base = 0x00000000;
    }
}

void
XAAFillImageWriteRects(ScrnInfoPtr pScrn,
                       int rop,
                       unsigned int planemask,
                       int nBox,
                       BoxPtr pBox, int xorg, int yorg, PixmapPtr pPix)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int x, phaseY, phaseX, height, width, blit_w;
    int pHeight = pPix->drawable.height;
    int pWidth = pPix->drawable.width;
    int Bpp = pPix->drawable.bitsPerPixel >> 3;
    int srcwidth = pPix->devKind;

    (*infoRec->SetupForImageWrite) (pScrn, rop, planemask, -1,
                                    pPix->drawable.bitsPerPixel,
                                    pPix->drawable.depth);

    while (nBox--) {
        x = pBox->x1;
        phaseY = (pBox->y1 - yorg) % pHeight;
        if (phaseY < 0)
            phaseY += pHeight;
        phaseX = (x - xorg) % pWidth;
        if (phaseX < 0)
            phaseX += pWidth;
        height = pBox->y2 - pBox->y1;
        width = pBox->x2 - x;

        while (1) {
            blit_w = pWidth - phaseX;
            if (blit_w > width)
                blit_w = width;

            WriteColumn(pScrn, pPix->devPrivate.ptr, x, pBox->y1,
                        blit_w, height, phaseX, phaseY, pHeight, srcwidth, Bpp);

            width -= blit_w;
            if (!width)
                break;
            x += blit_w;
            phaseX = (phaseX + blit_w) % pWidth;
        }
        pBox++;
    }

    if (infoRec->ImageWriteFlags & SYNC_AFTER_IMAGE_WRITE)
        (*infoRec->Sync) (pScrn);
    else
        SET_SYNC_FLAG(infoRec);
}

        /*************\
	|  Utilities  |
	\*************/

void
XAAClipAndRenderRects(GCPtr pGC,
                      ClipAndRenderRectsFunc BoxFunc,
                      int nrectFill, xRectangle *prect, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int Right, Bottom, MaxBoxes;
    BoxPtr pextent, pboxClipped, pboxClippedBase;

    MaxBoxes = infoRec->PreAllocSize / sizeof(BoxRec);
    pboxClippedBase = (BoxPtr) infoRec->PreAllocMem;
    pboxClipped = pboxClippedBase;

    if (RegionNumRects(pGC->pCompositeClip) == 1) {
        pextent = RegionRects(pGC->pCompositeClip);
        while (nrectFill--) {
            pboxClipped->x1 = max(pextent->x1, prect->x);
            pboxClipped->y1 = max(pextent->y1, prect->y);

            Right = (int) prect->x + (int) prect->width;
            pboxClipped->x2 = min(pextent->x2, Right);

            Bottom = (int) prect->y + (int) prect->height;
            pboxClipped->y2 = min(pextent->y2, Bottom);

            prect++;
            if ((pboxClipped->x1 < pboxClipped->x2) &&
                (pboxClipped->y1 < pboxClipped->y2)) {
                pboxClipped++;
                if (pboxClipped >= (pboxClippedBase + MaxBoxes)) {
                    (*BoxFunc) (pGC, MaxBoxes, pboxClippedBase, xorg, yorg);
                    pboxClipped = pboxClippedBase;
                }
            }
        }
    }
    else {
        pextent = RegionExtents(pGC->pCompositeClip);
        while (nrectFill--) {
            int n;
            BoxRec box, *pbox;

            box.x1 = max(pextent->x1, prect->x);
            box.y1 = max(pextent->y1, prect->y);

            Right = (int) prect->x + (int) prect->width;
            box.x2 = min(pextent->x2, Right);

            Bottom = (int) prect->y + (int) prect->height;
            box.y2 = min(pextent->y2, Bottom);

            prect++;

            if ((box.x1 >= box.x2) || (box.y1 >= box.y2))
                continue;

            n = RegionNumRects(pGC->pCompositeClip);
            pbox = RegionRects(pGC->pCompositeClip);

            /* clip the rectangle to each box in the clip region
               this is logically equivalent to calling Intersect()
             */
            while (n--) {
                pboxClipped->x1 = max(box.x1, pbox->x1);
                pboxClipped->y1 = max(box.y1, pbox->y1);
                pboxClipped->x2 = min(box.x2, pbox->x2);
                pboxClipped->y2 = min(box.y2, pbox->y2);
                pbox++;

                /* see if clipping left anything */
                if (pboxClipped->x1 < pboxClipped->x2 &&
                    pboxClipped->y1 < pboxClipped->y2) {
                    pboxClipped++;
                    if (pboxClipped >= (pboxClippedBase + MaxBoxes)) {
                        (*BoxFunc) (pGC, MaxBoxes, pboxClippedBase, xorg, yorg);
                        pboxClipped = pboxClippedBase;
                    }
                }
            }
        }
    }

    if (pboxClipped != pboxClippedBase)
        (*BoxFunc) (pGC, pboxClipped - pboxClippedBase, pboxClippedBase,
                    xorg, yorg);
}

int
XAAGetRectClipBoxes(GCPtr pGC,
                    BoxPtr pboxClippedBase,
                    int nrectFill, xRectangle *prectInit)
{
    int Right, Bottom;
    BoxPtr pextent, pboxClipped = pboxClippedBase;
    xRectangle *prect = prectInit;
    RegionPtr prgnClip = pGC->pCompositeClip;

    if (RegionNumRects(prgnClip) == 1) {
        pextent = RegionRects(prgnClip);
        while (nrectFill--) {
            pboxClipped->x1 = max(pextent->x1, prect->x);
            pboxClipped->y1 = max(pextent->y1, prect->y);

            Right = (int) prect->x + (int) prect->width;
            pboxClipped->x2 = min(pextent->x2, Right);

            Bottom = (int) prect->y + (int) prect->height;
            pboxClipped->y2 = min(pextent->y2, Bottom);

            prect++;
            if ((pboxClipped->x1 < pboxClipped->x2) &&
                (pboxClipped->y1 < pboxClipped->y2)) {
                pboxClipped++;
            }
        }
    }
    else {
        pextent = RegionExtents(prgnClip);
        while (nrectFill--) {
            int n;
            BoxRec box, *pbox;

            box.x1 = max(pextent->x1, prect->x);
            box.y1 = max(pextent->y1, prect->y);

            Right = (int) prect->x + (int) prect->width;
            box.x2 = min(pextent->x2, Right);

            Bottom = (int) prect->y + (int) prect->height;
            box.y2 = min(pextent->y2, Bottom);

            prect++;

            if ((box.x1 >= box.x2) || (box.y1 >= box.y2))
                continue;

            n = RegionNumRects(prgnClip);
            pbox = RegionRects(prgnClip);

            /* clip the rectangle to each box in the clip region
               this is logically equivalent to calling Intersect()
             */
            while (n--) {
                pboxClipped->x1 = max(box.x1, pbox->x1);
                pboxClipped->y1 = max(box.y1, pbox->y1);
                pboxClipped->x2 = min(box.x2, pbox->x2);
                pboxClipped->y2 = min(box.y2, pbox->y2);
                pbox++;

                /* see if clipping left anything */
                if (pboxClipped->x1 < pboxClipped->x2 &&
                    pboxClipped->y1 < pboxClipped->y2) {
                    pboxClipped++;
                }
            }
        }
    }

    return pboxClipped - pboxClippedBase;
}
