/*
 * © Copyright 2018 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
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
 */

#include <stdio.h>
#include "pan_blend_shaders.h"
#include "pan_util.h"
#include "panfrost-quirks.h"
#include "midgard/midgard_compile.h"
#include "bifrost/bifrost_compile.h"
#include "compiler/nir/nir_builder.h"
#include "nir/nir_lower_blend.h"
#include "panfrost/util/pan_lower_framebuffer.h"
#include "gallium/auxiliary/util/u_blend.h"
#include "util/u_memory.h"

/*
 * Implements the command stream portion of programmatic blend shaders.
 *
 * On Midgard, common blending operations are accelerated by the fixed-function
 * blending pipeline. Panfrost supports this fast path via the code in
 * pan_blending.c. Nevertheless, uncommon blend modes (including some seemingly
 * simple modes present in ES2) require "blend shaders", a special internal
 * shader type used for programmable blending.
 *
 * Blend shaders operate during the normal blending time, but they bypass the
 * fixed-function blending pipeline and instead go straight to the Midgard
 * shader cores. The shaders themselves are essentially just fragment shaders,
 * making heavy use of uint8 arithmetic to manipulate RGB values for the
 * framebuffer.
 *
 * As is typical with Midgard, shader binaries must be accompanied by
 * information about the first tag (ORed with the bottom nibble of address,
 * like usual) and work registers. Work register count is assumed to be less
 * than or equal to the coresponding fragment shader's work count. This
 * suggests that blend shader invocation is tied to fragment shader
 * execution.
 *
 * The shaders themselves use the standard ISA. The source pixel colour,
 * including alpha, is preloaded into r0 as a vec4 of float32. The destination
 * pixel colour must be loaded explicitly via load/store ops, possibly
 * performing conversions in software. The blended colour must be stored with a
 * fragment writeout in the correct framebuffer format, either in software or
 * via conversion opcodes on the load/store pipe.
 *
 * Blend shaders hardcode constants. Naively, this requires recompilation each
 * time the blend color changes, which is a performance risk. Accordingly, we
 * 'cheat' a bit: instead of loading the constant, we compile a shader with a
 * dummy constant, exporting the offset to the immediate in the shader binary,
 * storing this generic binary and metadata in the CSO itself at CSO create
 * time.
 *
 * We then hot patch in the color into this shader at attachment / color change
 * time, allowing for CSO create to be the only expensive operation
 * (compilation).
 */

static nir_lower_blend_options
nir_make_options(const struct pipe_blend_state *blend, unsigned i)
{
        nir_lower_blend_options options = { 0 };

        if (blend->logicop_enable) {
            options.logicop_enable = true;
            options.logicop_func = blend->logicop_func;
            return options;
        }

        options.logicop_enable = false;

        if (!blend->independent_blend_enable)
                i = 0;

        /* If blend is disabled, we just use replace mode */

        nir_lower_blend_channel rgb = {
                .func = BLEND_FUNC_ADD,
                .src_factor = BLEND_FACTOR_ZERO,
                .invert_src_factor = true,
                .dst_factor = BLEND_FACTOR_ZERO,
                .invert_dst_factor = false
        };

        nir_lower_blend_channel alpha = rgb;

        if (blend->rt[i].blend_enable) {
                rgb.func = util_blend_func_to_shader(blend->rt[i].rgb_func);
                rgb.src_factor = util_blend_factor_to_shader(blend->rt[i].rgb_src_factor);
                rgb.dst_factor = util_blend_factor_to_shader(blend->rt[i].rgb_dst_factor);
                rgb.invert_src_factor = util_blend_factor_is_inverted(blend->rt[i].rgb_src_factor);
                rgb.invert_dst_factor = util_blend_factor_is_inverted(blend->rt[i].rgb_dst_factor);

                alpha.func = util_blend_func_to_shader(blend->rt[i].alpha_func);
                alpha.src_factor = util_blend_factor_to_shader(blend->rt[i].alpha_src_factor);
                alpha.dst_factor = util_blend_factor_to_shader(blend->rt[i].alpha_dst_factor);
                alpha.invert_src_factor = util_blend_factor_is_inverted(blend->rt[i].alpha_src_factor);
                alpha.invert_dst_factor = util_blend_factor_is_inverted(blend->rt[i].alpha_dst_factor);
        }

        options.rgb = rgb;
        options.alpha = alpha;

        options.colormask = blend->rt[i].colormask;

        return options;
}

static nir_ssa_def *
nir_iclamp(nir_builder *b, nir_ssa_def *v, int32_t lo, int32_t hi)
{
        return nir_imin(b, nir_imax(b, v, nir_imm_int(b, lo)), nir_imm_int(b, hi));
}

struct panfrost_blend_shader *
panfrost_create_blend_shader(struct panfrost_context *ctx,
                             struct panfrost_blend_state *state,
                             const struct panfrost_blend_shader_key *key)
{
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_blend_shader *res = rzalloc(ctx, struct panfrost_blend_shader);

        res->ctx = ctx;
        res->key = *key;

        /* Build the shader */

        nir_shader *shader = nir_shader_create(ctx, MESA_SHADER_FRAGMENT, &midgard_nir_options, NULL);
        nir_function *fn = nir_function_create(shader, "main");
        fn->is_entrypoint = true;
        nir_function_impl *impl = nir_function_impl_create(fn);

        const struct util_format_description *format_desc =
                util_format_description(key->format);

        nir_alu_type T = pan_unpacked_type_for_format(format_desc);
        enum glsl_base_type g =
                (T == nir_type_float16) ? GLSL_TYPE_FLOAT16 :
                (T == nir_type_float32) ? GLSL_TYPE_FLOAT :
                (T == nir_type_int8) ? GLSL_TYPE_INT8 :
                (T == nir_type_int16) ? GLSL_TYPE_INT16 :
                (T == nir_type_int32) ? GLSL_TYPE_INT :
                (T == nir_type_uint8) ? GLSL_TYPE_UINT8 :
                (T == nir_type_uint16) ? GLSL_TYPE_UINT16 :
                (T == nir_type_uint32) ? GLSL_TYPE_UINT :
                GLSL_TYPE_FLOAT;

        /* Create the blend variables */

        nir_variable *c_src = nir_variable_create(shader, nir_var_shader_in, glsl_vector_type(GLSL_TYPE_FLOAT, 4), "gl_Color");
        nir_variable *c_src1 = nir_variable_create(shader, nir_var_shader_in, glsl_vector_type(GLSL_TYPE_FLOAT, 4), "gl_Color1");
        nir_variable *c_out = nir_variable_create(shader, nir_var_shader_out, glsl_vector_type(g, 4), "gl_FragColor");

        c_src->data.location = VARYING_SLOT_COL0;
        c_src1->data.location = VARYING_SLOT_VAR0;
        c_out->data.location = FRAG_RESULT_COLOR;

        c_src1->data.driver_location = 1;

        /* Setup nir_builder */

        nir_builder _b;
        nir_builder *b = &_b;
        nir_builder_init(b, impl);
        b->cursor = nir_before_block(nir_start_block(impl));

        /* Setup inputs */

        nir_ssa_def *s_src[] = {nir_load_var(b, c_src), nir_load_var(b, c_src1)};

        for (int i = 0; i < ARRAY_SIZE(s_src); ++i) {
                if (T == nir_type_float16)
                        s_src[i] = nir_f2f16(b, s_src[i]);
                else if (T == nir_type_int16)
                        s_src[i] = nir_i2i16(b, nir_iclamp(b, s_src[i], -32768, 32767));
                else if (T == nir_type_uint16)
                        s_src[i] = nir_u2u16(b, nir_umin(b, s_src[i], nir_imm_int(b, 65535)));
                else if (T == nir_type_int8)
                        s_src[i] = nir_i2i8(b, nir_iclamp(b, s_src[i], -128, 127));
                else if (T == nir_type_uint8)
                        s_src[i] = nir_u2u8(b, nir_umin(b, s_src[i], nir_imm_int(b, 255)));
        }

        /* Build a trivial blend shader */
        nir_store_var(b, c_out, s_src[0], 0xFF);

        nir_lower_blend_options options = nir_make_options(&state->base, key->rt);
        options.format = key->format;
        options.is_bifrost = !!(dev->quirks & IS_BIFROST);
        options.src1 = s_src[1];

        if (T == nir_type_float16)
                options.half = true;

        NIR_PASS_V(shader, nir_lower_blend, options);

        res->nir = shader;
        return res;
}

static uint64_t
bifrost_get_blend_desc(const struct panfrost_device *dev,
                       enum pipe_format fmt, unsigned rt)
{
        const struct util_format_description *desc = util_format_description(fmt);
        uint64_t res;

        pan_pack(&res, BIFROST_INTERNAL_BLEND, cfg) {
                cfg.mode = MALI_BIFROST_BLEND_MODE_OPAQUE;
                cfg.fixed_function.num_comps = desc->nr_channels;
                cfg.fixed_function.rt = rt;

                nir_alu_type T = pan_unpacked_type_for_format(desc);
                switch (T) {
                case nir_type_float16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_F16;
                        break;
                case nir_type_float32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_F32;
                        break;
                case nir_type_int16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_I16;
                        break;
                case nir_type_int32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_I32;
                        break;
                case nir_type_uint16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_U16;
                        break;
                case nir_type_uint32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_U32;
                        break;
                default:
                        unreachable("Invalid format");
                }

                cfg.fixed_function.conversion.memory_format =
                         panfrost_format_to_bifrost_blend(dev, desc, true);
        }

        return res;
}

void
panfrost_compile_blend_shader(struct panfrost_blend_shader *shader,
                              const float *constants)
{
        struct panfrost_device *dev = pan_device(shader->ctx->base.screen);

        /* If the shader has already been compiled and the constants match
         * or the shader doesn't use the blend constants, we can keep the
         * compiled version.
         */
        if (shader->buffer &&
            (!constants ||
             !memcmp(shader->constants, constants, sizeof(shader->constants))))
                return;

        /* Compile or recompile the NIR shader */
        struct panfrost_compile_inputs inputs = {
                .gpu_id = dev->gpu_id,
                .is_blend = true,
                .blend.rt = shader->key.rt,
                .blend.nr_samples = shader->key.nr_samples,
                .rt_formats = {shader->key.format},
        };

        if (constants)
                memcpy(inputs.blend.constants, constants, sizeof(inputs.blend.constants));

        panfrost_program *program;

        if (dev->quirks & IS_BIFROST) {
                inputs.blend.bifrost_blend_desc =
                        bifrost_get_blend_desc(dev, shader->key.format, shader->key.rt);
                program = bifrost_compile_shader_nir(NULL, shader->nir, &inputs);
	} else {
                program = midgard_compile_shader_nir(NULL, shader->nir, &inputs);
        }

        /* Allow us to patch later */
        shader->first_tag = program->first_tag;
        shader->size = program->compiled.size;
        shader->buffer = reralloc_size(shader, shader->buffer, shader->size);
        memcpy(shader->buffer, program->compiled.data, shader->size);
        shader->work_count = program->work_register_count;

        ralloc_free(program);
}
