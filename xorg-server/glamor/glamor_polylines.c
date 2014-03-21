/*
 * Copyright © 2009 Intel Corporation
 * Copyright © 1998 Keith Packard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

#include "glamor_priv.h"

/** @file glamor_polylines.c
 *
 * GC PolyFillRect implementation, taken straight from fb_fill.c
 */

/**
 * glamor_poly_lines() checks if it can accelerate the lines as a group of
 * horizontal or vertical lines (rectangles), and uses existing rectangle fill
 * acceleration if so.
 */
static Bool
_glamor_poly_lines(DrawablePtr drawable, GCPtr gc, int mode, int n,
                   DDXPointPtr points, Bool fallback)
{
    xRectangle *rects;
    int x1, x2, y1, y2;
    int i;

    /* Don't try to do wide lines or non-solid fill style. */
    if (gc->lineWidth != 0) {
        /* This ends up in miSetSpans, which is accelerated as well as we
         * can hope X wide lines will be.
         */
        goto fail;
    }

    if (gc->lineStyle != LineSolid) {
        glamor_fallback("non-solid fill line style %d\n", gc->lineStyle);
        goto fail;
    }
    rects = malloc(sizeof(xRectangle) * (n - 1));
    x1 = points[0].x;
    y1 = points[0].y;
    /* If we have any non-horizontal/vertical, fall back. */
    for (i = 0; i < n - 1; i++) {
        if (mode == CoordModePrevious) {
            x2 = x1 + points[i + 1].x;
            y2 = y1 + points[i + 1].y;
        }
        else {
            x2 = points[i + 1].x;
            y2 = points[i + 1].y;
        }
        if (x1 != x2 && y1 != y2) {
            free(rects);
            glamor_fallback("stub diagonal poly_line\n");
            goto fail;
        }
        if (x1 < x2) {
            rects[i].x = x1;
            rects[i].width = x2 - x1 + 1;
        }
        else {
            rects[i].x = x2;
            rects[i].width = x1 - x2 + 1;
        }
        if (y1 < y2) {
            rects[i].y = y1;
            rects[i].height = y2 - y1 + 1;
        }
        else {
            rects[i].y = y2;
            rects[i].height = y1 - y2 + 1;
        }

        x1 = x2;
        y1 = y2;
    }
    gc->ops->PolyFillRect(drawable, gc, n - 1, rects);
    free(rects);
    return TRUE;

 fail:
    if (!fallback && glamor_ddx_fallback_check_pixmap(drawable)
        && glamor_ddx_fallback_check_gc(gc))
        return FALSE;

    switch (gc->lineStyle) {
    case LineSolid:
        if (gc->lineWidth == 0)
            miZeroLine(drawable, gc, mode, n, points);
        else
            miWideLine(drawable, gc, mode, n, points);
        break;
    case LineOnOffDash:
    case LineDoubleDash:
        miWideDash(drawable, gc, mode, n, points);
        break;
    }

    return TRUE;
}

void
glamor_poly_lines(DrawablePtr drawable, GCPtr gc, int mode, int n,
                  DDXPointPtr points)
{
    _glamor_poly_lines(drawable, gc, mode, n, points, TRUE);
}

Bool
glamor_poly_lines_nf(DrawablePtr drawable, GCPtr gc, int mode, int n,
                     DDXPointPtr points)
{
    return _glamor_poly_lines(drawable, gc, mode, n, points, FALSE);
}
