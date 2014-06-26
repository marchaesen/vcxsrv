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
 *    Junyan He <junyan.he@linux.intel.com>
 *
 */

/** @file glamor_render.c
 *
 * Render acceleration implementation
 */

#include "glamor_priv.h"

#ifdef RENDER
#include "mipict.h"
#include "fbpict.h"
#if 0
//#define DEBUGF(str, ...)  do {} while(0)
#define DEBUGF(str, ...) ErrorF(str, ##__VA_ARGS__)
//#define DEBUGRegionPrint(x) do {} while (0)
#define DEBUGRegionPrint RegionPrint
#endif

static struct blendinfo composite_op_info[] = {
    [PictOpClear] = {0, 0, GL_ZERO, GL_ZERO},
    [PictOpSrc] = {0, 0, GL_ONE, GL_ZERO},
    [PictOpDst] = {0, 0, GL_ZERO, GL_ONE},
    [PictOpOver] = {0, 1, GL_ONE, GL_ONE_MINUS_SRC_ALPHA},
    [PictOpOverReverse] = {1, 0, GL_ONE_MINUS_DST_ALPHA, GL_ONE},
    [PictOpIn] = {1, 0, GL_DST_ALPHA, GL_ZERO},
    [PictOpInReverse] = {0, 1, GL_ZERO, GL_SRC_ALPHA},
    [PictOpOut] = {1, 0, GL_ONE_MINUS_DST_ALPHA, GL_ZERO},
    [PictOpOutReverse] = {0, 1, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA},
    [PictOpAtop] = {1, 1, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA},
    [PictOpAtopReverse] = {1, 1, GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA},
    [PictOpXor] = {1, 1, GL_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA},
    [PictOpAdd] = {0, 0, GL_ONE, GL_ONE},
};

#define RepeatFix			10
static GLuint
glamor_create_composite_fs(struct shader_key *key)
{
    const char *repeat_define =
        "#define RepeatNone               	      0\n"
        "#define RepeatNormal                     1\n"
        "#define RepeatPad                        2\n"
        "#define RepeatReflect                    3\n"
        "#define RepeatFix		      	      10\n"
        "uniform int 			source_repeat_mode;\n"
        "uniform int 			mask_repeat_mode;\n";
    const char *relocate_texture =
        GLAMOR_DEFAULT_PRECISION
        "vec2 rel_tex_coord(vec2 texture, vec4 wh, int repeat) \n"
        "{\n"
        "   vec2 rel_tex; \n"
        "   rel_tex = texture * wh.xy; \n"
        "	if (repeat == RepeatNone)\n"
        "		return rel_tex; \n"
        "   else if (repeat == RepeatNormal) \n"
        "   	rel_tex = floor(rel_tex) + (fract(rel_tex) / wh.xy); 	\n"
        "   else if(repeat == RepeatPad) { \n"
        "           if (rel_tex.x >= 1.0) rel_tex.x = 1.0 - wh.z * wh.x / 2.;  	\n"
        "		else if(rel_tex.x < 0.0) rel_tex.x = 0.0;	  	\n"
        "           if (rel_tex.y >= 1.0) rel_tex.y = 1.0 - wh.w * wh.y / 2.;	\n"
        "		else if(rel_tex.y < 0.0) rel_tex.y = 0.0;	\n"
        "   	rel_tex = rel_tex / wh.xy; \n"
        "    } \n"
        "   else if(repeat == RepeatReflect) {\n"
        "		if ((1.0 - mod(abs(floor(rel_tex.x)), 2.0)) < 0.001)\n"
        "			rel_tex.x = 2.0 - (1.0 - fract(rel_tex.x))/wh.x;\n"
        "		else \n"
        "			rel_tex.x = fract(rel_tex.x)/wh.x;\n"
        "		if ((1.0 - mod(abs(floor(rel_tex.y)), 2.0)) < 0.001)\n"
        "			rel_tex.y = 2.0 - (1.0 - fract(rel_tex.y))/wh.y;\n"
        "		else \n"
        "			rel_tex.y = fract(rel_tex.y)/wh.y;\n"
        "    } \n"
        "   return rel_tex; \n"
        "}\n";
    /* The texture and the pixmap size is not match eaxctly, so can't sample it directly.
     * rel_sampler will recalculate the texture coords.*/
    const char *rel_sampler =
        " vec4 rel_sampler(sampler2D tex_image, vec2 tex, vec4 wh, int repeat, int set_alpha)\n"
        "{\n"
        "	tex = rel_tex_coord(tex, wh, repeat - RepeatFix);\n"
        "   if (repeat == RepeatFix) {\n"
        "		if (!(tex.x >= 0.0 && tex.x < 1.0 \n"
        "		    && tex.y >= 0.0 && tex.y < 1.0))\n"
        "			return vec4(0.0, 0.0, 0.0, set_alpha);\n"
        "		tex = (fract(tex) / wh.xy);\n"
        "	}\n"
        "	if (set_alpha != 1)\n"
        "		return texture2D(tex_image, tex);\n"
        "	else\n"
        "		return vec4(texture2D(tex_image, tex).rgb, 1.0);\n"
        "}\n";

    const char *source_solid_fetch =
        GLAMOR_DEFAULT_PRECISION
        "uniform vec4 source;\n"
        "vec4 get_source()\n"
        "{\n"
        "	return source;\n"
        "}\n";
    const char *source_alpha_pixmap_fetch =
        GLAMOR_DEFAULT_PRECISION
        "varying vec2 source_texture;\n"
        "uniform sampler2D source_sampler;\n"
        "uniform vec4 source_wh;"
        "vec4 get_source()\n"
        "{\n"
        "   if (source_repeat_mode < RepeatFix)\n"
        "		return texture2D(source_sampler, source_texture);\n"
        "   else \n"
        "		return rel_sampler(source_sampler, source_texture,\n"
        "				   source_wh, source_repeat_mode, 0);\n"
        "}\n";
    const char *source_pixmap_fetch =
        GLAMOR_DEFAULT_PRECISION
        "varying vec2 source_texture;\n"
        "uniform sampler2D source_sampler;\n"
        "uniform vec4 source_wh;\n"
        "vec4 get_source()\n"
        "{\n"
        "   if (source_repeat_mode < RepeatFix) \n"
        "		return vec4(texture2D(source_sampler, source_texture).rgb, 1);\n"
        "	else \n"
        "		return rel_sampler(source_sampler, source_texture,\n"
        "				   source_wh, source_repeat_mode, 1);\n"
        "}\n";
    const char *mask_solid_fetch =
        GLAMOR_DEFAULT_PRECISION
        "uniform vec4 mask;\n"
        "vec4 get_mask()\n"
        "{\n"
        "	return mask;\n"
        "}\n";
    const char *mask_alpha_pixmap_fetch =
        GLAMOR_DEFAULT_PRECISION
        "varying vec2 mask_texture;\n"
        "uniform sampler2D mask_sampler;\n"
        "uniform vec4 mask_wh;\n"
        "vec4 get_mask()\n"
        "{\n"
        "   if (mask_repeat_mode < RepeatFix) \n"
        "		return texture2D(mask_sampler, mask_texture);\n"
        "   else \n"
        "		return rel_sampler(mask_sampler, mask_texture,\n"
        "				   mask_wh, mask_repeat_mode, 0);\n"
        "}\n";
    const char *mask_pixmap_fetch =
        GLAMOR_DEFAULT_PRECISION
        "varying vec2 mask_texture;\n"
        "uniform sampler2D mask_sampler;\n"
        "uniform vec4 mask_wh;\n"
        "vec4 get_mask()\n"
        "{\n"
        "   if (mask_repeat_mode < RepeatFix) \n"
        "   	return vec4(texture2D(mask_sampler, mask_texture).rgb, 1);\n"
        "   else \n"
        "		return rel_sampler(mask_sampler, mask_texture,\n"
        "				   mask_wh, mask_repeat_mode, 1);\n"
        "}\n";
    const char *in_source_only =
        GLAMOR_DEFAULT_PRECISION
        "void main()\n"
        "{\n"
        "	gl_FragColor = get_source();\n"
        "}\n";
    const char *in_normal =
        GLAMOR_DEFAULT_PRECISION
        "void main()\n"
        "{\n"
        "	gl_FragColor = get_source() * get_mask().a;\n"
        "}\n";
    const char *in_ca_source =
        GLAMOR_DEFAULT_PRECISION
        "void main()\n"
        "{\n"
        "	gl_FragColor = get_source() * get_mask();\n"
        "}\n";
    const char *in_ca_alpha =
        GLAMOR_DEFAULT_PRECISION
        "void main()\n"
        "{\n"
        "	gl_FragColor = get_source().a * get_mask();\n"
        "}\n";
    char *source;
    const char *source_fetch;
    const char *mask_fetch = "";
    const char *in;
    GLuint prog;

    switch (key->source) {
    case SHADER_SOURCE_SOLID:
        source_fetch = source_solid_fetch;
        break;
    case SHADER_SOURCE_TEXTURE_ALPHA:
        source_fetch = source_alpha_pixmap_fetch;
        break;
    case SHADER_SOURCE_TEXTURE:
        source_fetch = source_pixmap_fetch;
        break;
    default:
        FatalError("Bad composite shader source");
    }

    switch (key->mask) {
    case SHADER_MASK_NONE:
        break;
    case SHADER_MASK_SOLID:
        mask_fetch = mask_solid_fetch;
        break;
    case SHADER_MASK_TEXTURE_ALPHA:
        mask_fetch = mask_alpha_pixmap_fetch;
        break;
    case SHADER_MASK_TEXTURE:
        mask_fetch = mask_pixmap_fetch;
        break;
    default:
        FatalError("Bad composite shader mask");
    }

    switch (key->in) {
    case SHADER_IN_SOURCE_ONLY:
        in = in_source_only;
        break;
    case SHADER_IN_NORMAL:
        in = in_normal;
        break;
    case SHADER_IN_CA_SOURCE:
        in = in_ca_source;
        break;
    case SHADER_IN_CA_ALPHA:
        in = in_ca_alpha;
        break;
    default:
        FatalError("Bad composite IN type");
    }

    XNFasprintf(&source, "%s%s%s%s%s%s", repeat_define, relocate_texture,
                rel_sampler, source_fetch, mask_fetch, in);

    prog = glamor_compile_glsl_prog(GL_FRAGMENT_SHADER, source);
    free(source);

    return prog;
}

static GLuint
glamor_create_composite_vs(struct shader_key *key)
{
    const char *main_opening =
        "attribute vec4 v_position;\n"
        "attribute vec4 v_texcoord0;\n"
        "attribute vec4 v_texcoord1;\n"
        "varying vec2 source_texture;\n"
        "varying vec2 mask_texture;\n"
        "void main()\n"
        "{\n"
        "	gl_Position = v_position;\n";
    const char *source_coords = "	source_texture = v_texcoord0.xy;\n";
    const char *mask_coords = "	mask_texture = v_texcoord1.xy;\n";
    const char *main_closing = "}\n";
    const char *source_coords_setup = "";
    const char *mask_coords_setup = "";
    char *source;
    GLuint prog;

    if (key->source != SHADER_SOURCE_SOLID)
        source_coords_setup = source_coords;

    if (key->mask != SHADER_MASK_NONE && key->mask != SHADER_MASK_SOLID)
        mask_coords_setup = mask_coords;

    XNFasprintf(&source,
                "%s%s%s%s",
                main_opening,
                source_coords_setup, mask_coords_setup, main_closing);

    prog = glamor_compile_glsl_prog(GL_VERTEX_SHADER, source);
    free(source);

    return prog;
}

static void
glamor_create_composite_shader(ScreenPtr screen, struct shader_key *key,
                               glamor_composite_shader *shader)
{
    GLuint vs, fs, prog;
    GLint source_sampler_uniform_location, mask_sampler_uniform_location;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);

    glamor_make_current(glamor_priv);
    vs = glamor_create_composite_vs(key);
    if (vs == 0)
        return;
    fs = glamor_create_composite_fs(key);
    if (fs == 0)
        return;

    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);

    glBindAttribLocation(prog, GLAMOR_VERTEX_POS, "v_position");
    glBindAttribLocation(prog, GLAMOR_VERTEX_SOURCE, "v_texcoord0");
    glBindAttribLocation(prog, GLAMOR_VERTEX_MASK, "v_texcoord1");

    glamor_link_glsl_prog(screen, prog, "composite");

    shader->prog = prog;

    glUseProgram(prog);

    if (key->source == SHADER_SOURCE_SOLID) {
        shader->source_uniform_location = glGetUniformLocation(prog, "source");
    }
    else {
        source_sampler_uniform_location =
            glGetUniformLocation(prog, "source_sampler");
        glUniform1i(source_sampler_uniform_location, 0);
        shader->source_wh = glGetUniformLocation(prog, "source_wh");
        shader->source_repeat_mode =
            glGetUniformLocation(prog, "source_repeat_mode");
    }

    if (key->mask != SHADER_MASK_NONE) {
        if (key->mask == SHADER_MASK_SOLID) {
            shader->mask_uniform_location = glGetUniformLocation(prog, "mask");
        }
        else {
            mask_sampler_uniform_location =
                glGetUniformLocation(prog, "mask_sampler");
            glUniform1i(mask_sampler_uniform_location, 1);
            shader->mask_wh = glGetUniformLocation(prog, "mask_wh");
            shader->mask_repeat_mode =
                glGetUniformLocation(prog, "mask_repeat_mode");
        }
    }
}

static glamor_composite_shader *
glamor_lookup_composite_shader(ScreenPtr screen, struct
                               shader_key
                               *key)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    glamor_composite_shader *shader;

    shader = &glamor_priv->composite_shader[key->source][key->mask][key->in];
    if (shader->prog == 0)
        glamor_create_composite_shader(screen, key, shader);

    return shader;
}

static void
glamor_init_eb(unsigned short *eb, int vert_cnt)
{
    int i, j;

    for (i = 0, j = 0; j < vert_cnt; i += 6, j += 4) {
        eb[i] = j;
        eb[i + 1] = j + 1;
        eb[i + 2] = j + 2;
        eb[i + 3] = j;
        eb[i + 4] = j + 2;
        eb[i + 5] = j + 3;
    }
}

void
glamor_init_composite_shaders(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;
    unsigned short *eb;
    int eb_size;

    glamor_priv = glamor_get_screen_private(screen);
    glamor_make_current(glamor_priv);
    glGenBuffers(1, &glamor_priv->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glamor_priv->ebo);

    eb_size = GLAMOR_COMPOSITE_VBO_VERT_CNT * sizeof(short) * 2;

    eb = XNFalloc(eb_size);
    glamor_init_eb(eb, GLAMOR_COMPOSITE_VBO_VERT_CNT);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, eb_size, eb, GL_STATIC_DRAW);
    free(eb);
}

void
glamor_fini_composite_shaders(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv;
    glamor_composite_shader *shader;
    int i, j, k;

    glamor_priv = glamor_get_screen_private(screen);
    glamor_make_current(glamor_priv);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &glamor_priv->ebo);

    for (i = 0; i < SHADER_SOURCE_COUNT; i++)
        for (j = 0; j < SHADER_MASK_COUNT; j++)
            for (k = 0; k < SHADER_IN_COUNT; k++) {
                shader = &glamor_priv->composite_shader[i][j][k];
                if (shader->prog)
                    glDeleteProgram(shader->prog);
            }
}

static Bool
glamor_set_composite_op(ScreenPtr screen,
                        CARD8 op, struct blendinfo *op_info_result,
                        PicturePtr dest, PicturePtr mask)
{
    GLenum source_blend, dest_blend;
    struct blendinfo *op_info;

    if (op >= ARRAY_SIZE(composite_op_info)) {
        glamor_fallback("unsupported render op %d \n", op);
        return GL_FALSE;
    }
    op_info = &composite_op_info[op];

    source_blend = op_info->source_blend;
    dest_blend = op_info->dest_blend;

    /* If there's no dst alpha channel, adjust the blend op so that we'll treat
     * it as always 1.
     */
    if (PICT_FORMAT_A(dest->format) == 0 && op_info->dest_alpha) {
        if (source_blend == GL_DST_ALPHA)
            source_blend = GL_ONE;
        else if (source_blend == GL_ONE_MINUS_DST_ALPHA)
            source_blend = GL_ZERO;
    }

    /* Set up the source alpha value for blending in component alpha mode. */
    if (mask && mask->componentAlpha
        && PICT_FORMAT_RGB(mask->format) != 0 && op_info->source_alpha) {
        if (dest_blend == GL_SRC_ALPHA)
            dest_blend = GL_SRC_COLOR;
        else if (dest_blend == GL_ONE_MINUS_SRC_ALPHA)
            dest_blend = GL_ONE_MINUS_SRC_COLOR;
    }

    op_info_result->source_blend = source_blend;
    op_info_result->dest_blend = dest_blend;
    op_info_result->source_alpha = op_info->source_alpha;
    op_info_result->dest_alpha = op_info->dest_alpha;

    return TRUE;
}

static void
glamor_set_composite_texture(glamor_screen_private *glamor_priv, int unit,
                             PicturePtr picture,
                             glamor_pixmap_private *pixmap_priv,
                             GLuint wh_location, GLuint repeat_location)
{
    float wh[4];
    int repeat_type;

    glamor_make_current(glamor_priv);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, pixmap_priv->base.fbo->tex);
    repeat_type = picture->repeatType;
    switch (picture->repeatType) {
    case RepeatNone:
        if (glamor_priv->gl_flavor != GLAMOR_GL_ES2) {
            /* XXX  GLES2 doesn't support GL_CLAMP_TO_BORDER. */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            GL_CLAMP_TO_BORDER);
        } else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        break;
    case RepeatNormal:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        break;
    case RepeatPad:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        break;
    case RepeatReflect:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
        break;
    }

    switch (picture->filter) {
    default:
    case PictFilterFast:
    case PictFilterNearest:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        break;
    case PictFilterGood:
    case PictFilterBest:
    case PictFilterBilinear:
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        break;
    }

    /*
     *  GLES2 doesn't support RepeatNone. We need to fix it anyway.
     *
     **/
    if (repeat_type != RepeatNone)
        repeat_type += RepeatFix;
    else if (glamor_priv->gl_flavor == GLAMOR_GL_ES2
             || pixmap_priv->type == GLAMOR_TEXTURE_LARGE) {
        if (picture->transform
            || (GLAMOR_PIXMAP_FBO_NOT_EXACT_SIZE(pixmap_priv)))
            repeat_type += RepeatFix;
    }
    if (repeat_type >= RepeatFix) {
        glamor_pixmap_fbo_fix_wh_ratio(wh, pixmap_priv);
        if ((wh[0] != 1.0 || wh[1] != 1.0)
            || (glamor_priv->gl_flavor == GLAMOR_GL_ES2
                && repeat_type == RepeatFix))
            glUniform4fv(wh_location, 1, wh);
        else
            repeat_type -= RepeatFix;
    }
    glUniform1i(repeat_location, repeat_type);
}

static void
glamor_set_composite_solid(float *color, GLint uniform_location)
{
    glUniform4fv(uniform_location, 1, color);
}

static int
compatible_formats(CARD8 op, PicturePtr dst, PicturePtr src)
{
    if (op == PictOpSrc) {
        if (src->format == dst->format)
            return 1;

        if (src->format == PICT_a8r8g8b8 && dst->format == PICT_x8r8g8b8)
            return 1;

        if (src->format == PICT_a8b8g8r8 && dst->format == PICT_x8b8g8r8)
            return 1;
    }
    else if (op == PictOpOver) {
        if (src->alphaMap || dst->alphaMap)
            return 0;

        if (src->format != dst->format)
            return 0;

        if (src->format == PICT_x8r8g8b8 || src->format == PICT_x8b8g8r8)
            return 1;
    }

    return 0;
}

static char
glamor_get_picture_location(PicturePtr picture)
{
    if (picture == NULL)
        return ' ';

    if (picture->pDrawable == NULL) {
        switch (picture->pSourcePict->type) {
        case SourcePictTypeSolidFill:
            return 'c';
        case SourcePictTypeLinear:
            return 'l';
        case SourcePictTypeRadial:
            return 'r';
        default:
            return '?';
        }
    }
    return glamor_get_drawable_location(picture->pDrawable);
}

static Bool
glamor_composite_with_copy(CARD8 op,
                           PicturePtr source,
                           PicturePtr dest,
                           INT16 x_source,
                           INT16 y_source,
                           INT16 x_dest, INT16 y_dest, RegionPtr region)
{
    int ret = FALSE;

    if (!source->pDrawable)
        return FALSE;

    if (!compatible_formats(op, dest, source))
        return FALSE;

    if (source->repeat || source->transform) {
        return FALSE;
    }

    x_dest += dest->pDrawable->x;
    y_dest += dest->pDrawable->y;
    x_source += source->pDrawable->x;
    y_source += source->pDrawable->y;
    if (PICT_FORMAT_A(source->format) == 0) {
        /* Fallback if we sample outside the source so that we
         * swizzle the correct clear color for out-of-bounds texels.
         */
        if (region->extents.x1 + x_source - x_dest < 0)
            goto cleanup_region;
        if (region->extents.x2 + x_source - x_dest > source->pDrawable->width)
            goto cleanup_region;

        if (region->extents.y1 + y_source - y_dest < 0)
            goto cleanup_region;
        if (region->extents.y2 + y_source - y_dest > source->pDrawable->height)
            goto cleanup_region;
    }
    ret = glamor_copy_n_to_n_nf(source->pDrawable,
                                dest->pDrawable, NULL,
                                RegionRects(region), RegionNumRects(region),
                                x_source - x_dest, y_source - y_dest,
                                FALSE, FALSE, 0, NULL);
 cleanup_region:
    return ret;
}

void *
glamor_setup_composite_vbo(ScreenPtr screen, int n_verts)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    int vert_size;
    char *vbo_offset;
    float *vb;

    glamor_priv->render_nr_verts = 0;
    glamor_priv->vb_stride = 2 * sizeof(float);
    if (glamor_priv->has_source_coords)
        glamor_priv->vb_stride += 2 * sizeof(float);
    if (glamor_priv->has_mask_coords)
        glamor_priv->vb_stride += 2 * sizeof(float);

    vert_size = n_verts * glamor_priv->vb_stride;

    glamor_make_current(glamor_priv);
    vb = glamor_get_vbo_space(screen, vert_size, &vbo_offset);

    glVertexAttribPointer(GLAMOR_VERTEX_POS, 2, GL_FLOAT, GL_FALSE,
                          glamor_priv->vb_stride, vbo_offset);
    glEnableVertexAttribArray(GLAMOR_VERTEX_POS);

    if (glamor_priv->has_source_coords) {
        glVertexAttribPointer(GLAMOR_VERTEX_SOURCE, 2,
                              GL_FLOAT, GL_FALSE,
                              glamor_priv->vb_stride,
                              vbo_offset + 2 * sizeof(float));
        glEnableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    }

    if (glamor_priv->has_mask_coords) {
        glVertexAttribPointer(GLAMOR_VERTEX_MASK, 2, GL_FLOAT, GL_FALSE,
                              glamor_priv->vb_stride,
                              vbo_offset + (glamor_priv->has_source_coords ?
                                            4 : 2) * sizeof(float));
        glEnableVertexAttribArray(GLAMOR_VERTEX_MASK);
    }

    return vb;
}

static void
glamor_flush_composite_rects(ScreenPtr screen)
{
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);

    glamor_make_current(glamor_priv);

    if (!glamor_priv->render_nr_verts)
        return;

    if (glamor_priv->gl_flavor == GLAMOR_GL_DESKTOP) {
        glDrawRangeElements(GL_TRIANGLES, 0, glamor_priv->render_nr_verts,
                            (glamor_priv->render_nr_verts * 3) / 2,
                            GL_UNSIGNED_SHORT, NULL);
    } else {
        glDrawElements(GL_TRIANGLES, (glamor_priv->render_nr_verts * 3) / 2,
                       GL_UNSIGNED_SHORT, NULL);
    }
}

int pict_format_combine_tab[][3] = {
    {PICT_TYPE_ARGB, PICT_TYPE_A, PICT_TYPE_ARGB},
    {PICT_TYPE_ABGR, PICT_TYPE_A, PICT_TYPE_ABGR},
};

static Bool
combine_pict_format(PictFormatShort * des, const PictFormatShort src,
                    const PictFormatShort mask, enum shader_in in_ca)
{
    PictFormatShort new_vis;
    int src_type, mask_type, src_bpp;
    int i;

    if (src == mask) {
        *des = src;
        return TRUE;
    }
    src_bpp = PICT_FORMAT_BPP(src);

    assert(src_bpp == PICT_FORMAT_BPP(mask));

    new_vis = PICT_FORMAT_VIS(src) | PICT_FORMAT_VIS(mask);

    switch (in_ca) {
    case SHADER_IN_SOURCE_ONLY:
        return TRUE;
    case SHADER_IN_NORMAL:
        src_type = PICT_FORMAT_TYPE(src);
        mask_type = PICT_TYPE_A;
        break;
    case SHADER_IN_CA_SOURCE:
        src_type = PICT_FORMAT_TYPE(src);
        mask_type = PICT_FORMAT_TYPE(mask);
        break;
    case SHADER_IN_CA_ALPHA:
        src_type = PICT_TYPE_A;
        mask_type = PICT_FORMAT_TYPE(mask);
        break;
    default:
        return FALSE;
    }

    if (src_type == mask_type) {
        *des = PICT_VISFORMAT(src_bpp, src_type, new_vis);
        return TRUE;
    }

    for (i = 0;
         i <
         sizeof(pict_format_combine_tab) /
         sizeof(pict_format_combine_tab[0]); i++) {
        if ((src_type == pict_format_combine_tab[i][0]
             && mask_type == pict_format_combine_tab[i][1])
            || (src_type == pict_format_combine_tab[i][1]
                && mask_type == pict_format_combine_tab[i][0])) {
            *des = PICT_VISFORMAT(src_bpp, pict_format_combine_tab[i]
                                  [2], new_vis);
            return TRUE;
        }
    }
    return FALSE;
}

static void
glamor_set_normalize_tcoords_generic(glamor_pixmap_private *priv,
                                     int repeat_type,
                                     float *matrix,
                                     float xscale, float yscale,
                                     int x1, int y1, int x2, int y2,
                                     int yInverted, float *texcoords,
                                     int stride)
{
    if (!matrix && repeat_type == RepeatNone)
        glamor_set_normalize_tcoords_ext(priv, xscale, yscale,
                                         x1, y1,
                                         x2, y2, yInverted, texcoords, stride);
    else if (matrix && repeat_type == RepeatNone)
        glamor_set_transformed_normalize_tcoords_ext(priv, matrix, xscale,
                                                     yscale, x1, y1,
                                                     x2, y2,
                                                     yInverted,
                                                     texcoords, stride);
    else if (!matrix && repeat_type != RepeatNone)
        glamor_set_repeat_normalize_tcoords_ext(priv, repeat_type,
                                                xscale, yscale,
                                                x1, y1,
                                                x2, y2,
                                                yInverted, texcoords, stride);
    else if (matrix && repeat_type != RepeatNone)
        glamor_set_repeat_transformed_normalize_tcoords_ext(priv, repeat_type,
                                                            matrix, xscale,
                                                            yscale, x1, y1, x2,
                                                            y2, yInverted,
                                                            texcoords, stride);
}

Bool
glamor_composite_choose_shader(CARD8 op,
                               PicturePtr source,
                               PicturePtr mask,
                               PicturePtr dest,
                               glamor_pixmap_private *source_pixmap_priv,
                               glamor_pixmap_private *mask_pixmap_priv,
                               glamor_pixmap_private *dest_pixmap_priv,
                               struct shader_key *s_key,
                               glamor_composite_shader ** shader,
                               struct blendinfo *op_info,
                               PictFormatShort *psaved_source_format)
{
    ScreenPtr screen = dest->pDrawable->pScreen;
    PixmapPtr dest_pixmap = dest_pixmap_priv->base.pixmap;
    PixmapPtr source_pixmap = NULL;
    PixmapPtr mask_pixmap = NULL;
    enum glamor_pixmap_status source_status = GLAMOR_NONE;
    enum glamor_pixmap_status mask_status = GLAMOR_NONE;
    PictFormatShort saved_source_format = 0;
    struct shader_key key;
    GLfloat source_solid_color[4];
    GLfloat mask_solid_color[4];
    Bool ret = FALSE;

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(dest_pixmap_priv)) {
        glamor_fallback("dest has no fbo.\n");
        goto fail;
    }

    memset(&key, 0, sizeof(key));
    if (!source) {
        key.source = SHADER_SOURCE_SOLID;
        source_solid_color[0] = 0.0;
        source_solid_color[1] = 0.0;
        source_solid_color[2] = 0.0;
        source_solid_color[3] = 0.0;
    }
    else if (!source->pDrawable) {
        if (source->pSourcePict->type == SourcePictTypeSolidFill) {
            key.source = SHADER_SOURCE_SOLID;
            glamor_get_rgba_from_pixel(source->pSourcePict->solidFill.color,
                                       &source_solid_color[0],
                                       &source_solid_color[1],
                                       &source_solid_color[2],
                                       &source_solid_color[3], PICT_a8r8g8b8);
        }
        else
            goto fail;
    }
    else {
        if (PICT_FORMAT_A(source->format))
            key.source = SHADER_SOURCE_TEXTURE_ALPHA;
        else
            key.source = SHADER_SOURCE_TEXTURE;
    }

    if (mask) {
        if (!mask->pDrawable) {
            if (mask->pSourcePict->type == SourcePictTypeSolidFill) {
                key.mask = SHADER_MASK_SOLID;
                glamor_get_rgba_from_pixel
                    (mask->pSourcePict->solidFill.color,
                     &mask_solid_color[0],
                     &mask_solid_color[1],
                     &mask_solid_color[2], &mask_solid_color[3], PICT_a8r8g8b8);
            }
            else
                goto fail;
        }
        else {
            key.mask = SHADER_MASK_TEXTURE_ALPHA;
        }

        if (!mask->componentAlpha) {
            key.in = SHADER_IN_NORMAL;
        }
        else {
            if (op == PictOpClear)
                key.mask = SHADER_MASK_NONE;
            else if (op == PictOpSrc || op == PictOpAdd
                     || op == PictOpIn || op == PictOpOut
                     || op == PictOpOverReverse)
                key.in = SHADER_IN_CA_SOURCE;
            else if (op == PictOpOutReverse || op == PictOpInReverse) {
                key.in = SHADER_IN_CA_ALPHA;
            }
            else {
                glamor_fallback("Unsupported component alpha op: %d\n", op);
                goto fail;
            }
        }
    }
    else {
        key.mask = SHADER_MASK_NONE;
        key.in = SHADER_IN_SOURCE_ONLY;
    }

    if (source && source->alphaMap) {
        glamor_fallback("source alphaMap\n");
        goto fail;
    }
    if (mask && mask->alphaMap) {
        glamor_fallback("mask alphaMap\n");
        goto fail;
    }

    if (key.source == SHADER_SOURCE_TEXTURE ||
        key.source == SHADER_SOURCE_TEXTURE_ALPHA) {
        source_pixmap = source_pixmap_priv->base.pixmap;
        if (source_pixmap == dest_pixmap) {
            /* XXX source and the dest share the same texture.
             * Does it need special handle? */
            glamor_fallback("source == dest\n");
        }
        if (source_pixmap_priv->base.gl_fbo == GLAMOR_FBO_UNATTACHED) {
#ifdef GLAMOR_PIXMAP_DYNAMIC_UPLOAD
            source_status = GLAMOR_UPLOAD_PENDING;
#else
            glamor_fallback("no texture in source\n");
            goto fail;
#endif
        }
    }

    if (key.mask == SHADER_MASK_TEXTURE ||
        key.mask == SHADER_MASK_TEXTURE_ALPHA) {
        mask_pixmap = mask_pixmap_priv->base.pixmap;
        if (mask_pixmap == dest_pixmap) {
            glamor_fallback("mask == dest\n");
            goto fail;
        }
        if (mask_pixmap_priv->base.gl_fbo == GLAMOR_FBO_UNATTACHED) {
#ifdef GLAMOR_PIXMAP_DYNAMIC_UPLOAD
            mask_status = GLAMOR_UPLOAD_PENDING;
#else
            glamor_fallback("no texture in mask\n");
            goto fail;
#endif
        }
    }

#ifdef GLAMOR_PIXMAP_DYNAMIC_UPLOAD
    if (source_status == GLAMOR_UPLOAD_PENDING
        && mask_status == GLAMOR_UPLOAD_PENDING
        && source_pixmap == mask_pixmap) {

        if (source->format != mask->format) {
            saved_source_format = source->format;

            if (!combine_pict_format(&source->format, source->format,
                                     mask->format, key.in)) {
                glamor_fallback("combine source %x mask %x failed.\n",
                                source->format, mask->format);
                goto fail;
            }

            if (source->format != saved_source_format) {
                glamor_picture_format_fixup(source, source_pixmap_priv);
            }
            /* XXX
             * By default, glamor_upload_picture_to_texture will wire alpha to 1
             * if one picture doesn't have alpha. So we don't do that again in
             * rendering function. But here is a special case, as source and
             * mask share the same texture but may have different formats. For
             * example, source doesn't have alpha, but mask has alpha. Then the
             * texture will have the alpha value for the mask. And will not wire
             * to 1 for the source. In this case, we have to use different shader
             * to wire the source's alpha to 1.
             *
             * But this may cause a potential problem if the source's repeat mode
             * is REPEAT_NONE, and if the source is smaller than the dest, then
             * for the region not covered by the source may be painted incorrectly.
             * because we wire the alpha to 1.
             *
             **/
            if (!PICT_FORMAT_A(saved_source_format)
                && PICT_FORMAT_A(mask->format))
                key.source = SHADER_SOURCE_TEXTURE;

            if (!PICT_FORMAT_A(mask->format)
                && PICT_FORMAT_A(saved_source_format))
                key.mask = SHADER_MASK_TEXTURE;

            mask_status = GLAMOR_NONE;
        }

        source_status = glamor_upload_picture_to_texture(source);
        if (source_status != GLAMOR_UPLOAD_DONE) {
            glamor_fallback("Failed to upload source texture.\n");
            goto fail;
        }
    }
    else {
        if (source_status == GLAMOR_UPLOAD_PENDING) {
            source_status = glamor_upload_picture_to_texture(source);
            if (source_status != GLAMOR_UPLOAD_DONE) {
                glamor_fallback("Failed to upload source texture.\n");
                goto fail;
            }
        }

        if (mask_status == GLAMOR_UPLOAD_PENDING) {
            mask_status = glamor_upload_picture_to_texture(mask);
            if (mask_status != GLAMOR_UPLOAD_DONE) {
                glamor_fallback("Failed to upload mask texture.\n");
                goto fail;
            }
        }
    }
#endif

    /* If the source and mask are two differently-formatted views of
     * the same pixmap bits, and the pixmap was already uploaded (so
     * the dynamic code above doesn't apply), then fall back to
     * software.  We should use texture views to fix this properly.
     */
    if (source_pixmap && source_pixmap == mask_pixmap &&
        source->format != mask->format) {
        goto fail;
    }

    /*Before enter the rendering stage, we need to fixup
     * transformed source and mask, if the transform is not int translate. */
    if (key.source != SHADER_SOURCE_SOLID
        && source->transform
        && !pixman_transform_is_int_translate(source->transform)
        && source_pixmap_priv->type != GLAMOR_TEXTURE_LARGE) {
        if (!glamor_fixup_pixmap_priv(screen, source_pixmap_priv))
            goto fail;
    }
    if (key.mask != SHADER_MASK_NONE && key.mask != SHADER_MASK_SOLID
        && mask->transform
        && !pixman_transform_is_int_translate(mask->transform)
        && mask_pixmap_priv->type != GLAMOR_TEXTURE_LARGE) {
        if (!glamor_fixup_pixmap_priv(screen, mask_pixmap_priv))
            goto fail;
    }

    if (!glamor_set_composite_op(screen, op, op_info, dest, mask))
        goto fail;

    *shader = glamor_lookup_composite_shader(screen, &key);
    if ((*shader)->prog == 0) {
        glamor_fallback("no shader program for this render acccel mode\n");
        goto fail;
    }

    if (key.source == SHADER_SOURCE_SOLID)
        memcpy(&(*shader)->source_solid_color[0],
               source_solid_color, 4 * sizeof(float));
    else {
        (*shader)->source_priv = source_pixmap_priv;
        (*shader)->source = source;
    }

    if (key.mask == SHADER_MASK_SOLID)
        memcpy(&(*shader)->mask_solid_color[0],
               mask_solid_color, 4 * sizeof(float));
    else {
        (*shader)->mask_priv = mask_pixmap_priv;
        (*shader)->mask = mask;
    }

    ret = TRUE;
    memcpy(s_key, &key, sizeof(key));
    *psaved_source_format = saved_source_format;
    goto done;

 fail:
    if (saved_source_format)
        source->format = saved_source_format;
 done:
    return ret;
}

void
glamor_composite_set_shader_blend(glamor_pixmap_private *dest_priv,
                                  struct shader_key *key,
                                  glamor_composite_shader *shader,
                                  struct blendinfo *op_info)
{
    glamor_screen_private *glamor_priv;

    glamor_priv = dest_priv->base.glamor_priv;

    glamor_make_current(glamor_priv);
    glUseProgram(shader->prog);

    if (key->source == SHADER_SOURCE_SOLID) {
        glamor_set_composite_solid(shader->source_solid_color,
                                   shader->source_uniform_location);
    }
    else {
        glamor_set_composite_texture(glamor_priv, 0,
                                     shader->source,
                                     shader->source_priv, shader->source_wh,
                                     shader->source_repeat_mode);
    }

    if (key->mask != SHADER_MASK_NONE) {
        if (key->mask == SHADER_MASK_SOLID) {
            glamor_set_composite_solid(shader->mask_solid_color,
                                       shader->mask_uniform_location);
        }
        else {
            glamor_set_composite_texture(glamor_priv, 1,
                                         shader->mask,
                                         shader->mask_priv, shader->mask_wh,
                                         shader->mask_repeat_mode);
        }
    }

    if (op_info->source_blend == GL_ONE && op_info->dest_blend == GL_ZERO) {
        glDisable(GL_BLEND);
    }
    else {
        glEnable(GL_BLEND);
        glBlendFunc(op_info->source_blend, op_info->dest_blend);
    }
}

static Bool
glamor_composite_with_shader(CARD8 op,
                             PicturePtr source,
                             PicturePtr mask,
                             PicturePtr dest,
                             glamor_pixmap_private *source_pixmap_priv,
                             glamor_pixmap_private *mask_pixmap_priv,
                             glamor_pixmap_private *dest_pixmap_priv,
                             int nrect, glamor_composite_rect_t *rects,
                             Bool two_pass_ca)
{
    ScreenPtr screen = dest->pDrawable->pScreen;
    glamor_screen_private *glamor_priv = dest_pixmap_priv->base.glamor_priv;
    PixmapPtr dest_pixmap = dest_pixmap_priv->base.pixmap;
    PixmapPtr source_pixmap = NULL;
    PixmapPtr mask_pixmap = NULL;
    GLfloat dst_xscale, dst_yscale;
    GLfloat mask_xscale = 1, mask_yscale = 1, src_xscale = 1, src_yscale = 1;
    struct shader_key key, key_ca;
    int dest_x_off, dest_y_off;
    int source_x_off, source_y_off;
    int mask_x_off, mask_y_off;
    PictFormatShort saved_source_format = 0;
    float src_matrix[9], mask_matrix[9];
    float *psrc_matrix = NULL, *pmask_matrix = NULL;
    int nrect_max;
    Bool ret = FALSE;
    glamor_composite_shader *shader = NULL, *shader_ca = NULL;
    struct blendinfo op_info, op_info_ca;

    if (!glamor_composite_choose_shader(op, source, mask, dest,
                                        source_pixmap_priv, mask_pixmap_priv,
                                        dest_pixmap_priv,
                                        &key, &shader, &op_info,
                                        &saved_source_format)) {
        glamor_fallback("glamor_composite_choose_shader failed\n");
        return ret;
    }
    if (two_pass_ca) {
        if (!glamor_composite_choose_shader(PictOpAdd, source, mask, dest,
                                            source_pixmap_priv,
                                            mask_pixmap_priv, dest_pixmap_priv,
                                            &key_ca, &shader_ca, &op_info_ca,
                                            &saved_source_format)) {
            glamor_fallback("glamor_composite_choose_shader failed\n");
            return ret;
        }
    }

    glamor_set_destination_pixmap_priv_nc(dest_pixmap_priv);
    glamor_composite_set_shader_blend(dest_pixmap_priv, &key, shader, &op_info);

    glamor_make_current(glamor_priv);

    glamor_priv->has_source_coords = key.source != SHADER_SOURCE_SOLID;
    glamor_priv->has_mask_coords = (key.mask != SHADER_MASK_NONE &&
                                    key.mask != SHADER_MASK_SOLID);

    dest_pixmap = glamor_get_drawable_pixmap(dest->pDrawable);
    dest_pixmap_priv = glamor_get_pixmap_private(dest_pixmap);
    glamor_get_drawable_deltas(dest->pDrawable, dest_pixmap,
                               &dest_x_off, &dest_y_off);
    pixmap_priv_get_dest_scale(dest_pixmap_priv, &dst_xscale, &dst_yscale);

    if (glamor_priv->has_source_coords) {
        source_pixmap = source_pixmap_priv->base.pixmap;
        glamor_get_drawable_deltas(source->pDrawable,
                                   source_pixmap, &source_x_off, &source_y_off);
        pixmap_priv_get_scale(source_pixmap_priv, &src_xscale, &src_yscale);
        if (source->transform) {
            psrc_matrix = src_matrix;
            glamor_picture_get_matrixf(source, psrc_matrix);
        }
    }

    if (glamor_priv->has_mask_coords) {
        mask_pixmap = mask_pixmap_priv->base.pixmap;
        glamor_get_drawable_deltas(mask->pDrawable, mask_pixmap,
                                   &mask_x_off, &mask_y_off);
        pixmap_priv_get_scale(mask_pixmap_priv, &mask_xscale, &mask_yscale);
        if (mask->transform) {
            pmask_matrix = mask_matrix;
            glamor_picture_get_matrixf(mask, pmask_matrix);
        }
    }

    nrect_max = MIN(nrect, GLAMOR_COMPOSITE_VBO_VERT_CNT / 4);

    while (nrect) {
        int mrect, rect_processed;
        int vb_stride;
        float *vertices;

        mrect = nrect > nrect_max ? nrect_max : nrect;
        vertices = glamor_setup_composite_vbo(screen, mrect * 4);
        rect_processed = mrect;
        vb_stride = glamor_priv->vb_stride / sizeof(float);
        while (mrect--) {
            INT16 x_source;
            INT16 y_source;
            INT16 x_mask;
            INT16 y_mask;
            INT16 x_dest;
            INT16 y_dest;
            CARD16 width;
            CARD16 height;

            x_dest = rects->x_dst + dest_x_off;
            y_dest = rects->y_dst + dest_y_off;
            x_source = rects->x_src + source_x_off;
            y_source = rects->y_src + source_y_off;
            x_mask = rects->x_mask + mask_x_off;
            y_mask = rects->y_mask + mask_y_off;
            width = rects->width;
            height = rects->height;

            DEBUGF
                ("dest(%d,%d) source(%d %d) mask (%d %d), width %d height %d \n",
                 x_dest, y_dest, x_source, y_source, x_mask, y_mask, width,
                 height);

            glamor_set_normalize_vcoords_ext(dest_pixmap_priv, dst_xscale,
                                             dst_yscale, x_dest, y_dest,
                                             x_dest + width, y_dest + height,
                                             glamor_priv->yInverted, vertices,
                                             vb_stride);
            vertices += 2;
            if (key.source != SHADER_SOURCE_SOLID) {
                glamor_set_normalize_tcoords_generic(source_pixmap_priv,
                                                     source->repeatType,
                                                     psrc_matrix, src_xscale,
                                                     src_yscale, x_source,
                                                     y_source, x_source + width,
                                                     y_source + height,
                                                     glamor_priv->yInverted,
                                                     vertices, vb_stride);
                vertices += 2;
            }

            if (key.mask != SHADER_MASK_NONE && key.mask != SHADER_MASK_SOLID) {
                glamor_set_normalize_tcoords_generic(mask_pixmap_priv,
                                                     mask->repeatType,
                                                     pmask_matrix, mask_xscale,
                                                     mask_yscale, x_mask,
                                                     y_mask, x_mask + width,
                                                     y_mask + height,
                                                     glamor_priv->yInverted,
                                                     vertices, vb_stride);
                vertices += 2;
            }
            glamor_priv->render_nr_verts += 4;
            rects++;

            /* We've incremented by one of our 4 verts, now do the other 3. */
            vertices += 3 * vb_stride;
        }
        glamor_put_vbo_space(screen);
        glamor_flush_composite_rects(screen);
        nrect -= rect_processed;
        if (two_pass_ca) {
            glamor_composite_set_shader_blend(dest_pixmap_priv,
                                              &key_ca, shader_ca, &op_info_ca);
            glamor_flush_composite_rects(screen);
            if (nrect)
                glamor_composite_set_shader_blend(dest_pixmap_priv,
                                                  &key, shader, &op_info);
        }
    }

    glDisableVertexAttribArray(GLAMOR_VERTEX_POS);
    glDisableVertexAttribArray(GLAMOR_VERTEX_SOURCE);
    glDisableVertexAttribArray(GLAMOR_VERTEX_MASK);
    glDisable(GL_BLEND);
    DEBUGF("finish rendering.\n");
    glamor_priv->state = RENDER_STATE;
    glamor_priv->render_idle_cnt = 0;
    if (saved_source_format)
        source->format = saved_source_format;

    ret = TRUE;
    return ret;
}

PicturePtr
glamor_convert_gradient_picture(ScreenPtr screen,
                                PicturePtr source,
                                int x_source,
                                int y_source, int width, int height)
{
    PixmapPtr pixmap;
    PicturePtr dst = NULL;
    int error;
    PictFormatShort format;

    if (!source->pDrawable)
        format = PICT_a8r8g8b8;
    else
        format = source->format;
#ifdef GLAMOR_GRADIENT_SHADER
    if (!source->pDrawable) {
        if (source->pSourcePict->type == SourcePictTypeLinear) {
            dst = glamor_generate_linear_gradient_picture(screen,
                                                          source, x_source,
                                                          y_source, width,
                                                          height, format);
        }
        else if (source->pSourcePict->type == SourcePictTypeRadial) {
            dst = glamor_generate_radial_gradient_picture(screen,
                                                          source, x_source,
                                                          y_source, width,
                                                          height, format);
        }

        if (dst) {
#if 0                           /* Debug to compare it to pixman, Enable it if needed. */
            glamor_compare_pictures(screen, source,
                                    dst, x_source, y_source, width, height,
                                    0, 3);
#endif
            return dst;
        }
    }
#endif
    pixmap = glamor_create_pixmap(screen,
                                  width,
                                  height,
                                  PIXMAN_FORMAT_DEPTH(format),
                                  GLAMOR_CREATE_PIXMAP_CPU);

    if (!pixmap)
        return NULL;

    dst = CreatePicture(0,
                        &pixmap->drawable,
                        PictureMatchFormat(screen,
                                           PIXMAN_FORMAT_DEPTH(format),
                                           format), 0, 0, serverClient, &error);
    glamor_destroy_pixmap(pixmap);
    if (!dst)
        return NULL;

    ValidatePicture(dst);

    fbComposite(PictOpSrc, source, NULL, dst, x_source, y_source,
                0, 0, 0, 0, width, height);
    return dst;
}

Bool
glamor_composite_clipped_region(CARD8 op,
                                PicturePtr source,
                                PicturePtr mask,
                                PicturePtr dest,
                                glamor_pixmap_private *source_pixmap_priv,
                                glamor_pixmap_private *mask_pixmap_priv,
                                glamor_pixmap_private *dest_pixmap_priv,
                                RegionPtr region,
                                int x_source,
                                int y_source,
                                int x_mask, int y_mask, int x_dest, int y_dest)
{
    ScreenPtr screen = dest->pDrawable->pScreen;
    PixmapPtr source_pixmap = NULL, mask_pixmap = NULL;
    PicturePtr temp_src = source, temp_mask = mask;
    glamor_pixmap_private *temp_src_priv = source_pixmap_priv;
    glamor_pixmap_private *temp_mask_priv = mask_pixmap_priv;
    int x_temp_src, y_temp_src, x_temp_mask, y_temp_mask;
    BoxPtr extent;
    glamor_composite_rect_t rect[10];
    glamor_composite_rect_t *prect = rect;
    int prect_size = ARRAY_SIZE(rect);
    int ok = FALSE;
    int i;
    int width;
    int height;
    BoxPtr box;
    int nbox;
    Bool two_pass_ca = FALSE;

    extent = RegionExtents(region);
    box = RegionRects(region);
    nbox = RegionNumRects(region);
    width = extent->x2 - extent->x1;
    height = extent->y2 - extent->y1;

    x_temp_src = x_source;
    y_temp_src = y_source;
    x_temp_mask = x_mask;
    y_temp_mask = y_mask;

    DEBUGF("clipped (%d %d) (%d %d) (%d %d) width %d height %d \n",
           x_source, y_source, x_mask, y_mask, x_dest, y_dest, width, height);

    if (source_pixmap_priv)
        source_pixmap = source_pixmap_priv->base.pixmap;

    if (mask_pixmap_priv)
        mask_pixmap = mask_pixmap_priv->base.pixmap;

    /* XXX is it possible source mask have non-zero drawable.x/y? */
    if (source
        && ((!source->pDrawable
             && (source->pSourcePict->type != SourcePictTypeSolidFill))
            || (source->pDrawable
                && !GLAMOR_PIXMAP_PRIV_HAS_FBO(source_pixmap_priv)
                && (source_pixmap->drawable.width != width
                    || source_pixmap->drawable.height != height)))) {
        temp_src =
            glamor_convert_gradient_picture(screen, source,
                                            extent->x1 + x_source - x_dest,
                                            extent->y1 + y_source - y_dest,
                                            width, height);
        if (!temp_src) {
            temp_src = source;
            goto out;
        }
        temp_src_priv =
            glamor_get_pixmap_private((PixmapPtr) (temp_src->pDrawable));
        x_temp_src = -extent->x1 + x_dest;
        y_temp_src = -extent->y1 + y_dest;
    }

    if (mask
        &&
        ((!mask->pDrawable
          && (mask->pSourcePict->type != SourcePictTypeSolidFill))
         || (mask->pDrawable && !GLAMOR_PIXMAP_PRIV_HAS_FBO(mask_pixmap_priv)
             && (mask_pixmap->drawable.width != width
                 || mask_pixmap->drawable.height != height)))) {
        /* XXX if mask->pDrawable is the same as source->pDrawable, we have an opportunity
         * to do reduce one convertion. */
        temp_mask =
            glamor_convert_gradient_picture(screen, mask,
                                            extent->x1 + x_mask - x_dest,
                                            extent->y1 + y_mask - y_dest,
                                            width, height);
        if (!temp_mask) {
            temp_mask = mask;
            goto out;
        }
        temp_mask_priv =
            glamor_get_pixmap_private((PixmapPtr) (temp_mask->pDrawable));
        x_temp_mask = -extent->x1 + x_dest;
        y_temp_mask = -extent->y1 + y_dest;
    }
    /* Do two-pass PictOpOver componentAlpha, until we enable
     * dual source color blending.
     */

    if (mask && mask->componentAlpha) {
        if (op == PictOpOver) {
            two_pass_ca = TRUE;
            op = PictOpOutReverse;
        }
    }

    if (!mask && temp_src) {
        if (glamor_composite_with_copy(op, temp_src, dest,
                                       x_temp_src, y_temp_src,
                                       x_dest, y_dest, region)) {
            ok = TRUE;
            goto out;
        }
    }

    /*XXXXX, self copy? */

    x_dest += dest->pDrawable->x;
    y_dest += dest->pDrawable->y;
    if (temp_src && temp_src->pDrawable) {
        x_temp_src += temp_src->pDrawable->x;
        y_temp_src += temp_src->pDrawable->y;
    }
    if (temp_mask && temp_mask->pDrawable) {
        x_temp_mask += temp_mask->pDrawable->x;
        y_temp_mask += temp_mask->pDrawable->y;
    }

    if (nbox > ARRAY_SIZE(rect)) {
        prect = calloc(nbox, sizeof(*prect));
        if (prect)
            prect_size = nbox;
        else {
            prect = rect;
            prect_size = ARRAY_SIZE(rect);
        }
    }
    while (nbox) {
        int box_cnt;

        box_cnt = nbox > prect_size ? prect_size : nbox;
        for (i = 0; i < box_cnt; i++) {
            prect[i].x_src = box[i].x1 + x_temp_src - x_dest;
            prect[i].y_src = box[i].y1 + y_temp_src - y_dest;
            prect[i].x_mask = box[i].x1 + x_temp_mask - x_dest;
            prect[i].y_mask = box[i].y1 + y_temp_mask - y_dest;
            prect[i].x_dst = box[i].x1;
            prect[i].y_dst = box[i].y1;
            prect[i].width = box[i].x2 - box[i].x1;
            prect[i].height = box[i].y2 - box[i].y1;
            DEBUGF("dest %d %d \n", prect[i].x_dst, prect[i].y_dst);
        }
        ok = glamor_composite_with_shader(op, temp_src, temp_mask, dest,
                                          temp_src_priv, temp_mask_priv,
                                          dest_pixmap_priv,
                                          box_cnt, prect, two_pass_ca);
        if (!ok)
            break;
        nbox -= box_cnt;
        box += box_cnt;
    }

    if (prect != rect)
        free(prect);
 out:
    if (temp_src != source)
        FreePicture(temp_src, 0);
    if (temp_mask != mask)
        FreePicture(temp_mask, 0);

    return ok;
}

static Bool
_glamor_composite(CARD8 op,
                  PicturePtr source,
                  PicturePtr mask,
                  PicturePtr dest,
                  INT16 x_source,
                  INT16 y_source,
                  INT16 x_mask,
                  INT16 y_mask,
                  INT16 x_dest, INT16 y_dest,
                  CARD16 width, CARD16 height, Bool fallback)
{
    ScreenPtr screen = dest->pDrawable->pScreen;
    glamor_pixmap_private *dest_pixmap_priv;
    glamor_pixmap_private *source_pixmap_priv = NULL, *mask_pixmap_priv = NULL;
    PixmapPtr dest_pixmap = glamor_get_drawable_pixmap(dest->pDrawable);
    PixmapPtr source_pixmap = NULL, mask_pixmap = NULL;
    glamor_screen_private *glamor_priv = glamor_get_screen_private(screen);
    Bool ret = TRUE;
    RegionRec region;
    BoxPtr extent;
    int nbox, ok = FALSE;
    PixmapPtr sub_dest_pixmap = NULL;
    PixmapPtr sub_source_pixmap = NULL;
    PixmapPtr sub_mask_pixmap = NULL;
    int dest_x_off, dest_y_off, saved_dest_x, saved_dest_y;
    int source_x_off, source_y_off, saved_source_x, saved_source_y;
    int mask_x_off, mask_y_off, saved_mask_x, saved_mask_y;
    DrawablePtr saved_dest_drawable;
    DrawablePtr saved_source_drawable;
    DrawablePtr saved_mask_drawable;
    int force_clip = 0;

    dest_pixmap_priv = glamor_get_pixmap_private(dest_pixmap);

    if (source->pDrawable) {
        source_pixmap = glamor_get_drawable_pixmap(source->pDrawable);
        source_pixmap_priv = glamor_get_pixmap_private(source_pixmap);
        if (source_pixmap_priv && source_pixmap_priv->type == GLAMOR_DRM_ONLY)
            goto fail;
    }

    if (mask && mask->pDrawable) {
        mask_pixmap = glamor_get_drawable_pixmap(mask->pDrawable);
        mask_pixmap_priv = glamor_get_pixmap_private(mask_pixmap);
        if (mask_pixmap_priv && mask_pixmap_priv->type == GLAMOR_DRM_ONLY)
            goto fail;
    }

    DEBUGF
        ("source pixmap %p (%d %d) mask(%d %d) dest(%d %d) width %d height %d \n",
         source_pixmap, x_source, y_source, x_mask, y_mask, x_dest, y_dest,
         width, height);

    if (!GLAMOR_PIXMAP_PRIV_HAS_FBO(dest_pixmap_priv)) {
        goto fail;
    }

    if (op >= ARRAY_SIZE(composite_op_info))
        goto fail;

    if (mask && mask->componentAlpha) {
        if (op == PictOpAtop
            || op == PictOpAtopReverse
            || op == PictOpXor || op >= PictOpSaturate) {
            glamor_fallback("glamor_composite(): component alpha op %x\n", op);
            goto fail;
        }
    }

    if ((source && source->filter >= PictFilterConvolution)
        || (mask && mask->filter >= PictFilterConvolution)) {
        glamor_fallback("glamor_composite(): unsupported filter\n");
        goto fail;
    }

    if (!miComputeCompositeRegion(&region,
                                  source, mask, dest,
                                  x_source +
                                  (source_pixmap ? source->pDrawable->x : 0),
                                  y_source +
                                  (source_pixmap ? source->pDrawable->y : 0),
                                  x_mask +
                                  (mask_pixmap ? mask->pDrawable->x : 0),
                                  y_mask +
                                  (mask_pixmap ? mask->pDrawable->y : 0),
                                  x_dest + dest->pDrawable->x,
                                  y_dest + dest->pDrawable->y, width, height)) {
        ret = TRUE;
        goto done;
    }

    nbox = REGION_NUM_RECTS(&region);
    DEBUGF("first clipped when compositing.\n");
    DEBUGRegionPrint(&region);
    extent = RegionExtents(&region);
    if (nbox == 0) {
        ret = TRUE;
        goto done;
    }
    /* If destination is not a large pixmap, but the region is larger
     * than texture size limitation, and source or mask is memory pixmap,
     * then there may be need to load a large memory pixmap to a
     * texture, and this is not permitted. Then we force to clip the
     * destination and make sure latter will not upload a large memory
     * pixmap. */
    if (!glamor_check_fbo_size(glamor_priv,
                               extent->x2 - extent->x1, extent->y2 - extent->y1)
        && (dest_pixmap_priv->type != GLAMOR_TEXTURE_LARGE)
        && ((source_pixmap_priv
             && (source_pixmap_priv->type == GLAMOR_MEMORY ||
                 source->repeatType == RepeatPad))
            || (mask_pixmap_priv &&
                (mask_pixmap_priv->type == GLAMOR_MEMORY ||
                 mask->repeatType == RepeatPad))
            || (!source_pixmap_priv &&
                (source->pSourcePict->type != SourcePictTypeSolidFill))
            || (!mask_pixmap_priv && mask &&
                mask->pSourcePict->type != SourcePictTypeSolidFill)))
        force_clip = 1;

    if (force_clip || dest_pixmap_priv->type == GLAMOR_TEXTURE_LARGE
        || (source_pixmap_priv
            && source_pixmap_priv->type == GLAMOR_TEXTURE_LARGE)
        || (mask_pixmap_priv && mask_pixmap_priv->type == GLAMOR_TEXTURE_LARGE))
        ok = glamor_composite_largepixmap_region(op,
                                                 source, mask, dest,
                                                 source_pixmap_priv,
                                                 mask_pixmap_priv,
                                                 dest_pixmap_priv,
                                                 &region, force_clip,
                                                 x_source, y_source,
                                                 x_mask, y_mask,
                                                 x_dest, y_dest, width, height);
    else
        ok = glamor_composite_clipped_region(op, source,
                                             mask, dest,
                                             source_pixmap_priv,
                                             mask_pixmap_priv,
                                             dest_pixmap_priv,
                                             &region,
                                             x_source, y_source,
                                             x_mask, y_mask, x_dest, y_dest);

    REGION_UNINIT(dest->pDrawable->pScreen, &region);

    if (ok)
        goto done;
 fail:

    if (!fallback && glamor_ddx_fallback_check_pixmap(&dest_pixmap->drawable)
        && (!source_pixmap
            || glamor_ddx_fallback_check_pixmap(&source_pixmap->drawable))
        && (!mask_pixmap
            || glamor_ddx_fallback_check_pixmap(&mask_pixmap->drawable))) {
        ret = FALSE;
        goto done;
    }

    glamor_fallback
        ("from picts %p:%p %dx%d / %p:%p %d x %d (%c,%c)  to pict %p:%p %dx%d (%c)\n",
         source, source->pDrawable,
         source->pDrawable ? source->pDrawable->width : 0,
         source->pDrawable ? source->pDrawable->height : 0, mask,
         (!mask) ? NULL : mask->pDrawable, (!mask
                                            || !mask->pDrawable) ? 0 :
         mask->pDrawable->width, (!mask
                                  || !mask->pDrawable) ? 0 : mask->
         pDrawable->height, glamor_get_picture_location(source),
         glamor_get_picture_location(mask), dest, dest->pDrawable,
         dest->pDrawable->width, dest->pDrawable->height,
         glamor_get_picture_location(dest));

#define GET_SUB_PICTURE(p, access)		do {					\
	glamor_get_drawable_deltas(p->pDrawable, p ##_pixmap,				\
				   & p ##_x_off, & p ##_y_off);				\
	sub_ ##p ##_pixmap = glamor_get_sub_pixmap(p ##_pixmap,				\
					      x_ ##p + p ##_x_off + p->pDrawable->x,	\
					      y_ ##p + p ##_y_off + p->pDrawable->y,	\
					      width, height, access);			\
	if (sub_ ##p ##_pixmap != NULL) {						\
		saved_ ##p ##_drawable = p->pDrawable;					\
		saved_ ##p ##_x = x_ ##p;						\
		saved_ ##p ##_y = y_ ##p;						\
		if (p->pCompositeClip)							\
			pixman_region_translate (p->pCompositeClip,			\
						 -p->pDrawable->x - x_ ##p,		\
						 -p->pDrawable->y - y_ ##p);		\
		p->pDrawable = &sub_ ##p ##_pixmap->drawable;				\
		x_ ##p = 0;								\
		y_ ##p = 0;								\
	} } while(0)
    GET_SUB_PICTURE(dest, GLAMOR_ACCESS_RW);
    if (source->pDrawable && !source->transform)
        GET_SUB_PICTURE(source, GLAMOR_ACCESS_RO);
    if (mask && mask->pDrawable && !mask->transform)
        GET_SUB_PICTURE(mask, GLAMOR_ACCESS_RO);

    if (glamor_prepare_access_picture(dest, GLAMOR_ACCESS_RW) &&
        glamor_prepare_access_picture(source, GLAMOR_ACCESS_RO) &&
        glamor_prepare_access_picture(mask, GLAMOR_ACCESS_RO)) {
        fbComposite(op,
                    source, mask, dest,
                    x_source, y_source,
                    x_mask, y_mask, x_dest, y_dest, width, height);
    }
    glamor_finish_access_picture(mask);
    glamor_finish_access_picture(source);
    glamor_finish_access_picture(dest);

#define PUT_SUB_PICTURE(p, access)		do {				\
	if (sub_ ##p ##_pixmap != NULL) {					\
		x_ ##p = saved_ ##p ##_x;					\
		y_ ##p = saved_ ##p ##_y;					\
		p->pDrawable = saved_ ##p ##_drawable;				\
		if (p->pCompositeClip)						\
			pixman_region_translate (p->pCompositeClip,		\
						 p->pDrawable->x + x_ ##p,	\
						 p->pDrawable->y + y_ ##p);	\
		glamor_put_sub_pixmap(sub_ ##p ##_pixmap, p ##_pixmap,		\
				      x_ ##p + p ##_x_off + p->pDrawable->x,	\
				      y_ ##p + p ##_y_off + p->pDrawable->y,	\
				      width, height, access);			\
	}} while(0)
    if (mask && mask->pDrawable)
        PUT_SUB_PICTURE(mask, GLAMOR_ACCESS_RO);
    if (source->pDrawable)
        PUT_SUB_PICTURE(source, GLAMOR_ACCESS_RO);
    PUT_SUB_PICTURE(dest, GLAMOR_ACCESS_RW);
 done:
    return ret;
}

void
glamor_composite(CARD8 op,
                 PicturePtr source,
                 PicturePtr mask,
                 PicturePtr dest,
                 INT16 x_source,
                 INT16 y_source,
                 INT16 x_mask,
                 INT16 y_mask,
                 INT16 x_dest, INT16 y_dest, CARD16 width, CARD16 height)
{
    _glamor_composite(op, source, mask, dest, x_source, y_source,
                      x_mask, y_mask, x_dest, y_dest, width, height, TRUE);
}

Bool
glamor_composite_nf(CARD8 op,
                    PicturePtr source,
                    PicturePtr mask,
                    PicturePtr dest,
                    INT16 x_source,
                    INT16 y_source,
                    INT16 x_mask,
                    INT16 y_mask,
                    INT16 x_dest, INT16 y_dest, CARD16 width, CARD16 height)
{
    return _glamor_composite(op, source, mask, dest, x_source, y_source,
                             x_mask, y_mask, x_dest, y_dest, width, height,
                             FALSE);
}

static void
glamor_get_src_rect_extent(int nrect,
                           glamor_composite_rect_t *rects, BoxPtr extent)
{
    extent->x1 = MAXSHORT;
    extent->y1 = MAXSHORT;
    extent->x2 = MINSHORT;
    extent->y2 = MINSHORT;

    while (nrect--) {
        if (extent->x1 > rects->x_src)
            extent->x1 = rects->x_src;
        if (extent->y1 > rects->y_src)
            extent->y1 = rects->y_src;
        if (extent->x2 < rects->x_src + rects->width)
            extent->x2 = rects->x_src + rects->width;
        if (extent->y2 < rects->y_src + rects->height)
            extent->y2 = rects->y_src + rects->height;
        rects++;
    }
}

static void
glamor_composite_src_rect_translate(int nrect,
                                    glamor_composite_rect_t *rects,
                                    int x, int y)
{
    while (nrect--) {
        rects->x_src += x;
        rects->y_src += y;
        rects++;
    }
}

void
glamor_composite_glyph_rects(CARD8 op,
                             PicturePtr src, PicturePtr mask, PicturePtr dst,
                             int nrect, glamor_composite_rect_t *rects)
{
    int n;
    PicturePtr temp_src = NULL;
    glamor_composite_rect_t *r;

    ValidatePicture(src);
    ValidatePicture(dst);
    if (!(glamor_is_large_picture(src)
          || (mask && glamor_is_large_picture(mask))
          || glamor_is_large_picture(dst))) {
        glamor_pixmap_private *src_pixmap_priv = NULL;
        glamor_pixmap_private *mask_pixmap_priv = NULL;
        glamor_pixmap_private *dst_pixmap_priv;
        glamor_pixmap_private *temp_src_priv = NULL;
        BoxRec src_extent;

        dst_pixmap_priv = glamor_get_pixmap_private
            (glamor_get_drawable_pixmap(dst->pDrawable));

        if (mask && mask->pDrawable)
            mask_pixmap_priv = glamor_get_pixmap_private
                (glamor_get_drawable_pixmap(mask->pDrawable));
        if (src->pDrawable)
            src_pixmap_priv = glamor_get_pixmap_private
                (glamor_get_drawable_pixmap(src->pDrawable));

        if (!src->pDrawable
            && (src->pSourcePict->type != SourcePictTypeSolidFill)) {
            glamor_get_src_rect_extent(nrect, rects, &src_extent);
            temp_src = glamor_convert_gradient_picture(dst->pDrawable->pScreen,
                                                       src,
                                                       src_extent.x1,
                                                       src_extent.y1,
                                                       src_extent.x2 -
                                                       src_extent.x1,
                                                       src_extent.y2 -
                                                       src_extent.y1);
            if (!temp_src)
                goto fallback;

            temp_src_priv = glamor_get_pixmap_private
                ((PixmapPtr) (temp_src->pDrawable));
            glamor_composite_src_rect_translate(nrect, rects,
                                                -src_extent.x1, -src_extent.y1);
        }
        else {
            temp_src = src;
            temp_src_priv = src_pixmap_priv;
        }

        if (mask && mask->componentAlpha) {
            if (op == PictOpOver) {
                if (glamor_composite_with_shader(PictOpOutReverse,
                                                 temp_src, mask, dst,
                                                 temp_src_priv,
                                                 mask_pixmap_priv,
                                                 dst_pixmap_priv, nrect, rects,
                                                 TRUE))
                    goto done;
            }
        }
        else {
            if (glamor_composite_with_shader
                (op, temp_src, mask, dst, temp_src_priv, mask_pixmap_priv,
                 dst_pixmap_priv, nrect, rects, FALSE))
                goto done;
        }
    }
 fallback:
    n = nrect;
    r = rects;

    while (n--) {
        CompositePicture(op,
                         temp_src ? temp_src : src,
                         mask,
                         dst,
                         r->x_src, r->y_src,
                         r->x_mask, r->y_mask,
                         r->x_dst, r->y_dst, r->width, r->height);
        r++;
    }

 done:
    if (temp_src && temp_src != src)
        FreePicture(temp_src, 0);
}

static Bool
_glamor_composite_rects(CARD8 op,
                        PicturePtr pDst,
                        xRenderColor *color,
                        int nRect, xRectangle *rects, Bool fallback)
{
    miCompositeRects(op, pDst, color, nRect, rects);
    return TRUE;
}

void
glamor_composite_rects(CARD8 op,
                       PicturePtr pDst,
                       xRenderColor *color, int nRect, xRectangle *rects)
{
    _glamor_composite_rects(op, pDst, color, nRect, rects, TRUE);
}

Bool
glamor_composite_rects_nf(CARD8 op,
                          PicturePtr pDst,
                          xRenderColor *color, int nRect, xRectangle *rects)
{
    return _glamor_composite_rects(op, pDst, color, nRect, rects, FALSE);
}

#endif                          /* RENDER */
