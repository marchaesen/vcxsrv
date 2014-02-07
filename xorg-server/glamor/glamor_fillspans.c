/*
 * Copyright © 1998 Keith Packard
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

/** @file glamor_fillspans.c
 *
 * FillSpans implementation, taken from fb_fillsp.c
 */
#include "glamor_priv.h"

static Bool
_glamor_fill_spans(DrawablePtr drawable,
                   GCPtr gc,
                   int n, DDXPointPtr points, int *widths, int sorted,
                   Bool fallback)
{
    DDXPointPtr ppt;
    int nbox;
    BoxPtr pbox;
    int x1, x2, y;
    RegionPtr pClip = fbGetCompositeClip(gc);
    Bool ret = FALSE;

    if (gc->fillStyle != FillSolid && gc->fillStyle != FillTiled)
        goto fail;

    ppt = points;
    while (n--) {
        x1 = ppt->x;
        y = ppt->y;
        x2 = x1 + (int) *widths;
        ppt++;
        widths++;

        nbox = REGION_NUM_RECTS(pClip);
        pbox = REGION_RECTS(pClip);
        while (nbox--) {
            int real_x1 = x1, real_x2 = x2;

            if (real_x1 < pbox->x1)
                real_x1 = pbox->x1;

            if (real_x2 > pbox->x2)
                real_x2 = pbox->x2;

            if (real_x2 > real_x1 && pbox->y1 <= y && pbox->y2 > y) {
                if (!glamor_fill(drawable, gc, real_x1, y,
                                 real_x2 - real_x1, 1, fallback))
                    goto fail;
            }
            pbox++;
        }
    }
    ret = TRUE;
    goto done;

 fail:
    if (!fallback && glamor_ddx_fallback_check_pixmap(drawable)
        && glamor_ddx_fallback_check_gc(gc)) {
        goto done;
    }
    glamor_fallback("to %p (%c)\n", drawable,
                    glamor_get_drawable_location(drawable));
    if (glamor_prepare_access(drawable, GLAMOR_ACCESS_RW)) {
        if (glamor_prepare_access_gc(gc)) {
            fbFillSpans(drawable, gc, n, points, widths, sorted);
            glamor_finish_access_gc(gc);
        }
        glamor_finish_access(drawable, GLAMOR_ACCESS_RW);
    }
    ret = TRUE;

 done:
    return ret;
}

void
glamor_fill_spans(DrawablePtr drawable,
                  GCPtr gc, int n, DDXPointPtr points, int *widths, int sorted)
{
    _glamor_fill_spans(drawable, gc, n, points, widths, sorted, TRUE);
}

Bool
glamor_fill_spans_nf(DrawablePtr drawable,
                     GCPtr gc,
                     int n, DDXPointPtr points, int *widths, int sorted)
{
    return _glamor_fill_spans(drawable, gc, n, points, widths, sorted, FALSE);
}
