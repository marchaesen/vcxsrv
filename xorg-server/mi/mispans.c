/***********************************************************

Copyright 1989, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.

Copyright 1989 by Digital Equipment Corporation, Maynard, Massachusetts.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of Digital not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include "misc.h"
#include "pixmapstr.h"
#include "gcstruct.h"
#include "mispans.h"

/*

These routines maintain lists of Spans, in order to implement the
``touch-each-pixel-once'' rules of wide lines and arcs.

Written by Joel McCormack, Summer 1989.

*/

void
miInitSpanGroup(SpanGroup * spanGroup)
{
    spanGroup->size = 0;
    spanGroup->count = 0;
    spanGroup->group = NULL;
    spanGroup->ymin = MAXSHORT;
    spanGroup->ymax = MINSHORT;
}                               /* InitSpanGroup */

#define YMIN(spans) (spans->points[0].y)
#define YMAX(spans)  (spans->points[spans->count-1].y)

static void
miSubtractSpans(SpanGroup * spanGroup, Spans * sub)
{
    int i, subCount, spansCount;
    int ymin, ymax, xmin, xmax;
    Spans *spans;
    DDXPointPtr subPt, spansPt;
    int *subWid, *spansWid;
    int extra;

    ymin = YMIN(sub);
    ymax = YMAX(sub);
    spans = spanGroup->group;
    for (i = spanGroup->count; i; i--, spans++) {
        if (YMIN(spans) <= ymax && ymin <= YMAX(spans)) {
            subCount = sub->count;
            subPt = sub->points;
            subWid = sub->widths;
            spansCount = spans->count;
            spansPt = spans->points;
            spansWid = spans->widths;
            extra = 0;
            for (;;) {
                while (spansCount && spansPt->y < subPt->y) {
                    spansPt++;
                    spansWid++;
                    spansCount--;
                }
                if (!spansCount)
                    break;
                while (subCount && subPt->y < spansPt->y) {
                    subPt++;
                    subWid++;
                    subCount--;
                }
                if (!subCount)
                    break;
                if (subPt->y == spansPt->y) {
                    xmin = subPt->x;
                    xmax = xmin + *subWid;
                    if (xmin >= spansPt->x + *spansWid || spansPt->x >= xmax) {
                        ;
                    }
                    else if (xmin <= spansPt->x) {
                        if (xmax >= spansPt->x + *spansWid) {
                            memmove(spansPt, spansPt + 1,
                                    sizeof *spansPt * (spansCount - 1));
                            memmove(spansWid, spansWid + 1,
                                    sizeof *spansWid * (spansCount - 1));
                            spansPt--;
                            spansWid--;
                            spans->count--;
                            extra++;
                        }
                        else {
                            *spansWid = *spansWid - (xmax - spansPt->x);
                            spansPt->x = xmax;
                        }
                    }
                    else {
                        if (xmax >= spansPt->x + *spansWid) {
                            *spansWid = xmin - spansPt->x;
                        }
                        else {
                            if (!extra) {
                                DDXPointPtr newPt;
                                int *newwid;

#define EXTRA 8
                                newPt =
                                    (DDXPointPtr) realloc(spans->points,
                                                          (spans->count +
                                                           EXTRA) *
                                                          sizeof(DDXPointRec));
                                if (!newPt)
                                    break;
                                spansPt = newPt + (spansPt - spans->points);
                                spans->points = newPt;
                                newwid =
                                    (int *) realloc(spans->widths,
                                                    (spans->count +
                                                     EXTRA) * sizeof(int));
                                if (!newwid)
                                    break;
                                spansWid = newwid + (spansWid - spans->widths);
                                spans->widths = newwid;
                                extra = EXTRA;
                            }
                            memmove(spansPt + 1, spansPt,
                                    sizeof *spansPt * (spansCount));
                            memmove(spansWid + 1, spansWid,
                                    sizeof *spansWid * (spansCount));
                            spans->count++;
                            extra--;
                            *spansWid = xmin - spansPt->x;
                            spansWid++;
                            spansPt++;
                            *spansWid = *spansWid - (xmax - spansPt->x);
                            spansPt->x = xmax;
                        }
                    }
                }
                spansPt++;
                spansWid++;
                spansCount--;
            }
        }
    }
}

void
miAppendSpans(SpanGroup * spanGroup, SpanGroup * otherGroup, Spans * spans)
{
    int ymin, ymax;
    int spansCount;

    spansCount = spans->count;
    if (spansCount > 0) {
        if (spanGroup->size == spanGroup->count) {
            spanGroup->size = (spanGroup->size + 8) * 2;
            spanGroup->group = (Spans *)
                realloc(spanGroup->group, sizeof(Spans) * spanGroup->size);
        }

        spanGroup->group[spanGroup->count] = *spans;
        (spanGroup->count)++;
        ymin = spans->points[0].y;
        if (ymin < spanGroup->ymin)
            spanGroup->ymin = ymin;
        ymax = spans->points[spansCount - 1].y;
        if (ymax > spanGroup->ymax)
            spanGroup->ymax = ymax;
        if (otherGroup && otherGroup->ymin < ymax && ymin < otherGroup->ymax) {
            miSubtractSpans(otherGroup, spans);
        }
    }
    else {
        free(spans->points);
        free(spans->widths);
    }
}                               /* AppendSpans */

void
miFreeSpanGroup(SpanGroup * spanGroup)
{
    free(spanGroup->group);
}

static void
QuickSortSpansX(DDXPointRec points[], int widths[], int numSpans)
{
    int x;
    int i, j, m;
    DDXPointPtr r;

/* Always called with numSpans > 1 */
/* Sorts only by x, as all y should be the same */

#define ExchangeSpans(a, b)				    \
{							    \
    DDXPointRec 	tpt;				    \
    int    		tw;				    \
							    \
    tpt = points[a]; points[a] = points[b]; points[b] = tpt;    \
    tw = widths[a]; widths[a] = widths[b]; widths[b] = tw;  \
}

    do {
        if (numSpans < 9) {
            /* Do insertion sort */
            int xprev;

            xprev = points[0].x;
            i = 1;
            do {                /* while i != numSpans */
                x = points[i].x;
                if (xprev > x) {
                    /* points[i] is out of order.  Move into proper location. */
                    DDXPointRec tpt;
                    int tw, k;

                    for (j = 0; x >= points[j].x; j++) {
                    }
                    tpt = points[i];
                    tw = widths[i];
                    for (k = i; k != j; k--) {
                        points[k] = points[k - 1];
                        widths[k] = widths[k - 1];
                    }
                    points[j] = tpt;
                    widths[j] = tw;
                    x = points[i].x;
                }               /* if out of order */
                xprev = x;
                i++;
            } while (i != numSpans);
            return;
        }

        /* Choose partition element, stick in location 0 */
        m = numSpans / 2;
        if (points[m].x > points[0].x)
            ExchangeSpans(m, 0);
        if (points[m].x > points[numSpans - 1].x)
            ExchangeSpans(m, numSpans - 1);
        if (points[m].x > points[0].x)
            ExchangeSpans(m, 0);
        x = points[0].x;

        /* Partition array */
        i = 0;
        j = numSpans;
        do {
            r = &(points[i]);
            do {
                r++;
                i++;
            } while (i != numSpans && r->x < x);
            r = &(points[j]);
            do {
                r--;
                j--;
            } while (x < r->x);
            if (i < j)
                ExchangeSpans(i, j);
        } while (i < j);

        /* Move partition element back to middle */
        ExchangeSpans(0, j);

        /* Recurse */
        if (numSpans - j - 1 > 1)
            QuickSortSpansX(&points[j + 1], &widths[j + 1], numSpans - j - 1);
        numSpans = j;
    } while (numSpans > 1);
}                               /* QuickSortSpans */

static int
UniquifySpansX(Spans * spans, DDXPointRec * newPoints, int *newWidths)
{
    int newx1, newx2, oldpt, i, y;
    DDXPointRec *oldPoints;
    int *oldWidths;
    int *startNewWidths;

/* Always called with numSpans > 1 */
/* Uniquify the spans, and stash them into newPoints and newWidths.  Return the
   number of unique spans. */

    startNewWidths = newWidths;

    oldPoints = spans->points;
    oldWidths = spans->widths;

    y = oldPoints->y;
    newx1 = oldPoints->x;
    newx2 = newx1 + *oldWidths;

    for (i = spans->count - 1; i != 0; i--) {
        oldPoints++;
        oldWidths++;
        oldpt = oldPoints->x;
        if (oldpt > newx2) {
            /* Write current span, start a new one */
            newPoints->x = newx1;
            newPoints->y = y;
            *newWidths = newx2 - newx1;
            newPoints++;
            newWidths++;
            newx1 = oldpt;
            newx2 = oldpt + *oldWidths;
        }
        else {
            /* extend current span, if old extends beyond new */
            oldpt = oldpt + *oldWidths;
            if (oldpt > newx2)
                newx2 = oldpt;
        }
    }                           /* for */

    /* Write final span */
    newPoints->x = newx1;
    *newWidths = newx2 - newx1;
    newPoints->y = y;

    return (newWidths - startNewWidths) + 1;
}                               /* UniquifySpansX */

static void
miDisposeSpanGroup(SpanGroup * spanGroup)
{
    int i;
    Spans *spans;

    for (i = 0; i < spanGroup->count; i++) {
        spans = spanGroup->group + i;
        free(spans->points);
        free(spans->widths);
    }
}

void
miFillUniqueSpanGroup(DrawablePtr pDraw, GCPtr pGC, SpanGroup * spanGroup)
{
    int i;
    Spans *spans;
    Spans *yspans;
    int *ysizes;
    int ymin, ylength;

    /* Outgoing spans for one big call to FillSpans */
    DDXPointPtr points;
    int *widths;
    int count;

    if (spanGroup->count == 0)
        return;

    if (spanGroup->count == 1) {
        /* Already should be sorted, unique */
        spans = spanGroup->group;
        (*pGC->ops->FillSpans)
            (pDraw, pGC, spans->count, spans->points, spans->widths, TRUE);
        free(spans->points);
        free(spans->widths);
    }
    else {
        /* Yuck.  Gross.  Radix sort into y buckets, then sort x and uniquify */
        /* This seems to be the fastest thing to do.  I've tried sorting on
           both x and y at the same time rather than creating into all those
           y buckets, but it was somewhat slower. */

        ymin = spanGroup->ymin;
        ylength = spanGroup->ymax - ymin + 1;

        /* Allocate Spans for y buckets */
        yspans = malloc(ylength * sizeof(Spans));
        ysizes = malloc(ylength * sizeof(int));

        if (!yspans || !ysizes) {
            free(yspans);
            free(ysizes);
            miDisposeSpanGroup(spanGroup);
            return;
        }

        for (i = 0; i != ylength; i++) {
            ysizes[i] = 0;
            yspans[i].count = 0;
            yspans[i].points = NULL;
            yspans[i].widths = NULL;
        }

        /* Go through every single span and put it into the correct bucket */
        count = 0;
        for (i = 0, spans = spanGroup->group;
             i != spanGroup->count; i++, spans++) {
            int index;
            int j;

            for (j = 0, points = spans->points, widths = spans->widths;
                 j != spans->count; j++, points++, widths++) {
                index = points->y - ymin;
                if (index >= 0 && index < ylength) {
                    Spans *newspans = &(yspans[index]);

                    if (newspans->count == ysizes[index]) {
                        DDXPointPtr newpoints;
                        int *newwidths;

                        ysizes[index] = (ysizes[index] + 8) * 2;
                        newpoints = (DDXPointPtr) realloc(newspans->points,
                                                          ysizes[index] *
                                                          sizeof(DDXPointRec));
                        newwidths =
                            (int *) realloc(newspans->widths,
                                            ysizes[index] * sizeof(int));
                        if (!newpoints || !newwidths) {
                            for (i = 0; i < ylength; i++) {
                                free(yspans[i].points);
                                free(yspans[i].widths);
                            }
                            free(yspans);
                            free(ysizes);
                            free(newpoints);
                            free(newwidths);
                            miDisposeSpanGroup(spanGroup);
                            return;
                        }
                        newspans->points = newpoints;
                        newspans->widths = newwidths;
                    }
                    newspans->points[newspans->count] = *points;
                    newspans->widths[newspans->count] = *widths;
                    (newspans->count)++;
                }               /* if y value of span in range */
            }                   /* for j through spans */
            count += spans->count;
            free(spans->points);
            spans->points = NULL;
            free(spans->widths);
            spans->widths = NULL;
        }                       /* for i thorough Spans */

        /* Now sort by x and uniquify each bucket into the final array */
        points = malloc(count * sizeof(DDXPointRec));
        widths = malloc(count * sizeof(int));
        if (!points || !widths) {
            for (i = 0; i < ylength; i++) {
                free(yspans[i].points);
                free(yspans[i].widths);
            }
            free(yspans);
            free(ysizes);
            free(points);
            free(widths);
            return;
        }
        count = 0;
        for (i = 0; i != ylength; i++) {
            int ycount = yspans[i].count;

            if (ycount > 0) {
                if (ycount > 1) {
                    QuickSortSpansX(yspans[i].points, yspans[i].widths, ycount);
                    count += UniquifySpansX
                        (&(yspans[i]), &(points[count]), &(widths[count]));
                }
                else {
                    points[count] = yspans[i].points[0];
                    widths[count] = yspans[i].widths[0];
                    count++;
                }
                free(yspans[i].points);
                free(yspans[i].widths);
            }
        }

        (*pGC->ops->FillSpans) (pDraw, pGC, count, points, widths, TRUE);
        free(points);
        free(widths);
        free(yspans);
        free(ysizes);           /* use (DE)xalloc for these? */
    }

    spanGroup->count = 0;
    spanGroup->ymin = MAXSHORT;
    spanGroup->ymax = MINSHORT;
}
