/*
 * Copyright © 2001 Keith Packard
 * Copyright © 2008 Intel Corporation
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

/** @file glamor_core.c
 *
 * This file covers core X rendering in glamor.
 */

#include <stdlib.h>

#include "glamor_priv.h"

Bool
glamor_get_drawable_location(const DrawablePtr drawable)
{
    PixmapPtr pixmap = glamor_get_drawable_pixmap(drawable);
    glamor_pixmap_private *pixmap_priv = glamor_get_pixmap_private(pixmap);
    glamor_screen_private *glamor_priv =
        glamor_get_screen_private(drawable->pScreen);
    if (pixmap_priv->gl_fbo == GLAMOR_FBO_UNATTACHED)
        return 'm';
    if (pixmap_priv->fbo->fb == glamor_priv->screen_fbo)
        return 's';
    else
        return 'f';
}

GLint
glamor_compile_glsl_prog(GLenum type, const char *source)
{
    GLint ok;
    GLint prog;

    prog = glCreateShader(type);
    glShaderSource(prog, 1, (const GLchar **) &source, NULL);
    glCompileShader(prog);
    glGetShaderiv(prog, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLchar *info;
        GLint size;

        glGetShaderiv(prog, GL_INFO_LOG_LENGTH, &size);
        info = malloc(size);
        if (info) {
            glGetShaderInfoLog(prog, size, NULL, info);
            ErrorF("Failed to compile %s: %s\n",
                   type == GL_FRAGMENT_SHADER ? "FS" : "VS", info);
            ErrorF("Program source:\n%s", source);
            free(info);
        }
        else
            ErrorF("Failed to get shader compilation info.\n");
        FatalError("GLSL compile failure\n");
    }

    return prog;
}

void
glamor_link_glsl_prog(ScreenPtr screen, GLint prog, const char *format, ...)
{
    GLint ok;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);

    if (glamor_priv->has_khr_debug) {
        char *label;
        va_list va;

        va_start(va, format);
        XNFvasprintf(&label, format, va);
        glObjectLabel(GL_PROGRAM, prog, -1, label);
        free(label);
        va_end(va);
    }

    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLchar *info;
        GLint size;

        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &size);
        info = malloc(size);

        glGetProgramInfoLog(prog, size, NULL, info);
        ErrorF("Failed to link: %s\n", info);
        FatalError("GLSL link failure\n");
    }
}

/*
 *  When downloading a unsupported color format to CPU memory,
    we need to shuffle the color elements and then use a supported
    color format to read it back to CPU memory.

    For an example, the picture's format is PICT_b8g8r8a8,
    Then the expecting color layout is as below (little endian):
    0	1	2	3   : address
    a	r	g	b

    Now the in GLES2 the supported color format is GL_RGBA, type is
    GL_UNSIGNED_TYPE, then we need to shuffle the fragment
    color as :
	frag_color = sample(texture).argb;
    before we use glReadPixel to get it back.

    For the uploading process, the shuffle is a revert shuffle.
    We still use GL_RGBA, GL_UNSIGNED_BYTE to upload the color
    to a texture, then let's see
    0	1	2	3   : address
    a	r	g	b   : correct colors
    R	G	B	A   : GL_RGBA with GL_UNSIGNED_BYTE

    Now we need to shuffle again, the mapping rule is
    r = G, g = B, b = A, a = R. Then the uploading shuffle is as
    below:
	frag_color = sample(texture).gbar;
*/

void
glamor_init_finish_access_shaders(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;
    const char *vs_source =
        "attribute vec4 v_position;\n"
        "attribute vec4 v_texcoord0;\n"
        "varying vec2 source_texture;\n"
        "void main()\n"
        "{\n"
        "	gl_Position = v_position;\n"
        "	source_texture = v_texcoord0.xy;\n"
        "}\n";

    const char *common_source =
        GLAMOR_DEFAULT_PRECISION
        "varying vec2 source_texture;\n"
        "uniform sampler2D sampler;\n"
        "uniform int revert;\n"
        "uniform int swap_rb;\n"
        "#define REVERT_NONE       			0\n"
        "#define REVERT_NORMAL     			1\n"
        "#define SWAP_NONE_DOWNLOADING  		0\n"
        "#define SWAP_DOWNLOADING  			1\n"
        "#define SWAP_UPLOADING	  		2\n"
        "#define SWAP_NONE_UPLOADING		3\n";

    const char *fs_source =
        "void main()\n"
        "{\n"
        "   vec4 color = texture2D(sampler, source_texture);\n"
        "   if (revert == REVERT_NONE) \n"
        "    { \n"
        "     if ((swap_rb != SWAP_NONE_DOWNLOADING) && (swap_rb != SWAP_NONE_UPLOADING))   \n"
        "		gl_FragColor = color.bgra;\n"
        "     else \n"
        "		gl_FragColor = color.rgba;\n"
        "    } \n"
        "   else \n"
        "    { \n"
        "     if (swap_rb == SWAP_DOWNLOADING)   \n"
        "		gl_FragColor = color.argb;\n"
        "     else if (swap_rb == SWAP_NONE_DOWNLOADING)\n"
        "		gl_FragColor = color.abgr;\n"
        "     else if (swap_rb == SWAP_UPLOADING)\n"
        "		gl_FragColor = color.gbar;\n"
        "     else if (swap_rb == SWAP_NONE_UPLOADING)\n"
        "		gl_FragColor = color.abgr;\n"
        "    } \n"
        "}\n";

    const char *set_alpha_source =
        "void main()\n"
        "{\n"
        "   vec4 color = texture2D(sampler, source_texture);\n"
        "   if (revert == REVERT_NONE) \n"
        "    { \n"
        "     if ((swap_rb != SWAP_NONE_DOWNLOADING) && (swap_rb != SWAP_NONE_UPLOADING))   \n"
        "		gl_FragColor = vec4(color.bgr, 1);\n"
        "     else \n"
        "		gl_FragColor = vec4(color.rgb, 1);\n"
        "    } \n"
        "   else \n"
        "    { \n"
        "     if (swap_rb == SWAP_DOWNLOADING)   \n"
        "		gl_FragColor = vec4(1, color.rgb);\n"
        "     else if (swap_rb == SWAP_NONE_DOWNLOADING)\n"
        "		gl_FragColor = vec4(1, color.bgr);\n"
        "     else if (swap_rb == SWAP_UPLOADING)\n"
        "		gl_FragColor = vec4(color.gba, 1);\n"
        "     else if (swap_rb == SWAP_NONE_UPLOADING)\n"
        "		gl_FragColor = vec4(color.abg, 1);\n"
        "    } \n"
        "}\n";
    GLint fs_prog, vs_prog, avs_prog, set_alpha_prog;
    GLint sampler_uniform_location;
    char *source;

    glamor_priv = glamor_get_screen_private(screen);
    glamor_make_current(glamor_priv);
    glamor_priv->finish_access_prog[0] = glCreateProgram();
    glamor_priv->finish_access_prog[1] = glCreateProgram();

    vs_prog = glamor_compile_glsl_prog(GL_VERTEX_SHADER, vs_source);

    XNFasprintf(&source, "%s%s", common_source, fs_source);
    fs_prog = glamor_compile_glsl_prog(GL_FRAGMENT_SHADER, source);
    free(source);

    glAttachShader(glamor_priv->finish_access_prog[0], vs_prog);
    glAttachShader(glamor_priv->finish_access_prog[0], fs_prog);

    avs_prog = glamor_compile_glsl_prog(GL_VERTEX_SHADER, vs_source);

    XNFasprintf(&source, "%s%s", common_source, set_alpha_source);
    set_alpha_prog = glamor_compile_glsl_prog(GL_FRAGMENT_SHADER,
                                              source);
    free(source);

    glAttachShader(glamor_priv->finish_access_prog[1], avs_prog);
    glAttachShader(glamor_priv->finish_access_prog[1], set_alpha_prog);

    glBindAttribLocation(glamor_priv->finish_access_prog[0],
                         GLAMOR_VERTEX_POS, "v_position");
    glBindAttribLocation(glamor_priv->finish_access_prog[0],
                         GLAMOR_VERTEX_SOURCE, "v_texcoord0");
    glamor_link_glsl_prog(screen, glamor_priv->finish_access_prog[0],
                          "finish access 0");

    glBindAttribLocation(glamor_priv->finish_access_prog[1],
                         GLAMOR_VERTEX_POS, "v_position");
    glBindAttribLocation(glamor_priv->finish_access_prog[1],
                         GLAMOR_VERTEX_SOURCE, "v_texcoord0");
    glamor_link_glsl_prog(screen, glamor_priv->finish_access_prog[1],
                          "finish access 1");

    glamor_priv->finish_access_revert[0] =
        glGetUniformLocation(glamor_priv->finish_access_prog[0], "revert");

    glamor_priv->finish_access_swap_rb[0] =
        glGetUniformLocation(glamor_priv->finish_access_prog[0], "swap_rb");
    sampler_uniform_location =
        glGetUniformLocation(glamor_priv->finish_access_prog[0], "sampler");
    glUseProgram(glamor_priv->finish_access_prog[0]);
    glUniform1i(sampler_uniform_location, 0);
    glUniform1i(glamor_priv->finish_access_revert[0], 0);
    glUniform1i(glamor_priv->finish_access_swap_rb[0], 0);

    glamor_priv->finish_access_revert[1] =
        glGetUniformLocation(glamor_priv->finish_access_prog[1], "revert");
    glamor_priv->finish_access_swap_rb[1] =
        glGetUniformLocation(glamor_priv->finish_access_prog[1], "swap_rb");
    sampler_uniform_location =
        glGetUniformLocation(glamor_priv->finish_access_prog[1], "sampler");
    glUseProgram(glamor_priv->finish_access_prog[1]);
    glUniform1i(glamor_priv->finish_access_revert[1], 0);
    glUniform1i(sampler_uniform_location, 0);
    glUniform1i(glamor_priv->finish_access_swap_rb[1], 0);
}

static GCOps glamor_gc_ops = {
    .FillSpans = glamor_fill_spans,
    .SetSpans = glamor_set_spans,
    .PutImage = glamor_put_image,
    .CopyArea = glamor_copy_area,
    .CopyPlane = glamor_copy_plane,
    .PolyPoint = glamor_poly_point,
    .Polylines = glamor_poly_lines,
    .PolySegment = glamor_poly_segment,
    .PolyRectangle = miPolyRectangle,
    .PolyArc = miPolyArc,
    .FillPolygon = miFillPolygon,
    .PolyFillRect = glamor_poly_fill_rect,
    .PolyFillArc = miPolyFillArc,
    .PolyText8 = glamor_poly_text8,
    .PolyText16 = glamor_poly_text16,
    .ImageText8 = glamor_image_text8,
    .ImageText16 = glamor_image_text16,
    .ImageGlyphBlt = miImageGlyphBlt,
    .PolyGlyphBlt = glamor_poly_glyph_blt,
    .PushPixels = glamor_push_pixels,
};

/*
 * When the stipple is changed or drawn to, invalidate any
 * cached copy
 */
static void
glamor_invalidate_stipple(GCPtr gc)
{
    glamor_gc_private *gc_priv = glamor_get_gc_private(gc);

    if (gc_priv->stipple) {
        if (gc_priv->stipple_damage)
            DamageUnregister(gc_priv->stipple_damage);
        glamor_destroy_pixmap(gc_priv->stipple);
        gc_priv->stipple = NULL;
    }
}

static void
glamor_stipple_damage_report(DamagePtr damage, RegionPtr region,
                             void *closure)
{
    GCPtr       gc = closure;

    glamor_invalidate_stipple(gc);
}

static void
glamor_stipple_damage_destroy(DamagePtr damage, void *closure)
{
    GCPtr               gc = closure;
    glamor_gc_private   *gc_priv = glamor_get_gc_private(gc);

    gc_priv->stipple_damage = NULL;
    glamor_invalidate_stipple(gc);
}

void
glamor_track_stipple(GCPtr gc)
{
    if (gc->stipple) {
        glamor_gc_private *gc_priv = glamor_get_gc_private(gc);

        if (!gc_priv->stipple_damage)
            gc_priv->stipple_damage = DamageCreate(glamor_stipple_damage_report,
                                                   glamor_stipple_damage_destroy,
                                                   DamageReportNonEmpty,
                                                   TRUE, gc->pScreen, gc);
        if (gc_priv->stipple_damage)
            DamageRegister(&gc->stipple->drawable, gc_priv->stipple_damage);
    }
}

/**
 * uxa_validate_gc() sets the ops to glamor's implementations, which may be
 * accelerated or may sync the card and fall back to fb.
 */
void
glamor_validate_gc(GCPtr gc, unsigned long changes, DrawablePtr drawable)
{
    /* fbValidateGC will do direct access to pixmaps if the tiling has changed.
     * Preempt fbValidateGC by doing its work and masking the change out, so
     * that we can do the Prepare/finish_access.
     */
#ifdef FB_24_32BIT
    if ((changes & GCTile) && fbGetRotatedPixmap(gc)) {
        gc->pScreen->DestroyPixmap(fbGetRotatedPixmap(gc));
        fbGetRotatedPixmap(gc) = 0;
    }

    if (gc->fillStyle == FillTiled) {
        PixmapPtr old_tile, new_tile;

        old_tile = gc->tile.pixmap;
        if (old_tile->drawable.bitsPerPixel != drawable->bitsPerPixel) {
            new_tile = fbGetRotatedPixmap(gc);
            if (!new_tile ||
                new_tile->drawable.bitsPerPixel != drawable->bitsPerPixel) {
                if (new_tile)
                    gc->pScreen->DestroyPixmap(new_tile);
                /* fb24_32ReformatTile will do direct access of a newly-
                 * allocated pixmap.
                 */
                glamor_fallback
                    ("GC %p tile FB_24_32 transformat %p.\n", gc, old_tile);

                if (glamor_prepare_access
                    (&old_tile->drawable, GLAMOR_ACCESS_RO)) {
                    new_tile =
                        fb24_32ReformatTile(old_tile, drawable->bitsPerPixel);
                    glamor_finish_access(&old_tile->drawable);
                }
            }
            if (new_tile) {
                fbGetRotatedPixmap(gc) = old_tile;
                gc->tile.pixmap = new_tile;
                changes |= GCTile;
            }
        }
    }
#endif
    if (changes & GCTile) {
        if (!gc->tileIsPixel) {
            glamor_pixmap_private *pixmap_priv =
                glamor_get_pixmap_private(gc->tile.pixmap);
            if ((!GLAMOR_PIXMAP_PRIV_HAS_FBO(pixmap_priv))
                && FbEvenTile(gc->tile.pixmap->drawable.width *
                              drawable->bitsPerPixel)) {
                glamor_fallback
                    ("GC %p tile changed %p.\n", gc, gc->tile.pixmap);
                if (glamor_prepare_access
                    (&gc->tile.pixmap->drawable, GLAMOR_ACCESS_RW)) {
                    fbPadPixmap(gc->tile.pixmap);
                    glamor_finish_access(&gc->tile.pixmap->drawable);
                }
            }
        }
        /* Mask out the GCTile change notification, now that we've done FB's
         * job for it.
         */
        changes &= ~GCTile;
    }

    if (changes & GCStipple)
        glamor_invalidate_stipple(gc);

    if (changes & GCStipple && gc->stipple) {
        /* We can't inline stipple handling like we do for GCTile because
         * it sets fbgc privates.
         */
        if (glamor_prepare_access(&gc->stipple->drawable, GLAMOR_ACCESS_RW)) {
            fbValidateGC(gc, changes, drawable);
            glamor_finish_access(&gc->stipple->drawable);
        }
    }
    else {
        fbValidateGC(gc, changes, drawable);
    }

    if (changes & GCDashList) {
        glamor_gc_private *gc_priv = glamor_get_gc_private(gc);

        if (gc_priv->dash) {
            glamor_destroy_pixmap(gc_priv->dash);
            gc_priv->dash = NULL;
        }
    }

    gc->ops = &glamor_gc_ops;
}

void
glamor_destroy_gc(GCPtr gc)
{
    glamor_gc_private *gc_priv = glamor_get_gc_private(gc);

    if (gc_priv->dash) {
        glamor_destroy_pixmap(gc_priv->dash);
        gc_priv->dash = NULL;
    }
    glamor_invalidate_stipple(gc);
    if (gc_priv->stipple_damage)
        DamageDestroy(gc_priv->stipple_damage);
    miDestroyGC(gc);
}

static GCFuncs glamor_gc_funcs = {
    glamor_validate_gc,
    miChangeGC,
    miCopyGC,
    glamor_destroy_gc,
    miChangeClip,
    miDestroyClip,
    miCopyClip
};

/**
 * exaCreateGC makes a new GC and hooks up its funcs handler, so that
 * exaValidateGC() will get called.
 */
int
glamor_create_gc(GCPtr gc)
{
    glamor_gc_private *gc_priv = glamor_get_gc_private(gc);

    gc_priv->dash = NULL;
    gc_priv->stipple = NULL;
    if (!fbCreateGC(gc))
        return FALSE;

    gc->funcs = &glamor_gc_funcs;

    return TRUE;
}

RegionPtr
glamor_bitmap_to_region(PixmapPtr pixmap)
{
    RegionPtr ret;

    glamor_fallback("pixmap %p \n", pixmap);
    if (!glamor_prepare_access(&pixmap->drawable, GLAMOR_ACCESS_RO))
        return NULL;
    ret = fbPixmapToRegion(pixmap);
    glamor_finish_access(&pixmap->drawable);
    return ret;
}

