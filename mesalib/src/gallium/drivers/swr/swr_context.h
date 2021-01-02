/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ***************************************************************************/

#ifndef SWR_CONTEXT_H
#define SWR_CONTEXT_H

#include "common/os.h"

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_blitter.h"
#include "rasterizer/memory/SurfaceState.h"
#include "rasterizer/memory/InitMemory.h"
#include "jit_api.h"
#include "swr_state.h"
#include <unordered_map>

#define SWR_NEW_BLEND (1 << 0)
#define SWR_NEW_RASTERIZER (1 << 1)
#define SWR_NEW_DEPTH_STENCIL_ALPHA (1 << 2)
#define SWR_NEW_SAMPLER (1 << 3)
#define SWR_NEW_SAMPLER_VIEW (1 << 4)
#define SWR_NEW_VS (1 << 5)
#define SWR_NEW_FS (1 << 6)
#define SWR_NEW_GS (1 << 7)
#define SWR_NEW_VSCONSTANTS (1 << 8)
#define SWR_NEW_FSCONSTANTS (1 << 9)
#define SWR_NEW_GSCONSTANTS (1 << 10)
#define SWR_NEW_VERTEX (1 << 11)
#define SWR_NEW_STIPPLE (1 << 12)
#define SWR_NEW_SCISSOR (1 << 13)
#define SWR_NEW_VIEWPORT (1 << 14)
#define SWR_NEW_FRAMEBUFFER (1 << 15)
#define SWR_NEW_CLIP (1 << 16)
#define SWR_NEW_SO (1 << 17)
#define SWR_BLOCK_CLIENT_DRAW ( 1 << 18) // Indicates client draw will block
#define SWR_NEW_TCS (1 << 19)
#define SWR_NEW_TES (1 << 20)
#define SWR_NEW_TS (1 << 21)
#define SWR_NEW_TCSCONSTANTS (1 << 22)
#define SWR_NEW_TESCONSTANTS (1 << 23)

namespace std
{
template <> struct hash<BLEND_COMPILE_STATE> {
   std::size_t operator()(const BLEND_COMPILE_STATE &k) const
   {
      return util_hash_crc32(&k, sizeof(k));
   }
};
};

struct swr_jit_texture {
   uint32_t width; // same as number of elements
   uint32_t height;
   uint32_t depth; // doubles as array size
   uint32_t first_level;
   uint32_t last_level;
   const uint8_t *base_ptr;
   uint32_t num_samples;
   uint32_t sample_stride;
   uint32_t row_stride[PIPE_MAX_TEXTURE_LEVELS];
   uint32_t img_stride[PIPE_MAX_TEXTURE_LEVELS];
   uint32_t mip_offsets[PIPE_MAX_TEXTURE_LEVELS];
};

struct swr_jit_sampler {
   float min_lod;
   float max_lod;
   float lod_bias;
   float border_color[4];
};

struct swr_draw_context {
   const float *constantVS[PIPE_MAX_CONSTANT_BUFFERS];
   uint32_t num_constantsVS[PIPE_MAX_CONSTANT_BUFFERS];
   const float *constantFS[PIPE_MAX_CONSTANT_BUFFERS];
   uint32_t num_constantsFS[PIPE_MAX_CONSTANT_BUFFERS];
   const float *constantGS[PIPE_MAX_CONSTANT_BUFFERS];
   uint32_t num_constantsGS[PIPE_MAX_CONSTANT_BUFFERS];
   const float *constantTCS[PIPE_MAX_CONSTANT_BUFFERS];
   uint32_t num_constantsTCS[PIPE_MAX_CONSTANT_BUFFERS];
   const float *constantTES[PIPE_MAX_CONSTANT_BUFFERS];
   uint32_t num_constantsTES[PIPE_MAX_CONSTANT_BUFFERS];

   swr_jit_texture texturesVS[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   swr_jit_sampler samplersVS[PIPE_MAX_SAMPLERS];
   swr_jit_texture texturesFS[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   swr_jit_sampler samplersFS[PIPE_MAX_SAMPLERS];
   swr_jit_texture texturesGS[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   swr_jit_sampler samplersGS[PIPE_MAX_SAMPLERS];
   swr_jit_texture texturesTCS[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   swr_jit_sampler samplersTCS[PIPE_MAX_SAMPLERS];
   swr_jit_texture texturesTES[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   swr_jit_sampler samplersTES[PIPE_MAX_SAMPLERS];

   float userClipPlanes[PIPE_MAX_CLIP_PLANES][4];

   uint32_t polyStipple[32];

   SWR_SURFACE_STATE renderTargets[SWR_NUM_ATTACHMENTS];
   struct swr_query_result *pStats; // @llvm_struct
   SWR_INTERFACE *pAPI; // @llvm_struct - Needed for the swr_memory callbacks
   SWR_TILE_INTERFACE *pTileAPI; // @llvm_struct - Needed for the swr_memory callbacks

   uint64_t* soPrims; //number of primitives written to StreamOut buffer
};

/* gen_llvm_types FINI */

struct swr_context {
   struct pipe_context pipe; /**< base class */

   HANDLE swrContext;

   SWR_TS_STATE tsState;

   /** Constant state objects */
   struct swr_blend_state *blend;
   struct pipe_sampler_state *samplers[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   struct pipe_depth_stencil_alpha_state *depth_stencil;
   struct pipe_rasterizer_state *rasterizer;

   struct swr_vertex_shader *vs;
   struct swr_fragment_shader *fs;
   struct swr_geometry_shader *gs;
   struct swr_tess_control_shader *tcs;
   struct swr_tess_evaluation_shader *tes;
   struct swr_vertex_element_state *velems;

   /** Other rendering state */
   struct pipe_blend_color blend_color;
   struct pipe_stencil_ref stencil_ref;
   struct pipe_clip_state clip;
   struct pipe_constant_buffer
      constants[PIPE_SHADER_TYPES][PIPE_MAX_CONSTANT_BUFFERS];
   struct pipe_framebuffer_state framebuffer;
   struct swr_poly_stipple poly_stipple;
   struct pipe_scissor_state scissors[KNOB_NUM_VIEWPORTS_SCISSORS];
   SWR_RECT swr_scissors[KNOB_NUM_VIEWPORTS_SCISSORS];
   struct pipe_sampler_view *
      sampler_views[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_SAMPLER_VIEWS];

   struct pipe_viewport_state viewports[KNOB_NUM_VIEWPORTS_SCISSORS];
   struct pipe_vertex_buffer vertex_buffer[PIPE_MAX_ATTRIBS];

   struct blitter_context *blitter;

   /** Conditional query object and mode */
   struct pipe_query *render_cond_query;
   enum pipe_render_cond_flag render_cond_mode;
   bool render_cond_cond;
   unsigned active_queries;

   unsigned num_vertex_buffers;
   unsigned num_samplers[PIPE_SHADER_TYPES];
   unsigned num_sampler_views[PIPE_SHADER_TYPES];

   unsigned sample_mask;

   // streamout
   pipe_stream_output_target *so_targets[MAX_SO_STREAMS];
   uint32_t num_so_targets;
   uint64_t so_primCounter; // number of primitives written to StreamOut buffer

   /* Temp storage for user_buffer constants */
   struct swr_scratch_buffers *scratch;

   // blend jit functions
   std::unordered_map<BLEND_COMPILE_STATE, PFN_BLEND_JIT_FUNC> *blendJIT;

   /* Derived SWR API DrawState */
   struct swr_derived_state derived;

   /* SWR private state - draw context */
   struct swr_draw_context swrDC;

   unsigned dirty; /**< Mask of SWR_NEW_x flags */

   SWR_INTERFACE api;
   SWR_TILE_INTERFACE tileApi;

   uint32_t max_draws_in_flight;
};

static INLINE struct swr_context *
swr_context(struct pipe_context *pipe)
{
   return (struct swr_context *)pipe;
}

static INLINE void
swr_update_draw_context(struct swr_context *ctx,
      struct swr_query_result *pqr = nullptr)
{
   swr_draw_context *pDC =
      (swr_draw_context *)ctx->api.pfnSwrGetPrivateContextState(ctx->swrContext);
   if (pqr)
      ctx->swrDC.pStats = pqr;
   memcpy(pDC, &ctx->swrDC, sizeof(swr_draw_context));
}

struct pipe_context *swr_create_context(struct pipe_screen *, void *priv, unsigned flags);

void swr_state_init(struct pipe_context *pipe);

void swr_clear_init(struct pipe_context *pipe);

void swr_draw_init(struct pipe_context *pipe);

void swr_finish(struct pipe_context *pipe);

void swr_do_msaa_resolve(struct pipe_resource *src_resource,
                         struct pipe_resource *dst_resource);
#endif
