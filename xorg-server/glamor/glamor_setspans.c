/*
 * Copyright © 2009 Intel Corporation
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
 *    Zhigang Gong <zhigang.gong@linux.intel.com>
 *
 */

#include "glamor_priv.h"

static Bool
_glamor_set_spans(DrawablePtr drawable, GCPtr gc, char *src,
                  DDXPointPtr points, int *widths, int numPoints, int sorted,
                  Bool fallback)
{
    PixmapPtr dest_pixmap = glamor_get_drawable_pixmap(drawable);
    glamor_pixmap_private *dest_pixmap_priv;
    int i;
    uint8_t *drawpixels_src = (uint8_t *) src;
    RegionPtr clip = fbGetCompositeClip(gc);
    BoxRec *pbox;
    int x_off, y_off;
    Bool ret = FALSE;

    dest_pixmap_priv = glamor_get_pixmap_private(dest_pixmap);
    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(dest_pixmap_priv)) {
        glamor_fallback("pixmap has no fbo.\n");
        goto fail;
    }

    /* XXX Shall we set alu here? */
    if (!glamor_set_planemask(dest_pixmap, gc->planemask))
        goto fail;

    glamor_get_drawable_deltas(drawable, dest_pixmap, &x_off, &y_off);
    for (i = 0; i < numPoints; i++) {

        int n = REGION_NUM_RECTS(clip);

        pbox = REGION_RECTS(clip);
        while (n--) {
            int x1 = points[i].x;
            int x2 = x1 + widths[i];
            int y1 = points[i].y;

            if (pbox->y1 > points[i].y || pbox->y2 < points[i].y)
                break;
            x1 = x1 > pbox->x1 ? x1 : pbox->x1;
            x2 = x2 < pbox->x2 ? x2 : pbox->x2;
            if (x1 >= x2)
                continue;
            glamor_upload_sub_pixmap_to_texture(dest_pixmap, x1 + x_off,
                                                y1 + y_off, x2 - x1, 1,
                                                PixmapBytePad(widths[i],
                                                              drawable->depth),
                                                drawpixels_src, 0);
        }
        drawpixels_src += PixmapBytePad(widths[i], drawable->depth);
    }
    ret = TRUE;
    goto done;

 fail:
    if (!fallback && glamor_ddx_fallback_check_pixmap(drawable))
        goto done;

    glamor_fallback("to %p (%c)\n",
                    drawable, glamor_get_drawable_location(drawable));
    if (glamor_prepare_access(drawable, GLAMOR_ACCESS_RW)) {
        fbSetSpans(drawable, gc, src, points, widths, numPoints, sorted);
        glamor_finish_access(drawable, GLAMOR_ACCESS_RW);
    }
    ret = TRUE;

 done:
    return ret;
}

void
glamor_set_spans(DrawablePtr drawable, GCPtr gc, char *src,
                 DDXPointPtr points, int *widths, int n, int sorted)
{
    _glamor_set_spans(drawable, gc, src, points, widths, n, sorted, TRUE);
}

Bool
glamor_set_spans_nf(DrawablePtr drawable, GCPtr gc, char *src,
                    DDXPointPtr points, int *widths, int n, int sorted)
{
    return _glamor_set_spans(drawable, gc, src, points,
                             widths, n, sorted, FALSE);
}
