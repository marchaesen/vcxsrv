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
 *    Zhigang Gong <zhigang.gong@gmail.com>
 *
 */

#include <stdlib.h>

#include "glamor_priv.h"
#include "mipict.h"

/* Upload picture to texture.  We may need to flip the y axis or
 * wire alpha to 1. So we may conditional create fbo for the picture.
 * */
enum glamor_pixmap_status
glamor_upload_picture_to_texture(PicturePtr picture)
{
    PixmapPtr pixmap;

    assert(picture->pDrawable);
    pixmap = glamor_get_drawable_pixmap(picture->pDrawable);

    return glamor_upload_pixmap_to_texture(pixmap);
}

/*
 * We should already have drawable attached to it, if it has one.
 * Then set the attached pixmap to is_picture format, and set
 * the pict format.
 * */
int
glamor_create_picture(PicturePtr picture)
{
    PixmapPtr pixmap;
    glamor_pixmap_private *pixmap_priv;

    if (!picture || !picture->pDrawable)
        return 0;

    pixmap = glamor_get_drawable_pixmap(picture->pDrawable);
    pixmap_priv = glamor_get_pixmap_private(pixmap);
    if (!pixmap_priv) {
        /* We must create a pixmap priv to track the picture format even
         * if the pixmap is a pure in memory pixmap. The reason is that
         * we may need to upload this pixmap to a texture on the fly. During
         * the uploading, we need to know the picture format. */
        glamor_set_pixmap_type(pixmap, GLAMOR_MEMORY);
        pixmap_priv = glamor_get_pixmap_private(pixmap);
    }
    else {
        if (GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv)) {
            /* If the picture format is not compatible with glamor fbo format,
             * we have to mark this pixmap as a separated texture, and don't
             * fallback to DDX layer. */
            if (pixmap_priv->type == GLAMOR_TEXTURE_DRM
                && !glamor_pict_format_is_compatible(picture))
                glamor_set_pixmap_type(pixmap, GLAMOR_SEPARATE_TEXTURE);
        }
    }

    pixmap_priv->base.is_picture = 1;
    pixmap_priv->base.picture = picture;

    return miCreatePicture(picture);
}

void
glamor_destroy_picture(PicturePtr picture)
{
    PixmapPtr pixmap;
    glamor_pixmap_private *pixmap_priv;

    if (!picture || !picture->pDrawable)
        return;

    pixmap = glamor_get_drawable_pixmap(picture->pDrawable);
    pixmap_priv = glamor_get_pixmap_private(pixmap);

    if (pixmap_priv) {
        pixmap_priv->base.is_picture = 0;
        pixmap_priv->base.picture = NULL;
    }
    miDestroyPicture(picture);
}

void
glamor_picture_format_fixup(PicturePtr picture,
                            glamor_pixmap_private *pixmap_priv)
{
    pixmap_priv->base.picture = picture;
}
