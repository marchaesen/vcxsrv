/*
 * Copyright © 2014 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "glamor_priv.h"
#include "glamor_transfer.h"

/* XXX a kludge for now */
void
glamor_format_for_pixmap(PixmapPtr pixmap, GLenum *format, GLenum *type)
{
    switch (pixmap->drawable.depth) {
    case 24:
    case 32:
        *format = GL_BGRA;
        *type = GL_UNSIGNED_INT_8_8_8_8_REV;
        break;
    case 16:
        *format = GL_RGB;
        *type = GL_UNSIGNED_SHORT_5_6_5;
        break;
    case 15:
        *format = GL_BGRA;
        *type = GL_UNSIGNED_SHORT_1_5_5_5_REV;
        break;
    case 8:
        *format = GL_ALPHA;
        *type = GL_UNSIGNED_BYTE;
        break;
    default:
        FatalError("Invalid pixmap depth %d\n", pixmap->drawable.depth);
        break;
    }
}

/*
 * Write a region of bits into a pixmap
 */
void
glamor_upload_boxes(PixmapPtr pixmap, BoxPtr in_boxes, int in_nbox,
                    int dx_src, int dy_src,
                    int dx_dst, int dy_dst,
                    uint8_t *bits, uint32_t byte_stride)
{
    ScreenPtr                   screen = pixmap->drawable.pScreen;
    glamor_screen_private       *glamor_priv = glamor_get_screen_private(screen);
    glamor_pixmap_private       *priv = glamor_get_pixmap_private(pixmap);
    int                         box_x, box_y;
    int                         bytes_per_pixel = pixmap->drawable.bitsPerPixel >> 3;
    GLenum                      type;
    GLenum                      format;

    glamor_format_for_pixmap(pixmap, &format, &type);

    glamor_make_current(glamor_priv);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, byte_stride / bytes_per_pixel);

    glamor_pixmap_loop(priv, box_x, box_y) {
        BoxPtr                  box = glamor_pixmap_box_at(priv, box_x, box_y);
        glamor_pixmap_fbo       *fbo = glamor_pixmap_fbo_at(priv, box_x, box_y);
        BoxPtr                  boxes = in_boxes;
        int                     nbox = in_nbox;

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fbo->tex);

        while (nbox--) {

            /* compute drawable coordinates */
            int x1 = boxes->x1 + dx_dst;
            int x2 = boxes->x2 + dx_dst;
            int y1 = boxes->y1 + dy_dst;
            int y2 = boxes->y2 + dy_dst;

            boxes++;

            if (x1 < box->x1)
                x1 = box->x1;
            if (box->x2 < x2)
                x2 = box->x2;

            if (x2 <= x1)
                continue;

            if (y1 < box->y1)
                y1 = box->y1;
            if (box->y2 < y2)
                y2 = box->y2;

            if (y2 <= y1)
                continue;

            glPixelStorei(GL_UNPACK_SKIP_ROWS, y1 - dy_dst + dy_src);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, x1 - dx_dst + dx_src);

            glTexSubImage2D(GL_TEXTURE_2D, 0,
                            x1 - box->x1, y1 - box->y1,
                            x2 - x1, y2 - y1,
                            format, type,
                            bits);
        }
    }

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
}

/*
 * Upload a region of data
 */

void
glamor_upload_region(PixmapPtr pixmap, RegionPtr region,
                     int region_x, int region_y,
                     uint8_t *bits, uint32_t byte_stride)
{
    glamor_upload_boxes(pixmap, RegionRects(region), RegionNumRects(region),
                        -region_x, -region_y,
                        0, 0,
                        bits, byte_stride);
}

/*
 * Take the data in the pixmap and stuff it back into the FBO
 */
void
glamor_upload_pixmap(PixmapPtr pixmap)
{
    BoxRec box;

    box.x1 = 0;
    box.x2 = pixmap->drawable.width;
    box.y1 = 0;
    box.y2 = pixmap->drawable.height;
    glamor_upload_boxes(pixmap, &box, 1, 0, 0, 0, 0,
                        pixmap->devPrivate.ptr, pixmap->devKind);
}

/*
 * Read stuff from the pixmap FBOs and write to memory
 */
void
glamor_download_boxes(PixmapPtr pixmap, BoxPtr in_boxes, int in_nbox,
                      int dx_src, int dy_src,
                      int dx_dst, int dy_dst,
                      uint8_t *bits, uint32_t byte_stride)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    glamor_pixmap_private *priv = glamor_get_pixmap_private(pixmap);
    int box_x, box_y;
    int bytes_per_pixel = pixmap->drawable.bitsPerPixel >> 3;
    GLenum type;
    GLenum format;

    glamor_format_for_pixmap(pixmap, &format, &type);

    glamor_make_current(glamor_priv);

    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, byte_stride / bytes_per_pixel);

    glamor_pixmap_loop(priv, box_x, box_y) {
        BoxPtr                  box = glamor_pixmap_box_at(priv, box_x, box_y);
        glamor_pixmap_fbo       *fbo = glamor_pixmap_fbo_at(priv, box_x, box_y);
        BoxPtr                  boxes = in_boxes;
        int                     nbox = in_nbox;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo->fb);

        while (nbox--) {

            /* compute drawable coordinates */
            int                     x1 = boxes->x1 + dx_src;
            int                     x2 = boxes->x2 + dx_src;
            int                     y1 = boxes->y1 + dy_src;
            int                     y2 = boxes->y2 + dy_src;

            boxes++;

            if (x1 < box->x1)
                x1 = box->x1;
            if (box->x2 < x2)
                x2 = box->x2;

            if (y1 < box->y1)
                y1 = box->y1;
            if (box->y2 < y2)
                y2 = box->y2;

            if (x2 <= x1)
                continue;
            if (y2 <= y1)
                continue;

            glPixelStorei(GL_PACK_SKIP_PIXELS, x1 - dx_src + dx_dst);
            glPixelStorei(GL_PACK_SKIP_ROWS, y1 - dy_src + dy_dst);
            glReadPixels(x1 - box->x1, y1 - box->y1, x2 - x1, y2 - y1, format, type, bits);
        }
    }
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
}

/*
 * Read data from the pixmap FBO
 */
void
glamor_download_rect(PixmapPtr pixmap, int x, int y, int w, int h, uint8_t *bits)
{
    BoxRec      box;

    box.x1 = x;
    box.x2 = x + w;
    box.y1 = y;
    box.y2 = y + h;

    glamor_download_boxes(pixmap, &box, 1, 0, 0, -x, -y,
                          bits, PixmapBytePad(w, pixmap->drawable.depth));
}

/*
 * Pull the data from the FBO down to the pixmap
 */
void
glamor_download_pixmap(PixmapPtr pixmap)
{
    BoxRec      box;

    box.x1 = 0;
    box.x2 = pixmap->drawable.width;
    box.y1 = 0;
    box.y2 = pixmap->drawable.height;

    glamor_download_boxes(pixmap, &box, 1, 0, 0, 0, 0,
                          pixmap->devPrivate.ptr, pixmap->devKind);
}
