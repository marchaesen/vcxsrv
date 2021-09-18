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

#include <llvm/Config/llvm-config.h>

#if LLVM_VERSION_MAJOR < 7
// llvm redefines DEBUG
#pragma push_macro("DEBUG")
#undef DEBUG
#endif

#include <rasterizer/core/state.h>
#include "JitManager.h"

#if LLVM_VERSION_MAJOR < 7
#pragma pop_macro("DEBUG")
#endif

#include "common/os.h"
#include "jit_api.h"
#include "gen_state_llvm.h"
#include "core/multisample.h"
#include "core/state_funcs.h"

#include "gallivm/lp_bld_tgsi.h"
#include "util/format/u_format.h"

#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_helpers.h"
#include "util/u_framebuffer.h"
#include "util/u_viewport.h"
#include "util/u_prim.h"

#include "swr_state.h"
#include "swr_context.h"
#include "gen_surf_state_llvm.h"
#include "gen_swr_context_llvm.h"
#include "swr_screen.h"
#include "swr_resource.h"
#include "swr_tex_sample.h"
#include "swr_scratch.h"
#include "swr_shader.h"
#include "swr_fence.h"

/* These should be pulled out into separate files as necessary
 * Just initializing everything here to get going. */

static void *
swr_create_blend_state(struct pipe_context *pipe,
                       const struct pipe_blend_state *blend)
{
   struct swr_blend_state *state = CALLOC_STRUCT(swr_blend_state);
   assert(state != nullptr);

   memcpy(&state->pipe, blend, sizeof(*blend));

   struct pipe_blend_state *pipe_blend = &state->pipe;

   for (int target = 0;
        target < std::min(SWR_NUM_RENDERTARGETS, PIPE_MAX_COLOR_BUFS);
        target++) {

      struct pipe_rt_blend_state *rt_blend = &pipe_blend->rt[target];
      SWR_RENDER_TARGET_BLEND_STATE &blendState =
         state->blendState.renderTarget[target];
      RENDER_TARGET_BLEND_COMPILE_STATE &compileState =
         state->compileState[target];

      if (target != 0 && !pipe_blend->independent_blend_enable) {
         memcpy(&compileState,
                &state->compileState[0],
                sizeof(RENDER_TARGET_BLEND_COMPILE_STATE));
         continue;
      }

      compileState.blendEnable = rt_blend->blend_enable;
      if (compileState.blendEnable) {
         compileState.sourceAlphaBlendFactor =
            swr_convert_blend_factor(rt_blend->alpha_src_factor);
         compileState.destAlphaBlendFactor =
            swr_convert_blend_factor(rt_blend->alpha_dst_factor);
         compileState.sourceBlendFactor =
            swr_convert_blend_factor(rt_blend->rgb_src_factor);
         compileState.destBlendFactor =
            swr_convert_blend_factor(rt_blend->rgb_dst_factor);

         compileState.colorBlendFunc =
            swr_convert_blend_func(rt_blend->rgb_func);
         compileState.alphaBlendFunc =
            swr_convert_blend_func(rt_blend->alpha_func);
      }
      compileState.logicOpEnable = state->pipe.logicop_enable;
      if (compileState.logicOpEnable) {
         compileState.logicOpFunc =
            swr_convert_logic_op(state->pipe.logicop_func);
      }

      blendState.writeDisableRed =
         (rt_blend->colormask & PIPE_MASK_R) ? 0 : 1;
      blendState.writeDisableGreen =
         (rt_blend->colormask & PIPE_MASK_G) ? 0 : 1;
      blendState.writeDisableBlue =
         (rt_blend->colormask & PIPE_MASK_B) ? 0 : 1;
      blendState.writeDisableAlpha =
         (rt_blend->colormask & PIPE_MASK_A) ? 0 : 1;

      if (rt_blend->colormask == 0)
         compileState.blendEnable = false;
   }

   return state;
}

static void
swr_bind_blend_state(struct pipe_context *pipe, void *blend)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->blend == blend)
      return;

   ctx->blend = (swr_blend_state *)blend;

   ctx->dirty |= SWR_NEW_BLEND;
}

static void
swr_delete_blend_state(struct pipe_context *pipe, void *blend)
{
   FREE(blend);
}

static void
swr_set_blend_color(struct pipe_context *pipe,
                    const struct pipe_blend_color *color)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->blend_color = *color;

   ctx->dirty |= SWR_NEW_BLEND;
}

static void
swr_set_stencil_ref(struct pipe_context *pipe,
                    const struct pipe_stencil_ref ref)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->stencil_ref = ref;

   ctx->dirty |= SWR_NEW_DEPTH_STENCIL_ALPHA;
}

static void *
swr_create_depth_stencil_state(
   struct pipe_context *pipe,
   const struct pipe_depth_stencil_alpha_state *depth_stencil)
{
   struct pipe_depth_stencil_alpha_state *state;

   state = (pipe_depth_stencil_alpha_state *)mem_dup(depth_stencil,
                                                     sizeof *depth_stencil);

   return state;
}

static void
swr_bind_depth_stencil_state(struct pipe_context *pipe, void *depth_stencil)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->depth_stencil == (pipe_depth_stencil_alpha_state *)depth_stencil)
      return;

   ctx->depth_stencil = (pipe_depth_stencil_alpha_state *)depth_stencil;

   ctx->dirty |= SWR_NEW_DEPTH_STENCIL_ALPHA;
}

static void
swr_delete_depth_stencil_state(struct pipe_context *pipe, void *depth)
{
   FREE(depth);
}


static void *
swr_create_rasterizer_state(struct pipe_context *pipe,
                            const struct pipe_rasterizer_state *rast)
{
   struct pipe_rasterizer_state *state;
   state = (pipe_rasterizer_state *)mem_dup(rast, sizeof *rast);

   return state;
}

static void
swr_bind_rasterizer_state(struct pipe_context *pipe, void *handle)
{
   struct swr_context *ctx = swr_context(pipe);
   const struct pipe_rasterizer_state *rasterizer =
      (const struct pipe_rasterizer_state *)handle;

   if (ctx->rasterizer == (pipe_rasterizer_state *)rasterizer)
      return;

   ctx->rasterizer = (pipe_rasterizer_state *)rasterizer;

   ctx->dirty |= SWR_NEW_RASTERIZER;
}

static void
swr_delete_rasterizer_state(struct pipe_context *pipe, void *rasterizer)
{
   FREE(rasterizer);
}


static void *
swr_create_sampler_state(struct pipe_context *pipe,
                         const struct pipe_sampler_state *sampler)
{
   struct pipe_sampler_state *state =
      (pipe_sampler_state *)mem_dup(sampler, sizeof *sampler);

   return state;
}

static void
swr_bind_sampler_states(struct pipe_context *pipe,
                        enum pipe_shader_type shader,
                        unsigned start,
                        unsigned num,
                        void **samplers)
{
   struct swr_context *ctx = swr_context(pipe);
   unsigned i;

   assert(shader < PIPE_SHADER_TYPES);
   assert(start + num <= ARRAY_SIZE(ctx->samplers[shader]));

   /* set the new samplers */
   ctx->num_samplers[shader] = num;
   for (i = 0; i < num; i++) {
      ctx->samplers[shader][start + i] = (pipe_sampler_state *)samplers[i];
   }

   ctx->dirty |= SWR_NEW_SAMPLER;
}

static void
swr_delete_sampler_state(struct pipe_context *pipe, void *sampler)
{
   FREE(sampler);
}


static struct pipe_sampler_view *
swr_create_sampler_view(struct pipe_context *pipe,
                        struct pipe_resource *texture,
                        const struct pipe_sampler_view *templ)
{
   struct pipe_sampler_view *view = CALLOC_STRUCT(pipe_sampler_view);

   if (view) {
      *view = *templ;
      view->reference.count = 1;
      view->texture = NULL;
      pipe_resource_reference(&view->texture, texture);
      view->context = pipe;
   }

   return view;
}

static void
swr_set_sampler_views(struct pipe_context *pipe,
                      enum pipe_shader_type shader,
                      unsigned start,
                      unsigned num,
                      unsigned unbind_num_trailing_slots,
                      bool take_ownership,
                      struct pipe_sampler_view **views)
{
   struct swr_context *ctx = swr_context(pipe);
   uint i;

   assert(num <= PIPE_MAX_SHADER_SAMPLER_VIEWS);

   assert(shader < PIPE_SHADER_TYPES);
   assert(start + num <= ARRAY_SIZE(ctx->sampler_views[shader]));

   /* set the new sampler views */
   ctx->num_sampler_views[shader] = num;
   for (i = 0; i < num; i++) {
      if (take_ownership) {
         pipe_sampler_view_reference(&ctx->sampler_views[shader][start + i],
                                     NULL);
         ctx->sampler_views[shader][start + i] = views[i];
      } else {
         pipe_sampler_view_reference(&ctx->sampler_views[shader][start + i],
                                     views[i]);
      }
   }
   for (; i < num + unbind_num_trailing_slots; i++) {
      pipe_sampler_view_reference(&ctx->sampler_views[shader][start + i],
                                  NULL);
   }

   ctx->dirty |= SWR_NEW_SAMPLER_VIEW;
}

static void
swr_sampler_view_destroy(struct pipe_context *pipe,
                         struct pipe_sampler_view *view)
{
   pipe_resource_reference(&view->texture, NULL);
   FREE(view);
}

static void *
swr_create_vs_state(struct pipe_context *pipe,
                    const struct pipe_shader_state *vs)
{
   struct swr_vertex_shader *swr_vs = new swr_vertex_shader;
   if (!swr_vs)
      return NULL;

   swr_vs->pipe.tokens = tgsi_dup_tokens(vs->tokens);
   swr_vs->pipe.stream_output = vs->stream_output;

   lp_build_tgsi_info(vs->tokens, &swr_vs->info);

   swr_vs->soState = {0};

   if (swr_vs->pipe.stream_output.num_outputs) {
      pipe_stream_output_info *stream_output = &swr_vs->pipe.stream_output;

      swr_vs->soState.soEnable = true;
      // soState.rasterizerDisable set on state dirty
      // soState.streamToRasterizer not used

      for (uint32_t i = 0; i < stream_output->num_outputs; i++) {
         unsigned attrib_slot = stream_output->output[i].register_index;
         attrib_slot = swr_so_adjust_attrib(attrib_slot, swr_vs);
         swr_vs->soState.streamMasks[stream_output->output[i].stream] |=
            (1 << attrib_slot);
      }
      for (uint32_t i = 0; i < MAX_SO_STREAMS; i++) {
        swr_vs->soState.streamNumEntries[i] =
             _mm_popcnt_u32(swr_vs->soState.streamMasks[i]);
       }
   }

   return swr_vs;
}

static void
swr_bind_vs_state(struct pipe_context *pipe, void *vs)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->vs == vs)
      return;

   ctx->vs = (swr_vertex_shader *)vs;
   ctx->dirty |= SWR_NEW_VS;
}

static void
swr_delete_vs_state(struct pipe_context *pipe, void *vs)
{
   struct swr_vertex_shader *swr_vs = (swr_vertex_shader *)vs;
   FREE((void *)swr_vs->pipe.tokens);
   struct swr_screen *screen = swr_screen(pipe->screen);

   /* Defer deletion of vs state */
   swr_fence_work_delete_vs(screen->flush_fence, swr_vs);
}

static void *
swr_create_fs_state(struct pipe_context *pipe,
                    const struct pipe_shader_state *fs)
{
   struct swr_fragment_shader *swr_fs = new swr_fragment_shader;
   if (!swr_fs)
      return NULL;

   swr_fs->pipe.tokens = tgsi_dup_tokens(fs->tokens);

   lp_build_tgsi_info(fs->tokens, &swr_fs->info);

   return swr_fs;
}


static void
swr_bind_fs_state(struct pipe_context *pipe, void *fs)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->fs == fs)
      return;

   ctx->fs = (swr_fragment_shader *)fs;
   ctx->dirty |= SWR_NEW_FS;
}

static void
swr_delete_fs_state(struct pipe_context *pipe, void *fs)
{
   struct swr_fragment_shader *swr_fs = (swr_fragment_shader *)fs;
   FREE((void *)swr_fs->pipe.tokens);
   struct swr_screen *screen = swr_screen(pipe->screen);

   /* Defer deleton of fs state */
   swr_fence_work_delete_fs(screen->flush_fence, swr_fs);
}

static void *
swr_create_gs_state(struct pipe_context *pipe,
                    const struct pipe_shader_state *gs)
{
   struct swr_geometry_shader *swr_gs = new swr_geometry_shader;
   if (!swr_gs)
      return NULL;

   swr_gs->pipe.tokens = tgsi_dup_tokens(gs->tokens);
   lp_build_tgsi_info(gs->tokens, &swr_gs->info);
   return swr_gs;
}

static void
swr_bind_gs_state(struct pipe_context *pipe, void *gs)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->gs == gs)
      return;

   ctx->gs = (swr_geometry_shader *)gs;
   ctx->dirty |= SWR_NEW_GS;
}

static void
swr_delete_gs_state(struct pipe_context *pipe, void *gs)
{
   struct swr_geometry_shader *swr_gs = (swr_geometry_shader *)gs;
   FREE((void *)swr_gs->pipe.tokens);
   struct swr_screen *screen = swr_screen(pipe->screen);

   /* Defer deleton of fs state */
   swr_fence_work_delete_gs(screen->flush_fence, swr_gs);
}

static void *
swr_create_tcs_state(struct pipe_context *pipe,
                     const struct pipe_shader_state *tcs)
{
   struct swr_tess_control_shader *swr_tcs = new swr_tess_control_shader;
   if (!swr_tcs)
      return NULL;

   swr_tcs->pipe.tokens = tgsi_dup_tokens(tcs->tokens);
   lp_build_tgsi_info(tcs->tokens, &swr_tcs->info);
   return swr_tcs;
}

static void
swr_bind_tcs_state(struct pipe_context *pipe, void *tcs)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->tcs == tcs)
      return;

   ctx->tcs = (swr_tess_control_shader *)tcs;
   ctx->dirty |= SWR_NEW_TCS;
   ctx->dirty |= SWR_NEW_TS;
}

static void
swr_delete_tcs_state(struct pipe_context *pipe, void *tcs)
{
   struct swr_tess_control_shader *swr_tcs = (swr_tess_control_shader *)tcs;
   FREE((void *)swr_tcs->pipe.tokens);
   struct swr_screen *screen = swr_screen(pipe->screen);

   /* Defer deleton of tcs state */
   swr_fence_work_delete_tcs(screen->flush_fence, swr_tcs);
}

static void *
swr_create_tes_state(struct pipe_context *pipe,
                     const struct pipe_shader_state *tes)
{
   struct swr_tess_evaluation_shader *swr_tes = new swr_tess_evaluation_shader;
   if (!swr_tes)
      return NULL;

   swr_tes->pipe.tokens = tgsi_dup_tokens(tes->tokens);
   lp_build_tgsi_info(tes->tokens, &swr_tes->info);
   return swr_tes;
}

static void
swr_bind_tes_state(struct pipe_context *pipe, void *tes)
{
   struct swr_context *ctx = swr_context(pipe);

   if (ctx->tes == tes)
      return;

   // Save current tessellator state first
   if (ctx->tes != nullptr) {
      ctx->tes->ts_state = ctx->tsState;
   }

   ctx->tes = (swr_tess_evaluation_shader *)tes;

   ctx->dirty |= SWR_NEW_TES;
   ctx->dirty |= SWR_NEW_TS;
}

static void
swr_delete_tes_state(struct pipe_context *pipe, void *tes)
{
   struct swr_tess_evaluation_shader *swr_tes = (swr_tess_evaluation_shader *)tes;
   FREE((void *)swr_tes->pipe.tokens);
   struct swr_screen *screen = swr_screen(pipe->screen);

   /* Defer deleton of tes state */
   swr_fence_work_delete_tes(screen->flush_fence, swr_tes);
}

static void
swr_set_constant_buffer(struct pipe_context *pipe,
                        enum pipe_shader_type shader,
                        uint index, bool take_ownership,
                        const struct pipe_constant_buffer *cb)
{
   struct swr_context *ctx = swr_context(pipe);
   struct pipe_resource *constants = cb ? cb->buffer : NULL;

   assert(shader < PIPE_SHADER_TYPES);
   assert(index < ARRAY_SIZE(ctx->constants[shader]));

   /* note: reference counting */
   util_copy_constant_buffer(&ctx->constants[shader][index], cb, take_ownership);

   if (shader == PIPE_SHADER_VERTEX) {
      ctx->dirty |= SWR_NEW_VSCONSTANTS;
   } else if (shader == PIPE_SHADER_FRAGMENT) {
      ctx->dirty |= SWR_NEW_FSCONSTANTS;
   } else if (shader == PIPE_SHADER_GEOMETRY) {
      ctx->dirty |= SWR_NEW_GSCONSTANTS;
   } else if (shader == PIPE_SHADER_TESS_CTRL) {
      ctx->dirty |= SWR_NEW_TCSCONSTANTS;
   } else if (shader == PIPE_SHADER_TESS_EVAL) {
      ctx->dirty |= SWR_NEW_TESCONSTANTS;
   }
   if (cb && cb->user_buffer) {
      pipe_resource_reference(&constants, NULL);
   }
}


static void *
swr_create_vertex_elements_state(struct pipe_context *pipe,
                                 unsigned num_elements,
                                 const struct pipe_vertex_element *attribs)
{
   struct swr_vertex_element_state *velems;
   assert(num_elements <= PIPE_MAX_ATTRIBS);
   velems = new swr_vertex_element_state;
   if (velems) {
      memset((void*)&velems->fsState, 0, sizeof(velems->fsState));
      velems->fsState.bVertexIDOffsetEnable = true;
      velems->fsState.numAttribs = num_elements;
      for (unsigned i = 0; i < num_elements; i++) {
         // XXX: we should do this keyed on the VS usage info

         const struct util_format_description *desc =
            util_format_description((enum pipe_format)attribs[i].src_format);

         velems->fsState.layout[i].AlignedByteOffset = attribs[i].src_offset;
         velems->fsState.layout[i].Format =
            mesa_to_swr_format((enum pipe_format)attribs[i].src_format);
         velems->fsState.layout[i].StreamIndex =
            attribs[i].vertex_buffer_index;
         velems->fsState.layout[i].InstanceEnable =
            attribs[i].instance_divisor != 0;
         velems->fsState.layout[i].ComponentControl0 =
            desc->channel[0].type != UTIL_FORMAT_TYPE_VOID
            ? ComponentControl::StoreSrc
            : ComponentControl::Store0;
         velems->fsState.layout[i].ComponentControl1 =
            desc->channel[1].type != UTIL_FORMAT_TYPE_VOID
            ? ComponentControl::StoreSrc
            : ComponentControl::Store0;
         velems->fsState.layout[i].ComponentControl2 =
            desc->channel[2].type != UTIL_FORMAT_TYPE_VOID
            ? ComponentControl::StoreSrc
            : ComponentControl::Store0;
         velems->fsState.layout[i].ComponentControl3 =
            desc->channel[3].type != UTIL_FORMAT_TYPE_VOID
            ? ComponentControl::StoreSrc
            : ComponentControl::Store1Fp;
         velems->fsState.layout[i].ComponentPacking = ComponentEnable::XYZW;
         velems->fsState.layout[i].InstanceAdvancementState =
            attribs[i].instance_divisor;

         /* Calculate the pitch of each stream */
         const SWR_FORMAT_INFO &swr_desc = GetFormatInfo(
            mesa_to_swr_format((enum pipe_format)attribs[i].src_format));
         velems->stream_pitch[attribs[i].vertex_buffer_index] += swr_desc.Bpp;

         if (attribs[i].instance_divisor != 0) {
            velems->instanced_bufs |= 1U << attribs[i].vertex_buffer_index;
            uint32_t *min_instance_div =
               &velems->min_instance_div[attribs[i].vertex_buffer_index];
            if (!*min_instance_div ||
                attribs[i].instance_divisor < *min_instance_div)
               *min_instance_div = attribs[i].instance_divisor;
         }
      }
   }

   return velems;
}

static void
swr_bind_vertex_elements_state(struct pipe_context *pipe, void *velems)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_vertex_element_state *swr_velems =
      (struct swr_vertex_element_state *)velems;

   ctx->velems = swr_velems;
   ctx->dirty |= SWR_NEW_VERTEX;
}

static void
swr_delete_vertex_elements_state(struct pipe_context *pipe, void *velems)
{
   struct swr_vertex_element_state *swr_velems =
      (struct swr_vertex_element_state *) velems;
   /* XXX Need to destroy fetch shader? */
   delete swr_velems;
}


static void
swr_set_vertex_buffers(struct pipe_context *pipe,
                       unsigned start_slot,
                       unsigned num_elements,
                       unsigned unbind_num_trailing_slots,
                       bool take_ownership,
                       const struct pipe_vertex_buffer *buffers)
{
   struct swr_context *ctx = swr_context(pipe);

   assert(num_elements <= PIPE_MAX_ATTRIBS);

   util_set_vertex_buffers_count(ctx->vertex_buffer,
                                 &ctx->num_vertex_buffers,
                                 buffers,
                                 start_slot,
                                 num_elements,
                                 unbind_num_trailing_slots,
                                 take_ownership);

   ctx->dirty |= SWR_NEW_VERTEX;
}


static void
swr_set_polygon_stipple(struct pipe_context *pipe,
                        const struct pipe_poly_stipple *stipple)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->poly_stipple.pipe = *stipple; /* struct copy */
   ctx->dirty |= SWR_NEW_STIPPLE;
}

static void
swr_set_clip_state(struct pipe_context *pipe,
                   const struct pipe_clip_state *clip)
{
   struct swr_context *ctx = swr_context(pipe);

   ctx->clip = *clip;
   /* XXX Unimplemented, but prevents crash */

   ctx->dirty |= SWR_NEW_CLIP;
}


static void
swr_set_scissor_states(struct pipe_context *pipe,
                       unsigned start_slot,
                       unsigned num_scissors,
                       const struct pipe_scissor_state *scissors)
{
   struct swr_context *ctx = swr_context(pipe);

   memcpy(ctx->scissors + start_slot, scissors,
          sizeof(struct pipe_scissor_state) * num_scissors);

   for (unsigned i = 0; i < num_scissors; i++) {
      auto idx = start_slot + i;
      ctx->swr_scissors[idx].xmin = scissors[idx].minx;
      ctx->swr_scissors[idx].xmax = scissors[idx].maxx;
      ctx->swr_scissors[idx].ymin = scissors[idx].miny;
      ctx->swr_scissors[idx].ymax = scissors[idx].maxy;
   }
   ctx->dirty |= SWR_NEW_SCISSOR;
}

static void
swr_set_viewport_states(struct pipe_context *pipe,
                        unsigned start_slot,
                        unsigned num_viewports,
                        const struct pipe_viewport_state *vpt)
{
   struct swr_context *ctx = swr_context(pipe);

   memcpy(ctx->viewports + start_slot, vpt, sizeof(struct pipe_viewport_state) * num_viewports);
   ctx->dirty |= SWR_NEW_VIEWPORT;
}


static void
swr_set_framebuffer_state(struct pipe_context *pipe,
                          const struct pipe_framebuffer_state *fb)
{
   struct swr_context *ctx = swr_context(pipe);

   bool changed = !util_framebuffer_state_equal(&ctx->framebuffer, fb);

   assert(fb->width <= KNOB_GUARDBAND_WIDTH);
   assert(fb->height <= KNOB_GUARDBAND_HEIGHT);

   if (changed) {
      util_copy_framebuffer_state(&ctx->framebuffer, fb);

      /* 0 and 1 both indicate no msaa.  Core doesn't understand 0 samples */
      ctx->framebuffer.samples = std::max((ubyte)1, ctx->framebuffer.samples);

      ctx->dirty |= SWR_NEW_FRAMEBUFFER;
   }
}


static void
swr_set_sample_mask(struct pipe_context *pipe, unsigned sample_mask)
{
   struct swr_context *ctx = swr_context(pipe);

   if (sample_mask != ctx->sample_mask) {
      ctx->sample_mask = sample_mask;
      ctx->dirty |= SWR_NEW_RASTERIZER;
   }
}

/*
 * MSAA fixed sample position table
 * used by update_derived and get_sample_position
 * (integer locations on a 16x16 grid)
 */
static const uint8_t swr_sample_positions[][2] =
{ /* 1x*/ { 8, 8},
  /* 2x*/ {12,12},{ 4, 4},
  /* 4x*/ { 6, 2},{14, 6},{ 2,10},{10,14},
  /* 8x*/ { 9, 5},{ 7,11},{13, 9},{ 5, 3},
          { 3,13},{ 1, 7},{11,15},{15, 1},
  /*16x*/ { 9, 9},{ 7, 5},{ 5,10},{12, 7},
          { 3, 6},{10,13},{13,11},{11, 3},
          { 6,14},{ 8, 1},{ 4, 2},{ 2,12},
          { 0, 8},{15, 4},{14,15},{ 1, 0} };

static void
swr_get_sample_position(struct pipe_context *pipe,
                        unsigned sample_count, unsigned sample_index,
                        float *out_value)
{
   /* validate sample_count */
   sample_count = GetNumSamples(GetSampleCount(sample_count));

   const uint8_t *sample = swr_sample_positions[sample_count-1 + sample_index];
   out_value[0] = sample[0] / 16.0f;
   out_value[1] = sample[1] / 16.0f;
}


/*
 * Update resource in-use status
 * All resources bound to color or depth targets marked as WRITE resources.
 * VBO Vertex/index buffers and texture views marked as READ resources.
 */
void
swr_update_resource_status(struct pipe_context *pipe,
                           const struct pipe_draw_info *p_draw_info)
{
   struct swr_context *ctx = swr_context(pipe);
   struct pipe_framebuffer_state *fb = &ctx->framebuffer;

   /* colorbuffer targets */
   if (fb->nr_cbufs)
      for (uint32_t i = 0; i < fb->nr_cbufs; ++i)
         if (fb->cbufs[i])
            swr_resource_write(fb->cbufs[i]->texture);

   /* depth/stencil target */
   if (fb->zsbuf)
      swr_resource_write(fb->zsbuf->texture);

   /* VBO vertex buffers */
   for (uint32_t i = 0; i < ctx->num_vertex_buffers; i++) {
      struct pipe_vertex_buffer *vb = &ctx->vertex_buffer[i];
      if (!vb->is_user_buffer && vb->buffer.resource)
         swr_resource_read(vb->buffer.resource);
   }

   /* VBO index buffer */
   if (p_draw_info && p_draw_info->index_size) {
      if (!p_draw_info->has_user_indices)
         swr_resource_read(p_draw_info->index.resource);
   }

   /* transform feedback buffers */
   for (uint32_t i = 0; i < ctx->num_so_targets; i++) {
      struct pipe_stream_output_target *target = ctx->so_targets[i];
      if (target && target->buffer)
         swr_resource_write(target->buffer);
   }

   /* texture sampler views */
   for (uint32_t j : {PIPE_SHADER_VERTEX, PIPE_SHADER_FRAGMENT}) {
      for (uint32_t i = 0; i < ctx->num_sampler_views[j]; i++) {
         struct pipe_sampler_view *view = ctx->sampler_views[j][i];
         if (view)
            swr_resource_read(view->texture);
      }
   }

   /* constant buffers */
   for (uint32_t j : {PIPE_SHADER_VERTEX, PIPE_SHADER_FRAGMENT}) {
      for (uint32_t i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; i++) {
         struct pipe_constant_buffer *cb = &ctx->constants[j][i];
         if (cb->buffer)
            swr_resource_read(cb->buffer);
      }
   }
}

static void
swr_update_texture_state(struct swr_context *ctx,
                         enum pipe_shader_type shader_type,
                         unsigned num_sampler_views,
                         swr_jit_texture *textures)
{
   for (unsigned i = 0; i < num_sampler_views; i++) {
      struct pipe_sampler_view *view =
         ctx->sampler_views[shader_type][i];
      struct swr_jit_texture *jit_tex = &textures[i];

      memset(jit_tex, 0, sizeof(*jit_tex));
      if (view) {
         struct pipe_resource *res = view->texture;
         struct swr_resource *swr_res = swr_resource(res);
         SWR_SURFACE_STATE *swr = &swr_res->swr;
         size_t *mip_offsets = swr_res->mip_offsets;
         if (swr_res->has_depth && swr_res->has_stencil &&
            !util_format_has_depth(util_format_description(view->format))) {
            swr = &swr_res->secondary;
            mip_offsets = swr_res->secondary_mip_offsets;
         }

         jit_tex->width = res->width0;
         jit_tex->height = res->height0;
         jit_tex->base_ptr = (uint8_t*)swr->xpBaseAddress;
         jit_tex->num_samples = swr->numSamples;
         jit_tex->sample_stride = 0;
         if (view->target != PIPE_BUFFER) {
            jit_tex->first_level = view->u.tex.first_level;
            jit_tex->last_level = view->u.tex.last_level;
            if (view->target == PIPE_TEXTURE_3D)
               jit_tex->depth = res->depth0;
            else
               jit_tex->depth =
                  view->u.tex.last_layer - view->u.tex.first_layer + 1;
            jit_tex->base_ptr += view->u.tex.first_layer *
               swr->qpitch * swr->pitch;
         } else {
            unsigned view_blocksize = util_format_get_blocksize(view->format);
            jit_tex->base_ptr += view->u.buf.offset;
            jit_tex->width = view->u.buf.size / view_blocksize;
            jit_tex->depth = 1;
         }

         for (unsigned level = jit_tex->first_level;
              level <= jit_tex->last_level;
              level++) {
            jit_tex->row_stride[level] = swr->pitch;
            jit_tex->img_stride[level] = swr->qpitch * swr->pitch;
            jit_tex->mip_offsets[level] = mip_offsets[level];
         }
      }
   }
}

static void
swr_update_sampler_state(struct swr_context *ctx,
                         enum pipe_shader_type shader_type,
                         unsigned num_samplers,
                         swr_jit_sampler *samplers)
{
   for (unsigned i = 0; i < num_samplers; i++) {
      const struct pipe_sampler_state *sampler =
         ctx->samplers[shader_type][i];

      if (sampler) {
         samplers[i].min_lod = sampler->min_lod;
         samplers[i].max_lod = sampler->max_lod;
         samplers[i].lod_bias = sampler->lod_bias;
         COPY_4V(samplers[i].border_color, sampler->border_color.f);
      }
   }
}

static void
swr_update_constants(struct swr_context *ctx, enum pipe_shader_type shaderType)
{
   swr_draw_context *pDC = &ctx->swrDC;

   const float **constant;
   uint32_t *num_constants;
   struct swr_scratch_space *scratch;

   switch (shaderType) {
   case PIPE_SHADER_VERTEX:
      constant = pDC->constantVS;
      num_constants = pDC->num_constantsVS;
      scratch = &ctx->scratch->vs_constants;
      break;
   case PIPE_SHADER_FRAGMENT:
      constant = pDC->constantFS;
      num_constants = pDC->num_constantsFS;
      scratch = &ctx->scratch->fs_constants;
      break;
   case PIPE_SHADER_GEOMETRY:
      constant = pDC->constantGS;
      num_constants = pDC->num_constantsGS;
      scratch = &ctx->scratch->gs_constants;
      break;
   case PIPE_SHADER_TESS_CTRL:
      constant = pDC->constantTCS;
      num_constants = pDC->num_constantsTCS;
      scratch = &ctx->scratch->tcs_constants;
      break;
   case PIPE_SHADER_TESS_EVAL:
      constant = pDC->constantTES;
      num_constants = pDC->num_constantsTES;
      scratch = &ctx->scratch->tes_constants;
      break;
   default:
      assert(0 && "Unsupported shader type constants");
      return;
   }

   for (UINT i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; i++) {
      const pipe_constant_buffer *cb = &ctx->constants[shaderType][i];
      num_constants[i] = cb->buffer_size;
      if (cb->buffer) {
         constant[i] =
            (const float *)(swr_resource_data(cb->buffer) +
                            cb->buffer_offset);
      } else {
         /* Need to copy these constants to scratch space */
         if (cb->user_buffer && cb->buffer_size) {
            const void *ptr =
               ((const uint8_t *)cb->user_buffer + cb->buffer_offset);
            uint32_t size = AlignUp(cb->buffer_size, 4);
            ptr = swr_copy_to_scratch_space(ctx, scratch, ptr, size);
            constant[i] = (const float *)ptr;
         }
      }
   }
}

static bool
swr_change_rt(struct swr_context *ctx,
              unsigned attachment,
              const struct pipe_surface *sf)
{
   swr_draw_context *pDC = &ctx->swrDC;
   struct SWR_SURFACE_STATE *rt = &pDC->renderTargets[attachment];

   /* Do nothing if the render target hasn't changed */
   if ((!sf || !sf->texture) && (void*)(rt->xpBaseAddress) == nullptr)
      return false;

   /* Deal with disabling RT up front */
   if (!sf || !sf->texture) {
      /* If detaching attachment, mark tiles as RESOLVED so core
       * won't try to load from non-existent target. */
      swr_store_render_target(&ctx->pipe, attachment, SWR_TILE_RESOLVED);
      *rt = {0};
      return true;
   }

   const struct swr_resource *swr = swr_resource(sf->texture);
   const SWR_SURFACE_STATE *swr_surface = &swr->swr;
   SWR_FORMAT fmt = mesa_to_swr_format(sf->format);

   if (attachment == SWR_ATTACHMENT_STENCIL && swr->secondary.xpBaseAddress) {
      swr_surface = &swr->secondary;
      fmt = swr_surface->format;
   }

   if (rt->xpBaseAddress == swr_surface->xpBaseAddress &&
       rt->format == fmt &&
       rt->lod == sf->u.tex.level &&
       rt->arrayIndex == sf->u.tex.first_layer)
      return false;

   bool need_fence = false;

   /* StoreTile for changed target */
   if (rt->xpBaseAddress) {
      /* If changing attachment to a new target, mark tiles as
       * INVALID so they are reloaded from surface. */
      swr_store_render_target(&ctx->pipe, attachment, SWR_TILE_INVALID);
      need_fence = true;
   } else {
      /* if no previous attachment, invalidate tiles that may be marked
       * RESOLVED because of an old attachment */
      swr_invalidate_render_target(&ctx->pipe, attachment, sf->width, sf->height);
      /* no need to set fence here */
   }

   /* Make new attachment */
   *rt = *swr_surface;
   rt->format = fmt;
   rt->lod = sf->u.tex.level;
   rt->arrayIndex = sf->u.tex.first_layer;

   return need_fence;
}

/*
 * for cases where resources are shared between contexts, invalidate
 * this ctx's resource. so it can be fetched fresh.  Old ctx's resource
 * is already stored during a flush
 */
static inline void
swr_invalidate_buffers_after_ctx_change(struct pipe_context *pipe)
{
   struct swr_context *ctx = swr_context(pipe);

   for (uint32_t i = 0; i < ctx->framebuffer.nr_cbufs; i++) {
      struct pipe_surface *cb = ctx->framebuffer.cbufs[i];
      if (cb) {
         struct swr_resource *res = swr_resource(cb->texture);
         if (res->curr_pipe != pipe) {
            /* if curr_pipe is NULL (first use), status should not be WRITE */
            assert(res->curr_pipe || !(res->status & SWR_RESOURCE_WRITE));
            if (res->status & SWR_RESOURCE_WRITE) {
               swr_invalidate_render_target(pipe, i, cb->width, cb->height);
            }
         }
         res->curr_pipe = pipe;
      }
   }
   if (ctx->framebuffer.zsbuf) {
      struct pipe_surface *zb = ctx->framebuffer.zsbuf;
      if (zb) {
         struct swr_resource *res = swr_resource(zb->texture);
         if (res->curr_pipe != pipe) {
            /* if curr_pipe is NULL (first use), status should not be WRITE */
            assert(res->curr_pipe || !(res->status & SWR_RESOURCE_WRITE));
            if (res->status & SWR_RESOURCE_WRITE) {
               swr_invalidate_render_target(pipe, SWR_ATTACHMENT_DEPTH, zb->width, zb->height);
               swr_invalidate_render_target(pipe, SWR_ATTACHMENT_STENCIL, zb->width, zb->height);
            }
         }
         res->curr_pipe = pipe;
      }
   }
}

static inline void
swr_user_vbuf_range(const struct pipe_draw_info *info,
                    const struct swr_vertex_element_state *velems,
                    const struct pipe_vertex_buffer *vb,
                    uint32_t i,
                    uint32_t *totelems,
                    uint32_t *base,
                    uint32_t *size,
                    int index_bias)
{
   /* FIXME: The size is too large - we don't access the full extra stride. */
   unsigned elems;
   unsigned elem_pitch = vb->stride + velems->stream_pitch[i];
   if (velems->instanced_bufs & (1U << i)) {
      elems = info->instance_count / velems->min_instance_div[i] + 1;
      *totelems = info->start_instance + elems;
      *base = info->start_instance * vb->stride;
      *size = elems * elem_pitch;
   } else if (vb->stride) {
      elems = info->max_index - info->min_index + 1;
      *totelems = (info->max_index + (info->index_size ? index_bias : 0)) + 1;
      *base = (info->min_index + (info->index_size ? index_bias : 0)) * vb->stride;
      *size = elems * elem_pitch;
   } else {
      *totelems = 1;
      *base = 0;
      *size = velems->stream_pitch[i];
   }
}

static void
swr_update_poly_stipple(struct swr_context *ctx)
{
   struct swr_draw_context *pDC = &ctx->swrDC;

   assert(sizeof(ctx->poly_stipple.pipe.stipple) == sizeof(pDC->polyStipple));
   memcpy(pDC->polyStipple,
          ctx->poly_stipple.pipe.stipple,
          sizeof(ctx->poly_stipple.pipe.stipple));
}


static struct tgsi_shader_info *
swr_get_last_fe(const struct swr_context *ctx)
{
   tgsi_shader_info *pLastFE = &ctx->vs->info.base;

   if (ctx->gs) {
      pLastFE = &ctx->gs->info.base;
   }
   else if (ctx->tes) {
      pLastFE = &ctx->tes->info.base;
   }
   else if (ctx->tcs) {
      pLastFE = &ctx->tcs->info.base;
   }
   return pLastFE;
}


void
swr_update_derived(struct pipe_context *pipe,
                   const struct pipe_draw_info *p_draw_info,
                   const struct pipe_draw_start_count_bias *draw)
{
   struct swr_context *ctx = swr_context(pipe);
   struct swr_screen *screen = swr_screen(pipe->screen);

   /* When called from swr_clear (p_draw_info = null), set any null
    * state-objects to the dummy state objects to prevent nullptr dereference
    * in validation below.
    *
    * Important that this remains static for zero initialization.  These
    * aren't meant to be proper state objects, just empty structs. They will
    * not be written to.
    *
    * Shaders can't be part of the union since they contain std::unordered_map
    */
   static struct {
      union {
         struct pipe_rasterizer_state rasterizer;
         struct pipe_depth_stencil_alpha_state depth_stencil;
         struct swr_blend_state blend;
      } state;
      struct swr_vertex_shader vs;
      struct swr_fragment_shader fs;
   } swr_dummy;

   if (!p_draw_info) {
      if (!ctx->rasterizer)
         ctx->rasterizer = &swr_dummy.state.rasterizer;
      if (!ctx->depth_stencil)
         ctx->depth_stencil = &swr_dummy.state.depth_stencil;
      if (!ctx->blend)
         ctx->blend = &swr_dummy.state.blend;
      if (!ctx->vs)
         ctx->vs = &swr_dummy.vs;
      if (!ctx->fs)
         ctx->fs = &swr_dummy.fs;
   }

   /* Update screen->pipe to current pipe context. */
   screen->pipe = pipe;

   /* Any state that requires dirty flags to be re-triggered sets this mask */
   /* For example, user_buffer vertex and index buffers. */
   unsigned post_update_dirty_flags = 0;

   /* bring resources that changed context up-to-date */
   swr_invalidate_buffers_after_ctx_change(pipe);

   /* Render Targets */
   if (ctx->dirty & SWR_NEW_FRAMEBUFFER) {
      struct pipe_framebuffer_state *fb = &ctx->framebuffer;
      const struct util_format_description *desc = NULL;
      bool need_fence = false;

      /* colorbuffer targets */
      if (fb->nr_cbufs) {
         for (unsigned i = 0; i < fb->nr_cbufs; ++i)
            need_fence |= swr_change_rt(
                  ctx, SWR_ATTACHMENT_COLOR0 + i, fb->cbufs[i]);
      }
      for (unsigned i = fb->nr_cbufs; i < SWR_NUM_RENDERTARGETS; ++i)
         need_fence |= swr_change_rt(ctx, SWR_ATTACHMENT_COLOR0 + i, NULL);

      /* depth/stencil target */
      if (fb->zsbuf)
         desc = util_format_description(fb->zsbuf->format);
      if (fb->zsbuf && util_format_has_depth(desc))
         need_fence |= swr_change_rt(ctx, SWR_ATTACHMENT_DEPTH, fb->zsbuf);
      else
         need_fence |= swr_change_rt(ctx, SWR_ATTACHMENT_DEPTH, NULL);

      if (fb->zsbuf && util_format_has_stencil(desc))
         need_fence |= swr_change_rt(ctx, SWR_ATTACHMENT_STENCIL, fb->zsbuf);
      else
         need_fence |= swr_change_rt(ctx, SWR_ATTACHMENT_STENCIL, NULL);

      /* This fence ensures any attachment changes are resolved before the
       * next draw */
      if (need_fence)
         swr_fence_submit(ctx, screen->flush_fence);
   }

   /* Raster state */
   if (ctx->dirty & (SWR_NEW_RASTERIZER |
                     SWR_NEW_VS | // clipping
                     SWR_NEW_TES |
                     SWR_NEW_TCS |
                     SWR_NEW_FRAMEBUFFER)) {
      pipe_rasterizer_state *rasterizer = ctx->rasterizer;
      pipe_framebuffer_state *fb = &ctx->framebuffer;

      SWR_RASTSTATE *rastState = &ctx->derived.rastState;
      rastState->cullMode = swr_convert_cull_mode(rasterizer->cull_face);
      rastState->frontWinding = rasterizer->front_ccw
         ? SWR_FRONTWINDING_CCW
         : SWR_FRONTWINDING_CW;
      rastState->scissorEnable = rasterizer->scissor;
      rastState->pointSize = rasterizer->point_size > 0.0f
         ? rasterizer->point_size
         : 1.0f;
      rastState->lineWidth = rasterizer->line_width > 0.0f
         ? rasterizer->line_width
         : 1.0f;

      rastState->pointParam = rasterizer->point_size_per_vertex;

      rastState->pointSpriteEnable = rasterizer->sprite_coord_enable;
      rastState->pointSpriteTopOrigin =
         rasterizer->sprite_coord_mode == PIPE_SPRITE_COORD_UPPER_LEFT;

      /* If SWR_MSAA_FORCE_ENABLE is set, turn msaa on */
      if (screen->msaa_force_enable && !rasterizer->multisample) {
         /* Force enable and use the value the surface was created with */
         rasterizer->multisample = true;
         fb->samples = swr_resource(fb->cbufs[0]->texture)->swr.numSamples;
         fprintf(stderr,"msaa force enable: %d samples\n", fb->samples);
      }

      rastState->sampleCount = GetSampleCount(fb->samples);
      rastState->forcedSampleCount = false;
      rastState->bIsCenterPattern = !rasterizer->multisample;
      rastState->pixelLocation = SWR_PIXEL_LOCATION_CENTER;

      /* Only initialize sample positions if msaa is enabled */
      if (rasterizer->multisample) {
         for (uint32_t i = 0; i < fb->samples; i++) {
            const uint8_t *sample = swr_sample_positions[fb->samples-1 + i];
            rastState->samplePositions.SetXi(i, sample[0] << 4);
            rastState->samplePositions.SetYi(i, sample[1] << 4);
            rastState->samplePositions.SetX (i, sample[0] / 16.0f);
            rastState->samplePositions.SetY (i, sample[1] / 16.0f);
         }
         rastState->samplePositions.PrecalcSampleData(fb->samples);
      }

      bool do_offset = false;
      switch (rasterizer->fill_front) {
      case PIPE_POLYGON_MODE_FILL:
         do_offset = rasterizer->offset_tri;
         break;
      case PIPE_POLYGON_MODE_LINE:
         do_offset = rasterizer->offset_line;
         break;
      case PIPE_POLYGON_MODE_POINT:
         do_offset = rasterizer->offset_point;
         break;
      }

      if (do_offset) {
         rastState->depthBias = rasterizer->offset_units;
         rastState->slopeScaledDepthBias = rasterizer->offset_scale;
         rastState->depthBiasClamp = rasterizer->offset_clamp;
      } else {
         rastState->depthBias = 0;
         rastState->slopeScaledDepthBias = 0;
         rastState->depthBiasClamp = 0;
      }

      /* translate polygon mode, at least for the front==back case */
      rastState->fillMode = swr_convert_fill_mode(rasterizer->fill_front);

      struct pipe_surface *zb = fb->zsbuf;
      if (zb && swr_resource(zb->texture)->has_depth)
         rastState->depthFormat = swr_resource(zb->texture)->swr.format;

      rastState->depthClipEnable = rasterizer->depth_clip_near;
      rastState->clipEnable = rasterizer->depth_clip_near | rasterizer->depth_clip_far;
      rastState->clipHalfZ = rasterizer->clip_halfz;

      ctx->api.pfnSwrSetRastState(ctx->swrContext, rastState);
   }

   /* Viewport */
   if (ctx->dirty & (SWR_NEW_VIEWPORT | SWR_NEW_FRAMEBUFFER
                     | SWR_NEW_RASTERIZER)) {
      pipe_viewport_state *state = &ctx->viewports[0];
      pipe_framebuffer_state *fb = &ctx->framebuffer;
      pipe_rasterizer_state *rasterizer = ctx->rasterizer;

      SWR_VIEWPORT *vp = &ctx->derived.vp[0];
      SWR_VIEWPORT_MATRICES *vpm = &ctx->derived.vpm;

      for (unsigned i = 0; i < KNOB_NUM_VIEWPORTS_SCISSORS; i++) {
         vp->x = state->translate[0] - state->scale[0];
         vp->width = 2 * state->scale[0];
         vp->y = state->translate[1] - fabs(state->scale[1]);
         vp->height = 2 * fabs(state->scale[1]);
         util_viewport_zmin_zmax(state, rasterizer->clip_halfz,
                                 &vp->minZ, &vp->maxZ);

         if (rasterizer->depth_clip_near) {
            vp->minZ = 0.0f;
         }

         if (rasterizer->depth_clip_far) {
            vp->maxZ = 1.0f;
         }

         vpm->m00[i] = state->scale[0];
         vpm->m11[i] = state->scale[1];
         vpm->m22[i] = state->scale[2];
         vpm->m30[i] = state->translate[0];
         vpm->m31[i] = state->translate[1];
         vpm->m32[i] = state->translate[2];

         /* Now that the matrix is calculated, clip the view coords to screen
          * size.  OpenGL allows for -ve x,y in the viewport. */
         if (vp->x < 0.0f) {
            vp->width += vp->x;
            vp->x = 0.0f;
         }
         if (vp->y < 0.0f) {
            vp->height += vp->y;
            vp->y = 0.0f;
         }
         vp->width = std::min(vp->width, (float) fb->width - vp->x);
         vp->height = std::min(vp->height, (float) fb->height - vp->y);

         vp++;
         state++;
      }
      ctx->api.pfnSwrSetViewports(ctx->swrContext, KNOB_NUM_VIEWPORTS_SCISSORS,
                                  &ctx->derived.vp[0], &ctx->derived.vpm);
   }

   /* When called from swr_clear (p_draw_info = null), render targets,
    * rasterState and viewports (dependent on render targets) are the only
    * necessary validation.  Defer remaining validation by setting
    * post_update_dirty_flags and clear all dirty flags.  BackendState is
    * still unconditionally validated below */
   if (!p_draw_info) {
      post_update_dirty_flags = ctx->dirty & ~(SWR_NEW_FRAMEBUFFER |
                                               SWR_NEW_RASTERIZER |
                                               SWR_NEW_VIEWPORT);
      ctx->dirty = 0;
   }

   /* Scissor */
   if (ctx->dirty & SWR_NEW_SCISSOR) {
      ctx->api.pfnSwrSetScissorRects(ctx->swrContext, KNOB_NUM_VIEWPORTS_SCISSORS, ctx->swr_scissors);
   }

   /* Set vertex & index buffers */
   if (ctx->dirty & SWR_NEW_VERTEX) {
      const struct pipe_draw_info &info = *p_draw_info;

      /* vertex buffers */
      SWR_VERTEX_BUFFER_STATE swrVertexBuffers[PIPE_MAX_ATTRIBS];
      for (UINT i = 0; i < ctx->num_vertex_buffers; i++) {
         uint32_t size = 0, pitch = 0, elems = 0, partial_inbounds = 0;
         uint32_t min_vertex_index = 0;
         const uint8_t *p_data;
         struct pipe_vertex_buffer *vb = &ctx->vertex_buffer[i];

         pitch = vb->stride;
         if (vb->is_user_buffer) {
            /* Client buffer
             * client memory is one-time use, re-trigger SWR_NEW_VERTEX to
             * revalidate on each draw */
            post_update_dirty_flags |= SWR_NEW_VERTEX;

            uint32_t base;
            swr_user_vbuf_range(&info, ctx->velems, vb, i, &elems, &base, &size, draw->index_bias);
            partial_inbounds = 0;
            min_vertex_index = info.min_index + (info.index_size ? draw->index_bias : 0);

            size = AlignUp(size, 4);
            /* If size of client memory copy is too large, don't copy. The
             * draw will access user-buffer directly and then block.  This is
             * faster than queuing many large client draws. */
            if (size >= screen->client_copy_limit) {
               post_update_dirty_flags |= SWR_BLOCK_CLIENT_DRAW;
               p_data = (const uint8_t *) vb->buffer.user;
            } else {
               /* Copy only needed vertices to scratch space */
               const void *ptr = (const uint8_t *) vb->buffer.user + base;
               ptr = (uint8_t *)swr_copy_to_scratch_space(
                     ctx, &ctx->scratch->vertex_buffer, ptr, size);
               p_data = (const uint8_t *)ptr - base;
            }
         } else if (vb->buffer.resource) {
            /* VBO */
            if (!pitch) {
               /* If pitch=0 (ie vb->stride), buffer contains a single
                * constant attribute.  Use the stream_pitch which was
                * calculated during creation of vertex_elements_state for the
                * size of the attribute. */
               size = ctx->velems->stream_pitch[i];
               elems = 1;
               partial_inbounds = 0;
               min_vertex_index = 0;
            } else {
               /* size is based on buffer->width0 rather than info.max_index
                * to prevent having to validate VBO on each draw. */
               size = vb->buffer.resource->width0;
               elems = size / pitch;
               partial_inbounds = size % pitch;
               min_vertex_index = 0;
            }

            p_data = swr_resource_data(vb->buffer.resource) + vb->buffer_offset;
         } else
            p_data = NULL;

         swrVertexBuffers[i] = {0};
         swrVertexBuffers[i].index = i;
         swrVertexBuffers[i].pitch = pitch;
         swrVertexBuffers[i].xpData = (gfxptr_t) p_data;
         swrVertexBuffers[i].size = size;
         swrVertexBuffers[i].minVertex = min_vertex_index;
         swrVertexBuffers[i].maxVertex = elems;
         swrVertexBuffers[i].partialInboundsSize = partial_inbounds;
      }

      ctx->api.pfnSwrSetVertexBuffers(
         ctx->swrContext, ctx->num_vertex_buffers, swrVertexBuffers);

      /* index buffer, if required (info passed in by swr_draw_vbo) */
      SWR_FORMAT index_type = R32_UINT; /* Default for non-indexed draws */
      if (info.index_size) {
         const uint8_t *p_data;
         uint32_t size, pitch;

         pitch = info.index_size ? info.index_size : sizeof(uint32_t);
         index_type = swr_convert_index_type(pitch);

         if (!info.has_user_indices) {
            /* VBO
             * size is based on buffer->width0 rather than info.count
             * to prevent having to validate VBO on each draw */
            size = info.index.resource->width0;
            p_data = swr_resource_data(info.index.resource);
         } else {
            /* Client buffer
             * client memory is one-time use, re-trigger SWR_NEW_VERTEX to
             * revalidate on each draw */
            post_update_dirty_flags |= SWR_NEW_VERTEX;

            size = draw->count * pitch;

            size = AlignUp(size, 4);
            /* If size of client memory copy is too large, don't copy. The
             * draw will access user-buffer directly and then block.  This is
             * faster than queuing many large client draws. */
            if (size >= screen->client_copy_limit) {
               post_update_dirty_flags |= SWR_BLOCK_CLIENT_DRAW;
               p_data = (const uint8_t *) info.index.user +
                        draw->start * info.index_size;
            } else {
               /* Copy indices to scratch space */
               const void *ptr = (char*)info.index.user +
                                 draw->start * info.index_size;
               ptr = swr_copy_to_scratch_space(
                     ctx, &ctx->scratch->index_buffer, ptr, size);
               p_data = (const uint8_t *)ptr;
            }
         }

         SWR_INDEX_BUFFER_STATE swrIndexBuffer;
         swrIndexBuffer.format = swr_convert_index_type(info.index_size);
         swrIndexBuffer.xpIndices = (gfxptr_t) p_data;
         swrIndexBuffer.size = size;

         ctx->api.pfnSwrSetIndexBuffer(ctx->swrContext, &swrIndexBuffer);
      }

      struct swr_vertex_element_state *velems = ctx->velems;
      if (velems && velems->fsState.indexType != index_type) {
         velems->fsFunc = NULL;
         velems->fsState.indexType = index_type;
      }
   }

   /* GeometryShader */
   if (ctx->dirty & (SWR_NEW_GS |
                     SWR_NEW_VS |
                     SWR_NEW_TCS |
                     SWR_NEW_TES |
                     SWR_NEW_SAMPLER |
                     SWR_NEW_SAMPLER_VIEW)) {
      if (ctx->gs) {
         swr_jit_gs_key key;
         swr_generate_gs_key(key, ctx, ctx->gs);
         auto search = ctx->gs->map.find(key);
         PFN_GS_FUNC func;
         if (search != ctx->gs->map.end()) {
            func = search->second->shader;
         } else {
            func = swr_compile_gs(ctx, key);
         }
         ctx->api.pfnSwrSetGsFunc(ctx->swrContext, func);

         /* JIT sampler state */
         if (ctx->dirty & SWR_NEW_SAMPLER) {
            swr_update_sampler_state(ctx,
                                     PIPE_SHADER_GEOMETRY,
                                     key.nr_samplers,
                                     ctx->swrDC.samplersGS);
         }

         /* JIT sampler view state */
         if (ctx->dirty & (SWR_NEW_SAMPLER_VIEW | SWR_NEW_FRAMEBUFFER)) {
            swr_update_texture_state(ctx,
                                     PIPE_SHADER_GEOMETRY,
                                     key.nr_sampler_views,
                                     ctx->swrDC.texturesGS);
         }

         ctx->api.pfnSwrSetGsState(ctx->swrContext, &ctx->gs->gsState);
      } else {
         SWR_GS_STATE state = { 0 };
         ctx->api.pfnSwrSetGsState(ctx->swrContext, &state);
         ctx->api.pfnSwrSetGsFunc(ctx->swrContext, NULL);
      }
   }

   // We may need to restore tessellation state
   // This restored state may be however overwritten
   // during shader compilation
   if (ctx->dirty & SWR_NEW_TS) {
      if (ctx->tes != nullptr) {
         ctx->tsState = ctx->tes->ts_state;
         ctx->api.pfnSwrSetTsState(ctx->swrContext, &ctx->tsState);
      } else {
         SWR_TS_STATE state = { 0 };
         ctx->api.pfnSwrSetTsState(ctx->swrContext, &state);
      }
   }

   // Tessellation Evaluation Shader
   // Compile TES first, because TCS is optional
   if (ctx->dirty & (SWR_NEW_GS |
                     SWR_NEW_VS |
                     SWR_NEW_TCS |
                     SWR_NEW_TES |
                     SWR_NEW_SAMPLER |
                     SWR_NEW_SAMPLER_VIEW)) {
      if (ctx->tes) {
         swr_jit_tes_key key;
         swr_generate_tes_key(key, ctx, ctx->tes);

         auto search = ctx->tes->map.find(key);
         PFN_TES_FUNC func;
         if (search != ctx->tes->map.end()) {
            func = search->second->shader;
         } else {
            func = swr_compile_tes(ctx, key);
         }

         ctx->api.pfnSwrSetDsFunc(ctx->swrContext, func);

         /* JIT sampler state */
         if (ctx->dirty & SWR_NEW_SAMPLER) {
            swr_update_sampler_state(ctx,
                                     PIPE_SHADER_TESS_EVAL,
                                     key.nr_samplers,
                                     ctx->swrDC.samplersTES);
         }

         /* JIT sampler view state */
         if (ctx->dirty & (SWR_NEW_SAMPLER_VIEW | SWR_NEW_FRAMEBUFFER)) {
            swr_update_texture_state(ctx,
                                     PIPE_SHADER_TESS_EVAL,
                                     key.nr_sampler_views,
                                     ctx->swrDC.texturesTES);
         }

         // Update tessellation state in case it's been updated
         ctx->api.pfnSwrSetTsState(ctx->swrContext, &ctx->tsState);
      } else {
         ctx->api.pfnSwrSetDsFunc(ctx->swrContext, NULL);
      }
   }

   /* Tessellation Control Shader */
   if (ctx->dirty & (SWR_NEW_GS |
                     SWR_NEW_VS |
                     SWR_NEW_TCS |
                     SWR_NEW_TES |
                     SWR_NEW_SAMPLER |
                     SWR_NEW_SAMPLER_VIEW)) {
      if (ctx->tcs) {
         ctx->tcs->vertices_per_patch = ctx->patch_vertices;

         swr_jit_tcs_key key;
         swr_generate_tcs_key(key, ctx, ctx->tcs);

         auto search = ctx->tcs->map.find(key);
         PFN_TCS_FUNC func;
         if (search != ctx->tcs->map.end()) {
            func = search->second->shader;
         } else {
            func = swr_compile_tcs(ctx, key);
         }

         ctx->api.pfnSwrSetHsFunc(ctx->swrContext, func);

         /* JIT sampler state */
         if (ctx->dirty & SWR_NEW_SAMPLER) {
            swr_update_sampler_state(ctx,
                                     PIPE_SHADER_TESS_CTRL,
                                     key.nr_samplers,
                                     ctx->swrDC.samplersTCS);
         }

         /* JIT sampler view state */
         if (ctx->dirty & (SWR_NEW_SAMPLER_VIEW | SWR_NEW_FRAMEBUFFER)) {
            swr_update_texture_state(ctx,
                                     PIPE_SHADER_TESS_CTRL,
                                     key.nr_sampler_views,
                                     ctx->swrDC.texturesTCS);
         }

         // Update tessellation state in case it's been updated
         ctx->api.pfnSwrSetTsState(ctx->swrContext, &ctx->tsState);
      } else {
         ctx->api.pfnSwrSetHsFunc(ctx->swrContext, NULL);
      }
   }

   /* VertexShader */
   if (ctx->dirty
       & (SWR_NEW_VS | SWR_NEW_RASTERIZER | // for clip planes
          SWR_NEW_SAMPLER | SWR_NEW_SAMPLER_VIEW | SWR_NEW_FRAMEBUFFER)) {
      swr_jit_vs_key key;
      swr_generate_vs_key(key, ctx, ctx->vs);
      auto search = ctx->vs->map.find(key);
      PFN_VERTEX_FUNC func;
      if (search != ctx->vs->map.end()) {
         func = search->second->shader;
      } else {
         func = swr_compile_vs(ctx, key);
      }
      ctx->api.pfnSwrSetVertexFunc(ctx->swrContext, func);

      /* JIT sampler state */
      if (ctx->dirty & SWR_NEW_SAMPLER) {
         swr_update_sampler_state(
            ctx, PIPE_SHADER_VERTEX, key.nr_samplers, ctx->swrDC.samplersVS);
      }

      /* JIT sampler view state */
      if (ctx->dirty & (SWR_NEW_SAMPLER_VIEW | SWR_NEW_FRAMEBUFFER)) {
         swr_update_texture_state(ctx,
                                  PIPE_SHADER_VERTEX,
                                  key.nr_sampler_views,
                                  ctx->swrDC.texturesVS);
      }
   }

   /* work around the fact that poly stipple also affects lines */
   /* and points, since we rasterize them as triangles, too */
   /* Has to be before fragment shader, since it sets SWR_NEW_FS */
   if (p_draw_info) {
      bool new_prim_is_poly =
         (u_reduced_prim(p_draw_info->mode) == PIPE_PRIM_TRIANGLES) &&
         (ctx->derived.rastState.fillMode == SWR_FILLMODE_SOLID);
      if (new_prim_is_poly != ctx->poly_stipple.prim_is_poly) {
         ctx->dirty |= SWR_NEW_FS;
         ctx->poly_stipple.prim_is_poly = new_prim_is_poly;
      }
   }

   /* FragmentShader */
   if (ctx->dirty & (SWR_NEW_FS |
                     SWR_NEW_VS |
                     SWR_NEW_GS |
                     SWR_NEW_TES |
                     SWR_NEW_TCS |
                     SWR_NEW_RASTERIZER |
                     SWR_NEW_SAMPLER |
                     SWR_NEW_SAMPLER_VIEW |
                     SWR_NEW_FRAMEBUFFER)) {
      swr_jit_fs_key key;
      swr_generate_fs_key(key, ctx, ctx->fs);
      auto search = ctx->fs->map.find(key);
      PFN_PIXEL_KERNEL func;
      if (search != ctx->fs->map.end()) {
         func = search->second->shader;
      } else {
         func = swr_compile_fs(ctx, key);
      }
      SWR_PS_STATE psState = {0};
      psState.pfnPixelShader = func;
      psState.killsPixel = ctx->fs->info.base.uses_kill;
      psState.inputCoverage = SWR_INPUT_COVERAGE_NORMAL;
      psState.writesODepth = ctx->fs->info.base.writes_z;
      psState.usesSourceDepth = ctx->fs->info.base.reads_z;
      psState.shadingRate = SWR_SHADING_RATE_PIXEL;
      psState.renderTargetMask = (1 << ctx->framebuffer.nr_cbufs) - 1;
      psState.posOffset = SWR_PS_POSITION_SAMPLE_NONE;
      uint32_t barycentricsMask = 0;
#if 0
      // when we switch to mesa-master
      if (ctx->fs->info.base.uses_persp_center ||
          ctx->fs->info.base.uses_linear_center)
         barycentricsMask |= SWR_BARYCENTRIC_PER_PIXEL_MASK;
      if (ctx->fs->info.base.uses_persp_centroid ||
          ctx->fs->info.base.uses_linear_centroid)
         barycentricsMask |= SWR_BARYCENTRIC_CENTROID_MASK;
      if (ctx->fs->info.base.uses_persp_sample ||
          ctx->fs->info.base.uses_linear_sample)
         barycentricsMask |= SWR_BARYCENTRIC_PER_SAMPLE_MASK;
#else
      for (unsigned i = 0; i < ctx->fs->info.base.num_inputs; i++) {
         switch (ctx->fs->info.base.input_interpolate_loc[i]) {
         case TGSI_INTERPOLATE_LOC_CENTER:
            barycentricsMask |= SWR_BARYCENTRIC_PER_PIXEL_MASK;
            break;
         case TGSI_INTERPOLATE_LOC_CENTROID:
            barycentricsMask |= SWR_BARYCENTRIC_CENTROID_MASK;
            break;
         case TGSI_INTERPOLATE_LOC_SAMPLE:
            barycentricsMask |= SWR_BARYCENTRIC_PER_SAMPLE_MASK;
            break;
         }
      }
#endif
      psState.barycentricsMask = barycentricsMask;
      psState.usesUAV = false; // XXX
      psState.forceEarlyZ = false;
      ctx->api.pfnSwrSetPixelShaderState(ctx->swrContext, &psState);

      /* JIT sampler state */
      if (ctx->dirty & (SWR_NEW_SAMPLER |
                        SWR_NEW_FS)) {
         swr_update_sampler_state(ctx,
                                  PIPE_SHADER_FRAGMENT,
                                  key.nr_samplers,
                                  ctx->swrDC.samplersFS);
      }

      /* JIT sampler view state */
      if (ctx->dirty & (SWR_NEW_SAMPLER_VIEW |
                        SWR_NEW_FRAMEBUFFER |
                        SWR_NEW_FS)) {
         swr_update_texture_state(ctx,
                                  PIPE_SHADER_FRAGMENT,
                                  key.nr_sampler_views,
                                  ctx->swrDC.texturesFS);
      }
   }


   /* VertexShader Constants */
   if (ctx->dirty & SWR_NEW_VSCONSTANTS) {
      swr_update_constants(ctx, PIPE_SHADER_VERTEX);
   }

   /* FragmentShader Constants */
   if (ctx->dirty & SWR_NEW_FSCONSTANTS) {
      swr_update_constants(ctx, PIPE_SHADER_FRAGMENT);
   }

   /* GeometryShader Constants */
   if (ctx->dirty & SWR_NEW_GSCONSTANTS) {
      swr_update_constants(ctx, PIPE_SHADER_GEOMETRY);
   }

   /* Tessellation Control Shader Constants */
   if (ctx->dirty & SWR_NEW_TCSCONSTANTS) {
      swr_update_constants(ctx, PIPE_SHADER_TESS_CTRL);
   }

   /* Tessellation Evaluation Shader Constants */
   if (ctx->dirty & SWR_NEW_TESCONSTANTS) {
      swr_update_constants(ctx, PIPE_SHADER_TESS_EVAL);
   }

   /* Depth/stencil state */
   if (ctx->dirty & (SWR_NEW_DEPTH_STENCIL_ALPHA | SWR_NEW_FRAMEBUFFER)) {
      struct pipe_depth_stencil_alpha_state *depth = ctx->depth_stencil;
      struct pipe_stencil_state *stencil = depth->stencil;
      SWR_DEPTH_STENCIL_STATE depthStencilState = {{0}};
      SWR_DEPTH_BOUNDS_STATE depthBoundsState = {0};

      /* XXX, incomplete.  Need to flesh out stencil & alpha test state
      struct pipe_stencil_state *front_stencil =
      ctx->depth_stencil.stencil[0];
      struct pipe_stencil_state *back_stencil = ctx->depth_stencil.stencil[1];
      */
      if (stencil[0].enabled) {
         depthStencilState.stencilWriteEnable = 1;
         depthStencilState.stencilTestEnable = 1;
         depthStencilState.stencilTestFunc =
            swr_convert_depth_func(stencil[0].func);

         depthStencilState.stencilPassDepthPassOp =
            swr_convert_stencil_op(stencil[0].zpass_op);
         depthStencilState.stencilPassDepthFailOp =
            swr_convert_stencil_op(stencil[0].zfail_op);
         depthStencilState.stencilFailOp =
            swr_convert_stencil_op(stencil[0].fail_op);
         depthStencilState.stencilWriteMask = stencil[0].writemask;
         depthStencilState.stencilTestMask = stencil[0].valuemask;
         depthStencilState.stencilRefValue = ctx->stencil_ref.ref_value[0];
      }
      if (stencil[1].enabled) {
         depthStencilState.doubleSidedStencilTestEnable = 1;

         depthStencilState.backfaceStencilTestFunc =
            swr_convert_depth_func(stencil[1].func);

         depthStencilState.backfaceStencilPassDepthPassOp =
            swr_convert_stencil_op(stencil[1].zpass_op);
         depthStencilState.backfaceStencilPassDepthFailOp =
            swr_convert_stencil_op(stencil[1].zfail_op);
         depthStencilState.backfaceStencilFailOp =
            swr_convert_stencil_op(stencil[1].fail_op);
         depthStencilState.backfaceStencilWriteMask = stencil[1].writemask;
         depthStencilState.backfaceStencilTestMask = stencil[1].valuemask;

         depthStencilState.backfaceStencilRefValue =
            ctx->stencil_ref.ref_value[1];
      }

      depthStencilState.depthTestEnable = depth->depth_enabled;
      depthStencilState.depthTestFunc = swr_convert_depth_func(depth->depth_func);
      depthStencilState.depthWriteEnable = depth->depth_writemask;
      ctx->api.pfnSwrSetDepthStencilState(ctx->swrContext, &depthStencilState);

      depthBoundsState.depthBoundsTestEnable = depth->depth_bounds_test;
      depthBoundsState.depthBoundsTestMinValue = depth->depth_bounds_min;
      depthBoundsState.depthBoundsTestMaxValue = depth->depth_bounds_max;
      ctx->api.pfnSwrSetDepthBoundsState(ctx->swrContext, &depthBoundsState);
   }

   /* Blend State */
   if (ctx->dirty & (SWR_NEW_BLEND |
                     SWR_NEW_RASTERIZER |
                     SWR_NEW_FRAMEBUFFER |
                     SWR_NEW_DEPTH_STENCIL_ALPHA)) {
      struct pipe_framebuffer_state *fb = &ctx->framebuffer;

      SWR_BLEND_STATE blendState;
      memcpy(&blendState, &ctx->blend->blendState, sizeof(blendState));
      blendState.constantColor[0] = ctx->blend_color.color[0];
      blendState.constantColor[1] = ctx->blend_color.color[1];
      blendState.constantColor[2] = ctx->blend_color.color[2];
      blendState.constantColor[3] = ctx->blend_color.color[3];
      blendState.alphaTestReference =
         *((uint32_t*)&ctx->depth_stencil->alpha_ref_value);

      blendState.sampleMask = ctx->sample_mask;
      blendState.sampleCount = GetSampleCount(fb->samples);

      /* If there are no color buffers bound, disable writes on RT0
       * and skip loop */
      if (fb->nr_cbufs == 0) {
         blendState.renderTarget[0].writeDisableRed = 1;
         blendState.renderTarget[0].writeDisableGreen = 1;
         blendState.renderTarget[0].writeDisableBlue = 1;
         blendState.renderTarget[0].writeDisableAlpha = 1;
         ctx->api.pfnSwrSetBlendFunc(ctx->swrContext, 0, NULL);
      }
      else
         for (int target = 0;
               target < std::min(SWR_NUM_RENDERTARGETS,
                                 PIPE_MAX_COLOR_BUFS);
               target++) {
            if (!fb->cbufs[target])
               continue;

            struct swr_resource *colorBuffer =
               swr_resource(fb->cbufs[target]->texture);

            BLEND_COMPILE_STATE compileState;
            memset(&compileState, 0, sizeof(compileState));
            compileState.format = colorBuffer->swr.format;
            memcpy(&compileState.blendState,
                   &ctx->blend->compileState[target],
                   sizeof(compileState.blendState));

            const SWR_FORMAT_INFO& info = GetFormatInfo(compileState.format);
            if (compileState.blendState.logicOpEnable &&
                ((info.type[0] == SWR_TYPE_FLOAT) || info.isSRGB)) {
               compileState.blendState.logicOpEnable = false;
            }

            if (info.type[0] == SWR_TYPE_SINT || info.type[0] == SWR_TYPE_UINT)
               compileState.blendState.blendEnable = false;

            if (compileState.blendState.blendEnable == false &&
                compileState.blendState.logicOpEnable == false &&
                ctx->depth_stencil->alpha_enabled == 0) {
               ctx->api.pfnSwrSetBlendFunc(ctx->swrContext, target, NULL);
               continue;
            }

            compileState.desc.alphaTestEnable =
               ctx->depth_stencil->alpha_enabled;
            compileState.desc.independentAlphaBlendEnable =
               (compileState.blendState.sourceBlendFactor !=
                compileState.blendState.sourceAlphaBlendFactor) ||
               (compileState.blendState.destBlendFactor !=
                compileState.blendState.destAlphaBlendFactor) ||
               (compileState.blendState.colorBlendFunc !=
                compileState.blendState.alphaBlendFunc);
            compileState.desc.alphaToCoverageEnable =
               ctx->blend->pipe.alpha_to_coverage;
            compileState.desc.sampleMaskEnable = (blendState.sampleMask != 0);
            compileState.desc.numSamples = fb->samples;

            compileState.alphaTestFunction =
               swr_convert_depth_func(ctx->depth_stencil->alpha_func);
            compileState.alphaTestFormat = ALPHA_TEST_FLOAT32; // xxx

            compileState.Canonicalize();

            PFN_BLEND_JIT_FUNC func = NULL;
            auto search = ctx->blendJIT->find(compileState);
            if (search != ctx->blendJIT->end()) {
               func = search->second;
            } else {
               HANDLE hJitMgr = screen->hJitMgr;
               func = JitCompileBlend(hJitMgr, compileState);
               debug_printf("BLEND shader %p\n", func);
               assert(func && "Error: BlendShader = NULL");

               ctx->blendJIT->insert(std::make_pair(compileState, func));
            }
            ctx->api.pfnSwrSetBlendFunc(ctx->swrContext, target, func);
         }

      ctx->api.pfnSwrSetBlendState(ctx->swrContext, &blendState);
   }

   if (ctx->dirty & SWR_NEW_STIPPLE) {
      swr_update_poly_stipple(ctx);
   }

   if (ctx->dirty & (SWR_NEW_VS | SWR_NEW_TCS | SWR_NEW_TES | SWR_NEW_SO | SWR_NEW_RASTERIZER)) {
      ctx->vs->soState.rasterizerDisable =
         ctx->rasterizer->rasterizer_discard;
      ctx->api.pfnSwrSetSoState(ctx->swrContext, &ctx->vs->soState);

      pipe_stream_output_info *stream_output = &ctx->vs->pipe.stream_output;

      for (uint32_t i = 0; i < MAX_SO_STREAMS; i++) {
         SWR_STREAMOUT_BUFFER buffer = {0};
         if (ctx->so_targets[i]) {
             buffer.enable = true;
             buffer.pBuffer =
                (gfxptr_t)(swr_resource_data(ctx->so_targets[i]->buffer) +
                             ctx->so_targets[i]->buffer_offset);
             buffer.bufferSize = ctx->so_targets[i]->buffer_size >> 2;
             buffer.pitch = stream_output->stride[i];
             buffer.streamOffset = 0;
	 }

         ctx->api.pfnSwrSetSoBuffers(ctx->swrContext, &buffer, i);
      }
   }


   if (ctx->dirty & (SWR_NEW_CLIP | SWR_NEW_RASTERIZER | SWR_NEW_VS)) {
      // shader exporting clip distances overrides all user clip planes
      if (ctx->rasterizer->clip_plane_enable &&
          !swr_get_last_fe(ctx)->num_written_clipdistance)
      {
         swr_draw_context *pDC = &ctx->swrDC;
         memcpy(pDC->userClipPlanes,
                ctx->clip.ucp,
                sizeof(pDC->userClipPlanes));
      }
   }

   // set up backend state
   SWR_BACKEND_STATE backendState = {0};
   if (ctx->gs) {
      backendState.numAttributes = ctx->gs->info.base.num_outputs - 1;
   } else
   if (ctx->tes) {
      backendState.numAttributes = ctx->tes->info.base.num_outputs - 1;
      // no case for TCS, because if TCS is active, TES must be active
      // as well - pipeline stages after tessellation does not support patches
   }  else {
      backendState.numAttributes = ctx->vs->info.base.num_outputs - 1;
      if (ctx->fs->info.base.uses_primid) {
         backendState.numAttributes++;
         backendState.swizzleEnable = true;
         for (unsigned i = 0; i < sizeof(backendState.numComponents); i++) {
            backendState.swizzleMap[i].sourceAttrib = i;
         }
         backendState.swizzleMap[ctx->vs->info.base.num_outputs - 1].constantSource =
            SWR_CONSTANT_SOURCE_PRIM_ID;
         backendState.swizzleMap[ctx->vs->info.base.num_outputs - 1].componentOverrideMask = 1;
      }
   }
   if (ctx->rasterizer->sprite_coord_enable)
      backendState.numAttributes++;

   backendState.numAttributes = std::min((size_t)backendState.numAttributes,
                                         sizeof(backendState.numComponents));
   for (unsigned i = 0; i < backendState.numAttributes; i++)
      backendState.numComponents[i] = 4;
   backendState.constantInterpolationMask = ctx->fs->constantMask |
      (ctx->rasterizer->flatshade ? ctx->fs->flatConstantMask : 0);
   backendState.pointSpriteTexCoordMask = ctx->fs->pointSpriteMask;

   struct tgsi_shader_info *pLastFE = swr_get_last_fe(ctx);

   backendState.readRenderTargetArrayIndex = pLastFE->writes_layer;
   backendState.readViewportArrayIndex = pLastFE->writes_viewport_index;
   backendState.vertexAttribOffset = VERTEX_ATTRIB_START_SLOT; // TODO: optimize

   backendState.clipDistanceMask =
      pLastFE->num_written_clipdistance ?
      pLastFE->clipdist_writemask & ctx->rasterizer->clip_plane_enable :
      ctx->rasterizer->clip_plane_enable;

   backendState.cullDistanceMask =
      pLastFE->culldist_writemask << pLastFE->num_written_clipdistance;

   // Assume old layout of SGV, POSITION, CLIPCULL, ATTRIB
   backendState.vertexClipCullOffset = backendState.vertexAttribOffset - 2;

   ctx->api.pfnSwrSetBackendState(ctx->swrContext, &backendState);

   /* Ensure that any in-progress attachment change StoreTiles finish */
   if (swr_is_fence_pending(screen->flush_fence))
      swr_fence_finish(pipe->screen, NULL, screen->flush_fence, 0);

   /* Finally, update the in-use status of all resources involved in draw */
   swr_update_resource_status(pipe, p_draw_info);

   ctx->dirty = post_update_dirty_flags;
}


static struct pipe_stream_output_target *
swr_create_so_target(struct pipe_context *pipe,
                     struct pipe_resource *buffer,
                     unsigned buffer_offset,
                     unsigned buffer_size)
{
   struct pipe_stream_output_target *target;

   target = CALLOC_STRUCT(pipe_stream_output_target);
   if (!target)
      return NULL;

   target->context = pipe;
   target->reference.count = 1;
   pipe_resource_reference(&target->buffer, buffer);
   target->buffer_offset = buffer_offset;
   target->buffer_size = buffer_size;
   return target;
}

static void
swr_destroy_so_target(struct pipe_context *pipe,
                      struct pipe_stream_output_target *target)
{
   pipe_resource_reference(&target->buffer, NULL);
   FREE(target);
}

static void
swr_set_so_targets(struct pipe_context *pipe,
                   unsigned num_targets,
                   struct pipe_stream_output_target **targets,
                   const unsigned *offsets)
{
   struct swr_context *swr = swr_context(pipe);
   uint32_t i;

   assert(num_targets <= MAX_SO_STREAMS);

   for (i = 0; i < num_targets; i++) {
      pipe_so_target_reference(
         (struct pipe_stream_output_target **)&swr->so_targets[i],
         targets[i]);
   }

   for (/* fall-through */; i < swr->num_so_targets; i++) {
      pipe_so_target_reference(
         (struct pipe_stream_output_target **)&swr->so_targets[i], NULL);
   }

   swr->num_so_targets = num_targets;
   swr->swrDC.soPrims = &swr->so_primCounter;

   swr->dirty |= SWR_NEW_SO;
}

static void
swr_set_patch_vertices(struct pipe_context *pipe, uint8_t patch_vertices)
{
   struct swr_context *swr = swr_context(pipe);

   swr->patch_vertices = patch_vertices;
}


void
swr_state_init(struct pipe_context *pipe)
{
   pipe->create_blend_state = swr_create_blend_state;
   pipe->bind_blend_state = swr_bind_blend_state;
   pipe->delete_blend_state = swr_delete_blend_state;

   pipe->create_depth_stencil_alpha_state = swr_create_depth_stencil_state;
   pipe->bind_depth_stencil_alpha_state = swr_bind_depth_stencil_state;
   pipe->delete_depth_stencil_alpha_state = swr_delete_depth_stencil_state;

   pipe->create_rasterizer_state = swr_create_rasterizer_state;
   pipe->bind_rasterizer_state = swr_bind_rasterizer_state;
   pipe->delete_rasterizer_state = swr_delete_rasterizer_state;

   pipe->create_sampler_state = swr_create_sampler_state;
   pipe->bind_sampler_states = swr_bind_sampler_states;
   pipe->delete_sampler_state = swr_delete_sampler_state;

   pipe->create_sampler_view = swr_create_sampler_view;
   pipe->set_sampler_views = swr_set_sampler_views;
   pipe->sampler_view_destroy = swr_sampler_view_destroy;

   pipe->create_vs_state = swr_create_vs_state;
   pipe->bind_vs_state = swr_bind_vs_state;
   pipe->delete_vs_state = swr_delete_vs_state;

   pipe->create_fs_state = swr_create_fs_state;
   pipe->bind_fs_state = swr_bind_fs_state;
   pipe->delete_fs_state = swr_delete_fs_state;

   pipe->create_gs_state = swr_create_gs_state;
   pipe->bind_gs_state = swr_bind_gs_state;
   pipe->delete_gs_state = swr_delete_gs_state;

   pipe->create_tcs_state = swr_create_tcs_state;
   pipe->bind_tcs_state = swr_bind_tcs_state;
   pipe->delete_tcs_state = swr_delete_tcs_state;

   pipe->create_tes_state = swr_create_tes_state;
   pipe->bind_tes_state = swr_bind_tes_state;
   pipe->delete_tes_state = swr_delete_tes_state;

   pipe->set_constant_buffer = swr_set_constant_buffer;

   pipe->create_vertex_elements_state = swr_create_vertex_elements_state;
   pipe->bind_vertex_elements_state = swr_bind_vertex_elements_state;
   pipe->delete_vertex_elements_state = swr_delete_vertex_elements_state;

   pipe->set_vertex_buffers = swr_set_vertex_buffers;

   pipe->set_polygon_stipple = swr_set_polygon_stipple;
   pipe->set_clip_state = swr_set_clip_state;
   pipe->set_scissor_states = swr_set_scissor_states;
   pipe->set_viewport_states = swr_set_viewport_states;

   pipe->set_framebuffer_state = swr_set_framebuffer_state;

   pipe->set_blend_color = swr_set_blend_color;
   pipe->set_stencil_ref = swr_set_stencil_ref;

   pipe->set_sample_mask = swr_set_sample_mask;
   pipe->get_sample_position = swr_get_sample_position;

   pipe->create_stream_output_target = swr_create_so_target;
   pipe->stream_output_target_destroy = swr_destroy_so_target;
   pipe->set_stream_output_targets = swr_set_so_targets;

   pipe->set_patch_vertices = swr_set_patch_vertices;
}
