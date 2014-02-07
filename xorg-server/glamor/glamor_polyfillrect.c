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

/** @file glamor_fillspans.c
 *
 * GC PolyFillRect implementation, taken straight from fb_fill.c
 */

static Bool
_glamor_poly_fill_rect(DrawablePtr drawable,
                       GCPtr gc, int nrect, xRectangle *prect, Bool fallback)
{
    int fullX1, fullX2, fullY1, fullY2;
    int xorg, yorg;
    int n;
    register BoxPtr pbox;
    RegionPtr pClip = fbGetCompositeClip(gc);
    Bool ret = FALSE;

    xorg = drawable->x;
    yorg = drawable->y;

    while (nrect--) {
        fullX1 = prect->x + xorg;
        fullY1 = prect->y + yorg;
        fullX2 = fullX1 + (int) prect->width;
        fullY2 = fullY1 + (int) prect->height;

        n = REGION_NUM_RECTS(pClip);
        pbox = REGION_RECTS(pClip);
        /*
         * clip the rectangle to each box in the clip region
         * this is logically equivalent to calling Intersect(),
         * but rectangles may overlap each other here.
         */
        while (n--) {
            int x1 = fullX1;
            int x2 = fullX2;
            int y1 = fullY1;
            int y2 = fullY2;

            if (pbox->x1 > x1)
                x1 = pbox->x1;
            if (pbox->x2 < x2)
                x2 = pbox->x2;
            if (pbox->y1 > y1)
                y1 = pbox->y1;
            if (pbox->y2 < y2)
                y2 = pbox->y2;

            pbox++;
            if (x1 >= x2 || y1 >= y2)
                continue;
            if (!glamor_fill(drawable, gc, x1, y1, x2 - x1, y2 - y1, fallback)) {
                nrect++;
                goto fail;
            }
        }
        prect++;
    }
    ret = TRUE;
    goto done;

 fail:

    if (!fallback && glamor_ddx_fallback_check_pixmap(drawable)
        && glamor_ddx_fallback_check_gc(gc))
        goto done;

    glamor_fallback(" to %p (%c)\n",
                    drawable, glamor_get_drawable_location(drawable));
    if (glamor_prepare_access(drawable, GLAMOR_ACCESS_RW)) {
        if (glamor_prepare_access_gc(gc)) {
            fbPolyFillRect(drawable, gc, nrect, prect);
            glamor_finish_access_gc(gc);
        }
        glamor_finish_access(drawable, GLAMOR_ACCESS_RW);
    }
    ret = TRUE;

 done:
    return ret;
}

void
glamor_poly_fill_rect(DrawablePtr drawable,
                      GCPtr gc, int nrect, xRectangle *prect)
{
    _glamor_poly_fill_rect(drawable, gc, nrect, prect, TRUE);
}

Bool
glamor_poly_fill_rect_nf(DrawablePtr drawable,
                         GCPtr gc, int nrect, xRectangle *prect)
{
    return _glamor_poly_fill_rect(drawable, gc, nrect, prect, FALSE);
}
