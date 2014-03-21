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

/** @file glamor_putaimge.c
 *
 * XPutImage implementation
 */
#include "glamor_priv.h"

void
glamor_init_putimage_shaders(ScreenPtr screen)
{
}

void
glamor_fini_putimage_shaders(ScreenPtr screen)
{
}

static Bool
_glamor_put_image(DrawablePtr drawable, GCPtr gc, int depth, int x, int y,
                  int w, int h, int left_pad, int image_format, char *bits,
                  Bool fallback)
{
    PixmapPtr pixmap = glamor_get_drawable_pixmap(drawable);
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    RegionPtr clip;
    int x_off, y_off;
    Bool ret = FALSE;
    PixmapPtr temp_pixmap, sub_pixmap;
    glamor_pixmap_private *temp_pixmap_priv;
    BoxRec box;

    glamor_get_drawable_deltas(drawable, pixmap, &x_off, &y_off);
    clip = fbGetCompositeClip(gc);
    if (image_format == XYBitmap) {
        assert(depth == 1);
        goto fail;
    }

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv)) {
        glamor_fallback("has no fbo.\n");
        goto fail;
    }

    if (image_format != ZPixmap) {
        glamor_fallback("non-ZPixmap\n");
        goto fail;
    }

    if (!glamor_set_planemask(pixmap, gc->planemask)) {
        goto fail;
    }
    /* create a temporary pixmap and upload the bits to that
     * pixmap, then apply clip copy it to the destination pixmap.*/
    box.x1 = x + drawable->x;
    box.y1 = y + drawable->y;
    box.x2 = x + w + drawable->x;
    box.y2 = y + h + drawable->y;

    if ((clip != NULL && RegionContainsRect(clip, &box) != rgnIN)
        || gc->alu != GXcopy) {
        temp_pixmap = glamor_create_pixmap(drawable->pScreen, w, h, depth, 0);
        if (temp_pixmap == NULL)
            goto fail;

        temp_pixmap_priv = glamor_get_pixmap_private(temp_pixmap);

        if (GLAMOR_PIXMAP_PRIV_IS_PICTURE(pixmap_priv)) {
            temp_pixmap_priv->base.picture = pixmap_priv->base.picture;
            temp_pixmap_priv->base.is_picture = pixmap_priv->base.is_picture;
        }

        glamor_upload_sub_pixmap_to_texture(temp_pixmap, 0, 0, w, h,
                                            pixmap->devKind, bits, 0);

        glamor_copy_area(&temp_pixmap->drawable, drawable, gc, 0, 0, w, h, x,
                         y);
        glamor_destroy_pixmap(temp_pixmap);
    }
    else
        glamor_upload_sub_pixmap_to_texture(pixmap, x + drawable->x + x_off,
                                            y + drawable->y + y_off, w, h,
                                            PixmapBytePad(w, depth), bits, 0);
    ret = TRUE;
    goto done;

 fail:
    glamor_set_planemask(pixmap, ~0);

    if (!fallback && glamor_ddx_fallback_check_pixmap(&pixmap->drawable))
        goto done;

    glamor_fallback("to %p (%c)\n",
                    drawable, glamor_get_drawable_location(drawable));

    sub_pixmap = glamor_get_sub_pixmap(pixmap, x + x_off + drawable->x,
                                       y + y_off + drawable->y, w, h,
                                       GLAMOR_ACCESS_RW);
    if (sub_pixmap) {
        if (clip != NULL)
            pixman_region_translate(clip, -x - drawable->x, -y - drawable->y);

        fbPutImage(&sub_pixmap->drawable, gc, depth, 0, 0, w, h,
                   left_pad, image_format, bits);

        glamor_put_sub_pixmap(sub_pixmap, pixmap,
                              x + x_off + drawable->x,
                              y + y_off + drawable->y, w, h, GLAMOR_ACCESS_RW);
        if (clip != NULL)
            pixman_region_translate(clip, x + drawable->x, y + drawable->y);
    }
    else
        fbPutImage(drawable, gc, depth, x, y, w, h,
                   left_pad, image_format, bits);
    ret = TRUE;

 done:
    return ret;
}

void
glamor_put_image(DrawablePtr drawable, GCPtr gc, int depth, int x, int y,
                 int w, int h, int left_pad, int image_format, char *bits)
{
    _glamor_put_image(drawable, gc, depth, x, y, w, h,
                      left_pad, image_format, bits, TRUE);
}

Bool
glamor_put_image_nf(DrawablePtr drawable, GCPtr gc, int depth, int x, int y,
                    int w, int h, int left_pad, int image_format, char *bits)
{
    return _glamor_put_image(drawable, gc, depth, x, y, w, h,
                             left_pad, image_format, bits, FALSE);
}
