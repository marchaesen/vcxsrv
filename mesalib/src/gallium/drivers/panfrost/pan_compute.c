/*
 * Copyright (C) 2019 Collabora, Ltd.
 * Copyright (C) 2019 Red Hat Inc.
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

#include "pan_context.h"
#include "pan_cmdstream.h"
#include "panfrost-quirks.h"
#include "pan_bo.h"
#include "pan_shader.h"
#include "util/u_memory.h"
#include "nir_serialize.h"

/* Compute CSOs are tracked like graphics shader CSOs, but are
 * considerably simpler. We do not implement multiple
 * variants/keying. So the CSO create function just goes ahead and
 * compiles the thing. */

static void *
panfrost_create_compute_state(
        struct pipe_context *pctx,
        const struct pipe_compute_state *cso)
{
        struct panfrost_context *ctx = pan_context(pctx);
        struct panfrost_device *dev = pan_device(pctx->screen);

        struct panfrost_shader_variants *so = CALLOC_STRUCT(panfrost_shader_variants);
        so->cbase = *cso;
        so->is_compute = true;

        struct panfrost_shader_state *v = calloc(1, sizeof(*v));
        so->variants = v;

        so->variant_count = 1;
        so->active_variant = 0;

        if (cso->ir_type == PIPE_SHADER_IR_NIR_SERIALIZED) {
                struct blob_reader reader;
                const struct pipe_binary_program_header *hdr = cso->prog;

                blob_reader_init(&reader, hdr->blob, hdr->num_bytes);

                const struct nir_shader_compiler_options *options =
                        pan_shader_get_compiler_options(dev);

                so->cbase.prog = nir_deserialize(NULL, options, &reader);
                so->cbase.ir_type = PIPE_SHADER_IR_NIR;
        }

        panfrost_shader_compile(ctx, so->cbase.ir_type, so->cbase.prog,
                                MESA_SHADER_COMPUTE, v);

        return so;
}

static void
panfrost_bind_compute_state(struct pipe_context *pipe, void *cso)
{
        struct panfrost_context *ctx = pan_context(pipe);
        ctx->shader[PIPE_SHADER_COMPUTE] = cso;
}

static void
panfrost_delete_compute_state(struct pipe_context *pipe, void *cso)
{
        free(cso);
}

/* Launch grid is the compute equivalent of draw_vbo, so in this routine, we
 * construct the COMPUTE job and some of its payload.
 */

static void
panfrost_launch_grid(struct pipe_context *pipe,
                const struct pipe_grid_info *info)
{
        struct panfrost_context *ctx = pan_context(pipe);
        struct panfrost_device *dev = pan_device(pipe->screen);
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);

        /* TODO: Indirect compute dispatch */
        assert(!info->indirect);

        ctx->compute_grid = info;

        struct panfrost_ptr t =
                panfrost_pool_alloc_desc(&batch->pool, COMPUTE_JOB);

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
        panfrost_pack_work_groups_compute(invocation,
                                          info->grid[0], info->grid[1],
                                          info->grid[2],
                                          info->block[0], info->block[1],
                                          info->block[2],
                                          false);

        pan_section_pack(t.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
                cfg.job_task_split =
                        util_logbase2_ceil(info->block[0] + 1) +
                        util_logbase2_ceil(info->block[1] + 1) +
                        util_logbase2_ceil(info->block[2] + 1);
        }

        pan_section_pack(t.cpu, COMPUTE_JOB, DRAW, cfg) {
                cfg.draw_descriptor_is_64b = true;
                if (!pan_is_bifrost(dev))
                        cfg.texture_descriptor_is_64b = true;
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

        pan_section_pack(t.cpu, COMPUTE_JOB, DRAW_PADDING, cfg);

        panfrost_add_job(&batch->pool, &batch->scoreboard,
                         MALI_JOB_TYPE_COMPUTE, true, false, 0, 0, &t, true);
        panfrost_flush_all_batches(ctx);
}

static void
panfrost_set_compute_resources(struct pipe_context *pctx,
                         unsigned start, unsigned count,
                         struct pipe_surface **resources)
{
        /* TODO */
}

static void
panfrost_set_global_binding(struct pipe_context *pctx,
                      unsigned first, unsigned count,
                      struct pipe_resource **resources,
                      uint32_t **handles)
{
        /* TODO */
}

static void
panfrost_memory_barrier(struct pipe_context *pctx, unsigned flags)
{
        /* TODO */
}

void
panfrost_compute_context_init(struct pipe_context *pctx)
{
        pctx->create_compute_state = panfrost_create_compute_state;
        pctx->bind_compute_state = panfrost_bind_compute_state;
        pctx->delete_compute_state = panfrost_delete_compute_state;

        pctx->launch_grid = panfrost_launch_grid;

        pctx->set_compute_resources = panfrost_set_compute_resources;
        pctx->set_global_binding = panfrost_set_global_binding;

        pctx->memory_barrier = panfrost_memory_barrier;
}
