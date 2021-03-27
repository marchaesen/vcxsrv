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
#include "pan_blend_shaders.h"
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
 * fixed-function format conversion. As such, each blend shader must be
 * hardcoded to a particular framebuffer format to correctly pack/unpack it. As
 * a concrete example, to the hardware there is no difference (!) between RG16F
 * and RG16UI -- both are simply 4-byte-per-pixel chunks. Thus both formats
 * require a blend shader (even with blending is totally disabled!), required
 * to do conversion as necessary (if necessary).
 *
 * All of this state is encapsulated in the panfrost_blend_state struct
 * (our subclass of pipe_blend_state).
 */

/* Given an initialized CSO and a particular framebuffer format, grab a
 * blend shader, generating and compiling it if it doesn't exist
 * (lazy-loading in a way). This routine, when the cache hits, should
 * befast, suitable for calling every draw to avoid wacky dirty
 * tracking paths. If the cache hits, boom, done. */

struct panfrost_blend_shader *
panfrost_get_blend_shader(struct panfrost_context *ctx,
                          struct panfrost_blend_state *blend,
                          enum pipe_format fmt, unsigned nr_samples,
                          unsigned rt,
                          const float *constants)
{
        /* Prevent NULL collision issues.. */
        assert(fmt != 0);

        /* Check the cache. Key by the RT and format */
        struct hash_table *shaders = ctx->blend_shaders;
        struct panfrost_blend_shader_key key = {
                .rt = rt,
                .format = fmt,
                .nr_samples = MAX2(nr_samples, 1),
                .has_constants = constants != NULL,
                .logicop_enable = blend->base.logicop_enable,
        };

        if (blend->base.logicop_enable) {
                key.logicop_func = blend->base.logicop_func;
        } else {
                unsigned idx = blend->base.independent_blend_enable ? rt : 0;

                if (blend->base.rt[idx].blend_enable)
                        key.equation = blend->base.rt[idx];
        }

        struct hash_entry *he = _mesa_hash_table_search(shaders, &key);
        struct panfrost_blend_shader *shader = he ? he->data : NULL;

        if (!shader) {
                /* Cache miss. Build one instead, cache it, and go */
                shader = panfrost_create_blend_shader(ctx, blend, &key);
                _mesa_hash_table_insert(shaders, &shader->key, shader);
        }

        panfrost_compile_blend_shader(shader, constants);
        return shader;
}

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

        /* TODO: The following features are not yet implemented */
        assert(!blend->alpha_to_one);

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
panfrost_bind_blend_state(struct pipe_context *pipe,
                          void *cso)
{
        struct panfrost_context *ctx = pan_context(pipe);
        ctx->blend = (struct panfrost_blend_state *) cso;
}

static void
panfrost_delete_blend_state(struct pipe_context *pipe,
                            void *cso)
{
        struct panfrost_blend_state *blend = (struct panfrost_blend_state *) cso;
        ralloc_free(blend);
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
        unsigned constant_mask = pan_blend_constant_mask(&pan_blend, rti);
        struct panfrost_blend_shader *shader =
                panfrost_get_blend_shader(ctx, blend, fmt, nr_samples, rti,
                                          constant_mask ?
                                          ctx->blend_color.color : NULL);

        /* Upload the shader, sharing a BO */
        if (!(*bo)) {
                *bo = panfrost_batch_create_bo(batch, 4096,
                   PAN_BO_EXECUTE,
                   PAN_BO_ACCESS_PRIVATE |
                   PAN_BO_ACCESS_READ |
                   PAN_BO_ACCESS_FRAGMENT);
        }

        /* Size check */
        assert((*shader_offset + shader->size) < 4096);

        memcpy((*bo)->ptr.cpu + *shader_offset, shader->buffer, shader->size);

        struct panfrost_blend_final final = {
                .is_shader = true,
                .shader = {
                        .work_count = shader->work_count,
                        .first_tag = shader->first_tag,
                        .gpu = (*bo)->ptr.gpu + *shader_offset,
                },
                .load_dest = pan_blend_reads_dest(&pan_blend, rti),
        };

        *shader_offset += shader->size;

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
