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

/** @file glamor_fillspans.c
 *
 * GC fill implementation, based loosely on fb_fill.c
 */
Bool
glamor_fill(DrawablePtr drawable,
            GCPtr gc, int x, int y, int width, int height, Bool fallback)
{
    PixmapPtr dst_pixmap = glamor_get_drawable_pixmap(drawable);
    int off_x, off_y;
    PixmapPtr sub_pixmap = NULL;
    glamor_access_t sub_pixmap_access;
    DrawablePtr saved_drawable = NULL;
    int saved_x = x, saved_y = y;

    glamor_get_drawable_deltas(drawable, dst_pixmap, &off_x, &off_y);

    switch (gc->fillStyle) {
    case FillSolid:
        if (!glamor_solid(dst_pixmap,
                          x + off_x,
                          y + off_y,
                          width, height, gc->alu, gc->planemask, gc->fgPixel))
            goto fail;
        break;
    case FillStippled:
    case FillOpaqueStippled:
        if (!glamor_stipple(dst_pixmap,
                            gc->stipple,
                            x + off_x,
                            y + off_y,
                            width,
                            height,
                            gc->alu,
                            gc->planemask,
                            gc->fgPixel,
                            gc->bgPixel, gc->patOrg.x, gc->patOrg.y))
            goto fail;
        break;
    case FillTiled:
        if (!glamor_tile(dst_pixmap,
                         gc->tile.pixmap,
                         x + off_x,
                         y + off_y,
                         width,
                         height,
                         gc->alu,
                         gc->planemask,
                         x - drawable->x - gc->patOrg.x,
                         y - drawable->y - gc->patOrg.y))
            goto fail;
        break;
    }
    return TRUE;

 fail:
    if (!fallback) {
        if (glamor_ddx_fallback_check_pixmap(&dst_pixmap->drawable)
            && glamor_ddx_fallback_check_gc(gc))
            return FALSE;
    }
    /* Is it possible to set the access as WO? */

    sub_pixmap_access = GLAMOR_ACCESS_RW;

    sub_pixmap = glamor_get_sub_pixmap(dst_pixmap, x + off_x,
                                       y + off_y, width, height,
                                       sub_pixmap_access);

    if (sub_pixmap != NULL) {
        if (gc->fillStyle != FillSolid) {
            gc->patOrg.x += (drawable->x - x);
            gc->patOrg.y += (drawable->y - y);
        }
        saved_drawable = drawable;
        drawable = &sub_pixmap->drawable;
        saved_x = x;
        saved_y = y;
        x = 0;
        y = 0;
    }
    if (glamor_prepare_access(drawable, GLAMOR_ACCESS_RW)) {
        if (glamor_prepare_access_gc(gc)) {
            fbFill(drawable, gc, x, y, width, height);
            glamor_finish_access_gc(gc);
        }
        glamor_finish_access(drawable, GLAMOR_ACCESS_RW);
    }

    if (sub_pixmap != NULL) {
        if (gc->fillStyle != FillSolid) {
            gc->patOrg.x -= (saved_drawable->x - saved_x);
            gc->patOrg.y -= (saved_drawable->y - saved_y);
        }

        x = saved_x;
        y = saved_y;

        glamor_put_sub_pixmap(sub_pixmap, dst_pixmap,
                              x + off_x, y + off_y,
                              width, height, sub_pixmap_access);
    }

    return TRUE;
}

void
glamor_init_solid_shader(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;
    const char *solid_vs =
        "attribute vec4 v_position;"
        "void main()\n"
        "{\n"
        "       gl_Position = v_position;\n"
        "}\n";
    const char *solid_fs =
        GLAMOR_DEFAULT_PRECISION
        "uniform vec4 color;\n"
        "void main()\n"
        "{\n"
        "	gl_FragColor = color;\n"
        "}\n";
    GLint fs_prog, vs_prog;

    glamor_priv = glamor_get_screen_private(screen);
    glamor_get_context(glamor_priv);
    glamor_priv->solid_prog = glCreateProgram();
    vs_prog = glamor_compile_glsl_prog(GL_VERTEX_SHADER, solid_vs);
    fs_prog = glamor_compile_glsl_prog(GL_FRAGMENT_SHADER, solid_fs);
    glAttachShader(glamor_priv->solid_prog, vs_prog);
    glAttachShader(glamor_priv->solid_prog, fs_prog);

    glBindAttribLocation(glamor_priv->solid_prog,
                         GLAMOR_VERTEX_POS, "v_position");
    glamor_link_glsl_prog(glamor_priv->solid_prog);

    glamor_priv->solid_color_uniform_location =
        glGetUniformLocation(glamor_priv->solid_prog, "color");
    glamor_put_context(glamor_priv);
}

void
glamor_fini_solid_shader(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;

    glamor_priv = glamor_get_screen_private(screen);
    glamor_get_context(glamor_priv);
    glDeleteProgram(glamor_priv->solid_prog);
    glamor_put_context(glamor_priv);
}

static void
_glamor_solid_boxes(PixmapPtr pixmap, BoxPtr box, int nbox, float *color)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    GLfloat xscale, yscale;
    float vertices[32];
    float *pvertices = vertices;
    int valid_nbox = ARRAY_SIZE(vertices);

    glamor_set_destination_pixmap_priv_nc(pixmap_priv);

    glamor_get_context(glamor_priv);
    glUseProgram(glamor_priv->solid_prog);

    glUniform4fv(glamor_priv->solid_color_uniform_location, 1, color);

    pixmap_priv_get_dest_scale(pixmap_priv, &xscale, &yscale);

    if (_X_UNLIKELY(nbox * 4 * 2 > ARRAY_SIZE(vertices))) {
        int allocated_box;

        if (nbox * 6 > GLAMOR_COMPOSITE_VBO_VERT_CNT) {
            allocated_box = GLAMOR_COMPOSITE_VBO_VERT_CNT / 6;
        }
        else
            allocated_box = nbox;
        pvertices = malloc(allocated_box * 4 * 2 * sizeof(float));
        if (pvertices)
            valid_nbox = allocated_box;
        else {
            pvertices = vertices;
            valid_nbox = ARRAY_SIZE(vertices) / (4 * 2);
        }
    }

    if (_X_UNLIKELY(nbox > 1))
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glamor_priv->ebo);

    glVertexAttribPointer(GLAMOR_VERTEX_POS, 2, GL_FLOAT,
                          GL_FALSE, 2 * sizeof(float), pvertices);
    glEnableVertexAttribArray(GLAMOR_VERTEX_POS);

    while (nbox) {
        int box_cnt, i;
        float *valid_vertices;

        valid_vertices = pvertices;
        box_cnt = nbox > valid_nbox ? valid_nbox : nbox;
        for (i = 0; i < box_cnt; i++) {
            glamor_set_normalize_vcoords(pixmap_priv, xscale, yscale,
                                         box[i].x1, box[i].y1,
                                         box[i].x2, box[i].y2,
                                         glamor_priv->yInverted,
                                         valid_vertices);
            valid_vertices += 4 * 2;
        }
        if (box_cnt == 1)
            glDrawArrays(GL_TRIANGLE_FAN, 0, box_cnt * 4);
        else {
            if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP) {
                glDrawRangeElements(GL_TRIANGLES, 0, box_cnt * 4, box_cnt * 6,
                                    GL_UNSIGNED_SHORT, NULL);
            } else {
                glDrawElements(GL_TRIANGLES, box_cnt * 6, GL_UNSIGNED_SHORT,
                               NULL);
            }
        }
        nbox -= box_cnt;
        box += box_cnt;
    }

    if (pvertices != vertices)
        free(pvertices);

    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);
    glUseProgram(0);
    glamor_put_context(glamor_priv);
    glamor_priv->state = RENDER_STATE;
    glamor_priv->render_idle_cnt = 0;
}

Bool
glamor_solid_boxes(PixmapPtr pixmap,
                   BoxPtr box, int nbox, unsigned long fg_pixel)
{
    glamor_pixmap_private *pixmap_priv;
    GLfloat color[4];

    pixmap_priv = glamor_get_pixmap_private(pixmap);

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        return FALSE;

    glamor_get_rgba_from_pixel(fg_pixel,
                               &color[0],
                               &color[1],
                               &color[2], &color[3], format_for_pixmap(pixmap));

    if (pixmap_priv->type == GLAMOR_TEXTURE_LARGE) {
        RegionRec region;
        int n_region;
        glamor_pixmap_clipped_regions *clipped_regions;
        int i;

        RegionInitBoxes(&region, box, nbox);
        clipped_regions =
            glamor_compute_clipped_regions(pixmap_priv, &region, &n_region, 0,
                                           0, 0);
        for (i = 0; i < n_region; i++) {
            BoxPtr inner_box;
            int inner_nbox;

            SET_PIXMAP_FBO_CURRENT(pixmap_priv, clipped_regions[i].block_idx);

            inner_box = RegionRects(clipped_regions[i].region);
            inner_nbox = RegionNumRects(clipped_regions[i].region);
            _glamor_solid_boxes(pixmap, inner_box, inner_nbox, color);
            RegionDestroy(clipped_regions[i].region);
        }
        free(clipped_regions);
        RegionUninit(&region);
    }
    else
        _glamor_solid_boxes(pixmap, box, nbox, color);

    return TRUE;
}

Bool
glamor_solid(PixmapPtr pixmap, int x, int y, int width, int height,
             unsigned char alu, unsigned long planemask, unsigned long fg_pixel)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    glamor_pixmap_private *pixmap_priv;
    BoxRec box;

    pixmap_priv = glamor_get_pixmap_private(pixmap);

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
        return FALSE;

    if (!glamor_set_planemask(pixmap, planemask)) {
        glamor_fallback("Failedto set planemask  in glamor_solid.\n");
        return FALSE;
    }

    glamor_get_context(glamor_priv);
    if (!glamor_set_alu(screen, alu)) {
        if (alu == GXclear)
            fg_pixel = 0;
        else {
            glamor_fallback("unsupported alu %x\n", alu);
            glamor_put_context(glamor_priv);
            return FALSE;
        }
    }
    box.x1 = x;
    box.y1 = y;
    box.x2 = x + width;
    box.y2 = y + height;
    glamor_solid_boxes(pixmap, &box, 1, fg_pixel);

    glamor_set_alu(screen, GXcopy);
    glamor_put_context(glamor_priv);

    return TRUE;
}
