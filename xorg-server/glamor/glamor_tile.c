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

/** @file glamor_tile.c
 *
 * Implements the basic fill-with-a-tile support used by multiple GC ops.
 */

void
glamor_init_tile_shader(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;
    const char *tile_vs =
        "attribute vec4 v_position;\n"
        "attribute vec4 v_texcoord0;\n"
        "varying vec2 tile_texture;\n"
        "void main()\n"
        "{\n"
        "       gl_Position = v_position;\n"
        "       tile_texture = v_texcoord0.xy;\n"
        "}\n";
    const char *tile_fs =
        GLAMOR_DEFAULT_PRECISION
        "varying vec2 tile_texture;\n"
        "uniform sampler2D sampler;\n"
        "uniform vec2	wh;"
        "void main()\n"
        "{\n"
        "   vec2 rel_tex;"
        "   rel_tex = tile_texture * wh; \n"
        "   rel_tex = floor(rel_tex) + (fract(rel_tex) / wh); \n"
        "	gl_FragColor = texture2D(sampler, rel_tex);\n"
        "}\n";
    GLint fs_prog, vs_prog;
    GLint sampler_uniform_location;

    glamor_priv = glamor_get_screen_private(screen);
    glamor_make_current(glamor_priv);
    glamor_priv->tile_prog = glCreateProgram();
    vs_prog = glamor_compile_glsl_prog(GL_VERTEX_SHADER, tile_vs);
    fs_prog = glamor_compile_glsl_prog(GL_FRAGMENT_SHADER, tile_fs);
    glAttachShader(glamor_priv->tile_prog, vs_prog);
    glAttachShader(glamor_priv->tile_prog, fs_prog);

    glBindAttribLocation(glamor_priv->tile_prog,
                         GLAMOR_VERTEX_POS, "v_position");
    glBindAttribLocation(glamor_priv->tile_prog,
                         GLAMOR_VERTEX_SOURCE, "v_texcoord0");
    glamor_link_glsl_prog(screen, glamor_priv->tile_prog, "tile");

    sampler_uniform_location =
        glGetUniformLocation(glamor_priv->tile_prog, "sampler");
    glUseProgram(glamor_priv->tile_prog);
    glUniform1i(sampler_uniform_location, 0);

    glamor_priv->tile_wh =
        glGetUniformLocation(glamor_priv->tile_prog, "wh");
}

void
glamor_fini_tile_shader(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;

    glamor_priv = glamor_get_screen_private(screen);
    glamor_make_current(glamor_priv);
    glDeleteProgram(glamor_priv->tile_prog);
}

static void
_glamor_tile(PixmapPtr pixmap, PixmapPtr tile,
             int x, int y, int width, int height, int tile_x, int tile_y)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    int x1 = x;
    int x2 = x + width;
    int y1 = y;
    int y2 = y + height;
    int tile_x1 = tile_x;
    int tile_x2 = tile_x + width;
    int tile_y1 = tile_y;
    int tile_y2 = tile_y + height;
    float vertices[8];
    float source_texcoords[8];
    GLfloat dst_xscale, dst_yscale, src_xscale, src_yscale;
    glamor_pixmap_private *src_pixmap_priv;
    glamor_pixmap_private *dst_pixmap_priv;
    float wh[4];

    src_pixmap_priv = glamor_get_pixmap_private(tile);
    dst_pixmap_priv = glamor_get_pixmap_private(pixmap);

    glamor_set_destination_pixmap_priv_nc(dst_pixmap_priv);
    pixmap_priv_get_dest_scale(dst_pixmap_priv, &dst_xscale, &dst_yscale);
    pixmap_priv_get_scale(src_pixmap_priv, &src_xscale, &src_yscale);
    glamor_make_current(glamor_priv);
    glUseProgram(glamor_priv->tile_prog);

    glamor_pixmap_fbo_fix_wh_ratio(wh, src_pixmap_priv);
    glUniform2fv(glamor_priv->tile_wh, 1, wh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_pixmap_priv->base.fbo->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glamor_set_repeat_normalize_tcoords
        (src_pixmap_priv, RepeatNormal,
         src_xscale, src_yscale,
         tile_x1, tile_y1,
         tile_x2, tile_y2, glamor_priv->yInverted, source_texcoords);

    glVertexAttribPointer(GLAMOR_VERTEX_SOURCE, 2, GL_FLOAT, GL_FALSE,
                          2 * sizeof(float), source_texcoords);
    glEnableVertexAttribArray(GLAMOR_VERTEX_SOURCE);

    glamor_set_normalize_vcoords(dst_pixmap_priv, dst_xscale, dst_yscale,
                                 x1, y1,
                                 x2, y2, glamor_priv->yInverted, vertices);

    glVertexAttribPointer(GLAMOR_VERTEX_POS, 2, GL_FLOAT, GL_FALSE,
                          2 * sizeof(float), vertices);
    glEnableVertexAttribArray(GLAMOR_VERTEX_POS);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);

    glamor_priv->state = RENDER_STATE;
    glamor_priv->render_idle_cnt = 0;
}

Bool
glamor_tile(PixmapPtr pixmap, PixmapPtr tile,
            int x, int y, int width, int height,
            unsigned char alu, unsigned long planemask, int tile_x, int tile_y)
{
    ScreenPtr screen = pixmap->drawable.pScreen;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    glamor_pixmap_private *dst_pixmap_priv;
    glamor_pixmap_private *src_pixmap_priv;

    dst_pixmap_priv = glamor_get_pixmap_private(pixmap);
    src_pixmap_priv = glamor_get_pixmap_private(tile);

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(dst_pixmap_priv))
        return FALSE;

    if (glamor_priv->tile_prog == 0) {
        glamor_fallback("Tiling unsupported\n");
        goto fail;
    }

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(src_pixmap_priv)) {
        /* XXX dynamic uploading candidate. */
        glamor_fallback("Non-texture tile pixmap\n");
        goto fail;
    }

    if (!glamor_set_planemask(pixmap, planemask)) {
        glamor_fallback("unsupported planemask %lx\n", planemask);
        goto fail;
    }

    glamor_make_current(glamor_priv);
    if (!glamor_set_alu(screen, alu)) {
        glamor_fallback("unsupported alu %x\n", alu);
        goto fail;
    }

    if (dst_pixmap_priv->type == GLAMOR_TEXTURE_LARGE
        || src_pixmap_priv->type == GLAMOR_TEXTURE_LARGE) {
        glamor_pixmap_clipped_regions *clipped_dst_regions;
        int n_dst_region, i, j, k;
        BoxRec box;
        RegionRec region;

        box.x1 = x;
        box.y1 = y;
        box.x2 = x + width;
        box.y2 = y + height;
        RegionInitBoxes(&region, &box, 1);
        clipped_dst_regions = glamor_compute_clipped_regions(dst_pixmap_priv,
                                                             &region,
                                                             &n_dst_region, 0,
                                                             0, 0);
        for (i = 0; i < n_dst_region; i++) {
            int n_src_region;
            glamor_pixmap_clipped_regions *clipped_src_regions;
            BoxPtr current_boxes;
            int n_current_boxes;

            SET_PIXMAP_FBO_CURRENT(dst_pixmap_priv,
                                   clipped_dst_regions[i].block_idx);

            if (src_pixmap_priv->type == GLAMOR_TEXTURE_LARGE) {
                RegionTranslate(clipped_dst_regions[i].region,
                                tile_x - x, tile_y - y);
                DEBUGF("tiled a large src pixmap. %dx%d \n",
                       tile->drawable.width, tile->drawable.height);
                clipped_src_regions =
                    glamor_compute_clipped_regions(src_pixmap_priv,
                                                   clipped_dst_regions[i].
                                                   region, &n_src_region, 1, 0,
                                                   0);
                DEBUGF("got %d src regions %d \n", n_src_region);
                for (j = 0; j < n_src_region; j++) {

                    SET_PIXMAP_FBO_CURRENT(src_pixmap_priv,
                                           clipped_src_regions[j].block_idx);

                    RegionTranslate(clipped_src_regions[j].region,
                                    x - tile_x, y - tile_y);
                    current_boxes = RegionRects(clipped_src_regions[j].region);
                    n_current_boxes =
                        RegionNumRects(clipped_src_regions[j].region);
                    for (k = 0; k < n_current_boxes; k++) {
                        DEBUGF
                            ("Tile on %d %d %d %d dst block id %d tile block id %d tilex %d tiley %d\n",
                             current_boxes[k].x1, current_boxes[k].y1,
                             current_boxes[k].x2 - current_boxes[k].x1,
                             current_boxes[k].y2 - current_boxes[k].y1,
                             clipped_dst_regions[i].block_idx,
                             clipped_src_regions[j].block_idx,
                             (tile_x + (current_boxes[k].x1 - x)),
                             tile_y + (current_boxes[k].y1 - y));

                        _glamor_tile(pixmap, tile,
                                     current_boxes[k].x1, current_boxes[k].y1,
                                     current_boxes[k].x2 - current_boxes[k].x1,
                                     current_boxes[k].y2 - current_boxes[k].y1,
                                     (tile_x + (current_boxes[k].x1 - x)),
                                     (tile_y + (current_boxes[k].y1 - y)));
                    }

                    RegionDestroy(clipped_src_regions[j].region);
                }
                free(clipped_src_regions);
            }
            else {
                current_boxes = RegionRects(clipped_dst_regions[i].region);
                n_current_boxes = RegionNumRects(clipped_dst_regions[i].region);
                for (k = 0; k < n_current_boxes; k++) {
                    _glamor_tile(pixmap, tile,
                                 current_boxes[k].x1, current_boxes[k].y1,
                                 current_boxes[k].x2 - current_boxes[k].x1,
                                 current_boxes[k].y2 - current_boxes[k].y1,
                                 (tile_x + (current_boxes[k].x1 - x)),
                                 (tile_y + (current_boxes[k].y1 - y)));
                }
            }
            RegionDestroy(clipped_dst_regions[i].region);
        }
        free(clipped_dst_regions);
        RegionUninit(&region);
    }
    else
        _glamor_tile(pixmap, tile, x, y, width, height, tile_x, tile_y);

    glamor_set_alu(screen, GXcopy);
    return TRUE;
 fail:
    return FALSE;

}
