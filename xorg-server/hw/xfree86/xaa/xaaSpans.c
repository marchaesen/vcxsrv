
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
#include "mi.h"
#include "mispans.h"
#include "xaa.h"
#include "xaalocal.h"

static void XAARenderSolidSpans(GCPtr, int, DDXPointPtr, int *, int, int, int);
static void XAARenderColor8x8Spans(GCPtr, int, DDXPointPtr, int *, int, int,
                                   int);
static void XAARenderMono8x8Spans(GCPtr, int, DDXPointPtr, int *, int, int,
                                  int);
static void XAARenderCacheBltSpans(GCPtr, int, DDXPointPtr, int *, int, int,
                                   int);
static void XAARenderColorExpandSpans(GCPtr, int, DDXPointPtr, int *, int, int,
                                      int);
static void XAARenderCacheExpandSpans(GCPtr, int, DDXPointPtr, int *, int, int,
                                      int);
static void XAARenderPixmapCopySpans(GCPtr, int, DDXPointPtr, int *, int, int,
                                     int);

void
XAAFillSpans(DrawablePtr pDraw, GC * pGC, int nInit,    /* number of spans to fill */
             DDXPointPtr pptInit,       /* pointer to list of start points */
             int *pwidthInit,   /* pointer to list of n widths */
             int fSorted)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int type = 0;
    ClipAndRenderSpansFunc function;
    Bool fastClip = FALSE;

    if ((nInit <= 0) || !pGC->planemask)
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
        if ((pGC->fgPixel == pGC->bgPixel) && infoRec->FillSpansSolid &&
            CHECK_PLANEMASK(pGC, infoRec->FillSpansSolidFlags) &&
            CHECK_ROP(pGC, infoRec->FillSpansSolidFlags) &&
            CHECK_ROPSRC(pGC, infoRec->FillSpansSolidFlags) &&
            CHECK_FG(pGC, infoRec->FillSpansSolidFlags))
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
        function = XAARenderSolidSpans;
        if (infoRec->ClippingFlags & HARDWARE_CLIP_SOLID_FILL)
            fastClip = TRUE;
        break;
    case DO_COLOR_8x8:
        function = XAARenderColor8x8Spans;
        if (infoRec->ClippingFlags & HARDWARE_CLIP_COLOR_8x8_FILL)
            fastClip = TRUE;
        break;
    case DO_MONO_8x8:
        function = XAARenderMono8x8Spans;
        if (infoRec->ClippingFlags & HARDWARE_CLIP_MONO_8x8_FILL)
            fastClip = TRUE;
        break;
    case DO_CACHE_BLT:
        function = XAARenderCacheBltSpans;
        if (infoRec->ClippingFlags & HARDWARE_CLIP_SCREEN_TO_SCREEN_COPY)
            fastClip = TRUE;
        break;
    case DO_COLOR_EXPAND:
        function = XAARenderColorExpandSpans;
        break;
    case DO_CACHE_EXPAND:
        function = XAARenderCacheExpandSpans;
        if (infoRec->ClippingFlags &
            HARDWARE_CLIP_SCREEN_TO_SCREEN_COLOR_EXPAND)
            fastClip = TRUE;
        break;
    case DO_PIXMAP_COPY:
        function = XAARenderPixmapCopySpans;
        if (infoRec->ClippingFlags & HARDWARE_CLIP_SCREEN_TO_SCREEN_COPY)
            fastClip = TRUE;
        break;
    case DO_IMAGE_WRITE:
    default:
        (*XAAFallbackOps.FillSpans) (pDraw, pGC, nInit, pptInit,
                                     pwidthInit, fSorted);
        return;
    }

    if ((nInit < 10) || (RegionNumRects(pGC->pCompositeClip) != 1))
        fastClip = FALSE;

    if (fastClip) {
        infoRec->ClipBox = &pGC->pCompositeClip->extents;
        (*function) (pGC, nInit, pptInit, pwidthInit, fSorted,
                     pDraw->x, pDraw->y);
        infoRec->ClipBox = NULL;
    }
    else
        XAAClipAndRenderSpans(pGC, pptInit, pwidthInit, nInit, fSorted,
                              function, pDraw->x, pDraw->y);
}

        /*********************\
	|     Solid Spans     |
	\*********************/

static void
XAARenderSolidSpans(GCPtr pGC,
                    int n,
                    DDXPointPtr ppt,
                    int *pwidth, int fSorted, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    (*infoRec->FillSolidSpans) (infoRec->pScrn, pGC->fgPixel,
                                pGC->alu, pGC->planemask, n, ppt, pwidth,
                                fSorted);
}

        /************************\
	|     Mono 8x8 Spans     |
	\************************/

static void
XAARenderMono8x8Spans(GCPtr pGC,
                      int n,
                      DDXPointPtr ppt,
                      int *pwidth, int fSorted, int xorg, int yorg)
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

    (*infoRec->FillMono8x8PatternSpans) (infoRec->pScrn,
                                         fg, bg, pGC->alu, pGC->planemask,
                                         n, ppt, pwidth, fSorted,
                                         pPriv->pattern0, pPriv->pattern1,
                                         (xorg + pGC->patOrg.x),
                                         (yorg + pGC->patOrg.y));
}

        /*************************\
	|     Color 8x8 Spans     |
	\*************************/

static void
XAARenderColor8x8Spans(GCPtr pGC,
                       int n,
                       DDXPointPtr ppt,
                       int *pwidth, int fSorted, int xorg, int yorg)
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

    (*infoRec->FillColor8x8PatternSpans) (infoRec->pScrn,
                                          pGC->alu, pGC->planemask, n, ppt,
                                          pwidth, fSorted, pCache,
                                          (yorg + pGC->patOrg.x),
                                          (xorg + pGC->patOrg.y));
}

        /****************************\
	|     Color Expand Spans     |
	\****************************/

static void
XAARenderColorExpandSpans(GCPtr pGC,
                          int n,
                          DDXPointPtr ppt,
                          int *pwidth, int fSorted, int xorg, int yorg)
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

    (*infoRec->FillColorExpandSpans) (infoRec->pScrn, fg, bg,
                                      pGC->alu, pGC->planemask, n, ppt, pwidth,
                                      fSorted, (xorg + pGC->patOrg.x),
                                      (yorg + pGC->patOrg.y), pGC->stipple);

}

        /*************************\
	|     Cache Blt Spans     |
	\*************************/

static void
XAARenderCacheBltSpans(GCPtr pGC,
                       int n,
                       DDXPointPtr ppt,
                       int *pwidth, int fSorted, int xorg, int yorg)
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

    (*infoRec->FillCacheBltSpans) (infoRec->pScrn,
                                   pGC->alu, pGC->planemask, n, ppt, pwidth,
                                   fSorted, pCache, (xorg + pGC->patOrg.x),
                                   (yorg + pGC->patOrg.y));

}

        /****************************\
	|     Cache Expand Spans     |
	\****************************/

static void
XAARenderCacheExpandSpans(GCPtr pGC,
                          int n,
                          DDXPointPtr ppt,
                          int *pwidth, int fSorted, int xorg, int yorg)
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

    (*infoRec->FillCacheExpandSpans) (infoRec->pScrn, fg, bg,
                                      pGC->alu, pGC->planemask, n, ppt, pwidth,
                                      fSorted, (xorg + pGC->patOrg.x),
                                      (yorg + pGC->patOrg.y), pGC->stipple);
}

        /***************************\
	|     Pixmap Copy Spans     |
	\***************************/

static void
XAARenderPixmapCopySpans(GCPtr pGC,
                         int n,
                         DDXPointPtr ppt,
                         int *pwidth, int fSorted, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    XAACacheInfoPtr pCache = &(infoRec->ScratchCacheInfoRec);
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pGC->tile.pixmap);

    pCache->x = pPriv->offscreenArea->box.x1;
    pCache->y = pPriv->offscreenArea->box.y1;
    pCache->w = pCache->orig_w = pPriv->offscreenArea->box.x2 - pCache->x;
    pCache->h = pCache->orig_h = pPriv->offscreenArea->box.y2 - pCache->y;
    pCache->trans_color = -1;

    (*infoRec->FillCacheBltSpans) (infoRec->pScrn,
                                   pGC->alu, pGC->planemask, n, ppt, pwidth,
                                   fSorted, pCache, (xorg + pGC->patOrg.x),
                                   (yorg + pGC->patOrg.y));
}

        /****************\
	|     Solid      |
	\****************/

void
XAAFillSolidSpans(ScrnInfoPtr pScrn,
                  int fg, int rop,
                  unsigned int planemask,
                  int n, DDXPointPtr ppt, int *pwidth, int fSorted)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);

    (*infoRec->SetupForSolidFill) (pScrn, fg, rop, planemask);

    if (infoRec->ClipBox)
        (*infoRec->SetClippingRectangle) (infoRec->pScrn,
                                          infoRec->ClipBox->x1,
                                          infoRec->ClipBox->y1,
                                          infoRec->ClipBox->x2 - 1,
                                          infoRec->ClipBox->y2 - 1);

    while (n--) {
        if (*pwidth > 0)
            (*infoRec->SubsequentSolidFillRect) (pScrn, ppt->x, ppt->y,
                                                 *pwidth, 1);
        ppt++;
        pwidth++;
    }

    if (infoRec->ClipBox)
        (*infoRec->DisableClipping) (infoRec->pScrn);

    SET_SYNC_FLAG(infoRec);
}

        /***************\
	|   Mono 8x8    |
	\***************/

void
XAAFillMono8x8PatternSpansScreenOrigin(ScrnInfoPtr pScrn,
                                       int fg, int bg, int rop,
                                       unsigned int planemask,
                                       int n,
                                       DDXPointPtr ppt,
                                       int *pwidth, int fSorted,
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

    if (infoRec->ClipBox)
        (*infoRec->SetClippingRectangle) (infoRec->pScrn,
                                          infoRec->ClipBox->x1,
                                          infoRec->ClipBox->y1,
                                          infoRec->ClipBox->x2 - 1,
                                          infoRec->ClipBox->y2 - 1);

    while (n--) {
        (*infoRec->SubsequentMono8x8PatternFillRect) (pScrn,
                                                      xorg, yorg, ppt->x,
                                                      ppt->y, *pwidth, 1);
        ppt++;
        pwidth++;
    }

    if (infoRec->ClipBox)
        (*infoRec->DisableClipping) (infoRec->pScrn);

    SET_SYNC_FLAG(infoRec);
}

void
XAAFillMono8x8PatternSpans(ScrnInfoPtr pScrn,
                           int fg, int bg, int rop,
                           unsigned int planemask,
                           int n,
                           DDXPointPtr ppt,
                           int *pwidth, int fSorted,
                           int pattern0, int pattern1, int xorigin, int yorigin)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int patx = pattern0, paty = pattern1;
    int xorg, yorg, slot;
    XAACacheInfoPtr pCache = NULL;

    if (!(infoRec->Mono8x8PatternFillFlags & HARDWARE_PATTERN_PROGRAMMED_BITS)) {
        pCache = (*infoRec->CacheMono8x8Pattern) (pScrn, pattern0, pattern1);
        patx = pCache->x;
        paty = pCache->y;
    }

    (*infoRec->SetupForMono8x8PatternFill) (pScrn, patx, paty,
                                            fg, bg, rop, planemask);

    if (infoRec->ClipBox)
        (*infoRec->SetClippingRectangle) (infoRec->pScrn,
                                          infoRec->ClipBox->x1,
                                          infoRec->ClipBox->y1,
                                          infoRec->ClipBox->x2 - 1,
                                          infoRec->ClipBox->y2 - 1);

    while (n--) {
        xorg = (ppt->x - xorigin) & 0x07;
        yorg = (ppt->y - yorigin) & 0x07;

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
                slot = (yorg << 3) + xorg;
                xorg = patx + pCache->offsets[slot].x;
                yorg = paty + pCache->offsets[slot].y;
            }
        }

        (*infoRec->SubsequentMono8x8PatternFillRect) (pScrn,
                                                      xorg, yorg, ppt->x,
                                                      ppt->y, *pwidth, 1);
        ppt++;
        pwidth++;
    }

    if (infoRec->ClipBox)
        (*infoRec->DisableClipping) (infoRec->pScrn);

    SET_SYNC_FLAG(infoRec);
}

        /****************\
	|   Color 8x8    |
	\****************/

void
XAAFillColor8x8PatternSpansScreenOrigin(ScrnInfoPtr pScrn,
                                        int rop,
                                        unsigned int planemask,
                                        int n,
                                        DDXPointPtr ppt,
                                        int *pwidth, int fSorted,
                                        XAACacheInfoPtr pCache,
                                        int xorigin, int yorigin)
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

    if (infoRec->ClipBox)
        (*infoRec->SetClippingRectangle) (infoRec->pScrn,
                                          infoRec->ClipBox->x1,
                                          infoRec->ClipBox->y1,
                                          infoRec->ClipBox->x2 - 1,
                                          infoRec->ClipBox->y2 - 1);

    while (n--) {
        (*infoRec->SubsequentColor8x8PatternFillRect) (pScrn,
                                                       xorg, yorg, ppt->x,
                                                       ppt->y, *pwidth, 1);
        ppt++;
        pwidth++;
    }

    if (infoRec->ClipBox)
        (*infoRec->DisableClipping) (infoRec->pScrn);

    SET_SYNC_FLAG(infoRec);
}

void
XAAFillColor8x8PatternSpans(ScrnInfoPtr pScrn,
                            int rop,
                            unsigned int planemask,
                            int n,
                            DDXPointPtr ppt,
                            int *pwidth, int fSorted,
                            XAACacheInfoPtr pCache, int xorigin, int yorigin)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int xorg, yorg, slot;

    (*infoRec->SetupForColor8x8PatternFill) (pScrn, pCache->x, pCache->y,
                                             rop, planemask,
                                             pCache->trans_color);

    if (infoRec->ClipBox)
        (*infoRec->SetClippingRectangle) (infoRec->pScrn,
                                          infoRec->ClipBox->x1,
                                          infoRec->ClipBox->y1,
                                          infoRec->ClipBox->x2 - 1,
                                          infoRec->ClipBox->y2 - 1);

    while (n--) {
        xorg = (ppt->x - xorigin) & 0x07;
        yorg = (ppt->y - yorigin) & 0x07;

        if (!(infoRec->Color8x8PatternFillFlags &
              HARDWARE_PATTERN_PROGRAMMED_ORIGIN)) {
            slot = (yorg << 3) + xorg;
            yorg = pCache->y + pCache->offsets[slot].y;
            xorg = pCache->x + pCache->offsets[slot].x;
        }

        (*infoRec->SubsequentColor8x8PatternFillRect) (pScrn,
                                                       xorg, yorg, ppt->x,
                                                       ppt->y, *pwidth, 1);
        ppt++;
        pwidth++;
    }

    if (infoRec->ClipBox)
        (*infoRec->DisableClipping) (infoRec->pScrn);

    SET_SYNC_FLAG(infoRec);
}

        /*****************\
	|   Cache Blit    |
	\*****************/

void
XAAFillCacheBltSpans(ScrnInfoPtr pScrn,
                     int rop,
                     unsigned int planemask,
                     int n,
                     DDXPointPtr ppt,
                     int *pwidth,
                     int fSorted, XAACacheInfoPtr pCache, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int x, w, phaseX, phaseY, blit_w;

    (*infoRec->SetupForScreenToScreenCopy) (pScrn, 1, 1, rop, planemask,
                                            pCache->trans_color);

    if (infoRec->ClipBox)
        (*infoRec->SetClippingRectangle) (infoRec->pScrn,
                                          infoRec->ClipBox->x1,
                                          infoRec->ClipBox->y1,
                                          infoRec->ClipBox->x2 - 1,
                                          infoRec->ClipBox->y2 - 1);

    while (n--) {
        x = ppt->x;
        w = *pwidth;
        phaseX = (x - xorg) % pCache->orig_w;
        if (phaseX < 0)
            phaseX += pCache->orig_w;
        phaseY = (ppt->y - yorg) % pCache->orig_h;
        if (phaseY < 0)
            phaseY += pCache->orig_h;

        while (1) {
            blit_w = pCache->w - phaseX;
            if (blit_w > w)
                blit_w = w;

            (*infoRec->SubsequentScreenToScreenCopy) (pScrn,
                                                      pCache->x + phaseX,
                                                      pCache->y + phaseY, x,
                                                      ppt->y, blit_w, 1);

            w -= blit_w;
            if (!w)
                break;
            x += blit_w;
            phaseX = (phaseX + blit_w) % pCache->orig_w;
        }
        ppt++;
        pwidth++;
    }

    if (infoRec->ClipBox)
        (*infoRec->DisableClipping) (infoRec->pScrn);

    SET_SYNC_FLAG(infoRec);
}

        /****************\
	|  Cache Expand  |
	\****************/

void
XAAFillCacheExpandSpans(ScrnInfoPtr pScrn,
                        int fg, int bg, int rop,
                        unsigned int planemask,
                        int n,
                        DDXPointPtr ppt,
                        int *pwidth,
                        int fSorted, int xorg, int yorg, PixmapPtr pPix)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int x, w, phaseX, phaseY, blit_w, cacheWidth;
    XAACacheInfoPtr pCache;

    pCache = (*infoRec->CacheMonoStipple) (pScrn, pPix);

    cacheWidth = (pCache->w * pScrn->bitsPerPixel) /
        infoRec->CacheColorExpandDensity;

    (*infoRec->SetupForScreenToScreenColorExpandFill) (pScrn, fg, bg, rop,
                                                       planemask);

    if (infoRec->ClipBox)
        (*infoRec->SetClippingRectangle) (infoRec->pScrn,
                                          infoRec->ClipBox->x1,
                                          infoRec->ClipBox->y1,
                                          infoRec->ClipBox->x2 - 1,
                                          infoRec->ClipBox->y2 - 1);

    while (n--) {
        x = ppt->x;
        w = *pwidth;
        phaseX = (x - xorg) % pCache->orig_w;
        if (phaseX < 0)
            phaseX += pCache->orig_w;
        phaseY = (ppt->y - yorg) % pCache->orig_h;
        if (phaseY < 0)
            phaseY += pCache->orig_h;

        while (1) {
            blit_w = cacheWidth - phaseX;
            if (blit_w > w)
                blit_w = w;

            (*infoRec->SubsequentScreenToScreenColorExpandFill) (pScrn, x,
                                                                 ppt->y, blit_w,
                                                                 1, pCache->x,
                                                                 pCache->y +
                                                                 phaseY,
                                                                 phaseX);

            w -= blit_w;
            if (!w)
                break;
            x += blit_w;
            phaseX = (phaseX + blit_w) % pCache->orig_w;
        }
        ppt++;
        pwidth++;
    }

    if (infoRec->ClipBox)
        (*infoRec->DisableClipping) (infoRec->pScrn);

    SET_SYNC_FLAG(infoRec);
}

void
XAAClipAndRenderSpans(GCPtr pGC,
                      DDXPointPtr ppt,
                      int *pwidth,
                      int nspans,
                      int fSorted,
                      ClipAndRenderSpansFunc func, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    DDXPointPtr pptNew, pptBase;
    int *pwidthBase, *pwidthNew;
    int Right, numRects, MaxBoxes;

    MaxBoxes = infoRec->PreAllocSize / (sizeof(DDXPointRec) + sizeof(int));
    pptBase = (DDXPointRec *) infoRec->PreAllocMem;
    pwidthBase = (int *) (&pptBase[MaxBoxes]);

    pptNew = pptBase;
    pwidthNew = pwidthBase;

    numRects = RegionNumRects(pGC->pCompositeClip);

    if (numRects == 1) {
        BoxPtr pextent = RegionRects(pGC->pCompositeClip);

        while (nspans--) {
            if ((pextent->y1 <= ppt->y) && (ppt->y < pextent->y2)) {
                pptNew->x = max(pextent->x1, ppt->x);
                Right = ppt->x + *pwidth;
                *pwidthNew = min(pextent->x2, Right) - pptNew->x;

                if (*pwidthNew > 0) {
                    pptNew->y = ppt->y;
                    pptNew++;
                    pwidthNew++;

                    if (pptNew >= (pptBase + MaxBoxes)) {
                        (*func) (pGC, MaxBoxes, pptBase, pwidthBase, fSorted,
                                 xorg, yorg);
                        pptNew = pptBase;
                        pwidthNew = pwidthBase;
                    }
                }
            }
            ppt++;
            pwidth++;
        }
    }
    else if (numRects) {
        BoxPtr pbox;
        int nbox;

        while (nspans--) {
            nbox = numRects;
            pbox = RegionRects(pGC->pCompositeClip);

            /* find the first band */
            while (nbox && (pbox->y2 <= ppt->y)) {
                pbox++;
                nbox--;
            }

            if (nbox && (pbox->y1 <= ppt->y)) {
                int orig_y = pbox->y1;

                Right = ppt->x + *pwidth;
                while (nbox && (orig_y == pbox->y1)) {
                    if (pbox->x2 <= ppt->x) {
                        nbox--;
                        pbox++;
                        continue;
                    }

                    if (pbox->x1 >= Right) {
                        nbox = 0;
                        break;
                    }

                    pptNew->x = max(pbox->x1, ppt->x);
                    *pwidthNew = min(pbox->x2, Right) - pptNew->x;
                    if (*pwidthNew > 0) {
                        pptNew->y = ppt->y;
                        pptNew++;
                        pwidthNew++;

                        if (pptNew >= (pptBase + MaxBoxes)) {
                            (*func) (pGC, MaxBoxes, pptBase, pwidthBase,
                                     fSorted, xorg, yorg);
                            pptNew = pptBase;
                            pwidthNew = pwidthBase;
                        }
                    }
                    pbox++;
                    nbox--;
                }
            }
            ppt++;
            pwidth++;
        }
    }

    if (pptNew != pptBase)
        (*func) (pGC, pptNew - pptBase, pptBase, pwidthBase, fSorted,
                 xorg, yorg);
}
