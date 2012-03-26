
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"
#include "migc.h"
#include "gcstruct.h"
#include "pixmapstr.h"

/*
  Written mostly by Harm Hanemaayer (H.Hanemaayer@inter.nl.net).
 */

RegionPtr
XAACopyArea(DrawablePtr pSrcDrawable,
            DrawablePtr pDstDrawable,
            GC * pGC,
            int srcx, int srcy, int width, int height, int dstx, int dsty)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    if (pDstDrawable->type == DRAWABLE_WINDOW) {
        if ((pSrcDrawable->type == DRAWABLE_WINDOW) ||
            IS_OFFSCREEN_PIXMAP(pSrcDrawable)) {
            if (infoRec->ScreenToScreenBitBlt &&
                CHECK_ROP(pGC, infoRec->ScreenToScreenBitBltFlags) &&
                CHECK_ROPSRC(pGC, infoRec->ScreenToScreenBitBltFlags) &&
                CHECK_PLANEMASK(pGC, infoRec->ScreenToScreenBitBltFlags))
                return (XAABitBlt(pSrcDrawable, pDstDrawable,
                                  pGC, srcx, srcy, width, height, dstx, dsty,
                                  XAADoBitBlt, 0L));
        }
        else {
            if (infoRec->WritePixmap &&
                ((pDstDrawable->bitsPerPixel == pSrcDrawable->bitsPerPixel) ||
                 ((pDstDrawable->bitsPerPixel == 24) &&
                  (pSrcDrawable->bitsPerPixel == 32) &&
                  (infoRec->WritePixmapFlags & CONVERT_32BPP_TO_24BPP))) &&
                CHECK_ROP(pGC, infoRec->WritePixmapFlags) &&
                CHECK_ROPSRC(pGC, infoRec->WritePixmapFlags) &&
                CHECK_PLANEMASK(pGC, infoRec->WritePixmapFlags) &&
                CHECK_NO_GXCOPY(pGC, infoRec->WritePixmapFlags))
                return (XAABitBlt(pSrcDrawable, pDstDrawable,
                                  pGC, srcx, srcy, width, height, dstx, dsty,
                                  XAADoImageWrite, 0L));
        }
    }
    else if (IS_OFFSCREEN_PIXMAP(pDstDrawable)) {
        if ((pSrcDrawable->type == DRAWABLE_WINDOW) ||
            IS_OFFSCREEN_PIXMAP(pSrcDrawable)) {
            if (infoRec->ScreenToScreenBitBlt &&
                CHECK_ROP(pGC, infoRec->ScreenToScreenBitBltFlags) &&
                CHECK_ROPSRC(pGC, infoRec->ScreenToScreenBitBltFlags) &&
                CHECK_PLANEMASK(pGC, infoRec->ScreenToScreenBitBltFlags))
                return (XAABitBlt(pSrcDrawable, pDstDrawable,
                                  pGC, srcx, srcy, width, height, dstx, dsty,
                                  XAADoBitBlt, 0L));
        }
    }

    return (XAAFallbackOps.CopyArea(pSrcDrawable, pDstDrawable, pGC,
                                    srcx, srcy, width, height, dstx, dsty));
}

void
XAADoBitBlt(DrawablePtr pSrc,
            DrawablePtr pDst, GC * pGC, RegionPtr prgnDst, DDXPointPtr pptSrc)
{
    int nbox, careful;
    BoxPtr pbox, pboxTmp, pboxNext, pboxBase, pboxNew1, pboxNew2;
    DDXPointPtr pptTmp, pptNew1, pptNew2;
    int xdir, ydir;
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);

    /* XXX we have to err on the side of safety when both are windows,
     * because we don't know if IncludeInferiors is being used.
     */
    careful = ((pSrc == pDst) ||
               ((pSrc->type == DRAWABLE_WINDOW) &&
                (pDst->type == DRAWABLE_WINDOW)));

    pbox = RegionRects(prgnDst);
    nbox = RegionNumRects(prgnDst);

    pboxNew1 = NULL;
    pptNew1 = NULL;
    pboxNew2 = NULL;
    pptNew2 = NULL;
    if (careful && (pptSrc->y < pbox->y1)) {
        /* walk source botttom to top */
        ydir = -1;

        if (nbox > 1) {
            /* keep ordering in each band, reverse order of bands */
            pboxNew1 = (BoxPtr) malloc(sizeof(BoxRec) * nbox);
            if (!pboxNew1)
                return;
            pptNew1 = (DDXPointPtr) malloc(sizeof(DDXPointRec) * nbox);
            if (!pptNew1) {
                free(pboxNew1);
                return;
            }
            pboxBase = pboxNext = pbox + nbox - 1;
            while (pboxBase >= pbox) {
                while ((pboxNext >= pbox) && (pboxBase->y1 == pboxNext->y1))
                    pboxNext--;
                pboxTmp = pboxNext + 1;
                pptTmp = pptSrc + (pboxTmp - pbox);
                while (pboxTmp <= pboxBase) {
                    *pboxNew1++ = *pboxTmp++;
                    *pptNew1++ = *pptTmp++;
                }
                pboxBase = pboxNext;
            }
            pboxNew1 -= nbox;
            pbox = pboxNew1;
            pptNew1 -= nbox;
            pptSrc = pptNew1;
        }
    }
    else {
        /* walk source top to bottom */
        ydir = 1;
    }

    if (careful && (pptSrc->x < pbox->x1)) {
        /* walk source right to left */
        xdir = -1;

        if (nbox > 1) {
            /* reverse order of rects in each band */
            pboxNew2 = (BoxPtr) malloc(sizeof(BoxRec) * nbox);
            pptNew2 = (DDXPointPtr) malloc(sizeof(DDXPointRec) * nbox);
            if (!pboxNew2 || !pptNew2) {
                free(pptNew2);
                free(pboxNew2);
                if (pboxNew1) {
                    free(pptNew1);
                    free(pboxNew1);
                }
                return;
            }
            pboxBase = pboxNext = pbox;
            while (pboxBase < pbox + nbox) {
                while ((pboxNext < pbox + nbox) &&
                       (pboxNext->y1 == pboxBase->y1))
                    pboxNext++;
                pboxTmp = pboxNext;
                pptTmp = pptSrc + (pboxTmp - pbox);
                while (pboxTmp != pboxBase) {
                    *pboxNew2++ = *--pboxTmp;
                    *pptNew2++ = *--pptTmp;
                }
                pboxBase = pboxNext;
            }
            pboxNew2 -= nbox;
            pbox = pboxNew2;
            pptNew2 -= nbox;
            pptSrc = pptNew2;
        }
    }
    else {
        /* walk source left to right */
        xdir = 1;
    }

    (*infoRec->ScreenToScreenBitBlt) (infoRec->pScrn, nbox, pptSrc, pbox,
                                      xdir, ydir, pGC->alu, pGC->planemask);

    if (pboxNew2) {
        free(pptNew2);
        free(pboxNew2);
    }
    if (pboxNew1) {
        free(pptNew1);
        free(pboxNew1);
    }

}

void
XAADoImageWrite(DrawablePtr pSrc,
                DrawablePtr pDst,
                GC * pGC, RegionPtr prgnDst, DDXPointPtr pptSrc)
{
    int srcwidth;
    unsigned char *psrcBase;    /* start of image */
    unsigned char *srcPntr;     /* index into the image */
    BoxPtr pbox = RegionRects(prgnDst);
    int nbox = RegionNumRects(prgnDst);
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int Bpp = pSrc->bitsPerPixel >> 3;

    psrcBase = (unsigned char *) ((PixmapPtr) pSrc)->devPrivate.ptr;
    srcwidth = (int) ((PixmapPtr) pSrc)->devKind;

    for (; nbox; pbox++, pptSrc++, nbox--) {
        srcPntr = psrcBase + (pptSrc->y * srcwidth) + (pptSrc->x * Bpp);

        (*infoRec->WritePixmap) (infoRec->pScrn, pbox->x1, pbox->y1,
                                 pbox->x2 - pbox->x1, pbox->y2 - pbox->y1,
                                 srcPntr, srcwidth, pGC->alu, pGC->planemask,
                                 -1, pSrc->bitsPerPixel, pSrc->depth);
    }
}

void
XAADoImageRead(DrawablePtr pSrc,
               DrawablePtr pDst,
               GC * pGC, RegionPtr prgnDst, DDXPointPtr pptSrc)
{
    int dstwidth;
    unsigned char *pdstBase;    /* start of image */
    unsigned char *dstPntr;     /* index into the image */
    BoxPtr pbox = RegionRects(prgnDst);
    int nbox = RegionNumRects(prgnDst);
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int Bpp = pSrc->bitsPerPixel >> 3;  /* wouldn't get here unless both
                                           src and dst have same bpp */

    pdstBase = (unsigned char *) ((PixmapPtr) pDst)->devPrivate.ptr;
    dstwidth = (int) ((PixmapPtr) pDst)->devKind;

    for (; nbox; pbox++, pptSrc++, nbox--) {
        dstPntr = pdstBase + (pbox->y1 * dstwidth) + (pbox->x1 * Bpp);

        (*infoRec->ReadPixmap) (infoRec->pScrn, pptSrc->x, pptSrc->y,
                                pbox->x2 - pbox->x1, pbox->y2 - pbox->y1,
                                dstPntr, dstwidth, pSrc->bitsPerPixel,
                                pSrc->depth);
    }
}

void
XAAScreenToScreenBitBlt(ScrnInfoPtr pScrn,
                        int nbox,
                        DDXPointPtr pptSrc,
                        BoxPtr pbox,
                        int xdir, int ydir, int alu, unsigned int planemask)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int dirsetup;

    if ((!(infoRec->CopyAreaFlags & ONLY_TWO_BITBLT_DIRECTIONS)
         || (xdir == ydir)) &&
        (!(infoRec->CopyAreaFlags & ONLY_LEFT_TO_RIGHT_BITBLT)
         || (xdir == 1))) {
        (*infoRec->SetupForScreenToScreenCopy) (pScrn,
                                                xdir, ydir, alu, planemask, -1);
        for (; nbox; pbox++, pptSrc++, nbox--)
            (*infoRec->SubsequentScreenToScreenCopy) (pScrn, pptSrc->x,
                                                      pptSrc->y, pbox->x1,
                                                      pbox->y1,
                                                      pbox->x2 - pbox->x1,
                                                      pbox->y2 - pbox->y1);
        SET_SYNC_FLAG(infoRec);
        return;
    }

    if (infoRec->CopyAreaFlags & ONLY_LEFT_TO_RIGHT_BITBLT) {
        /*
         * This is the case of a chip that only supports xdir = 1,
         * with ydir = 1 or ydir = -1, but we have xdir = -1.
         */
        (*infoRec->SetupForScreenToScreenCopy) (pScrn,
                                                1, ydir, alu, planemask, -1);
        for (; nbox; pbox++, pptSrc++, nbox--)
            if (pptSrc->y != pbox->y1 || pptSrc->x >= pbox->x1)
                /* No problem. Do a xdir = 1 blit instead. */
                (*infoRec->SubsequentScreenToScreenCopy) (pScrn,
                                                          pptSrc->x, pptSrc->y,
                                                          pbox->x1, pbox->y1,
                                                          pbox->x2 - pbox->x1,
                                                          pbox->y2 - pbox->y1);
            else {
                /*
                 * This is the difficult case. Needs striping into
                 * non-overlapping horizontal chunks.
                 */
                int stripeWidth, w, fullStripes, extra, i;

                stripeWidth = 16;
                w = pbox->x2 - pbox->x1;
                if (pbox->x1 - pptSrc->x < stripeWidth)
                    stripeWidth = pbox->x1 - pptSrc->x;
                fullStripes = w / stripeWidth;
                extra = w % stripeWidth;

                /* First, take care of the little bit on the far right */
                if (extra)
                    (*infoRec->SubsequentScreenToScreenCopy) (pScrn,
                                                              pptSrc->x +
                                                              fullStripes *
                                                              stripeWidth,
                                                              pptSrc->y,
                                                              pbox->x1 +
                                                              fullStripes *
                                                              stripeWidth,
                                                              pbox->y1, extra,
                                                              pbox->y2 -
                                                              pbox->y1);

                /* Now, take care of the rest of the blit */
                for (i = fullStripes - 1; i >= 0; i--)
                    (*infoRec->SubsequentScreenToScreenCopy) (pScrn,
                                                              pptSrc->x +
                                                              i * stripeWidth,
                                                              pptSrc->y,
                                                              pbox->x1 +
                                                              i * stripeWidth,
                                                              pbox->y1,
                                                              stripeWidth,
                                                              pbox->y2 -
                                                              pbox->y1);
            }
        SET_SYNC_FLAG(infoRec);
        return;
    }

    /*
     * Now the case of a chip that only supports xdir = ydir = 1 or
     * xdir = ydir = -1, but we have xdir != ydir.
     */
    dirsetup = 0;               /* No direction set up yet. */
    for (; nbox; pbox++, pptSrc++, nbox--) {
        if (xdir == 1 && pptSrc->y != pbox->y1) {
            /* Do a xdir = ydir = -1 blit instead. */
            if (dirsetup != -1) {
                (*infoRec->SetupForScreenToScreenCopy) (pScrn,
                                                        -1, -1, alu, planemask,
                                                        -1);
                dirsetup = -1;
            }
            (*infoRec->SubsequentScreenToScreenCopy) (pScrn, pptSrc->x,
                                                      pptSrc->y, pbox->x1,
                                                      pbox->y1,
                                                      pbox->x2 - pbox->x1,
                                                      pbox->y2 - pbox->y1);
        }
        else if (xdir == -1 && pptSrc->y != pbox->y1) {
            /* Do a xdir = ydir = 1 blit instead. */
            if (dirsetup != 1) {
                (*infoRec->SetupForScreenToScreenCopy) (pScrn,
                                                        1, 1, alu, planemask,
                                                        -1);
                dirsetup = 1;
            }
            (*infoRec->SubsequentScreenToScreenCopy) (pScrn, pptSrc->x,
                                                      pptSrc->y, pbox->x1,
                                                      pbox->y1,
                                                      pbox->x2 - pbox->x1,
                                                      pbox->y2 - pbox->y1);
        }
        else if (xdir == 1) {
            /*
             * xdir = 1, ydir = -1.
             * Perform line-by-line xdir = ydir = 1 blits, going up.
             */
            int i;

            if (dirsetup != 1) {
                (*infoRec->SetupForScreenToScreenCopy) (pScrn,
                                                        1, 1, alu, planemask,
                                                        -1);
                dirsetup = 1;
            }
            for (i = pbox->y2 - pbox->y1 - 1; i >= 0; i--)
                (*infoRec->SubsequentScreenToScreenCopy) (pScrn,
                                                          pptSrc->x,
                                                          pptSrc->y + i,
                                                          pbox->x1,
                                                          pbox->y1 + i,
                                                          pbox->x2 - pbox->x1,
                                                          1);
        }
        else {
            /*
             * xdir = -1, ydir = 1.
             * Perform line-by-line xdir = ydir = -1 blits, going down.
             */
            int i;

            if (dirsetup != -1) {
                (*infoRec->SetupForScreenToScreenCopy) (pScrn,
                                                        -1, -1, alu, planemask,
                                                        -1);
                dirsetup = -1;
            }
            for (i = 0; i < pbox->y2 - pbox->y1; i++)
                (*infoRec->SubsequentScreenToScreenCopy) (pScrn,
                                                          pptSrc->x,
                                                          pptSrc->y + i,
                                                          pbox->x1,
                                                          pbox->y1 + i,
                                                          pbox->x2 - pbox->x1,
                                                          1);
        }
    }                           /* next box */
    SET_SYNC_FLAG(infoRec);
}
