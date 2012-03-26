/*

XAAPolylinesWideSolid does not maintain a span list and subsequently does
not follow the "touch-each-pixel-once" rules for wide lines and arcs.
This means it can only be used in the case where we have
miSpansEasyRop(pGC->alu).  Since we clip spans on the fly, we
limited usage of this function to one rect situations. This
function is used only for solid lines. 

  Adapted from miWideLine by Mark Vojkovich (mvojkovi@ucsd.edu)
Original mi code written by Keith Packard.

*/

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include <math.h>

#include "misc.h"
#include "xf86.h"
#include "xf86_OSproc.h"

#include <X11/X.h>
#include "windowstr.h"
#include "gcstruct.h"
#include "regionstr.h"
#include "miwideline.h"
#include "mi.h"
#include "xf86str.h"
#include "xaa.h"
#include "xaalocal.h"

#define DRAW_POINT(pScrn, x, y) \
  if(hardClip) (*infoRec->SubsequentSolidFillRect)(pScrn, x, y, 1, 1); \
  else XAAPointHelper(pScrn, x, y)

#define FILL_RECT(pScrn, x, y, w, h) \
  if(hardClip) (*infoRec->SubsequentSolidFillRect)(pScrn, x, y, w, h); \
  else XAAFillRectHelper(pScrn, x, y, w, h)

#define FILL_SPAN(pScrn, x, y, w) \
  if(hardClip) (*infoRec->SubsequentSolidFillRect)(pScrn, x, y, w, 1); \
  else XAASpanHelper(pScrn, x, y, w)

#define CLIPSTEPEDGE(edgey,edge,edgeleft) \
    if (ybase == edgey) { \
	if (edgeleft) { \
	    if (edge->x > xcl) \
		xcl = edge->x; \
	} else { \
	    if (edge->x < xcr) \
		xcr = edge->x; \
	} \
	edgey++; \
	edge->x += edge->stepx; \
	edge->e += edge->dx; \
	if (edge->e > 0) { \
	    edge->x += edge->signdx; \
	    edge->e -= edge->dy; \
	} \
    }

static void
XAAPointHelper(ScrnInfoPtr pScrn, int x, int y)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    BoxPtr extents = infoRec->ClipBox;

    if ((x >= extents->x1) && (x < extents->x2) &&
        (y >= extents->y1) && (y < extents->y2))
        (*infoRec->SubsequentSolidFillRect) (pScrn, x, y, 1, 1);
}

static void
XAAFillRectHelper(ScrnInfoPtr pScrn, int x1, int y1, int dx, int dy)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    BoxPtr extents = infoRec->ClipBox;
    int x2 = x1 + dx;
    int y2 = y1 + dy;

    if (x1 < extents->x1)
        x1 = extents->x1;
    if (x2 >= extents->x2)
        x2 = extents->x2;
    if ((dx = x2 - x1) < 1)
        return;
    if (y1 < extents->y1)
        y1 = extents->y1;
    if (y2 >= extents->y2)
        y2 = extents->y2;
    if ((dy = y2 - y1) < 1)
        return;

    (*infoRec->SubsequentSolidFillRect) (pScrn, x1, y1, dx, dy);
}

static void
XAASpanHelper(ScrnInfoPtr pScrn, int x1, int y, int width)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_SCRNINFOPTR(pScrn);
    BoxPtr extents = infoRec->ClipBox;
    int x2;

    if ((y < extents->y1) || (y >= extents->y2))
        return;

    x2 = x1 + width;
    if (x1 < extents->x1)
        x1 = extents->x1;
    if (x2 > extents->x2)
        x2 = extents->x2;
    width = x2 - x1;

    if (width > 0)
        (*infoRec->SubsequentSolidFillRect) (pScrn, x1, y, width, 1);

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

static void
XAAFillPolyHelper(GCPtr pGC, int y,     /* start y coordinate */
                  int overall_height,   /* height of entire segment */
                  PolyEdgePtr left, PolyEdgePtr right,
                  int left_count, int right_count)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    BoxPtr extents = infoRec->ClipBox;
    int left_x, left_e, left_stepx, left_signdx, left_dy, left_dx;
    int right_x, right_e, right_stepx, right_signdx, right_dy, right_dx;
    int height, left_height, right_height;
    int xorg;
    Bool hardClip;

    if ((y >= extents->y2) || ((y + overall_height) <= extents->y1))
        return;

    /* Muffle compiler */
    left_x = left_e = left_stepx = left_signdx = left_dy = left_dx = 0;
    right_x = right_e = right_stepx = right_signdx = right_dy = right_dx = 0;

    left_height = right_height = 0;
    xorg = 0;

    hardClip = (infoRec->ClippingFlags & HARDWARE_CLIP_SOLID_FILL);

    while ((left_count || left_height) && (right_count || right_height)) {
        if (!left_height && left_count) {
            left_height = left->height;
            left_x = left->x + xorg;
            left_stepx = left->stepx;
            left_signdx = left->signdx;
            left_e = left->e;
            left_dy = left->dy;
            left_dx = left->dx;
            left_count--;
            left++;
        }
        if (!right_height && right_count) {
            right_height = right->height;
            right_x = right->x + xorg + 1;
            right_stepx = right->stepx;
            right_signdx = right->signdx;
            right_e = right->e;
            right_dy = right->dy;
            right_dx = right->dx;
            right_count--;
            right++;
        }

        height = (left_height > right_height) ? right_height : left_height;

        left_height -= height;
        right_height -= height;

        if (hardClip && infoRec->SubsequentSolidFillTrap && (height > 6)) {
            int right_DX, left_DX;

            right_DX = (right_dx * right_signdx) + (right_stepx * right_dy);
            left_DX = (left_dx * left_signdx) + (left_stepx * left_dy);

            (*infoRec->SubsequentSolidFillTrap) (infoRec->pScrn, y, height,
                                                 left_x, left_DX, left_dy,
                                                 left_e, right_x - 1, right_DX,
                                                 right_dy, right_e);

            FixError(left_x, left_dx, left_dy, left_e, left_signdx,
                     left_stepx, height);
            FixError(right_x, right_dx, right_dy, right_e, right_signdx,
                     right_stepx, height);
            y += height;
            continue;
        }

        while (height--) {
            if (right_x > left_x) {
                FILL_SPAN(infoRec->pScrn, left_x, y, right_x - left_x);
            }
            y++;

            left_x += left_stepx;
            left_e += left_dx;
            if (left_e > 0) {
                left_x += left_signdx;
                left_e -= left_dy;
            }
            right_x += right_stepx;
            right_e += right_dx;
            if (right_e > 0) {
                right_x += right_signdx;
                right_e -= right_dy;
            }

        }
    }
}

static void
XAAWideSegment(GCPtr pGC,
               int x1, int y1, int x2, int y2,
               Bool projectLeft, Bool projectRight,
               LineFacePtr leftFace, LineFacePtr rightFace)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    double l, L, r;
    double xa, ya;
    double projectXoff, projectYoff;
    double k;
    double maxy;
    int x, y;
    int dx, dy;
    int finaly;
    PolyEdgePtr left, right;
    PolyEdgePtr top, bottom;
    int lefty, righty, topy, bottomy;
    int signdx;
    PolyEdgeRec lefts[2], rights[2];
    LineFacePtr tface;
    int lw = pGC->lineWidth;
    Bool hardClip = (infoRec->ClippingFlags & HARDWARE_CLIP_SOLID_FILL);

    /* draw top-to-bottom always */
    if ((y2 < y1) || ((y2 == y1) && (x2 < x1))) {
        x = x1;
        x1 = x2;
        x2 = x;

        y = y1;
        y1 = y2;
        y2 = y;

        x = projectLeft;
        projectLeft = projectRight;
        projectRight = x;

        tface = leftFace;
        leftFace = rightFace;
        rightFace = tface;
    }

    dy = y2 - y1;
    signdx = 1;
    dx = x2 - x1;
    if (dx < 0)
        signdx = -1;

    leftFace->x = x1;
    leftFace->y = y1;
    leftFace->dx = dx;
    leftFace->dy = dy;

    rightFace->x = x2;
    rightFace->y = y2;
    rightFace->dx = -dx;
    rightFace->dy = -dy;

    if (!dy) {
        rightFace->xa = 0;
        rightFace->ya = (double) lw / 2.0;
        rightFace->k = -(double) (lw * dx) / 2.0;
        leftFace->xa = 0;
        leftFace->ya = -rightFace->ya;
        leftFace->k = rightFace->k;
        x = x1;
        if (projectLeft)
            x -= (lw >> 1);
        y = y1 - (lw >> 1);
        dx = x2 - x;
        if (projectRight)
            dx += ((lw + 1) >> 1);
        dy = lw;
        FILL_RECT(infoRec->pScrn, x, y, dx, dy);
    }
    else if (!dx) {
        leftFace->xa = (double) lw / 2.0;
        leftFace->ya = 0;
        leftFace->k = (double) (lw * dy) / 2.0;
        rightFace->xa = -leftFace->xa;
        rightFace->ya = 0;
        rightFace->k = leftFace->k;
        y = y1;
        if (projectLeft)
            y -= lw >> 1;
        x = x1 - (lw >> 1);
        dy = y2 - y;
        if (projectRight)
            dy += ((lw + 1) >> 1);
        dx = lw;
        FILL_RECT(infoRec->pScrn, x, y, dx, dy);
    }
    else {
        l = ((double) lw) / 2.0;
        L = sqrt((double) (dx * dx + dy * dy));

        if (dx < 0) {
            right = &rights[1];
            left = &lefts[0];
            top = &rights[0];
            bottom = &lefts[1];
        }
        else {
            right = &rights[0];
            left = &lefts[1];
            top = &lefts[0];
            bottom = &rights[1];
        }
        r = l / L;

        /* coord of upper bound at integral y */
        ya = -r * dx;
        xa = r * dy;

        projectXoff = -ya;
        projectYoff = xa;

        /* xa * dy - ya * dx */
        k = l * L;

        leftFace->xa = xa;
        leftFace->ya = ya;
        leftFace->k = k;
        rightFace->xa = -xa;
        rightFace->ya = -ya;
        rightFace->k = k;

        if (projectLeft)
            righty = miPolyBuildEdge(xa - projectXoff, ya - projectYoff,
                                     k, dx, dy, x1, y1, 0, right);
        else
            righty = miPolyBuildEdge(xa, ya, k, dx, dy, x1, y1, 0, right);

        /* coord of lower bound at integral y */
        ya = -ya;
        xa = -xa;

        /* xa * dy - ya * dx */
        k = -k;

        if (projectLeft)
            lefty = miPolyBuildEdge(xa - projectXoff, ya - projectYoff,
                                    k, dx, dy, x1, y1, 1, left);
        else
            lefty = miPolyBuildEdge(xa, ya, k, dx, dy, x1, y1, 1, left);

        /* coord of top face at integral y */

        if (signdx > 0) {
            ya = -ya;
            xa = -xa;
        }

        if (projectLeft) {
            double xap = xa - projectXoff;
            double yap = ya - projectYoff;

            topy = miPolyBuildEdge(xap, yap, xap * dx + yap * dy,
                                   -dy, dx, x1, y1, dx > 0, top);
        }
        else
            topy = miPolyBuildEdge(xa, ya, 0.0, -dy, dx, x1, y1, dx > 0, top);

        /* coord of bottom face at integral y */

        if (projectRight) {
            double xap = xa + projectXoff;
            double yap = ya + projectYoff;

            bottomy = miPolyBuildEdge(xap, yap, xap * dx + yap * dy,
                                      -dy, dx, x2, y2, dx < 0, bottom);
            maxy = -ya + projectYoff;
        }
        else {
            bottomy = miPolyBuildEdge(xa, ya, 0.0,
                                      -dy, dx, x2, y2, dx < 0, bottom);
            maxy = -ya;
        }

        finaly = ICEIL(maxy) + y2;

        if (dx < 0) {
            left->height = bottomy - lefty;
            right->height = finaly - righty;
            top->height = righty - topy;
        }
        else {
            right->height = bottomy - righty;
            left->height = finaly - lefty;
            top->height = lefty - topy;
        }
        bottom->height = finaly - bottomy;
        XAAFillPolyHelper(pGC, topy,
                          bottom->height + bottomy - topy, lefts, rights, 2, 2);
    }
}

static void
XAALineArcI(GCPtr pGC, int xorg, int yorg)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int x, y, e, ex;
    int slw = pGC->lineWidth;
    Bool hardClip = (infoRec->ClippingFlags & HARDWARE_CLIP_SOLID_FILL);

    y = (slw >> 1) + 1;
    if (slw & 1)
        e = -((y << 2) + 3);
    else
        e = -(y << 3);
    ex = -4;
    x = 0;
    while (y) {
        e += (y << 3) - 4;
        while (e >= 0) {
            x++;
            e += (ex = -((x << 3) + 4));
        }
        y--;
        slw = (x << 1) + 1;
        if ((e == ex) && (slw > 1))
            slw--;

        FILL_SPAN(infoRec->pScrn, xorg - x, yorg - y, slw);

        if ((y != 0) && ((slw > 1) || (e != ex))) {
            FILL_SPAN(infoRec->pScrn, xorg - x, yorg + y, slw);
        }
    }
}

static void
XAALineArcD(GCPtr pGC,
            double xorg,
            double yorg,
            PolyEdgePtr edge1,
            int edgey1,
            Bool edgeleft1, PolyEdgePtr edge2, int edgey2, Bool edgeleft2)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    double radius, x0, y0, el, er, yk, xlk, xrk, k;
    int xbase, ybase, y, boty, xl, xr, xcl, xcr;
    int ymin, ymax;
    Bool edge1IsMin, edge2IsMin;
    int ymin1, ymin2;
    Bool hardClip = (infoRec->ClippingFlags & HARDWARE_CLIP_SOLID_FILL);

    xbase = floor(xorg);
    x0 = xorg - xbase;
    ybase = ICEIL(yorg);
    y0 = yorg - ybase;

    xlk = x0 + x0 + 1.0;
    xrk = x0 + x0 - 1.0;
    yk = y0 + y0 - 1.0;
    radius = ((double) pGC->lineWidth) / 2.0;
    y = floor(radius - y0 + 1.0);
    ybase -= y;
    ymin = ybase;
    ymax = 65536;
    edge1IsMin = FALSE;
    ymin1 = edgey1;
    if (edge1->dy >= 0) {
        if (!edge1->dy) {
            if (edgeleft1)
                edge1IsMin = TRUE;
            else
                ymax = edgey1;
            edgey1 = 65536;
        }
        else if ((edge1->signdx < 0) == edgeleft1)
            edge1IsMin = TRUE;
    }
    edge2IsMin = FALSE;
    ymin2 = edgey2;
    if (edge2->dy >= 0) {
        if (!edge2->dy) {
            if (edgeleft2)
                edge2IsMin = TRUE;
            else
                ymax = edgey2;
            edgey2 = 65536;
        }
        else if ((edge2->signdx < 0) == edgeleft2)
            edge2IsMin = TRUE;
    }
    if (edge1IsMin) {
        ymin = ymin1;
        if (edge2IsMin && (ymin1 > ymin2))
            ymin = ymin2;
    }
    else if (edge2IsMin)
        ymin = ymin2;
    el = radius * radius - ((y + y0) * (y + y0)) - (x0 * x0);
    er = el + xrk;
    xl = 1;
    xr = 0;
    if (x0 < 0.5) {
        xl = 0;
        el -= xlk;
    }
    boty = (y0 < -0.5) ? 1 : 0;
    if (ybase + y - boty > ymax)
        boty = ymax - ybase - y;
    while (y > boty) {
        k = (y << 1) + yk;
        er += k;
        while (er > 0.0) {
            xr++;
            er += xrk - (xr << 1);
        }
        el += k;
        while (el >= 0.0) {
            xl--;
            el += (xl << 1) - xlk;
        }
        y--;
        ybase++;
        if (ybase < ymin)
            continue;
        xcl = xl + xbase;
        xcr = xr + xbase;
        CLIPSTEPEDGE(edgey1, edge1, edgeleft1);
        CLIPSTEPEDGE(edgey2, edge2, edgeleft2);
        if (xcr >= xcl) {
            FILL_SPAN(infoRec->pScrn, xcl, ybase, xcr - xcl + 1);
        }
    }
    er = xrk - (xr << 1) - er;
    el = (xl << 1) - xlk - el;
    boty = floor(-y0 - radius + 1.0);
    if (ybase + y - boty > ymax)
        boty = ymax - ybase - y;
    while (y > boty) {
        k = (y << 1) + yk;
        er -= k;
        while ((er >= 0.0) && (xr >= 0)) {
            xr--;
            er += xrk - (xr << 1);
        }
        el -= k;
        while ((el > 0.0) && (xl <= 0)) {
            xl++;
            el += (xl << 1) - xlk;
        }
        y--;
        ybase++;
        if (ybase < ymin)
            continue;
        xcl = xl + xbase;
        xcr = xr + xbase;
        CLIPSTEPEDGE(edgey1, edge1, edgeleft1);
        CLIPSTEPEDGE(edgey2, edge2, edgeleft2);
        if (xcr >= xcl) {
            FILL_SPAN(infoRec->pScrn, xcl, ybase, xcr - xcl + 1);
        }
    }
}

static void
XAALineArc(GCPtr pGC,
           LineFacePtr leftFace,
           LineFacePtr rightFace, double xorg, double yorg, Bool isInt)
{
    int xorgi, yorgi;
    PolyEdgeRec edge1, edge2;
    int edgey1, edgey2;
    Bool edgeleft1, edgeleft2;

    if (isInt) {
        xorgi = leftFace ? leftFace->x : rightFace->x;
        yorgi = leftFace ? leftFace->y : rightFace->y;
    }
    else {                      /* Muffle compiler */
        xorgi = yorgi = 0;
    }
    edgey1 = 65536;
    edgey2 = 65536;
    edge1.x = 0;                /* not used, keep memory checkers happy */
    edge1.dy = -1;
    edge2.x = 0;                /* not used, keep memory checkers happy */
    edge2.dy = -1;
    edgeleft1 = FALSE;
    edgeleft2 = FALSE;

    if ((pGC->lineWidth > 2) &&
        ((pGC->capStyle == CapRound && pGC->joinStyle != JoinRound) ||
         (pGC->joinStyle == JoinRound && pGC->capStyle == CapButt))) {
        if (isInt) {
            xorg = (double) xorgi;
            yorg = (double) yorgi;
        }

        if (leftFace && rightFace)
            miRoundJoinClip(leftFace, rightFace, &edge1, &edge2,
                            &edgey1, &edgey2, &edgeleft1, &edgeleft2);
        else if (leftFace)
            edgey1 = miRoundCapClip(leftFace, isInt, &edge1, &edgeleft1);
        else if (rightFace)
            edgey2 = miRoundCapClip(rightFace, isInt, &edge2, &edgeleft2);

        isInt = FALSE;
    }

    if (isInt) {
        if (pGC->lineWidth == 1) {
            XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
            Bool hardClip = (infoRec->ClippingFlags & HARDWARE_CLIP_SOLID_FILL);

            DRAW_POINT(infoRec->pScrn, xorgi, yorgi);
        }
        else
            XAALineArcI(pGC, xorgi, yorgi);
    }
    else
        XAALineArcD(pGC, xorg, yorg, &edge1, edgey1, edgeleft1,
                    &edge2, edgey2, edgeleft2);

}

static void
XAALineJoin(GCPtr pGC, LineFacePtr pLeft, LineFacePtr pRight)
{
    double mx = 0, my = 0;
    double denom = 0;
    PolyVertexRec vertices[4];
    PolySlopeRec slopes[4];
    int edgecount;
    PolyEdgeRec left[4], right[4];
    int nleft, nright;
    int y, height;
    int swapslopes;
    int joinStyle = pGC->joinStyle;
    int lw = pGC->lineWidth;

    if (lw == 1) {
        /* Lines going in the same direction have no join */
        if ((pLeft->dx >= 0) == (pRight->dx <= 0))
            return;
        if (joinStyle != JoinRound) {
            denom = -pLeft->dx * (double) pRight->dy + pRight->dx *
                (double) pLeft->dy;
            if (denom == 0.0)
                return;         /* no join to draw */
        }
        if (joinStyle != JoinMiter) {
            XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
            Bool hardClip = (infoRec->ClippingFlags & HARDWARE_CLIP_SOLID_FILL);

            DRAW_POINT(infoRec->pScrn, pLeft->x, pLeft->y);
            return;
        }
    }
    else {
        if (joinStyle == JoinRound) {
            XAALineArc(pGC, pLeft, pRight, (double) 0.0, (double) 0.0, TRUE);
            return;
        }
        denom = -pLeft->dx * (double) pRight->dy + pRight->dx *
            (double) pLeft->dy;
        if (denom == 0.0)
            return;             /* no join to draw */
    }

    swapslopes = 0;
    if (denom > 0) {
        pLeft->xa = -pLeft->xa;
        pLeft->ya = -pLeft->ya;
        pLeft->dx = -pLeft->dx;
        pLeft->dy = -pLeft->dy;
    }
    else {
        swapslopes = 1;
        pRight->xa = -pRight->xa;
        pRight->ya = -pRight->ya;
        pRight->dx = -pRight->dx;
        pRight->dy = -pRight->dy;
    }

    vertices[0].x = pRight->xa;
    vertices[0].y = pRight->ya;
    slopes[0].dx = -pRight->dy;
    slopes[0].dy = pRight->dx;
    slopes[0].k = 0;

    vertices[1].x = 0;
    vertices[1].y = 0;
    slopes[1].dx = pLeft->dy;
    slopes[1].dy = -pLeft->dx;
    slopes[1].k = 0;

    vertices[2].x = pLeft->xa;
    vertices[2].y = pLeft->ya;

    if (joinStyle == JoinMiter) {
        my = (pLeft->dy * (pRight->xa * pRight->dy - pRight->ya * pRight->dx) -
              pRight->dy * (pLeft->xa * pLeft->dy - pLeft->ya * pLeft->dx)) /
            denom;
        if (pLeft->dy != 0)
            mx = pLeft->xa + (my - pLeft->ya) *
                (double) pLeft->dx / (double) pLeft->dy;
        else
            mx = pRight->xa + (my - pRight->ya) *
                (double) pRight->dx / (double) pRight->dy;

        /* check miter limit */
        if ((mx * mx + my * my) * 4 > SQSECANT * lw * lw)
            joinStyle = JoinBevel;
    }

    if (joinStyle == JoinMiter) {
        slopes[2].dx = pLeft->dx;
        slopes[2].dy = pLeft->dy;
        slopes[2].k = pLeft->k;
        if (swapslopes) {
            slopes[2].dx = -slopes[2].dx;
            slopes[2].dy = -slopes[2].dy;
            slopes[2].k = -slopes[2].k;
        }
        vertices[3].x = mx;
        vertices[3].y = my;
        slopes[3].dx = pRight->dx;
        slopes[3].dy = pRight->dy;
        slopes[3].k = pRight->k;
        if (swapslopes) {
            slopes[3].dx = -slopes[3].dx;
            slopes[3].dy = -slopes[3].dy;
            slopes[3].k = -slopes[3].k;
        }
        edgecount = 4;
    }
    else {
        double scale, dx, dy, adx, ady;

        adx = dx = pRight->xa - pLeft->xa;
        ady = dy = pRight->ya - pLeft->ya;
        if (adx < 0)
            adx = -adx;
        if (ady < 0)
            ady = -ady;
        scale = ady;
        if (adx > ady)
            scale = adx;
        slopes[2].dx = (dx * 65536) / scale;
        slopes[2].dy = (dy * 65536) / scale;
        slopes[2].k = ((pLeft->xa + pRight->xa) * slopes[2].dy -
                       (pLeft->ya + pRight->ya) * slopes[2].dx) / 2.0;
        edgecount = 3;
    }

    y = miPolyBuildPoly(vertices, slopes, edgecount, pLeft->x, pLeft->y,
                        left, right, &nleft, &nright, &height);
    XAAFillPolyHelper(pGC, y, height, left, right, nleft, nright);
}

void
XAAPolylinesWideSolid(DrawablePtr pDrawable,
                      GCPtr pGC, int mode, int npt, DDXPointPtr pPts)
{
    XAAInfoRecPtr infoRec = GET_XAAINFORECPTR_FROM_GC(pGC);
    int x1, y1, x2, y2;
    Bool projectLeft, projectRight;
    LineFaceRec leftFace, rightFace, prevRightFace, firstFace;
    int first = TRUE;
    Bool somethingDrawn = FALSE;
    Bool selfJoin = FALSE;
    int xorg = pDrawable->x;
    int yorg = pDrawable->y;
    Bool hardClip = FALSE;

    if (!RegionNumRects(pGC->pCompositeClip))
        return;

    if (RegionNumRects(pGC->pCompositeClip) != 1) {
        miWideLine(pDrawable, pGC, mode, npt, pPts);
        return;
    }

    x2 = pPts->x;
    y2 = pPts->y;
    if (npt > 1) {
        if (mode == CoordModePrevious) {
            int nptTmp;
            register DDXPointPtr pPtsTmp;

            x1 = x2;
            y1 = y2;
            nptTmp = npt;
            pPtsTmp = pPts + 1;
            while (--nptTmp) {
                x1 += pPtsTmp->x;
                y1 += pPtsTmp->y;
                ++pPtsTmp;
            }
            if ((x2 == x1) && (y2 == y1))
                selfJoin = TRUE;
        }
        else if ((x2 == pPts[npt - 1].x) && (y2 == pPts[npt - 1].y))
            selfJoin = TRUE;
    }

    projectLeft = ((pGC->capStyle == CapProjecting) && !selfJoin);
    projectRight = FALSE;

    (*infoRec->SetupForSolidFill) (infoRec->pScrn, pGC->fgPixel, pGC->alu,
                                   pGC->planemask);

    infoRec->ClipBox = &pGC->pCompositeClip->extents;

    if (infoRec->ClippingFlags & HARDWARE_CLIP_SOLID_FILL) {
        hardClip = TRUE;
        (*infoRec->SetClippingRectangle) (infoRec->pScrn,
                                          infoRec->ClipBox->x1,
                                          infoRec->ClipBox->y1,
                                          infoRec->ClipBox->x2 - 1,
                                          infoRec->ClipBox->y2 - 1);
    }

    x2 += xorg;
    y2 += yorg;
    while (--npt) {
        x1 = x2;
        y1 = y2;
        ++pPts;
        x2 = pPts->x;
        y2 = pPts->y;
        if (mode == CoordModePrevious) {
            x2 += x1;
            y2 += y1;
        }
        else {
            x2 += xorg;
            y2 += yorg;
        }
        if ((x1 != x2) || (y1 != y2)) {
            somethingDrawn = TRUE;
            if ((npt == 1) && (pGC->capStyle == CapProjecting) && !selfJoin)
                projectRight = TRUE;
            XAAWideSegment(pGC, x1, y1, x2, y2,
                           projectLeft, projectRight, &leftFace, &rightFace);
            if (first) {
                if (selfJoin)
                    firstFace = leftFace;
                else if (pGC->capStyle == CapRound) {
                    if (pGC->lineWidth == 1) {
                        DRAW_POINT(infoRec->pScrn, x1, y1);
                    }
                    else
                        XAALineArc(pGC, &leftFace, (LineFacePtr) NULL,
                                   (double) 0.0, (double) 0.0, TRUE);
                }
            }
            else
                XAALineJoin(pGC, &leftFace, &prevRightFace);

            prevRightFace = rightFace;
            first = FALSE;
            projectLeft = FALSE;
        }
        if (npt == 1 && somethingDrawn) {
            if (selfJoin)
                XAALineJoin(pGC, &firstFace, &rightFace);
            else if (pGC->capStyle == CapRound) {
                if (pGC->lineWidth == 1) {
                    DRAW_POINT(infoRec->pScrn, x2, y2);
                }
                else
                    XAALineArc(pGC, (LineFacePtr) NULL, &rightFace,
                               (double) 0.0, (double) 0.0, TRUE);
            }
        }
    }
    /* handle crock where all points are coincedent */
    if (!somethingDrawn) {
        projectLeft = (pGC->capStyle == CapProjecting);
        XAAWideSegment(pGC, x2, y2, x2, y2, projectLeft, projectLeft,
                       &leftFace, &rightFace);
        if (pGC->capStyle == CapRound) {
            XAALineArc(pGC, &leftFace, (LineFacePtr) NULL,
                       (double) 0.0, (double) 0.0, TRUE);
            rightFace.dx = -1;  /* sleezy hack to make it work */
            XAALineArc(pGC, (LineFacePtr) NULL, &rightFace,
                       (double) 0.0, (double) 0.0, TRUE);
        }
    }

    infoRec->ClipBox = NULL;
    if (hardClip)
        (*infoRec->DisableClipping) (infoRec->pScrn);

    SET_SYNC_FLAG(infoRec);
}
