/*
 * © Copyright 2018 Alyssa Rosenzweig
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

#ifndef __BUILDER_H__
#define __BUILDER_H__

#define _LARGEFILE64_SOURCE 1
#include <sys/mman.h>
#include <assert.h>
#include "pan_resource.h"
#include "pan_job.h"
#include "pan_blend_cso.h"
#include "pan_encoder.h"
#include "pan_texture.h"

#include "pipe/p_compiler.h"
#include "pipe/p_config.h"
#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_format.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/u_blitter.h"
#include "util/hash_table.h"
#include "util/simple_mtx.h"

#include "midgard/midgard_compile.h"
#include "compiler/shader_enums.h"

/* Forward declare to avoid extra header dep */
struct prim_convert_context;

#define SET_BIT(lval, bit, cond) \
	if (cond) \
		lval |= (bit); \
	else \
		lval &= ~(bit);

/* Dirty tracking flags. 3D is for general 3D state. Shader flags are
 * per-stage. Renderer refers to Renderer State Descriptors. Vertex refers to
 * vertex attributes/elements. */

enum pan_dirty_3d {
        PAN_DIRTY_VIEWPORT       = BITFIELD_BIT(0),
        PAN_DIRTY_SCISSOR        = BITFIELD_BIT(1),
        PAN_DIRTY_VERTEX         = BITFIELD_BIT(2),
        PAN_DIRTY_PARAMS         = BITFIELD_BIT(3),
        PAN_DIRTY_DRAWID         = BITFIELD_BIT(4),
        PAN_DIRTY_TLS_SIZE       = BITFIELD_BIT(5),
};

enum pan_dirty_shader {
        PAN_DIRTY_STAGE_RENDERER = BITFIELD_BIT(0),
        PAN_DIRTY_STAGE_TEXTURE  = BITFIELD_BIT(1),
        PAN_DIRTY_STAGE_SAMPLER  = BITFIELD_BIT(2),
        PAN_DIRTY_STAGE_IMAGE    = BITFIELD_BIT(3),
        PAN_DIRTY_STAGE_CONST    = BITFIELD_BIT(4),
        PAN_DIRTY_STAGE_SSBO     = BITFIELD_BIT(5),
};

struct panfrost_constant_buffer {
        struct pipe_constant_buffer cb[PIPE_MAX_CONSTANT_BUFFERS];
        uint32_t enabled_mask;
};

struct panfrost_query {
        /* Passthrough from Gallium */
        unsigned type;
        unsigned index;

        /* For computed queries. 64-bit to prevent overflow */
        struct {
                uint64_t start;
                uint64_t end;
        };

        /* Memory for the GPU to writeback the value of the query */
        struct pipe_resource *rsrc;

        /* Whether an occlusion query is for a MSAA framebuffer */
        bool msaa;
};

struct pipe_fence_handle {
        struct pipe_reference reference;
        uint32_t syncobj;
        bool signaled;
};

struct panfrost_streamout_target {
        struct pipe_stream_output_target base;
        uint32_t offset;
};

struct panfrost_streamout {
        struct pipe_stream_output_target *targets[PIPE_MAX_SO_BUFFERS];
        unsigned num_targets;
};

struct panfrost_context {
        /* Gallium context */
        struct pipe_context base;

        /* Dirty global state */
        enum pan_dirty_3d dirty;

        /* Per shader stage dirty state */
        enum pan_dirty_shader dirty_shader[PIPE_SHADER_TYPES];

        /* Unowned pools, so manage yourself. */
        struct panfrost_pool descs, shaders;

        /* Sync obj used to keep track of in-flight jobs. */
        uint32_t syncobj;

        /* Set of 32 batches. When the set is full, the LRU entry (the batch
         * with the smallest seqnum) is flushed to free a slot.
         */
        struct {
                uint64_t seqnum;
                struct panfrost_batch slots[PAN_MAX_BATCHES];

                /** Set of active batches for faster traversal */
                BITSET_DECLARE(active, PAN_MAX_BATCHES);
        } batches;

        /* Map from resources to panfrost_batches */
        struct hash_table *writers;

        /* Bound job batch */
        struct panfrost_batch *batch;

        /* Within a launch_grid call.. */
        const struct pipe_grid_info *compute_grid;

        struct pipe_framebuffer_state pipe_framebuffer;
        struct panfrost_streamout streamout;

        bool active_queries;
        uint64_t prims_generated;
        uint64_t tf_prims_generated;
        struct panfrost_query *occlusion_query;

        bool indirect_draw;
        unsigned drawid;
        unsigned vertex_count;
        unsigned instance_count;
        unsigned offset_start;
        unsigned base_vertex;
        unsigned base_instance;
        mali_ptr first_vertex_sysval_ptr;
        mali_ptr base_vertex_sysval_ptr;
        mali_ptr base_instance_sysval_ptr;
        enum pipe_prim_type active_prim;

        /* If instancing is enabled, vertex count padded for instance; if
         * it is disabled, just equal to plain vertex count */
        unsigned padded_count;

        struct panfrost_constant_buffer constant_buffer[PIPE_SHADER_TYPES];
        struct panfrost_rasterizer *rasterizer;
        struct panfrost_shader_variants *shader[PIPE_SHADER_TYPES];
        struct panfrost_vertex_state *vertex;

        struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];
        uint32_t vb_mask;

        struct pipe_shader_buffer ssbo[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_BUFFERS];
        uint32_t ssbo_mask[PIPE_SHADER_TYPES];

        struct pipe_image_view images[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_IMAGES];
        uint32_t image_mask[PIPE_SHADER_TYPES];

        struct panfrost_sampler_state *samplers[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
        unsigned sampler_count[PIPE_SHADER_TYPES];

        struct panfrost_sampler_view *sampler_views[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_SAMPLER_VIEWS];
        unsigned sampler_view_count[PIPE_SHADER_TYPES];

        struct blitter_context *blitter;

        struct panfrost_blend_state *blend;

        struct pipe_viewport_state pipe_viewport;
        struct pipe_scissor_state scissor;
        struct pipe_blend_color blend_color;
        struct panfrost_zsa_state *depth_stencil;
        struct pipe_stencil_ref stencil_ref;
        uint16_t sample_mask;
        unsigned min_samples;

        struct panfrost_query *cond_query;
        bool cond_cond;
        enum pipe_render_cond_flag cond_mode;

        bool is_noop;

        /* Mask of active render targets */
        uint8_t fb_rt_mask;
};

/* Corresponds to the CSO */

struct panfrost_rasterizer;

/* Linked varyings */
struct pan_linkage {
        /* If the upload is owned by the CSO instead
         * of the pool, the referenced BO. Else,
         * NULL. */
        struct panfrost_bo *bo;

        /* Uploaded attribute descriptors */
        mali_ptr producer, consumer;

        /* Varyings buffers required */
        uint32_t present;

        /* Per-vertex stride for general varying buffer */
        uint32_t stride;
};

#define RSD_WORDS 16

/* Variants bundle together to form the backing CSO, bundling multiple
 * shaders with varying emulated features baked in */

/* A shader state corresponds to the actual, current variant of the shader */
struct panfrost_shader_state {
        /* Compiled, mapped descriptor, ready for the hardware */
        bool compiled;

        /* Respectively, shader binary and Renderer State Descriptor */
        struct panfrost_pool_ref bin, state;

        /* For fragment shaders, a prepared (but not uploaded RSD) */
        uint32_t partial_rsd[RSD_WORDS];

        struct pan_shader_info info;

        /* Linked varyings, for non-separable programs */
        struct pan_linkage linkage;

        struct pipe_stream_output_info stream_output;
        uint64_t so_mask;

        /* Variants */
        enum pipe_format rt_formats[8];
        unsigned nr_cbufs;

        /* Mask of state that dirties the sysvals */
        unsigned dirty_3d, dirty_shader;
};

/* A collection of varyings (the CSO) */
struct panfrost_shader_variants {
        /* A panfrost_shader_variants can represent a shader for
         * either graphics or compute */

        bool is_compute;

        union {
                struct pipe_shader_state base;
                struct pipe_compute_state cbase;
        };

        /** Lock for the variants array */
        simple_mtx_t lock;

        struct panfrost_shader_state *variants;
        unsigned variant_space;

        unsigned variant_count;

        /* The current active variant */
        unsigned active_variant;
};

struct pan_vertex_buffer {
        unsigned vbi;
        unsigned divisor;
};

struct panfrost_vertex_state {
        unsigned num_elements;

        /* buffers corresponds to attribute buffer, element_buffers corresponds
         * to an index in buffers for each vertex element */
        struct pan_vertex_buffer buffers[PIPE_MAX_ATTRIBS];
        unsigned element_buffer[PIPE_MAX_ATTRIBS];
        unsigned nr_bufs;

        struct pipe_vertex_element pipe[PIPE_MAX_ATTRIBS];
        unsigned formats[PIPE_MAX_ATTRIBS];
};

struct panfrost_zsa_state;
struct panfrost_sampler_state;
struct panfrost_sampler_view;

static inline struct panfrost_context *
pan_context(struct pipe_context *pcontext)
{
        return (struct panfrost_context *) pcontext;
}

static inline struct panfrost_streamout_target *
pan_so_target(struct pipe_stream_output_target *target)
{
        return (struct panfrost_streamout_target *)target;
}

static inline struct panfrost_shader_state *
panfrost_get_shader_state(struct panfrost_context *ctx,
                          enum pipe_shader_type st)
{
        struct panfrost_shader_variants *all = ctx->shader[st];

        if (!all)
                return NULL;

        return &all->variants[all->active_variant];
}

struct pipe_context *
panfrost_create_context(struct pipe_screen *screen, void *priv, unsigned flags);

bool
panfrost_writes_point_size(struct panfrost_context *ctx);

struct panfrost_ptr
panfrost_vertex_tiler_job(struct panfrost_context *ctx, bool is_tiler);

void
panfrost_flush(
        struct pipe_context *pipe,
        struct pipe_fence_handle **fence,
        unsigned flags);

bool
panfrost_render_condition_check(struct panfrost_context *ctx);

void
panfrost_shader_compile(struct pipe_screen *pscreen,
                        struct panfrost_pool *shader_pool,
                        struct panfrost_pool *desc_pool,
                        enum pipe_shader_ir ir_type,
                        const void *ir,
                        gl_shader_stage stage,
                        struct panfrost_shader_state *state);

void
panfrost_analyze_sysvals(struct panfrost_shader_state *ss);

mali_ptr
panfrost_get_index_buffer_bounded(struct panfrost_batch *batch,
                                  const struct pipe_draw_info *info,
                                  const struct pipe_draw_start_count_bias *draw,
                                  unsigned *min_index, unsigned *max_index);

/* Instancing */

mali_ptr
panfrost_vertex_buffer_address(struct panfrost_context *ctx, unsigned i);

/* Compute */

void
panfrost_compute_context_init(struct pipe_context *pctx);

static inline void
panfrost_dirty_state_all(struct panfrost_context *ctx)
{
        ctx->dirty = ~0;

        for (unsigned i = 0; i < PIPE_SHADER_TYPES; ++i)
                ctx->dirty_shader[i] = ~0;
}

static inline void
panfrost_clean_state_3d(struct panfrost_context *ctx)
{
        ctx->dirty = 0;

        for (unsigned i = 0; i < PIPE_SHADER_TYPES; ++i) {
                if (i != PIPE_SHADER_COMPUTE)
                        ctx->dirty_shader[i] = 0;
        }
}

#endif
