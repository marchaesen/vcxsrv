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
        struct panfrost_screen *screen = pan_screen(pctx->screen);

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
                        screen->vtbl.get_compiler_options();

                so->cbase.prog = nir_deserialize(NULL, options, &reader);
                so->cbase.ir_type = PIPE_SHADER_IR_NIR;
        }

        panfrost_shader_compile(pctx->screen, &ctx->shaders, &ctx->descs,
                        so->cbase.ir_type, so->cbase.prog, MESA_SHADER_COMPUTE,
                        v);

        /* There are no variants so we won't need the NIR again */
        ralloc_free((void *)so->cbase.prog);
        so->cbase.prog = NULL;

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
        struct panfrost_shader_variants *so =
                (struct panfrost_shader_variants *)cso;

        free(so->variants);
        free(cso);
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
        if (!resources)
                return;

        struct panfrost_context *ctx = pan_context(pctx);
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);

        for (unsigned i = first; i < first + count; ++i) {
                struct panfrost_resource *rsrc = pan_resource(resources[i]);
                panfrost_batch_write_rsrc(batch, rsrc, PIPE_SHADER_COMPUTE);

                util_range_add(&rsrc->base, &rsrc->valid_buffer_range,
                                0, rsrc->base.width0);

                /* The handle points to uint32_t, but space is allocated for 64 bits */
                memcpy(handles[i], &rsrc->image.data.bo->ptr.gpu, sizeof(mali_ptr));
        }
}

static void
panfrost_memory_barrier(struct pipe_context *pctx, unsigned flags)
{
        /* TODO: Be smart and only flush the minimum needed, maybe emitting a
         * cache flush job if that would help */
        panfrost_flush_all_batches(pan_context(pctx), "Memory barrier");
}

void
panfrost_compute_context_init(struct pipe_context *pctx)
{
        pctx->create_compute_state = panfrost_create_compute_state;
        pctx->bind_compute_state = panfrost_bind_compute_state;
        pctx->delete_compute_state = panfrost_delete_compute_state;

        pctx->set_compute_resources = panfrost_set_compute_resources;
        pctx->set_global_binding = panfrost_set_global_binding;

        pctx->memory_barrier = panfrost_memory_barrier;
}
