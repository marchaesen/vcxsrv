
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <stdlib.h>

#include <X11/X.h>
#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include "scrnintstr.h"
#include "pixmapstr.h"
#include "miline.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"

void
#ifdef POLYSEGMENT
 XAAPolySegment(DrawablePtr pDrawable, GCPtr pGC, int nseg, xSegment * pSeg
#else
XAAPolyLines(DrawablePtr pDrawable, GCPtr pGC, int mode,        /* Origin or Previous */
             int npt,           /* number of points */
             DDXPointPtr pptInit
#endif
    )
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    BoxPtr pboxInit = RegionRects(pGC->pCompositeClip);
    int nboxInit = RegionNumRects(pGC->pCompositeClip);
    unsigned int bias = miGetZeroLineBias(pDrawable->pScreen);
    int xorg = pDrawable->x;
    int yorg = pDrawable->y;
    int nbox;
    BoxPtr pbox;

#ifndef POLYSEGMENT
    DDXPointPtr ppt;
#endif
    int x1, x2, y1, y2, tmp, len;

    if (!nboxInit)
        return;

    if (infoRec->SolidLineFlags & LINE_LIMIT_COORDS) {
        int minValX = infoRec->SolidLineLimits.x1;
        int maxValX = infoRec->SolidLineLimits.x2;
        int minValY = infoRec->SolidLineLimits.y1;
        int maxValY = infoRec->SolidLineLimits.y2;

#ifdef POLYSEGMENT
        int n = nseg;
        xSegment *s = pSeg;

        while (n--)
#else
        int n = npt;
        int xorgtmp = xorg;
        int yorgtmp = yorg;

        ppt = pptInit;
        x2 = ppt->x + xorgtmp;
        y2 = ppt->y + yorgtmp;
        while (--n)
#endif
        {
#ifdef POLYSEGMENT
            x1 = s->x1 + xorg;
            y1 = s->y1 + yorg;
            x2 = s->x2 + xorg;
            y2 = s->y2 + yorg;
            s++;
#else
            x1 = x2;
            y1 = y2;
            ++ppt;
            if (mode == CoordModePrevious) {
                xorgtmp = x1;
                yorgtmp = y1;
            }
            x2 = ppt->x + xorgtmp;
            y2 = ppt->y + yorgtmp;
#endif
            if (x1 > maxValX || x1 < minValX ||
                x2 > maxValX || x2 < minValX ||
                y1 > maxValY || y1 < minValY || y2 > maxValY || y2 < minValY) {
#ifdef POLYSEGMENT
                XAAFallbackOps.PolySegment(pDrawable, pGC, nseg, pSeg);
#else
                XAAFallbackOps.Polylines(pDrawable, pGC, mode, npt, pptInit);
#endif
                return;
            }
        }
    }

    (*infoRec->SetupForSolidLine) (infoRec->pScrn, pGC->fgPixel,
                                   pGC->alu, pGC->planemask);

#ifdef POLYSEGMENT
    while (nseg--)
#else
    ppt = pptInit;
    x2 = ppt->x + xorg;
    y2 = ppt->y + yorg;
    while (--npt)
#endif
    {
        nbox = nboxInit;
        pbox = pboxInit;

#ifdef POLYSEGMENT
        x1 = pSeg->x1 + xorg;
        y1 = pSeg->y1 + yorg;
        x2 = pSeg->x2 + xorg;
        y2 = pSeg->y2 + yorg;
        pSeg++;
#else
        x1 = x2;
        y1 = y2;
        ++ppt;
        if (mode == CoordModePrevious) {
            xorg = x1;
            yorg = y1;
        }
        x2 = ppt->x + xorg;
        y2 = ppt->y + yorg;
#endif

        if (x1 == x2) {         /* vertical line */
            /* make the line go top to bottom of screen, keeping
               endpoint semantics
             */
            if (y1 > y2) {
                tmp = y2;
                y2 = y1 + 1;
                y1 = tmp + 1;
#ifdef POLYSEGMENT
                if (pGC->capStyle != CapNotLast)
                    y1--;
#endif
            }
#ifdef POLYSEGMENT
            else if (pGC->capStyle != CapNotLast)
                y2++;
#endif
            /* get to first band that might contain part of line */
            while (nbox && (pbox->y2 <= y1)) {
                pbox++;
                nbox--;
            }

            /* stop when lower edge of box is beyond end of line */
            while (nbox && (y2 >= pbox->y1)) {
                if ((x1 >= pbox->x1) && (x1 < pbox->x2)) {
                    tmp = max(y1, pbox->y1);
                    len = min(y2, pbox->y2) - tmp;
                    if (len)
                        (*infoRec->SubsequentSolidHorVertLine) (infoRec->pScrn,
                                                                x1, tmp, len,
                                                                DEGREES_270);
                }
                nbox--;
                pbox++;
            }
#ifndef POLYSEGMENT
            y2 = ppt->y + yorg;
#endif
        }
        else if (y1 == y2) {    /* horizontal line */
            /* force line from left to right, keeping endpoint semantics */
            if (x1 > x2) {
                tmp = x2;
                x2 = x1 + 1;
                x1 = tmp + 1;
#ifdef POLYSEGMENT
                if (pGC->capStyle != CapNotLast)
                    x1--;
#endif
            }
#ifdef POLYSEGMENT
            else if (pGC->capStyle != CapNotLast)
                x2++;
#endif

            /* find the correct band */
            while (nbox && (pbox->y2 <= y1)) {
                pbox++;
                nbox--;
            }

            /* try to draw the line, if we haven't gone beyond it */
            if (nbox && (pbox->y1 <= y1)) {
                int orig_y = pbox->y1;

                /* when we leave this band, we're done */
                while (nbox && (orig_y == pbox->y1)) {
                    if (pbox->x2 <= x1) {
                        /* skip boxes until one might contain start point */
                        nbox--;
                        pbox++;
                        continue;
                    }

                    /* stop if left of box is beyond right of line */
                    if (pbox->x1 >= x2) {
                        nbox = 0;
                        break;
                    }

                    tmp = max(x1, pbox->x1);
                    len = min(x2, pbox->x2) - tmp;
                    if (len)
                        (*infoRec->SubsequentSolidHorVertLine) (infoRec->pScrn,
                                                                tmp, y1, len,
                                                                DEGREES_0);
                    nbox--;
                    pbox++;
                }
            }
#ifndef POLYSEGMENT
            x2 = ppt->x + xorg;
#endif
        }
        else {                  /* sloped line */
            unsigned int oc1, oc2;
            int dmin, dmaj, e, octant;

            if (infoRec->SubsequentSolidBresenhamLine) {
                if ((dmaj = x2 - x1) < 0) {
                    dmaj = -dmaj;
                    octant = XDECREASING;
                }
                else
                    octant = 0;

                if ((dmin = y2 - y1) < 0) {
                    dmin = -dmin;
                    octant |= YDECREASING;
                }

                if (dmin >= dmaj) {
                    tmp = dmin;
                    dmin = dmaj;
                    dmaj = tmp;
                    octant |= YMAJOR;
                }

                e = -dmaj - ((bias >> octant) & 1);
                len = dmaj;
                dmin <<= 1;
                dmaj <<= 1;
            }
            else {              /* Muffle compiler */
                dmin = dmaj = e = octant = len = 0;
            }

            while (nbox--) {
                oc1 = oc2 = 0;
                OUTCODES(oc1, x1, y1, pbox);
                OUTCODES(oc2, x2, y2, pbox);
                if (!(oc1 | oc2)) {     /* unclipped */
                    if (infoRec->SubsequentSolidTwoPointLine) {
                        (*infoRec->SubsequentSolidTwoPointLine) (infoRec->pScrn,
                                                                 x1, y1, x2, y2,
#ifdef POLYSEGMENT
                                                                 (pGC->
                                                                  capStyle !=
                                                                  CapNotLast) ?
                                                                 0 :
#endif
                                                                 OMIT_LAST);
                    }
                    else {
                        (*infoRec->SubsequentSolidBresenhamLine) (infoRec->
                                                                  pScrn, x1, y1,
                                                                  dmaj, dmin, e,
#ifdef POLYSEGMENT
                                                                  (pGC->
                                                                   capStyle !=
                                                                   CapNotLast)
                                                                  ? (len + 1) :
#endif
                                                                  len, octant);
                    }
                    break;
                }
                else if (oc1 & oc2) {   /* completely clipped */
                    pbox++;
                }
                else if (infoRec->ClippingFlags & HARDWARE_CLIP_SOLID_LINE) {
                    (*infoRec->SetClippingRectangle) (infoRec->pScrn,
                                                      pbox->x1, pbox->y1,
                                                      pbox->x2 - 1,
                                                      pbox->y2 - 1);

                    if (infoRec->SubsequentSolidBresenhamLine) {
                        (*infoRec->SubsequentSolidBresenhamLine) (infoRec->
                                                                  pScrn, x1, y1,
                                                                  dmaj, dmin, e,
#ifdef POLYSEGMENT
                                                                  (pGC->
                                                                   capStyle !=
                                                                   CapNotLast)
                                                                  ? (len + 1) :
#endif
                                                                  len, octant);
                    }
                    else {
                        (*infoRec->SubsequentSolidTwoPointLine) (infoRec->pScrn,
                                                                 x1, y1, x2, y2,
#ifdef POLYSEGMENT
                                                                 (pGC->
                                                                  capStyle !=
                                                                  CapNotLast) ?
                                                                 0 :
#endif
                                                                 OMIT_LAST);
                    }
                    (*infoRec->DisableClipping) (infoRec->pScrn);
                    pbox++;
                }
                else {
                    int new_x1 = x1, new_y1 = y1, new_x2 = x2, new_y2 = y2;
                    int clip1 = 0, clip2 = 0;
                    int err, adx, ady;

                    if (octant & YMAJOR) {
                        ady = dmaj >> 1;
                        adx = dmin >> 1;
                    }
                    else {
                        ady = dmin >> 1;
                        adx = dmaj >> 1;
                    }

                    if (miZeroClipLine(pbox->x1, pbox->y1,
                                       pbox->x2 - 1, pbox->y2 - 1,
                                       &new_x1, &new_y1, &new_x2, &new_y2,
                                       adx, ady, &clip1, &clip2,
                                       octant, bias, oc1, oc2) == -1) {
                        pbox++;
                        continue;
                    }

                    if (octant & YMAJOR)
                        len = abs(new_y2 - new_y1);
                    else
                        len = abs(new_x2 - new_x1);
#ifdef POLYSEGMENT
                    if (clip2 != 0 || pGC->capStyle != CapNotLast)
                        len++;
#else
                    len += (clip2 != 0);
#endif
                    if (len) {
                        int abserr, clipdx, clipdy;

                        /* unwind bresenham error term to first point */
                        if (clip1) {
                            clipdx = abs(new_x1 - x1);
                            clipdy = abs(new_y1 - y1);

                            if (octant & YMAJOR)
                                err = e + clipdy * dmin - clipdx * dmaj;
                            else
                                err = e + clipdx * dmin - clipdy * dmaj;
                        }
                        else
                            err = e;

#define range infoRec->SolidBresenhamLineErrorTermBits
                        abserr = abs(err);
                        while ((abserr & range) ||
                               (dmaj & range) || (dmin & range)) {
                            dmin >>= 1;
                            dmaj >>= 1;
                            abserr >>= 1;
                            err /= 2;
                        }

                        (*infoRec->SubsequentSolidBresenhamLine) (infoRec->
                                                                  pScrn, new_x1,
                                                                  new_y1, dmaj,
                                                                  dmin, err,
                                                                  len, octant);
                    }
                    pbox++;
                }
            }                   /* while (nbox--) */
        }                       /* sloped line */
    }                           /* while (nline--) */

#ifndef POLYSEGMENT
    /* paint the last point if the end style isn't CapNotLast.
       (Assume that a projecting, butt, or round cap that is one
       pixel wide is the same as the single pixel of the endpoint.)
     */

    if ((pGC->capStyle != CapNotLast) &&
        ((ppt->x + xorg != pptInit->x + pDrawable->x) ||
         (ppt->y + yorg != pptInit->y + pDrawable->y) ||
         (ppt == pptInit + 1))) {
        nbox = nboxInit;
        pbox = pboxInit;
        while (nbox--) {
            if ((x2 >= pbox->x1) && (y2 >= pbox->y1) &&
                (x2 < pbox->x2) && (y2 < pbox->y2)) {
                (*infoRec->SubsequentSolidHorVertLine) (infoRec->pScrn, x2, y2,
                                                        1, DEGREES_0);
                break;
            }
            else
                pbox++;
        }
    }
#endif

    SET_SYNC_FLAG(infoRec);
}
