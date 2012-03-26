
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
 XAAPolySegmentDashed(DrawablePtr pDrawable,
                      GCPtr pGC, int nseg, xSegment * pSeg
#else
XAAPolyLinesDashed(DrawablePtr pDrawable, GCPtr pGC, int mode,  /* Origin or Previous */
                   int npt,     /* number of points */
                   DDXPointPtr pptInit
#endif
    )
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    XAAGCPtr pGCPriv = (XAAGCPtr) dixLookupPrivate(&pGC->devPrivates,
                                                   XAAGetGCKey());
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
    unsigned int oc1, oc2;
    int dmin, dmaj, e, octant;
    int x1, x2, y1, y2, tmp, len, offset;
    int PatternLength, PatternOffset;

    if (!nboxInit)
        return;

    if (infoRec->DashedLineFlags & LINE_LIMIT_COORDS) {
        int minValX = infoRec->DashedLineLimits.x1;
        int maxValX = infoRec->DashedLineLimits.x2;
        int minValY = infoRec->DashedLineLimits.y1;
        int maxValY = infoRec->DashedLineLimits.y2;

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

    PatternLength = pGCPriv->DashLength;
    PatternOffset = pGC->dashOffset % PatternLength;

    (*infoRec->SetupForDashedLine) (infoRec->pScrn, pGC->fgPixel,
                                    (pGC->lineStyle ==
                                     LineDoubleDash) ? pGC->bgPixel : -1,
                                    pGC->alu, pGC->planemask, PatternLength,
                                    pGCPriv->DashPattern);

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

        if (infoRec->SubsequentDashedBresenhamLine) {
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
        else {                  /* Muffle compiler */
            dmin = dmaj = e = octant = len = 0;
        }

        while (nbox--) {
            oc1 = oc2 = 0;
            OUTCODES(oc1, x1, y1, pbox);
            OUTCODES(oc2, x2, y2, pbox);
            if (!(oc1 | oc2)) { /* uncliped */
                if (infoRec->SubsequentDashedTwoPointLine) {
                    (*infoRec->SubsequentDashedTwoPointLine) (infoRec->pScrn,
                                                              x1, y1, x2, y2,
#ifdef POLYSEGMENT
                                                              (pGC->capStyle !=
                                                               CapNotLast) ? 0 :
#endif
                                                              OMIT_LAST,
                                                              PatternOffset);
                }
                else {
                    (*infoRec->SubsequentDashedBresenhamLine) (infoRec->pScrn,
                                                               x1, y1, dmaj,
                                                               dmin, e,
#ifdef POLYSEGMENT
                                                               (pGC->capStyle !=
                                                                CapNotLast)
                                                               ? (len + 1) :
#endif
                                                               len, octant,
                                                               PatternOffset);
                }
                break;
            }
            else if (oc1 & oc2) {       /* completely clipped */
                pbox++;
            }
            else if (infoRec->ClippingFlags & HARDWARE_CLIP_DASHED_LINE) {
                (*infoRec->SetClippingRectangle) (infoRec->pScrn,
                                                  pbox->x1, pbox->y1,
                                                  pbox->x2 - 1, pbox->y2 - 1);

                if (infoRec->SubsequentDashedBresenhamLine) {
                    (*infoRec->SubsequentDashedBresenhamLine) (infoRec->pScrn,
                                                               x1, y1, dmaj,
                                                               dmin, e,
#ifdef POLYSEGMENT
                                                               (pGC->capStyle !=
                                                                CapNotLast)
                                                               ? (len + 1) :
#endif
                                                               len, octant,
                                                               PatternOffset);
                }
                else {
                    (*infoRec->SubsequentDashedTwoPointLine) (infoRec->pScrn,
                                                              x1, y1, x2, y2,
#ifdef POLYSEGMENT
                                                              (pGC->capStyle !=
                                                               CapNotLast) ? 0 :
#endif
                                                              OMIT_LAST,
                                                              PatternOffset);
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

#define range infoRec->DashedBresenhamLineErrorTermBits
                    abserr = abs(err);
                    while ((abserr & range) || (dmaj & range) || (dmin & range)) {
                        dmin >>= 1;
                        dmaj >>= 1;
                        abserr >>= 1;
                        err /= 2;
                    }

                    if (octant & YMAJOR)
                        offset = abs(new_y1 - y1);
                    else
                        offset = abs(new_x1 - x1);

                    offset += PatternOffset;
                    offset %= PatternLength;

                    (*infoRec->SubsequentDashedBresenhamLine) (infoRec->pScrn,
                                                               new_x1, new_y1,
                                                               dmaj, dmin, err,
                                                               len, octant,
                                                               offset);
                }
                pbox++;
            }
        }                       /* while (nbox--) */
#ifndef POLYSEGMENT
        len = abs(y2 - y1);
        tmp = abs(x2 - x1);
        PatternOffset += (len > tmp) ? len : tmp;
        PatternOffset %= PatternLength;
#endif
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
                if (infoRec->SubsequentDashedTwoPointLine) {
                    (*infoRec->SubsequentDashedTwoPointLine) (infoRec->pScrn,
                                                              x2, y2, x2, y2, 0,
                                                              PatternOffset);
                }
                else {
                    (*infoRec->SubsequentDashedBresenhamLine) (infoRec->pScrn,
                                                               x2, y2, 2, 0, -1,
                                                               1, 0,
                                                               PatternOffset);
                }
                break;
            }
            else
                pbox++;
        }
    }
#endif

    SET_SYNC_FLAG(infoRec);
}
