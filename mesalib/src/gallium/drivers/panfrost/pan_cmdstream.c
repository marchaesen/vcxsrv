/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2020 Collabora Ltd.
 * Copyright © 2017 Intel Corporation
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
#include "util/u_draw.h"
#include "util/u_memory.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "gallium/auxiliary/util/u_blend.h"

#include "panfrost-quirks.h"
#include "genxml/gen_macros.h"

#include "pan_pool.h"
#include "pan_bo.h"
#include "pan_blend.h"
#include "pan_context.h"
#include "pan_job.h"
#include "pan_shader.h"
#include "pan_texture.h"
#include "pan_util.h"
#include "pan_indirect_draw.h"
#include "pan_indirect_dispatch.h"
#include "pan_blitter.h"

struct panfrost_rasterizer {
        struct pipe_rasterizer_state base;

        /* Partially packed RSD words */
        struct mali_multisample_misc_packed multisample;
        struct mali_stencil_mask_misc_packed stencil_misc;
};

struct panfrost_zsa_state {
        struct pipe_depth_stencil_alpha_state base;

        /* Is any depth, stencil, or alpha testing enabled? */
        bool enabled;

        /* Mask of PIPE_CLEAR_{DEPTH,STENCIL} written */
        unsigned draws;

        /* Prepacked words from the RSD */
        struct mali_multisample_misc_packed rsd_depth;
        struct mali_stencil_mask_misc_packed rsd_stencil;
        struct mali_stencil_packed stencil_front, stencil_back;
};

struct panfrost_sampler_state {
        struct pipe_sampler_state base;
        struct mali_sampler_packed hw;
};

/* Misnomer: Sampler view corresponds to textures, not samplers */

struct panfrost_sampler_view {
        struct pipe_sampler_view base;
        struct panfrost_pool_ref state;
        struct mali_texture_packed bifrost_descriptor;
        mali_ptr texture_bo;
        uint64_t modifier;
};

/* Statically assert that PIPE_* enums match the hardware enums.
 * (As long as they match, we don't need to translate them.)
 */
UNUSED static void
pan_pipe_asserts()
{
#define PIPE_ASSERT(x) STATIC_ASSERT((int)x)

        /* Compare functions are natural in both Gallium and Mali */
        PIPE_ASSERT(PIPE_FUNC_NEVER    == MALI_FUNC_NEVER);
        PIPE_ASSERT(PIPE_FUNC_LESS     == MALI_FUNC_LESS);
        PIPE_ASSERT(PIPE_FUNC_EQUAL    == MALI_FUNC_EQUAL);
        PIPE_ASSERT(PIPE_FUNC_LEQUAL   == MALI_FUNC_LEQUAL);
        PIPE_ASSERT(PIPE_FUNC_GREATER  == MALI_FUNC_GREATER);
        PIPE_ASSERT(PIPE_FUNC_NOTEQUAL == MALI_FUNC_NOT_EQUAL);
        PIPE_ASSERT(PIPE_FUNC_GEQUAL   == MALI_FUNC_GEQUAL);
        PIPE_ASSERT(PIPE_FUNC_ALWAYS   == MALI_FUNC_ALWAYS);
}

static inline enum mali_sample_pattern
panfrost_sample_pattern(unsigned samples)
{
        switch (samples) {
        case 1:  return MALI_SAMPLE_PATTERN_SINGLE_SAMPLED;
        case 4:  return MALI_SAMPLE_PATTERN_ROTATED_4X_GRID;
        case 8:  return MALI_SAMPLE_PATTERN_D3D_8X_GRID;
        case 16: return MALI_SAMPLE_PATTERN_D3D_16X_GRID;
        default: unreachable("Unsupported sample count");
        }
}

static unsigned
translate_tex_wrap(enum pipe_tex_wrap w, bool using_nearest)
{
        /* Bifrost doesn't support the GL_CLAMP wrap mode, so instead use
         * CLAMP_TO_EDGE and CLAMP_TO_BORDER. On Midgard, CLAMP is broken for
         * nearest filtering, so use CLAMP_TO_EDGE in that case. */

        switch (w) {
        case PIPE_TEX_WRAP_REPEAT: return MALI_WRAP_MODE_REPEAT;
        case PIPE_TEX_WRAP_CLAMP:
                return using_nearest ? MALI_WRAP_MODE_CLAMP_TO_EDGE :
#if PAN_ARCH <= 5
                     MALI_WRAP_MODE_CLAMP;
#else
                     MALI_WRAP_MODE_CLAMP_TO_BORDER;
#endif
        case PIPE_TEX_WRAP_CLAMP_TO_EDGE: return MALI_WRAP_MODE_CLAMP_TO_EDGE;
        case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return MALI_WRAP_MODE_CLAMP_TO_BORDER;
        case PIPE_TEX_WRAP_MIRROR_REPEAT: return MALI_WRAP_MODE_MIRRORED_REPEAT;
        case PIPE_TEX_WRAP_MIRROR_CLAMP:
                return using_nearest ? MALI_WRAP_MODE_MIRRORED_CLAMP_TO_EDGE :
#if PAN_ARCH <= 5
                     MALI_WRAP_MODE_MIRRORED_CLAMP;
#else
                     MALI_WRAP_MODE_MIRRORED_CLAMP_TO_BORDER;
#endif
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
        return !cso->compare_mode ? MALI_FUNC_NEVER :
                panfrost_flip_compare_func((enum mali_func) cso->compare_func);
}

static enum mali_mipmap_mode
pan_pipe_to_mipmode(enum pipe_tex_mipfilter f)
{
        switch (f) {
        case PIPE_TEX_MIPFILTER_NEAREST: return MALI_MIPMAP_MODE_NEAREST;
        case PIPE_TEX_MIPFILTER_LINEAR: return MALI_MIPMAP_MODE_TRILINEAR;
#if PAN_ARCH >= 6
        case PIPE_TEX_MIPFILTER_NONE: return MALI_MIPMAP_MODE_NONE;
#else
        case PIPE_TEX_MIPFILTER_NONE: return MALI_MIPMAP_MODE_NEAREST;
#endif
        default: unreachable("Invalid");
        }
}


static void *
panfrost_create_sampler_state(
        struct pipe_context *pctx,
        const struct pipe_sampler_state *cso)
{
        struct panfrost_sampler_state *so = CALLOC_STRUCT(panfrost_sampler_state);
        so->base = *cso;

        bool using_nearest = cso->min_img_filter == PIPE_TEX_MIPFILTER_NEAREST;

        pan_pack(&so->hw, SAMPLER, cfg) {
                cfg.magnify_nearest = cso->mag_img_filter == PIPE_TEX_FILTER_NEAREST;
                cfg.minify_nearest = cso->min_img_filter == PIPE_TEX_FILTER_NEAREST;

                cfg.normalized_coordinates = cso->normalized_coords;
                cfg.lod_bias = FIXED_16(cso->lod_bias, true);
                cfg.minimum_lod = FIXED_16(cso->min_lod, false);
                cfg.maximum_lod = FIXED_16(cso->max_lod, false);

                cfg.wrap_mode_s = translate_tex_wrap(cso->wrap_s, using_nearest);
                cfg.wrap_mode_t = translate_tex_wrap(cso->wrap_t, using_nearest);
                cfg.wrap_mode_r = translate_tex_wrap(cso->wrap_r, using_nearest);

                cfg.mipmap_mode = pan_pipe_to_mipmode(cso->min_mip_filter);
                cfg.compare_function = panfrost_sampler_compare_func(cso);
                cfg.seamless_cube_map = cso->seamless_cube_map;

                cfg.border_color_r = cso->border_color.ui[0];
                cfg.border_color_g = cso->border_color.ui[1];
                cfg.border_color_b = cso->border_color.ui[2];
                cfg.border_color_a = cso->border_color.ui[3];

#if PAN_ARCH >= 6
                if (cso->max_anisotropy > 1) {
                        cfg.maximum_anisotropy = cso->max_anisotropy;
                        cfg.lod_algorithm = MALI_LOD_ALGORITHM_ANISOTROPIC;
                }
#else
                /* Emulate disabled mipmapping by clamping the LOD as tight as
                 * possible (from 0 to epsilon = 1/256) */
                if (cso->min_mip_filter == PIPE_TEX_MIPFILTER_NONE)
                        cfg.maximum_lod = cfg.minimum_lod + 1;
#endif
        }

        return so;
}

static bool
panfrost_fs_required(
                struct panfrost_shader_state *fs,
                struct panfrost_blend_state *blend,
                struct pipe_framebuffer_state *state,
                const struct panfrost_zsa_state *zsa)
{
        /* If we generally have side effects. This inclues use of discard,
         * which can affect the results of an occlusion query. */
        if (fs->info.fs.sidefx)
                return true;

        /* Using an empty FS requires early-z to be enabled, but alpha test
         * needs it disabled */
        if ((enum mali_func) zsa->base.alpha_func != MALI_FUNC_ALWAYS)
                return true;

        /* If colour is written we need to execute */
        for (unsigned i = 0; i < state->nr_cbufs; ++i) {
                if (state->cbufs[i] && !blend->info[i].no_colour)
                        return true;
        }

        /* If depth is written and not implied we need to execute.
         * TODO: Predicate on Z/S writes being enabled */
        return (fs->info.fs.writes_depth || fs->info.fs.writes_stencil);
}

#if PAN_ARCH >= 5
UNUSED static uint16_t
pack_blend_constant(enum pipe_format format, float cons)
{
        const struct util_format_description *format_desc =
                util_format_description(format);

        unsigned chan_size = 0;

        for (unsigned i = 0; i < format_desc->nr_channels; i++)
                chan_size = MAX2(format_desc->channel[0].size, chan_size);

        uint16_t unorm = (cons * ((1 << chan_size) - 1));
        return unorm << (16 - chan_size);
}

static void
panfrost_emit_blend(struct panfrost_batch *batch, void *rts, mali_ptr *blend_shaders)
{
        unsigned rt_count = batch->key.nr_cbufs;
        struct panfrost_context *ctx = batch->ctx;
        const struct panfrost_blend_state *so = ctx->blend;
        bool dithered = so->base.dither;

        /* Always have at least one render target for depth-only passes */
        for (unsigned i = 0; i < MAX2(rt_count, 1); ++i) {
                struct mali_blend_packed *packed = rts + (i * pan_size(BLEND));

                /* Disable blending for unbacked render targets */
                if (rt_count == 0 || !batch->key.cbufs[i] || so->info[i].no_colour) {
                        pan_pack(rts + i * pan_size(BLEND), BLEND, cfg) {
                                cfg.enable = false;
#if PAN_ARCH >= 6
                                cfg.internal.mode = MALI_BLEND_MODE_OFF;
#endif
                        }

                        continue;
                }

                struct pan_blend_info info = so->info[i];
                enum pipe_format format = batch->key.cbufs[i]->format;
                float cons = pan_blend_get_constant(info.constant_mask,
                                                    ctx->blend_color.color);

                /* Word 0: Flags and constant */
                pan_pack(packed, BLEND, cfg) {
                        cfg.srgb = util_format_is_srgb(format);
                        cfg.load_destination = info.load_dest;
                        cfg.round_to_fb_precision = !dithered;
                        cfg.alpha_to_one = ctx->blend->base.alpha_to_one;
#if PAN_ARCH >= 6
                        cfg.constant = pack_blend_constant(format, cons);
#else
                        cfg.blend_shader = (blend_shaders[i] != 0);

                        if (blend_shaders[i])
                                cfg.shader_pc = blend_shaders[i];
                        else
                                cfg.constant = cons;
#endif
                }

                if (!blend_shaders[i]) {
                        /* Word 1: Blend Equation */
                        STATIC_ASSERT(pan_size(BLEND_EQUATION) == 4);
                        packed->opaque[PAN_ARCH >= 6 ? 1 : 2] = so->equation[i];
                }

#if PAN_ARCH >= 6
                const struct panfrost_device *dev = pan_device(ctx->base.screen);
                struct panfrost_shader_state *fs =
                        panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);

                /* Words 2 and 3: Internal blend */
                if (blend_shaders[i]) {
                        /* The blend shader's address needs to be at
                         * the same top 32 bit as the fragment shader.
                         * TODO: Ensure that's always the case.
                         */
                        assert(!fs->bin.bo ||
                                        (blend_shaders[i] & (0xffffffffull << 32)) ==
                                        (fs->bin.gpu & (0xffffffffull << 32)));

                        unsigned ret_offset = fs->info.bifrost.blend[i].return_offset;
                        assert(!(ret_offset & 0x7));

                        pan_pack(&packed->opaque[2], INTERNAL_BLEND, cfg) {
                                cfg.mode = MALI_BLEND_MODE_SHADER;
                                cfg.shader.pc = (u32) blend_shaders[i];
                                cfg.shader.return_value = ret_offset ?
                                        fs->bin.gpu + ret_offset : 0;
                        }
                } else {
                        pan_pack(&packed->opaque[2], INTERNAL_BLEND, cfg) {
                                cfg.mode = info.opaque ?
                                        MALI_BLEND_MODE_OPAQUE :
                                        MALI_BLEND_MODE_FIXED_FUNCTION;

                                /* If we want the conversion to work properly,
                                 * num_comps must be set to 4
                                 */
                                cfg.fixed_function.num_comps = 4;
                                cfg.fixed_function.conversion.memory_format =
                                        panfrost_format_to_bifrost_blend(dev, format, dithered);
                                cfg.fixed_function.conversion.register_format =
                                        fs->info.bifrost.blend[i].format;
                                cfg.fixed_function.rt = i;
                        }
                }
#endif
        }

        for (unsigned i = 0; i < batch->key.nr_cbufs; ++i) {
                if (!so->info[i].no_colour && batch->key.cbufs[i]) {
                        batch->draws |= (PIPE_CLEAR_COLOR0 << i);
                        batch->resolve |= (PIPE_CLEAR_COLOR0 << i);
                }
        }
}
#endif

/* Construct a partial RSD corresponding to no executed fragment shader, and
 * merge with the existing partial RSD. */

static void
pan_merge_empty_fs(struct mali_renderer_state_packed *rsd)
{
        struct mali_renderer_state_packed empty_rsd;

        pan_pack(&empty_rsd, RENDERER_STATE, cfg) {
#if PAN_ARCH >= 6
                cfg.properties.shader_modifies_coverage = true;
                cfg.properties.allow_forward_pixel_to_kill = true;
                cfg.properties.allow_forward_pixel_to_be_killed = true;
                cfg.properties.zs_update_operation = MALI_PIXEL_KILL_STRONG_EARLY;
#else
                cfg.shader.shader = 0x1;
                cfg.properties.work_register_count = 1;
                cfg.properties.depth_source = MALI_DEPTH_SOURCE_FIXED_FUNCTION;
                cfg.properties.force_early_z = true;
#endif
        }

        pan_merge((*rsd), empty_rsd, RENDERER_STATE);
}

static void
panfrost_prepare_fs_state(struct panfrost_context *ctx,
                          mali_ptr *blend_shaders,
                          struct mali_renderer_state_packed *rsd)
{
        struct pipe_rasterizer_state *rast = &ctx->rasterizer->base;
        const struct panfrost_zsa_state *zsa = ctx->depth_stencil;
        struct panfrost_shader_state *fs = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);
        struct panfrost_blend_state *so = ctx->blend;
        bool alpha_to_coverage = ctx->blend->base.alpha_to_coverage;
        bool msaa = rast->multisample;

        unsigned rt_count = ctx->pipe_framebuffer.nr_cbufs;

        bool has_blend_shader = false;

        for (unsigned c = 0; c < rt_count; ++c)
                has_blend_shader |= (blend_shaders[c] != 0);

        pan_pack(rsd, RENDERER_STATE, cfg) {
                if (panfrost_fs_required(fs, so, &ctx->pipe_framebuffer, zsa)) {
#if PAN_ARCH >= 6
                        /* Track if any colour buffer is reused across draws, either
                         * from reading it directly, or from failing to write it */
                        unsigned rt_mask = ctx->fb_rt_mask;
                        uint64_t rt_written = (fs->info.outputs_written >> FRAG_RESULT_DATA0);
                        bool blend_reads_dest = (so->load_dest_mask & rt_mask);

                        cfg.properties.allow_forward_pixel_to_kill =
                                fs->info.fs.can_fpk &&
                                !(rt_mask & ~rt_written) &&
                                !alpha_to_coverage &&
                                !blend_reads_dest;
#else
                        cfg.properties.force_early_z =
                                fs->info.fs.can_early_z && !alpha_to_coverage &&
                                ((enum mali_func) zsa->base.alpha_func == MALI_FUNC_ALWAYS);

                        /* TODO: Reduce this limit? */
                        if (has_blend_shader)
                                cfg.properties.work_register_count = MAX2(fs->info.work_reg_count, 8);
                        else
                                cfg.properties.work_register_count = fs->info.work_reg_count;

                        /* Hardware quirks around early-zs forcing without a
                         * depth buffer. Note this breaks occlusion queries. */
                        bool has_oq = ctx->occlusion_query && ctx->active_queries;
                        bool force_ez_with_discard = !zsa->enabled && !has_oq;

                        cfg.properties.shader_reads_tilebuffer =
                                force_ez_with_discard && fs->info.fs.can_discard;
                        cfg.properties.shader_contains_discard =
                                !force_ez_with_discard && fs->info.fs.can_discard;
#endif
                }

#if PAN_ARCH == 4
                if (rt_count > 0) {
                        cfg.multisample_misc.load_destination = so->info[0].load_dest;
                        cfg.multisample_misc.blend_shader = (blend_shaders[0] != 0);
                        cfg.stencil_mask_misc.write_enable = !so->info[0].no_colour;
                        cfg.stencil_mask_misc.srgb = util_format_is_srgb(ctx->pipe_framebuffer.cbufs[0]->format);
                        cfg.stencil_mask_misc.dither_disable = !so->base.dither;
                        cfg.stencil_mask_misc.alpha_to_one = so->base.alpha_to_one;

                        if (blend_shaders[0]) {
                                cfg.blend_shader = blend_shaders[0];
                        } else {
                                cfg.blend_constant = pan_blend_get_constant(
                                                so->info[0].constant_mask,
                                                ctx->blend_color.color);
                        }
                } else {
                        /* If there is no colour buffer, leaving fields default is
                         * fine, except for blending which is nonnullable */
                        cfg.blend_equation.color_mask = 0xf;
                        cfg.blend_equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
                        cfg.blend_equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
                        cfg.blend_equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
                        cfg.blend_equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
                        cfg.blend_equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
                        cfg.blend_equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
                }
#elif PAN_ARCH == 5
                /* Workaround */
                cfg.legacy_blend_shader = panfrost_last_nonnull(blend_shaders, rt_count);
#endif

                cfg.multisample_misc.sample_mask = msaa ? ctx->sample_mask : 0xFFFF;

                cfg.multisample_misc.evaluate_per_sample =
                        msaa && (ctx->min_samples > 1);

#if PAN_ARCH >= 6
                /* MSAA blend shaders need to pass their sample ID to
                 * LD_TILE/ST_TILE, so we must preload it. Additionally, we
                 * need per-sample shading for the blend shader, accomplished
                 * by forcing per-sample shading for the whole program. */

                if (msaa && has_blend_shader) {
                        cfg.multisample_misc.evaluate_per_sample = true;
                        cfg.preload.fragment.sample_mask_id = true;
                }
#endif

                cfg.stencil_mask_misc.alpha_to_coverage = alpha_to_coverage;
                cfg.depth_units = rast->offset_units * 2.0f;
                cfg.depth_factor = rast->offset_scale;

                bool back_enab = zsa->base.stencil[1].enabled;
                cfg.stencil_front.reference_value = ctx->stencil_ref.ref_value[0];
                cfg.stencil_back.reference_value = ctx->stencil_ref.ref_value[back_enab ? 1 : 0];

#if PAN_ARCH <= 5
                /* v6+ fits register preload here, no alpha testing */
                cfg.alpha_reference = zsa->base.alpha_ref_value;
#endif
        }
}

static void
panfrost_emit_frag_shader(struct panfrost_context *ctx,
                          struct mali_renderer_state_packed *fragmeta,
                          mali_ptr *blend_shaders)
{
        const struct panfrost_zsa_state *zsa = ctx->depth_stencil;
        const struct panfrost_rasterizer *rast = ctx->rasterizer;
        struct panfrost_shader_state *fs =
                panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);

        /* We need to merge several several partial renderer state descriptors,
         * so stage to temporary storage rather than reading back write-combine
         * memory, which will trash performance. */
        struct mali_renderer_state_packed rsd;
        panfrost_prepare_fs_state(ctx, blend_shaders, &rsd);

#if PAN_ARCH == 4
        if (ctx->pipe_framebuffer.nr_cbufs > 0 && !blend_shaders[0]) {
                /* Word 14: SFBD Blend Equation */
                STATIC_ASSERT(pan_size(BLEND_EQUATION) == 4);
                rsd.opaque[14] = ctx->blend->equation[0];
        }
#endif

        /* Merge with CSO state and upload */
        if (panfrost_fs_required(fs, ctx->blend, &ctx->pipe_framebuffer, zsa)) {
                struct mali_renderer_state_packed *partial_rsd =
                        (struct mali_renderer_state_packed *)&fs->partial_rsd;
                STATIC_ASSERT(sizeof(fs->partial_rsd) == sizeof(*partial_rsd));
                pan_merge(rsd, *partial_rsd, RENDERER_STATE);
        } else {
                pan_merge_empty_fs(&rsd);
        }

        /* Word 8, 9 Misc state */
        rsd.opaque[8] |= zsa->rsd_depth.opaque[0]
                       | rast->multisample.opaque[0];

        rsd.opaque[9] |= zsa->rsd_stencil.opaque[0]
                       | rast->stencil_misc.opaque[0];

        /* Word 10, 11 Stencil Front and Back */
        rsd.opaque[10] |= zsa->stencil_front.opaque[0];
        rsd.opaque[11] |= zsa->stencil_back.opaque[0];

        memcpy(fragmeta, &rsd, sizeof(rsd));
}

static mali_ptr
panfrost_emit_compute_shader_meta(struct panfrost_batch *batch, enum pipe_shader_type stage)
{
        struct panfrost_shader_state *ss = panfrost_get_shader_state(batch->ctx, stage);

        panfrost_batch_add_bo(batch, ss->bin.bo, PIPE_SHADER_VERTEX);
        panfrost_batch_add_bo(batch, ss->state.bo, PIPE_SHADER_VERTEX);

        return ss->state.gpu;
}

static mali_ptr
panfrost_emit_frag_shader_meta(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_shader_state *ss = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);

        panfrost_batch_add_bo(batch, ss->bin.bo, PIPE_SHADER_FRAGMENT);

        struct panfrost_ptr xfer;

#if PAN_ARCH == 4
        xfer = pan_pool_alloc_desc(&batch->pool.base, RENDERER_STATE);
#else
        unsigned rt_count = MAX2(ctx->pipe_framebuffer.nr_cbufs, 1);

        xfer = pan_pool_alloc_desc_aggregate(&batch->pool.base,
                                             PAN_DESC(RENDERER_STATE),
                                             PAN_DESC_ARRAY(rt_count, BLEND));
#endif

        mali_ptr blend_shaders[PIPE_MAX_COLOR_BUFS] = { 0 };
        unsigned shader_offset = 0;
        struct panfrost_bo *shader_bo = NULL;

        for (unsigned c = 0; c < ctx->pipe_framebuffer.nr_cbufs; ++c) {
                if (ctx->pipe_framebuffer.cbufs[c]) {
                        blend_shaders[c] = panfrost_get_blend(batch,
                                        c, &shader_bo, &shader_offset);
                }
        }

        panfrost_emit_frag_shader(ctx, (struct mali_renderer_state_packed *) xfer.cpu, blend_shaders);

#if PAN_ARCH >= 5
        panfrost_emit_blend(batch, xfer.cpu + pan_size(RENDERER_STATE), blend_shaders);
#else
        batch->draws |= PIPE_CLEAR_COLOR0;
        batch->resolve |= PIPE_CLEAR_COLOR0;
#endif

        if (ctx->depth_stencil->base.depth_enabled)
                batch->read |= PIPE_CLEAR_DEPTH;

        if (ctx->depth_stencil->base.stencil[0].enabled)
                batch->read |= PIPE_CLEAR_STENCIL;

        return xfer.gpu;
}

static mali_ptr
panfrost_emit_viewport(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;
        const struct pipe_viewport_state *vp = &ctx->pipe_viewport;
        const struct pipe_scissor_state *ss = &ctx->scissor;
        const struct pipe_rasterizer_state *rast = &ctx->rasterizer->base;

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

        unsigned minx = MIN2(batch->key.width, MAX2((int) vp_minx, 0));
        unsigned maxx = MIN2(batch->key.width, MAX2((int) vp_maxx, 0));
        unsigned miny = MIN2(batch->key.height, MAX2((int) vp_miny, 0));
        unsigned maxy = MIN2(batch->key.height, MAX2((int) vp_maxy, 0));

        if (ss && rast->scissor) {
                minx = MAX2(ss->minx, minx);
                miny = MAX2(ss->miny, miny);
                maxx = MIN2(ss->maxx, maxx);
                maxy = MIN2(ss->maxy, maxy);
        }

        /* Set the range to [1, 1) so max values don't wrap round */
        if (maxx == 0 || maxy == 0)
                maxx = maxy = minx = miny = 1;

        struct panfrost_ptr T = pan_pool_alloc_desc(&batch->pool.base, VIEWPORT);

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
        batch->scissor_culls_everything = (minx >= maxx || miny >= maxy);

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
                panfrost_batch_read_rsrc(batch, rsrc, st);

                /* Alignment gauranteed by
                 * PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT */
                return rsrc->image.data.bo->ptr.gpu + cb->buffer_offset;
        } else if (cb->user_buffer) {
                return pan_pool_upload_aligned(&batch->pool.base,
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
        struct panfrost_resource *rsrc = pan_resource(sb.buffer);
        struct panfrost_bo *bo = rsrc->image.data.bo;

        panfrost_batch_write_rsrc(batch, rsrc, st);

        util_range_add(&rsrc->base, &rsrc->valid_buffer_range,
                        sb.buffer_offset, sb.buffer_size);

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

#if PAN_ARCH >= 6
static void
panfrost_upload_rt_conversion_sysval(struct panfrost_batch *batch,
                unsigned size_and_rt, struct sysval_uniform *uniform)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        unsigned rt = size_and_rt & 0xF;
        unsigned size = size_and_rt >> 4;

        if (rt < batch->key.nr_cbufs && batch->key.cbufs[rt]) {
                enum pipe_format format = batch->key.cbufs[rt]->format;
                uniform->u[0] =
                        GENX(pan_blend_get_internal_desc)(dev, format, rt, size, false) >> 32;
        } else {
                pan_pack(&uniform->u[0], INTERNAL_CONVERSION, cfg)
                        cfg.memory_format = dev->formats[PIPE_FORMAT_NONE].hw;
        }
}
#endif

static void
panfrost_upload_sysvals(struct panfrost_batch *batch,
                        const struct panfrost_ptr *ptr,
                        struct panfrost_shader_state *ss,
                        enum pipe_shader_type st)
{
        struct sysval_uniform *uniforms = ptr->cpu;

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
                        for (unsigned j = 0; j < 3; j++) {
                                batch->num_wg_sysval[j] =
                                        ptr->gpu + (i * sizeof(*uniforms)) + (j * 4);
                        }
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
#if PAN_ARCH >= 6
                case PAN_SYSVAL_RT_CONVERSION:
                        panfrost_upload_rt_conversion_sysval(batch,
                                        PAN_SYSVAL_ID(sysval), &uniforms[i]);
                        break;
#endif
                case PAN_SYSVAL_VERTEX_INSTANCE_OFFSETS:
                        batch->ctx->first_vertex_sysval_ptr =
                                ptr->gpu + (i * sizeof(*uniforms));
                        batch->ctx->base_vertex_sysval_ptr =
                                batch->ctx->first_vertex_sysval_ptr + 4;
                        batch->ctx->base_instance_sysval_ptr =
                                batch->ctx->first_vertex_sysval_ptr + 8;

                        uniforms[i].u[0] = batch->ctx->offset_start;
                        uniforms[i].u[1] = batch->ctx->base_vertex;
                        uniforms[i].u[2] = batch->ctx->base_instance;
                        break;
                case PAN_SYSVAL_DRAWID:
                        uniforms[i].u[0] = batch->ctx->drawid;
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
                panfrost_bo_mmap(rsrc->image.data.bo);
                panfrost_flush_writer(ctx, rsrc, "CPU constant buffer mapping");
                panfrost_bo_wait(rsrc->image.data.bo, INT64_MAX, false);

                return rsrc->image.data.bo->ptr.cpu + cb->buffer_offset;
        } else if (cb->user_buffer) {
                return cb->user_buffer + cb->buffer_offset;
        } else
                unreachable("No constant buffer");
}

static mali_ptr
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
                pan_pool_alloc_aligned(&batch->pool.base, sys_size, 16);

        /* Upload sysvals requested by the shader */
        panfrost_upload_sysvals(batch, &transfer, ss, stage);

        /* Next up, attach UBOs. UBO count includes gaps but no sysval UBO */
        struct panfrost_shader_state *shader = panfrost_get_shader_state(ctx, stage);
        unsigned ubo_count = shader->info.ubo_count - (sys_size ? 1 : 0);
        unsigned sysval_ubo = sys_size ? ubo_count : ~0;

        struct panfrost_ptr ubos =
                pan_pool_alloc_desc_array(&batch->pool.base,
                                          ubo_count + 1,
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

        u_foreach_bit(ubo, ss->info.ubo_mask & buf->enabled_mask) {
                size_t usz = buf->cb[ubo].buffer_size;

                if (usz == 0) {
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

        if (ss->info.push.count == 0)
                return ubos.gpu;

        /* Copy push constants required by the shader */
        struct panfrost_ptr push_transfer =
                pan_pool_alloc_aligned(&batch->pool.base,
                                       ss->info.push.count * 4, 16);

        uint32_t *push_cpu = (uint32_t *) push_transfer.cpu;
        *push_constants = push_transfer.gpu;

        for (unsigned i = 0; i < ss->info.push.count; ++i) {
                struct panfrost_ubo_word src = ss->info.push.words[i];

                if (src.ubo == sysval_ubo) {
                        unsigned sysval_idx = src.offset / 16;
                        unsigned sysval_comp = (src.offset % 16) / 4;
                        unsigned sysval_type = PAN_SYSVAL_TYPE(ss->info.sysvals.sysvals[sysval_idx]);
                        mali_ptr ptr = push_transfer.gpu + (4 * i);

                        switch (sysval_type) {
                        case PAN_SYSVAL_VERTEX_INSTANCE_OFFSETS:
                                switch (sysval_comp) {
                                case 0:
                                        batch->ctx->first_vertex_sysval_ptr = ptr;
                                        break;
                                case 1:
                                        batch->ctx->base_vertex_sysval_ptr = ptr;
                                        break;
                                case 2:
                                        batch->ctx->base_instance_sysval_ptr = ptr;
                                        break;
                                case 3:
                                        /* Spurious (Midgard doesn't pack) */
                                        break;
                                default:
                                        unreachable("Invalid vertex/instance offset component\n");
                                }
                                break;

                        case PAN_SYSVAL_NUM_WORK_GROUPS:
                                batch->num_wg_sysval[sysval_comp] = ptr;
                                break;

                        default:
                                break;
                        }
                }
                /* Map the UBO, this should be cheap. However this is reading
                 * from write-combine memory which is _very_ slow. It might pay
                 * off to upload sysvals to a staging buffer on the CPU on the
                 * assumption sysvals will get pushed (TODO) */

                const void *mapped_ubo = (src.ubo == sysval_ubo) ? transfer.cpu :
                        panfrost_map_constant_buffer_cpu(ctx, buf, src.ubo);

                /* TODO: Is there any benefit to combining ranges */
                memcpy(push_cpu + i, (uint8_t *) mapped_ubo + src.offset, 4);
        }

        return ubos.gpu;
}

static mali_ptr
panfrost_emit_shared_memory(struct panfrost_batch *batch,
                            const struct pipe_grid_info *info)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_shader_variants *all = ctx->shader[PIPE_SHADER_COMPUTE];
        struct panfrost_shader_state *ss = &all->variants[all->active_variant];
        struct panfrost_ptr t =
                pan_pool_alloc_desc(&batch->pool.base, LOCAL_STORAGE);

        pan_pack(t.cpu, LOCAL_STORAGE, ls) {
                unsigned wls_single_size =
                        util_next_power_of_two(MAX2(ss->info.wls_size, 128));

                if (ss->info.wls_size) {
                        ls.wls_instances =
                                util_next_power_of_two(info->grid[0]) *
                                util_next_power_of_two(info->grid[1]) *
                                util_next_power_of_two(info->grid[2]);

                        ls.wls_size_scale = util_logbase2(wls_single_size) + 1;

                        unsigned wls_size = wls_single_size * ls.wls_instances * dev->core_count;

                        ls.wls_base_pointer =
                                (panfrost_batch_get_shared_memory(batch,
                                                                  wls_size,
                                                                  1))->ptr.gpu;
                } else {
                        ls.wls_instances = MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM;
                }

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

#if PAN_ARCH <= 5
static mali_ptr
panfrost_get_tex_desc(struct panfrost_batch *batch,
                      enum pipe_shader_type st,
                      struct panfrost_sampler_view *view)
{
        if (!view)
                return (mali_ptr) 0;

        struct pipe_sampler_view *pview = &view->base;
        struct panfrost_resource *rsrc = pan_resource(pview->texture);

        panfrost_batch_read_rsrc(batch, rsrc, st);
        panfrost_batch_add_bo(batch, view->state.bo, st);

        return view->state.gpu;
}
#endif

static void
panfrost_create_sampler_view_bo(struct panfrost_sampler_view *so,
                                struct pipe_context *pctx,
                                struct pipe_resource *texture)
{
        struct panfrost_device *device = pan_device(pctx->screen);
        struct panfrost_context *ctx = pan_context(pctx);
        struct panfrost_resource *prsrc = (struct panfrost_resource *)texture;
        enum pipe_format format = so->base.format;
        assert(prsrc->image.data.bo);

        /* Format to access the stencil/depth portion of a Z32_S8 texture */
        if (format == PIPE_FORMAT_X32_S8X24_UINT) {
                assert(prsrc->separate_stencil);
                texture = &prsrc->separate_stencil->base;
                prsrc = (struct panfrost_resource *)texture;
                format = texture->format;
        } else if (format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
                format = PIPE_FORMAT_Z32_FLOAT;
        }

        const struct util_format_description *desc = util_format_description(format);

        bool fake_rgtc = !panfrost_supports_compressed_format(device, MALI_BC4_UNORM);

        if (desc->layout == UTIL_FORMAT_LAYOUT_RGTC && fake_rgtc) {
                if (desc->is_snorm)
                        format = PIPE_FORMAT_R8G8B8A8_SNORM;
                else
                        format = PIPE_FORMAT_R8G8B8A8_UNORM;
                desc = util_format_description(format);
        }

        so->texture_bo = prsrc->image.data.bo->ptr.gpu;
        so->modifier = prsrc->image.layout.modifier;

        /* MSAA only supported for 2D textures */

        assert(texture->nr_samples <= 1 ||
               so->base.target == PIPE_TEXTURE_2D ||
               so->base.target == PIPE_TEXTURE_2D_ARRAY);

        enum mali_texture_dimension type =
                panfrost_translate_texture_dimension(so->base.target);

        bool is_buffer = (so->base.target == PIPE_BUFFER);

        unsigned first_level = is_buffer ? 0 : so->base.u.tex.first_level;
        unsigned last_level = is_buffer ? 0 : so->base.u.tex.last_level;
        unsigned first_layer = is_buffer ? 0 : so->base.u.tex.first_layer;
        unsigned last_layer = is_buffer ? 0 : so->base.u.tex.last_layer;
        unsigned buf_offset = is_buffer ? so->base.u.buf.offset : 0;
        unsigned buf_size = (is_buffer ? so->base.u.buf.size : 0) /
                            util_format_get_blocksize(format);

        if (so->base.target == PIPE_TEXTURE_3D) {
                first_layer /= prsrc->image.layout.depth;
                last_layer /= prsrc->image.layout.depth;
                assert(!first_layer && !last_layer);
        }

        struct pan_image_view iview = {
                .format = format,
                .dim = type,
                .first_level = first_level,
                .last_level = last_level,
                .first_layer = first_layer,
                .last_layer = last_layer,
                .swizzle = {
                        so->base.swizzle_r,
                        so->base.swizzle_g,
                        so->base.swizzle_b,
                        so->base.swizzle_a,
                },
                .image = &prsrc->image,

                .buf.offset = buf_offset,
                .buf.size = buf_size,
        };

        unsigned size =
                (PAN_ARCH <= 5 ? pan_size(TEXTURE) : 0) +
                GENX(panfrost_estimate_texture_payload_size)(&iview);

        struct panfrost_ptr payload = pan_pool_alloc_aligned(&ctx->descs.base, size, 64);
        so->state = panfrost_pool_take_ref(&ctx->descs, payload.gpu);

        void *tex = (PAN_ARCH >= 6) ? &so->bifrost_descriptor : payload.cpu;

        if (PAN_ARCH <= 5) {
                payload.cpu += pan_size(TEXTURE);
                payload.gpu += pan_size(TEXTURE);
        }

        GENX(panfrost_new_texture)(device, &iview, tex, &payload);
}

static void
panfrost_update_sampler_view(struct panfrost_sampler_view *view,
                             struct pipe_context *pctx)
{
        struct panfrost_resource *rsrc = pan_resource(view->base.texture);
        if (view->texture_bo != rsrc->image.data.bo->ptr.gpu ||
            view->modifier != rsrc->image.layout.modifier) {
                panfrost_bo_unreference(view->state.bo);
                panfrost_create_sampler_view_bo(view, pctx, &rsrc->base);
        }
}

static mali_ptr
panfrost_emit_texture_descriptors(struct panfrost_batch *batch,
                                  enum pipe_shader_type stage)
{
        struct panfrost_context *ctx = batch->ctx;

        if (!ctx->sampler_view_count[stage])
                return 0;

#if PAN_ARCH >= 6
        struct panfrost_ptr T =
                pan_pool_alloc_desc_array(&batch->pool.base,
                                          ctx->sampler_view_count[stage],
                                          TEXTURE);
        struct mali_texture_packed *out =
                (struct mali_texture_packed *) T.cpu;

        for (int i = 0; i < ctx->sampler_view_count[stage]; ++i) {
                struct panfrost_sampler_view *view = ctx->sampler_views[stage][i];
                struct pipe_sampler_view *pview = &view->base;
                struct panfrost_resource *rsrc = pan_resource(pview->texture);

                panfrost_update_sampler_view(view, &ctx->base);
                out[i] = view->bifrost_descriptor;

                panfrost_batch_read_rsrc(batch, rsrc, stage);
                panfrost_batch_add_bo(batch, view->state.bo, stage);
        }

        return T.gpu;
#else
        uint64_t trampolines[PIPE_MAX_SHADER_SAMPLER_VIEWS];

        for (int i = 0; i < ctx->sampler_view_count[stage]; ++i) {
                struct panfrost_sampler_view *view = ctx->sampler_views[stage][i];

                panfrost_update_sampler_view(view, &ctx->base);

                trampolines[i] = panfrost_get_tex_desc(batch, stage, view);
        }

        return pan_pool_upload_aligned(&batch->pool.base, trampolines,
                                       sizeof(uint64_t) *
                                       ctx->sampler_view_count[stage],
                                       sizeof(uint64_t));
#endif
}

static mali_ptr
panfrost_emit_sampler_descriptors(struct panfrost_batch *batch,
                                  enum pipe_shader_type stage)
{
        struct panfrost_context *ctx = batch->ctx;

        if (!ctx->sampler_count[stage])
                return 0;

        struct panfrost_ptr T =
                pan_pool_alloc_desc_array(&batch->pool.base,
                                          ctx->sampler_count[stage],
                                          SAMPLER);
        struct mali_sampler_packed *out = (struct mali_sampler_packed *) T.cpu;

        for (unsigned i = 0; i < ctx->sampler_count[stage]; ++i)
                out[i] = ctx->samplers[stage][i]->hw;

        return T.gpu;
}

/* Packs all image attribute descs and attribute buffer descs.
 * `first_image_buf_index` must be the index of the first image attribute buffer descriptor.
 */
static void
emit_image_attribs(struct panfrost_context *ctx, enum pipe_shader_type shader,
                   struct mali_attribute_packed *attribs, unsigned first_buf)
{
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        unsigned last_bit = util_last_bit(ctx->image_mask[shader]);

        for (unsigned i = 0; i < last_bit; ++i) {
                enum pipe_format format = ctx->images[shader][i].format;

                pan_pack(attribs + i, ATTRIBUTE, cfg) {
                        /* Continuation record means 2 buffers per image */
                        cfg.buffer_index = first_buf + (i * 2);
                        cfg.offset_enable = (PAN_ARCH <= 5);
                        cfg.format = dev->formats[format].hw;
                }
        }
}

static enum mali_attribute_type
pan_modifier_to_attr_type(uint64_t modifier)
{
        switch (modifier) {
        case DRM_FORMAT_MOD_LINEAR:
                return MALI_ATTRIBUTE_TYPE_3D_LINEAR;
        case DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED:
                return MALI_ATTRIBUTE_TYPE_3D_INTERLEAVED;
        default:
                unreachable("Invalid modifier for attribute record");
        }
}

static void
emit_image_bufs(struct panfrost_batch *batch, enum pipe_shader_type shader,
                struct mali_attribute_buffer_packed *bufs,
                unsigned first_image_buf_index)
{
        struct panfrost_context *ctx = batch->ctx;
        unsigned last_bit = util_last_bit(ctx->image_mask[shader]);

        for (unsigned i = 0; i < last_bit; ++i) {
                struct pipe_image_view *image = &ctx->images[shader][i];

                if (!(ctx->image_mask[shader] & (1 << i)) ||
                    !(image->shader_access & PIPE_IMAGE_ACCESS_READ_WRITE)) {
                        /* Unused image bindings */
                        pan_pack(bufs + (i * 2), ATTRIBUTE_BUFFER, cfg);
                        pan_pack(bufs + (i * 2) + 1, ATTRIBUTE_BUFFER, cfg);
                        continue;
                }

                struct panfrost_resource *rsrc = pan_resource(image->resource);

                /* TODO: MSAA */
                assert(image->resource->nr_samples <= 1 && "MSAA'd images not supported");

                bool is_3d = rsrc->base.target == PIPE_TEXTURE_3D;
                bool is_buffer = rsrc->base.target == PIPE_BUFFER;

                unsigned offset = is_buffer ? image->u.buf.offset :
                        panfrost_texture_offset(&rsrc->image.layout,
                                                image->u.tex.level,
                                                is_3d ? 0 : image->u.tex.first_layer,
                                                is_3d ? image->u.tex.first_layer : 0);

                if (image->shader_access & PIPE_IMAGE_ACCESS_WRITE) {
                        panfrost_batch_write_rsrc(batch, rsrc, shader);

                        unsigned level = is_buffer ? 0 : image->u.tex.level;
                        BITSET_SET(rsrc->valid.data, level);

                        if (is_buffer) {
                                util_range_add(&rsrc->base, &rsrc->valid_buffer_range,
                                                0, rsrc->base.width0);
                        }
                } else {
                        panfrost_batch_read_rsrc(batch, rsrc, shader);
                }

                pan_pack(bufs + (i * 2), ATTRIBUTE_BUFFER, cfg) {
                        cfg.type = pan_modifier_to_attr_type(rsrc->image.layout.modifier);
                        cfg.pointer = rsrc->image.data.bo->ptr.gpu + offset;
                        cfg.stride = util_format_get_blocksize(image->format);
                        cfg.size = rsrc->image.data.bo->size - offset;
                }

                if (is_buffer) {
                        pan_pack(bufs + (i * 2) + 1, ATTRIBUTE_BUFFER_CONTINUATION_3D, cfg) {
                                cfg.s_dimension = rsrc->base.width0 /
                                        util_format_get_blocksize(image->format);
                                cfg.t_dimension = cfg.r_dimension = 1;
                        }

                        continue;
                }

                pan_pack(bufs + (i * 2) + 1, ATTRIBUTE_BUFFER_CONTINUATION_3D, cfg) {
                        unsigned level = image->u.tex.level;

                        cfg.s_dimension = u_minify(rsrc->base.width0, level);
                        cfg.t_dimension = u_minify(rsrc->base.height0, level);
                        cfg.r_dimension = is_3d ?
                                u_minify(rsrc->base.depth0, level) :
                                image->u.tex.last_layer - image->u.tex.first_layer + 1;

                        cfg.row_stride =
                                rsrc->image.layout.slices[level].row_stride;

                        if (rsrc->base.target != PIPE_TEXTURE_2D) {
                                cfg.slice_stride =
                                        panfrost_get_layer_stride(&rsrc->image.layout,
                                                                  level);
                        }
                }
        }
}

static mali_ptr
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

        /* Images always need a MALI_ATTRIBUTE_BUFFER_CONTINUATION_3D */
        unsigned attr_count = shader->info.attribute_count;
        unsigned buf_count = (attr_count * 2) + (PAN_ARCH >= 6 ? 1 : 0);

        struct panfrost_ptr bufs =
                pan_pool_alloc_desc_array(&batch->pool.base, buf_count, ATTRIBUTE_BUFFER);

        struct panfrost_ptr attribs =
                pan_pool_alloc_desc_array(&batch->pool.base, attr_count, ATTRIBUTE);

        emit_image_attribs(ctx, type, attribs.cpu, 0);
        emit_image_bufs(batch, type, bufs.cpu, 0);

        /* We need an empty attrib buf to stop the prefetching on Bifrost */
#if PAN_ARCH >= 6
        pan_pack(bufs.cpu + ((buf_count - 1) * pan_size(ATTRIBUTE_BUFFER)),
                 ATTRIBUTE_BUFFER, cfg);
#endif

        *buffers = bufs.gpu;
        return attribs.gpu;
}

static mali_ptr
panfrost_emit_vertex_data(struct panfrost_batch *batch,
                          mali_ptr *buffers)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_vertex_state *so = ctx->vertex;
        struct panfrost_shader_state *vs = panfrost_get_shader_state(ctx, PIPE_SHADER_VERTEX);
        bool instanced = ctx->indirect_draw || ctx->instance_count > 1;
        uint32_t image_mask = ctx->image_mask[PIPE_SHADER_VERTEX];
        unsigned nr_images = util_last_bit(image_mask);

        /* Worst case: everything is NPOT, which is only possible if instancing
         * is enabled. Otherwise single record is gauranteed.
         * Also, we allocate more memory than what's needed here if either instancing
         * is enabled or images are present, this can be improved. */
        unsigned bufs_per_attrib = (instanced || nr_images > 0) ? 2 : 1;
        unsigned nr_bufs = ((so->nr_bufs + nr_images) * bufs_per_attrib) +
                           (PAN_ARCH >= 6 ? 1 : 0);

#if PAN_ARCH <= 5
        /* Midgard needs vertexid/instanceid handled specially */
        bool special_vbufs = vs->info.attribute_count >= PAN_VERTEX_ID;

        if (special_vbufs)
                nr_bufs += 2;
#endif

        if (!nr_bufs) {
                *buffers = 0;
                return 0;
        }

        struct panfrost_ptr S =
                pan_pool_alloc_desc_array(&batch->pool.base, nr_bufs,
                                          ATTRIBUTE_BUFFER);
        struct panfrost_ptr T =
                pan_pool_alloc_desc_array(&batch->pool.base,
                                          vs->info.attribute_count,
                                          ATTRIBUTE);

        struct mali_attribute_buffer_packed *bufs =
                (struct mali_attribute_buffer_packed *) S.cpu;

        struct mali_attribute_packed *out =
                (struct mali_attribute_packed *) T.cpu;

        unsigned attrib_to_buffer[PIPE_MAX_ATTRIBS] = { 0 };
        unsigned k = 0;

        for (unsigned i = 0; i < so->nr_bufs; ++i) {
                unsigned vbi = so->buffers[i].vbi;
                unsigned divisor = so->buffers[i].divisor;
                attrib_to_buffer[i] = k;

                if (!(ctx->vb_mask & (1 << vbi)))
                        continue;

                struct pipe_vertex_buffer *buf = &ctx->vertex_buffers[vbi];
                struct panfrost_resource *rsrc;

                rsrc = pan_resource(buf->buffer.resource);
                if (!rsrc)
                        continue;

                panfrost_batch_read_rsrc(batch, rsrc, PIPE_SHADER_VERTEX);

                /* Mask off lower bits, see offset fixup below */
                mali_ptr raw_addr = rsrc->image.data.bo->ptr.gpu + buf->buffer_offset;
                mali_ptr addr = raw_addr & ~63;

                /* Since we advanced the base pointer, we shrink the buffer
                 * size, but add the offset we subtracted */
                unsigned size = rsrc->base.width0 + (raw_addr - addr)
                        - buf->buffer_offset;

                /* When there is a divisor, the hardware-level divisor is
                 * the product of the instance divisor and the padded count */
                unsigned stride = buf->stride;

                if (ctx->indirect_draw) {
                        /* We allocated 2 records for each attribute buffer */
                        assert((k & 1) == 0);

                        /* With indirect draws we can't guess the vertex_count.
                         * Pre-set the address, stride and size fields, the
                         * compute shader do the rest.
                         */
                        pan_pack(bufs + k, ATTRIBUTE_BUFFER, cfg) {
                                cfg.type = MALI_ATTRIBUTE_TYPE_1D;
                                cfg.pointer = addr;
                                cfg.stride = stride;
                                cfg.size = size;
                        }

                        /* We store the unmodified divisor in the continuation
                         * slot so the compute shader can retrieve it.
                         */
                        pan_pack(bufs + k + 1, ATTRIBUTE_BUFFER_CONTINUATION_NPOT, cfg) {
                                cfg.divisor = divisor;
                        }

                        k += 2;
                        continue;
                }

                unsigned hw_divisor = ctx->padded_count * divisor;

                if (ctx->instance_count <= 1) {
                        /* Per-instance would be every attribute equal */
                        if (divisor)
                                stride = 0;

                        pan_pack(bufs + k, ATTRIBUTE_BUFFER, cfg) {
                                cfg.pointer = addr;
                                cfg.stride = stride;
                                cfg.size = size;
                        }
                } else if (!divisor) {
                        pan_pack(bufs + k, ATTRIBUTE_BUFFER, cfg) {
                                cfg.type = MALI_ATTRIBUTE_TYPE_1D_MODULUS;
                                cfg.pointer = addr;
                                cfg.stride = stride;
                                cfg.size = size;
                                cfg.divisor = ctx->padded_count;
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

                        /* Records with continuations must be aligned */
                        k = ALIGN_POT(k, 2);
                        attrib_to_buffer[i] = k;

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

#if PAN_ARCH <= 5
        /* Add special gl_VertexID/gl_InstanceID buffers */
        if (special_vbufs) {
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
#endif

        k = ALIGN_POT(k, 2);
        emit_image_attribs(ctx, PIPE_SHADER_VERTEX, out + so->num_elements, k);
        emit_image_bufs(batch, PIPE_SHADER_VERTEX, bufs + k, k);
        k += (util_last_bit(ctx->image_mask[PIPE_SHADER_VERTEX]) * 2);

#if PAN_ARCH >= 6
        /* We need an empty attrib buf to stop the prefetching on Bifrost */
        pan_pack(&bufs[k], ATTRIBUTE_BUFFER, cfg);
#endif

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

                /* BOs are aligned; just fixup for buffer_offset */
                signed src_offset = so->pipe[i].src_offset;
                src_offset += (buf->buffer_offset & 63);

                /* Base instance offset */
                if (ctx->base_instance && so->pipe[i].instance_divisor) {
                        src_offset += (ctx->base_instance * buf->stride) /
                                      so->pipe[i].instance_divisor;
                }

                /* Also, somewhat obscurely per-instance data needs to be
                 * offset in response to a delayed start in an indexed draw */

                if (so->pipe[i].instance_divisor && ctx->instance_count > 1)
                        src_offset -= buf->stride * ctx->offset_start;

                pan_pack(out + i, ATTRIBUTE, cfg) {
                        cfg.buffer_index = attrib_to_buffer[so->element_buffer[i]];
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
        mali_ptr ptr =
                batch->ctx->indirect_draw ? 0 :
                pan_pool_alloc_aligned(&batch->invisible_pool.base, size, 64).gpu;

        pan_pack(slot, ATTRIBUTE_BUFFER, cfg) {
                cfg.stride = stride;
                cfg.size = size;
                cfg.pointer = ptr;
        }

        return ptr;
}

static unsigned
panfrost_xfb_offset(unsigned stride, struct pipe_stream_output_target *target)
{
        return target->buffer_offset + (pan_so_target(target)->offset * stride);
}

static void
panfrost_emit_streamout(struct panfrost_batch *batch,
                        struct mali_attribute_buffer_packed *slot,
                        unsigned stride, unsigned count,
                        struct pipe_stream_output_target *target)
{
        unsigned max_size = target->buffer_size;
        unsigned expected_size = stride * count;

        /* Grab the BO and bind it to the batch */
        struct panfrost_resource *rsrc = pan_resource(target->buffer);
        struct panfrost_bo *bo = rsrc->image.data.bo;

        panfrost_batch_write_rsrc(batch, rsrc, PIPE_SHADER_VERTEX);
        panfrost_batch_read_rsrc(batch, rsrc, PIPE_SHADER_FRAGMENT);

        unsigned offset = panfrost_xfb_offset(stride, target);

        pan_pack(slot, ATTRIBUTE_BUFFER, cfg) {
                cfg.pointer = bo->ptr.gpu + (offset & ~63);
                cfg.stride = stride;
                cfg.size = MIN2(max_size, expected_size) + (offset & 63);

                util_range_add(&rsrc->base, &rsrc->valid_buffer_range,
                                offset, cfg.size);
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

/* Given a varying, figure out which index it corresponds to */

static inline unsigned
pan_varying_index(unsigned present, enum pan_special_varying v)
{
        return util_bitcount(present & BITFIELD_MASK(v));
}

/* Get the base offset for XFB buffers, which by convention come after
 * everything else. Wrapper function for semantic reasons; by construction this
 * is just popcount. */

static inline unsigned
pan_xfb_base(unsigned present)
{
        return util_bitcount(present);
}

/* Determines which varying buffers are required */

static inline unsigned
pan_varying_present(const struct panfrost_device *dev,
                    struct pan_shader_info *producer,
                    struct pan_shader_info *consumer,
                    uint16_t point_coord_mask)
{
        /* At the moment we always emit general and position buffers. Not
         * strictly necessary but usually harmless */

        unsigned present = BITFIELD_BIT(PAN_VARY_GENERAL) | BITFIELD_BIT(PAN_VARY_POSITION);

        /* Enable special buffers by the shader info */

        if (producer->vs.writes_point_size)
                present |= BITFIELD_BIT(PAN_VARY_PSIZ);

#if PAN_ARCH <= 5
        /* On Midgard, these exist as real varyings. Later architectures use
         * LD_VAR_SPECIAL reads instead. */

        if (consumer->fs.reads_point_coord)
                present |= BITFIELD_BIT(PAN_VARY_PNTCOORD);

        if (consumer->fs.reads_face)
                present |= BITFIELD_BIT(PAN_VARY_FACE);

        if (consumer->fs.reads_frag_coord)
                present |= BITFIELD_BIT(PAN_VARY_FRAGCOORD);

        /* Also, if we have a point sprite, we need a point coord buffer */

        for (unsigned i = 0; i < consumer->varyings.input_count; i++)  {
                gl_varying_slot loc = consumer->varyings.input[i].location;

                if (util_varying_is_point_coord(loc, point_coord_mask))
                        present |= BITFIELD_BIT(PAN_VARY_PNTCOORD);
        }
#endif

        return present;
}

/* Emitters for varying records */

static void
pan_emit_vary(const struct panfrost_device *dev,
              struct mali_attribute_packed *out,
              unsigned buffer_index,
              mali_pixel_format format, unsigned offset)
{
        pan_pack(out, ATTRIBUTE, cfg) {
                cfg.buffer_index = buffer_index;
                cfg.offset_enable = (PAN_ARCH <= 5);
                cfg.format = format;
                cfg.offset = offset;
        }
}

/* Special records */

static const struct {
       unsigned components;
       enum mali_format format;
} pan_varying_formats[PAN_VARY_MAX] = {
        [PAN_VARY_POSITION]     = { 4, MALI_SNAP_4 },
        [PAN_VARY_PSIZ]         = { 1, MALI_R16F },
        [PAN_VARY_PNTCOORD]     = { 1, MALI_R16F },
        [PAN_VARY_FACE]         = { 1, MALI_R32I },
        [PAN_VARY_FRAGCOORD]    = { 4, MALI_RGBA32F },
};

static mali_pixel_format
pan_special_format(const struct panfrost_device *dev,
                enum pan_special_varying buf)
{
        assert(buf < PAN_VARY_MAX);
        mali_pixel_format format = (pan_varying_formats[buf].format << 12);

#if PAN_ARCH <= 6
        unsigned nr = pan_varying_formats[buf].components;
        format |= panfrost_get_default_swizzle(nr);
#endif

        return format;
}

static void
pan_emit_vary_special(const struct panfrost_device *dev,
                      struct mali_attribute_packed *out,
                      unsigned present, enum pan_special_varying buf)
{
        pan_emit_vary(dev, out, pan_varying_index(present, buf),
                        pan_special_format(dev, buf), 0);
}

/* Negative indicates a varying is not found */

static signed
pan_find_vary(const struct pan_shader_varying *vary,
                unsigned vary_count, unsigned loc)
{
        for (unsigned i = 0; i < vary_count; ++i) {
                if (vary[i].location == loc)
                        return i;
        }

        return -1;
}

/* Assign varying locations for the general buffer. Returns the calculated
 * per-vertex stride, and outputs offsets into the passed array. Negative
 * offset indicates a varying is not used. */

static unsigned
pan_assign_varyings(const struct panfrost_device *dev,
                    struct pan_shader_info *producer,
                    struct pan_shader_info *consumer,
                    signed *offsets)
{
        unsigned producer_count = producer->varyings.output_count;
        unsigned consumer_count = consumer->varyings.input_count;

        const struct pan_shader_varying *producer_vars = producer->varyings.output;
        const struct pan_shader_varying *consumer_vars = consumer->varyings.input;

        unsigned stride = 0;

        for (unsigned i = 0; i < producer_count; ++i) {
                signed loc = pan_find_vary(consumer_vars, consumer_count,
                                producer_vars[i].location);

                if (loc >= 0) {
                        offsets[i] = stride;

                        enum pipe_format format = consumer_vars[loc].format;
                        stride += util_format_get_blocksize(format);
                } else {
                        offsets[i] = -1;
                }
        }

        return stride;
}

/* Emitter for a single varying (attribute) descriptor */

static void
panfrost_emit_varying(const struct panfrost_device *dev,
                      struct mali_attribute_packed *out,
                      const struct pan_shader_varying varying,
                      enum pipe_format pipe_format,
                      unsigned present,
                      uint16_t point_sprite_mask,
                      struct pipe_stream_output_info *xfb,
                      uint64_t xfb_loc_mask,
                      unsigned max_xfb,
                      unsigned *xfb_offsets,
                      signed offset,
                      enum pan_special_varying pos_varying)
{
        /* Note: varying.format != pipe_format in some obscure cases due to a
         * limitation of the NIR linker. This should be fixed in the future to
         * eliminate the additional lookups. See:
         * dEQP-GLES3.functional.shaders.conditionals.if.sequence_statements_vertex
         */
        gl_varying_slot loc = varying.location;
        mali_pixel_format format = dev->formats[pipe_format].hw;

        struct pipe_stream_output *o = (xfb_loc_mask & BITFIELD64_BIT(loc)) ?
                pan_get_so(xfb, loc) : NULL;

        if (util_varying_is_point_coord(loc, point_sprite_mask)) {
                pan_emit_vary_special(dev, out, present, PAN_VARY_PNTCOORD);
        } else if (o && o->output_buffer < max_xfb) {
                unsigned fixup_offset = xfb_offsets[o->output_buffer] & 63;

                pan_emit_vary(dev, out,
                                pan_xfb_base(present) + o->output_buffer,
                                format, (o->dst_offset * 4) + fixup_offset);
        } else if (loc == VARYING_SLOT_POS) {
                pan_emit_vary_special(dev, out, present, pos_varying);
        } else if (loc == VARYING_SLOT_PSIZ) {
                pan_emit_vary_special(dev, out, present, PAN_VARY_PSIZ);
        } else if (loc == VARYING_SLOT_FACE) {
                pan_emit_vary_special(dev, out, present, PAN_VARY_FACE);
        } else if (offset < 0) {
                pan_emit_vary(dev, out, 0, (MALI_CONSTANT << 12), 0);
        } else {
                STATIC_ASSERT(PAN_VARY_GENERAL == 0);
                pan_emit_vary(dev, out, 0, format, offset);
        }
}

/* Links varyings and uploads ATTRIBUTE descriptors. Can execute at link time,
 * rather than draw time (under good conditions). */

static void
panfrost_emit_varying_descs(
                struct panfrost_pool *pool,
                struct panfrost_shader_state *producer,
                struct panfrost_shader_state *consumer,
                struct panfrost_streamout *xfb,
                uint16_t point_coord_mask,
                struct pan_linkage *out)
{
        struct panfrost_device *dev = pool->base.dev;
        struct pipe_stream_output_info *xfb_info = &producer->stream_output;
        unsigned producer_count = producer->info.varyings.output_count;
        unsigned consumer_count = consumer->info.varyings.input_count;

        /* Offsets within the general varying buffer, indexed by location */
        signed offsets[PAN_MAX_VARYINGS];
        assert(producer_count <= ARRAY_SIZE(offsets));
        assert(consumer_count <= ARRAY_SIZE(offsets));

        /* Allocate enough descriptors for both shader stages */
        struct panfrost_ptr T =
                pan_pool_alloc_desc_array(&pool->base,
                                          producer_count + consumer_count,
                                          ATTRIBUTE);

        /* Take a reference if we're being put on the CSO */
        if (!pool->owned) {
                out->bo = pool->transient_bo;
                panfrost_bo_reference(out->bo);
        }

        struct mali_attribute_packed *descs = T.cpu;
        out->producer = producer_count ? T.gpu : 0;
        out->consumer = consumer_count ? T.gpu +
                (pan_size(ATTRIBUTE) * producer_count) : 0;

        /* Lay out the varyings. Must use producer to lay out, in order to
         * respect transform feedback precisions. */
        out->present = pan_varying_present(dev, &producer->info,
                        &consumer->info, point_coord_mask);

        out->stride = pan_assign_varyings(dev, &producer->info,
                        &consumer->info, offsets);

        unsigned xfb_offsets[PIPE_MAX_SO_BUFFERS];

        for (unsigned i = 0; i < xfb->num_targets; ++i) {
                xfb_offsets[i] = panfrost_xfb_offset(xfb_info->stride[i] * 4,
                                xfb->targets[i]);
        }

        for (unsigned i = 0; i < producer_count; ++i) {
                signed j = pan_find_vary(consumer->info.varyings.input,
                                consumer->info.varyings.input_count,
                                producer->info.varyings.output[i].location);

                enum pipe_format format = (j >= 0) ?
                        consumer->info.varyings.input[j].format :
                        producer->info.varyings.output[i].format;

                panfrost_emit_varying(dev, descs + i,
                                producer->info.varyings.output[i], format,
                                out->present, 0, &producer->stream_output,
                                producer->so_mask, xfb->num_targets,
                                xfb_offsets, offsets[i], PAN_VARY_POSITION);
        }

        for (unsigned i = 0; i < consumer_count; ++i) {
                signed j = pan_find_vary(producer->info.varyings.output,
                                producer->info.varyings.output_count,
                                consumer->info.varyings.input[i].location);

                signed offset = (j >= 0) ? offsets[j] : -1;

                panfrost_emit_varying(dev, descs + producer_count + i,
                                consumer->info.varyings.input[i],
                                consumer->info.varyings.input[i].format,
                                out->present, point_coord_mask,
                                &producer->stream_output, producer->so_mask,
                                xfb->num_targets, xfb_offsets, offset,
                                PAN_VARY_FRAGCOORD);
        }
}

#if PAN_ARCH <= 5
static void
pan_emit_special_input(struct mali_attribute_buffer_packed *out,
                unsigned present,
                enum pan_special_varying v,
                unsigned special)
{
        if (present & BITFIELD_BIT(v)) {
                unsigned idx = pan_varying_index(present, v);

                pan_pack(out + idx, ATTRIBUTE_BUFFER, cfg) {
                        cfg.special = special;
                        cfg.type = 0;
                }
        }
}
#endif

static void
panfrost_emit_varying_descriptor(struct panfrost_batch *batch,
                                 unsigned vertex_count,
                                 mali_ptr *vs_attribs,
                                 mali_ptr *fs_attribs,
                                 mali_ptr *buffers,
                                 unsigned *buffer_count,
                                 mali_ptr *position,
                                 mali_ptr *psiz,
                                 bool point_coord_replace)
{
        /* Load the shaders */
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_shader_state *vs, *fs;

        vs = panfrost_get_shader_state(ctx, PIPE_SHADER_VERTEX);
        fs = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);

        uint16_t point_coord_mask = 0;

#if PAN_ARCH <= 5
        /* Point sprites are lowered on Bifrost and newer */
        if (point_coord_replace)
                point_coord_mask = ctx->rasterizer->base.sprite_coord_enable;
#endif

        /* In good conditions, we only need to link varyings once */
        bool prelink =
                (point_coord_mask == 0) &&
                (ctx->streamout.num_targets == 0) &&
                !vs->info.separable &&
                !fs->info.separable;

        /* Try to reduce copies */
        struct pan_linkage _linkage;
        struct pan_linkage *linkage = prelink ? &vs->linkage : &_linkage;

        /* Emit ATTRIBUTE descriptors if needed */
        if (!prelink || vs->linkage.bo == NULL) {
                struct panfrost_pool *pool =
                        prelink ? &ctx->descs : &batch->pool;

                panfrost_emit_varying_descs(pool, vs, fs, &ctx->streamout, point_coord_mask, linkage);
        }

        struct pipe_stream_output_info *so = &vs->stream_output;
        unsigned present = linkage->present, stride = linkage->stride;
        unsigned xfb_base = pan_xfb_base(present);
        struct panfrost_ptr T =
                pan_pool_alloc_desc_array(&batch->pool.base,
                                          xfb_base +
                                          ctx->streamout.num_targets + 1,
                                          ATTRIBUTE_BUFFER);
        struct mali_attribute_buffer_packed *varyings =
                (struct mali_attribute_buffer_packed *) T.cpu;

        if (buffer_count)
                *buffer_count = xfb_base + ctx->streamout.num_targets;

#if PAN_ARCH >= 6
        /* Suppress prefetch on Bifrost */
        memset(varyings + (xfb_base * ctx->streamout.num_targets), 0, sizeof(*varyings));
#endif

        /* Emit the stream out buffers. We need enough room for all the
         * vertices we emit across all instances */

        unsigned out_count = ctx->instance_count *
                u_stream_outputs_for_vertices(ctx->active_prim, ctx->vertex_count);

        for (unsigned i = 0; i < ctx->streamout.num_targets; ++i) {
                panfrost_emit_streamout(batch, &varyings[xfb_base + i],
                                        so->stride[i] * 4,
                                        out_count,
                                        ctx->streamout.targets[i]);
        }

        if (stride) {
                panfrost_emit_varyings(batch,
                                &varyings[pan_varying_index(present, PAN_VARY_GENERAL)],
                                stride, vertex_count);
        }

        /* fp32 vec4 gl_Position */
        *position = panfrost_emit_varyings(batch,
                        &varyings[pan_varying_index(present, PAN_VARY_POSITION)],
                        sizeof(float) * 4, vertex_count);

        if (present & BITFIELD_BIT(PAN_VARY_PSIZ)) {
                *psiz = panfrost_emit_varyings(batch,
                                &varyings[pan_varying_index(present, PAN_VARY_PSIZ)],
                                2, vertex_count);
        }

#if PAN_ARCH <= 5
        pan_emit_special_input(varyings, present,
                        PAN_VARY_PNTCOORD, MALI_ATTRIBUTE_SPECIAL_POINT_COORD);
        pan_emit_special_input(varyings, present, PAN_VARY_FACE,
                        MALI_ATTRIBUTE_SPECIAL_FRONT_FACING);
        pan_emit_special_input(varyings, present, PAN_VARY_FRAGCOORD,
                        MALI_ATTRIBUTE_SPECIAL_FRAG_COORD);
#endif

        *buffers = T.gpu;
        *vs_attribs = linkage->producer;
        *fs_attribs = linkage->consumer;
}

static void
panfrost_emit_vertex_tiler_jobs(struct panfrost_batch *batch,
                                const struct panfrost_ptr *vertex_job,
                                const struct panfrost_ptr *tiler_job)
{
        struct panfrost_context *ctx = batch->ctx;

        /* If rasterizer discard is enable, only submit the vertex. XXX - set
         * job_barrier in case buffers get ping-ponged and we need to enforce
         * ordering, this has a perf hit! See
         * KHR-GLES31.core.vertex_attrib_binding.advanced-iterations */

        unsigned vertex = panfrost_add_job(&batch->pool.base, &batch->scoreboard,
                                           MALI_JOB_TYPE_VERTEX, true, false,
                                           ctx->indirect_draw ?
                                           batch->indirect_draw_job_id : 0,
                                           0, vertex_job, false);

        if (ctx->rasterizer->base.rasterizer_discard || batch->scissor_culls_everything)
                return;

        panfrost_add_job(&batch->pool.base, &batch->scoreboard,
                         MALI_JOB_TYPE_TILER, false, false,
                         vertex, 0, tiler_job, false);
}

static void
emit_tls(struct panfrost_batch *batch)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        /* Emitted with the FB descriptor on Midgard. */
        if (PAN_ARCH <= 5 && batch->framebuffer.gpu)
                return;

        struct panfrost_bo *tls_bo =
                batch->stack_size ?
                panfrost_batch_get_scratchpad(batch,
                                              batch->stack_size,
                                              dev->thread_tls_alloc,
                                              dev->core_count):
                NULL;
        struct pan_tls_info tls = {
                .tls = {
                        .ptr = tls_bo ? tls_bo->ptr.gpu : 0,
                        .size = batch->stack_size,
                },
        };

        assert(batch->tls.cpu);
        GENX(pan_emit_tls)(&tls, batch->tls.cpu);
}

static void
emit_fbd(struct panfrost_batch *batch, const struct pan_fb_info *fb)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);
        struct panfrost_bo *tls_bo =
                batch->stack_size ?
                panfrost_batch_get_scratchpad(batch,
                                              batch->stack_size,
                                              dev->thread_tls_alloc,
                                              dev->core_count):
                NULL;
        struct pan_tls_info tls = {
                .tls = {
                        .ptr = tls_bo ? tls_bo->ptr.gpu : 0,
                        .size = batch->stack_size,
                },
        };

        batch->framebuffer.gpu |=
                GENX(pan_emit_fbd)(dev, fb, &tls, &batch->tiler_ctx,
                                   batch->framebuffer.cpu);
}

/* Mark a surface as written */

static void
panfrost_initialize_surface(struct panfrost_batch *batch,
                            struct pipe_surface *surf)
{
        if (surf) {
                struct panfrost_resource *rsrc = pan_resource(surf->texture);
                BITSET_SET(rsrc->valid.data, surf->u.tex.level);
        }
}

/* Generate a fragment job. This should be called once per frame. (According to
 * presentations, this is supposed to correspond to eglSwapBuffers) */

static mali_ptr
emit_fragment_job(struct panfrost_batch *batch, const struct pan_fb_info *pfb)
{
        /* Mark the affected buffers as initialized, since we're writing to it.
         * Also, add the surfaces we're writing to to the batch */

        struct pipe_framebuffer_state *fb = &batch->key;

        for (unsigned i = 0; i < fb->nr_cbufs; ++i)
                panfrost_initialize_surface(batch, fb->cbufs[i]);

        panfrost_initialize_surface(batch, fb->zsbuf);

        /* The passed tile coords can be out of range in some cases, so we need
         * to clamp them to the framebuffer size to avoid a TILE_RANGE_FAULT.
         * Theoretically we also need to clamp the coordinates positive, but we
         * avoid that edge case as all four values are unsigned. Also,
         * theoretically we could clamp the minima, but if that has to happen
         * the asserts would fail anyway (since the maxima would get clamped
         * and then be smaller than the minima). An edge case of sorts occurs
         * when no scissors are added to draw, so by default min=~0 and max=0.
         * But that can't happen if any actual drawing occurs (beyond a
         * wallpaper reload), so this is again irrelevant in practice. */

        batch->maxx = MIN2(batch->maxx, fb->width);
        batch->maxy = MIN2(batch->maxy, fb->height);

        /* Rendering region must be at least 1x1; otherwise, there is nothing
         * to do and the whole job chain should have been discarded. */

        assert(batch->maxx > batch->minx);
        assert(batch->maxy > batch->miny);

        struct panfrost_ptr transfer =
                pan_pool_alloc_desc(&batch->pool.base, FRAGMENT_JOB);

        GENX(pan_emit_fragment_job)(pfb, batch->framebuffer.gpu,
                                    transfer.cpu);

        return transfer.gpu;
}

#define DEFINE_CASE(c) case PIPE_PRIM_##c: return MALI_DRAW_MODE_##c;

static uint8_t
pan_draw_mode(enum pipe_prim_type mode)
{
        switch (mode) {
                DEFINE_CASE(POINTS);
                DEFINE_CASE(LINES);
                DEFINE_CASE(LINE_LOOP);
                DEFINE_CASE(LINE_STRIP);
                DEFINE_CASE(TRIANGLES);
                DEFINE_CASE(TRIANGLE_STRIP);
                DEFINE_CASE(TRIANGLE_FAN);
                DEFINE_CASE(QUADS);
                DEFINE_CASE(POLYGON);
#if PAN_ARCH <= 6
                DEFINE_CASE(QUAD_STRIP);
#endif

        default:
                unreachable("Invalid draw mode");
        }
}

#undef DEFINE_CASE

/* Count generated primitives (when there is no geom/tess shaders) for
 * transform feedback */

static void
panfrost_statistics_record(
                struct panfrost_context *ctx,
                const struct pipe_draw_info *info,
                const struct pipe_draw_start_count_bias *draw)
{
        if (!ctx->active_queries)
                return;

        uint32_t prims = u_prims_for_vertices(info->mode, draw->count);
        ctx->prims_generated += prims;

        if (!ctx->streamout.num_targets)
                return;

        ctx->tf_prims_generated += prims;
}

static void
panfrost_update_streamout_offsets(struct panfrost_context *ctx)
{
        for (unsigned i = 0; i < ctx->streamout.num_targets; ++i) {
                unsigned count;

                count = u_stream_outputs_for_vertices(ctx->active_prim,
                                                      ctx->vertex_count);
                pan_so_target(ctx->streamout.targets[i])->offset += count;
        }
}

static inline void
pan_emit_draw_descs(struct panfrost_batch *batch,
                struct MALI_DRAW *d, enum pipe_shader_type st)
{
        d->offset_start = batch->ctx->offset_start;
        d->instance_size = batch->ctx->instance_count > 1 ?
                           batch->ctx->padded_count : 1;

        d->uniform_buffers = batch->uniform_buffers[st];
        d->push_uniforms = batch->push_uniforms[st];
        d->textures = batch->textures[st];
        d->samplers = batch->samplers[st];
}

static inline enum mali_index_type
panfrost_translate_index_size(unsigned size)
{
        STATIC_ASSERT(MALI_INDEX_TYPE_NONE  == 0);
        STATIC_ASSERT(MALI_INDEX_TYPE_UINT8  == 1);
        STATIC_ASSERT(MALI_INDEX_TYPE_UINT16 == 2);

        return (size == 4) ? MALI_INDEX_TYPE_UINT32 : size;
}

static void
panfrost_draw_emit_vertex(struct panfrost_batch *batch,
                          const struct pipe_draw_info *info,
                          void *invocation_template,
                          mali_ptr vs_vary, mali_ptr varyings,
                          mali_ptr attribs, mali_ptr attrib_bufs,
                          void *job)
{
        void *section =
                pan_section_ptr(job, COMPUTE_JOB, INVOCATION);
        memcpy(section, invocation_template, pan_size(INVOCATION));

        pan_section_pack(job, COMPUTE_JOB, PARAMETERS, cfg) {
                cfg.job_task_split = 5;
        }

        pan_section_pack(job, COMPUTE_JOB, DRAW, cfg) {
                cfg.draw_descriptor_is_64b = true;
                cfg.state = batch->rsd[PIPE_SHADER_VERTEX];
                cfg.attributes = attribs;
                cfg.attribute_buffers = attrib_bufs;
                cfg.varyings = vs_vary;
                cfg.varying_buffers = vs_vary ? varyings : 0;
                cfg.thread_storage = batch->tls.gpu;
                pan_emit_draw_descs(batch, &cfg, PIPE_SHADER_VERTEX);
        }
}

static void
panfrost_emit_primitive_size(struct panfrost_context *ctx,
                             bool points, mali_ptr size_array,
                             void *prim_size)
{
        struct panfrost_rasterizer *rast = ctx->rasterizer;

        pan_pack(prim_size, PRIMITIVE_SIZE, cfg) {
                if (panfrost_writes_point_size(ctx)) {
                        cfg.size_array = size_array;
                } else {
                        cfg.constant = points ?
                                       rast->base.point_size :
                                       rast->base.line_width;
                }
        }
}

static bool
panfrost_is_implicit_prim_restart(const struct pipe_draw_info *info)
{
        unsigned implicit_index = (1 << (info->index_size * 8)) - 1;
        bool implicit = info->restart_index == implicit_index;
        return info->primitive_restart && implicit;
}

static inline void
panfrost_update_state_tex(struct panfrost_batch *batch,
                          enum pipe_shader_type st)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_shader_state *ss = panfrost_get_shader_state(ctx, st);

        unsigned dirty_3d = ctx->dirty;
        unsigned dirty = ctx->dirty_shader[st];

        if (dirty & PAN_DIRTY_STAGE_TEXTURE) {
                batch->textures[st] =
                        panfrost_emit_texture_descriptors(batch, st);
        }

        if (dirty & PAN_DIRTY_STAGE_SAMPLER) {
                batch->samplers[st] =
                        panfrost_emit_sampler_descriptors(batch, st);
        }

        if ((dirty & ss->dirty_shader) || (dirty_3d & ss->dirty_3d)) {
                batch->uniform_buffers[st] = panfrost_emit_const_buf(batch, st,
                                &batch->push_uniforms[st]);
        }
}

static inline void
panfrost_update_state_3d(struct panfrost_batch *batch)
{
        unsigned dirty = batch->ctx->dirty;

        if (dirty & (PAN_DIRTY_VIEWPORT | PAN_DIRTY_SCISSOR))
                batch->viewport = panfrost_emit_viewport(batch);

        if (dirty & PAN_DIRTY_TLS_SIZE)
                panfrost_batch_adjust_stack_size(batch);
}

static void
panfrost_update_state_vs(struct panfrost_batch *batch)
{
        enum pipe_shader_type st = PIPE_SHADER_VERTEX;
        unsigned dirty = batch->ctx->dirty_shader[st];

        if (dirty & PAN_DIRTY_STAGE_RENDERER)
                batch->rsd[st] = panfrost_emit_compute_shader_meta(batch, st);

        panfrost_update_state_tex(batch, st);
}

static void
panfrost_update_state_fs(struct panfrost_batch *batch)
{
        enum pipe_shader_type st = PIPE_SHADER_FRAGMENT;
        unsigned dirty = batch->ctx->dirty_shader[st];

        if (dirty & PAN_DIRTY_STAGE_RENDERER)
                batch->rsd[st] = panfrost_emit_frag_shader_meta(batch);

        if (dirty & PAN_DIRTY_STAGE_IMAGE) {
                batch->attribs[st] = panfrost_emit_image_attribs(batch,
                                &batch->attrib_bufs[st], st);
        }

        panfrost_update_state_tex(batch, st);
}

#if PAN_ARCH >= 6
static mali_ptr
panfrost_batch_get_bifrost_tiler(struct panfrost_batch *batch, unsigned vertex_count)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        if (!vertex_count)
                return 0;

        if (batch->tiler_ctx.bifrost)
                return batch->tiler_ctx.bifrost;

        struct panfrost_ptr t =
                pan_pool_alloc_desc(&batch->pool.base, TILER_HEAP);

        GENX(pan_emit_tiler_heap)(dev, t.cpu);

        mali_ptr heap = t.gpu;

        t = pan_pool_alloc_desc(&batch->pool.base, TILER_CONTEXT);
        GENX(pan_emit_tiler_ctx)(dev, batch->key.width, batch->key.height,
                                 util_framebuffer_get_num_samples(&batch->key),
                                 heap, t.cpu);

        batch->tiler_ctx.bifrost = t.gpu;
        return batch->tiler_ctx.bifrost;
}
#endif

static void
panfrost_draw_emit_tiler(struct panfrost_batch *batch,
                         const struct pipe_draw_info *info,
                         const struct pipe_draw_start_count_bias *draw,
                         void *invocation_template,
                         mali_ptr indices, mali_ptr fs_vary, mali_ptr varyings,
                         mali_ptr pos, mali_ptr psiz, void *job)
{
        struct panfrost_context *ctx = batch->ctx;
        struct pipe_rasterizer_state *rast = &ctx->rasterizer->base;

        void *section = pan_section_ptr(job, TILER_JOB, INVOCATION);
        memcpy(section, invocation_template, pan_size(INVOCATION));

        section = pan_section_ptr(job, TILER_JOB, PRIMITIVE);
        pan_pack(section, PRIMITIVE, cfg) {
                cfg.draw_mode = pan_draw_mode(info->mode);
                if (panfrost_writes_point_size(ctx))
                        cfg.point_size_array_format = MALI_POINT_SIZE_ARRAY_FORMAT_FP16;

                /* For line primitives, PRIMITIVE.first_provoking_vertex must
                 * be set to true and the provoking vertex is selected with
                 * DRAW.flat_shading_vertex.
                 */
                if (info->mode == PIPE_PRIM_LINES ||
                    info->mode == PIPE_PRIM_LINE_LOOP ||
                    info->mode == PIPE_PRIM_LINE_STRIP)
                        cfg.first_provoking_vertex = true;
                else
                        cfg.first_provoking_vertex = rast->flatshade_first;

                if (panfrost_is_implicit_prim_restart(info)) {
                        cfg.primitive_restart = MALI_PRIMITIVE_RESTART_IMPLICIT;
                } else if (info->primitive_restart) {
                        cfg.primitive_restart = MALI_PRIMITIVE_RESTART_EXPLICIT;
                        cfg.primitive_restart_index = info->restart_index;
                }

                cfg.job_task_split = 6;

                cfg.index_count = ctx->indirect_draw ? 1 : draw->count;
                cfg.index_type = panfrost_translate_index_size(info->index_size);

                if (cfg.index_type) {
                        cfg.indices = indices;
                        cfg.base_vertex_offset = draw->index_bias - ctx->offset_start;
                }
        }

        bool points = info->mode == PIPE_PRIM_POINTS;
        void *prim_size = pan_section_ptr(job, TILER_JOB, PRIMITIVE_SIZE);

#if PAN_ARCH >= 6
        pan_section_pack(job, TILER_JOB, TILER, cfg) {
                cfg.address = panfrost_batch_get_bifrost_tiler(batch, ~0);
        }

        pan_section_pack(job, TILER_JOB, PADDING, cfg);
#endif

        section = pan_section_ptr(job, TILER_JOB, DRAW);
        pan_pack(section, DRAW, cfg) {
                cfg.four_components_per_vertex = true;
                cfg.draw_descriptor_is_64b = true;
                cfg.front_face_ccw = rast->front_ccw;
                cfg.cull_front_face = rast->cull_face & PIPE_FACE_FRONT;
                cfg.cull_back_face = rast->cull_face & PIPE_FACE_BACK;
                cfg.position = pos;
                cfg.state = batch->rsd[PIPE_SHADER_FRAGMENT];
                cfg.attributes = batch->attribs[PIPE_SHADER_FRAGMENT];
                cfg.attribute_buffers = batch->attrib_bufs[PIPE_SHADER_FRAGMENT];
                cfg.viewport = batch->viewport;
                cfg.varyings = fs_vary;
                cfg.varying_buffers = fs_vary ? varyings : 0;
                cfg.thread_storage = batch->tls.gpu;

                /* For all primitives but lines DRAW.flat_shading_vertex must
                 * be set to 0 and the provoking vertex is selected with the
                 * PRIMITIVE.first_provoking_vertex field.
                 */
                if (info->mode == PIPE_PRIM_LINES ||
                    info->mode == PIPE_PRIM_LINE_LOOP ||
                    info->mode == PIPE_PRIM_LINE_STRIP) {
                        /* The logic is inverted across arches. */
                        cfg.flat_shading_vertex = rast->flatshade_first
                                                ^ (PAN_ARCH <= 5);
                }

                pan_emit_draw_descs(batch, &cfg, PIPE_SHADER_FRAGMENT);

                if (ctx->occlusion_query && ctx->active_queries) {
                        if (ctx->occlusion_query->type == PIPE_QUERY_OCCLUSION_COUNTER)
                                cfg.occlusion_query = MALI_OCCLUSION_MODE_COUNTER;
                        else
                                cfg.occlusion_query = MALI_OCCLUSION_MODE_PREDICATE;

                        struct panfrost_resource *rsrc = pan_resource(ctx->occlusion_query->rsrc);
                        cfg.occlusion = rsrc->image.data.bo->ptr.gpu;
                        panfrost_batch_write_rsrc(ctx->batch, rsrc,
                                              PIPE_SHADER_FRAGMENT);
                }
        }

        panfrost_emit_primitive_size(ctx, points, psiz, prim_size);
}

static void
panfrost_direct_draw(struct panfrost_batch *batch,
                     const struct pipe_draw_info *info,
                     unsigned drawid_offset,
                     const struct pipe_draw_start_count_bias *draw)
{
        if (!draw->count || !info->instance_count)
                return;

        struct panfrost_context *ctx = batch->ctx;

        /* Take into account a negative bias */
        ctx->indirect_draw = false;
        ctx->vertex_count = draw->count + (info->index_size ? abs(draw->index_bias) : 0);
        ctx->instance_count = info->instance_count;
        ctx->base_vertex = info->index_size ? draw->index_bias : 0;
        ctx->base_instance = info->start_instance;
        ctx->active_prim = info->mode;
        ctx->drawid = drawid_offset;

        struct panfrost_ptr tiler =
                pan_pool_alloc_desc(&batch->pool.base, TILER_JOB);
        struct panfrost_ptr vertex =
                pan_pool_alloc_desc(&batch->pool.base, COMPUTE_JOB);

        unsigned vertex_count = ctx->vertex_count;

        unsigned min_index = 0, max_index = 0;
        mali_ptr indices = 0;

        if (info->index_size) {
                indices = panfrost_get_index_buffer_bounded(batch, info, draw,
                                                            &min_index,
                                                            &max_index);

                /* Use the corresponding values */
                vertex_count = max_index - min_index + 1;
                ctx->offset_start = min_index + draw->index_bias;
        } else {
                ctx->offset_start = draw->start;
        }

        if (info->instance_count > 1)
                ctx->padded_count = panfrost_padded_vertex_count(vertex_count);
        else
                ctx->padded_count = vertex_count;

        panfrost_statistics_record(ctx, info, draw);

        struct mali_invocation_packed invocation;
        if (info->instance_count > 1) {
                panfrost_pack_work_groups_compute(&invocation,
                                                  1, vertex_count, info->instance_count,
                                                  1, 1, 1, true, false);
        } else {
                pan_pack(&invocation, INVOCATION, cfg) {
                        cfg.invocations = MALI_POSITIVE(vertex_count);
                        cfg.size_y_shift = 0;
                        cfg.size_z_shift = 0;
                        cfg.workgroups_x_shift = 0;
                        cfg.workgroups_y_shift = 0;
                        cfg.workgroups_z_shift = 32;
                        cfg.thread_group_split = MALI_SPLIT_MIN_EFFICIENT;
                }
        }

        /* Emit all sort of descriptors. */
        mali_ptr varyings = 0, vs_vary = 0, fs_vary = 0, pos = 0, psiz = 0;

        panfrost_emit_varying_descriptor(batch,
                                         ctx->padded_count *
                                         ctx->instance_count,
                                         &vs_vary, &fs_vary, &varyings,
                                         NULL, &pos, &psiz,
                                         info->mode == PIPE_PRIM_POINTS);

        mali_ptr attribs, attrib_bufs;
        attribs = panfrost_emit_vertex_data(batch, &attrib_bufs);

        panfrost_update_state_3d(batch);
        panfrost_update_state_vs(batch);
        panfrost_update_state_fs(batch);
        panfrost_clean_state_3d(ctx);

        /* Fire off the draw itself */
        panfrost_draw_emit_vertex(batch, info, &invocation,
                                  vs_vary, varyings, attribs, attrib_bufs, vertex.cpu);
        panfrost_draw_emit_tiler(batch, info, draw, &invocation, indices,
                                 fs_vary, varyings, pos, psiz, tiler.cpu);
        panfrost_emit_vertex_tiler_jobs(batch, &vertex, &tiler);

        /* Increment transform feedback offsets */
        panfrost_update_streamout_offsets(ctx);
}

static void
panfrost_indirect_draw(struct panfrost_batch *batch,
                       const struct pipe_draw_info *info,
                       unsigned drawid_offset,
                       const struct pipe_draw_indirect_info *indirect,
                       const struct pipe_draw_start_count_bias *draw)
{
        /* Indirect draw count and multi-draw not supported. */
        assert(indirect->draw_count == 1 && !indirect->indirect_draw_count);

        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_device *dev = pan_device(ctx->base.screen);

        /* TODO: update statistics (see panfrost_statistics_record()) */
        /* TODO: Increment transform feedback offsets */
        assert(ctx->streamout.num_targets == 0);

        ctx->active_prim = info->mode;
        ctx->drawid = drawid_offset;
        ctx->indirect_draw = true;

        struct panfrost_ptr tiler =
                pan_pool_alloc_desc(&batch->pool.base, TILER_JOB);
        struct panfrost_ptr vertex =
                pan_pool_alloc_desc(&batch->pool.base, COMPUTE_JOB);

        struct panfrost_shader_state *vs =
                panfrost_get_shader_state(ctx, PIPE_SHADER_VERTEX);

        struct panfrost_bo *index_buf = NULL;

        if (info->index_size) {
                assert(!info->has_user_indices);
                struct panfrost_resource *rsrc = pan_resource(info->index.resource);
                index_buf = rsrc->image.data.bo;
                panfrost_batch_read_rsrc(batch, rsrc, PIPE_SHADER_VERTEX);
        }

        mali_ptr varyings = 0, vs_vary = 0, fs_vary = 0, pos = 0, psiz = 0;
        unsigned varying_buf_count;

        /* We want to create templates, set all count fields to 0 to reflect
         * that.
         */
        ctx->instance_count = ctx->vertex_count = ctx->padded_count = 0;
        ctx->offset_start = 0;

        /* Set the {first,base}_vertex sysvals to NULL. Will be updated if the
         * vertex shader uses gl_VertexID or gl_BaseVertex.
         */
        ctx->first_vertex_sysval_ptr = 0;
        ctx->base_vertex_sysval_ptr = 0;
        ctx->base_instance_sysval_ptr = 0;

        panfrost_update_state_3d(batch);
        panfrost_update_state_vs(batch);
        panfrost_update_state_fs(batch);
        panfrost_clean_state_3d(ctx);

        bool point_coord_replace = (info->mode == PIPE_PRIM_POINTS);

        panfrost_emit_varying_descriptor(batch, 0,
                                         &vs_vary, &fs_vary, &varyings,
                                         &varying_buf_count, &pos, &psiz,
                                         point_coord_replace);

        mali_ptr attribs, attrib_bufs;
        attribs = panfrost_emit_vertex_data(batch, &attrib_bufs);

        /* Zero-ed invocation, the compute job will update it. */
        static struct mali_invocation_packed invocation;

        /* Fire off the draw itself */
        panfrost_draw_emit_vertex(batch, info, &invocation, vs_vary, varyings,
                                  attribs, attrib_bufs, vertex.cpu);
        panfrost_draw_emit_tiler(batch, info, draw, &invocation,
                                 index_buf ? index_buf->ptr.gpu : 0,
                                 fs_vary, varyings, pos, psiz, tiler.cpu);

        /* Add the varying heap BO to the batch if we're allocating varyings. */
        if (varyings) {
                panfrost_batch_add_bo(batch,
                                      dev->indirect_draw_shaders.varying_heap,
                                      PIPE_SHADER_VERTEX);
        }

        assert(indirect->buffer);

        struct panfrost_resource *draw_buf = pan_resource(indirect->buffer);

        /* Don't count images: those attributes don't need to be patched. */
        unsigned attrib_count =
                vs->info.attribute_count -
                util_bitcount(ctx->image_mask[PIPE_SHADER_VERTEX]);

        panfrost_batch_read_rsrc(batch, draw_buf, PIPE_SHADER_VERTEX);

        struct pan_indirect_draw_info draw_info = {
                .last_indirect_draw = batch->indirect_draw_job_id,
                .draw_buf = draw_buf->image.data.bo->ptr.gpu + indirect->offset,
                .index_buf = index_buf ? index_buf->ptr.gpu : 0,
                .first_vertex_sysval = ctx->first_vertex_sysval_ptr,
                .base_vertex_sysval = ctx->base_vertex_sysval_ptr,
                .base_instance_sysval = ctx->base_instance_sysval_ptr,
                .vertex_job = vertex.gpu,
                .tiler_job = tiler.gpu,
                .attrib_bufs = attrib_bufs,
                .attribs = attribs,
                .attrib_count = attrib_count,
                .varying_bufs = varyings,
                .index_size = info->index_size,
        };

        if (panfrost_writes_point_size(ctx))
                draw_info.flags |= PAN_INDIRECT_DRAW_UPDATE_PRIM_SIZE;

        if (vs->info.vs.writes_point_size)
                draw_info.flags |= PAN_INDIRECT_DRAW_HAS_PSIZ;


        if (info->primitive_restart) {
                draw_info.restart_index = info->restart_index;
                draw_info.flags |= PAN_INDIRECT_DRAW_PRIMITIVE_RESTART;
        }

        batch->indirect_draw_job_id =
                GENX(panfrost_emit_indirect_draw)(&batch->pool.base,
                                                  &batch->scoreboard,
                                                  &draw_info,
                                                  &batch->indirect_draw_ctx);

        panfrost_emit_vertex_tiler_jobs(batch, &vertex, &tiler);
}

static void
panfrost_draw_vbo(struct pipe_context *pipe,
                  const struct pipe_draw_info *info,
                  unsigned drawid_offset,
                  const struct pipe_draw_indirect_info *indirect,
                  const struct pipe_draw_start_count_bias *draws,
                  unsigned num_draws)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct panfrost_device *dev = pan_device(pipe->screen);

        if (!panfrost_render_condition_check(ctx))
                return;

        /* Emulate indirect draws unless we're using the experimental path */
        if (!(dev->debug & PAN_DBG_INDIRECT) && indirect && indirect->buffer) {
                assert(num_draws == 1);
                util_draw_indirect(pipe, info, indirect);
                return;
        }

        /* Do some common setup */
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);

        /* Don't add too many jobs to a single batch. Hardware has a hard limit
         * of 65536 jobs, but we choose a smaller soft limit (arbitrary) to
         * avoid the risk of timeouts. This might not be a good idea. */
        if (unlikely(batch->scoreboard.job_index > 10000))
                batch = panfrost_get_fresh_batch_for_fbo(ctx, "Too many draws");

        unsigned zs_draws = ctx->depth_stencil->draws;
        batch->draws |= zs_draws;
        batch->resolve |= zs_draws;

        /* Mark everything dirty when debugging */
        if (unlikely(dev->debug & PAN_DBG_DIRTY))
                panfrost_dirty_state_all(ctx);

        /* Conservatively assume draw parameters always change */
        ctx->dirty |= PAN_DIRTY_PARAMS | PAN_DIRTY_DRAWID;

        if (indirect) {
                assert(num_draws == 1);

                if (indirect->count_from_stream_output) {
                        struct pipe_draw_start_count_bias tmp_draw = *draws;
                        struct panfrost_streamout_target *so =
                                pan_so_target(indirect->count_from_stream_output);

                        tmp_draw.start = 0;
                        tmp_draw.count = so->offset;
                        tmp_draw.index_bias = 0;
                        panfrost_direct_draw(batch, info, drawid_offset, &tmp_draw);
                        return;
                }

                panfrost_indirect_draw(batch, info, drawid_offset, indirect, &draws[0]);
                return;
        }

        struct pipe_draw_info tmp_info = *info;
        unsigned drawid = drawid_offset;

        for (unsigned i = 0; i < num_draws; i++) {
                panfrost_direct_draw(batch, &tmp_info, drawid, &draws[i]);

                if (tmp_info.increment_draw_id) {
                        ctx->dirty |= PAN_DIRTY_DRAWID;
                        drawid++;
                }
        }

}

/* Launch grid is the compute equivalent of draw_vbo, so in this routine, we
 * construct the COMPUTE job and some of its payload.
 */

static void
panfrost_launch_grid(struct pipe_context *pipe,
                const struct pipe_grid_info *info)
{
        struct panfrost_context *ctx = pan_context(pipe);

        /* XXX - shouldn't be necessary with working memory barriers. Affected
         * test: KHR-GLES31.core.compute_shader.pipeline-post-xfb */
        panfrost_flush_all_batches(ctx, "Launch grid pre-barrier");

        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);

        struct panfrost_shader_state *cs =
                &ctx->shader[PIPE_SHADER_COMPUTE]->variants[0];

        /* Indirect dispatch can't handle workgroup local storage since that
         * would require dynamic memory allocation. Bail in this case. */
        if (info->indirect && !cs->info.wls_size) {
                struct pipe_transfer *transfer;
                uint32_t *params = pipe_buffer_map_range(pipe, info->indirect,
                                info->indirect_offset,
                                3 * sizeof(uint32_t),
                                PIPE_MAP_READ,
                                &transfer);

                struct pipe_grid_info direct = *info;
                direct.indirect = NULL;
                direct.grid[0] = params[0];
                direct.grid[1] = params[1];
                direct.grid[2] = params[2];
                pipe_buffer_unmap(pipe, transfer);

                if (params[0] && params[1] && params[2])
                        panfrost_launch_grid(pipe, &direct);

                return;
        }

        ctx->compute_grid = info;

        struct panfrost_ptr t =
                pan_pool_alloc_desc(&batch->pool.base, COMPUTE_JOB);

        /* We implement OpenCL inputs as uniforms (or a UBO -- same thing), so
         * reuse the graphics path for this by lowering to Gallium */

        struct pipe_constant_buffer ubuf = {
                .buffer = NULL,
                .buffer_offset = 0,
                .buffer_size = ctx->shader[PIPE_SHADER_COMPUTE]->cbase.req_input_mem,
                .user_buffer = info->input
        };

        if (info->input)
                pipe->set_constant_buffer(pipe, PIPE_SHADER_COMPUTE, 0, false, &ubuf);

        /* Invoke according to the grid info */

        void *invocation =
                pan_section_ptr(t.cpu, COMPUTE_JOB, INVOCATION);
        unsigned num_wg[3] = { info->grid[0], info->grid[1], info->grid[2] };

        if (info->indirect)
                num_wg[0] = num_wg[1] = num_wg[2] = 1;

        panfrost_pack_work_groups_compute(invocation,
                                          num_wg[0], num_wg[1], num_wg[2],
                                          info->block[0], info->block[1],
                                          info->block[2],
                                          false, info->indirect != NULL);

        pan_section_pack(t.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
                cfg.job_task_split =
                        util_logbase2_ceil(info->block[0] + 1) +
                        util_logbase2_ceil(info->block[1] + 1) +
                        util_logbase2_ceil(info->block[2] + 1);
        }

        pan_section_pack(t.cpu, COMPUTE_JOB, DRAW, cfg) {
                cfg.draw_descriptor_is_64b = true;
                cfg.state = panfrost_emit_compute_shader_meta(batch, PIPE_SHADER_COMPUTE);
                cfg.attributes = panfrost_emit_image_attribs(batch, &cfg.attribute_buffers, PIPE_SHADER_COMPUTE);
                cfg.thread_storage = panfrost_emit_shared_memory(batch, info);
                cfg.uniform_buffers = panfrost_emit_const_buf(batch,
                                PIPE_SHADER_COMPUTE, &cfg.push_uniforms);
                cfg.textures = panfrost_emit_texture_descriptors(batch,
                                PIPE_SHADER_COMPUTE);
                cfg.samplers = panfrost_emit_sampler_descriptors(batch,
                                PIPE_SHADER_COMPUTE);
        }

        unsigned indirect_dep = 0;
        if (info->indirect) {
                struct pan_indirect_dispatch_info indirect = {
                        .job = t.gpu,
                        .indirect_dim = pan_resource(info->indirect)->image.data.bo->ptr.gpu +
                                        info->indirect_offset,
                        .num_wg_sysval = {
                                batch->num_wg_sysval[0],
                                batch->num_wg_sysval[1],
                                batch->num_wg_sysval[2],
                        },
                };

                indirect_dep = GENX(pan_indirect_dispatch_emit)(&batch->pool.base,
                                                                &batch->scoreboard,
                                                                &indirect);
        }

        panfrost_add_job(&batch->pool.base, &batch->scoreboard,
                         MALI_JOB_TYPE_COMPUTE, true, false,
                         indirect_dep, 0, &t, false);
        panfrost_flush_all_batches(ctx, "Launch grid post-barrier");
}

static void *
panfrost_create_rasterizer_state(
        struct pipe_context *pctx,
        const struct pipe_rasterizer_state *cso)
{
        struct panfrost_rasterizer *so = CALLOC_STRUCT(panfrost_rasterizer);

        so->base = *cso;

        /* Gauranteed with the core GL call, so don't expose ARB_polygon_offset */
        assert(cso->offset_clamp == 0.0);

        pan_pack(&so->multisample, MULTISAMPLE_MISC, cfg) {
                cfg.multisample_enable = cso->multisample;
                cfg.fixed_function_near_discard = cso->depth_clip_near;
                cfg.fixed_function_far_discard = cso->depth_clip_far;
                cfg.shader_depth_range_fixed = true;
        }

        pan_pack(&so->stencil_misc, STENCIL_MASK_MISC, cfg) {
                cfg.depth_range_1 = cso->offset_tri;
                cfg.depth_range_2 = cso->offset_tri;
                cfg.single_sampled_lines = !cso->multisample;
        }

        return so;
}

/* Assigns a vertex buffer for a given (index, divisor) tuple */

static unsigned
pan_assign_vertex_buffer(struct pan_vertex_buffer *buffers,
                         unsigned *nr_bufs,
                         unsigned vbi,
                         unsigned divisor)
{
        /* Look up the buffer */
        for (unsigned i = 0; i < (*nr_bufs); ++i) {
                if (buffers[i].vbi == vbi && buffers[i].divisor == divisor)
                        return i;
        }

        /* Else, create a new buffer */
        unsigned idx = (*nr_bufs)++;

        buffers[idx] = (struct pan_vertex_buffer) {
                .vbi = vbi,
                .divisor = divisor
        };

        return idx;
}

static void *
panfrost_create_vertex_elements_state(
        struct pipe_context *pctx,
        unsigned num_elements,
        const struct pipe_vertex_element *elements)
{
        struct panfrost_vertex_state *so = CALLOC_STRUCT(panfrost_vertex_state);
        struct panfrost_device *dev = pan_device(pctx->screen);

        so->num_elements = num_elements;
        memcpy(so->pipe, elements, sizeof(*elements) * num_elements);

        /* Assign attribute buffers corresponding to the vertex buffers, keyed
         * for a particular divisor since that's how instancing works on Mali */
        for (unsigned i = 0; i < num_elements; ++i) {
                so->element_buffer[i] = pan_assign_vertex_buffer(
                                so->buffers, &so->nr_bufs,
                                elements[i].vertex_buffer_index,
                                elements[i].instance_divisor);
        }

        for (int i = 0; i < num_elements; ++i) {
                enum pipe_format fmt = elements[i].src_format;
                const struct util_format_description *desc = util_format_description(fmt);
                so->formats[i] = dev->formats[desc->format].hw;
                assert(so->formats[i]);
        }

        /* Let's also prepare vertex builtins */
        so->formats[PAN_VERTEX_ID] = dev->formats[PIPE_FORMAT_R32_UINT].hw;
        so->formats[PAN_INSTANCE_ID] = dev->formats[PIPE_FORMAT_R32_UINT].hw;

        return so;
}

static inline unsigned
pan_pipe_to_stencil_op(enum pipe_stencil_op in)
{
        switch (in) {
        case PIPE_STENCIL_OP_KEEP: return MALI_STENCIL_OP_KEEP;
        case PIPE_STENCIL_OP_ZERO: return MALI_STENCIL_OP_ZERO;
        case PIPE_STENCIL_OP_REPLACE: return MALI_STENCIL_OP_REPLACE;
        case PIPE_STENCIL_OP_INCR: return MALI_STENCIL_OP_INCR_SAT;
        case PIPE_STENCIL_OP_DECR: return MALI_STENCIL_OP_DECR_SAT;
        case PIPE_STENCIL_OP_INCR_WRAP: return MALI_STENCIL_OP_INCR_WRAP;
        case PIPE_STENCIL_OP_DECR_WRAP: return MALI_STENCIL_OP_DECR_WRAP;
        case PIPE_STENCIL_OP_INVERT: return MALI_STENCIL_OP_INVERT;
        default: unreachable("Invalid stencil op");
        }
}

static inline void
pan_pipe_to_stencil(const struct pipe_stencil_state *in,
                    struct mali_stencil_packed *out)
{
        pan_pack(out, STENCIL, s) {
                s.mask = in->valuemask;
                s.compare_function = (enum mali_func) in->func;
                s.stencil_fail = pan_pipe_to_stencil_op(in->fail_op);
                s.depth_fail = pan_pipe_to_stencil_op(in->zfail_op);
                s.depth_pass = pan_pipe_to_stencil_op(in->zpass_op);
        }
}

static void *
panfrost_create_depth_stencil_state(struct pipe_context *pipe,
                                    const struct pipe_depth_stencil_alpha_state *zsa)
{
        struct panfrost_zsa_state *so = CALLOC_STRUCT(panfrost_zsa_state);
        so->base = *zsa;

        /* Normalize (there's no separate enable) */
        if (!zsa->alpha_enabled)
                so->base.alpha_func = MALI_FUNC_ALWAYS;

        /* Prepack relevant parts of the Renderer State Descriptor. They will
         * be ORed in at draw-time */
        pan_pack(&so->rsd_depth, MULTISAMPLE_MISC, cfg) {
                cfg.depth_function = zsa->depth_enabled ?
                        (enum mali_func) zsa->depth_func : MALI_FUNC_ALWAYS;

                cfg.depth_write_mask = zsa->depth_writemask;
        }

        pan_pack(&so->rsd_stencil, STENCIL_MASK_MISC, cfg) {
                cfg.stencil_enable = zsa->stencil[0].enabled;

                cfg.stencil_mask_front = zsa->stencil[0].writemask;
                cfg.stencil_mask_back = zsa->stencil[1].enabled ?
                        zsa->stencil[1].writemask : zsa->stencil[0].writemask;

#if PAN_ARCH <= 5
                cfg.alpha_test_compare_function =
                        (enum mali_func) so->base.alpha_func;
#endif
        }

        /* Stencil tests have their own words in the RSD */
        pan_pipe_to_stencil(&zsa->stencil[0], &so->stencil_front);

        if (zsa->stencil[1].enabled)
                pan_pipe_to_stencil(&zsa->stencil[1], &so->stencil_back);
	else
                so->stencil_back = so->stencil_front;

        so->enabled = zsa->stencil[0].enabled ||
                (zsa->depth_enabled && zsa->depth_func != PIPE_FUNC_ALWAYS);

        /* Write masks need tracking together */
        if (zsa->depth_writemask)
                so->draws |= PIPE_CLEAR_DEPTH;

        if (zsa->stencil[0].enabled)
                so->draws |= PIPE_CLEAR_STENCIL;

        /* TODO: Bounds test should be easy */
        assert(!zsa->depth_bounds_test);

        return so;
}

static struct pipe_sampler_view *
panfrost_create_sampler_view(
        struct pipe_context *pctx,
        struct pipe_resource *texture,
        const struct pipe_sampler_view *template)
{
        struct panfrost_context *ctx = pan_context(pctx);
        struct panfrost_sampler_view *so = rzalloc(pctx, struct panfrost_sampler_view);

        pan_legalize_afbc_format(ctx, pan_resource(texture), template->format);

        pipe_reference(NULL, &texture->reference);

        so->base = *template;
        so->base.texture = texture;
        so->base.reference.count = 1;
        so->base.context = pctx;

        panfrost_create_sampler_view_bo(so, pctx, texture);

        return (struct pipe_sampler_view *) so;
}

/* A given Gallium blend state can be encoded to the hardware in numerous,
 * dramatically divergent ways due to the interactions of blending with
 * framebuffer formats. Conceptually, there are two modes:
 *
 * - Fixed-function blending (for suitable framebuffer formats, suitable blend
 *   state, and suitable blend constant)
 *
 * - Blend shaders (for everything else)
 *
 * A given Gallium blend configuration will compile to exactly one
 * fixed-function blend state, if it compiles to any, although the constant
 * will vary across runs as that is tracked outside of the Gallium CSO.
 *
 * However, that same blend configuration will compile to many different blend
 * shaders, depending on the framebuffer formats active. The rationale is that
 * blend shaders override not just fixed-function blending but also
 * fixed-function format conversion, so blend shaders are keyed to a particular
 * framebuffer format. As an example, the tilebuffer format is identical for
 * RG16F and RG16UI -- both are simply 32-bit raw pixels -- so both require
 * blend shaders.
 *
 * All of this state is encapsulated in the panfrost_blend_state struct
 * (our subclass of pipe_blend_state).
 */

/* Create a blend CSO. Essentially, try to compile a fixed-function
 * expression and initialize blend shaders */

static void *
panfrost_create_blend_state(struct pipe_context *pipe,
                            const struct pipe_blend_state *blend)
{
        struct panfrost_blend_state *so = CALLOC_STRUCT(panfrost_blend_state);
        so->base = *blend;

        so->pan.logicop_enable = blend->logicop_enable;
        so->pan.logicop_func = blend->logicop_func;
        so->pan.rt_count = blend->max_rt + 1;

        for (unsigned c = 0; c < so->pan.rt_count; ++c) {
                unsigned g = blend->independent_blend_enable ? c : 0;
                const struct pipe_rt_blend_state pipe = blend->rt[g];
                struct pan_blend_equation equation = {0};

                equation.color_mask = pipe.colormask;
                equation.blend_enable = pipe.blend_enable;

                if (pipe.blend_enable) {
                        equation.rgb_func = util_blend_func_to_shader(pipe.rgb_func);
                        equation.rgb_src_factor = util_blend_factor_to_shader(pipe.rgb_src_factor);
                        equation.rgb_invert_src_factor = util_blend_factor_is_inverted(pipe.rgb_src_factor);
                        equation.rgb_dst_factor = util_blend_factor_to_shader(pipe.rgb_dst_factor);
                        equation.rgb_invert_dst_factor = util_blend_factor_is_inverted(pipe.rgb_dst_factor);
                        equation.alpha_func = util_blend_func_to_shader(pipe.alpha_func);
                        equation.alpha_src_factor = util_blend_factor_to_shader(pipe.alpha_src_factor);
                        equation.alpha_invert_src_factor = util_blend_factor_is_inverted(pipe.alpha_src_factor);
                        equation.alpha_dst_factor = util_blend_factor_to_shader(pipe.alpha_dst_factor);
                        equation.alpha_invert_dst_factor = util_blend_factor_is_inverted(pipe.alpha_dst_factor);
                }

                /* Determine some common properties */
                unsigned constant_mask = pan_blend_constant_mask(equation);
                const bool supports_2src = pan_blend_supports_2src(PAN_ARCH);
                so->info[c] = (struct pan_blend_info) {
                        .no_colour = (equation.color_mask == 0),
                        .opaque = pan_blend_is_opaque(equation),
                        .constant_mask = constant_mask,

                        /* TODO: check the dest for the logicop */
                        .load_dest = blend->logicop_enable ||
                                pan_blend_reads_dest(equation),

                        /* Could this possibly be fixed-function? */
                        .fixed_function = !blend->logicop_enable &&
                                pan_blend_can_fixed_function(equation,
                                                             supports_2src) &&
                                (!constant_mask ||
                                 pan_blend_supports_constant(PAN_ARCH, c))
                };

                so->pan.rts[c].equation = equation;

                /* Bifrost needs to know if any render target loads its
                 * destination in the hot draw path, so precompute this */
                if (so->info[c].load_dest)
                        so->load_dest_mask |= BITFIELD_BIT(c);

                /* Converting equations to Mali style is expensive, do it at
                 * CSO create time instead of draw-time */
                if (so->info[c].fixed_function) {
                        so->equation[c] = pan_pack_blend(equation);
                }
        }

        return so;
}

static void
prepare_rsd(struct panfrost_shader_state *state,
            struct panfrost_pool *pool, bool upload)
{
        struct mali_renderer_state_packed *out =
                (struct mali_renderer_state_packed *)&state->partial_rsd;

        if (upload) {
                struct panfrost_ptr ptr =
                        pan_pool_alloc_desc(&pool->base, RENDERER_STATE);

                state->state = panfrost_pool_take_ref(pool, ptr.gpu);
                out = ptr.cpu;
        }

        pan_pack(out, RENDERER_STATE, cfg) {
                pan_shader_prepare_rsd(&state->info, state->bin.gpu, &cfg);
        }
}

static void
panfrost_get_sample_position(struct pipe_context *context,
                             unsigned sample_count,
                             unsigned sample_index,
                             float *out_value)
{
        panfrost_query_sample_position(
                        panfrost_sample_pattern(sample_count),
                        sample_index,
                        out_value);
}

static void
screen_destroy(struct pipe_screen *pscreen)
{
        struct panfrost_device *dev = pan_device(pscreen);
        GENX(panfrost_cleanup_indirect_draw_shaders)(dev);
        GENX(pan_indirect_dispatch_cleanup)(dev);
        GENX(pan_blitter_cleanup)(dev);
}

static void
preload(struct panfrost_batch *batch, struct pan_fb_info *fb)
{
        GENX(pan_preload_fb)(&batch->pool.base, &batch->scoreboard, fb, batch->tls.gpu,
                             PAN_ARCH >= 6 ? batch->tiler_ctx.bifrost : 0, NULL);
}

static void
init_batch(struct panfrost_batch *batch)
{
        /* Reserve the framebuffer and local storage descriptors */
        batch->framebuffer =
#if PAN_ARCH == 4
                pan_pool_alloc_desc(&batch->pool.base, FRAMEBUFFER);
#else
                pan_pool_alloc_desc_aggregate(&batch->pool.base,
                                              PAN_DESC(FRAMEBUFFER),
                                              PAN_DESC(ZS_CRC_EXTENSION),
                                              PAN_DESC_ARRAY(MAX2(batch->key.nr_cbufs, 1), RENDER_TARGET));

                batch->framebuffer.gpu |= MALI_FBD_TAG_IS_MFBD;
#endif

#if PAN_ARCH >= 6
        batch->tls = pan_pool_alloc_desc(&batch->pool.base, LOCAL_STORAGE);
#else
        /* On Midgard, the TLS is embedded in the FB descriptor */
        batch->tls = batch->framebuffer;
#endif
}

static void
panfrost_sampler_view_destroy(
        struct pipe_context *pctx,
        struct pipe_sampler_view *pview)
{
        struct panfrost_sampler_view *view = (struct panfrost_sampler_view *) pview;

        pipe_resource_reference(&pview->texture, NULL);
        panfrost_bo_unreference(view->state.bo);
        ralloc_free(view);
}

static void
context_init(struct pipe_context *pipe)
{
        pipe->draw_vbo           = panfrost_draw_vbo;
        pipe->launch_grid        = panfrost_launch_grid;

        pipe->create_vertex_elements_state = panfrost_create_vertex_elements_state;
        pipe->create_rasterizer_state = panfrost_create_rasterizer_state;
        pipe->create_depth_stencil_alpha_state = panfrost_create_depth_stencil_state;
        pipe->create_sampler_view = panfrost_create_sampler_view;
        pipe->sampler_view_destroy = panfrost_sampler_view_destroy;
        pipe->create_sampler_state = panfrost_create_sampler_state;
        pipe->create_blend_state = panfrost_create_blend_state;

        pipe->get_sample_position = panfrost_get_sample_position;
}

#if PAN_ARCH <= 5

/* Returns the polygon list's GPU address if available, or otherwise allocates
 * the polygon list.  It's perfectly fast to use allocate/free BO directly,
 * since we'll hit the BO cache and this is one-per-batch anyway. */

static mali_ptr
batch_get_polygon_list(struct panfrost_batch *batch)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        if (!batch->tiler_ctx.midgard.polygon_list) {
                bool has_draws = batch->scoreboard.first_tiler != NULL;
                unsigned size =
                        panfrost_tiler_get_polygon_list_size(dev,
                                                             batch->key.width,
                                                             batch->key.height,
                                                             has_draws);
                size = util_next_power_of_two(size);

                /* Create the BO as invisible if we can. In the non-hierarchical tiler case,
                 * we need to write the polygon list manually because there's not WRITE_VALUE
                 * job in the chain (maybe we should add one...). */
                bool init_polygon_list = !has_draws && (dev->quirks & MIDGARD_NO_HIER_TILING);
                batch->tiler_ctx.midgard.polygon_list =
                        panfrost_batch_create_bo(batch, size,
                                                 init_polygon_list ? 0 : PAN_BO_INVISIBLE,
                                                 PIPE_SHADER_VERTEX,
                                                 "Polygon list");
                panfrost_batch_add_bo(batch, batch->tiler_ctx.midgard.polygon_list,
                                PIPE_SHADER_FRAGMENT);

                if (init_polygon_list) {
                        assert(batch->tiler_ctx.midgard.polygon_list->ptr.cpu);
                        uint32_t *polygon_list_body =
                                batch->tiler_ctx.midgard.polygon_list->ptr.cpu +
                                MALI_MIDGARD_TILER_MINIMUM_HEADER_SIZE;

                        /* Magic for Mali T720 */
                        polygon_list_body[0] = 0xa0000000;
                }

                batch->tiler_ctx.midgard.disable = !has_draws;
        }

        return batch->tiler_ctx.midgard.polygon_list->ptr.gpu;
}
#endif

static void
init_polygon_list(struct panfrost_batch *batch)
{
#if PAN_ARCH <= 5
        mali_ptr polygon_list = batch_get_polygon_list(batch);
        panfrost_scoreboard_initialize_tiler(&batch->pool.base,
                                             &batch->scoreboard,
                                             polygon_list);
#endif
}

void
GENX(panfrost_cmdstream_screen_init)(struct panfrost_screen *screen)
{
        struct panfrost_device *dev = &screen->dev;

        screen->vtbl.prepare_rsd = prepare_rsd;
        screen->vtbl.emit_tls    = emit_tls;
        screen->vtbl.emit_fbd    = emit_fbd;
        screen->vtbl.emit_fragment_job = emit_fragment_job;
        screen->vtbl.screen_destroy = screen_destroy;
        screen->vtbl.preload     = preload;
        screen->vtbl.context_init = context_init;
        screen->vtbl.init_batch = init_batch;
        screen->vtbl.get_blend_shader = GENX(pan_blend_get_shader_locked);
        screen->vtbl.init_polygon_list = init_polygon_list;
        screen->vtbl.get_compiler_options = GENX(pan_shader_get_compiler_options);
        screen->vtbl.compile_shader = GENX(pan_shader_compile);

        GENX(pan_blitter_init)(dev, &screen->blitter.bin_pool.base,
                               &screen->blitter.desc_pool.base);
        GENX(pan_indirect_dispatch_init)(dev);
        GENX(panfrost_init_indirect_draw_shaders)(dev, &screen->indirect_draw.bin_pool.base);
}
