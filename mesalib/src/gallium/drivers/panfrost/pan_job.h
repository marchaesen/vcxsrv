/*
 * Copyright (C) 2019 Alyssa Rosenzweig
 * Copyright (C) 2014-2017 Broadcom
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

#ifndef __PAN_JOB_H__
#define __PAN_JOB_H__

#include "util/u_dynarray.h"
#include "pipe/p_state.h"
#include "pan_cs.h"
#include "pan_mempool.h"
#include "pan_resource.h"
#include "pan_scoreboard.h"

/* A panfrost_batch corresponds to a bound FBO we're rendering to,
 * collecting over multiple draws. */

struct panfrost_batch {
        struct panfrost_context *ctx;
        struct pipe_framebuffer_state key;

        /* Sequence number used to implement LRU eviction when all batch slots are used */
        uint64_t seqnum;

        /* Buffers cleared (PIPE_CLEAR_* bitmask) */
        unsigned clear;

        /* Buffers drawn */
        unsigned draws;

        /* Buffers read */
        unsigned read;

        /* Buffers needing resolve to memory */
        unsigned resolve;

        /* Packed clear values, indexed by both render target as well as word.
         * Essentially, a single pixel is packed, with some padding to bring it
         * up to a 32-bit interval; that pixel is then duplicated over to fill
         * all 16-bytes */

        uint32_t clear_color[PIPE_MAX_COLOR_BUFS][4];
        float clear_depth;
        unsigned clear_stencil;

        /* Amount of thread local storage required per thread */
        unsigned stack_size;

        /* Amount of shared memory needed per workgroup (for compute) */
        unsigned shared_size;

        /* The bounding box covered by this job, taking scissors into account.
         * Basically, the bounding box we have to run fragment shaders for */

        unsigned minx, miny;
        unsigned maxx, maxy;

        /* Acts as a rasterizer discard */
        bool scissor_culls_everything;

        /* BOs referenced not in the pool */
        int first_bo, last_bo;
        unsigned num_bos;
        struct util_sparse_array bos;

        /* Pool owned by this batch (released when the batch is released) used for temporary descriptors */
        struct panfrost_pool pool;

        /* Pool also owned by this batch that is not CPU mapped (created as
         * INVISIBLE) used for private GPU-internal structures, particularly
         * varyings */
        struct panfrost_pool invisible_pool;

        /* Job scoreboarding state */
        struct pan_scoreboard scoreboard;

        /* Polygon list bound to the batch, or NULL if none bound yet */
        struct panfrost_bo *polygon_list;

        /* Scratchpad BO bound to the batch, or NULL if none bound yet */
        struct panfrost_bo *scratchpad;

        /* Shared memory BO bound to the batch, or NULL if none bound yet */
        struct panfrost_bo *shared_memory;

        /* Framebuffer descriptor. */
        struct panfrost_ptr framebuffer;

        /* Thread local storage descriptor. */
        struct panfrost_ptr tls;

        /* Tiler context */
        struct pan_tiler_context tiler_ctx;

        /* Indirect draw data */
        struct panfrost_ptr indirect_draw_ctx;
        unsigned indirect_draw_job_id;

        /* Keep the num_work_groups sysval around for indirect dispatch */
        mali_ptr num_wg_sysval[3];

        /* Cached descriptors */
        mali_ptr viewport;
        mali_ptr rsd[PIPE_SHADER_TYPES];
        mali_ptr textures[PIPE_SHADER_TYPES];
        mali_ptr samplers[PIPE_SHADER_TYPES];
        mali_ptr attribs[PIPE_SHADER_TYPES];
        mali_ptr attrib_bufs[PIPE_SHADER_TYPES];
        mali_ptr uniform_buffers[PIPE_SHADER_TYPES];
        mali_ptr push_uniforms[PIPE_SHADER_TYPES];

        /* Referenced resources */
        struct set *resources;
};

/* Functions for managing the above */

struct panfrost_batch *
panfrost_get_batch_for_fbo(struct panfrost_context *ctx);

struct panfrost_batch *
panfrost_get_fresh_batch_for_fbo(struct panfrost_context *ctx, const char *reason);

void
panfrost_batch_add_bo(struct panfrost_batch *batch,
                      struct panfrost_bo *bo,
                      enum pipe_shader_type stage);

void
panfrost_batch_read_rsrc(struct panfrost_batch *batch,
                         struct panfrost_resource *rsrc,
                         enum pipe_shader_type stage);

void
panfrost_batch_write_rsrc(struct panfrost_batch *batch,
                          struct panfrost_resource *rsrc,
                          enum pipe_shader_type stage);

struct panfrost_bo *
panfrost_batch_create_bo(struct panfrost_batch *batch, size_t size,
                         uint32_t create_flags, uint32_t access_flags,
                         const char *label);

void
panfrost_flush_all_batches(struct panfrost_context *ctx, const char *reason);

void
panfrost_flush_batches_accessing_rsrc(struct panfrost_context *ctx,
                                      struct panfrost_resource *rsrc,
                                      const char *reason);

void
panfrost_flush_writer(struct panfrost_context *ctx,
                      struct panfrost_resource *rsrc,
                      const char *reason);

void
panfrost_batch_adjust_stack_size(struct panfrost_batch *batch);

struct panfrost_bo *
panfrost_batch_get_scratchpad(struct panfrost_batch *batch, unsigned size, unsigned thread_tls_alloc, unsigned core_count);

struct panfrost_bo *
panfrost_batch_get_shared_memory(struct panfrost_batch *batch, unsigned size, unsigned workgroup_count);

void
panfrost_batch_clear(struct panfrost_batch *batch,
                     unsigned buffers,
                     const union pipe_color_union *color,
                     double depth, unsigned stencil);

void
panfrost_batch_union_scissor(struct panfrost_batch *batch,
                             unsigned minx, unsigned miny,
                             unsigned maxx, unsigned maxy);

#endif
