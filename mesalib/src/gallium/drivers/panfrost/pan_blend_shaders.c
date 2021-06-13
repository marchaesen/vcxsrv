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
#include "pan_shader.h"
#include "pan_util.h"
#include "panfrost-quirks.h"
#include "compiler/nir/nir_builder.h"
#include "panfrost/util/nir_lower_blend.h"
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

struct panfrost_blend_shader *
panfrost_create_blend_shader(struct panfrost_context *ctx,
                             struct panfrost_blend_state *state,
                             const struct panfrost_blend_shader_key *key)
{
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_blend_shader *res = rzalloc(ctx, struct panfrost_blend_shader);
        struct pan_blend_state pan_blend = state->pan;

        res->ctx = ctx;
        res->key = *key;

        /* Build the shader */
        pan_blend.rts[key->rt].format = key->format;
        pan_blend.rts[key->rt].nr_samples = key->nr_samples;
        res->nir = pan_blend_create_shader(dev, &pan_blend, key->rt);

        return res;
}

uint64_t
bifrost_get_blend_desc(const struct panfrost_device *dev,
                       enum pipe_format fmt, unsigned rt, unsigned force_size)
{
        const struct util_format_description *desc = util_format_description(fmt);
        uint64_t res;

        pan_pack(&res, BIFROST_INTERNAL_BLEND, cfg) {
                cfg.mode = MALI_BIFROST_BLEND_MODE_OPAQUE;
                cfg.fixed_function.num_comps = desc->nr_channels;
                cfg.fixed_function.rt = rt;

                nir_alu_type T = pan_unpacked_type_for_format(desc);

                if (force_size)
                        T = nir_alu_type_get_base_type(T) | force_size;

                switch (T) {
                case nir_type_float16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_F16;
                        break;
                case nir_type_float32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_F32;
                        break;
                case nir_type_int8:
                case nir_type_int16:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_I16;
                        break;
                case nir_type_int32:
                        cfg.fixed_function.conversion.register_format =
                                MALI_BIFROST_REGISTER_FILE_FORMAT_I32;
                        break;
                case nir_type_uint8:
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

        if (pan_is_bifrost(dev)) {
                inputs.blend.bifrost_blend_desc =
                        bifrost_get_blend_desc(dev, shader->key.format, shader->key.rt, 0);
        }

        struct pan_shader_info info;
        struct util_dynarray binary;

        util_dynarray_init(&binary, NULL);
        pan_shader_compile(dev, shader->nir, &inputs, &binary, &info);

        /* Allow us to patch later */
        shader->first_tag = pan_is_bifrost(dev) ? 0 : info.midgard.first_tag;
        shader->size = binary.size;
        shader->buffer = reralloc_size(shader, shader->buffer, shader->size);
        memcpy(shader->buffer, binary.data, shader->size);
        shader->work_count = info.work_reg_count;

        util_dynarray_fini(&binary);
}
