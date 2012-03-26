
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

/*
   Much of this file based on code by 
	Harm Hanemaayer (H.Hanemaayer@inter.nl.net).
*/

void
XAAPolyRectangleThinSolid(DrawablePtr pDrawable,
                          GCPtr pGC, int nRectsInit, xRectangle *pRectsInit)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int nClipRects;             /* number of clip rectangles */
    BoxPtr pClipRects;          /* points to the list of clip rects */
    int xOrigin;                /* Drawables x origin */
    int yOrigin;                /* Drawables x origin */
    xRectangle *pRect;          /* list of rects */
    int nRects;                 /* running count of number of rects */
    int origX1, origY1;         /* original rectangle's U/L corner */
    int origX2, origY2;         /* original rectangle's L/R corner */
    int clippedX1;              /* clipped rectangle's left x */
    int clippedY1;              /* clipped rectangle's top y */
    int clippedX2;              /* clipped rectangle's right x */
    int clippedY2;              /* clipped rectangle's bottom y */
    int clipXMin;               /* upper left corner of clip rect */
    int clipYMin;               /* upper left corner of clip rect */
    int clipXMax;               /* lower right corner of clip rect */
    int clipYMax;               /* lower right corner of clip rect */
    int width, height;          /* width and height of rect */

    nClipRects = RegionNumRects(pGC->pCompositeClip);
    pClipRects = RegionRects(pGC->pCompositeClip);

    if (!nClipRects)
        return;

    xOrigin = pDrawable->x;
    yOrigin = pDrawable->y;

    (*infoRec->SetupForSolidLine) (infoRec->pScrn,
                                   pGC->fgPixel, pGC->alu, pGC->planemask);

    for (; nClipRects > 0; nClipRects--, pClipRects++) {
        clipYMin = pClipRects->y1;
        clipYMax = pClipRects->y2 - 1;
        clipXMin = pClipRects->x1;
        clipXMax = pClipRects->x2 - 1;

        for (pRect = pRectsInit, nRects = nRectsInit;
             nRects > 0; nRects--, pRect++) {
            /* translate rectangle data over to the drawable */
            origX1 = pRect->x + xOrigin;
            origY1 = pRect->y + yOrigin;
            origX2 = origX1 + pRect->width;
            origY2 = origY1 + pRect->height;

            /* reject entire rectangle if completely outside clip rect */
            if ((origX1 > clipXMax) || (origX2 < clipXMin) ||
                (origY1 > clipYMax) || (origY2 < clipYMin))
                continue;

            /* clip the rectangle */
            clippedX1 = max(origX1, clipXMin);
            clippedX2 = min(origX2, clipXMax);
            clippedY1 = max(origY1, clipYMin);
            clippedY2 = min(origY2, clipYMax);

            width = clippedX2 - clippedX1 + 1;

            if (origY1 >= clipYMin) {
                (*infoRec->SubsequentSolidHorVertLine) (infoRec->pScrn,
                                                        clippedX1, clippedY1,
                                                        width, DEGREES_0);

                /* don't overwrite corner */
                clippedY1++;
            }

            if ((origY2 <= clipYMax) && (origY1 != origY2)) {
                (*infoRec->SubsequentSolidHorVertLine) (infoRec->pScrn,
                                                        clippedX1, clippedY2,
                                                        width, DEGREES_0);

                /* don't overwrite corner */
                clippedY2--;
            }

            if (clippedY2 < clippedY1)
                continue;

            height = clippedY2 - clippedY1 + 1;

            /* draw vertical edges using lines if not clipped out */
            if (origX1 >= clipXMin)
                (*infoRec->SubsequentSolidHorVertLine) (infoRec->pScrn,
                                                        clippedX1, clippedY1,
                                                        height, DEGREES_270);

            if ((origX2 <= clipXMax) && (origX2 != origX1))
                (*infoRec->SubsequentSolidHorVertLine) (infoRec->pScrn,
                                                        clippedX2, clippedY1,
                                                        height, DEGREES_270);
        }
    }

    SET_SYNC_FLAG(infoRec);
}
