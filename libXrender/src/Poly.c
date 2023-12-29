/*
 *
 * Copyright © 2002 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "Xrenderint.h"

typedef struct _Edge Edge;

struct _Edge {
    XLineFixed	edge;
    XFixed	current_x;
    Bool	clockWise;
    Edge	*next, *prev;
};

static int
CompareEdge (const void *o1, const void *o2)
{
    const Edge	*e1 = o1, *e2 = o2;

    return e1->edge.p1.y - e2->edge.p1.y;
}

static XFixed
XRenderComputeX (XLineFixed *line, XFixed y)
{
    XFixed  dx = line->p2.x - line->p1.x;
    double  ex = (double) (y - line->p1.y) * (double) dx;
    XFixed  dy = line->p2.y - line->p1.y;

    return (XFixed) line->p1.x + (XFixed) (ex / dy);
}

static double
XRenderComputeInverseSlope (XLineFixed *l)
{
    return (XFixedToDouble (l->p2.x - l->p1.x) /
	    XFixedToDouble (l->p2.y - l->p1.y));
}

static double
XRenderComputeXIntercept (XLineFixed *l, double inverse_slope)
{
    return XFixedToDouble (l->p1.x) - inverse_slope * XFixedToDouble (l->p1.y);
}

static XFixed
XRenderComputeIntersect (XLineFixed *l1, XLineFixed *l2)
{
    /*
     * x = m1y + b1
     * x = m2y + b2
     * m1y + b1 = m2y + b2
     * y * (m1 - m2) = b2 - b1
     * y = (b2 - b1) / (m1 - m2)
     */
    double  m1 = XRenderComputeInverseSlope (l1);
    double  b1 = XRenderComputeXIntercept (l1, m1);
    double  m2 = XRenderComputeInverseSlope (l2);
    double  b2 = XRenderComputeXIntercept (l2, m2);

    if ( m1 == m2 ) return XDoubleToFixed(32766); /* lines don't intersect */

    return XDoubleToFixed ((b2 - b1) / (m1 - m2));
}

static int
XRenderComputeTrapezoids (Edge		*edges,
			  int		nedges,
			  int		winding _X_UNUSED,
			  XTrapezoid	*traps,
			  int           *ntraps,
			  int           maxtraps)
{
    int		ok = 1, inactive;
    Edge	*active;
    Edge	*e, *en, *next;
    XFixed	y, next_y, intersect;

    qsort (edges, (size_t) nedges, sizeof (Edge), CompareEdge);

    *ntraps = 0;
    y = edges[0].edge.p1.y;
    active = NULL;
    inactive = 0;
    while (active || inactive < nedges)
    {
	/* insert new active edges into list */
	while (inactive < nedges)
	{
	    e = &edges[inactive];
	    if (e->edge.p1.y > y)
		break;
	    /* move this edge into the active list */
	    inactive++;
	    e->next = active;
	    e->prev = NULL;
	    if (active)
		active->prev = e;
	    active = e;
	}
	/* compute x coordinates along this group */
	for (e = active; e; e = e->next)
	    e->current_x = XRenderComputeX (&e->edge, y);

	/* sort active list */
	for (e = active; e; e = next)
	{
	    next = e->next;
	    /*
	     * Find one later in the list that belongs before the
	     * current one
	     */
	    for (en = next; en; en = en->next)
	    {
		if (en->current_x < e->current_x ||
		    (en->current_x == e->current_x &&
		     en->edge.p2.x < e->edge.p2.x))
		{
		    /*
		     * insert en before e
		     *
		     * extract en
		     */
		    en->prev->next = en->next;
		    if (en->next)
			en->next->prev = en->prev;
		    /*
		     * insert en
		     */
		    if (e->prev)
			e->prev->next = en;
		    else
			active = en;
		    en->prev = e->prev;
		    e->prev = en;
		    en->next = e;
		    /*
		     * start over at en
		     */
		    next = en;
		    break;
		}
	    }
	}
#if 0
	printf ("y: %6.3g:", y / 65536.0);
	for (e = active; e; e = e->next)
	{
	    printf (" %6.3g", e->current_x / 65536.0);
	}
	printf ("\n");
#endif
	/* find next inflection point */
	next_y = active->edge.p2.y;
	for (e = active; e; e = en)
	{
	    if (e->edge.p2.y < next_y)
		next_y = e->edge.p2.y;
	    en = e->next;
	    /* check intersect */
	    if (en && e->edge.p2.x > en->edge.p2.x)
	    {
		intersect = XRenderComputeIntersect (&e->edge, &e->next->edge);
		/* make sure this point is below the actual intersection */
		intersect = intersect + 1;
		if (intersect < next_y && intersect > y)
		    next_y = intersect;
	    }
	}
	/* check next inactive point */
	if (inactive < nedges && edges[inactive].edge.p1.y < next_y)
	    next_y = edges[inactive].edge.p1.y;

	/* walk the list generating trapezoids */
	for (e = active; e && (en = e->next); e = en->next)
	{
	    traps->top = y;
	    traps->bottom = next_y;
	    traps->left = e->edge;
	    traps->right = en->edge;
	    traps++;
	    (*ntraps)++;
            if ( --maxtraps <= 0 ) {
	       ok = 0;
	       break;
	    }
	}

	y = next_y;

	/* delete inactive edges from list */
	for (e = active; e; e = next)
	{
	    next = e->next;
	    if (e->edge.p2.y <= y)
	    {
		if (e->prev)
		    e->prev->next = e->next;
		else
		    active = e->next;
		if (e->next)
		    e->next->prev = e->prev;
	    }
	}
    }
    return ok;
}

void
XRenderCompositeDoublePoly (Display		    *dpy,
			    int			    op,
			    Picture		    src,
			    Picture		    dst,
			    _Xconst XRenderPictFormat	*maskFormat,
			    int			    xSrc,
			    int			    ySrc,
			    int			    xDst _X_UNUSED,
			    int			    yDst _X_UNUSED,
			    _Xconst XPointDouble    *fpoints,
			    int			    npoints,
			    int			    winding)
{
    Edge	    *edges;
    XTrapezoid	    *traps;
    int		    i, nedges, ntraps;
    XFixed	    prevx = 0, prevy = 0, firstx = 0, firsty = 0;
    XFixed	    top = 0, bottom = 0;	/* GCCism */

    edges = Xmalloc (((size_t) npoints * sizeof (Edge)) +
                     ((size_t) (npoints * npoints) * sizeof (XTrapezoid)));
    if (!edges)
	return;
    traps = (XTrapezoid *) (edges + npoints);
    nedges = 0;
    for (i = 0; i <= npoints; i++)
    {
	XFixed	x, y;

	if (i == npoints)
	{
	    x = firstx;
	    y = firsty;
	}
	else
	{
	    x = XDoubleToFixed (fpoints[i].x);
	    y = XDoubleToFixed (fpoints[i].y);
	}
	if (i)
	{
	    if (y < top)
		top = y;
	    else if (y > bottom)
		bottom = y;
	    if (prevy < y)
	    {
		edges[nedges].edge.p1.x = prevx;
		edges[nedges].edge.p1.y = prevy;
		edges[nedges].edge.p2.x = x;
		edges[nedges].edge.p2.y = y;
		edges[nedges].clockWise = True;
		nedges++;
	    }
	    else if (prevy > y)
	    {
		edges[nedges].edge.p1.x = x;
		edges[nedges].edge.p1.y = y;
		edges[nedges].edge.p2.x = prevx;
		edges[nedges].edge.p2.y = prevy;
		edges[nedges].clockWise = False;
		nedges++;
	    }
	    /* drop horizontal edges */
	}
	else
	{
	    top = y;
	    bottom = y;
	    firstx = x;
	    firsty = y;
	}
	prevx = x;
	prevy = y;
    }
    if ( XRenderComputeTrapezoids (edges, nedges, winding, traps, &ntraps, npoints * npoints )) {
       /* XXX adjust xSrc/xDst */
       XRenderCompositeTrapezoids (dpy, op, src, dst, maskFormat, xSrc, ySrc, traps, ntraps);
    }
    Xfree (edges);
}
