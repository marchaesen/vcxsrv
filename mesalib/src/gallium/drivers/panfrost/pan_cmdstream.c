/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2020 Collabora Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "util/macros.h"
#include "util/u_prim.h"
#include "util/u_vbuf.h"
#include "util/u_helpers.h"

#include "panfrost-quirks.h"

#include "pan_pool.h"
#include "pan_bo.h"
#include "pan_cmdstream.h"
#include "pan_context.h"
#include "pan_job.h"
#include "pan_shader.h"
#include "pan_texture.h"
#include "pan_blend_shaders.h"

/* If a BO is accessed for a particular shader stage, will it be in the primary
 * batch (vertex/tiler) or the secondary batch (fragment)? Anything but
 * fragment will be primary, e.g. compute jobs will be considered
 * "vertex/tiler" by analogy */

static inline uint32_t
panfrost_bo_access_for_stage(enum pipe_shader_type stage)
{
        assert(stage == PIPE_SHADER_FRAGMENT ||
               stage == PIPE_SHADER_VERTEX ||
               stage == PIPE_SHADER_COMPUTE);

        return stage == PIPE_SHADER_FRAGMENT ?
               PAN_BO_ACCESS_FRAGMENT :
               PAN_BO_ACCESS_VERTEX_TILER;
}

/* Gets a GPU address for the associated index buffer. Only gauranteed to be
 * good for the duration of the draw (transient), could last longer. Also get
 * the bounds on the index buffer for the range accessed by the draw. We do
 * these operations together because there are natural optimizations which
 * require them to be together. */

mali_ptr
panfrost_get_index_buffer_bounded(struct panfrost_context *ctx,
                                  const struct pipe_draw_info *info,
                                  const struct pipe_draw_start_count *draw,
                                  unsigned *min_index, unsigned *max_index)
{
        struct panfrost_resource *rsrc = pan_resource(info->index.resource);
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
        off_t offset = draw->start * info->index_size;
        bool needs_indices = true;
        mali_ptr out = 0;

        if (info->index_bounds_valid) {
                *min_index = info->min_index;
                *max_index = info->max_index;
                needs_indices = false;
        }

        if (!info->has_user_indices) {
                /* Only resources can be directly mapped */
                panfrost_batch_add_bo(batch, rsrc->bo,
                                      PAN_BO_ACCESS_SHARED |
                                      PAN_BO_ACCESS_READ |
                                      PAN_BO_ACCESS_VERTEX_TILER);
                out = rsrc->bo->ptr.gpu + offset;

                /* Check the cache */
                needs_indices = !panfrost_minmax_cache_get(rsrc->index_cache,
                                                           draw->start,
                                                           draw->count,
                                                           min_index,
                                                           max_index);
        } else {
                /* Otherwise, we need to upload to transient memory */
                const uint8_t *ibuf8 = (const uint8_t *) info->index.user;
                struct panfrost_ptr T =
                        panfrost_pool_alloc_aligned(&batch->pool,
                                draw->count * info->index_size,
                                info->index_size);

                memcpy(T.cpu, ibuf8 + offset, draw->count * info->index_size);
                out = T.gpu;
        }

        if (needs_indices) {
                /* Fallback */
                u_vbuf_get_minmax_index(&ctx->base, info, draw, min_index, max_index);

                if (!info->has_user_indices)
                        panfrost_minmax_cache_add(rsrc->index_cache,
                                                  draw->start, draw->count,
                                                  *min_index, *max_index);
        }

        return out;
}

static unsigned
translate_tex_wrap(enum pipe_tex_wrap w, bool supports_clamp, bool using_nearest)
{
        /* Bifrost doesn't support the GL_CLAMP wrap mode, so instead use
         * CLAMP_TO_EDGE and CLAMP_TO_BORDER. On Midgard, CLAMP is broken for
         * nearest filtering, so use CLAMP_TO_EDGE in that case. */

        switch (w) {
        case PIPE_TEX_WRAP_REPEAT: return MALI_WRAP_MODE_REPEAT;
        case PIPE_TEX_WRAP_CLAMP:
                return using_nearest ? MALI_WRAP_MODE_CLAMP_TO_EDGE :
                     (supports_clamp ? MALI_WRAP_MODE_CLAMP :
                                       MALI_WRAP_MODE_CLAMP_TO_BORDER);
        case PIPE_TEX_WRAP_CLAMP_TO_EDGE: return MALI_WRAP_MODE_CLAMP_TO_EDGE;
        case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return MALI_WRAP_MODE_CLAMP_TO_BORDER;
        case PIPE_TEX_WRAP_MIRROR_REPEAT: return MALI_WRAP_MODE_MIRRORED_REPEAT;
        case PIPE_TEX_WRAP_MIRROR_CLAMP:
                return using_nearest ? MALI_WRAP_MODE_MIRRORED_CLAMP_TO_EDGE :
                     (supports_clamp ? MALI_WRAP_MODE_MIRRORED_CLAMP :
                                       MALI_WRAP_MODE_MIRRORED_CLAMP_TO_BORDER);
        case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE: return MALI_WRAP_MODE_MIRRORED_CLAMP_TO_EDGE;
        case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER: return MALI_WRAP_MODE_MIRRORED_CLAMP_TO_BORDER;
        default: unreachable("Invalid wrap");
        }
}

/* The hardware compares in the wrong order order, so we have to flip before
 * encoding. Yes, really. */

static enum mali_func
panfrost_sampler_compare_func(const struct pipe_sampler_state *cso)
{
        if (!cso->compare_mode)
                return MALI_FUNC_NEVER;

        enum mali_func f = panfrost_translate_compare_func(cso->compare_func);
        return panfrost_flip_compare_func(f);
}

static enum mali_mipmap_mode
pan_pipe_to_mipmode(enum pipe_tex_mipfilter f)
{
        switch (f) {
        case PIPE_TEX_MIPFILTER_NEAREST: return MALI_MIPMAP_MODE_NEAREST;
        case PIPE_TEX_MIPFILTER_LINEAR: return MALI_MIPMAP_MODE_TRILINEAR;
        case PIPE_TEX_MIPFILTER_NONE: return MALI_MIPMAP_MODE_NONE;
        default: unreachable("Invalid");
        }
}

void panfrost_sampler_desc_init(const struct pipe_sampler_state *cso,
                                struct mali_midgard_sampler_packed *hw)
{
        bool using_nearest = cso->min_img_filter == PIPE_TEX_MIPFILTER_NEAREST;

        pan_pack(hw, MIDGARD_SAMPLER, cfg) {
                cfg.magnify_nearest = cso->mag_img_filter == PIPE_TEX_FILTER_NEAREST;
                cfg.minify_nearest = cso->min_img_filter == PIPE_TEX_FILTER_NEAREST;
                cfg.mipmap_mode = (cso->min_mip_filter == PIPE_TEX_MIPFILTER_LINEAR) ?
                        MALI_MIPMAP_MODE_TRILINEAR : MALI_MIPMAP_MODE_NEAREST;
                cfg.normalized_coordinates = cso->normalized_coords;

                cfg.lod_bias = FIXED_16(cso->lod_bias, true);

                cfg.minimum_lod = FIXED_16(cso->min_lod, false);

                /* If necessary, we disable mipmapping in the sampler descriptor by
                 * clamping the LOD as tight as possible (from 0 to epsilon,
                 * essentially -- remember these are fixed point numbers, so
                 * epsilon=1/256) */

                cfg.maximum_lod = (cso->min_mip_filter == PIPE_TEX_MIPFILTER_NONE) ?
                        cfg.minimum_lod + 1 :
                        FIXED_16(cso->max_lod, false);

                cfg.wrap_mode_s = translate_tex_wrap(cso->wrap_s, true, using_nearest);
                cfg.wrap_mode_t = translate_tex_wrap(cso->wrap_t, true, using_nearest);
                cfg.wrap_mode_r = translate_tex_wrap(cso->wrap_r, true, using_nearest);

                cfg.compare_function = panfrost_sampler_compare_func(cso);
                cfg.seamless_cube_map = cso->seamless_cube_map;

                cfg.border_color_r = cso->border_color.ui[0];
                cfg.border_color_g = cso->border_color.ui[1];
                cfg.border_color_b = cso->border_color.ui[2];
                cfg.border_color_a = cso->border_color.ui[3];
        }
}

void panfrost_sampler_desc_init_bifrost(const struct pipe_sampler_state *cso,
                                        struct mali_bifrost_sampler_packed *hw)
{
        bool using_nearest = cso->min_img_filter == PIPE_TEX_MIPFILTER_NEAREST;

        pan_pack(hw, BIFROST_SAMPLER, cfg) {
                cfg.point_sample_magnify = cso->mag_img_filter == PIPE_TEX_FILTER_NEAREST;
                cfg.point_sample_minify = cso->min_img_filter == PIPE_TEX_FILTER_NEAREST;
                cfg.mipmap_mode = pan_pipe_to_mipmode(cso->min_mip_filter);
                cfg.normalized_coordinates = cso->normalized_coords;

                cfg.lod_bias = FIXED_16(cso->lod_bias, true);
                cfg.minimum_lod = FIXED_16(cso->min_lod, false);
                cfg.maximum_lod = FIXED_16(cso->max_lod, false);

                if (cso->max_anisotropy > 1) {
                        cfg.maximum_anisotropy = cso->max_anisotropy;
                        cfg.lod_algorithm = MALI_LOD_ALGORITHM_ANISOTROPIC;
                }

                cfg.wrap_mode_s = translate_tex_wrap(cso->wrap_s, false, using_nearest);
                cfg.wrap_mode_t = translate_tex_wrap(cso->wrap_t, false, using_nearest);
                cfg.wrap_mode_r = translate_tex_wrap(cso->wrap_r, false, using_nearest);

                cfg.compare_function = panfrost_sampler_compare_func(cso);
                cfg.seamless_cube_map = cso->seamless_cube_map;

                cfg.border_color_r = cso->border_color.ui[0];
                cfg.border_color_g = cso->border_color.ui[1];
                cfg.border_color_b = cso->border_color.ui[2];
                cfg.border_color_a = cso->border_color.ui[3];
        }
}

static bool
panfrost_fs_required(
                struct panfrost_shader_state *fs,
                struct panfrost_blend_final *blend,
                struct pipe_framebuffer_state *state)
{
        /* If we generally have side effects */
        if (fs->info.fs.sidefx)
                return true;

        /* If colour is written we need to execute */
        for (unsigned i = 0; i < state->nr_cbufs; ++i) {
                if (!blend[i].no_colour && state->cbufs[i])
                        return true;
        }

        /* If depth is written and not implied we need to execute.
         * TODO: Predicate on Z/S writes being enabled */
        return (fs->info.fs.writes_depth || fs->info.fs.writes_stencil);
}

static enum mali_bifrost_register_file_format
bifrost_blend_type_from_nir(nir_alu_type nir_type)
{
        switch(nir_type) {
        case 0: /* Render target not in use */
                return 0;
        case nir_type_float16:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_F16;
        case nir_type_float32:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_F32;
        case nir_type_int32:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_I32;
        case nir_type_uint32:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_U32;
        case nir_type_int16:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_I16;
        case nir_type_uint16:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_U16;
        default:
                unreachable("Unsupported blend shader type for NIR alu type");
                return 0;
        }
}

static void
panfrost_emit_bifrost_blend(struct panfrost_batch *batch,
                            struct panfrost_blend_final *blend,
                            void *rts)
{
        unsigned rt_count = batch->key.nr_cbufs;
        const struct panfrost_device *dev = pan_device(batch->ctx->base.screen);
        struct panfrost_shader_state *fs = panfrost_get_shader_state(batch->ctx, PIPE_SHADER_FRAGMENT);

        /* Always have at least one render target for depth-only passes */
        for (unsigned i = 0; i < MAX2(rt_count, 1); ++i) {
                /* Disable blending for unbacked render targets */
                if (rt_count == 0 || !batch->key.cbufs[i]) {
                        pan_pack(rts, BLEND, cfg) {
                                cfg.enable = false;
                                cfg.bifrost.internal.mode = MALI_BIFROST_BLEND_MODE_OFF;
                        }

                        continue;
                }

                pan_pack(rts + i * MALI_BLEND_LENGTH, BLEND, cfg) {
                        if (blend[i].no_colour) {
                                cfg.enable = false;
                        } else {
                                cfg.srgb = util_format_is_srgb(batch->key.cbufs[i]->format);
                                cfg.load_destination = blend[i].load_dest;
                                cfg.round_to_fb_precision = !batch->ctx->blend->base.dither;
                        }

                        if (blend[i].is_shader) {
                                /* The blend shader's address needs to be at
                                 * the same top 32 bit as the fragment shader.
                                 * TODO: Ensure that's always the case.
                                 */
                                assert(!fs->bo ||
                                        (blend[i].shader.gpu & (0xffffffffull << 32)) ==
                                       (fs->bo->ptr.gpu & (0xffffffffull << 32)));
                                cfg.bifrost.internal.shader.pc = (u32)blend[i].shader.gpu;
                                unsigned ret_offset = fs->info.bifrost.blend[i].return_offset;
                                if (ret_offset) {
                                        assert(!(ret_offset & 0x7));
                                        cfg.bifrost.internal.shader.return_value =
                                                fs->bo->ptr.gpu + ret_offset;
                                }
                                cfg.bifrost.internal.mode = MALI_BIFROST_BLEND_MODE_SHADER;
                        } else {
                                enum pipe_format format = batch->key.cbufs[i]->format;
                                const struct util_format_description *format_desc;
                                unsigned chan_size = 0;

                                format_desc = util_format_description(format);

                                for (unsigned i = 0; i < format_desc->nr_channels; i++)
                                        chan_size = MAX2(format_desc->channel[0].size, chan_size);

                                cfg.bifrost.equation = blend[i].equation.equation;

                                /* Fixed point constant */
                                u16 constant = blend[i].equation.constant * ((1 << chan_size) - 1);
                                constant <<= 16 - chan_size;
                                cfg.bifrost.constant = constant;

                                if (blend[i].opaque)
                                        cfg.bifrost.internal.mode = MALI_BIFROST_BLEND_MODE_OPAQUE;
                                else
                                        cfg.bifrost.internal.mode = MALI_BIFROST_BLEND_MODE_FIXED_FUNCTION;

                                /* If we want the conversion to work properly,
                                 * num_comps must be set to 4
                                 */
                                cfg.bifrost.internal.fixed_function.num_comps = 4;
                                cfg.bifrost.internal.fixed_function.conversion.memory_format =
                                        panfrost_format_to_bifrost_blend(dev, format_desc, true);
                                cfg.bifrost.internal.fixed_function.conversion.register_format =
                                        bifrost_blend_type_from_nir(fs->info.bifrost.blend[i].type);
                                cfg.bifrost.internal.fixed_function.rt = i;
                        }
                }
        }
}

static void
panfrost_emit_midgard_blend(struct panfrost_batch *batch,
                            struct panfrost_blend_final *blend,
                            void *rts)
{
        unsigned rt_count = batch->key.nr_cbufs;

        /* Always have at least one render target for depth-only passes */
        for (unsigned i = 0; i < MAX2(rt_count, 1); ++i) {
                /* Disable blending for unbacked render targets */
                if (rt_count == 0 || !batch->key.cbufs[i]) {
                        pan_pack(rts, BLEND, cfg) {
                                cfg.midgard.equation.color_mask = 0xf;
                                cfg.midgard.equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
                                cfg.midgard.equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
                                cfg.midgard.equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
                                cfg.midgard.equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
                                cfg.midgard.equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
                                cfg.midgard.equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
                        }

                        continue;
                }

                pan_pack(rts + i * MALI_BLEND_LENGTH, BLEND, cfg) {
                        if (blend[i].no_colour) {
                                cfg.enable = false;
                                continue;
                        }

                        cfg.srgb = util_format_is_srgb(batch->key.cbufs[i]->format);
                        cfg.load_destination = blend[i].load_dest;
                        cfg.round_to_fb_precision = !batch->ctx->blend->base.dither;
                        cfg.midgard.blend_shader = blend[i].is_shader;
                        if (blend[i].is_shader) {
                                cfg.midgard.shader_pc = blend[i].shader.gpu | blend[i].shader.first_tag;
                        } else {
                                cfg.midgard.equation = blend[i].equation.equation;
                                cfg.midgard.constant = blend[i].equation.constant;
                        }
                }
        }
}

static void
panfrost_emit_blend(struct panfrost_batch *batch, void *rts,
                struct panfrost_blend_final *blend)
{
        const struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        if (pan_is_bifrost(dev))
                panfrost_emit_bifrost_blend(batch, blend, rts);
        else
                panfrost_emit_midgard_blend(batch, blend, rts);

        for (unsigned i = 0; i < batch->key.nr_cbufs; ++i) {
                if (!blend[i].no_colour && batch->key.cbufs[i])
                        batch->draws |= (PIPE_CLEAR_COLOR0 << i);
        }
}

static void
panfrost_prepare_bifrost_fs_state(struct panfrost_context *ctx,
                                  struct panfrost_blend_final *blend,
                                  struct MALI_RENDERER_STATE *state)
{
        const struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_shader_state *fs = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);
        bool alpha_to_coverage = ctx->blend->base.alpha_to_coverage;

        if (!panfrost_fs_required(fs, blend, &ctx->pipe_framebuffer)) {
                state->properties.uniform_buffer_count = 32;
                state->properties.bifrost.shader_modifies_coverage = true;
                state->properties.bifrost.allow_forward_pixel_to_kill = true;
                state->properties.bifrost.allow_forward_pixel_to_be_killed = true;
                state->properties.bifrost.zs_update_operation = MALI_PIXEL_KILL_STRONG_EARLY;
        } else {
                pan_shader_prepare_rsd(dev, &fs->info,
                                       fs->bo ? fs->bo->ptr.gpu : 0,
                                       state);

                bool no_blend = true;

                for (unsigned i = 0; i < ctx->pipe_framebuffer.nr_cbufs; ++i) {
                        no_blend &= (!blend[i].load_dest || blend[i].no_colour)
                                || (!ctx->pipe_framebuffer.cbufs[i]);
                }

                state->properties.bifrost.allow_forward_pixel_to_kill =
                        !fs->info.fs.writes_depth &&
                        !fs->info.fs.writes_stencil &&
                        !fs->info.fs.writes_coverage &&
                        !fs->info.fs.can_discard &&
                        !fs->info.fs.outputs_read &&
                        !alpha_to_coverage &&
                        no_blend;
        }
}

static void
panfrost_prepare_midgard_fs_state(struct panfrost_context *ctx,
                                  struct panfrost_blend_final *blend,
                                  struct MALI_RENDERER_STATE *state)
{
        const struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_shader_state *fs = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);
        const struct panfrost_zsa_state *zsa = ctx->depth_stencil;
        unsigned rt_count = ctx->pipe_framebuffer.nr_cbufs;
        bool alpha_to_coverage = ctx->blend->base.alpha_to_coverage;

        if (!panfrost_fs_required(fs, blend, &ctx->pipe_framebuffer)) {
                state->shader.shader = 0x1;
                state->properties.midgard.work_register_count = 1;
                state->properties.depth_source = MALI_DEPTH_SOURCE_FIXED_FUNCTION;
                state->properties.midgard.force_early_z = true;
        } else {
                pan_shader_prepare_rsd(dev, &fs->info,
                                       fs->bo ? fs->bo->ptr.gpu : 0,
                                       state);

                /* Reasons to disable early-Z from a shader perspective */
                bool late_z = fs->info.fs.can_discard || fs->info.writes_global ||
                              fs->info.fs.writes_depth || fs->info.fs.writes_stencil ||
                              (zsa->alpha_func != MALI_FUNC_ALWAYS);

                /* If either depth or stencil is enabled, discard matters */
                bool zs_enabled =
                        (zsa->base.depth_enabled && zsa->base.depth_func != PIPE_FUNC_ALWAYS) ||
                        zsa->base.stencil[0].enabled;

                bool has_blend_shader = false;

                for (unsigned c = 0; c < rt_count; ++c)
                        has_blend_shader |= blend[c].is_shader;

                /* TODO: Reduce this limit? */
                if (has_blend_shader)
                        state->properties.midgard.work_register_count = MAX2(fs->info.work_reg_count, 8);
                else
                        state->properties.midgard.work_register_count = fs->info.work_reg_count;

                state->properties.midgard.force_early_z = !(late_z || alpha_to_coverage);

                /* Workaround a hardware errata where early-z cannot be enabled
                 * when discarding even when the depth buffer is read-only, by
                 * lying to the hardware about the discard and setting the
                 * reads tilebuffer? flag to compensate */
                state->properties.midgard.shader_reads_tilebuffer =
                        fs->info.fs.outputs_read ||
                        (!zs_enabled && fs->info.fs.can_discard);
                state->properties.midgard.shader_contains_discard =
                        zs_enabled && fs->info.fs.can_discard;
        }

        if (dev->quirks & MIDGARD_SFBD && ctx->pipe_framebuffer.nr_cbufs > 0) {
                state->multisample_misc.sfbd_load_destination = blend[0].load_dest;
                state->multisample_misc.sfbd_blend_shader = blend[0].is_shader;
                state->stencil_mask_misc.sfbd_write_enable = !blend[0].no_colour;
                state->stencil_mask_misc.sfbd_srgb = util_format_is_srgb(ctx->pipe_framebuffer.cbufs[0]->format);
                state->stencil_mask_misc.sfbd_dither_disable = !ctx->blend->base.dither;

                if (blend[0].is_shader) {
                        state->sfbd_blend_shader = blend[0].shader.gpu |
                                                   blend[0].shader.first_tag;
                } else {
                        state->sfbd_blend_equation = blend[0].equation.equation;
                        state->sfbd_blend_constant = blend[0].equation.constant;
                }
        } else if (dev->quirks & MIDGARD_SFBD) {
                /* If there is no colour buffer, leaving fields default is
                 * fine, except for blending which is nonnullable */
                state->sfbd_blend_equation.color_mask = 0xf;
                state->sfbd_blend_equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
                state->sfbd_blend_equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
                state->sfbd_blend_equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
                state->sfbd_blend_equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
                state->sfbd_blend_equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
                state->sfbd_blend_equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
        } else {
                /* Bug where MRT-capable hw apparently reads the last blend
                 * shader from here instead of the usual location? */

                for (signed rt = ((signed) rt_count - 1); rt >= 0; --rt) {
                        if (!blend[rt].is_shader)
                                continue;

                        state->sfbd_blend_shader = blend[rt].shader.gpu |
                                                   blend[rt].shader.first_tag;
                        break;
                }
        }
}

static void
panfrost_prepare_fs_state(struct panfrost_context *ctx,
                          struct panfrost_blend_final *blend,
                          struct MALI_RENDERER_STATE *state)
{
        const struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_shader_state *fs = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);
        struct pipe_rasterizer_state *rast = &ctx->rasterizer->base;
        const struct panfrost_zsa_state *zsa = ctx->depth_stencil;
        bool alpha_to_coverage = ctx->blend->base.alpha_to_coverage;

        if (pan_is_bifrost(dev))
                panfrost_prepare_bifrost_fs_state(ctx, blend, state);
        else
                panfrost_prepare_midgard_fs_state(ctx, blend, state);

        bool msaa = rast->multisample;
        state->multisample_misc.multisample_enable = msaa;
        state->multisample_misc.sample_mask = (msaa ? ctx->sample_mask : ~0) & 0xFFFF;

        state->multisample_misc.evaluate_per_sample =
                msaa && (ctx->min_samples > 1 || fs->info.fs.sample_shading);

        state->multisample_misc.depth_function = zsa->base.depth_enabled ?
                panfrost_translate_compare_func(zsa->base.depth_func) :
                MALI_FUNC_ALWAYS;

        state->multisample_misc.depth_write_mask = zsa->base.depth_writemask;
        state->multisample_misc.fixed_function_near_discard = rast->depth_clip_near;
        state->multisample_misc.fixed_function_far_discard = rast->depth_clip_far;
        state->multisample_misc.shader_depth_range_fixed = true;

        state->stencil_mask_misc.stencil_mask_front = zsa->stencil_mask_front;
        state->stencil_mask_misc.stencil_mask_back = zsa->stencil_mask_back;
        state->stencil_mask_misc.stencil_enable = zsa->base.stencil[0].enabled;
        state->stencil_mask_misc.alpha_to_coverage = alpha_to_coverage;
        state->stencil_mask_misc.alpha_test_compare_function = zsa->alpha_func;
        state->stencil_mask_misc.depth_range_1 = rast->offset_tri;
        state->stencil_mask_misc.depth_range_2 = rast->offset_tri;
        state->stencil_mask_misc.single_sampled_lines = !rast->multisample;
        state->depth_units = rast->offset_units * 2.0f;
        state->depth_factor = rast->offset_scale;

        bool back_enab = zsa->base.stencil[1].enabled;
        state->stencil_front = zsa->stencil_front;
        state->stencil_back = zsa->stencil_back;
        state->stencil_front.reference_value = ctx->stencil_ref.ref_value[0];
        state->stencil_back.reference_value = ctx->stencil_ref.ref_value[back_enab ? 1 : 0];

        /* v6+ fits register preload here, no alpha testing */
        if (dev->arch <= 5)
                state->alpha_reference = zsa->base.alpha_ref_value;
}


static void
panfrost_emit_frag_shader(struct panfrost_context *ctx,
                          struct mali_renderer_state_packed *fragmeta,
                          struct panfrost_blend_final *blend)
{
        pan_pack(fragmeta, RENDERER_STATE, cfg) {
                panfrost_prepare_fs_state(ctx, blend, &cfg);
        }
}

mali_ptr
panfrost_emit_compute_shader_meta(struct panfrost_batch *batch, enum pipe_shader_type stage)
{
        struct panfrost_shader_state *ss = panfrost_get_shader_state(batch->ctx, stage);

        panfrost_batch_add_bo(batch, ss->bo,
                              PAN_BO_ACCESS_PRIVATE |
                              PAN_BO_ACCESS_READ |
                              PAN_BO_ACCESS_VERTEX_TILER);

        panfrost_batch_add_bo(batch, pan_resource(ss->upload.rsrc)->bo,
                              PAN_BO_ACCESS_PRIVATE |
                              PAN_BO_ACCESS_READ |
                              PAN_BO_ACCESS_VERTEX_TILER);

        return pan_resource(ss->upload.rsrc)->bo->ptr.gpu + ss->upload.offset;
}

mali_ptr
panfrost_emit_frag_shader_meta(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_shader_state *ss = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);

        /* Add the shader BO to the batch. */
        panfrost_batch_add_bo(batch, ss->bo,
                              PAN_BO_ACCESS_PRIVATE |
                              PAN_BO_ACCESS_READ |
                              PAN_BO_ACCESS_FRAGMENT);

        struct panfrost_device *dev = pan_device(ctx->base.screen);
        unsigned rt_count = MAX2(ctx->pipe_framebuffer.nr_cbufs, 1);
        struct panfrost_ptr xfer;

        if (dev->quirks & MIDGARD_SFBD) {
                xfer = panfrost_pool_alloc_desc(&batch->pool, RENDERER_STATE);
        } else {
                xfer = panfrost_pool_alloc_desc_aggregate(&batch->pool,
                                                          PAN_DESC(RENDERER_STATE),
                                                          PAN_DESC_ARRAY(rt_count, BLEND));
        }

        struct panfrost_blend_final blend[PIPE_MAX_COLOR_BUFS];
        unsigned shader_offset = 0;
        struct panfrost_bo *shader_bo = NULL;

        for (unsigned c = 0; c < ctx->pipe_framebuffer.nr_cbufs; ++c) {
                if (ctx->pipe_framebuffer.cbufs[c]) {
                        blend[c] = panfrost_get_blend_for_context(ctx, c,
                                        &shader_bo, &shader_offset);
                }
        }

        panfrost_emit_frag_shader(ctx, (struct mali_renderer_state_packed *) xfer.cpu, blend);

        if (!(dev->quirks & MIDGARD_SFBD))
                panfrost_emit_blend(batch, xfer.cpu + MALI_RENDERER_STATE_LENGTH, blend);
        else
                batch->draws |= PIPE_CLEAR_COLOR0;

        if (ctx->depth_stencil->base.depth_enabled)
                batch->read |= PIPE_CLEAR_DEPTH;

        if (ctx->depth_stencil->base.stencil[0].enabled)
                batch->read |= PIPE_CLEAR_STENCIL;

        return xfer.gpu;
}

mali_ptr
panfrost_emit_viewport(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;
        const struct pipe_viewport_state *vp = &ctx->pipe_viewport;
        const struct pipe_scissor_state *ss = &ctx->scissor;
        const struct pipe_rasterizer_state *rast = &ctx->rasterizer->base;
        const struct pipe_framebuffer_state *fb = &ctx->pipe_framebuffer;

        /* Derive min/max from translate/scale. Note since |x| >= 0 by
         * definition, we have that -|x| <= |x| hence translate - |scale| <=
         * translate + |scale|, so the ordering is correct here. */
        float vp_minx = vp->translate[0] - fabsf(vp->scale[0]);
        float vp_maxx = vp->translate[0] + fabsf(vp->scale[0]);
        float vp_miny = vp->translate[1] - fabsf(vp->scale[1]);
        float vp_maxy = vp->translate[1] + fabsf(vp->scale[1]);
        float minz = (vp->translate[2] - fabsf(vp->scale[2]));
        float maxz = (vp->translate[2] + fabsf(vp->scale[2]));

        /* Scissor to the intersection of viewport and to the scissor, clamped
         * to the framebuffer */

        unsigned minx = MIN2(fb->width, MAX2((int) vp_minx, 0));
        unsigned maxx = MIN2(fb->width, MAX2((int) vp_maxx, 0));
        unsigned miny = MIN2(fb->height, MAX2((int) vp_miny, 0));
        unsigned maxy = MIN2(fb->height, MAX2((int) vp_maxy, 0));

        if (ss && rast->scissor) {
                minx = MAX2(ss->minx, minx);
                miny = MAX2(ss->miny, miny);
                maxx = MIN2(ss->maxx, maxx);
                maxy = MIN2(ss->maxy, maxy);
        }

        /* Set the range to [1, 1) so max values don't wrap round */
        if (maxx == 0 || maxy == 0)
                maxx = maxy = minx = miny = 1;

        struct panfrost_ptr T = panfrost_pool_alloc_desc(&batch->pool, VIEWPORT);

        pan_pack(T.cpu, VIEWPORT, cfg) {
                /* [minx, maxx) and [miny, maxy) are exclusive ranges, but
                 * these are inclusive */
                cfg.scissor_minimum_x = minx;
                cfg.scissor_minimum_y = miny;
                cfg.scissor_maximum_x = maxx - 1;
                cfg.scissor_maximum_y = maxy - 1;

                cfg.minimum_z = rast->depth_clip_near ? minz : -INFINITY;
                cfg.maximum_z = rast->depth_clip_far ? maxz : INFINITY;
        }

        panfrost_batch_union_scissor(batch, minx, miny, maxx, maxy);
        return T.gpu;
}

static mali_ptr
panfrost_map_constant_buffer_gpu(struct panfrost_batch *batch,
                                 enum pipe_shader_type st,
                                 struct panfrost_constant_buffer *buf,
                                 unsigned index)
{
        struct pipe_constant_buffer *cb = &buf->cb[index];
        struct panfrost_resource *rsrc = pan_resource(cb->buffer);

        if (rsrc) {
                panfrost_batch_add_bo(batch, rsrc->bo,
                                      PAN_BO_ACCESS_SHARED |
                                      PAN_BO_ACCESS_READ |
                                      panfrost_bo_access_for_stage(st));

                /* Alignment gauranteed by
                 * PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT */
                return rsrc->bo->ptr.gpu + cb->buffer_offset;
        } else if (cb->user_buffer) {
                return panfrost_pool_upload_aligned(&batch->pool,
                                                 cb->user_buffer +
                                                 cb->buffer_offset,
                                                 cb->buffer_size, 16);
        } else {
                unreachable("No constant buffer");
        }
}

struct sysval_uniform {
        union {
                float f[4];
                int32_t i[4];
                uint32_t u[4];
                uint64_t du[2];
        };
};

static void
panfrost_upload_viewport_scale_sysval(struct panfrost_batch *batch,
                                      struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        const struct pipe_viewport_state *vp = &ctx->pipe_viewport;

        uniform->f[0] = vp->scale[0];
        uniform->f[1] = vp->scale[1];
        uniform->f[2] = vp->scale[2];
}

static void
panfrost_upload_viewport_offset_sysval(struct panfrost_batch *batch,
                                       struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        const struct pipe_viewport_state *vp = &ctx->pipe_viewport;

        uniform->f[0] = vp->translate[0];
        uniform->f[1] = vp->translate[1];
        uniform->f[2] = vp->translate[2];
}

static void panfrost_upload_txs_sysval(struct panfrost_batch *batch,
                                       enum pipe_shader_type st,
                                       unsigned int sysvalid,
                                       struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        unsigned texidx = PAN_SYSVAL_ID_TO_TXS_TEX_IDX(sysvalid);
        unsigned dim = PAN_SYSVAL_ID_TO_TXS_DIM(sysvalid);
        bool is_array = PAN_SYSVAL_ID_TO_TXS_IS_ARRAY(sysvalid);
        struct pipe_sampler_view *tex = &ctx->sampler_views[st][texidx]->base;

        assert(dim);

        if (tex->target == PIPE_BUFFER) {
                assert(dim == 1);
                uniform->i[0] =
                        tex->u.buf.size / util_format_get_blocksize(tex->format);
                return;
        }

        uniform->i[0] = u_minify(tex->texture->width0, tex->u.tex.first_level);

        if (dim > 1)
                uniform->i[1] = u_minify(tex->texture->height0,
                                         tex->u.tex.first_level);

        if (dim > 2)
                uniform->i[2] = u_minify(tex->texture->depth0,
                                         tex->u.tex.first_level);

        if (is_array)
                uniform->i[dim] = tex->texture->array_size;
}

static void panfrost_upload_image_size_sysval(struct panfrost_batch *batch,
                                              enum pipe_shader_type st,
                                              unsigned int sysvalid,
                                              struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        unsigned idx = PAN_SYSVAL_ID_TO_TXS_TEX_IDX(sysvalid);
        unsigned dim = PAN_SYSVAL_ID_TO_TXS_DIM(sysvalid);
        unsigned is_array = PAN_SYSVAL_ID_TO_TXS_IS_ARRAY(sysvalid);

        assert(dim && dim < 4);

        struct pipe_image_view *image = &ctx->images[st][idx];

        if (image->resource->target == PIPE_BUFFER) {
                unsigned blocksize = util_format_get_blocksize(image->format);
                uniform->i[0] = image->resource->width0 / blocksize;
                return;
        }

        uniform->i[0] = u_minify(image->resource->width0,
                                 image->u.tex.level);

        if (dim > 1)
                uniform->i[1] = u_minify(image->resource->height0,
                                         image->u.tex.level);

        if (dim > 2)
                uniform->i[2] = u_minify(image->resource->depth0,
                                         image->u.tex.level);

        if (is_array)
                uniform->i[dim] = image->resource->array_size;
}

static void
panfrost_upload_ssbo_sysval(struct panfrost_batch *batch,
                            enum pipe_shader_type st,
                            unsigned ssbo_id,
                            struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;

        assert(ctx->ssbo_mask[st] & (1 << ssbo_id));
        struct pipe_shader_buffer sb = ctx->ssbo[st][ssbo_id];

        /* Compute address */
        struct panfrost_bo *bo = pan_resource(sb.buffer)->bo;

        panfrost_batch_add_bo(batch, bo,
                              PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_RW |
                              panfrost_bo_access_for_stage(st));

        /* Upload address and size as sysval */
        uniform->du[0] = bo->ptr.gpu + sb.buffer_offset;
        uniform->u[2] = sb.buffer_size;
}

static void
panfrost_upload_sampler_sysval(struct panfrost_batch *batch,
                               enum pipe_shader_type st,
                               unsigned samp_idx,
                               struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        struct pipe_sampler_state *sampl = &ctx->samplers[st][samp_idx]->base;

        uniform->f[0] = sampl->min_lod;
        uniform->f[1] = sampl->max_lod;
        uniform->f[2] = sampl->lod_bias;

        /* Even without any errata, Midgard represents "no mipmapping" as
         * fixing the LOD with the clamps; keep behaviour consistent. c.f.
         * panfrost_create_sampler_state which also explains our choice of
         * epsilon value (again to keep behaviour consistent) */

        if (sampl->min_mip_filter == PIPE_TEX_MIPFILTER_NONE)
                uniform->f[1] = uniform->f[0] + (1.0/256.0);
}

static void
panfrost_upload_num_work_groups_sysval(struct panfrost_batch *batch,
                                       struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;

        uniform->u[0] = ctx->compute_grid->grid[0];
        uniform->u[1] = ctx->compute_grid->grid[1];
        uniform->u[2] = ctx->compute_grid->grid[2];
}

static void
panfrost_upload_local_group_size_sysval(struct panfrost_batch *batch,
                                        struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;

        uniform->u[0] = ctx->compute_grid->block[0];
        uniform->u[1] = ctx->compute_grid->block[1];
        uniform->u[2] = ctx->compute_grid->block[2];
}

static void
panfrost_upload_work_dim_sysval(struct panfrost_batch *batch,
                                struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;

        uniform->u[0] = ctx->compute_grid->work_dim;
}

/* Sample positions are pushed in a Bifrost specific format on Bifrost. On
 * Midgard, we emulate the Bifrost path with some extra arithmetic in the
 * shader, to keep the code as unified as possible. */

static void
panfrost_upload_sample_positions_sysval(struct panfrost_batch *batch,
                                struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *dev = pan_device(ctx->base.screen);

        unsigned samples = util_framebuffer_get_num_samples(&batch->key);
        uniform->du[0] = panfrost_sample_positions(dev, panfrost_sample_pattern(samples));
}

static void
panfrost_upload_multisampled_sysval(struct panfrost_batch *batch,
                                struct sysval_uniform *uniform)
{
        unsigned samples = util_framebuffer_get_num_samples(&batch->key);
        uniform->u[0] = samples > 1;
}

static void
panfrost_upload_rt_conversion_sysval(struct panfrost_batch *batch, unsigned rt,
                struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *dev = pan_device(ctx->base.screen);

        if (rt < batch->key.nr_cbufs && batch->key.cbufs[rt]) {
                enum pipe_format format = batch->key.cbufs[rt]->format;
                uniform->u[0] = bifrost_get_blend_desc(dev, format, rt, 32) >> 32;
        } else {
                pan_pack(&uniform->u[0], BIFROST_INTERNAL_CONVERSION, cfg)
                        cfg.memory_format = dev->formats[PIPE_FORMAT_NONE].hw;
        }
}

static void
panfrost_upload_sysvals(struct panfrost_batch *batch, void *buf,
                        struct panfrost_shader_state *ss,
                        enum pipe_shader_type st)
{
        struct sysval_uniform *uniforms = (void *)buf;

        for (unsigned i = 0; i < ss->info.sysvals.sysval_count; ++i) {
                int sysval = ss->info.sysvals.sysvals[i];

                switch (PAN_SYSVAL_TYPE(sysval)) {
                case PAN_SYSVAL_VIEWPORT_SCALE:
                        panfrost_upload_viewport_scale_sysval(batch,
                                                              &uniforms[i]);
                        break;
                case PAN_SYSVAL_VIEWPORT_OFFSET:
                        panfrost_upload_viewport_offset_sysval(batch,
                                                               &uniforms[i]);
                        break;
                case PAN_SYSVAL_TEXTURE_SIZE:
                        panfrost_upload_txs_sysval(batch, st,
                                                   PAN_SYSVAL_ID(sysval),
                                                   &uniforms[i]);
                        break;
                case PAN_SYSVAL_SSBO:
                        panfrost_upload_ssbo_sysval(batch, st,
                                                    PAN_SYSVAL_ID(sysval),
                                                    &uniforms[i]);
                        break;
                case PAN_SYSVAL_NUM_WORK_GROUPS:
                        panfrost_upload_num_work_groups_sysval(batch,
                                                               &uniforms[i]);
                        break;
                case PAN_SYSVAL_LOCAL_GROUP_SIZE:
                        panfrost_upload_local_group_size_sysval(batch,
                                                                &uniforms[i]);
                        break;
                case PAN_SYSVAL_WORK_DIM:
                        panfrost_upload_work_dim_sysval(batch,
                                                        &uniforms[i]);
                        break;
                case PAN_SYSVAL_SAMPLER:
                        panfrost_upload_sampler_sysval(batch, st,
                                                       PAN_SYSVAL_ID(sysval),
                                                       &uniforms[i]);
                        break;
                case PAN_SYSVAL_IMAGE_SIZE:
                        panfrost_upload_image_size_sysval(batch, st,
                                                          PAN_SYSVAL_ID(sysval),
                                                          &uniforms[i]);
                        break;
                case PAN_SYSVAL_SAMPLE_POSITIONS:
                        panfrost_upload_sample_positions_sysval(batch,
                                                        &uniforms[i]);
                        break;
                case PAN_SYSVAL_MULTISAMPLED:
                        panfrost_upload_multisampled_sysval(batch,
                                                               &uniforms[i]);
                        break;
                case PAN_SYSVAL_RT_CONVERSION:
                        panfrost_upload_rt_conversion_sysval(batch,
                                        PAN_SYSVAL_ID(sysval), &uniforms[i]);
                        break;
                default:
                        assert(0);
                }
        }
}

static const void *
panfrost_map_constant_buffer_cpu(struct panfrost_context *ctx,
                                 struct panfrost_constant_buffer *buf,
                                 unsigned index)
{
        struct pipe_constant_buffer *cb = &buf->cb[index];
        struct panfrost_resource *rsrc = pan_resource(cb->buffer);

        if (rsrc) {
                panfrost_bo_mmap(rsrc->bo);
                panfrost_flush_batches_accessing_bo(ctx, rsrc->bo, false);
                panfrost_bo_wait(rsrc->bo, INT64_MAX, false);

                return rsrc->bo->ptr.cpu + cb->buffer_offset;
        } else if (cb->user_buffer) {
                return cb->user_buffer + cb->buffer_offset;
        } else
                unreachable("No constant buffer");
}

mali_ptr
panfrost_emit_const_buf(struct panfrost_batch *batch,
                        enum pipe_shader_type stage,
                        mali_ptr *push_constants)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_shader_variants *all = ctx->shader[stage];

        if (!all)
                return 0;

        struct panfrost_constant_buffer *buf = &ctx->constant_buffer[stage];
        struct panfrost_shader_state *ss = &all->variants[all->active_variant];

        /* Allocate room for the sysval and the uniforms */
        size_t sys_size = sizeof(float) * 4 * ss->info.sysvals.sysval_count;
        struct panfrost_ptr transfer =
                panfrost_pool_alloc_aligned(&batch->pool, sys_size, 16);

        /* Upload sysvals requested by the shader */
        panfrost_upload_sysvals(batch, transfer.cpu, ss, stage);

        /* Next up, attach UBOs. UBO count includes gaps but no sysval UBO */
        struct panfrost_shader_state *shader = panfrost_get_shader_state(ctx, stage);
        unsigned ubo_count = shader->info.ubo_count - (sys_size ? 1 : 0);
        unsigned sysval_ubo = sys_size ? ubo_count : ~0;

        struct panfrost_ptr ubos =
                panfrost_pool_alloc_desc_array(&batch->pool, ubo_count + 1,
                                               UNIFORM_BUFFER);

        uint64_t *ubo_ptr = (uint64_t *) ubos.cpu;

        /* Upload sysval as a final UBO */

        if (sys_size) {
                pan_pack(ubo_ptr + ubo_count, UNIFORM_BUFFER, cfg) {
                        cfg.entries = DIV_ROUND_UP(sys_size, 16);
                        cfg.pointer = transfer.gpu;
                }
        }

        /* The rest are honest-to-goodness UBOs */

        for (unsigned ubo = 0; ubo < ubo_count; ++ubo) {
                size_t usz = buf->cb[ubo].buffer_size;
                bool enabled = buf->enabled_mask & (1 << ubo);
                bool empty = usz == 0;

                if (!enabled || empty) {
                        ubo_ptr[ubo] = 0;
                        continue;
                }

                /* Issue (57) for the ARB_uniform_buffer_object spec says that
                 * the buffer can be larger than the uniform data inside it,
                 * so clamp ubo size to what hardware supports. */

                pan_pack(ubo_ptr + ubo, UNIFORM_BUFFER, cfg) {
                        cfg.entries = MIN2(DIV_ROUND_UP(usz, 16), 1 << 12);
                        cfg.pointer = panfrost_map_constant_buffer_gpu(batch,
                                        stage, buf, ubo);
                }
        }

        /* Copy push constants required by the shader */
        struct panfrost_ptr push_transfer =
                panfrost_pool_alloc_aligned(&batch->pool,
                                            ss->info.push.count * 4, 16);

        uint32_t *push_cpu = (uint32_t *) push_transfer.cpu;
        *push_constants = push_transfer.gpu;

        for (unsigned i = 0; i < ss->info.push.count; ++i) {
                struct panfrost_ubo_word src = ss->info.push.words[i];

                /* Map the UBO, this should be cheap. However this is reading
                 * from write-combine memory which is _very_ slow. It might pay
                 * off to upload sysvals to a staging buffer on the CPU on the
                 * assumption sysvals will get pushed (TODO) */

                const void *mapped_ubo = (src.ubo == sysval_ubo) ? transfer.cpu :
                        panfrost_map_constant_buffer_cpu(ctx, buf, src.ubo);

                /* TODO: Is there any benefit to combining ranges */
                memcpy(push_cpu + i, (uint8_t *) mapped_ubo + src.offset, 4);
        }

        buf->dirty_mask = 0;
        return ubos.gpu;
}

mali_ptr
panfrost_emit_shared_memory(struct panfrost_batch *batch,
                            const struct pipe_grid_info *info)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_shader_variants *all = ctx->shader[PIPE_SHADER_COMPUTE];
        struct panfrost_shader_state *ss = &all->variants[all->active_variant];
        unsigned single_size = util_next_power_of_two(MAX2(ss->info.wls_size,
                                                           128));

        unsigned instances =
                util_next_power_of_two(info->grid[0]) *
                util_next_power_of_two(info->grid[1]) *
                util_next_power_of_two(info->grid[2]);

        unsigned shared_size = single_size * instances * dev->core_count;
        struct panfrost_bo *bo = panfrost_batch_get_shared_memory(batch,
                                                                  shared_size,
                                                                  1);
        struct panfrost_ptr t =
                panfrost_pool_alloc_desc(&batch->pool, LOCAL_STORAGE);

        pan_pack(t.cpu, LOCAL_STORAGE, ls) {
                ls.wls_base_pointer = bo->ptr.gpu;
                ls.wls_instances = instances;
                ls.wls_size_scale = util_logbase2(single_size) + 1;

                if (ss->info.tls_size) {
                        unsigned shift =
                                panfrost_get_stack_shift(ss->info.tls_size);
                        struct panfrost_bo *bo =
                                panfrost_batch_get_scratchpad(batch,
                                                              ss->info.tls_size,
                                                              dev->thread_tls_alloc,
                                                              dev->core_count);

                        ls.tls_size = shift;
                        ls.tls_base_pointer = bo->ptr.gpu;
                }
        };

        return t.gpu;
}

static mali_ptr
panfrost_get_tex_desc(struct panfrost_batch *batch,
                      enum pipe_shader_type st,
                      struct panfrost_sampler_view *view)
{
        if (!view)
                return (mali_ptr) 0;

        struct pipe_sampler_view *pview = &view->base;
        struct panfrost_resource *rsrc = pan_resource(pview->texture);

        /* Add the BO to the job so it's retained until the job is done. */

        panfrost_batch_add_bo(batch, rsrc->bo,
                              PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_READ |
                              panfrost_bo_access_for_stage(st));

        panfrost_batch_add_bo(batch, view->bo,
                              PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_READ |
                              panfrost_bo_access_for_stage(st));

        return view->bo->ptr.gpu;
}

static void
panfrost_update_sampler_view(struct panfrost_sampler_view *view,
                             struct pipe_context *pctx)
{
        struct panfrost_resource *rsrc = pan_resource(view->base.texture);
        if (view->texture_bo != rsrc->bo->ptr.gpu ||
            view->modifier != rsrc->layout.modifier) {
                panfrost_bo_unreference(view->bo);
                panfrost_create_sampler_view_bo(view, pctx, &rsrc->base);
        }
}

mali_ptr
panfrost_emit_texture_descriptors(struct panfrost_batch *batch,
                                  enum pipe_shader_type stage)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *device = pan_device(ctx->base.screen);

        if (!ctx->sampler_view_count[stage])
                return 0;

        if (pan_is_bifrost(device)) {
                struct panfrost_ptr T =
                        panfrost_pool_alloc_desc_array(&batch->pool,
                                                       ctx->sampler_view_count[stage],
                                                       BIFROST_TEXTURE);
                struct mali_bifrost_texture_packed *out =
                        (struct mali_bifrost_texture_packed *) T.cpu;

                for (int i = 0; i < ctx->sampler_view_count[stage]; ++i) {
                        struct panfrost_sampler_view *view = ctx->sampler_views[stage][i];
                        struct pipe_sampler_view *pview = &view->base;
                        struct panfrost_resource *rsrc = pan_resource(pview->texture);

                        panfrost_update_sampler_view(view, &ctx->base);
                        out[i] = view->bifrost_descriptor;

                        /* Add the BOs to the job so they are retained until the job is done. */

                        panfrost_batch_add_bo(batch, rsrc->bo,
                                              PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_READ |
                                              panfrost_bo_access_for_stage(stage));

                        panfrost_batch_add_bo(batch, view->bo,
                                              PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_READ |
                                              panfrost_bo_access_for_stage(stage));
                }

                return T.gpu;
        } else {
                uint64_t trampolines[PIPE_MAX_SHADER_SAMPLER_VIEWS];

                for (int i = 0; i < ctx->sampler_view_count[stage]; ++i) {
                        struct panfrost_sampler_view *view = ctx->sampler_views[stage][i];

                        panfrost_update_sampler_view(view, &ctx->base);

                        trampolines[i] = panfrost_get_tex_desc(batch, stage, view);
                }

                return panfrost_pool_upload_aligned(&batch->pool, trampolines,
                                sizeof(uint64_t) *
                                ctx->sampler_view_count[stage],
                                sizeof(uint64_t));
        }
}

mali_ptr
panfrost_emit_sampler_descriptors(struct panfrost_batch *batch,
                                  enum pipe_shader_type stage)
{
        struct panfrost_context *ctx = batch->ctx;

        if (!ctx->sampler_count[stage])
                return 0;

        assert(MALI_BIFROST_SAMPLER_LENGTH == MALI_MIDGARD_SAMPLER_LENGTH);
        assert(MALI_BIFROST_SAMPLER_ALIGN == MALI_MIDGARD_SAMPLER_ALIGN);

        struct panfrost_ptr T =
                panfrost_pool_alloc_desc_array(&batch->pool, ctx->sampler_count[stage],
                                               MIDGARD_SAMPLER);
        struct mali_midgard_sampler_packed *out = (struct mali_midgard_sampler_packed *) T.cpu;

        for (unsigned i = 0; i < ctx->sampler_count[stage]; ++i)
                out[i] = ctx->samplers[stage][i]->hw;

        return T.gpu;
}

/* Packs all image attribute descs and attribute buffer descs.
 * `first_image_buf_index` must be the index of the first image attribute buffer descriptor.
 */
static void
emit_image_attribs(struct panfrost_batch *batch, enum pipe_shader_type shader,
                   struct mali_attribute_packed *attribs,
                   struct mali_attribute_buffer_packed *bufs,
                   unsigned first_image_buf_index)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *dev = pan_device(ctx->base.screen);

        unsigned k = 0;
        unsigned last_bit = util_last_bit(ctx->image_mask[shader]);
        for (unsigned i = 0; i < last_bit; ++i) {
                struct pipe_image_view *image = &ctx->images[shader][i];

                /* TODO: understand how v3d/freedreno does it */
                if (!(ctx->image_mask[shader] & (1 << i)) ||
                    !(image->shader_access & PIPE_IMAGE_ACCESS_READ_WRITE)) {
                        /* Unused image bindings */
                        pan_pack(bufs + (k * 2), ATTRIBUTE_BUFFER, cfg);
                        pan_pack(bufs + (k * 2) + 1, ATTRIBUTE_BUFFER, cfg);
                        pan_pack(attribs + k, ATTRIBUTE, cfg);
                        k++;
                        continue;
                }

                struct panfrost_resource *rsrc = pan_resource(image->resource);

                /* TODO: MSAA */
                assert(image->resource->nr_samples <= 1 && "MSAA'd images not supported");

                bool is_3d = rsrc->base.target == PIPE_TEXTURE_3D;
                bool is_linear = rsrc->layout.modifier == DRM_FORMAT_MOD_LINEAR;
                bool is_buffer = rsrc->base.target == PIPE_BUFFER;

                unsigned offset = is_buffer ? image->u.buf.offset :
                        panfrost_texture_offset(&rsrc->layout,
                                                image->u.tex.level,
                                                is_3d ? 0 : image->u.tex.first_layer,
                                                is_3d ? image->u.tex.first_layer : 0);

                /* AFBC should've been converted to tiled on panfrost_set_shader_image */
                assert(!drm_is_afbc(rsrc->layout.modifier));

                /* Add a dependency of the batch on the shader image buffer */
                uint32_t flags = PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_VERTEX_TILER;
                if (image->shader_access & PIPE_IMAGE_ACCESS_READ)
                        flags |= PAN_BO_ACCESS_READ;
                if (image->shader_access & PIPE_IMAGE_ACCESS_WRITE) {
                        flags |= PAN_BO_ACCESS_WRITE;
                        unsigned level = is_buffer ? 0 : image->u.tex.level;
                        rsrc->layout.slices[level].initialized = true;
                }
                panfrost_batch_add_bo(batch, rsrc->bo, flags);

                pan_pack(bufs + (k * 2), ATTRIBUTE_BUFFER, cfg) {
                        cfg.type = is_linear ?
                                MALI_ATTRIBUTE_TYPE_3D_LINEAR :
                                MALI_ATTRIBUTE_TYPE_3D_INTERLEAVED;

                        cfg.pointer = rsrc->bo->ptr.gpu + offset;
                        cfg.stride = util_format_get_blocksize(image->format);
                        cfg.size = rsrc->bo->size;
                }

                pan_pack(bufs + (k * 2) + 1, ATTRIBUTE_BUFFER_CONTINUATION_3D, cfg) {
                        cfg.s_dimension = rsrc->base.width0;
                        cfg.t_dimension = rsrc->base.height0;
                        cfg.r_dimension = is_3d ? rsrc->base.depth0 :
                                image->u.tex.last_layer - image->u.tex.first_layer + 1;

                        cfg.row_stride =
                                is_buffer ? 0 : rsrc->layout.slices[image->u.tex.level].row_stride;

                        if (rsrc->base.target != PIPE_TEXTURE_2D && !is_buffer) {
                                cfg.slice_stride =
                                        panfrost_get_layer_stride(&rsrc->layout,
                                                                  image->u.tex.level);
                        }
                }

                /* We map compute shader attributes 1:2 with attribute buffers, because
                 * every image attribute buffer needs an ATTRIBUTE_BUFFER_CONTINUATION_3D */
                pan_pack(attribs + k, ATTRIBUTE, cfg) {
                        cfg.buffer_index = first_image_buf_index + (k * 2);
                        cfg.offset_enable = !pan_is_bifrost(dev);
                        cfg.format =
                                dev->formats[image->format].hw;
                }

                k++;
        }
}

mali_ptr
panfrost_emit_image_attribs(struct panfrost_batch *batch,
                            mali_ptr *buffers,
                            enum pipe_shader_type type)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_shader_state *shader = panfrost_get_shader_state(ctx, type);

        if (!shader->info.attribute_count) {
                *buffers = 0;
                return 0;
        }

        struct panfrost_device *dev = pan_device(ctx->base.screen);

        /* Images always need a MALI_ATTRIBUTE_BUFFER_CONTINUATION_3D */
        unsigned attr_count = shader->info.attribute_count;
        unsigned buf_count = (attr_count * 2) + (pan_is_bifrost(dev) ? 1 : 0);

        struct panfrost_ptr bufs =
                panfrost_pool_alloc_desc_array(&batch->pool, buf_count, ATTRIBUTE_BUFFER);

        struct panfrost_ptr attribs =
                panfrost_pool_alloc_desc_array(&batch->pool, attr_count, ATTRIBUTE);

        emit_image_attribs(batch, type, attribs.cpu, bufs.cpu, 0);

        /* We need an empty attrib buf to stop the prefetching on Bifrost */
        if (pan_is_bifrost(dev)) {
                pan_pack(bufs.cpu +
                         ((buf_count - 1) * MALI_ATTRIBUTE_BUFFER_LENGTH),
                         ATTRIBUTE_BUFFER, cfg);
        }

        *buffers = bufs.gpu;
        return attribs.gpu;
}

mali_ptr
panfrost_emit_vertex_data(struct panfrost_batch *batch,
                          mali_ptr *buffers)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_vertex_state *so = ctx->vertex;
        struct panfrost_shader_state *vs = panfrost_get_shader_state(ctx, PIPE_SHADER_VERTEX);
        uint32_t image_mask = ctx->image_mask[PIPE_SHADER_VERTEX];
        unsigned nr_images = util_bitcount(image_mask);

        /* Worst case: everything is NPOT, which is only possible if instancing
         * is enabled. Otherwise single record is gauranteed.
         * Also, we allocate more memory than what's needed here if either instancing
         * is enabled or images are present, this can be improved. */
        unsigned bufs_per_attrib = (ctx->instance_count > 1 || nr_images > 0) ? 2 : 1;
        unsigned nr_bufs = (vs->info.attribute_count * bufs_per_attrib) +
                           (pan_is_bifrost(dev) ? 1 : 0);

        if (!nr_bufs) {
                *buffers = 0;
                return 0;
        }

        struct panfrost_ptr S =
                panfrost_pool_alloc_desc_array(&batch->pool, nr_bufs,
                                               ATTRIBUTE_BUFFER);
        struct panfrost_ptr T =
                panfrost_pool_alloc_desc_array(&batch->pool,
                                               vs->info.attribute_count,
                                               ATTRIBUTE);

        struct mali_attribute_buffer_packed *bufs =
                (struct mali_attribute_buffer_packed *) S.cpu;

        struct mali_attribute_packed *out =
                (struct mali_attribute_packed *) T.cpu;

        unsigned attrib_to_buffer[PIPE_MAX_ATTRIBS] = { 0 };
        unsigned k = 0;

        for (unsigned i = 0; i < so->num_elements; ++i) {
                /* We map buffers 1:1 with the attributes, which
                 * means duplicating some vertex buffers (who cares? aside from
                 * maybe some caching implications but I somehow doubt that
                 * matters) */

                struct pipe_vertex_element *elem = &so->pipe[i];
                unsigned vbi = elem->vertex_buffer_index;
                attrib_to_buffer[i] = k;

                if (!(ctx->vb_mask & (1 << vbi)))
                        continue;

                struct pipe_vertex_buffer *buf = &ctx->vertex_buffers[vbi];
                struct panfrost_resource *rsrc;

                rsrc = pan_resource(buf->buffer.resource);
                if (!rsrc)
                        continue;

                /* Add a dependency of the batch on the vertex buffer */
                panfrost_batch_add_bo(batch, rsrc->bo,
                                      PAN_BO_ACCESS_SHARED |
                                      PAN_BO_ACCESS_READ |
                                      PAN_BO_ACCESS_VERTEX_TILER);

                /* Mask off lower bits, see offset fixup below */
                mali_ptr raw_addr = rsrc->bo->ptr.gpu + buf->buffer_offset;
                mali_ptr addr = raw_addr & ~63;

                /* Since we advanced the base pointer, we shrink the buffer
                 * size, but add the offset we subtracted */
                unsigned size = rsrc->base.width0 + (raw_addr - addr)
                        - buf->buffer_offset;

                /* When there is a divisor, the hardware-level divisor is
                 * the product of the instance divisor and the padded count */
                unsigned divisor = elem->instance_divisor;
                unsigned hw_divisor = ctx->padded_count * divisor;
                unsigned stride = buf->stride;

                /* If there's a divisor(=1) but no instancing, we want every
                 * attribute to be the same */

                if (divisor && ctx->instance_count == 1)
                        stride = 0;

                if (!divisor || ctx->instance_count <= 1) {
                        pan_pack(bufs + k, ATTRIBUTE_BUFFER, cfg) {
                                if (ctx->instance_count > 1) {
                                        cfg.type = MALI_ATTRIBUTE_TYPE_1D_MODULUS;
                                        cfg.divisor = ctx->padded_count;
                                }

                                cfg.pointer = addr;
                                cfg.stride = stride;
                                cfg.size = size;
                        }
                } else if (util_is_power_of_two_or_zero(hw_divisor)) {
                        pan_pack(bufs + k, ATTRIBUTE_BUFFER, cfg) {
                                cfg.type = MALI_ATTRIBUTE_TYPE_1D_POT_DIVISOR;
                                cfg.pointer = addr;
                                cfg.stride = stride;
                                cfg.size = size;
                                cfg.divisor_r = __builtin_ctz(hw_divisor);
                        }

                } else {
                        unsigned shift = 0, extra_flags = 0;

                        unsigned magic_divisor =
                                panfrost_compute_magic_divisor(hw_divisor, &shift, &extra_flags);

                        pan_pack(bufs + k, ATTRIBUTE_BUFFER, cfg) {
                                cfg.type = MALI_ATTRIBUTE_TYPE_1D_NPOT_DIVISOR;
                                cfg.pointer = addr;
                                cfg.stride = stride;
                                cfg.size = size;

                                cfg.divisor_r = shift;
                                cfg.divisor_e = extra_flags;
                        }

                        pan_pack(bufs + k + 1, ATTRIBUTE_BUFFER_CONTINUATION_NPOT, cfg) {
                                cfg.divisor_numerator = magic_divisor;
                                cfg.divisor = divisor;
                        }

                        ++k;
                }

                ++k;
        }

        /* Add special gl_VertexID/gl_InstanceID buffers */

        if (unlikely(vs->info.attribute_count >= PAN_VERTEX_ID)) {
                panfrost_vertex_id(ctx->padded_count, &bufs[k], ctx->instance_count > 1);

                pan_pack(out + PAN_VERTEX_ID, ATTRIBUTE, cfg) {
                        cfg.buffer_index = k++;
                        cfg.format = so->formats[PAN_VERTEX_ID];
                }

                panfrost_instance_id(ctx->padded_count, &bufs[k], ctx->instance_count > 1);

                pan_pack(out + PAN_INSTANCE_ID, ATTRIBUTE, cfg) {
                        cfg.buffer_index = k++;
                        cfg.format = so->formats[PAN_INSTANCE_ID];
                }
        }

        k = ALIGN_POT(k, 2);
        emit_image_attribs(batch, PIPE_SHADER_VERTEX, out + so->num_elements, bufs + k, k);
        k += util_bitcount(ctx->image_mask[PIPE_SHADER_VERTEX]);

        /* We need an empty attrib buf to stop the prefetching on Bifrost */
        if (pan_is_bifrost(dev))
                pan_pack(&bufs[k], ATTRIBUTE_BUFFER, cfg);

        /* Attribute addresses require 64-byte alignment, so let:
         *
         *      base' = base & ~63 = base - (base & 63)
         *      offset' = offset + (base & 63)
         *
         * Since base' + offset' = base + offset, these are equivalent
         * addressing modes and now base is 64 aligned.
         */

        for (unsigned i = 0; i < so->num_elements; ++i) {
                unsigned vbi = so->pipe[i].vertex_buffer_index;
                struct pipe_vertex_buffer *buf = &ctx->vertex_buffers[vbi];

                /* Adjust by the masked off bits of the offset. Make sure we
                 * read src_offset from so->hw (which is not GPU visible)
                 * rather than target (which is) due to caching effects */

                unsigned src_offset = so->pipe[i].src_offset;

                /* BOs aligned to 4k so guaranteed aligned to 64 */
                src_offset += (buf->buffer_offset & 63);

                /* Also, somewhat obscurely per-instance data needs to be
                 * offset in response to a delayed start in an indexed draw */

                if (so->pipe[i].instance_divisor && ctx->instance_count > 1)
                        src_offset -= buf->stride * ctx->offset_start;

                pan_pack(out + i, ATTRIBUTE, cfg) {
                        cfg.buffer_index = attrib_to_buffer[i];
                        cfg.format = so->formats[i];
                        cfg.offset = src_offset;
                }
        }

        *buffers = S.gpu;
        return T.gpu;
}

static mali_ptr
panfrost_emit_varyings(struct panfrost_batch *batch,
                struct mali_attribute_buffer_packed *slot,
                unsigned stride, unsigned count)
{
        unsigned size = stride * count;
        mali_ptr ptr = panfrost_pool_alloc_aligned(&batch->invisible_pool, size, 64).gpu;

        pan_pack(slot, ATTRIBUTE_BUFFER, cfg) {
                cfg.stride = stride;
                cfg.size = size;
                cfg.pointer = ptr;
        }

        return ptr;
}

static unsigned
panfrost_streamout_offset(unsigned stride,
                        struct pipe_stream_output_target *target)
{
        return (target->buffer_offset + (pan_so_target(target)->offset * stride * 4)) & 63;
}

static void
panfrost_emit_streamout(struct panfrost_batch *batch,
                        struct mali_attribute_buffer_packed *slot,
                        unsigned stride_words, unsigned count,
                        struct pipe_stream_output_target *target)
{
        unsigned stride = stride_words * 4;
        unsigned max_size = target->buffer_size;
        unsigned expected_size = stride * count;

        /* Grab the BO and bind it to the batch */
        struct panfrost_bo *bo = pan_resource(target->buffer)->bo;

        /* Varyings are WRITE from the perspective of the VERTEX but READ from
         * the perspective of the TILER and FRAGMENT.
         */
        panfrost_batch_add_bo(batch, bo,
                              PAN_BO_ACCESS_SHARED |
                              PAN_BO_ACCESS_RW |
                              PAN_BO_ACCESS_VERTEX_TILER |
                              PAN_BO_ACCESS_FRAGMENT);

        /* We will have an offset applied to get alignment */
        mali_ptr addr = bo->ptr.gpu + target->buffer_offset + (pan_so_target(target)->offset * stride);

        pan_pack(slot, ATTRIBUTE_BUFFER, cfg) {
                cfg.pointer = (addr & ~63);
                cfg.stride = stride;
                cfg.size = MIN2(max_size, expected_size) + (addr & 63);
        }
}

/* Helpers for manipulating stream out information so we can pack varyings
 * accordingly. Compute the src_offset for a given captured varying */

static struct pipe_stream_output *
pan_get_so(struct pipe_stream_output_info *info, gl_varying_slot loc)
{
        for (unsigned i = 0; i < info->num_outputs; ++i) {
                if (info->output[i].register_index == loc)
                        return &info->output[i];
        }

        unreachable("Varying not captured");
}

static unsigned
pan_varying_size(enum mali_format fmt)
{
        unsigned type = MALI_EXTRACT_TYPE(fmt);
        unsigned chan = MALI_EXTRACT_CHANNELS(fmt);
        unsigned bits = MALI_EXTRACT_BITS(fmt);
        unsigned bpc = 0;

        if (bits == MALI_CHANNEL_FLOAT) {
                /* No doubles */
                bool fp16 = (type == MALI_FORMAT_SINT);
                assert(fp16 || (type == MALI_FORMAT_UNORM));

                bpc = fp16 ? 2 : 4;
        } else {
                assert(type >= MALI_FORMAT_SNORM && type <= MALI_FORMAT_SINT);

                /* See the enums */
                bits = 1 << bits;
                assert(bits >= 8);
                bpc = bits / 8;
        }

        return bpc * chan;
}

/* Given a varying, figure out which index it corresponds to */

static inline unsigned
pan_varying_index(unsigned present, enum pan_special_varying v)
{
        unsigned mask = (1 << v) - 1;
        return util_bitcount(present & mask);
}

/* Get the base offset for XFB buffers, which by convention come after
 * everything else. Wrapper function for semantic reasons; by construction this
 * is just popcount. */

static inline unsigned
pan_xfb_base(unsigned present)
{
        return util_bitcount(present);
}

/* Computes the present mask for varyings so we can start emitting varying records */

static inline unsigned
pan_varying_present(const struct panfrost_device *dev,
                    struct panfrost_shader_state *vs,
                    struct panfrost_shader_state *fs,
                    uint16_t point_coord_mask)
{
        /* At the moment we always emit general and position buffers. Not
         * strictly necessary but usually harmless */

        unsigned present = (1 << PAN_VARY_GENERAL) | (1 << PAN_VARY_POSITION);

        /* Enable special buffers by the shader info */

        if (vs->info.vs.writes_point_size)
                present |= (1 << PAN_VARY_PSIZ);

        if (fs->info.fs.reads_point_coord)
                present |= (1 << PAN_VARY_PNTCOORD);

        if (fs->info.fs.reads_face)
                present |= (1 << PAN_VARY_FACE);

        if (fs->info.fs.reads_frag_coord && !pan_is_bifrost(dev))
                present |= (1 << PAN_VARY_FRAGCOORD);

        /* Also, if we have a point sprite, we need a point coord buffer */

        for (unsigned i = 0; i < fs->info.varyings.input_count; i++)  {
                gl_varying_slot loc = fs->info.varyings.input[i].location;

                if (util_varying_is_point_coord(loc, point_coord_mask))
                        present |= (1 << PAN_VARY_PNTCOORD);
        }

        return present;
}

/* Emitters for varying records */

static void
pan_emit_vary(const struct panfrost_device *dev,
              struct mali_attribute_packed *out,
              unsigned present, enum pan_special_varying buf,
              enum mali_format format, unsigned offset)
{
        unsigned nr_channels = MALI_EXTRACT_CHANNELS(format);
        unsigned swizzle = dev->quirks & HAS_SWIZZLES ?
                           panfrost_get_default_swizzle(nr_channels) :
                           panfrost_bifrost_swizzle(nr_channels);

        pan_pack(out, ATTRIBUTE, cfg) {
                cfg.buffer_index = pan_varying_index(present, buf);
                cfg.offset_enable = !pan_is_bifrost(dev);
                cfg.format = (format << 12) | swizzle;
                cfg.offset = offset;
        }
}

/* General varying that is unused */

static void
pan_emit_vary_only(const struct panfrost_device *dev,
                   struct mali_attribute_packed *out,
                   unsigned present)
{
        pan_emit_vary(dev, out, present, 0, MALI_CONSTANT, 0);
}

/* Special records */

static const enum mali_format pan_varying_formats[PAN_VARY_MAX] = {
        [PAN_VARY_POSITION]     = MALI_SNAP_4,
        [PAN_VARY_PSIZ]         = MALI_R16F,
        [PAN_VARY_PNTCOORD]     = MALI_R16F,
        [PAN_VARY_FACE]         = MALI_R32I,
        [PAN_VARY_FRAGCOORD]    = MALI_RGBA32F
};

static void
pan_emit_vary_special(const struct panfrost_device *dev,
                      struct mali_attribute_packed *out,
                      unsigned present, enum pan_special_varying buf)
{
        assert(buf < PAN_VARY_MAX);
        pan_emit_vary(dev, out, present, buf, pan_varying_formats[buf], 0);
}

static enum mali_format
pan_xfb_format(enum mali_format format, unsigned nr)
{
        if (MALI_EXTRACT_BITS(format) == MALI_CHANNEL_FLOAT)
                return MALI_R32F | MALI_NR_CHANNELS(nr);
        else
                return MALI_EXTRACT_TYPE(format) | MALI_NR_CHANNELS(nr) | MALI_CHANNEL_32;
}

/* Transform feedback records. Note struct pipe_stream_output is (if packed as
 * a bitfield) 32-bit, smaller than a 64-bit pointer, so may as well pass by
 * value. */

static void
pan_emit_vary_xfb(const struct panfrost_device *dev,
                  struct mali_attribute_packed *out,
                  unsigned present,
                  unsigned max_xfb,
                  unsigned *streamout_offsets,
                  enum mali_format format,
                  struct pipe_stream_output o)
{
        unsigned swizzle = dev->quirks & HAS_SWIZZLES ?
                           panfrost_get_default_swizzle(o.num_components) :
                           panfrost_bifrost_swizzle(o.num_components);

        pan_pack(out, ATTRIBUTE, cfg) {
                /* XFB buffers come after everything else */
                cfg.buffer_index = pan_xfb_base(present) + o.output_buffer;
                cfg.offset_enable = !pan_is_bifrost(dev);

                /* Override number of channels and precision to highp */
                cfg.format = (pan_xfb_format(format, o.num_components) << 12) | swizzle;

                /* Apply given offsets together */
                cfg.offset = (o.dst_offset * 4) /* dwords */
                        + streamout_offsets[o.output_buffer];
        }
}

/* Determine if we should capture a varying for XFB. This requires actually
 * having a buffer for it. If we don't capture it, we'll fallback to a general
 * varying path (linked or unlinked, possibly discarding the write) */

static bool
panfrost_xfb_captured(struct panfrost_shader_state *xfb,
                unsigned loc, unsigned max_xfb)
{
        if (!(xfb->so_mask & (1ll << loc)))
                return false;

        struct pipe_stream_output *o = pan_get_so(&xfb->stream_output, loc);
        return o->output_buffer < max_xfb;
}

static void
pan_emit_general_varying(const struct panfrost_device *dev,
                         struct mali_attribute_packed *out,
                         struct panfrost_shader_state *other,
                         struct panfrost_shader_state *xfb,
                         gl_varying_slot loc,
                         enum mali_format format,
                         unsigned present,
                         unsigned *gen_offsets,
                         enum mali_format *gen_formats,
                         unsigned *gen_stride,
                         unsigned idx,
                         bool should_alloc)
{
        /* Check if we're linked */
        unsigned other_varying_count =
                other->info.stage == MESA_SHADER_FRAGMENT ?
                other->info.varyings.input_count :
                other->info.varyings.output_count;
        const struct pan_shader_varying *other_varyings =
                other->info.stage == MESA_SHADER_FRAGMENT ?
                other->info.varyings.input :
                other->info.varyings.output;
        signed other_idx = -1;

        for (unsigned j = 0; j < other_varying_count; ++j) {
                if (other_varyings[j].location == loc) {
                        other_idx = j;
                        break;
                }
        }

        if (other_idx < 0) {
                pan_emit_vary_only(dev, out, present);
                return;
        }

        unsigned offset = gen_offsets[other_idx];

        if (should_alloc) {
                /* We're linked, so allocate a space via a watermark allocation */
                enum mali_format alt =
                        dev->formats[other_varyings[other_idx].format].hw >> 12;

                /* Do interpolation at minimum precision */
                unsigned size_main = pan_varying_size(format);
                unsigned size_alt = pan_varying_size(alt);
                unsigned size = MIN2(size_main, size_alt);

                /* If a varying is marked for XFB but not actually captured, we
                 * should match the format to the format that would otherwise
                 * be used for XFB, since dEQP checks for invariance here. It's
                 * unclear if this is required by the spec. */

                if (xfb->so_mask & (1ull << loc)) {
                        struct pipe_stream_output *o = pan_get_so(&xfb->stream_output, loc);
                        format = pan_xfb_format(format, o->num_components);
                        size = pan_varying_size(format);
                } else if (size == size_alt) {
                        format = alt;
                }

                gen_offsets[idx] = *gen_stride;
                gen_formats[other_idx] = format;
                offset = *gen_stride;
                *gen_stride += size;
        }

        pan_emit_vary(dev, out, present, PAN_VARY_GENERAL, format, offset);
}

/* Higher-level wrapper around all of the above, classifying a varying into one
 * of the above types */

static void
panfrost_emit_varying(const struct panfrost_device *dev,
                      struct mali_attribute_packed *out,
                      struct panfrost_shader_state *stage,
                      struct panfrost_shader_state *other,
                      struct panfrost_shader_state *xfb,
                      unsigned present,
                      uint16_t point_sprite_mask,
                      unsigned max_xfb,
                      unsigned *streamout_offsets,
                      unsigned *gen_offsets,
                      enum mali_format *gen_formats,
                      unsigned *gen_stride,
                      unsigned idx,
                      bool should_alloc,
                      bool is_fragment)
{
        gl_varying_slot loc =
                stage->info.stage == MESA_SHADER_FRAGMENT ?
                stage->info.varyings.input[idx].location :
                stage->info.varyings.output[idx].location;
        enum mali_format format =
                stage->info.stage == MESA_SHADER_FRAGMENT ?
                dev->formats[stage->info.varyings.input[idx].format].hw >> 12 :
                dev->formats[stage->info.varyings.output[idx].format].hw >> 12;

        /* Override format to match linkage */
        if (!should_alloc && gen_formats[idx])
                format = gen_formats[idx];

        if (util_varying_is_point_coord(loc, point_sprite_mask)) {
                pan_emit_vary_special(dev, out, present, PAN_VARY_PNTCOORD);
        } else if (panfrost_xfb_captured(xfb, loc, max_xfb)) {
                struct pipe_stream_output *o = pan_get_so(&xfb->stream_output, loc);
                pan_emit_vary_xfb(dev, out, present, max_xfb, streamout_offsets, format, *o);
        } else if (loc == VARYING_SLOT_POS) {
                if (is_fragment)
                        pan_emit_vary_special(dev, out, present, PAN_VARY_FRAGCOORD);
                else
                        pan_emit_vary_special(dev, out, present, PAN_VARY_POSITION);
        } else if (loc == VARYING_SLOT_PSIZ) {
                pan_emit_vary_special(dev, out, present, PAN_VARY_PSIZ);
        } else if (loc == VARYING_SLOT_PNTC) {
                pan_emit_vary_special(dev, out, present, PAN_VARY_PNTCOORD);
        } else if (loc == VARYING_SLOT_FACE) {
                pan_emit_vary_special(dev, out, present, PAN_VARY_FACE);
        } else {
                pan_emit_general_varying(dev, out, other, xfb, loc, format, present,
                                         gen_offsets, gen_formats, gen_stride,
                                         idx, should_alloc);
        }
}

static void
pan_emit_special_input(struct mali_attribute_buffer_packed *out,
                unsigned present,
                enum pan_special_varying v,
                unsigned special)
{
        if (present & (1 << v)) {
                unsigned idx = pan_varying_index(present, v);

                pan_pack(out + idx, ATTRIBUTE_BUFFER, cfg) {
                        cfg.special = special;
                        cfg.type = 0;
                }
        }
}

void
panfrost_emit_varying_descriptor(struct panfrost_batch *batch,
                                 unsigned vertex_count,
                                 mali_ptr *vs_attribs,
                                 mali_ptr *fs_attribs,
                                 mali_ptr *buffers,
                                 mali_ptr *position,
                                 mali_ptr *psiz)
{
        /* Load the shaders */
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_shader_state *vs, *fs;
        size_t vs_size;

        /* Allocate the varying descriptor */

        vs = panfrost_get_shader_state(ctx, PIPE_SHADER_VERTEX);
        fs = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);
        vs_size = MALI_ATTRIBUTE_LENGTH * vs->info.varyings.output_count;

        struct panfrost_ptr trans =
                panfrost_pool_alloc_desc_array(&batch->pool,
                                               vs->info.varyings.output_count +
                                               fs->info.varyings.input_count,
                                               ATTRIBUTE);

        struct pipe_stream_output_info *so = &vs->stream_output;
        uint16_t point_coord_mask = ctx->rasterizer->base.sprite_coord_enable;

        /* TODO: point sprites need lowering on Bifrost */
        if (pan_is_bifrost(dev))
                point_coord_mask =  0;

        unsigned present = pan_varying_present(dev, vs, fs, point_coord_mask);

        /* Check if this varying is linked by us. This is the case for
         * general-purpose, non-captured varyings. If it is, link it. If it's
         * not, use the provided stream out information to determine the
         * offset, since it was already linked for us. */

        unsigned gen_offsets[32];
        enum mali_format gen_formats[32];
        memset(gen_offsets, 0, sizeof(gen_offsets));
        memset(gen_formats, 0, sizeof(gen_formats));

        unsigned gen_stride = 0;
        assert(vs->info.varyings.output_count < ARRAY_SIZE(gen_offsets));
        assert(fs->info.varyings.input_count < ARRAY_SIZE(gen_offsets));

        unsigned streamout_offsets[32];

        for (unsigned i = 0; i < ctx->streamout.num_targets; ++i) {
                streamout_offsets[i] = panfrost_streamout_offset(
                                        so->stride[i],
                                        ctx->streamout.targets[i]);
        }

        struct mali_attribute_packed *ovs = (struct mali_attribute_packed *)trans.cpu;
        struct mali_attribute_packed *ofs = ovs + vs->info.varyings.output_count;

        for (unsigned i = 0; i < vs->info.varyings.output_count; i++) {
                panfrost_emit_varying(dev, ovs + i, vs, fs, vs, present, 0,
                                      ctx->streamout.num_targets, streamout_offsets,
                                      gen_offsets, gen_formats, &gen_stride, i,
                                      true, false);
        }

        for (unsigned i = 0; i < fs->info.varyings.input_count; i++) {
                panfrost_emit_varying(dev, ofs + i, fs, vs, vs, present, point_coord_mask,
                                      ctx->streamout.num_targets, streamout_offsets,
                                      gen_offsets, gen_formats, &gen_stride, i,
                                      false, true);
        }

        unsigned xfb_base = pan_xfb_base(present);
        struct panfrost_ptr T =
                panfrost_pool_alloc_desc_array(&batch->pool,
                                               xfb_base +
                                               ctx->streamout.num_targets + 1,
                                               ATTRIBUTE_BUFFER);
        struct mali_attribute_buffer_packed *varyings =
                (struct mali_attribute_buffer_packed *) T.cpu;

        /* Suppress prefetch on Bifrost */
        memset(varyings + (xfb_base * ctx->streamout.num_targets), 0, sizeof(*varyings));

        /* Emit the stream out buffers */

        unsigned out_count = u_stream_outputs_for_vertices(ctx->active_prim,
                                                           ctx->vertex_count);

        for (unsigned i = 0; i < ctx->streamout.num_targets; ++i) {
                panfrost_emit_streamout(batch, &varyings[xfb_base + i],
                                        so->stride[i],
                                        out_count,
                                        ctx->streamout.targets[i]);
        }

        panfrost_emit_varyings(batch,
                        &varyings[pan_varying_index(present, PAN_VARY_GENERAL)],
                        gen_stride, vertex_count);

        /* fp32 vec4 gl_Position */
        *position = panfrost_emit_varyings(batch,
                        &varyings[pan_varying_index(present, PAN_VARY_POSITION)],
                        sizeof(float) * 4, vertex_count);

        if (present & (1 << PAN_VARY_PSIZ)) {
                *psiz = panfrost_emit_varyings(batch,
                                &varyings[pan_varying_index(present, PAN_VARY_PSIZ)],
                                2, vertex_count);
        }

        pan_emit_special_input(varyings, present, PAN_VARY_PNTCOORD, MALI_ATTRIBUTE_SPECIAL_POINT_COORD);
        pan_emit_special_input(varyings, present, PAN_VARY_FACE, MALI_ATTRIBUTE_SPECIAL_FRONT_FACING);
        pan_emit_special_input(varyings, present, PAN_VARY_FRAGCOORD, MALI_ATTRIBUTE_SPECIAL_FRAG_COORD);

        *buffers = T.gpu;
        *vs_attribs = vs->info.varyings.output_count ? trans.gpu : 0;
        *fs_attribs = fs->info.varyings.input_count ? trans.gpu + vs_size : 0;
}

void
panfrost_emit_vertex_tiler_jobs(struct panfrost_batch *batch,
                                const struct panfrost_ptr *vertex_job,
                                const struct panfrost_ptr *tiler_job)
{
        struct panfrost_context *ctx = batch->ctx;

        /* If rasterizer discard is enable, only submit the vertex */

        unsigned vertex = panfrost_add_job(&batch->pool, &batch->scoreboard,
                                           MALI_JOB_TYPE_VERTEX, false, false,
                                           0, 0, vertex_job, false);

        if (ctx->rasterizer->base.rasterizer_discard)
                return;

        panfrost_add_job(&batch->pool, &batch->scoreboard, MALI_JOB_TYPE_TILER,
                         false, false, vertex, 0, tiler_job, false);
}
