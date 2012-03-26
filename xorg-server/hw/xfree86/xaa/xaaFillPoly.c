
/*
 * Copyright 1996  The XFree86 Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation 
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL 
 * HARM HANEMAAYER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF 
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
 * SOFTWARE.
 * 
 */

/*
 * Written by Mark Vojkovich.  Loosly based on an original version
 * written by Harm Hanemaayer (H.Hanemaayer@inter.nl.net) which
 * only did solid rectangles and didn't have trapezoid support.
 *
 */

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
#include "micoord.h"

#include "xaa.h"
#include "xaalocal.h"

#define POLY_USE_MI		0
#define POLY_FULLY_CLIPPED	1
#define POLY_IS_EASY		2

#define Setup(c,x,vertex,dx,dy,e,sign,step,DX) {\
    x = intToX(vertex); \
    if ((dy = intToY(c) - y)) { \
    	DX = dx = intToX(c) - x; \
	step = 0; \
    	if (dx >= 0) \
    	{ \
	    e = 0; \
	    sign = 1; \
	    if (dx >= dy) {\
	    	step = dx / dy; \
	    	dx %= dy; \
	    } \
    	} \
    	else \
    	{ \
	    e = 1 - dy; \
	    sign = -1; \
	    dx = -dx; \
	    if (dx >= dy) { \
		step = - (dx / dy); \
		dx %= dy; \
	    } \
    	} \
    } \
    x += origin; \
    vertex = c; \
}

#define Step(x,dx,dy,e,sign,step) {\
    x += step; \
    if ((e += dx) > 0) \
    { \
	x += sign; \
	e -= dy; \
    } \
}

#define FixError(x, dx, dy, e, sign, step, h)	{	\
	   e += (h) * dx;				\
	   x += (h) * step;				\
	   if(e > 0) {					\
		x += e * sign/dy;			\
		e %= dy;				\
	   	if(e) {					\
		   x += sign;				\
		   e -= dy;				\
		}					\
	   } 	 					\
}

/*
   XAAIsEasyPoly -

   Checks CoordModeOrigin one rect polygons to see if we need
   to use Mi.
   Returns: POLY_USE_MI, POLY_FULLY_CLIPPED or POLY_IS_EASY
	as well as the pointer to the "top" point and the y
	extents.
*/

int
XAAIsEasyPolygon(DDXPointPtr ptsIn, int count, BoxPtr extents, int origin, DDXPointPtr * topPoint,      /* return */
                 int *topY, int *bottomY,       /* return */
                 int shape)
{
    int c = 0, vertex1, vertex2;

    *topY = 32767;
    *bottomY = 0;

    origin -= (origin & 0x8000) << 1;
    vertex1 = extents->x1 - origin;
    vertex2 = extents->x2 - origin /* - 0x00010001 */ ;
    /* I think this was an error in cfb ^ */

    if (shape == Convex) {
        while (count--) {
            c = *((int *) ptsIn);
            if (((c - vertex1) | (vertex2 - c)) & 0x80008000)
                return POLY_USE_MI;

            c = intToY(c);
            if (c < *topY) {
                *topY = c;
                *topPoint = ptsIn;
            }
            ptsIn++;
            if (c > *bottomY)
                *bottomY = c;
        }
    }
    else {
        int yFlip = 0;
        int dx2, dx1, x1, x2;

        x2 = x1 = -1;
        dx2 = dx1 = 1;

        while (count--) {
            c = *((int *) ptsIn);
            if (((c - vertex1) | (vertex2 - c)) & 0x80008000)
                return POLY_USE_MI;
            c = intToY(c);
            if (c < *topY) {
                *topY = c;
                *topPoint = ptsIn;
            }
            ptsIn++;
            if (c > *bottomY)
                *bottomY = c;
            if (c == x1)
                continue;
            if (dx1 > 0) {
                if (x2 < 0)
                    x2 = c;
                else
                    dx2 = dx1 = (c - x1) >> 31;
            }
            else if ((c - x1) >> 31 != dx1) {
                dx1 = ~dx1;
                yFlip++;
            }
            x1 = c;
        }
        x1 = (x2 - c) >> 31;
        if (x1 != dx1)
            yFlip++;
        if (x1 != dx2)
            yFlip++;
        if (yFlip != 2) {
            if (*topY == *bottomY)
                return POLY_FULLY_CLIPPED;
            else
                return POLY_USE_MI;
        }
    }
    if (*topY == *bottomY)
        return POLY_FULLY_CLIPPED;

    return POLY_IS_EASY;
}

void
XAAFillPolygonSolid(DrawablePtr pDraw,
                    GCPtr pGC,
                    int shape, int mode, int count, DDXPointPtr ptsIn)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int origin, vertex1, vertex2;
    int *vertex1p, *vertex2p, *endp;
    int x1 = 0, x2 = 0;
    int dx1 = 0, dx2 = 0, dy1 = 0, dy2 = 0;
    int DX1 = 0, DX2 = 0, e1 = 0, e2 = 0;
    int step1 = 0, step2 = 0, sign1 = 0, sign2 = 0;
    int c, y, maxy, h, yoffset;
    DDXPointPtr topPoint;

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    if (mode == CoordModePrevious) {
        register DDXPointPtr ppt = ptsIn + 1;

        for (origin = 1; origin < count; origin++, ppt++) {
            ppt->x += (ppt - 1)->x;
            ppt->y += (ppt - 1)->y;
        }
        mode = CoordModeOrigin;
    }

    if (RegionNumRects(pGC->pCompositeClip) != 1) {
        miFillPolygon(pDraw, pGC, shape, mode, count, ptsIn);
        return;
    }

    origin = coordToInt(pDraw->x, pDraw->y);

    switch (XAAIsEasyPolygon(ptsIn, count, &pGC->pCompositeClip->extents,
                             origin, &topPoint, &y, &maxy, shape)) {
    case POLY_USE_MI:
        miFillPolygon(pDraw, pGC, shape, mode, count, ptsIn);
    case POLY_FULLY_CLIPPED:
        return;
    }

    endp = (int *) ptsIn + count;
    vertex2p = vertex1p = (int *) topPoint;
    origin = pDraw->x;
    yoffset = pDraw->y;
    vertex2 = vertex1 = *vertex2p++;
    if (vertex2p == endp)
        vertex2p = (int *) ptsIn;

    (*infoRec->SetupForSolidFill) (infoRec->pScrn, pGC->fgPixel, pGC->alu,
                                   pGC->planemask);

    while (1) {
        if (y == intToY(vertex1)) {
            do {
                if (vertex1p == (int *) ptsIn)
                    vertex1p = endp;
                c = *--vertex1p;
                Setup(c, x1, vertex1, dx1, dy1, e1, sign1, step1, DX1)
            } while (y >= intToY(vertex1));
            h = dy1;
        }
        else {
            Step(x1, dx1, dy1, e1, sign1, step1)
                h = intToY(vertex1) - y;
        }
        if (y == intToY(vertex2)) {
            do {
                c = *vertex2p++;
                if (vertex2p == endp)
                    vertex2p = (int *) ptsIn;
                Setup(c, x2, vertex2, dx2, dy2, e2, sign2, step2, DX2)
            } while (y >= intToY(vertex2));
            if (dy2 < h)
                h = dy2;
        }
        else {
            Step(x2, dx2, dy2, e2, sign2, step2)
                if ((c = (intToY(vertex2) - y)) < h)
                h = c;
        }

        /* fill spans for this segment */
        if (DX1 | DX2) {
            if (infoRec->SubsequentSolidFillTrap && (h > 6)) {
                if (x1 == x2) {
                    while (x1 == x2) {
                        y++;
                        if (!--h)
                            break;
                        Step(x1, dx1, dy1, e1, sign1, step1)
                            Step(x2, dx2, dy2, e2, sign2, step2)
                    }
                    if (y == maxy)
                        break;
                    if (!h)
                        continue;
                }

                if (x1 < x2)
                    (*infoRec->SubsequentSolidFillTrap) (infoRec->pScrn,
                                                         y + yoffset, h,
                                                         x1, DX1, dy1, e1,
                                                         x2 - 1, DX2, dy2, e2);
                else
                    (*infoRec->SubsequentSolidFillTrap) (infoRec->pScrn,
                                                         y + yoffset, h,
                                                         x2, DX2, dy2, e2,
                                                         x1 - 1, DX1, dy1, e1);
                y += h;
                if (--h) {
                    FixError(x1, dx1, dy1, e1, sign1, step1, h);
                    FixError(x2, dx2, dy2, e2, sign2, step2, h);
                    h = 0;
                }
            }
            else {
                while (1) {
                    if (x2 > x1)
                        (*infoRec->SubsequentSolidFillRect) (infoRec->pScrn,
                                                             x1, y + yoffset,
                                                             x2 - x1, 1);
                    else if (x1 > x2)
                        (*infoRec->SubsequentSolidFillRect) (infoRec->pScrn,
                                                             x2, y + yoffset,
                                                             x1 - x2, 1);
                    y++;
                    if (!--h)
                        break;
                    Step(x1, dx1, dy1, e1, sign1, step1)
                        Step(x2, dx2, dy2, e2, sign2, step2)
                }
            }
        }
        else {
            if (x2 > x1)
                (*infoRec->SubsequentSolidFillRect) (infoRec->pScrn,
                                                     x1, y + yoffset, x2 - x1,
                                                     h);
            else if (x1 > x2)
                (*infoRec->SubsequentSolidFillRect) (infoRec->pScrn,
                                                     x2, y + yoffset, x1 - x2,
                                                     h);

            y += h;
            h = 0;
        }
        if (y == maxy)
            break;
    }
    SET_SYNC_FLAG(infoRec);
}

void
XAAFillPolygonHelper(ScrnInfoPtr pScrn,
                     DDXPointPtr ptsIn,
                     int count,
                     DDXPointPtr topPoint,
                     int y,
                     int maxy,
                     int origin,
                     RectFuncPtr RectFunc,
                     TrapFuncPtr TrapFunc,
                     int xorg, int yorg, XAACacheInfoPtr pCache)
{
    int *vertex1p, *vertex2p, *endp;
    int vertex1, vertex2;
    int x1 = 0, x2 = 0;
    int dx1 = 0, dx2 = 0, dy1 = 0, dy2 = 0;
    int DX1 = 0, DX2 = 0, e1 = 0, e2 = 0;
    int step1 = 0, step2 = 0, sign1 = 0, sign2 = 0;
    int c, h, yoffset;

    endp = (int *) ptsIn + count;
    vertex2p = vertex1p = (int *) topPoint;
    yoffset = intToY(origin);
    origin = intToX(origin);
    vertex2 = vertex1 = *vertex2p++;
    if (vertex2p == endp)
        vertex2p = (int *) ptsIn;

    while (1) {
        if (y == intToY(vertex1)) {
            do {
                if (vertex1p == (int *) ptsIn)
                    vertex1p = endp;
                c = *--vertex1p;
                Setup(c, x1, vertex1, dx1, dy1, e1, sign1, step1, DX1)
            } while (y >= intToY(vertex1));
            h = dy1;
        }
        else {
            Step(x1, dx1, dy1, e1, sign1, step1)
                h = intToY(vertex1) - y;
        }
        if (y == intToY(vertex2)) {
            do {
                c = *vertex2p++;
                if (vertex2p == endp)
                    vertex2p = (int *) ptsIn;
                Setup(c, x2, vertex2, dx2, dy2, e2, sign2, step2, DX2)
            } while (y >= intToY(vertex2));
            if (dy2 < h)
                h = dy2;
        }
        else {
            Step(x2, dx2, dy2, e2, sign2, step2)
                if ((c = (intToY(vertex2) - y)) < h)
                h = c;
        }

        /* fill spans for this segment */
        if (DX1 | DX2) {
            if (TrapFunc && (h > 6)) {
                if (x1 == x2) {
                    while (x1 == x2) {
                        y++;
                        if (!--h)
                            break;
                        Step(x1, dx1, dy1, e1, sign1, step1)
                            Step(x2, dx2, dy2, e2, sign2, step2)
                    }
                    if (y == maxy)
                        break;
                    if (!h)
                        continue;
                }

                if (x1 < x2)
                    (*TrapFunc) (pScrn, y + yoffset, h,
                                 x1, DX1, dy1, e1,
                                 x2 - 1, DX2, dy2, e2, xorg, yorg, pCache);
                else
                    (*TrapFunc) (pScrn, y + yoffset, h,
                                 x2, DX2, dy2, e2,
                                 x1 - 1, DX1, dy1, e1, xorg, yorg, pCache);
                y += h;
                if (--h) {
                    FixError(x1, dx1, dy1, e1, sign1, step1, h);
                    FixError(x2, dx2, dy2, e2, sign2, step2, h);
                    h = 0;
                }
            }
            else {
                while (1) {
                    if (x2 > x1)
                        (*RectFunc) (pScrn,
                                     x1, y + yoffset, x2 - x1, 1, xorg, yorg,
                                     pCache);
                    else if (x1 > x2)
                        (*RectFunc) (pScrn,
                                     x2, y + yoffset, x1 - x2, 1, xorg, yorg,
                                     pCache);
                    y++;
                    if (!--h)
                        break;
                    Step(x1, dx1, dy1, e1, sign1, step1)
                        Step(x2, dx2, dy2, e2, sign2, step2)
                }
            }
        }
        else {
            if (x2 > x1)
                (*RectFunc) (pScrn,
                             x1, y + yoffset, x2 - x1, h, xorg, yorg, pCache);
            else if (x1 > x2)
                (*RectFunc) (pScrn,
                             x2, y + yoffset, x1 - x2, h, xorg, yorg, pCache);

            y += h;
            h = 0;
        }
        if (y == maxy)
            break;
    }
}

        /*****************\
	|  Solid Helpers  |
	\*****************/

static void
SolidTrapHelper(ScrnInfoPtr pScrn,
                int y, int h,
                int x1, int dx1, int dy1, int e1,
                int x2, int dx2, int dy2, int e2,
                int xorg, int yorg, XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);

    (*infoRec->SubsequentSolidFillTrap) (pScrn,
                                         y, h, x1, dx1, dy1, e1, x2, dx2, dy2,
                                         e2);
}

static void
SolidRectHelper(ScrnInfoPtr pScrn,
                int x, int y, int w, int h,
                int xorg, int yorg, XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);

    (*infoRec->SubsequentSolidFillRect) (pScrn, x, y, w, h);
}

        /*********************\
	|  Mono 8x8 Patterns  |
	\*********************/

static void
Mono8x8PatternTrapHelper_ScreenOrigin(ScrnInfoPtr pScrn,
                                      int y, int h,
                                      int x1, int dx1, int dy1, int e1,
                                      int x2, int dx2, int dy2, int e2,
                                      int xorg, int yorg,
                                      XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);

    (*infoRec->SubsequentMono8x8PatternFillTrap) (pScrn, xorg, yorg,
                                                  y, h, x1, dx1, dy1, e1, x2,
                                                  dx2, dy2, e2);
}

static void
Mono8x8PatternRectHelper_ScreenOrigin(ScrnInfoPtr pScrn,
                                      int x, int y, int w, int h,
                                      int xorg, int yorg,
                                      XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);

    (*infoRec->SubsequentMono8x8PatternFillRect) (pScrn, xorg, yorg,
                                                  x, y, w, h);
}

static void
Mono8x8PatternRectHelper(ScrnInfoPtr pScrn,
                         int x, int y, int w, int h,
                         int xorg, int yorg, XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);

    xorg = (x - xorg) & 0x07;
    yorg = (y - yorg) & 0x07;

    if (!(infoRec->Mono8x8PatternFillFlags &
          HARDWARE_PATTERN_PROGRAMMED_ORIGIN)) {
        if (infoRec->Mono8x8PatternFillFlags & HARDWARE_PATTERN_PROGRAMMED_BITS) {
            int patx = pCache->pat0;
            int paty = pCache->pat1;

            XAARotateMonoPattern(&patx, &paty, xorg, yorg,
                                 (infoRec->Mono8x8PatternFillFlags &
                                  BIT_ORDER_IN_BYTE_MSBFIRST));
            xorg = patx;
            yorg = paty;
        }
        else {
            int slot = (yorg << 3) + xorg;

            xorg = pCache->x + pCache->offsets[slot].x;
            yorg = pCache->y + pCache->offsets[slot].y;
        }
    }

    (*infoRec->SubsequentMono8x8PatternFillRect) (pScrn, xorg, yorg,
                                                  x, y, w, h);
}

        /****************\
	|  Cache Expand  |
	\****************/

static void
CacheExpandRectHelper(ScrnInfoPtr pScrn,
                      int X, int Y, int Width, int Height,
                      int xorg, int yorg, XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int x, phaseY, phaseX, skipleft, w, blit_w, blit_h;
    int cacheWidth;

    cacheWidth = (pCache->w * pScrn->bitsPerPixel) /
        infoRec->CacheColorExpandDensity;

    phaseY = (Y - yorg) % pCache->orig_h;
    if (phaseY < 0)
        phaseY += pCache->orig_h;
    phaseX = (X - xorg) % pCache->orig_w;
    if (phaseX < 0)
        phaseX += pCache->orig_w;

    while (1) {
        w = Width;
        skipleft = phaseX;
        x = X;
        blit_h = pCache->h - phaseY;
        if (blit_h > Height)
            blit_h = Height;

        while (1) {
            blit_w = cacheWidth - skipleft;
            if (blit_w > w)
                blit_w = w;
            (*infoRec->SubsequentScreenToScreenColorExpandFill) (pScrn, x, Y,
                                                                 blit_w, blit_h,
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
        Height -= blit_h;
        if (!Height)
            break;
        Y += blit_h;
        phaseY = (phaseY + blit_h) % pCache->orig_h;
    }
}

        /**************\
	|  Cache Blit  |
	\**************/

static void
CacheBltRectHelper(ScrnInfoPtr pScrn,
                   int X, int Y, int Width, int Height,
                   int xorg, int yorg, XAACacheInfoPtr pCache)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    int x, phaseY, phaseX, skipleft, w, blit_w, blit_h;

    phaseY = (Y - yorg) % pCache->orig_h;
    if (phaseY < 0)
        phaseY += pCache->orig_h;
    phaseX = (X - xorg) % pCache->orig_w;
    if (phaseX < 0)
        phaseX += pCache->orig_w;

    while (1) {
        w = Width;
        skipleft = phaseX;
        x = X;
        blit_h = pCache->h - phaseY;
        if (blit_h > Height)
            blit_h = Height;

        while (1) {
            blit_w = pCache->w - skipleft;
            if (blit_w > w)
                blit_w = w;
            (*infoRec->SubsequentScreenToScreenCopy) (pScrn,
                                                      pCache->x + skipleft,
                                                      pCache->y + phaseY, x, Y,
                                                      blit_w, blit_h);
            w -= blit_w;
            if (!w)
                break;
            x += blit_w;
            skipleft = (skipleft + blit_w) % pCache->orig_w;
        }
        Height -= blit_h;
        if (!Height)
            break;
        Y += blit_h;
        phaseY = (phaseY + blit_h) % pCache->orig_h;
    }
}

        /**********************\
	|   Stippled Polygons  |
	\**********************/

void
XAAFillPolygonStippled(DrawablePtr pDraw,
                       GCPtr pGC,
                       int shape, int mode, int count, DDXPointPtr ptsIn)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pGC->stipple);
    int origin, type, patx, paty, fg, bg;
    int y, maxy, xorg, yorg;
    DDXPointPtr topPoint;
    XAACacheInfoPtr pCache = NULL;
    RectFuncPtr RectFunc = NULL;
    TrapFuncPtr TrapFunc = NULL;

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    if (mode == CoordModePrevious) {
        register DDXPointPtr ppt = ptsIn + 1;

        for (origin = 1; origin < count; origin++, ppt++) {
            ppt->x += (ppt - 1)->x;
            ppt->y += (ppt - 1)->y;
        }
        mode = CoordModeOrigin;
    }

    if (RegionNumRects(pGC->pCompositeClip) != 1) {
        miFillPolygon(pDraw, pGC, shape, mode, count, ptsIn);
        return;
    }

    if (pGC->fillStyle == FillStippled) {
        type = (*infoRec->StippledFillChooser) (pGC);
        fg = pGC->fgPixel;
        bg = -1;
    }
    else {
        type = (*infoRec->OpaqueStippledFillChooser) (pGC);
        fg = pGC->fgPixel;
        bg = pGC->bgPixel;
    }

    if (!type) {
        (*XAAFallbackOps.FillPolygon) (pDraw, pGC, shape, mode, count, ptsIn);
        return;
    }

    if ((type == DO_COLOR_EXPAND) || (type == DO_COLOR_8x8)) {
        miFillPolygon(pDraw, pGC, shape, mode, count, ptsIn);
        return;
    }

    origin = pDraw->x;

    switch (XAAIsEasyPolygon(ptsIn, count, &pGC->pCompositeClip->extents,
                             origin, &topPoint, &y, &maxy, shape)) {
    case POLY_USE_MI:
        miFillPolygon(pDraw, pGC, shape, mode, count, ptsIn);
    case POLY_FULLY_CLIPPED:
        return;
    }

    xorg = (pDraw->x + pGC->patOrg.x);
    yorg = (pDraw->y + pGC->patOrg.y);

    if ((fg == bg) && (bg != -1) && infoRec->SetupForSolidFill) {

        (*infoRec->SetupForSolidFill) (infoRec->pScrn, fg,
                                       pGC->alu, pGC->planemask);

        RectFunc = SolidRectHelper;
        TrapFunc = infoRec->SubsequentSolidFillTrap ? SolidTrapHelper : NULL;
    }
    else
        switch (type) {
        case DO_MONO_8x8:
            patx = pPriv->pattern0;
            paty = pPriv->pattern1;
            if (infoRec->Mono8x8PatternFillFlags &
                HARDWARE_PATTERN_SCREEN_ORIGIN) {
                xorg = (-xorg) & 0x07;
                yorg = (-yorg) & 0x07;
                if (infoRec->Mono8x8PatternFillFlags &
                    HARDWARE_PATTERN_PROGRAMMED_BITS) {
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
                        (*infoRec->CacheMono8x8Pattern) (infoRec->pScrn, patx,
                                                         paty);
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
                RectFunc = Mono8x8PatternRectHelper_ScreenOrigin;
                if (infoRec->SubsequentMono8x8PatternFillTrap)
                    TrapFunc = Mono8x8PatternTrapHelper_ScreenOrigin;
            }
            else {              /* !HARDWARE_PATTERN_SCREEN_ORIGIN */
                if (!(infoRec->Mono8x8PatternFillFlags &
                      HARDWARE_PATTERN_PROGRAMMED_BITS)) {
                    pCache =
                        (*infoRec->CacheMono8x8Pattern) (infoRec->pScrn, patx,
                                                         paty);
                    patx = pCache->x;
                    paty = pCache->y;
                }
                else {
                    pCache = &(infoRec->ScratchCacheInfoRec);
                    pCache->pat0 = patx;
                    pCache->pat1 = paty;
                }
                RectFunc = Mono8x8PatternRectHelper;
            }

            (*infoRec->SetupForMono8x8PatternFill) (infoRec->pScrn,
                                                    patx, paty, fg, bg,
                                                    pGC->alu, pGC->planemask);
            break;
        case DO_CACHE_EXPAND:
            pCache =
                (*infoRec->CacheMonoStipple) (infoRec->pScrn, pGC->stipple);

            (*infoRec->SetupForScreenToScreenColorExpandFill) (infoRec->pScrn,
                                                               fg, bg, pGC->alu,
                                                               pGC->planemask);

            RectFunc = CacheExpandRectHelper;
            break;
        case DO_CACHE_BLT:
            pCache = (*infoRec->CacheStipple) (infoRec->pScrn, pGC->stipple,
                                               fg, bg);
            (*infoRec->SetupForScreenToScreenCopy) (infoRec->pScrn, 1, 1,
                                                    pGC->alu, pGC->planemask,
                                                    pCache->trans_color);

            RectFunc = CacheBltRectHelper;
            break;
        default:
            return;
        }

    XAAFillPolygonHelper(infoRec->pScrn, ptsIn, count, topPoint,
                         y, maxy, origin, RectFunc, TrapFunc, xorg, yorg,
                         pCache);

    SET_SYNC_FLAG(infoRec);
}

        /*******************\
	|   Tiled Polygons  |
	\*******************/

void
XAAFillPolygonTiled(DrawablePtr pDraw,
                    GCPtr pGC,
                    int shape, int mode, int count, DDXPointPtr ptsIn)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    XAAPixmapPtr pPriv = XAA_GET_PIXMAP_PRIVATE(pGC->tile.pixmap);
    int origin, type, patx, paty;
    int y, maxy, xorg, yorg;
    DDXPointPtr topPoint;
    XAACacheInfoPtr pCache = NULL;
    RectFuncPtr RectFunc = NULL;
    TrapFuncPtr TrapFunc = NULL;

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    if (mode == CoordModePrevious) {
        register DDXPointPtr ppt = ptsIn + 1;

        for (origin = 1; origin < count; origin++, ppt++) {
            ppt->x += (ppt - 1)->x;
            ppt->y += (ppt - 1)->y;
        }
        mode = CoordModeOrigin;
    }

    if (RegionNumRects(pGC->pCompositeClip) != 1) {
        miFillPolygon(pDraw, pGC, shape, mode, count, ptsIn);
        return;
    }

    type = (*infoRec->TiledFillChooser) (pGC);

    if (!type || (type == DO_IMAGE_WRITE)) {
        (*XAAFallbackOps.FillPolygon) (pDraw, pGC, shape, mode, count, ptsIn);
        return;
    }

    if (type == DO_COLOR_8x8) {
        miFillPolygon(pDraw, pGC, shape, mode, count, ptsIn);
        return;
    }

    origin = pDraw->x;

    switch (XAAIsEasyPolygon(ptsIn, count, &pGC->pCompositeClip->extents,
                             origin, &topPoint, &y, &maxy, shape)) {
    case POLY_USE_MI:
        miFillPolygon(pDraw, pGC, shape, mode, count, ptsIn);
    case POLY_FULLY_CLIPPED:
        return;
    }

    xorg = (pDraw->x + pGC->patOrg.x);
    yorg = (pDraw->y + pGC->patOrg.y);

    switch (type) {
    case DO_MONO_8x8:
        patx = pPriv->pattern0;
        paty = pPriv->pattern1;
        if (infoRec->Mono8x8PatternFillFlags & HARDWARE_PATTERN_SCREEN_ORIGIN) {
            xorg = (-xorg) & 0x07;
            yorg = (-yorg) & 0x07;
            if (infoRec->Mono8x8PatternFillFlags &
                HARDWARE_PATTERN_PROGRAMMED_BITS) {
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
                    (*infoRec->CacheMono8x8Pattern) (infoRec->pScrn, patx,
                                                     paty);
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
            RectFunc = Mono8x8PatternRectHelper_ScreenOrigin;
            if (infoRec->SubsequentMono8x8PatternFillTrap)
                TrapFunc = Mono8x8PatternTrapHelper_ScreenOrigin;
        }
        else {                  /* !HARDWARE_PATTERN_SCREEN_ORIGIN */
            if (!(infoRec->Mono8x8PatternFillFlags &
                  HARDWARE_PATTERN_PROGRAMMED_BITS)) {
                pCache =
                    (*infoRec->CacheMono8x8Pattern) (infoRec->pScrn, patx,
                                                     paty);
                patx = pCache->x;
                paty = pCache->y;
            }
            else {
                pCache = &(infoRec->ScratchCacheInfoRec);
                pCache->pat0 = patx;
                pCache->pat1 = paty;
            }
            RectFunc = Mono8x8PatternRectHelper;
        }

        (*infoRec->SetupForMono8x8PatternFill) (infoRec->pScrn,
                                                patx, paty, pPriv->fg,
                                                pPriv->bg, pGC->alu,
                                                pGC->planemask);
        break;
    case DO_CACHE_BLT:
        pCache = (*infoRec->CacheTile) (infoRec->pScrn, pGC->tile.pixmap);
        (*infoRec->SetupForScreenToScreenCopy) (infoRec->pScrn, 1, 1,
                                                pGC->alu, pGC->planemask, -1);

        RectFunc = CacheBltRectHelper;
        break;
    case DO_PIXMAP_COPY:
        pCache = &(infoRec->ScratchCacheInfoRec);
        pCache->x = pPriv->offscreenArea->box.x1;
        pCache->y = pPriv->offscreenArea->box.y1;
        pCache->w = pCache->orig_w = pPriv->offscreenArea->box.x2 - pCache->x;
        pCache->h = pCache->orig_h = pPriv->offscreenArea->box.y2 - pCache->y;

        (*infoRec->SetupForScreenToScreenCopy) (infoRec->pScrn, 1, 1,
                                                pGC->alu, pGC->planemask, -1);

        RectFunc = CacheBltRectHelper;
        break;
    default:
        return;
    }

    XAAFillPolygonHelper(infoRec->pScrn, ptsIn, count, topPoint,
                         y, maxy, origin, RectFunc, TrapFunc, xorg, yorg,
                         pCache);

    SET_SYNC_FLAG(infoRec);
}
