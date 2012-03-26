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
 * Written by Harm Hanemaayer (H.Hanemaayer@inter.nl.net).
 */

/*
 * Filled solid arcs, based on cfbfillarc.c.
 *
 * Fill arc using calls to low-level span fill. Because the math for
 * each span can be done concurrently with the drawing of the span
 * with a graphics coprocessor operation, this is faster than just
 * using miPolyFillArc, which first calculates all the spans and then
 * calls FillSpans.
 *
 * Clipped arcs are dispatched to FillSpans.
 */
#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <limits.h>

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "scrnintstr.h"
#include "pixmapstr.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"
#include "mifillarc.h"
#include "mi.h"

/*
 * This is based on the integer-math versions from mi. Perhaps on a
 * Pentium, the floating-point (double)-math version is faster.
 */

static void
XAAFillEllipseSolid(DrawablePtr pDraw, GCPtr pGC, xArc * arc)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    register int x, y, e;
    int yk, xk, ym, xm, dx, dy, xorg, yorg;
    int slw;
    miFillArcRec info;

    (*infoRec->SetupForSolidFill) (infoRec->pScrn, pGC->fgPixel, pGC->alu,
                                   pGC->planemask);

    miFillArcSetup(arc, &info);
    MIFILLARCSETUP();
    if (pGC->miTranslate) {
        xorg += pDraw->x;
        yorg += pDraw->y;
    }
    while (y > 0) {
        MIFILLARCSTEP(slw);
        if (slw > 0) {
            (*infoRec->SubsequentSolidFillRect) (infoRec->pScrn, xorg - x,
                                                 yorg - y, slw, 1);
            if (miFillArcLower(slw))
                (*infoRec->SubsequentSolidFillRect) (infoRec->pScrn,
                                                     xorg - x, yorg + y + dy,
                                                     slw, 1);
        }
    }

    SET_SYNC_FLAG(infoRec);
}

#define ADDSPAN(l,r) \
    if (r >= l) \
	(*infoRec->SubsequentSolidFillRect)( \
	    infoRec->pScrn, l, ya, r - l + 1, 1);

#define ADDSLICESPANS(flip) \
    if (!flip) \
    { \
	ADDSPAN(xl, xr); \
    } \
    else \
    { \
	xc = xorg - x; \
	ADDSPAN(xc, xr); \
	xc += slw - 1; \
	ADDSPAN(xl, xc); \
    }

static void
XAAFillArcSliceSolid(DrawablePtr pDraw, GCPtr pGC, xArc * arc)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int yk, xk, ym, xm, dx, dy, xorg, yorg, slw;
    register int x, y, e;
    miFillArcRec info;
    miArcSliceRec slice;
    int ya, xl, xr, xc;

    (*infoRec->SetupForSolidFill) (infoRec->pScrn, pGC->fgPixel, pGC->alu,
                                   pGC->planemask);

    miFillArcSetup(arc, &info);
    miFillArcSliceSetup(arc, &slice, pGC);
    MIFILLARCSETUP();
    slw = arc->height;
    if (slice.flip_top || slice.flip_bot)
        slw += (arc->height >> 1) + 1;
    if (pGC->miTranslate) {
        xorg += pDraw->x;
        yorg += pDraw->y;
        slice.edge1.x += pDraw->x;
        slice.edge2.x += pDraw->x;
    }
    while (y > 0) {
        MIFILLARCSTEP(slw);
        MIARCSLICESTEP(slice.edge1);
        MIARCSLICESTEP(slice.edge2);
        if (miFillSliceUpper(slice)) {
            ya = yorg - y;
            MIARCSLICEUPPER(xl, xr, slice, slw);

            ADDSLICESPANS(slice.flip_top);
        }
        if (miFillSliceLower(slice)) {
            ya = yorg + y + dy;
            MIARCSLICELOWER(xl, xr, slice, slw);
            ADDSLICESPANS(slice.flip_bot);
        }
    }

    SET_SYNC_FLAG(infoRec);
}

void
XAAPolyFillArcSolid(DrawablePtr pDraw, GCPtr pGC, int narcs, xArc * parcs)
{
    register xArc *arc;
    register int i;
    int x2, y2;
    BoxRec box;
    RegionPtr cclip;

    cclip = pGC->pCompositeClip;

    if (!RegionNumRects(cclip))
        return;

    for (arc = parcs, i = narcs; --i >= 0; arc++) {
        if (miFillArcEmpty(arc))
            continue;
        if (miCanFillArc(arc)) {
            box.x1 = arc->x + pDraw->x;
            box.y1 = arc->y + pDraw->y;
            /*
             * Because box.x2 and box.y2 get truncated to 16 bits, and the
             * RECT_IN_REGION test treats the resulting number as a signed
             * integer, the RECT_IN_REGION test alone can go the wrong way.
             * This can result in a server crash because the rendering
             * routines in this file deal directly with cpu addresses
             * of pixels to be stored, and do not clip or otherwise check
             * that all such addresses are within their respective pixmaps.
             * So we only allow the RECT_IN_REGION test to be used for
             * values that can be expressed correctly in a signed short.
             */
            x2 = box.x1 + (int) arc->width + 1;
            box.x2 = x2;
            y2 = box.y1 + (int) arc->height + 1;
            box.y2 = y2;
            if ((x2 <= SHRT_MAX) && (y2 <= SHRT_MAX) &&
                (RegionContainsRect(cclip, &box) == rgnIN)) {
                if ((arc->angle2 >= FULLCIRCLE) || (arc->angle2 <= -FULLCIRCLE))
                    XAAFillEllipseSolid(pDraw, pGC, arc);
                else
                    XAAFillArcSliceSolid(pDraw, pGC, arc);
                continue;
            }
        }
        miPolyFillArc(pDraw, pGC, 1, arc);
    }
}
