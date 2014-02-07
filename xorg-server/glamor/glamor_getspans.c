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
_glamor_get_spans(DrawablePtr drawable,
                  int wmax,
                  DDXPointPtr points, int *widths, int count, char *dst,
                  Bool fallback)
{
    PixmapPtr pixmap = glamor_get_drawable_pixmap(drawable);
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    int i;
    uint8_t *readpixels_dst = (uint8_t *) dst;
    void *data;
    int x_off, y_off;
    Bool ret = FALSE;

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        goto fail;

    glamor_get_drawable_deltas(drawable, pixmap, &x_off, &y_off);
    for (i = 0; i < count; i++) {
        data = glamor_download_sub_pixmap_to_cpu(pixmap, points[i].x + x_off,
                                                 points[i].y + y_off, widths[i],
                                                 1, PixmapBytePad(widths[i],
                                                                  drawable->
                                                                  depth),
                                                 readpixels_dst, 0,
                                                 GLAMOR_ACCESS_RO);
        (void)data;
        assert(data == readpixels_dst);
        readpixels_dst += PixmapBytePad(widths[i], drawable->depth);
    }

    ret = TRUE;
    goto done;
 fail:

    if (!fallback && glamor_ddx_fallback_check_pixmap(drawable))
        goto done;

    ret = TRUE;
    if (glamor_prepare_access(drawable, GLAMOR_ACCESS_RO)) {
        fbGetSpans(drawable, wmax, points, widths, count, dst);
        glamor_finish_access(drawable, GLAMOR_ACCESS_RO);
    }
 done:
    return ret;
}

void
glamor_get_spans(DrawablePtr drawable,
                 int wmax,
                 DDXPointPtr points, int *widths, int count, char *dst)
{
    _glamor_get_spans(drawable, wmax, points, widths, count, dst, TRUE);
}

Bool
glamor_get_spans_nf(DrawablePtr drawable,
                    int wmax,
                    DDXPointPtr points, int *widths, int count, char *dst)
{
    return _glamor_get_spans(drawable, wmax, points, widths, count, dst, FALSE);
}
