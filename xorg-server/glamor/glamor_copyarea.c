/*
 * Copyright © 2008 Intel Corporation
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
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Zhigang Gong <zhigang.gong@linux.intel.com>
 */

#include "glamor_priv.h"

/** @file glamor_copyarea.c
 *
 * GC CopyArea implementation
 */
static Bool
glamor_copy_n_to_n_fbo_blit(DrawablePtr src,
                            DrawablePtr dst,
                            GCPtr gc, BoxPtr box, int nbox, int dx, int dy)
{
    ScreenPtr screen = dst->pScreen;
    PixmapPtr dst_pixmap = glamor_get_drawable_pixmap(dst);
    PixmapPtr src_pixmap = glamor_get_drawable_pixmap(src);
    glamor_pixmap_private *src_pixmap_priv, *dst_pixmap_priv;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    int dst_x_off, dst_y_off, src_x_off, src_y_off, i;
    int fbo_x_off, fbo_y_off;
    int src_fbo_x_off, src_fbo_y_off;

    if (!glamor_priv->has_fbo_blit) {
        glamor_delayed_fallback(screen, "no EXT_framebuffer_blit\n");
        return FALSE;
    }
    src_pixmap_priv = glamor_get_pixmap_private(src_pixmap);
    dst_pixmap_priv = glamor_get_pixmap_private(dst_pixmap);

    if (gc) {
        if (gc->alu != GXcopy) {
            glamor_delayed_fallback(screen, "non-copy ALU\n");
            return FALSE;
        }
    }

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(src_pixmap_priv)) {
        glamor_delayed_fallback(screen, "no src fbo\n");
        return FALSE;
    }

    if (glamor_set_destination_pixmap(dst_pixmap))
        return FALSE;

    pixmap_priv_get_fbo_off(dst_pixmap_priv, &fbo_x_off, &fbo_y_off);
    pixmap_priv_get_fbo_off(src_pixmap_priv, &src_fbo_x_off, &src_fbo_y_off);

    glamor_make_current(glamor_priv);
    glBindFramebuffer(GL_READ_FRAMEBUFFER_EXT, src_pixmap_priv->base.fbo->fb);
    glamor_get_drawable_deltas(dst, dst_pixmap, &dst_x_off, &dst_y_off);
    glamor_get_drawable_deltas(src, src_pixmap, &src_x_off, &src_y_off);
    dst_x_off += fbo_x_off;
    dst_y_off += fbo_y_off;
    src_y_off += dy + src_fbo_y_off;
    src_x_off += src_fbo_x_off;

    for (i = 0; i < nbox; i++) {
        if (glamor_priv->yInverted) {
            glBlitFramebuffer(box[i].x1 + dx + src_x_off,
                              box[i].y1 + src_y_off,
                              box[i].x2 + dx + src_x_off,
                              box[i].y2 + src_y_off,
                              box[i].x1 + dst_x_off,
                              box[i].y1 + dst_y_off,
                              box[i].x2 + dst_x_off,
                              box[i].y2 + dst_y_off,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        else {
            int flip_dst_y1 =
                dst_pixmap->drawable.height - (box[i].y2 + dst_y_off);
            int flip_dst_y2 =
                dst_pixmap->drawable.height - (box[i].y1 + dst_y_off);
            int flip_src_y1 =
                src_pixmap->drawable.height - (box[i].y2 + src_y_off);
            int flip_src_y2 =
                src_pixmap->drawable.height - (box[i].y1 + src_y_off);

            glBlitFramebuffer(box[i].x1 + dx + src_x_off,
                              flip_src_y1,
                              box[i].x2 + dx + src_x_off,
                              flip_src_y2,
                              box[i].x1 + dst_x_off,
                              flip_dst_y1,
                              box[i].x2 + dst_x_off,
                              flip_dst_y2,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
    }
    glamor_priv->state = BLIT_STATE;
    return TRUE;
}

static Bool
glamor_copy_n_to_n_textured(DrawablePtr src,
                            DrawablePtr dst,
                            GCPtr gc, BoxPtr box, int nbox, int dx, int dy)
{
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(dst->pScreen);
    PixmapPtr src_pixmap = glamor_get_drawable_pixmap(src);
    PixmapPtr dst_pixmap = glamor_get_drawable_pixmap(dst);
    int i;
    float vertices[8], texcoords[8];
    glamor_pixmap_private *src_pixmap_priv;
    glamor_pixmap_private *dst_pixmap_priv;
    int src_x_off, src_y_off, dst_x_off, dst_y_off;
    enum glamor_pixmap_status src_status = GLAMOR_NONE;
    GLfloat dst_xscale, dst_yscale, src_xscale, src_yscale;

    src_pixmap_priv = glamor_get_pixmap_private(src_pixmap);
    dst_pixmap_priv = glamor_get_pixmap_private(dst_pixmap);

    if (src_pixmap_priv->base.gl_fbo == GLAMOR_FBO_UNATTACHED) {
#ifndef GLAMOR_PIXMAP_DYNAMIC_UPLOAD
        glamor_delayed_fallback(dst->pScreen, "src has no fbo.\n");
        return FALSE;
#else
        src_status = glamor_upload_pixmap_to_texture(src_pixmap);
        if (src_status != GLAMOR_UPLOAD_DONE)
            return FALSE;

        src_pixmap_priv = glamor_get_pixmap_private(src_pixmap);
#endif
    }

    pixmap_priv_get_dest_scale(dst_pixmap_priv, &dst_xscale, &dst_yscale);
    pixmap_priv_get_scale(src_pixmap_priv, &src_xscale, &src_yscale);

    glamor_get_drawable_deltas(dst, dst_pixmap, &dst_x_off, &dst_y_off);

    glamor_make_current(glamor_priv);

    glamor_set_destination_pixmap_priv_nc(dst_pixmap_priv);
    glVertexAttribPointer(GLAMOR_VERTEX_POS, 2, GL_FLOAT,
                          GL_FALSE, 2 * sizeof(float), vertices);
    glEnableVertexAttribArray(GLAMOR_VERTEX_POS);

    glamor_get_drawable_deltas(src, src_pixmap, &src_x_off, &src_y_off);
    dx += src_x_off;
    dy += src_y_off;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_pixmap_priv->base.fbo->tex);
    if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glVertexAttribPointer(GLAMOR_VERTEX_SOURCE, 2, GL_FLOAT, GL_FALSE,
                          2 * sizeof(float), texcoords);
    glEnableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    glUseProgram(glamor_priv->finish_access_prog[0]);
    glUniform1i(glamor_priv->finish_access_revert[0], REVERT_NONE);
    glUniform1i(glamor_priv->finish_access_swap_rb[0], SWAP_NONE_UPLOADING);

    for (i = 0; i < nbox; i++) {

        glamor_set_normalize_vcoords(dst_pixmap_priv,
                                     dst_xscale, dst_yscale,
                                     box[i].x1 + dst_x_off,
                                     box[i].y1 + dst_y_off,
                                     box[i].x2 + dst_x_off,
                                     box[i].y2 + dst_y_off,
                                     glamor_priv->yInverted, vertices);

        glamor_set_normalize_tcoords(src_pixmap_priv,
                                     src_xscale,
                                     src_yscale,
                                     box[i].x1 + dx,
                                     box[i].y1 + dy,
                                     box[i].x2 + dx,
                                     box[i].y2 + dy,
                                     glamor_priv->yInverted, texcoords);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);
    glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    /* The source texture is bound to a fbo, we have to flush it here. */
    glamor_priv->state = RENDER_STATE;
    glamor_priv->render_idle_cnt = 0;
    return TRUE;
}

static Bool
__glamor_copy_n_to_n(DrawablePtr src,
                     DrawablePtr dst,
                     GCPtr gc,
                     BoxPtr box,
                     int nbox,
                     int dx,
                     int dy,
                     Bool reverse,
                     Bool upsidedown, Pixel bitplane, void *closure)
{
    PixmapPtr dst_pixmap, src_pixmap, temp_pixmap = NULL;
    DrawablePtr temp_src = src;
    glamor_pixmap_private *dst_pixmap_priv, *src_pixmap_priv;
    glamor_screen_private *glamor_priv;
    BoxRec bound;
    ScreenPtr screen;
    int temp_dx = dx;
    int temp_dy = dy;
    int src_x_off, src_y_off, dst_x_off, dst_y_off;
    int i;
    int overlaped = 0;
    Bool ret = FALSE;

    dst_pixmap = glamor_get_drawable_pixmap(dst);
    dst_pixmap_priv = glamor_get_pixmap_private(dst_pixmap);
    src_pixmap = glamor_get_drawable_pixmap(src);
    src_pixmap_priv = glamor_get_pixmap_private(src_pixmap);
    screen = dst_pixmap->drawable.pScreen;
    glamor_priv = glamor_get_screen_private(dst->pScreen);
    glamor_get_drawable_deltas(src, src_pixmap, &src_x_off, &src_y_off);

    glamor_get_drawable_deltas(dst, dst_pixmap, &dst_x_off, &dst_y_off);

    if (src_pixmap_priv->base.fbo
        && src_pixmap_priv->base.fbo->fb == dst_pixmap_priv->base.fbo->fb) {
        int x_shift = abs(src_x_off - dx - dst_x_off);
        int y_shift = abs(src_y_off - dy - dst_y_off);

        for (i = 0; i < nbox; i++) {
            if (x_shift < abs(box[i].x2 - box[i].x1)
                && y_shift < abs(box[i].y2 - box[i].y1)) {
                overlaped = 1;
                break;
            }
        }
    }
    DEBUGF("Copy %d %d %dx%d dx %d dy %d from %p to %p \n",
           box[0].x1, box[0].y1,
           box[0].x2 - box[0].x1, box[0].y2 - box[0].y1,
           dx, dy, src_pixmap, dst_pixmap);
    if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP &&
        !overlaped &&
        (glamor_priv->state != RENDER_STATE
         || !src_pixmap_priv->base.gl_tex || !dst_pixmap_priv->base.gl_tex)
        && glamor_copy_n_to_n_fbo_blit(src, dst, gc, box, nbox, dx, dy)) {
        ret = TRUE;
        goto done;
    }
    glamor_calculate_boxes_bound(&bound, box, nbox);

    /*  Overlaped indicate the src and dst are the same pixmap. */
    if (overlaped || (!GLAMOR_PIXMAP_PRIV_HAS_FBO(src_pixmap_priv)
                      && (((bound.x2 - bound.x1) * (bound.y2 - bound.y1)
                           * 4 >
                           src_pixmap->drawable.width *
                           src_pixmap->drawable.height)
                          || !(glamor_check_fbo_size(glamor_priv,
                                                     src_pixmap->drawable.width,
                                                     src_pixmap->drawable.
                                                     height))))) {

        temp_pixmap = glamor_create_pixmap(screen,
                                           bound.x2 - bound.x1,
                                           bound.y2 - bound.y1,
                                           src_pixmap->drawable.depth,
                                           overlaped ? 0 :
                                           GLAMOR_CREATE_PIXMAP_CPU);
        assert(bound.x2 - bound.x1 <= glamor_priv->max_fbo_size);
        assert(bound.y2 - bound.y1 <= glamor_priv->max_fbo_size);
        if (!temp_pixmap)
            goto done;
        glamor_translate_boxes(box, nbox, -bound.x1, -bound.y1);
        temp_src = &temp_pixmap->drawable;

        if (overlaped)
            glamor_copy_n_to_n_textured(src, temp_src, gc, box,
                                        nbox,
                                        temp_dx + bound.x1, temp_dy + bound.y1);
        else
            fbCopyNtoN(src, temp_src, gc, box, nbox,
                       temp_dx + bound.x1, temp_dy + bound.y1,
                       reverse, upsidedown, bitplane, closure);
        glamor_translate_boxes(box, nbox, bound.x1, bound.y1);
        temp_dx = -bound.x1;
        temp_dy = -bound.y1;
    }
    else {
        temp_dx = dx;
        temp_dy = dy;
        temp_src = src;
    }

    if (glamor_copy_n_to_n_textured
        (temp_src, dst, gc, box, nbox, temp_dx, temp_dy)) {
        ret = TRUE;
    }
 done:
    if (temp_src != src)
        glamor_destroy_pixmap(temp_pixmap);
    return ret;
}

static Bool
_glamor_copy_n_to_n(DrawablePtr src,
                    DrawablePtr dst,
                    GCPtr gc,
                    BoxPtr box,
                    int nbox,
                    int dx,
                    int dy,
                    Bool reverse,
                    Bool upsidedown, Pixel bitplane,
                    void *closure, Bool fallback)
{
    ScreenPtr screen = dst->pScreen;
    PixmapPtr dst_pixmap, src_pixmap;
    glamor_pixmap_private *dst_pixmap_priv, *src_pixmap_priv;
    glamor_screen_private *glamor_priv;
    BoxPtr extent;
    RegionRec region;
    int src_x_off, src_y_off, dst_x_off, dst_y_off;
    Bool ok = FALSE;
    int force_clip = 0;

    if (nbox == 0)
        return TRUE;
    dst_pixmap = glamor_get_drawable_pixmap(dst);
    dst_pixmap_priv = glamor_get_pixmap_private(dst_pixmap);
    src_pixmap = glamor_get_drawable_pixmap(src);
    src_pixmap_priv = glamor_get_pixmap_private(src_pixmap);

    glamor_priv = glamor_get_screen_private(screen);

    DEBUGF("Copy %d %d %dx%d dx %d dy %d from %p to %p \n",
           box[0].x1, box[0].y1,
           box[0].x2 - box[0].x1, box[0].y2 - box[0].y1,
           dx, dy, src_pixmap, dst_pixmap);

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(dst_pixmap_priv))
        goto fall_back;

    if (gc) {
        if (!glamor_set_planemask(dst_pixmap, gc->planemask))
            goto fall_back;
        glamor_make_current(glamor_priv);
        if (!glamor_set_alu(screen, gc->alu)) {
            goto fail_noregion;
        }
    }

    if (!src_pixmap_priv) {
        glamor_set_pixmap_type(src_pixmap, GLAMOR_MEMORY);
        src_pixmap_priv = glamor_get_pixmap_private(src_pixmap);
    }

    glamor_get_drawable_deltas(src, src_pixmap, &src_x_off, &src_y_off);
    glamor_get_drawable_deltas(dst, dst_pixmap, &dst_x_off, &dst_y_off);

    RegionInitBoxes(&region, box, nbox);
    extent = RegionExtents(&region);

    if (!glamor_check_fbo_size(glamor_priv,
                               extent->x2 - extent->x1, extent->y2 - extent->y1)
        && (src_pixmap_priv->type == GLAMOR_MEMORY
            || (src_pixmap_priv == dst_pixmap_priv))) {
        force_clip = 1;
    }

    if (force_clip || dst_pixmap_priv->type == GLAMOR_TEXTURE_LARGE
        || src_pixmap_priv->type == GLAMOR_TEXTURE_LARGE) {
        glamor_pixmap_clipped_regions *clipped_dst_regions;
        int n_dst_region, i, j;
        PixmapPtr temp_source_pixmap;
        glamor_pixmap_private *temp_source_priv = NULL;

        RegionTranslate(&region, dst_x_off, dst_y_off);
        if (!force_clip)
            clipped_dst_regions =
                glamor_compute_clipped_regions(dst_pixmap_priv, &region,
                                               &n_dst_region, 0, reverse,
                                               upsidedown);
        else
            clipped_dst_regions =
                glamor_compute_clipped_regions_ext(dst_pixmap_priv, &region,
                                                   &n_dst_region,
                                                   glamor_priv->max_fbo_size,
                                                   glamor_priv->max_fbo_size,
                                                   reverse, upsidedown);
        for (i = 0; i < n_dst_region; i++) {
            int n_src_region;
            glamor_pixmap_clipped_regions *clipped_src_regions;
            BoxPtr current_boxes;
            int n_current_boxes;

            SET_PIXMAP_FBO_CURRENT(dst_pixmap_priv,
                                   clipped_dst_regions[i].block_idx);

            temp_source_pixmap = NULL;
            if (src_pixmap_priv->type == GLAMOR_TEXTURE_LARGE) {
                RegionTranslate(clipped_dst_regions[i].region,
                                -dst_x_off + src_x_off + dx,
                                -dst_y_off + src_y_off + dy);
                clipped_src_regions =
                    glamor_compute_clipped_regions(src_pixmap_priv,
                                                   clipped_dst_regions[i].
                                                   region, &n_src_region, 0,
                                                   reverse, upsidedown);
                DEBUGF("Source is large pixmap.\n");
                for (j = 0; j < n_src_region; j++) {
                    if (src_pixmap_priv != dst_pixmap_priv)
                        SET_PIXMAP_FBO_CURRENT(src_pixmap_priv,
                                               clipped_src_regions[j].
                                               block_idx);
                    else if (src_pixmap_priv == dst_pixmap_priv &&
                             clipped_src_regions[j].block_idx !=
                             clipped_dst_regions[i].block_idx) {
                        /* source and the dest are the same, but need different block_idx.
                         * we create a empty pixmap and fill the required source fbo and box to
                         * it. It's a little hacky, but avoid extra copy. */
                        temp_source_pixmap =
                            glamor_create_pixmap(src->pScreen, 0, 0, src->depth,
                                                 0);
                        if (!temp_source_pixmap) {
                            ok = FALSE;
                            goto fail;
                        }
                        src->pScreen->ModifyPixmapHeader(temp_source_pixmap,
                                                         src_pixmap->drawable.
                                                         width,
                                                         src_pixmap->drawable.
                                                         height, 0, 0,
                                                         src_pixmap->devKind,
                                                         NULL);
                        temp_source_priv =
                            glamor_get_pixmap_private(temp_source_pixmap);
                        *temp_source_priv = *src_pixmap_priv;
                        temp_source_priv->large.box =
                            src_pixmap_priv->large.
                            box_array[clipped_src_regions[j].block_idx];
                        temp_source_priv->base.fbo =
                            src_pixmap_priv->large.
                            fbo_array[clipped_src_regions[j].block_idx];
                        temp_source_priv->base.pixmap = temp_source_pixmap;
                    }
                    assert(temp_source_pixmap ||
                           !(src_pixmap_priv == dst_pixmap_priv &&
                             (clipped_src_regions[j].block_idx !=
                              clipped_dst_regions[i].block_idx)));

                    RegionTranslate(clipped_src_regions[j].region,
                                    -src_x_off - dx, -src_y_off - dy);
                    current_boxes = RegionRects(clipped_src_regions[j].region);
                    n_current_boxes =
                        RegionNumRects(clipped_src_regions[j].region);
                    DEBUGF("dst pixmap fbo idx %d src pixmap fbo idx %d \n",
                           clipped_dst_regions[i].block_idx,
                           clipped_src_regions[j].block_idx);
                    DEBUGF("Copy %d %d %d %d dx %d dy %d from %p to %p \n",
                           current_boxes[0].x1, current_boxes[0].y1,
                           current_boxes[0].x2, current_boxes[0].y2, dx, dy,
                           src_pixmap, dst_pixmap);
                    if (!temp_source_pixmap)
                        ok = __glamor_copy_n_to_n(src, dst, gc, current_boxes,
                                                  n_current_boxes, dx, dy,
                                                  reverse, upsidedown, bitplane,
                                                  closure);
                    else {
                        ok = __glamor_copy_n_to_n(&temp_source_pixmap->drawable,
                                                  dst, gc, current_boxes,
                                                  n_current_boxes, dx, dy,
                                                  reverse, upsidedown, bitplane,
                                                  closure);
                        temp_source_priv->type = GLAMOR_MEMORY;
                        temp_source_priv->base.fbo = NULL;
                        glamor_destroy_pixmap(temp_source_pixmap);
                        temp_source_pixmap = NULL;
                    }

                    RegionDestroy(clipped_src_regions[j].region);
                    if (!ok) {
                        assert(0);
                        goto fail;
                    }
                }

                if (n_src_region == 0)
                    ok = TRUE;
                free(clipped_src_regions);
            }
            else {
                RegionTranslate(clipped_dst_regions[i].region,
                                -dst_x_off, -dst_y_off);
                current_boxes = RegionRects(clipped_dst_regions[i].region);
                n_current_boxes = RegionNumRects(clipped_dst_regions[i].region);

                DEBUGF("dest pixmap fbo idx %d \n",
                       clipped_dst_regions[i].block_idx);
                DEBUGF("Copy %d %d %d %d dx %d dy %d from %p to %p \n",
                       current_boxes[0].x1, current_boxes[0].y1,
                       current_boxes[0].x2, current_boxes[0].y2,
                       dx, dy, src_pixmap, dst_pixmap);

                ok = __glamor_copy_n_to_n(src, dst, gc, current_boxes,
                                          n_current_boxes, dx, dy, reverse,
                                          upsidedown, bitplane, closure);

            }
            RegionDestroy(clipped_dst_regions[i].region);
        }
        if (n_dst_region == 0)
            ok = TRUE;
        free(clipped_dst_regions);
    }
    else {
        ok = __glamor_copy_n_to_n(src, dst, gc, box, nbox, dx, dy,
                                  reverse, upsidedown, bitplane, closure);
    }

 fail:
    RegionUninit(&region);
 fail_noregion:
    glamor_make_current(glamor_priv);
    glamor_set_alu(screen, GXcopy);

    if (ok)
        return TRUE;
 fall_back:
    if (!fallback && glamor_ddx_fallback_check_pixmap(src)
        && glamor_ddx_fallback_check_pixmap(dst))
        goto done;

    if (src_pixmap_priv->type == GLAMOR_DRM_ONLY
        || dst_pixmap_priv->type == GLAMOR_DRM_ONLY) {
        LogMessage(X_WARNING,
                   "Access a DRM only pixmap is not allowed within glamor.\n");
        return TRUE;
    }
    glamor_report_delayed_fallbacks(src->pScreen);
    glamor_report_delayed_fallbacks(dst->pScreen);

    glamor_fallback("from %p to %p (%c,%c)\n", src, dst,
                    glamor_get_drawable_location(src),
                    glamor_get_drawable_location(dst));

    if (glamor_prepare_access(dst, GLAMOR_ACCESS_RW) &&
        glamor_prepare_access(src, GLAMOR_ACCESS_RO) &&
        glamor_prepare_access_gc(gc)) {
        fbCopyNtoN(src, dst, gc, box, nbox,
                   dx, dy, reverse, upsidedown, bitplane, closure);
    }
    glamor_finish_access_gc(gc);
    glamor_finish_access(src);
    glamor_finish_access(dst);
    ok = TRUE;

 done:
    glamor_clear_delayed_fallbacks(src->pScreen);
    glamor_clear_delayed_fallbacks(dst->pScreen);
    return ok;
}

RegionPtr
glamor_copy_area(DrawablePtr src, DrawablePtr dst, GCPtr gc,
                 int srcx, int srcy, int width, int height, int dstx, int dsty)
{
    RegionPtr region;

    region = miDoCopy(src, dst, gc,
                      srcx, srcy, width, height,
                      dstx, dsty, glamor_copy_n_to_n, 0, NULL);

    return region;
}

void
glamor_copy_n_to_n(DrawablePtr src,
                   DrawablePtr dst,
                   GCPtr gc,
                   BoxPtr box,
                   int nbox,
                   int dx,
                   int dy,
                   Bool reverse, Bool upsidedown, Pixel bitplane, void *closure)
{
    _glamor_copy_n_to_n(src, dst, gc, box, nbox, dx,
                        dy, reverse, upsidedown, bitplane, closure, TRUE);
}

Bool
glamor_copy_n_to_n_nf(DrawablePtr src,
                      DrawablePtr dst,
                      GCPtr gc,
                      BoxPtr box,
                      int nbox,
                      int dx,
                      int dy,
                      Bool reverse,
                      Bool upsidedown, Pixel bitplane, void *closure)
{
    return _glamor_copy_n_to_n(src, dst, gc, box, nbox, dx,
                               dy, reverse, upsidedown, bitplane, closure,
                               FALSE);
}
