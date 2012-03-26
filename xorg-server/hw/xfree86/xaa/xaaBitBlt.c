
/* 
   This is a lighter version of cfbBitBlt.  We calculate the boxes
   when accelerating pixmap->screen and screen->screen copies. 
   We also pass the GC to the doBitBlt function so that it has access
   to the fg and bg so CopyPlane can use this. 
*/

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "mi.h"
#include "pixmapstr.h"
#include "gcstruct.h"
#include "windowstr.h"
#include "xaalocal.h"

RegionPtr
XAABitBlt(DrawablePtr pSrcDrawable,
          DrawablePtr pDstDrawable,
          GC * pGC,
          int srcx, int srcy,
          int width, int height,
          int dstx, int dsty,
          void (*doBitBlt) (DrawablePtr, DrawablePtr, GCPtr, RegionPtr,
                            DDXPointPtr), unsigned long bitPlane)
{

    RegionPtr prgnSrcClip = NULL;       /* may be a new region, or just a copy */
    RegionPtr prgnExposed;
    Bool freeSrcClip = FALSE;
    RegionRec rgnDst;
    DDXPointPtr pptSrc, ppt;
    DDXPointRec origDest;
    BoxPtr pbox;
    BoxRec fastBox;
    int i, dx, dy, numRects;
    xRectangle origSource;
    int fastClip = 0;           /* for fast clipping with pixmap source */
    int fastExpose = 0;         /* for fast exposures with pixmap source */

    origSource.x = srcx;
    origSource.y = srcy;
    origSource.width = width;
    origSource.height = height;
    origDest.x = dstx;
    origDest.y = dsty;

    if (pSrcDrawable->pScreen->SourceValidate) {
        (*pSrcDrawable->pScreen->SourceValidate) (pSrcDrawable, srcx, srcy,
                                                  width, height,
                                                  pGC->subWindowMode);
    }

    srcx += pSrcDrawable->x;
    srcy += pSrcDrawable->y;

    /* clip the source */
    if (pSrcDrawable->type == DRAWABLE_PIXMAP) {
        if ((pSrcDrawable == pDstDrawable) && (pGC->clientClipType == CT_NONE))
            prgnSrcClip = pGC->pCompositeClip;
        else
            fastClip = 1;
    }
    else {                      /* Window */
        if (pGC->subWindowMode == IncludeInferiors) {
            if (!((WindowPtr) pSrcDrawable)->parent) {
                /*
                 * special case bitblt from root window in
                 * IncludeInferiors mode; just like from a pixmap
                 */
                fastClip = 1;
            }
            else if ((pSrcDrawable == pDstDrawable) &&
                     (pGC->clientClipType == CT_NONE)) {
                prgnSrcClip = pGC->pCompositeClip;
            }
            else {
                prgnSrcClip = NotClippedByChildren((WindowPtr) pSrcDrawable);
                freeSrcClip = TRUE;
            }
        }
        else {
            prgnSrcClip = &((WindowPtr) pSrcDrawable)->clipList;
        }
    }

    fastBox.x1 = srcx;
    fastBox.y1 = srcy;
    fastBox.x2 = srcx + width;
    fastBox.y2 = srcy + height;

    /* Don't create a source region if we are doing a fast clip */
    if (fastClip) {
        fastExpose = 1;
        /*
         * clip the source; if regions extend beyond the source size,
         * make sure exposure events get sent
         */
        if (fastBox.x1 < pSrcDrawable->x) {
            fastBox.x1 = pSrcDrawable->x;
            fastExpose = 0;
        }
        if (fastBox.y1 < pSrcDrawable->y) {
            fastBox.y1 = pSrcDrawable->y;
            fastExpose = 0;
        }
        if (fastBox.x2 > pSrcDrawable->x + (int) pSrcDrawable->width) {
            fastBox.x2 = pSrcDrawable->x + (int) pSrcDrawable->width;
            fastExpose = 0;
        }
        if (fastBox.y2 > pSrcDrawable->y + (int) pSrcDrawable->height) {
            fastBox.y2 = pSrcDrawable->y + (int) pSrcDrawable->height;
            fastExpose = 0;
        }
    }
    else {
        RegionInit(&rgnDst, &fastBox, 1);
        RegionIntersect(&rgnDst, &rgnDst, prgnSrcClip);
    }

    dstx += pDstDrawable->x;
    dsty += pDstDrawable->y;

    if (pDstDrawable->type == DRAWABLE_WINDOW) {
        if (!((WindowPtr) pDstDrawable)->realized) {
            if (!fastClip)
                RegionUninit(&rgnDst);
            if (freeSrcClip)
                RegionDestroy(prgnSrcClip);
            return NULL;
        }
    }

    dx = srcx - dstx;
    dy = srcy - dsty;

    /* Translate and clip the dst to the destination composite clip */
    if (fastClip) {
        RegionPtr cclip;

        /* Translate the region directly */
        fastBox.x1 -= dx;
        fastBox.x2 -= dx;
        fastBox.y1 -= dy;
        fastBox.y2 -= dy;

        /* If the destination composite clip is one rectangle we can
           do the clip directly.  Otherwise we have to create a full
           blown region and call intersect */

        cclip = pGC->pCompositeClip;
        if (RegionNumRects(cclip) == 1) {
            BoxPtr pBox = RegionRects(cclip);

            if (fastBox.x1 < pBox->x1)
                fastBox.x1 = pBox->x1;
            if (fastBox.x2 > pBox->x2)
                fastBox.x2 = pBox->x2;
            if (fastBox.y1 < pBox->y1)
                fastBox.y1 = pBox->y1;
            if (fastBox.y2 > pBox->y2)
                fastBox.y2 = pBox->y2;

            /* Check to see if the region is empty */
            if (fastBox.x1 >= fastBox.x2 || fastBox.y1 >= fastBox.y2) {
                RegionNull(&rgnDst);
            }
            else {
                RegionInit(&rgnDst, &fastBox, 1);
            }
        }
        else {
            /* We must turn off fastClip now, since we must create
               a full blown region.  It is intersected with the
               composite clip below. */
            fastClip = 0;
            RegionInit(&rgnDst, &fastBox, 1);
        }
    }
    else {
        RegionTranslate(&rgnDst, -dx, -dy);
    }

    if (!fastClip) {
        RegionIntersect(&rgnDst, &rgnDst, pGC->pCompositeClip);
    }

    /* Do bit blitting */
    numRects = RegionNumRects(&rgnDst);
    if (numRects && width && height) {
        if (!(pptSrc = (DDXPointPtr) malloc(numRects * sizeof(DDXPointRec)))) {
            RegionUninit(&rgnDst);
            if (freeSrcClip)
                RegionDestroy(prgnSrcClip);
            return NULL;
        }
        pbox = RegionRects(&rgnDst);
        ppt = pptSrc;
        for (i = numRects; --i >= 0; pbox++, ppt++) {
            ppt->x = pbox->x1 + dx;
            ppt->y = pbox->y1 + dy;
        }

        (*doBitBlt) (pSrcDrawable, pDstDrawable, pGC, &rgnDst, pptSrc);
        free(pptSrc);
    }

    prgnExposed = NULL;
    if (pGC->fExpose) {
        /* Pixmap sources generate a NoExposed (we return NULL to do this) */
        if (!fastExpose)
            prgnExposed = miHandleExposures(pSrcDrawable, pDstDrawable, pGC,
                                            origSource.x, origSource.y,
                                            (int) origSource.width,
                                            (int) origSource.height,
                                            origDest.x, origDest.y, bitPlane);
    }
    RegionUninit(&rgnDst);
    if (freeSrcClip)
        RegionDestroy(prgnSrcClip);
    return prgnExposed;
}
