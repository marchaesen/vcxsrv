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
#define CACHE_LINE_SIZE 1024 /* TODO */
#include <sys/mman.h>
#include <assert.h>
#include "pan_resource.h"
#include "pan_job.h"
#include "pan_blend_cso.h"
#include "pan_encoder.h"
#include "pan_texture.h"
#include "midgard_pack.h"

#include "pipe/p_compiler.h"
#include "pipe/p_config.h"
#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_format.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/u_blitter.h"
#include "util/hash_table.h"

#include "midgard/midgard_compile.h"
#include "compiler/shader_enums.h"

/* Forward declare to avoid extra header dep */
struct prim_convert_context;

#define MAX_VARYINGS   4096

#define SET_BIT(lval, bit, cond) \
	if (cond) \
		lval |= (bit); \
	else \
		lval &= ~(bit);

struct panfrost_constant_buffer {
        struct pipe_constant_buffer cb[PIPE_MAX_CONSTANT_BUFFERS];
        uint32_t enabled_mask;
        uint32_t dirty_mask;
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
        struct panfrost_bo *bo;

        /* Whether an occlusion query is for a MSAA framebuffer */
        bool msaa;
};

struct panfrost_fence {
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

        /* Upload manager for small resident GPU-internal data structures, like
         * sampler descriptors. We use an upload manager since the minimum BO
         * size from the kernel is 4kb */
        struct u_upload_mgr *state_uploader;

        /* Sync obj used to keep track of in-flight jobs. */
        uint32_t syncobj;

        /* Bound job batch and map of panfrost_batch_key to job batches */
        struct panfrost_batch *batch;
        struct hash_table *batches;

        /* panfrost_bo -> panfrost_bo_access */
        struct hash_table *accessed_bos;

        /* Within a launch_grid call.. */
        const struct pipe_grid_info *compute_grid;

        /* Bit mask for supported PIPE_DRAW for this hardware */
        unsigned draw_modes;

        struct pipe_framebuffer_state pipe_framebuffer;
        struct panfrost_streamout streamout;

        bool active_queries;
        uint64_t prims_generated;
        uint64_t tf_prims_generated;
        struct panfrost_query *occlusion_query;

        unsigned vertex_count;
        unsigned instance_count;
        unsigned offset_start;
        enum pipe_prim_type active_prim;

        /* If instancing is enabled, vertex count padded for instance; if
         * it is disabled, just equal to plain vertex count */
        unsigned padded_count;

        /* TODO: Multiple uniform buffers (index =/= 0), finer updates? */

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

        struct primconvert_context *primconvert;
        struct blitter_context *blitter;

        struct panfrost_blend_state *blend;

        struct pipe_viewport_state pipe_viewport;
        struct pipe_scissor_state scissor;
        struct pipe_blend_color blend_color;
        struct panfrost_zsa_state *depth_stencil;
        struct pipe_stencil_ref stencil_ref;
        unsigned sample_mask;
        unsigned min_samples;

        struct panfrost_blend_state *blit_blend;
        struct hash_table *blend_shaders;

        struct panfrost_query *cond_query;
        bool cond_cond;
        enum pipe_render_cond_flag cond_mode;

        bool is_noop;
};

/* Corresponds to the CSO */

struct panfrost_rasterizer {
        struct pipe_rasterizer_state base;
};

/* Variants bundle together to form the backing CSO, bundling multiple
 * shaders with varying emulated features baked in */

/* A shader state corresponds to the actual, current variant of the shader */
struct panfrost_shader_state {
        /* Compiled, mapped descriptor, ready for the hardware */
        bool compiled;

        /* Uploaded shader descriptor (TODO: maybe stuff the packed unuploaded
         * bits in a union to save some memory?) */

        struct {
                struct pipe_resource *rsrc;
                uint32_t offset;
        } upload;

        struct pan_shader_info info;

        struct pipe_stream_output_info stream_output;
        uint64_t so_mask;

        /* GPU-executable memory */
        struct panfrost_bo *bo;

        enum pipe_format rt_formats[8];
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

        struct panfrost_shader_state *variants;
        unsigned variant_space;

        unsigned variant_count;

        /* The current active variant */
        unsigned active_variant;
};

struct panfrost_vertex_state {
        unsigned num_elements;

        struct pipe_vertex_element pipe[PIPE_MAX_ATTRIBS];
        unsigned formats[PIPE_MAX_ATTRIBS];
};

struct panfrost_zsa_state {
        struct pipe_depth_stencil_alpha_state base;
        enum mali_func alpha_func;

        /* Precomputed stencil state */
        struct MALI_STENCIL stencil_front;
        struct MALI_STENCIL stencil_back;
        u8 stencil_mask_front;
        u8 stencil_mask_back;
};

struct panfrost_sampler_state {
        struct pipe_sampler_state base;
        struct mali_midgard_sampler_packed hw;
};

/* Misnomer: Sampler view corresponds to textures, not samplers */

struct panfrost_sampler_view {
        struct pipe_sampler_view base;
        struct panfrost_bo *bo;
        struct mali_bifrost_texture_packed bifrost_descriptor;
        mali_ptr texture_bo;
        uint64_t modifier;
};

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

mali_ptr panfrost_sfbd_fragment(struct panfrost_batch *batch, bool has_draws);
mali_ptr panfrost_mfbd_fragment(struct panfrost_batch *batch, bool has_draws);

void
panfrost_attach_mfbd(struct panfrost_batch *batch, unsigned vertex_count);

void
panfrost_attach_sfbd(struct panfrost_batch *batch, unsigned vertex_count);

void
panfrost_emit_midg_tiler(struct panfrost_batch *batch,
                         struct mali_midgard_tiler_packed *tp,
                         unsigned vertex_count);

mali_ptr
panfrost_fragment_job(struct panfrost_batch *batch, bool has_draws);

void
panfrost_shader_compile(struct panfrost_context *ctx,
                        enum pipe_shader_ir ir_type,
                        const void *ir,
                        gl_shader_stage stage,
                        struct panfrost_shader_state *state);

void
panfrost_create_sampler_view_bo(struct panfrost_sampler_view *so,
                                struct pipe_context *pctx,
                                struct pipe_resource *texture);

/* Instancing */

mali_ptr
panfrost_vertex_buffer_address(struct panfrost_context *ctx, unsigned i);

/* Compute */

void
panfrost_compute_context_init(struct pipe_context *pctx);


#endif
