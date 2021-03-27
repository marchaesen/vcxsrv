/*
 * Copyright (C) 2020 Collabora, Ltd.
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
 *
 * Authors:
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include <math.h>
#include <stdio.h>
#include "pan_encoder.h"
#include "pan_pool.h"
#include "pan_shader.h"
#include "pan_scoreboard.h"
#include "pan_texture.h"
#include "panfrost-quirks.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_math.h"

/* On Midgard, the native blit infrastructure (via MFBD preloads) is broken or
 * missing in many cases. We instead use software paths as fallbacks to
 * implement blits, which are done as TILER jobs. No vertex shader is
 * necessary since we can supply screen-space coordinates directly.
 *
 * This is primarily designed as a fallback for preloads but could be extended
 * for other clears/blits if needed in the future. */

static void
panfrost_build_blit_shader(struct panfrost_device *dev,
                           gl_frag_result loc,
                           nir_alu_type T,
                           bool ms,
                           struct util_dynarray *binary,
                           struct pan_shader_info *info)
{
        bool is_colour = loc >= FRAG_RESULT_DATA0;

        nir_builder _b =
           nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                          pan_shader_get_compiler_options(dev),
                                          "pan_blit");
        nir_builder *b = &_b;
        nir_shader *shader = b->shader;

        shader->info.internal = true;

        nir_variable *c_src = nir_variable_create(shader, nir_var_shader_in, glsl_vector_type(GLSL_TYPE_FLOAT, 2), "coord");
        nir_variable *c_out = nir_variable_create(shader, nir_var_shader_out, glsl_vector_type(
                                GLSL_TYPE_FLOAT, is_colour ? 4 : 1), "out");

        c_src->data.location = VARYING_SLOT_TEX0;
        c_out->data.location = loc;

        nir_ssa_def *coord = nir_load_var(b, c_src);

        nir_tex_instr *tex = nir_tex_instr_create(shader, ms ? 3 : 1);

        tex->dest_type = T;

        if (ms) {
                tex->src[0].src_type = nir_tex_src_coord;
                tex->src[0].src = nir_src_for_ssa(nir_f2i32(b, coord));
                tex->coord_components = 2;
 
                tex->src[1].src_type = nir_tex_src_ms_index;
                tex->src[1].src = nir_src_for_ssa(nir_load_sample_id(b));

                tex->src[2].src_type = nir_tex_src_lod;
                tex->src[2].src = nir_src_for_ssa(nir_imm_int(b, 0));
                tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
                tex->op = nir_texop_txf_ms;
        } else {
                tex->op = nir_texop_tex;

                tex->src[0].src_type = nir_tex_src_coord;
                tex->src[0].src = nir_src_for_ssa(coord);
                tex->coord_components = 2;

                tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
        }

        nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, NULL);
        nir_builder_instr_insert(b, &tex->instr);

        if (is_colour) {
                nir_store_var(b, c_out, &tex->dest.ssa, 0xFF);
        } else {
                unsigned c = loc == FRAG_RESULT_STENCIL ? 1 : 0;
                nir_store_var(b, c_out, nir_channel(b, &tex->dest.ssa, c), 0xFF);
        }

        struct panfrost_compile_inputs inputs = {
                .gpu_id = dev->gpu_id,
                .is_blit = true,
        };

        pan_shader_compile(dev, shader, &inputs, binary, info);

        ralloc_free(shader);
}

/* Compile and upload all possible blit shaders ahead-of-time to reduce draw
 * time overhead. There's only ~30 of them at the moment, so this is fine */

void
panfrost_init_blit_shaders(struct panfrost_device *dev)
{
        static const struct {
                gl_frag_result loc;
                unsigned types;
        } shader_descs[] = {
                { FRAG_RESULT_DEPTH,   1 << PAN_BLIT_FLOAT },
                { FRAG_RESULT_STENCIL, 1 << PAN_BLIT_UINT },
                { FRAG_RESULT_DATA0,  ~0 },
                { FRAG_RESULT_DATA1,  ~0 },
                { FRAG_RESULT_DATA2,  ~0 },
                { FRAG_RESULT_DATA3,  ~0 },
                { FRAG_RESULT_DATA4,  ~0 },
                { FRAG_RESULT_DATA5,  ~0 },
                { FRAG_RESULT_DATA6,  ~0 },
                { FRAG_RESULT_DATA7,  ~0 }
        };

        nir_alu_type nir_types[PAN_BLIT_NUM_TYPES] = {
                nir_type_float32,
                nir_type_uint32,
                nir_type_int32
        };

        /* Total size = # of shaders * bytes per shader. There are
         * shaders for each RT (so up to DATA7 -- overestimate is
         * okay) and up to NUM_TYPES variants of each, * 2 for multisampling
         * variants. These shaders are simple enough that they should be less
         * than 8 quadwords each (again, overestimate is fine). */

        unsigned offset = 0;
        unsigned total_size = (FRAG_RESULT_DATA7 * PAN_BLIT_NUM_TYPES) * (8 * 16) * 2;

        if (pan_is_bifrost(dev))
                total_size *= 4;

        dev->blit_shaders.bo = panfrost_bo_create(dev, total_size, PAN_BO_EXECUTE);

        /* Don't bother generating multisampling variants if we don't actually
         * support multisampling */
        bool has_ms = !(dev->quirks & MIDGARD_SFBD);
        struct util_dynarray binary;

        util_dynarray_init(&binary, NULL);

        for (unsigned ms = 0; ms <= has_ms; ++ms) {
                for (unsigned i = 0; i < ARRAY_SIZE(shader_descs); ++i) {
                        unsigned loc = shader_descs[i].loc;

                        for (enum pan_blit_type T = 0; T < PAN_BLIT_NUM_TYPES; ++T) {
                                if (!(shader_descs[i].types & (1 << T)))
                                        continue;

                                struct pan_blit_shader *shader = &dev->blit_shaders.loads[loc][T][ms];
                                struct pan_shader_info info;

                                util_dynarray_clear(&binary);
                                panfrost_build_blit_shader(dev, loc,
                                                           nir_types[T], ms,
                                                           &binary, &info);

                                assert(offset + binary.size < total_size);
                                memcpy(dev->blit_shaders.bo->ptr.cpu + offset,
                                       binary.data, binary.size);

                                shader->shader = (dev->blit_shaders.bo->ptr.gpu + offset);
                                if (pan_is_bifrost(dev)) {
                                        int rt = loc - FRAG_RESULT_DATA0;
                                        if (rt >= 0 && rt < 8 &&
                                            info.bifrost.blend[rt].return_offset) {
                                                shader->blend_ret_addr =
                                                        shader->shader +
                                                        info.bifrost.blend[rt].return_offset;
                                        }
                                } else {
                                        shader->shader |= info.midgard.first_tag;
                                }


                                offset += ALIGN_POT(binary.size,
                                                    pan_is_bifrost(dev) ? 128 : 64);
                        }
                }
        }

        util_dynarray_fini(&binary);
}

static void
panfrost_load_emit_viewport(struct pan_pool *pool, struct MALI_DRAW *draw,
                            struct pan_image *image)
{
        struct panfrost_ptr t = panfrost_pool_alloc_desc(pool, VIEWPORT);
        unsigned width = u_minify(image->width0, image->first_level);
        unsigned height = u_minify(image->height0, image->first_level);

        pan_pack(t.cpu, VIEWPORT, cfg) {
                cfg.scissor_maximum_x = width - 1; /* Inclusive */
                cfg.scissor_maximum_y = height - 1;
        }

        draw->viewport = t.gpu;
}

static void
panfrost_load_prepare_rsd(struct pan_pool *pool, struct MALI_RENDERER_STATE *state,
                          struct pan_image *image, unsigned loc)
{
        /* Determine the sampler type needed. Stencil is always sampled as
         * UINT. Pure (U)INT is always (U)INT. Everything else is FLOAT. */
        enum pan_blit_type T =
                (loc == FRAG_RESULT_STENCIL) ? PAN_BLIT_UINT :
                (util_format_is_pure_uint(image->format)) ? PAN_BLIT_UINT :
                (util_format_is_pure_sint(image->format)) ? PAN_BLIT_INT :
                PAN_BLIT_FLOAT;
        bool ms = image->nr_samples > 1;
        const struct pan_blit_shader *shader =
                &pool->dev->blit_shaders.loads[loc][T][ms];

        state->shader.shader = shader->shader;
        assert(state->shader.shader);
        state->shader.varying_count = 1;
        state->shader.texture_count = 1;
        state->shader.sampler_count = 1;

        state->properties.stencil_from_shader = (loc == FRAG_RESULT_STENCIL);
        state->properties.depth_source = (loc == FRAG_RESULT_DEPTH) ?
                                         MALI_DEPTH_SOURCE_SHADER :
                                         MALI_DEPTH_SOURCE_FIXED_FUNCTION;

        state->multisample_misc.sample_mask = 0xFFFF;
        state->multisample_misc.multisample_enable = ms;
        state->multisample_misc.evaluate_per_sample = ms;
        state->multisample_misc.depth_write_mask = (loc == FRAG_RESULT_DEPTH);
        state->multisample_misc.depth_function = MALI_FUNC_ALWAYS;

        state->stencil_mask_misc.stencil_enable = (loc == FRAG_RESULT_STENCIL);
        state->stencil_mask_misc.stencil_mask_front = 0xFF;
        state->stencil_mask_misc.stencil_mask_back = 0xFF;

        state->stencil_front.compare_function = MALI_FUNC_ALWAYS;
        state->stencil_front.stencil_fail = MALI_STENCIL_OP_REPLACE;
        state->stencil_front.depth_fail = MALI_STENCIL_OP_REPLACE;
        state->stencil_front.depth_pass = MALI_STENCIL_OP_REPLACE;
        state->stencil_front.mask = 0xFF;
        state->stencil_back = state->stencil_front;
}

static void
panfrost_load_emit_varying(struct pan_pool *pool, struct MALI_DRAW *draw,
                          mali_ptr coordinates, unsigned vertex_count)
{
        /* Bifrost needs an empty desc to mark end of prefetching */
        bool padding_buffer = pan_is_bifrost(pool->dev);

        struct panfrost_ptr varying =
                panfrost_pool_alloc_desc(pool, ATTRIBUTE);
        struct panfrost_ptr varying_buffer =
                panfrost_pool_alloc_desc_array(pool, (padding_buffer ? 2 : 1),
                                               ATTRIBUTE_BUFFER);

        pan_pack(varying_buffer.cpu, ATTRIBUTE_BUFFER, cfg) {
                cfg.pointer = coordinates;
                cfg.stride = 4 * sizeof(float);
                cfg.size = cfg.stride * vertex_count;
        }

        if (padding_buffer) {
                pan_pack(varying_buffer.cpu + MALI_ATTRIBUTE_BUFFER_LENGTH,
                         ATTRIBUTE_BUFFER, cfg);
        }

        pan_pack(varying.cpu, ATTRIBUTE, cfg) {
                cfg.buffer_index = 0;
                cfg.offset_enable = !pan_is_bifrost(pool->dev);
                cfg.format = pool->dev->formats[PIPE_FORMAT_R32G32_FLOAT].hw;
        }

        draw->varyings = varying.gpu;
        draw->varying_buffers = varying_buffer.gpu;
        draw->position = coordinates;
}

static void
midgard_load_emit_texture(struct pan_pool *pool, struct MALI_DRAW *draw,
                          struct pan_image *image)
{
        struct panfrost_ptr texture =
                 panfrost_pool_alloc_desc_aggregate(pool,
                                                    PAN_DESC(MIDGARD_TEXTURE),
                                                    PAN_DESC_ARRAY(MAX2(image->nr_samples, 1),
                                                                   SURFACE_WITH_STRIDE));

        struct panfrost_ptr payload = {
                texture.cpu + MALI_MIDGARD_TEXTURE_LENGTH,
                texture.gpu + MALI_MIDGARD_TEXTURE_LENGTH,
        };

        struct panfrost_ptr sampler =
                 panfrost_pool_alloc_desc(pool, MIDGARD_SAMPLER);

        /* Create the texture descriptor. We partially compute the base address
         * ourselves to account for layer, such that the texture descriptor
         * itself is for a 2D texture with array size 1 even for 3D/array
         * textures, removing the need to separately key the blit shaders for
         * 2D and 3D variants */

        unsigned offset =
                image->first_layer *
                panfrost_get_layer_stride(image->layout, image->first_level);

        unsigned char swizzle[4] = {
                PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W
        };

        panfrost_new_texture(pool->dev, image->layout, texture.cpu,
                             image->width0, image->height0,
                             MAX2(image->nr_samples, 1), 1,
                             image->format, MALI_TEXTURE_DIMENSION_2D,
                             image->first_level, image->last_level,
                             0, 0,
                             image->nr_samples,
                             swizzle,
                             image->bo->ptr.gpu + offset, &payload);

        pan_pack(sampler.cpu, MIDGARD_SAMPLER, cfg)
                cfg.normalized_coordinates = false;

        draw->textures = panfrost_pool_upload(pool, &texture.gpu, sizeof(texture.gpu));
        draw->samplers = sampler.gpu;
}

static void
midgard_load_emit_blend_rt(struct pan_pool *pool, void *out,
                           mali_ptr blend_shader, struct pan_image *image,
                           unsigned rt, unsigned loc)
{
        bool disabled = loc != (FRAG_RESULT_DATA0 + rt);
        bool srgb = util_format_is_srgb(image->format);

        pan_pack(out, BLEND, cfg) {
                if (disabled) {
                        cfg.midgard.equation.color_mask = 0xf;
                        cfg.midgard.equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
                        cfg.midgard.equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
                        cfg.midgard.equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
                        cfg.midgard.equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
                        cfg.midgard.equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
                        cfg.midgard.equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
                        continue;
                }

                cfg.round_to_fb_precision = true;
                cfg.srgb = srgb;

                if (!blend_shader) {
                        cfg.midgard.equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
                        cfg.midgard.equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
                        cfg.midgard.equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
                        cfg.midgard.equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
                        cfg.midgard.equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
                        cfg.midgard.equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
                        cfg.midgard.equation.color_mask = 0xf;
                } else {
                        cfg.midgard.blend_shader = true;
                        cfg.midgard.shader_pc = blend_shader;
                }
        }
}

static void
midgard_load_emit_rsd(struct pan_pool *pool, struct MALI_DRAW *draw,
                      mali_ptr blend_shader, struct pan_image *image,
                      unsigned loc)
{
        struct panfrost_ptr t =
                panfrost_pool_alloc_desc_aggregate(pool,
                                                   PAN_DESC(RENDERER_STATE),
                                                   PAN_DESC_ARRAY(8, BLEND));
        bool srgb = util_format_is_srgb(image->format);

        pan_pack(t.cpu, RENDERER_STATE, cfg) {
                panfrost_load_prepare_rsd(pool, &cfg, image, loc);
                cfg.properties.midgard.work_register_count = 4;
                cfg.properties.midgard.force_early_z = (loc >= FRAG_RESULT_DATA0);
                cfg.stencil_mask_misc.alpha_test_compare_function = MALI_FUNC_ALWAYS;
                if (!(pool->dev->quirks & MIDGARD_SFBD)) {
                        cfg.sfbd_blend_shader = blend_shader;
                        continue;
                }

                cfg.stencil_mask_misc.sfbd_write_enable = true;
                cfg.stencil_mask_misc.sfbd_dither_disable = true;
                cfg.stencil_mask_misc.sfbd_srgb = srgb;
                cfg.multisample_misc.sfbd_blend_shader = !!blend_shader;
                if (cfg.multisample_misc.sfbd_blend_shader) {
                        cfg.sfbd_blend_shader = blend_shader;
                        continue;
                }

                cfg.sfbd_blend_equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
                cfg.sfbd_blend_equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
                cfg.sfbd_blend_equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
                cfg.sfbd_blend_equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
                cfg.sfbd_blend_equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
                cfg.sfbd_blend_equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
                cfg.sfbd_blend_constant = 0;

                if (loc >= FRAG_RESULT_DATA0)
                        cfg.sfbd_blend_equation.color_mask = 0xf;
        }

        for (unsigned i = 0; i < 8; ++i) {
                void *dest = t.cpu + MALI_RENDERER_STATE_LENGTH + MALI_BLEND_LENGTH * i;

                midgard_load_emit_blend_rt(pool, dest, blend_shader, image, i, loc);
        }

        draw->state = t.gpu;
}

/* Add a shader-based load on Midgard (draw-time for GL). Shaders are
 * precached */

void
panfrost_load_midg(struct pan_pool *pool,
                   struct pan_scoreboard *scoreboard,
                   mali_ptr blend_shader,
                   mali_ptr fbd,
                   mali_ptr coordinates, unsigned vertex_count,
                   struct pan_image *image,
                   unsigned loc)
{
        struct panfrost_ptr t =
                panfrost_pool_alloc_desc(pool, MIDGARD_TILER_JOB);

        pan_section_pack(t.cpu, MIDGARD_TILER_JOB, DRAW, cfg) {
                cfg.texture_descriptor_is_64b = true;
                cfg.draw_descriptor_is_64b = true;
                cfg.four_components_per_vertex = true;

                panfrost_load_emit_varying(pool, &cfg, coordinates, vertex_count);
                midgard_load_emit_texture(pool, &cfg, image);
                panfrost_load_emit_viewport(pool, &cfg, image);
                cfg.fbd = fbd;
                midgard_load_emit_rsd(pool, &cfg, blend_shader, image, loc);
        }

        pan_section_pack(t.cpu, MIDGARD_TILER_JOB, PRIMITIVE, cfg) {
                cfg.draw_mode = MALI_DRAW_MODE_TRIANGLES;
                cfg.index_count = vertex_count;
                cfg.job_task_split = 6;
        }

        pan_section_pack(t.cpu, MIDGARD_TILER_JOB, PRIMITIVE_SIZE, cfg) {
                cfg.constant = 1.0f;
        }

        panfrost_pack_work_groups_compute(pan_section_ptr(t.cpu,
                                                          MIDGARD_TILER_JOB,
                                                          INVOCATION),
                                          1, vertex_count, 1, 1, 1, 1, true);

        panfrost_add_job(pool, scoreboard, MALI_JOB_TYPE_TILER, false, false,
                         0, 0, &t, true);
}

static void
bifrost_load_emit_texture(struct pan_pool *pool, struct MALI_DRAW *draw,
                          struct pan_image *image)
{
        struct panfrost_ptr texture =
                 panfrost_pool_alloc_desc_aggregate(pool,
                                                    PAN_DESC(BIFROST_TEXTURE),
                                                    PAN_DESC_ARRAY(MAX2(image->nr_samples, 1),
                                                                   SURFACE_WITH_STRIDE));
        struct panfrost_ptr sampler =
                 panfrost_pool_alloc_desc(pool, BIFROST_SAMPLER);
        struct panfrost_ptr payload = {
                 .cpu = texture.cpu + MALI_BIFROST_TEXTURE_LENGTH,
                 .gpu = texture.gpu + MALI_BIFROST_TEXTURE_LENGTH,
        };

        unsigned offset =
                image->first_layer *
                panfrost_get_layer_stride(image->layout, image->first_level);

        unsigned char swizzle[4] = {
                PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W
        };

        panfrost_new_texture(pool->dev, image->layout, texture.cpu,
                             image->width0, image->height0,
                             MAX2(image->nr_samples, 1), 1,
                             image->format, MALI_TEXTURE_DIMENSION_2D,
                             image->first_level, image->last_level,
                             0, 0,
                             image->nr_samples,
                             swizzle,
                             image->bo->ptr.gpu + offset, &payload);

        pan_pack(sampler.cpu, BIFROST_SAMPLER, cfg) {
                cfg.seamless_cube_map = false;
                cfg.normalized_coordinates = false;
                cfg.point_sample_minify = true;
                cfg.point_sample_magnify = true;
        }

        draw->textures = texture.gpu;
        draw->samplers = sampler.gpu;
}

static enum mali_bifrost_register_file_format
blit_type_to_reg_fmt(enum pan_blit_type btype)
{
        switch (btype) {
        case PAN_BLIT_FLOAT:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_F32;
        case PAN_BLIT_INT:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_I32;
        case PAN_BLIT_UINT:
                return MALI_BIFROST_REGISTER_FILE_FORMAT_U32;
        default:
                unreachable("Invalid blit type");
        }
}

static void
bifrost_load_emit_blend_rt(struct pan_pool *pool, void *out,
                           mali_ptr blend_shader, struct pan_image *image,
                           unsigned rt, unsigned loc)
{
        enum pan_blit_type T =
                (loc == FRAG_RESULT_STENCIL) ? PAN_BLIT_UINT :
                (util_format_is_pure_uint(image->format)) ? PAN_BLIT_UINT :
                (util_format_is_pure_sint(image->format)) ? PAN_BLIT_INT :
                PAN_BLIT_FLOAT;
        bool disabled = loc != (FRAG_RESULT_DATA0 + rt);
        bool srgb = util_format_is_srgb(image->format);

        pan_pack(out, BLEND, cfg) {
                if (disabled) {
                        cfg.enable = false;
                        cfg.bifrost.internal.mode = MALI_BIFROST_BLEND_MODE_OFF;
                        continue;
                }

                cfg.round_to_fb_precision = true;
                cfg.srgb = srgb;
                cfg.bifrost.internal.mode = blend_shader ?
                                            MALI_BIFROST_BLEND_MODE_SHADER :
                                            MALI_BIFROST_BLEND_MODE_OPAQUE;
                if (blend_shader) {
                        cfg.bifrost.internal.shader.pc = blend_shader;
                } else {
                        const struct util_format_description *format_desc =
                                util_format_description(image->format);

                        cfg.bifrost.equation.rgb.a = MALI_BLEND_OPERAND_A_SRC;
                        cfg.bifrost.equation.rgb.b = MALI_BLEND_OPERAND_B_SRC;
                        cfg.bifrost.equation.rgb.c = MALI_BLEND_OPERAND_C_ZERO;
                        cfg.bifrost.equation.alpha.a = MALI_BLEND_OPERAND_A_SRC;
                        cfg.bifrost.equation.alpha.b = MALI_BLEND_OPERAND_B_SRC;
                        cfg.bifrost.equation.alpha.c = MALI_BLEND_OPERAND_C_ZERO;
                        cfg.bifrost.equation.color_mask = 0xf;
                        cfg.bifrost.internal.fixed_function.num_comps = 4;
                        cfg.bifrost.internal.fixed_function.conversion.memory_format =
                                panfrost_format_to_bifrost_blend(pool->dev, format_desc, true);
                        cfg.bifrost.internal.fixed_function.conversion.register_format =
                                blit_type_to_reg_fmt(T);

                        cfg.bifrost.internal.fixed_function.rt = rt;
                }
        }
}

static void
bifrost_load_emit_rsd(struct pan_pool *pool, struct MALI_DRAW *draw,
                      mali_ptr blend_shader, struct pan_image *image,
                      unsigned loc)
{
        struct panfrost_ptr t =
                panfrost_pool_alloc_desc_aggregate(pool,
                                                   PAN_DESC(RENDERER_STATE),
                                                   PAN_DESC_ARRAY(8, BLEND));

        pan_pack(t.cpu, RENDERER_STATE, cfg) {
                panfrost_load_prepare_rsd(pool, &cfg, image, loc);
                if (loc >= FRAG_RESULT_DATA0) {
                        cfg.properties.bifrost.zs_update_operation =
                                MALI_PIXEL_KILL_STRONG_EARLY;
                        cfg.properties.bifrost.pixel_kill_operation =
                                MALI_PIXEL_KILL_FORCE_EARLY;
                } else {
                        cfg.properties.bifrost.zs_update_operation =
                                MALI_PIXEL_KILL_FORCE_LATE;
                        cfg.properties.bifrost.pixel_kill_operation =
                                MALI_PIXEL_KILL_FORCE_LATE;
                }
                cfg.properties.bifrost.allow_forward_pixel_to_kill = true;
                cfg.preload.fragment.coverage = true;
                cfg.preload.fragment.sample_mask_id = image->nr_samples > 1;
        }

        for (unsigned i = 0; i < 8; ++i) {
                void *dest = t.cpu + MALI_RENDERER_STATE_LENGTH + MALI_BLEND_LENGTH * i;

                bifrost_load_emit_blend_rt(pool, dest, blend_shader, image, i, loc);
        }

        draw->state = t.gpu;
}

void
panfrost_load_bifrost(struct pan_pool *pool,
                      struct pan_scoreboard *scoreboard,
                      mali_ptr blend_shader,
                      mali_ptr thread_storage,
                      mali_ptr tiler,
                      mali_ptr coordinates, unsigned vertex_count,
                      struct pan_image *image,
                      unsigned loc)
{
        struct panfrost_ptr t =
                panfrost_pool_alloc_desc(pool, BIFROST_TILER_JOB);

        pan_section_pack(t.cpu, BIFROST_TILER_JOB, DRAW, cfg) {
                cfg.four_components_per_vertex = true;
                cfg.draw_descriptor_is_64b = true;

                panfrost_load_emit_varying(pool, &cfg, coordinates, vertex_count);
                bifrost_load_emit_texture(pool, &cfg, image);
                panfrost_load_emit_viewport(pool, &cfg, image);
                cfg.thread_storage = thread_storage;
                bifrost_load_emit_rsd(pool, &cfg, blend_shader, image, loc);
        }

        pan_section_pack(t.cpu, BIFROST_TILER_JOB, PRIMITIVE, cfg) {
                cfg.draw_mode = MALI_DRAW_MODE_TRIANGLES;
                cfg.index_count = vertex_count;
                cfg.job_task_split = 6;
        }

        pan_section_pack(t.cpu, BIFROST_TILER_JOB, PRIMITIVE_SIZE, cfg) {
                cfg.constant = 1.0f;
        }

        panfrost_pack_work_groups_compute(pan_section_ptr(t.cpu,
                                                          MIDGARD_TILER_JOB,
                                                          INVOCATION),
                                          1, vertex_count, 1, 1, 1, 1, true);

        pan_section_pack(t.cpu, BIFROST_TILER_JOB, PADDING, cfg) { }
        pan_section_pack(t.cpu, BIFROST_TILER_JOB, TILER, cfg) {
                cfg.address = tiler;
        }

        panfrost_add_job(pool, scoreboard, MALI_JOB_TYPE_TILER, false, false,
                         0, 0, &t, true);
}
