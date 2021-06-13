/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 * Authors (Collabora):
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 *
 */

#include <stdio.h>
#include "util/u_memory.h"
#include "gallium/auxiliary/util/u_blend.h"
#include "pan_context.h"
#include "pan_blend_cso.h"
#include "pan_bo.h"
#include "panfrost-quirks.h"

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
        struct panfrost_context *ctx = pan_context(pipe);
        struct panfrost_blend_state *so = rzalloc(ctx, struct panfrost_blend_state);
        so->base = *blend;

        so->pan.dither = blend->dither;
        so->pan.logicop_enable = blend->logicop_enable;
        so->pan.logicop_func = blend->logicop_func;
        so->pan.rt_count = blend->max_rt + 1;

        for (unsigned c = 0; c < so->pan.rt_count; ++c) {
                unsigned g = blend->independent_blend_enable ? c : 0;
                const struct pipe_rt_blend_state *pipe = &blend->rt[g];
                struct pan_blend_equation *equation = &so->pan.rts[c].equation;

                equation->color_mask = pipe->colormask;
                equation->blend_enable = pipe->blend_enable;
                if (!equation->blend_enable)
                        continue;

                equation->rgb_func = util_blend_func_to_shader(pipe->rgb_func);
                equation->rgb_src_factor = util_blend_factor_to_shader(pipe->rgb_src_factor);
                equation->rgb_invert_src_factor = util_blend_factor_is_inverted(pipe->rgb_src_factor);
                equation->rgb_dst_factor = util_blend_factor_to_shader(pipe->rgb_dst_factor);
                equation->rgb_invert_dst_factor = util_blend_factor_is_inverted(pipe->rgb_dst_factor);
                equation->alpha_func = util_blend_func_to_shader(pipe->alpha_func);
                equation->alpha_src_factor = util_blend_factor_to_shader(pipe->alpha_src_factor);
                equation->alpha_invert_src_factor = util_blend_factor_is_inverted(pipe->alpha_src_factor);
                equation->alpha_dst_factor = util_blend_factor_to_shader(pipe->alpha_dst_factor);
                equation->alpha_invert_dst_factor = util_blend_factor_is_inverted(pipe->alpha_dst_factor);
        }

        return so;
}

static void
panfrost_bind_blend_state(struct pipe_context *pipe, void *cso)
{
        struct panfrost_context *ctx = pan_context(pipe);
        ctx->blend = cso;
}

static void
panfrost_delete_blend_state(struct pipe_context *pipe, void *cso)
{
        ralloc_free(cso);
}

static void
panfrost_set_blend_color(struct pipe_context *pipe,
                         const struct pipe_blend_color *blend_color)
{
        struct panfrost_context *ctx = pan_context(pipe);

        if (blend_color)
                ctx->blend_color = *blend_color;
}

/* Create a final blend given the context */

struct panfrost_blend_final
panfrost_get_blend_for_context(struct panfrost_context *ctx, unsigned rti, struct panfrost_bo **bo, unsigned *shader_offset)
{
        struct panfrost_device *dev = pan_device(ctx->base.screen);
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
        struct pipe_framebuffer_state *fb = &ctx->pipe_framebuffer;
        enum pipe_format fmt = fb->cbufs[rti]->format;
        unsigned nr_samples = fb->cbufs[rti]->nr_samples ? :
                              fb->cbufs[rti]->texture->nr_samples;

        /* Grab the blend state */
        struct panfrost_blend_state *blend = ctx->blend;
        struct pan_blend_state pan_blend = blend->pan;

        pan_blend.rts[rti].format = fmt;
        pan_blend.rts[rti].nr_samples = nr_samples;
        memcpy(pan_blend.constants, ctx->blend_color.color,
               sizeof(pan_blend.constants));

        /* First, we'll try fixed function, matching equation and constant */
        if (pan_blend_can_fixed_function(dev, &pan_blend, rti)) {
                struct panfrost_blend_final final = {
                        .load_dest = pan_blend_reads_dest(&pan_blend, rti),
                        .equation.constant = pan_blend_get_constant(dev, &pan_blend, rti),
                        .opaque = pan_blend_is_opaque(&pan_blend, rti),
                        .no_colour = pan_blend.rts[rti].equation.color_mask == 0,
                };

                pan_blend_to_fixed_function_equation(dev, &pan_blend, rti,
                                                     &final.equation.equation);
                return final;
        }


        /* Otherwise, we need to grab a shader */
        /* Upload the shader, sharing a BO */
        if (!(*bo)) {
                *bo = panfrost_batch_create_bo(batch, 4096,
                   PAN_BO_EXECUTE,
                   PAN_BO_ACCESS_PRIVATE |
                   PAN_BO_ACCESS_READ |
                   PAN_BO_ACCESS_FRAGMENT);
        }

        struct panfrost_shader_state *ss = panfrost_get_shader_state(ctx, PIPE_SHADER_FRAGMENT);

        /* Default for Midgard */
        nir_alu_type col0_type = nir_type_float32;
        nir_alu_type col1_type = nir_type_float32;

        /* Bifrost has per-output types, respect them */
        if (pan_is_bifrost(dev)) {
                col0_type = ss->info.bifrost.blend[rti].type;
                col1_type = ss->info.bifrost.blend_src1_type;
        }

        pthread_mutex_lock(&dev->blend_shaders.lock);
        struct pan_blend_shader_variant *shader =
                pan_blend_get_shader_locked(dev, &pan_blend,
                                col0_type, col1_type, rti);

        /* Size check */
        assert((*shader_offset + shader->binary.size) < 4096);

        memcpy((*bo)->ptr.cpu + *shader_offset, shader->binary.data, shader->binary.size);

        struct panfrost_blend_final final = {
                .is_shader = true,
                .shader = {
                        .first_tag = shader->first_tag,
                        .gpu = (*bo)->ptr.gpu + *shader_offset,
                },
                .load_dest = pan_blend_reads_dest(&pan_blend, rti),
        };

        *shader_offset += shader->binary.size;
        pthread_mutex_unlock(&dev->blend_shaders.lock);

        return final;
}

void
panfrost_blend_context_init(struct pipe_context *pipe)
{
        pipe->create_blend_state = panfrost_create_blend_state;
        pipe->bind_blend_state   = panfrost_bind_blend_state;
        pipe->delete_blend_state = panfrost_delete_blend_state;

        pipe->set_blend_color = panfrost_set_blend_color;
}
