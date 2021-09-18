/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "d3d12_blit.h"
#include "d3d12_context.h"
#include "d3d12_compiler.h"
#include "d3d12_debug.h"
#include "d3d12_fence.h"
#include "d3d12_format.h"
#include "d3d12_query.h"
#include "d3d12_resource.h"
#include "d3d12_root_signature.h"
#include "d3d12_screen.h"
#include "d3d12_surface.h"

#include "util/u_blitter.h"
#include "util/u_dual_blend.h"
#include "util/u_framebuffer.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "util/u_pstipple.h"
#include "util/u_dl.h"
#include "nir_to_dxil.h"

#include "D3D12ResourceState.h"

#include <dxguids/dxguids.h>

extern "C" {
#include "indices/u_primconvert.h"
}

#include <string.h>

static void
d3d12_context_destroy(struct pipe_context *pctx)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   if (ctx->validation_tools)
      d3d12_validator_destroy(ctx->validation_tools);

   if (ctx->timestamp_query)
      pctx->destroy_query(pctx, ctx->timestamp_query);

   util_blitter_destroy(ctx->blitter);
   d3d12_end_batch(ctx, d3d12_current_batch(ctx));
   for (unsigned i = 0; i < ARRAY_SIZE(ctx->batches); ++i)
      d3d12_destroy_batch(ctx, &ctx->batches[i]);
   ctx->cmdlist->Release();
   ctx->cmdqueue_fence->Release();
   d3d12_descriptor_pool_free(ctx->sampler_pool);
   util_primconvert_destroy(ctx->primconvert);
   slab_destroy_child(&ctx->transfer_pool);
   d3d12_gs_variant_cache_destroy(ctx);
   d3d12_gfx_pipeline_state_cache_destroy(ctx);
   d3d12_root_signature_cache_destroy(ctx);

   u_suballocator_destroy(&ctx->query_allocator);

   if (pctx->stream_uploader)
      u_upload_destroy(pctx->stream_uploader);
   if (pctx->const_uploader)
      u_upload_destroy(pctx->const_uploader);

   delete ctx->resource_state_manager;

   FREE(ctx);
}

static void *
d3d12_create_vertex_elements_state(struct pipe_context *pctx,
                                   unsigned num_elements,
                                   const struct pipe_vertex_element *elements)
{
   struct d3d12_vertex_elements_state *cso = CALLOC_STRUCT(d3d12_vertex_elements_state);
   if (!cso)
      return NULL;

   for (unsigned i = 0; i < num_elements; ++i) {
      cso->elements[i].SemanticName = "TEXCOORD";
      cso->elements[i].SemanticIndex = i;

      enum pipe_format format_helper =
         d3d12_emulated_vtx_format((enum pipe_format)elements[i].src_format);
      bool needs_emulation = format_helper != elements[i].src_format;
      cso->needs_format_emulation |= needs_emulation;
      cso->format_conversion[i] =
         needs_emulation ? (enum pipe_format)elements[i].src_format : PIPE_FORMAT_NONE;

      cso->elements[i].Format = d3d12_get_format(format_helper);
      assert(cso->elements[i].Format != DXGI_FORMAT_UNKNOWN);
      cso->elements[i].InputSlot = elements[i].vertex_buffer_index;
      cso->elements[i].AlignedByteOffset = elements[i].src_offset;

      if (elements[i].instance_divisor) {
         cso->elements[i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
         cso->elements[i].InstanceDataStepRate = elements[i].instance_divisor;
      } else {
         cso->elements[i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
         cso->elements[i].InstanceDataStepRate = 0;
      }
   }

   cso->num_elements = num_elements;
   return cso;
}

static void
d3d12_bind_vertex_elements_state(struct pipe_context *pctx,
                                 void *ve)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   ctx->gfx_pipeline_state.ves = (struct d3d12_vertex_elements_state *)ve;
   ctx->state_dirty |= D3D12_DIRTY_VERTEX_ELEMENTS;
}

static void
d3d12_delete_vertex_elements_state(struct pipe_context *pctx,
                                   void *ve)
{
   FREE(ve);
}

static D3D12_BLEND
blend_factor_rgb(enum pipe_blendfactor factor)
{
   switch (factor) {
   case PIPE_BLENDFACTOR_ZERO: return D3D12_BLEND_ZERO;
   case PIPE_BLENDFACTOR_ONE: return D3D12_BLEND_ONE;
   case PIPE_BLENDFACTOR_SRC_COLOR: return D3D12_BLEND_SRC_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
   case PIPE_BLENDFACTOR_DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
   case PIPE_BLENDFACTOR_DST_COLOR: return D3D12_BLEND_DEST_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE: return D3D12_BLEND_SRC_ALPHA_SAT;
   case PIPE_BLENDFACTOR_CONST_COLOR: return D3D12_BLEND_BLEND_FACTOR;
   case PIPE_BLENDFACTOR_SRC1_COLOR: return D3D12_BLEND_SRC1_COLOR;
   case PIPE_BLENDFACTOR_SRC1_ALPHA: return D3D12_BLEND_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_COLOR: return D3D12_BLEND_INV_DEST_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_COLOR: return D3D12_BLEND_INV_BLEND_FACTOR;
   case PIPE_BLENDFACTOR_INV_SRC1_COLOR: return D3D12_BLEND_INV_SRC1_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA: return D3D12_BLEND_INV_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_CONST_ALPHA: return D3D12_BLEND_BLEND_FACTOR; /* Doesn't exist in D3D12 */
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA: return D3D12_BLEND_INV_BLEND_FACTOR; /* Doesn't exist in D3D12 */
   }
   unreachable("unexpected blend factor");
}

static D3D12_BLEND
blend_factor_alpha(enum pipe_blendfactor factor)
{
   switch (factor) {
   case PIPE_BLENDFACTOR_ZERO: return D3D12_BLEND_ZERO;
   case PIPE_BLENDFACTOR_ONE: return D3D12_BLEND_ONE;
   case PIPE_BLENDFACTOR_SRC_COLOR:
   case PIPE_BLENDFACTOR_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
   case PIPE_BLENDFACTOR_DST_COLOR:
   case PIPE_BLENDFACTOR_DST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE: return D3D12_BLEND_SRC_ALPHA_SAT;
   case PIPE_BLENDFACTOR_CONST_COLOR:
   case PIPE_BLENDFACTOR_CONST_ALPHA: return D3D12_BLEND_BLEND_FACTOR;
   case PIPE_BLENDFACTOR_SRC1_COLOR:
   case PIPE_BLENDFACTOR_SRC1_ALPHA: return D3D12_BLEND_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_INV_SRC_COLOR:
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_COLOR:
   case PIPE_BLENDFACTOR_INV_DST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
   case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA: return D3D12_BLEND_INV_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_INV_CONST_COLOR:
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA: return D3D12_BLEND_INV_BLEND_FACTOR;
   }
   unreachable("unexpected blend factor");
}

static unsigned
need_blend_factor_rgb(enum pipe_blendfactor factor)
{
   switch (factor) {
   case PIPE_BLENDFACTOR_CONST_COLOR:
   case PIPE_BLENDFACTOR_INV_CONST_COLOR:
      return D3D12_BLEND_FACTOR_COLOR;
   case PIPE_BLENDFACTOR_CONST_ALPHA:
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
      return D3D12_BLEND_FACTOR_ALPHA;

   default:
      return D3D12_BLEND_FACTOR_NONE;
   }
}

static unsigned
need_blend_factor_alpha(enum pipe_blendfactor factor)
{
   switch (factor) {
   case PIPE_BLENDFACTOR_CONST_COLOR:
   case PIPE_BLENDFACTOR_INV_CONST_COLOR:
   case PIPE_BLENDFACTOR_CONST_ALPHA:
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
      return D3D12_BLEND_FACTOR_ANY;

   default:
      return D3D12_BLEND_FACTOR_NONE;
   }
}

static D3D12_BLEND_OP
blend_op(enum pipe_blend_func func)
{
   switch (func) {
   case PIPE_BLEND_ADD: return D3D12_BLEND_OP_ADD;
   case PIPE_BLEND_SUBTRACT: return D3D12_BLEND_OP_SUBTRACT;
   case PIPE_BLEND_REVERSE_SUBTRACT: return D3D12_BLEND_OP_REV_SUBTRACT;
   case PIPE_BLEND_MIN: return D3D12_BLEND_OP_MIN;
   case PIPE_BLEND_MAX: return D3D12_BLEND_OP_MAX;
   }
   unreachable("unexpected blend function");
}

static D3D12_COMPARISON_FUNC
compare_op(enum pipe_compare_func op)
{
   switch (op) {
      case PIPE_FUNC_NEVER: return D3D12_COMPARISON_FUNC_NEVER;
      case PIPE_FUNC_LESS: return D3D12_COMPARISON_FUNC_LESS;
      case PIPE_FUNC_EQUAL: return D3D12_COMPARISON_FUNC_EQUAL;
      case PIPE_FUNC_LEQUAL: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
      case PIPE_FUNC_GREATER: return D3D12_COMPARISON_FUNC_GREATER;
      case PIPE_FUNC_NOTEQUAL: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
      case PIPE_FUNC_GEQUAL: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
      case PIPE_FUNC_ALWAYS: return D3D12_COMPARISON_FUNC_ALWAYS;
   }
   unreachable("unexpected compare");
}

static D3D12_LOGIC_OP
logic_op(enum pipe_logicop func)
{
   switch (func) {
   case PIPE_LOGICOP_CLEAR: return D3D12_LOGIC_OP_CLEAR;
   case PIPE_LOGICOP_NOR: return D3D12_LOGIC_OP_NOR;
   case PIPE_LOGICOP_AND_INVERTED: return D3D12_LOGIC_OP_AND_INVERTED;
   case PIPE_LOGICOP_COPY_INVERTED: return D3D12_LOGIC_OP_COPY_INVERTED;
   case PIPE_LOGICOP_AND_REVERSE: return D3D12_LOGIC_OP_AND_REVERSE;
   case PIPE_LOGICOP_INVERT: return D3D12_LOGIC_OP_INVERT;
   case PIPE_LOGICOP_XOR: return D3D12_LOGIC_OP_XOR;
   case PIPE_LOGICOP_NAND: return D3D12_LOGIC_OP_NAND;
   case PIPE_LOGICOP_AND: return D3D12_LOGIC_OP_AND;
   case PIPE_LOGICOP_EQUIV: return D3D12_LOGIC_OP_EQUIV;
   case PIPE_LOGICOP_NOOP: return D3D12_LOGIC_OP_NOOP;
   case PIPE_LOGICOP_OR_INVERTED: return D3D12_LOGIC_OP_OR_INVERTED;
   case PIPE_LOGICOP_COPY: return D3D12_LOGIC_OP_COPY;
   case PIPE_LOGICOP_OR_REVERSE: return D3D12_LOGIC_OP_OR_REVERSE;
   case PIPE_LOGICOP_OR: return D3D12_LOGIC_OP_OR;
   case PIPE_LOGICOP_SET: return D3D12_LOGIC_OP_SET;
   }
   unreachable("unexpected logicop function");
}

static UINT8
color_write_mask(unsigned colormask)
{
   UINT8 mask = 0;

   if (colormask & PIPE_MASK_R)
      mask |= D3D12_COLOR_WRITE_ENABLE_RED;
   if (colormask & PIPE_MASK_G)
      mask |= D3D12_COLOR_WRITE_ENABLE_GREEN;
   if (colormask & PIPE_MASK_B)
      mask |= D3D12_COLOR_WRITE_ENABLE_BLUE;
   if (colormask & PIPE_MASK_A)
      mask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;

   return mask;
}

static void *
d3d12_create_blend_state(struct pipe_context *pctx,
                         const struct pipe_blend_state *blend_state)
{
   struct d3d12_blend_state *state = CALLOC_STRUCT(d3d12_blend_state);
   if (!state)
      return NULL;

   if (blend_state->logicop_enable) {
      state->desc.RenderTarget[0].LogicOpEnable = TRUE;
      state->desc.RenderTarget[0].LogicOp = logic_op((pipe_logicop) blend_state->logicop_func);
   }

   /* TODO Dithering */

   state->desc.AlphaToCoverageEnable = blend_state->alpha_to_coverage;

   int num_targets = 1;
   if (blend_state->independent_blend_enable) {
      state->desc.IndependentBlendEnable = TRUE;
      num_targets = PIPE_MAX_COLOR_BUFS;
   }

   for (int i = 0; i < num_targets; ++i) {
      const struct pipe_rt_blend_state *rt = blend_state->rt + i;

      if (rt->blend_enable) {
         state->desc.RenderTarget[i].BlendEnable = TRUE;
         state->desc.RenderTarget[i].SrcBlend = blend_factor_rgb((pipe_blendfactor) rt->rgb_src_factor);
         state->desc.RenderTarget[i].DestBlend = blend_factor_rgb((pipe_blendfactor) rt->rgb_dst_factor);
         state->desc.RenderTarget[i].BlendOp = blend_op((pipe_blend_func) rt->rgb_func);
         state->desc.RenderTarget[i].SrcBlendAlpha = blend_factor_alpha((pipe_blendfactor) rt->alpha_src_factor);
         state->desc.RenderTarget[i].DestBlendAlpha = blend_factor_alpha((pipe_blendfactor) rt->alpha_dst_factor);
         state->desc.RenderTarget[i].BlendOpAlpha = blend_op((pipe_blend_func) rt->alpha_func);

         state->blend_factor_flags |= need_blend_factor_rgb((pipe_blendfactor) rt->rgb_src_factor);
         state->blend_factor_flags |= need_blend_factor_rgb((pipe_blendfactor) rt->rgb_dst_factor);
         state->blend_factor_flags |= need_blend_factor_alpha((pipe_blendfactor) rt->alpha_src_factor);
         state->blend_factor_flags |= need_blend_factor_alpha((pipe_blendfactor) rt->alpha_dst_factor);

         if (state->blend_factor_flags == (D3D12_BLEND_FACTOR_COLOR | D3D12_BLEND_FACTOR_ALPHA) &&
             (d3d12_debug & D3D12_DEBUG_VERBOSE)) {
            /* We can't set a blend factor for both constant color and constant alpha */
            debug_printf("D3D12: unsupported blend factors combination (const color and const alpha)\n");
         }

         if (util_blend_state_is_dual(blend_state, i))
            state->is_dual_src = true;
      }

      state->desc.RenderTarget[i].RenderTargetWriteMask = color_write_mask(rt->colormask);
   }

   return state;
}

static void
d3d12_bind_blend_state(struct pipe_context *pctx, void *blend_state)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   struct d3d12_blend_state *new_state = (struct d3d12_blend_state *) blend_state;
   struct d3d12_blend_state *old_state = ctx->gfx_pipeline_state.blend;

   ctx->gfx_pipeline_state.blend = new_state;
   ctx->state_dirty |= D3D12_DIRTY_BLEND;
   if (new_state == NULL || old_state == NULL ||
       new_state->blend_factor_flags != old_state->blend_factor_flags)
      ctx->state_dirty |= D3D12_DIRTY_BLEND_COLOR;
}

static void
d3d12_delete_blend_state(struct pipe_context *pctx, void *blend_state)
{
   d3d12_gfx_pipeline_state_cache_invalidate(d3d12_context(pctx), blend_state);
   FREE(blend_state);
}

static D3D12_STENCIL_OP
stencil_op(enum pipe_stencil_op op)
{
   switch (op) {
   case PIPE_STENCIL_OP_KEEP: return D3D12_STENCIL_OP_KEEP;
   case PIPE_STENCIL_OP_ZERO: return D3D12_STENCIL_OP_ZERO;
   case PIPE_STENCIL_OP_REPLACE: return D3D12_STENCIL_OP_REPLACE;
   case PIPE_STENCIL_OP_INCR: return D3D12_STENCIL_OP_INCR_SAT;
   case PIPE_STENCIL_OP_DECR: return D3D12_STENCIL_OP_DECR_SAT;
   case PIPE_STENCIL_OP_INCR_WRAP: return D3D12_STENCIL_OP_INCR;
   case PIPE_STENCIL_OP_DECR_WRAP: return D3D12_STENCIL_OP_DECR;
   case PIPE_STENCIL_OP_INVERT: return D3D12_STENCIL_OP_INVERT;
   }
   unreachable("unexpected op");
}

static D3D12_DEPTH_STENCILOP_DESC
stencil_op_state(const struct pipe_stencil_state *src)
{
   D3D12_DEPTH_STENCILOP_DESC ret;
   ret.StencilFailOp = stencil_op((pipe_stencil_op) src->fail_op);
   ret.StencilPassOp = stencil_op((pipe_stencil_op) src->zpass_op);
   ret.StencilDepthFailOp = stencil_op((pipe_stencil_op) src->zfail_op);
   ret.StencilFunc = compare_op((pipe_compare_func) src->func);
   return ret;
}

static void *
d3d12_create_depth_stencil_alpha_state(struct pipe_context *pctx,
                                       const struct pipe_depth_stencil_alpha_state *depth_stencil_alpha)
{
   struct d3d12_depth_stencil_alpha_state *dsa = CALLOC_STRUCT(d3d12_depth_stencil_alpha_state);
   if (!dsa)
      return NULL;

   if (depth_stencil_alpha->depth_enabled) {
      dsa->desc.DepthEnable = TRUE;
      dsa->desc.DepthFunc = compare_op((pipe_compare_func) depth_stencil_alpha->depth_func);
   }

   /* TODO Add support for GL_depth_bound_tests */
   #if 0
   if (depth_stencil_alpha->depth.bounds_test) {
      dsa->desc.DepthBoundsTestEnable = TRUE;
      dsa->min_depth_bounds = depth_stencil_alpha->depth.bounds_min;
      dsa->max_depth_bounds = depth_stencil_alpha->depth.bounds_max;
   }
   #endif

   if (depth_stencil_alpha->stencil[0].enabled) {
      dsa->desc.StencilEnable = TRUE;
      dsa->desc.FrontFace = stencil_op_state(depth_stencil_alpha->stencil);
   }

   if (depth_stencil_alpha->stencil[1].enabled)
      dsa->desc.BackFace = stencil_op_state(depth_stencil_alpha->stencil + 1);
   else
      dsa->desc.BackFace = dsa->desc.FrontFace;

   dsa->desc.StencilReadMask = depth_stencil_alpha->stencil[0].valuemask; /* FIXME Back face mask */
   dsa->desc.StencilWriteMask = depth_stencil_alpha->stencil[0].writemask; /* FIXME Back face mask */
   dsa->desc.DepthWriteMask = (D3D12_DEPTH_WRITE_MASK) depth_stencil_alpha->depth_writemask;

   return dsa;
}

static void
d3d12_bind_depth_stencil_alpha_state(struct pipe_context *pctx,
                                     void *dsa)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   ctx->gfx_pipeline_state.zsa = (struct d3d12_depth_stencil_alpha_state *) dsa;
   ctx->state_dirty |= D3D12_DIRTY_ZSA;
}

static void
d3d12_delete_depth_stencil_alpha_state(struct pipe_context *pctx,
                                       void *dsa_state)
{
   d3d12_gfx_pipeline_state_cache_invalidate(d3d12_context(pctx), dsa_state);
   FREE(dsa_state);
}

static D3D12_FILL_MODE
fill_mode(unsigned mode)
{
   switch (mode) {
   case PIPE_POLYGON_MODE_FILL:
      return D3D12_FILL_MODE_SOLID;
   case PIPE_POLYGON_MODE_LINE:
      return D3D12_FILL_MODE_WIREFRAME;
   case PIPE_POLYGON_MODE_POINT:
      return D3D12_FILL_MODE_SOLID;

   default:
      unreachable("unsupported fill-mode");
   }
}

static void *
d3d12_create_rasterizer_state(struct pipe_context *pctx,
                              const struct pipe_rasterizer_state *rs_state)
{
   struct d3d12_rasterizer_state *cso = CALLOC_STRUCT(d3d12_rasterizer_state);
   if (!cso)
      return NULL;

   cso->base = *rs_state;

   assert(rs_state->depth_clip_near == rs_state->depth_clip_far);

   switch (rs_state->cull_face) {
   case PIPE_FACE_NONE:
      if (rs_state->fill_front != rs_state->fill_back) {
         cso->base.cull_face = PIPE_FACE_BACK;
         cso->desc.CullMode = D3D12_CULL_MODE_BACK;
         cso->desc.FillMode = fill_mode(rs_state->fill_front);

         /* create a modified CSO for the back-state, so we can draw with
          * either.
          */
         struct pipe_rasterizer_state templ = *rs_state;
         templ.cull_face = PIPE_FACE_FRONT;
         templ.fill_front = rs_state->fill_back;
         cso->twoface_back = d3d12_create_rasterizer_state(pctx, &templ);

         if (!cso->twoface_back) {
            FREE(cso);
            return NULL;
         }
      } else {
         cso->desc.CullMode = D3D12_CULL_MODE_NONE;
         cso->desc.FillMode = fill_mode(rs_state->fill_front);
      }
      break;

   case PIPE_FACE_FRONT:
      cso->desc.CullMode = D3D12_CULL_MODE_FRONT;
      cso->desc.FillMode = fill_mode(rs_state->fill_back);
      break;

   case PIPE_FACE_BACK:
      cso->desc.CullMode = D3D12_CULL_MODE_BACK;
      cso->desc.FillMode = fill_mode(rs_state->fill_front);
      break;

   case PIPE_FACE_FRONT_AND_BACK:
      /* this is wrong, and we shouldn't actually have to support this! */
      cso->desc.CullMode = D3D12_CULL_MODE_NONE;
      cso->desc.FillMode = D3D12_FILL_MODE_SOLID;
      break;

   default:
      unreachable("unsupported cull-mode");
   }

   cso->desc.FrontCounterClockwise = rs_state->front_ccw;
   cso->desc.DepthClipEnable = rs_state->depth_clip_near;
   cso->desc.MultisampleEnable = rs_state->multisample;
   cso->desc.AntialiasedLineEnable = rs_state->line_smooth;
   cso->desc.ForcedSampleCount = 0; // TODO
   cso->desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF; /* Not Implemented */

   return cso;
}

static void
d3d12_bind_rasterizer_state(struct pipe_context *pctx, void *rs_state)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   ctx->gfx_pipeline_state.rast = (struct d3d12_rasterizer_state *)rs_state;
   ctx->state_dirty |= D3D12_DIRTY_RASTERIZER | D3D12_DIRTY_SCISSOR;
}

static void
d3d12_delete_rasterizer_state(struct pipe_context *pctx, void *rs_state)
{
   d3d12_gfx_pipeline_state_cache_invalidate(d3d12_context(pctx), rs_state);
   FREE(rs_state);
}

static D3D12_TEXTURE_ADDRESS_MODE
sampler_address_mode(enum pipe_tex_wrap wrap, enum pipe_tex_filter filter)
{
   switch (wrap) {
   case PIPE_TEX_WRAP_REPEAT: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
   case PIPE_TEX_WRAP_CLAMP: return filter == PIPE_TEX_FILTER_NEAREST ?
                                D3D12_TEXTURE_ADDRESS_MODE_CLAMP :
                                D3D12_TEXTURE_ADDRESS_MODE_BORDER;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
   case PIPE_TEX_WRAP_MIRROR_REPEAT: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
   case PIPE_TEX_WRAP_MIRROR_CLAMP: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE; /* not technically correct, but kinda works */
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE; /* FIXME: Doesn't exist in D3D12 */
   }
   unreachable("unexpected wrap");
}

static D3D12_FILTER
get_filter(const struct pipe_sampler_state *state)
{
   static const D3D12_FILTER lut[16] = {
      D3D12_FILTER_MIN_MAG_MIP_POINT,
      D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR,
      D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT,
      D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR,
      D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT,
      D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
      D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,
      D3D12_FILTER_MIN_MAG_MIP_LINEAR,
      D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT,
      D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR,
      D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT,
      D3D12_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR,
      D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT,
      D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR,
      D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
      D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
   };

   static const D3D12_FILTER anisotropic_lut[2] = {
      D3D12_FILTER_ANISOTROPIC,
      D3D12_FILTER_COMPARISON_ANISOTROPIC,
   };

   if (state->max_anisotropy > 1) {
      return anisotropic_lut[state->compare_mode];
   } else {
      int idx = (state->mag_img_filter << 1) |
                (state->min_img_filter << 2) |
                (state->compare_mode << 3);
      if (state->min_mip_filter != PIPE_TEX_MIPFILTER_NONE)
         idx |= state->min_mip_filter;
      return lut[idx];
   }
}

static void *
d3d12_create_sampler_state(struct pipe_context *pctx,
                           const struct pipe_sampler_state *state)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   struct d3d12_screen *screen = d3d12_screen(pctx->screen);
   struct d3d12_sampler_state *ss;
   D3D12_SAMPLER_DESC desc = {};
   if (!state)
      return NULL;

   ss = CALLOC_STRUCT(d3d12_sampler_state);
   ss->filter = (pipe_tex_filter)state->min_img_filter;
   ss->wrap_r = (pipe_tex_wrap)state->wrap_r;
   ss->wrap_s = (pipe_tex_wrap)state->wrap_s;
   ss->wrap_t = (pipe_tex_wrap)state->wrap_t;
   ss->lod_bias = state->lod_bias;
   ss->min_lod = state->min_lod;
   ss->max_lod = state->max_lod;
   memcpy(ss->border_color, state->border_color.f, sizeof(float) * 4);
   ss->compare_func = (pipe_compare_func)state->compare_func;

   if (state->min_mip_filter < PIPE_TEX_MIPFILTER_NONE) {
      desc.MinLOD = state->min_lod;
      desc.MaxLOD = state->max_lod;
   } else if (state->min_mip_filter == PIPE_TEX_MIPFILTER_NONE) {
      desc.MinLOD = 0;
      desc.MaxLOD = 0;
   } else {
      unreachable("unexpected mip filter");
   }

   if (state->compare_mode == PIPE_TEX_COMPARE_R_TO_TEXTURE) {
      desc.ComparisonFunc = compare_op((pipe_compare_func) state->compare_func);
      desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
   } else if (state->compare_mode == PIPE_TEX_COMPARE_NONE) {
      desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
      desc.Filter = get_filter(state);
   } else
      unreachable("unexpected comparison mode");

   desc.MaxAnisotropy = state->max_anisotropy;

   desc.AddressU = sampler_address_mode((pipe_tex_wrap) state->wrap_s,
                                        (pipe_tex_filter) state->min_img_filter);
   desc.AddressV = sampler_address_mode((pipe_tex_wrap) state->wrap_t,
                                        (pipe_tex_filter) state->min_img_filter);
   desc.AddressW = sampler_address_mode((pipe_tex_wrap) state->wrap_r,
                                        (pipe_tex_filter) state->min_img_filter);
   desc.MipLODBias = CLAMP(state->lod_bias, -16.0f, 15.99f);
   memcpy(desc.BorderColor, state->border_color.f, sizeof(float) * 4);

   // TODO Normalized Coordinates?
   d3d12_descriptor_pool_alloc_handle(ctx->sampler_pool, &ss->handle);
   screen->dev->CreateSampler(&desc, ss->handle.cpu_handle);

   if (state->compare_mode == PIPE_TEX_COMPARE_R_TO_TEXTURE) {
      desc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
      struct pipe_sampler_state fake_state = *state;
      fake_state.compare_mode = PIPE_TEX_COMPARE_NONE;
      desc.Filter = get_filter(&fake_state);

      d3d12_descriptor_pool_alloc_handle(ctx->sampler_pool,
                                         &ss->handle_without_shadow);
      screen->dev->CreateSampler(&desc,
                                 ss->handle_without_shadow.cpu_handle);
      ss->is_shadow_sampler = true;
   }

   return ss;
}

static void
d3d12_bind_sampler_states(struct pipe_context *pctx,
                          enum pipe_shader_type shader,
                          unsigned start_slot,
                          unsigned num_samplers,
                          void **samplers)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   bool shader_state_dirty = false;

#define STATIC_ASSERT_PIPE_EQUAL_COMP_FUNC(X) \
   static_assert((enum compare_func)PIPE_FUNC_##X == COMPARE_FUNC_##X, #X " needs switch case");

   STATIC_ASSERT_PIPE_EQUAL_COMP_FUNC(LESS);
   STATIC_ASSERT_PIPE_EQUAL_COMP_FUNC(GREATER);
   STATIC_ASSERT_PIPE_EQUAL_COMP_FUNC(LEQUAL);
   STATIC_ASSERT_PIPE_EQUAL_COMP_FUNC(GEQUAL);
   STATIC_ASSERT_PIPE_EQUAL_COMP_FUNC(NOTEQUAL);
   STATIC_ASSERT_PIPE_EQUAL_COMP_FUNC(NEVER);
   STATIC_ASSERT_PIPE_EQUAL_COMP_FUNC(ALWAYS);

#undef STATIC_ASSERT_PIPE_EQUAL_COMP_FUNC

   for (unsigned i = 0; i < num_samplers; ++i) {
      d3d12_sampler_state *sampler = (struct d3d12_sampler_state*) samplers[i];
      ctx->samplers[shader][start_slot + i] = sampler;
      dxil_wrap_sampler_state &wrap = ctx->tex_wrap_states[shader][start_slot + i];
      if (sampler) {
         shader_state_dirty |= wrap.wrap[0] != sampler->wrap_s ||
                               wrap.wrap[1] != sampler->wrap_t ||
                               wrap.wrap[2] != sampler->wrap_r;
         shader_state_dirty |= !!memcmp(wrap.border_color, sampler->border_color, 4 * sizeof(float));

         wrap.wrap[0] = sampler->wrap_s;
         wrap.wrap[1] = sampler->wrap_t;
         wrap.wrap[2] = sampler->wrap_r;
         wrap.lod_bias = sampler->lod_bias;
         wrap.min_lod = sampler->min_lod;
         wrap.max_lod = sampler->max_lod;
         memcpy(wrap.border_color, sampler->border_color, 4 * sizeof(float));
         ctx->tex_compare_func[shader][start_slot + i] = (enum compare_func)sampler->compare_func;
      } else {
         memset(&wrap, 0, sizeof (dxil_wrap_sampler_state));
      }
   }

   ctx->num_samplers[shader] = start_slot + num_samplers;
   ctx->shader_dirty[shader] |= D3D12_SHADER_DIRTY_SAMPLERS;
   if (shader_state_dirty)
      ctx->state_dirty |= D3D12_DIRTY_SHADER;
}

static void
d3d12_delete_sampler_state(struct pipe_context *pctx,
                           void *ss)
{
   struct d3d12_batch *batch = d3d12_current_batch(d3d12_context(pctx));
   struct d3d12_sampler_state *state = (struct d3d12_sampler_state*) ss;
   util_dynarray_append(&batch->zombie_samplers, d3d12_descriptor_handle,
                        state->handle);
   if (state->is_shadow_sampler)
      util_dynarray_append(&batch->zombie_samplers, d3d12_descriptor_handle,
                           state->handle_without_shadow);
   FREE(ss);
}

static D3D12_SRV_DIMENSION
view_dimension(enum pipe_texture_target target, unsigned samples)
{
   switch (target) {
   case PIPE_BUFFER: return D3D12_SRV_DIMENSION_BUFFER;
   case PIPE_TEXTURE_1D: return D3D12_SRV_DIMENSION_TEXTURE1D;
   case PIPE_TEXTURE_1D_ARRAY: return D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D:
      return samples > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS :
                           D3D12_SRV_DIMENSION_TEXTURE2D;
   case PIPE_TEXTURE_2D_ARRAY:
      return samples > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY :
                           D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
   case PIPE_TEXTURE_CUBE: return D3D12_SRV_DIMENSION_TEXTURECUBE;
   case PIPE_TEXTURE_CUBE_ARRAY: return D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
   case PIPE_TEXTURE_3D: return D3D12_SRV_DIMENSION_TEXTURE3D;
   default:
      unreachable("unexpected target");
   }
}

static D3D12_SHADER_COMPONENT_MAPPING
component_mapping(enum pipe_swizzle swizzle, D3D12_SHADER_COMPONENT_MAPPING id)
{
   switch (swizzle) {
   case PIPE_SWIZZLE_X: return D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0;
   case PIPE_SWIZZLE_Y: return D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1;
   case PIPE_SWIZZLE_Z: return D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2;
   case PIPE_SWIZZLE_W: return D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3;
   case PIPE_SWIZZLE_0: return D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_0;
   case PIPE_SWIZZLE_1: return D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1;
   case PIPE_SWIZZLE_NONE: return id;
   default:
      unreachable("unexpected swizzle");
   }
}

static struct pipe_sampler_view *
d3d12_create_sampler_view(struct pipe_context *pctx,
                          struct pipe_resource *texture,
                          const struct pipe_sampler_view *state)
{
   struct d3d12_screen *screen = d3d12_screen(pctx->screen);
   struct d3d12_resource *res = d3d12_resource(texture);
   struct d3d12_sampler_view *sampler_view = CALLOC_STRUCT(d3d12_sampler_view);

   sampler_view->base = *state;
   sampler_view->base.texture = NULL;
   pipe_resource_reference(&sampler_view->base.texture, texture);
   sampler_view->base.reference.count = 1;
   sampler_view->base.context = pctx;
   sampler_view->mip_levels = state->u.tex.last_level - state->u.tex.first_level + 1;
   sampler_view->array_size = texture->array_size;

   D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
   struct d3d12_format_info format_info = d3d12_get_format_info(state->format, state->target);
   pipe_swizzle swizzle[4] = {
      format_info.swizzle[sampler_view->base.swizzle_r],
      format_info.swizzle[sampler_view->base.swizzle_g],
      format_info.swizzle[sampler_view->base.swizzle_b],
      format_info.swizzle[sampler_view->base.swizzle_a]
   };

   sampler_view->swizzle_override_r = swizzle[0];
   sampler_view->swizzle_override_g = swizzle[1];
   sampler_view->swizzle_override_b = swizzle[2];
   sampler_view->swizzle_override_a = swizzle[3];

   desc.Format = d3d12_get_resource_srv_format(state->format, state->target);
   desc.ViewDimension = view_dimension(state->target, texture->nr_samples);

   /* Integer cube textures are not really supported, because TextureLoad doesn't exist
    * for cube maps, and we sampling is not supported for integer textures, so we have to
    * handle this SRV as if it were a 2D texture array */
   if ((desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE ||
        desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY) &&
       util_format_is_pure_integer(state->format)) {
      desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
   }

   desc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
         component_mapping(swizzle[0], D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0),
         component_mapping(swizzle[1], D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1),
         component_mapping(swizzle[2], D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2),
         component_mapping(swizzle[3], D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_3)
         );

   unsigned array_size = state->u.tex.last_layer - state->u.tex.first_layer + 1;
   switch (desc.ViewDimension) {
   case D3D12_SRV_DIMENSION_TEXTURE1D:
      if (state->u.tex.first_layer > 0)
         debug_printf("D3D12: can't create 1D SRV from layer %d\n",
                      state->u.tex.first_layer);

      desc.Texture1D.MostDetailedMip = state->u.tex.first_level;
      desc.Texture1D.MipLevels = sampler_view->mip_levels;
      desc.Texture1D.ResourceMinLODClamp = 0.0f;
      break;
   case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
      desc.Texture1DArray.MostDetailedMip = state->u.tex.first_level;
      desc.Texture1DArray.MipLevels = sampler_view->mip_levels;
      desc.Texture1DArray.ResourceMinLODClamp = 0.0f;
      desc.Texture1DArray.FirstArraySlice = state->u.tex.first_layer;
      desc.Texture1DArray.ArraySize = array_size;
      break;
   case D3D12_SRV_DIMENSION_TEXTURE2D:
      if (state->u.tex.first_layer > 0)
         debug_printf("D3D12: can't create 2D SRV from layer %d\n",
                      state->u.tex.first_layer);

      desc.Texture2D.MostDetailedMip = state->u.tex.first_level;
      desc.Texture2D.MipLevels = sampler_view->mip_levels;
      desc.Texture2D.PlaneSlice = format_info.plane_slice;
      desc.Texture2D.ResourceMinLODClamp = 0.0f;
      break;
   case D3D12_SRV_DIMENSION_TEXTURE2DMS:
      if (state->u.tex.first_layer > 0)
         debug_printf("D3D12: can't create 2DMS SRV from layer %d\n",
                      state->u.tex.first_layer);
      break;
   case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
      desc.Texture2DArray.MostDetailedMip = state->u.tex.first_level;
      desc.Texture2DArray.MipLevels = sampler_view->mip_levels;
      desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
      desc.Texture2DArray.FirstArraySlice = state->u.tex.first_layer;
      desc.Texture2DArray.PlaneSlice = format_info.plane_slice;
      desc.Texture2DArray.ArraySize = array_size;
      break;
   case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
      desc.Texture2DMSArray.FirstArraySlice = state->u.tex.first_layer;
      desc.Texture2DMSArray.ArraySize = array_size;
      break;
   case D3D12_SRV_DIMENSION_TEXTURE3D:
      if (state->u.tex.first_layer > 0)
         debug_printf("D3D12: can't create 3D SRV from layer %d\n",
                      state->u.tex.first_layer);

      desc.Texture3D.MostDetailedMip = state->u.tex.first_level;
      desc.Texture3D.MipLevels = sampler_view->mip_levels;
      desc.Texture3D.ResourceMinLODClamp = 0.0f;
      break;
   case D3D12_SRV_DIMENSION_TEXTURECUBE:
      if (state->u.tex.first_layer > 0)
         debug_printf("D3D12: can't create CUBE SRV from layer %d\n",
                      state->u.tex.first_layer);

      desc.TextureCube.MostDetailedMip = state->u.tex.first_level;
      desc.TextureCube.MipLevels = sampler_view->mip_levels;
      desc.TextureCube.ResourceMinLODClamp = 0.0f;
      break;
   case D3D12_SRV_DIMENSION_BUFFER:
      desc.Buffer.FirstElement = 0;
      desc.Buffer.StructureByteStride = 0;
      desc.Buffer.NumElements = texture->width0 / util_format_get_blocksize(state->format);
      break;
   default:
      unreachable("Invalid SRV dimension");
   }

   mtx_lock(&screen->descriptor_pool_mutex);
   d3d12_descriptor_pool_alloc_handle(screen->view_pool, &sampler_view->handle);
   mtx_unlock(&screen->descriptor_pool_mutex);

   screen->dev->CreateShaderResourceView(d3d12_resource_resource(res), &desc,
                                         sampler_view->handle.cpu_handle);

   return &sampler_view->base;
}

static void
d3d12_increment_sampler_view_bind_count(struct pipe_context *ctx,
   enum pipe_shader_type shader_type,
   struct pipe_sampler_view *view) {
      struct d3d12_resource *res = d3d12_resource(view->texture);
      if (res)
         res->bind_counts[shader_type][D3D12_RESOURCE_BINDING_TYPE_SRV]++;
}

static void
d3d12_decrement_sampler_view_bind_count(struct pipe_context *ctx,
                              enum pipe_shader_type shader_type,
                              struct pipe_sampler_view *view) {
   struct d3d12_resource *res = d3d12_resource(view->texture);
   if (res) {
      assert(res->bind_counts[shader_type][D3D12_RESOURCE_BINDING_TYPE_SRV] > 0);
      res->bind_counts[shader_type][D3D12_RESOURCE_BINDING_TYPE_SRV]--;
   }
}

static void
d3d12_set_sampler_views(struct pipe_context *pctx,
                        enum pipe_shader_type shader_type,
                        unsigned start_slot,
                        unsigned num_views,
                        unsigned unbind_num_trailing_slots,
                        bool take_ownership,
                        struct pipe_sampler_view **views)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   unsigned shader_bit = (1 << shader_type);
   ctx->has_int_samplers &= ~shader_bit;

   for (unsigned i = 0; i < num_views; ++i) {
      struct pipe_sampler_view *&old_view = ctx->sampler_views[shader_type][start_slot + i];
      if (old_view)
         d3d12_decrement_sampler_view_bind_count(pctx, shader_type, old_view);

      struct pipe_sampler_view *new_view = views[i];
      if (new_view)
         d3d12_increment_sampler_view_bind_count(pctx, shader_type, new_view);

      if (take_ownership) {
         pipe_sampler_view_reference(&old_view, NULL);
         old_view = views[i];
      } else {
         pipe_sampler_view_reference(&old_view, views[i]);
      }

      if (views[i]) {
         dxil_wrap_sampler_state &wss = ctx->tex_wrap_states[shader_type][start_slot + i];
         dxil_texture_swizzle_state &swizzle_state = ctx->tex_swizzle_state[shader_type][i];
         if (util_format_is_pure_integer(views[i]->format)) {
            ctx->has_int_samplers |= shader_bit;
            wss.is_int_sampler = 1;
            wss.last_level = views[i]->texture->last_level;
            /* When we emulate a integer cube texture (array) by using a texture 2d Array
             * the coordinates are evaluated to always reside withing the acceptable range
             * because the 3d ray for picking the texel is always pointing at one cube face,
             * hence we can skip the boundary condition handling when the texture operations are
             * lowered to texel fetches later. */
            wss.skip_boundary_conditions = views[i]->target == PIPE_TEXTURE_CUBE ||
                                           views[i]->target == PIPE_TEXTURE_CUBE_ARRAY;
         } else {
            wss.is_int_sampler = 0;
         }
         /* We need the swizzle state for compare texture lowering, because it
          * encode the use of the shadow texture lookup result as either luminosity,
          * intensity, or alpha. and we need the swizzle state for applying the
          * boundary color correctly */
         struct d3d12_sampler_view *ss = d3d12_sampler_view(views[i]);
         swizzle_state.swizzle_r = ss->swizzle_override_r;
         swizzle_state.swizzle_g = ss->swizzle_override_g;
         swizzle_state.swizzle_b = ss->swizzle_override_b;
         swizzle_state.swizzle_a = ss->swizzle_override_a;
      }
   }

   for (unsigned i = 0; i < unbind_num_trailing_slots; i++) {
      struct pipe_sampler_view *&old_view = ctx->sampler_views[shader_type][start_slot + num_views + i];
      if (old_view)
         d3d12_decrement_sampler_view_bind_count(pctx, shader_type, old_view);
      pipe_sampler_view_reference(&old_view, NULL);
   }
   ctx->num_sampler_views[shader_type] = start_slot + num_views;
   ctx->shader_dirty[shader_type] |= D3D12_SHADER_DIRTY_SAMPLER_VIEWS;
}

static void
d3d12_destroy_sampler_view(struct pipe_context *pctx,
                           struct pipe_sampler_view *pview)
{
   struct d3d12_sampler_view *view = d3d12_sampler_view(pview);
   d3d12_descriptor_handle_free(&view->handle);
   pipe_resource_reference(&view->base.texture, NULL);
   FREE(view);
}

static void
delete_shader(struct d3d12_context *ctx, enum pipe_shader_type stage,
              struct d3d12_shader_selector *shader)
{
   d3d12_gfx_pipeline_state_cache_invalidate_shader(ctx, stage, shader);

   /* Make sure the pipeline state no longer reference the deleted shader */
   struct d3d12_shader *iter = shader->first;
   while (iter) {
      if (ctx->gfx_pipeline_state.stages[stage] == iter) {
         ctx->gfx_pipeline_state.stages[stage] = NULL;
         break;
      }
      iter = iter->next_variant;
   }

   d3d12_shader_free(shader);
}

static void
bind_stage(struct d3d12_context *ctx, enum pipe_shader_type stage,
           struct d3d12_shader_selector *shader)
{
   assert(stage < D3D12_GFX_SHADER_STAGES);
   ctx->gfx_stages[stage] = shader;
}

static void *
d3d12_create_vs_state(struct pipe_context *pctx,
                      const struct pipe_shader_state *shader)
{
   return d3d12_create_shader(d3d12_context(pctx), PIPE_SHADER_VERTEX, shader);
}

static void
d3d12_bind_vs_state(struct pipe_context *pctx,
                    void *vss)
{
   bind_stage(d3d12_context(pctx), PIPE_SHADER_VERTEX,
              (struct d3d12_shader_selector *) vss);
}

static void
d3d12_delete_vs_state(struct pipe_context *pctx,
                      void *vs)
{
   delete_shader(d3d12_context(pctx), PIPE_SHADER_VERTEX,
                 (struct d3d12_shader_selector *) vs);
}

static void *
d3d12_create_fs_state(struct pipe_context *pctx,
                      const struct pipe_shader_state *shader)
{
   return d3d12_create_shader(d3d12_context(pctx), PIPE_SHADER_FRAGMENT, shader);
}

static void
d3d12_bind_fs_state(struct pipe_context *pctx,
                    void *fss)
{
   bind_stage(d3d12_context(pctx), PIPE_SHADER_FRAGMENT,
              (struct d3d12_shader_selector *) fss);
}

static void
d3d12_delete_fs_state(struct pipe_context *pctx,
                      void *fs)
{
   delete_shader(d3d12_context(pctx), PIPE_SHADER_FRAGMENT,
                 (struct d3d12_shader_selector *) fs);
}

static void *
d3d12_create_gs_state(struct pipe_context *pctx,
                      const struct pipe_shader_state *shader)
{
   return d3d12_create_shader(d3d12_context(pctx), PIPE_SHADER_GEOMETRY, shader);
}

static void
d3d12_bind_gs_state(struct pipe_context *pctx, void *gss)
{
   bind_stage(d3d12_context(pctx), PIPE_SHADER_GEOMETRY,
              (struct d3d12_shader_selector *) gss);
}

static void
d3d12_delete_gs_state(struct pipe_context *pctx, void *gs)
{
   delete_shader(d3d12_context(pctx), PIPE_SHADER_GEOMETRY,
                 (struct d3d12_shader_selector *) gs);
}

static bool
d3d12_init_polygon_stipple(struct pipe_context *pctx)
{
   struct d3d12_context *ctx = d3d12_context(pctx);

   ctx->pstipple.texture = util_pstipple_create_stipple_texture(pctx, NULL);
   if (!ctx->pstipple.texture)
      return false;

   ctx->pstipple.sampler_view = util_pstipple_create_sampler_view(pctx, ctx->pstipple.texture);
   if (!ctx->pstipple.sampler_view)
      return false;

   ctx->pstipple.sampler_cso = (struct d3d12_sampler_state *)util_pstipple_create_sampler(pctx);
   if (!ctx->pstipple.sampler_cso)
      return false;

   return true;
}

static void
d3d12_set_polygon_stipple(struct pipe_context *pctx,
                          const struct pipe_poly_stipple *ps)
{
   static bool initialized = false;
   static const uint32_t zero[32] = {0};
   static uint32_t undef[32] = {0};
   struct d3d12_context *ctx = d3d12_context(pctx);

   if (!initialized)
      memset(undef, UINT32_MAX, sizeof(undef));

   if (!memcmp(ctx->pstipple.pattern, ps->stipple, sizeof(ps->stipple)))
      return;

   memcpy(ctx->pstipple.pattern, ps->stipple, sizeof(ps->stipple));
   ctx->pstipple.enabled = !!memcmp(ps->stipple, undef, sizeof(ps->stipple)) &&
                           !!memcmp(ps->stipple, zero, sizeof(ps->stipple));
   if (ctx->pstipple.enabled)
      util_pstipple_update_stipple_texture(pctx, ctx->pstipple.texture, ps->stipple);
}

static void
d3d12_set_vertex_buffers(struct pipe_context *pctx,
                         unsigned start_slot,
                         unsigned num_buffers,
                         unsigned unbind_num_trailing_slots,
                         bool take_ownership,
                         const struct pipe_vertex_buffer *buffers)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   util_set_vertex_buffers_count(ctx->vbs, &ctx->num_vbs,
                                 buffers, start_slot, num_buffers,
                                 unbind_num_trailing_slots,
                                 take_ownership);

   for (unsigned i = 0; i < ctx->num_vbs; ++i) {
      const struct pipe_vertex_buffer* buf = ctx->vbs + i;
      if (!buf->buffer.resource)
         continue;
      struct d3d12_resource *res = d3d12_resource(buf->buffer.resource);
      ctx->vbvs[i].BufferLocation = d3d12_resource_gpu_virtual_address(res) + buf->buffer_offset;
      ctx->vbvs[i].StrideInBytes = buf->stride;
      ctx->vbvs[i].SizeInBytes = res->base.width0 - buf->buffer_offset;
   }
   ctx->state_dirty |= D3D12_DIRTY_VERTEX_BUFFERS;
}

static void
d3d12_set_viewport_states(struct pipe_context *pctx,
                          unsigned start_slot,
                          unsigned num_viewports,
                          const struct pipe_viewport_state *state)
{
   struct d3d12_context *ctx = d3d12_context(pctx);

   for (unsigned i = 0; i < num_viewports; ++i) {
      if (state[i].scale[1] < 0) {
         ctx->flip_y = 1.0f;
         ctx->viewports[start_slot + i].TopLeftY = state[i].translate[1] + state[i].scale[1];
         ctx->viewports[start_slot + i].Height = -state[i].scale[1] * 2;
      } else {
         ctx->flip_y = -1.0f;
         ctx->viewports[start_slot + i].TopLeftY = state[i].translate[1] - state[i].scale[1];
         ctx->viewports[start_slot + i].Height = state[i].scale[1] * 2;
      }
      ctx->viewports[start_slot + i].TopLeftX = state[i].translate[0] - state[i].scale[0];
      ctx->viewports[start_slot + i].Width = state[i].scale[0] * 2;

      float near_depth = state[i].translate[2] - state[i].scale[2];
      float far_depth = state[i].translate[2] + state[i].scale[2];

      ctx->reverse_depth_range = near_depth > far_depth;
      if (ctx->reverse_depth_range) {
         float tmp = near_depth;
         near_depth = far_depth;
         far_depth = tmp;
      }
      ctx->viewports[start_slot + i].MinDepth = near_depth;
      ctx->viewports[start_slot + i].MaxDepth = far_depth;
      ctx->viewport_states[start_slot + i] = state[i];
   }
   ctx->num_viewports = start_slot + num_viewports;
   ctx->state_dirty |= D3D12_DIRTY_VIEWPORT;
}


static void
d3d12_set_scissor_states(struct pipe_context *pctx,
                         unsigned start_slot, unsigned num_scissors,
                         const struct pipe_scissor_state *states)
{
   struct d3d12_context *ctx = d3d12_context(pctx);

   for (unsigned i = 0; i < num_scissors; i++) {
      ctx->scissors[start_slot + i].left = states[i].minx;
      ctx->scissors[start_slot + i].top = states[i].miny;
      ctx->scissors[start_slot + i].right = states[i].maxx;
      ctx->scissors[start_slot + i].bottom = states[i].maxy;
      ctx->scissor_states[start_slot + i] = states[i];
   }
   ctx->state_dirty |= D3D12_DIRTY_SCISSOR;
}

static void
d3d12_decrement_constant_buffer_bind_count(struct d3d12_context *ctx,
                                           enum pipe_shader_type shader,
                                           struct d3d12_resource *res) {
   assert(res->bind_counts[shader][D3D12_RESOURCE_BINDING_TYPE_CBV] > 0);
   res->bind_counts[shader][D3D12_RESOURCE_BINDING_TYPE_CBV]--;
}

static void
d3d12_increment_constant_buffer_bind_count(struct d3d12_context *ctx,
                                           enum pipe_shader_type shader,
                                           struct d3d12_resource *res) {
   res->bind_counts[shader][D3D12_RESOURCE_BINDING_TYPE_CBV]++;
}

static void
d3d12_set_constant_buffer(struct pipe_context *pctx,
                          enum pipe_shader_type shader, uint index,
                          bool take_ownership,
                          const struct pipe_constant_buffer *buf)
{
   struct d3d12_context *ctx = d3d12_context(pctx);

   if (buf) {
      struct pipe_resource *buffer = buf->buffer;
      unsigned offset = buf->buffer_offset;
      if (buf->user_buffer) {
         u_upload_data(pctx->const_uploader, 0, buf->buffer_size,
                       D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                       buf->user_buffer, &offset, &ctx->cbufs[shader][index].buffer);

      } else {
         if (take_ownership) {
            struct d3d12_resource *old_buf = d3d12_resource(ctx->cbufs[shader][index].buffer);
            if (old_buf)
               d3d12_decrement_constant_buffer_bind_count(ctx, shader, old_buf);
            pipe_resource_reference(&ctx->cbufs[shader][index].buffer, NULL);
            ctx->cbufs[shader][index].buffer = buffer;
            if (buffer)
               d3d12_increment_constant_buffer_bind_count(ctx, shader, d3d12_resource(buffer));
         } else {
            pipe_resource_reference(&ctx->cbufs[shader][index].buffer, buffer);
         }
      }


      ctx->cbufs[shader][index].buffer_offset = offset;
      ctx->cbufs[shader][index].buffer_size = buf->buffer_size;
      ctx->cbufs[shader][index].user_buffer = NULL;

   } else {
      pipe_resource_reference(&ctx->cbufs[shader][index].buffer, NULL);
      ctx->cbufs[shader][index].buffer_offset = 0;
      ctx->cbufs[shader][index].buffer_size = 0;
      ctx->cbufs[shader][index].user_buffer = NULL;
   }
   ctx->shader_dirty[shader] |= D3D12_SHADER_DIRTY_CONSTBUF;
}

static void
d3d12_set_framebuffer_state(struct pipe_context *pctx,
                            const struct pipe_framebuffer_state *state)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   int samples = -1;

   util_copy_framebuffer_state(&d3d12_context(pctx)->fb, state);

   ctx->gfx_pipeline_state.num_cbufs = state->nr_cbufs;
   ctx->gfx_pipeline_state.has_float_rtv = false;
   for (int i = 0; i < state->nr_cbufs; ++i) {
      if (state->cbufs[i]) {
         if (util_format_is_float(state->cbufs[i]->format))
            ctx->gfx_pipeline_state.has_float_rtv = true;
         ctx->gfx_pipeline_state.rtv_formats[i] = d3d12_get_format(state->cbufs[i]->format);
         samples = MAX2(samples, (int)state->cbufs[i]->texture->nr_samples);
      } else {
         ctx->gfx_pipeline_state.rtv_formats[i] = DXGI_FORMAT_UNKNOWN;
      }
   }

   if (state->zsbuf) {
      ctx->gfx_pipeline_state.dsv_format = d3d12_get_resource_rt_format(state->zsbuf->format);
      samples = MAX2(samples, (int)ctx->fb.zsbuf->texture->nr_samples);
   } else
      ctx->gfx_pipeline_state.dsv_format = DXGI_FORMAT_UNKNOWN;

   if (samples < 0)
      samples = state->samples;

   ctx->gfx_pipeline_state.samples = MAX2(samples, 1);

   ctx->state_dirty |= D3D12_DIRTY_FRAMEBUFFER;
}

static void
d3d12_set_blend_color(struct pipe_context *pctx,
                     const struct pipe_blend_color *color)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   memcpy(ctx->blend_factor, color->color, sizeof(float) * 4);
   ctx->state_dirty |= D3D12_DIRTY_BLEND_COLOR;
}

static void
d3d12_set_sample_mask(struct pipe_context *pctx, unsigned sample_mask)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   ctx->gfx_pipeline_state.sample_mask = sample_mask;
   ctx->state_dirty |= D3D12_DIRTY_SAMPLE_MASK;
}

static void
d3d12_set_stencil_ref(struct pipe_context *pctx,
                      const struct pipe_stencil_ref ref)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   if ((ref.ref_value[0] != ref.ref_value[1]) &&
       (d3d12_debug & D3D12_DEBUG_VERBOSE))
       debug_printf("D3D12: Different values for front and back stencil reference are not supported\n");
   ctx->stencil_ref = ref;
   ctx->state_dirty |= D3D12_DIRTY_STENCIL_REF;
}

static void
d3d12_set_clip_state(struct pipe_context *pctx,
                     const struct pipe_clip_state *pcs)
{
}

static struct pipe_stream_output_target *
d3d12_create_stream_output_target(struct pipe_context *pctx,
                                  struct pipe_resource *pres,
                                  unsigned buffer_offset,
                                  unsigned buffer_size)
{
   struct d3d12_resource *res = d3d12_resource(pres);
   struct d3d12_stream_output_target *cso = CALLOC_STRUCT(d3d12_stream_output_target);

   if (!cso)
      return NULL;

   pipe_reference_init(&cso->base.reference, 1);
   pipe_resource_reference(&cso->base.buffer, pres);
   cso->base.buffer_offset = buffer_offset;
   cso->base.buffer_size = buffer_size;
   cso->base.context = pctx;

   if (res->bo && res->bo->buffer && d3d12_buffer(res->bo->buffer)->map)
      util_range_add(pres, &res->valid_buffer_range, buffer_offset,
                     buffer_offset + buffer_size);

   return &cso->base;
}

static void
d3d12_stream_output_target_destroy(struct pipe_context *ctx,
                                   struct pipe_stream_output_target *state)
{
   pipe_resource_reference(&state->buffer, NULL);

   FREE(state);
}

static void
fill_stream_output_buffer_view(D3D12_STREAM_OUTPUT_BUFFER_VIEW *view,
                               struct d3d12_stream_output_target *target)
{
   struct d3d12_resource *res = d3d12_resource(target->base.buffer);
   struct d3d12_resource *fill_res = d3d12_resource(target->fill_buffer);

   view->SizeInBytes = target->base.buffer_size;
   view->BufferLocation = d3d12_resource_gpu_virtual_address(res) + target->base.buffer_offset;
   view->BufferFilledSizeLocation = d3d12_resource_gpu_virtual_address(fill_res) + target->fill_buffer_offset;
}

static void
d3d12_set_stream_output_targets(struct pipe_context *pctx,
                                unsigned num_targets,
                                struct pipe_stream_output_target **targets,
                                const unsigned *offsets)
{
   struct d3d12_context *ctx = d3d12_context(pctx);

   assert(num_targets <= ARRAY_SIZE(ctx->so_targets));

   d3d12_disable_fake_so_buffers(ctx);

   for (unsigned i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
      struct d3d12_stream_output_target *target =
         i < num_targets ? (struct d3d12_stream_output_target *)targets[i] : NULL;

      if (target) {
         /* Sub-allocate a new fill buffer each time to avoid GPU/CPU synchronization */
         u_suballocator_alloc(&ctx->so_allocator, sizeof(uint64_t), 4,
                              &target->fill_buffer_offset, &target->fill_buffer);
         fill_stream_output_buffer_view(&ctx->so_buffer_views[i], target);
         pipe_so_target_reference(&ctx->so_targets[i], targets[i]);
      } else {
         ctx->so_buffer_views[i].SizeInBytes = 0;
         pipe_so_target_reference(&ctx->so_targets[i], NULL);
      }
   }

   ctx->gfx_pipeline_state.num_so_targets = num_targets;
   ctx->state_dirty |= D3D12_DIRTY_STREAM_OUTPUT;
}

static void
d3d12_invalidate_context_bindings(struct d3d12_context *ctx, struct d3d12_resource *res) {
   // For each shader type, if the resource is currently bound as CBV or SRV
   // set the context shader_dirty bit.
   for (uint i = 0; i < PIPE_SHADER_TYPES; ++i) {
      if (res->bind_counts[i][D3D12_RESOURCE_BINDING_TYPE_CBV] > 0) {
         ctx->shader_dirty[i] |= D3D12_SHADER_DIRTY_CONSTBUF;
      }

      if (res->bind_counts[i][D3D12_RESOURCE_BINDING_TYPE_SRV] > 0) {
         ctx->shader_dirty[i] |= D3D12_SHADER_DIRTY_SAMPLER_VIEWS;
      }
   }
}

bool
d3d12_enable_fake_so_buffers(struct d3d12_context *ctx, unsigned factor)
{
   if (ctx->fake_so_buffer_factor == factor)
      return true;

   d3d12_disable_fake_so_buffers(ctx);

   for (unsigned i = 0; i < ctx->gfx_pipeline_state.num_so_targets; ++i) {
      struct d3d12_stream_output_target *target = (struct d3d12_stream_output_target *)ctx->so_targets[i];
      struct d3d12_stream_output_target *fake_target;

      fake_target = CALLOC_STRUCT(d3d12_stream_output_target);
      if (!fake_target)
         return false;
      pipe_reference_init(&fake_target->base.reference, 1);
      fake_target->base.context = &ctx->base;

      d3d12_resource_wait_idle(ctx, d3d12_resource(target->base.buffer));

      /* Check if another target is using the same buffer */
      for (unsigned j = 0; j < i; ++j) {
         if (ctx->so_targets[j] && ctx->so_targets[j]->buffer == target->base.buffer) {
            struct d3d12_stream_output_target *prev_target =
               (struct d3d12_stream_output_target *)ctx->fake_so_targets[j];
            pipe_resource_reference(&fake_target->base.buffer, prev_target->base.buffer);
            pipe_resource_reference(&fake_target->fill_buffer, prev_target->fill_buffer);
            fake_target->fill_buffer_offset = prev_target->fill_buffer_offset;
            fake_target->cached_filled_size = prev_target->cached_filled_size;
            break;
         }
      }

      /* Create new SO buffer 6x (2 triangles instead of 1 point) the original size if not */
      if (!fake_target->base.buffer) {
         fake_target->base.buffer = pipe_buffer_create(ctx->base.screen,
                                                       PIPE_BIND_STREAM_OUTPUT,
                                                       PIPE_USAGE_STAGING,
                                                       target->base.buffer->width0 * factor);
         u_suballocator_alloc(&ctx->so_allocator, sizeof(uint64_t), 4,
                              &fake_target->fill_buffer_offset, &fake_target->fill_buffer);
         pipe_buffer_read(&ctx->base, target->fill_buffer,
                          target->fill_buffer_offset, sizeof(uint64_t),
                          &fake_target->cached_filled_size);
      }

      fake_target->base.buffer_offset = target->base.buffer_offset * factor;
      fake_target->base.buffer_size = (target->base.buffer_size - fake_target->cached_filled_size) * factor;
      ctx->fake_so_targets[i] = &fake_target->base;
      fill_stream_output_buffer_view(&ctx->fake_so_buffer_views[i], fake_target);
   }

   ctx->fake_so_buffer_factor = factor;
   ctx->cmdlist_dirty |= D3D12_DIRTY_STREAM_OUTPUT;

   return true;
}

bool
d3d12_disable_fake_so_buffers(struct d3d12_context *ctx)
{
   if (ctx->fake_so_buffer_factor == 0)
      return true;

   d3d12_flush_cmdlist_and_wait(ctx);

   for (unsigned i = 0; i < ctx->gfx_pipeline_state.num_so_targets; ++i) {
      struct d3d12_stream_output_target *target = (struct d3d12_stream_output_target *)ctx->so_targets[i];
      struct d3d12_stream_output_target *fake_target = (struct d3d12_stream_output_target *)ctx->fake_so_targets[i];
      uint64_t filled_size = 0;
      struct pipe_transfer *src_transfer, *dst_transfer;
      uint8_t *src, *dst;

      if (fake_target == NULL)
         continue;

      pipe_buffer_read(&ctx->base, fake_target->fill_buffer,
                       fake_target->fill_buffer_offset, sizeof(uint64_t),
                       &filled_size);

      src = (uint8_t *)pipe_buffer_map_range(&ctx->base, fake_target->base.buffer,
                                             fake_target->base.buffer_offset,
                                             fake_target->base.buffer_size,
                                             PIPE_MAP_READ, &src_transfer);
      dst = (uint8_t *)pipe_buffer_map_range(&ctx->base, target->base.buffer,
                                             target->base.buffer_offset,
                                             target->base.buffer_size,
                                             PIPE_MAP_READ, &dst_transfer);

      /* Note: This will break once support for gl_SkipComponents is added */
      uint32_t stride = ctx->gfx_pipeline_state.so_info.stride[i] * 4;
      uint64_t src_offset = 0, dst_offset = fake_target->cached_filled_size;
      while (src_offset < filled_size) {
         memcpy(dst + dst_offset, src + src_offset, stride);
         src_offset += stride * ctx->fake_so_buffer_factor;
         dst_offset += stride;
      }

      pipe_buffer_unmap(&ctx->base, src_transfer);
      pipe_buffer_unmap(&ctx->base, dst_transfer);

      pipe_so_target_reference(&ctx->fake_so_targets[i], NULL);
      ctx->fake_so_buffer_views[i].SizeInBytes = 0;

      /* Make sure the buffer is not copied twice */
      for (unsigned j = i + 1; j <= ctx->gfx_pipeline_state.num_so_targets; ++j) {
         if (ctx->so_targets[j] && ctx->so_targets[j]->buffer == target->base.buffer)
            pipe_so_target_reference(&ctx->fake_so_targets[j], NULL);
      }
   }

   ctx->fake_so_buffer_factor = 0;
   ctx->cmdlist_dirty |= D3D12_DIRTY_STREAM_OUTPUT;

   return true;
}

void
d3d12_flush_cmdlist(struct d3d12_context *ctx)
{
   d3d12_end_batch(ctx, d3d12_current_batch(ctx));

   ctx->current_batch_idx++;
   if (ctx->current_batch_idx == ARRAY_SIZE(ctx->batches))
      ctx->current_batch_idx = 0;

   d3d12_start_batch(ctx, d3d12_current_batch(ctx));
}

void
d3d12_flush_cmdlist_and_wait(struct d3d12_context *ctx)
{
   struct d3d12_batch *batch = d3d12_current_batch(ctx);

   d3d12_foreach_submitted_batch(ctx, old_batch)
      d3d12_reset_batch(ctx, old_batch, PIPE_TIMEOUT_INFINITE);
   d3d12_flush_cmdlist(ctx);
   d3d12_reset_batch(ctx, batch, PIPE_TIMEOUT_INFINITE);
}

void
d3d12_transition_resource_state(struct d3d12_context *ctx,
                                struct d3d12_resource *res,
                                D3D12_RESOURCE_STATES state,
                                d3d12_bind_invalidate_option bind_invalidate)
{
   TransitionableResourceState *xres = d3d12_resource_state(res);
   
   if (bind_invalidate == D3D12_BIND_INVALIDATE_FULL)
      d3d12_invalidate_context_bindings(ctx, res);

   ctx->resource_state_manager->TransitionResource(xres, state);
}

void
d3d12_transition_subresources_state(struct d3d12_context *ctx,
                                    struct d3d12_resource *res,
                                    uint32_t start_level, uint32_t num_levels,
                                    uint32_t start_layer, uint32_t num_layers,
                                    uint32_t start_plane, uint32_t num_planes,
                                    D3D12_RESOURCE_STATES state,
                                    d3d12_bind_invalidate_option bind_invalidate)
{
   TransitionableResourceState *xres = d3d12_resource_state(res);

   if(bind_invalidate == D3D12_BIND_INVALIDATE_FULL)
      d3d12_invalidate_context_bindings(ctx, res);

   for (uint32_t l = 0; l < num_levels; l++) {
      const uint32_t level = start_level + l;
      for (uint32_t a = 0; a < num_layers; a++) {
         const uint32_t layer = start_layer + a;
         for( uint32_t p = 0; p < num_planes; p++) {
            const uint32_t plane = start_plane + p;
            uint32_t subres_id = level + (layer * res->mip_levels) + plane * (res->mip_levels * res->base.array_size);
            assert(subres_id < xres->NumSubresources());
            ctx->resource_state_manager->TransitionSubresource(xres, subres_id, state);
         }
      }
   }
}

void
d3d12_apply_resource_states(struct d3d12_context *ctx)
{
   ctx->resource_state_manager->ApplyAllResourceTransitions(ctx->cmdlist, ctx->fence_value);
}

static void
d3d12_clear_render_target(struct pipe_context *pctx,
                          struct pipe_surface *psurf,
                          const union pipe_color_union *color,
                          unsigned dstx, unsigned dsty,
                          unsigned width, unsigned height,
                          bool render_condition_enabled)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   struct d3d12_surface *surf = d3d12_surface(psurf);

   if (!render_condition_enabled && ctx->current_predication)
      ctx->cmdlist->SetPredication(NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);

   struct d3d12_resource *res = d3d12_resource(psurf->texture);
   d3d12_transition_resource_state(ctx, res,
                                   D3D12_RESOURCE_STATE_RENDER_TARGET,
                                   D3D12_BIND_INVALIDATE_FULL);
   d3d12_apply_resource_states(ctx);

   enum pipe_format format = psurf->texture->format;
   float clear_color[4];

   if (util_format_is_pure_uint(format)) {
      for (int c = 0; c < 4; ++c)
         clear_color[c] = color->ui[c];
   } else if (util_format_is_pure_sint(format)) {
      for (int c = 0; c < 4; ++c)
         clear_color[c] = color->i[c];
   } else {
      for (int c = 0; c < 4; ++c)
         clear_color[c] = color->f[c];
   }

   D3D12_RECT rect = { (int)dstx, (int)dsty,
                       (int)dstx + (int)width,
                       (int)dsty + (int)height };
   ctx->cmdlist->ClearRenderTargetView(surf->desc_handle.cpu_handle,
                                       clear_color, 1, &rect);

   d3d12_batch_reference_surface_texture(d3d12_current_batch(ctx), surf);

   if (!render_condition_enabled && ctx->current_predication) {
      ctx->cmdlist->SetPredication(
         d3d12_resource_resource(ctx->current_predication), 0,
         D3D12_PREDICATION_OP_EQUAL_ZERO);
   }
}

static void
d3d12_clear_depth_stencil(struct pipe_context *pctx,
                          struct pipe_surface *psurf,
                          unsigned clear_flags,
                          double depth,
                          unsigned stencil,
                          unsigned dstx, unsigned dsty,
                          unsigned width, unsigned height,
                          bool render_condition_enabled)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   struct d3d12_surface *surf = d3d12_surface(psurf);

   if (!render_condition_enabled && ctx->current_predication)
      ctx->cmdlist->SetPredication(NULL, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);

   D3D12_CLEAR_FLAGS flags = (D3D12_CLEAR_FLAGS)0;
   if (clear_flags & PIPE_CLEAR_DEPTH)
      flags |= D3D12_CLEAR_FLAG_DEPTH;
   if (clear_flags & PIPE_CLEAR_STENCIL)
      flags |= D3D12_CLEAR_FLAG_STENCIL;

   struct d3d12_resource *res = d3d12_resource(ctx->fb.zsbuf->texture);
   d3d12_transition_resource_state(ctx, res,
                                   D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                   D3D12_BIND_INVALIDATE_FULL);
   d3d12_apply_resource_states(ctx);

   D3D12_RECT rect = { (int)dstx, (int)dsty,
                       (int)dstx + (int)width,
                       (int)dsty + (int)height };
   ctx->cmdlist->ClearDepthStencilView(surf->desc_handle.cpu_handle, flags,
                                       depth, stencil, 1, &rect);

   d3d12_batch_reference_surface_texture(d3d12_current_batch(ctx), surf);

   if (!render_condition_enabled && ctx->current_predication) {
      ctx->cmdlist->SetPredication(
         d3d12_resource_resource(ctx->current_predication), 0,
         D3D12_PREDICATION_OP_EQUAL_ZERO);
   }
}

static void
d3d12_clear(struct pipe_context *pctx,
            unsigned buffers,
            const struct pipe_scissor_state *scissor_state,
            const union pipe_color_union *color,
            double depth, unsigned stencil)
{
   struct d3d12_context *ctx = d3d12_context(pctx);

   if (buffers & PIPE_CLEAR_COLOR) {
      for (int i = 0; i < ctx->fb.nr_cbufs; ++i) {
         if (buffers & (PIPE_CLEAR_COLOR0 << i)) {
            struct pipe_surface *psurf = ctx->fb.cbufs[i];
            d3d12_clear_render_target(pctx, psurf, color,
                                      0, 0, psurf->width, psurf->height,
                                      true);
         }
      }
   }

   if (buffers & PIPE_CLEAR_DEPTHSTENCIL && ctx->fb.zsbuf) {
      struct pipe_surface *psurf = ctx->fb.zsbuf;
      d3d12_clear_depth_stencil(pctx, psurf,
                                buffers & PIPE_CLEAR_DEPTHSTENCIL,
                                depth, stencil,
                                0, 0, psurf->width, psurf->height,
                                true);
   }
}

static void
d3d12_flush(struct pipe_context *pipe,
            struct pipe_fence_handle **fence,
            unsigned flags)
{
   struct d3d12_context *ctx = d3d12_context(pipe);
   struct d3d12_batch *batch = d3d12_current_batch(ctx);

   d3d12_flush_cmdlist(ctx);

   if (fence)
      d3d12_fence_reference((struct d3d12_fence **)fence, batch->fence);
}

static void
d3d12_flush_resource(struct pipe_context *pctx,
                     struct pipe_resource *pres)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   struct d3d12_resource *res = d3d12_resource(pres);

   d3d12_transition_resource_state(ctx, res,
                                   D3D12_RESOURCE_STATE_COMMON,
                                   D3D12_BIND_INVALIDATE_FULL);
   d3d12_apply_resource_states(ctx);
}

static void
d3d12_init_null_sampler(struct d3d12_context *ctx)
{
   struct d3d12_screen *screen = d3d12_screen(ctx->base.screen);

   d3d12_descriptor_pool_alloc_handle(ctx->sampler_pool, &ctx->null_sampler);

   D3D12_SAMPLER_DESC desc;
   desc.Filter = D3D12_FILTER_ANISOTROPIC;
   desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
   desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
   desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
   desc.MipLODBias = 0.0f;
   desc.MaxAnisotropy = 0;
   desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
   desc.MinLOD = 0.0f;
   desc.MaxLOD = 0.0f;
   memset(desc.BorderColor, 0, sizeof(desc.BorderColor));
   screen->dev->CreateSampler(&desc, ctx->null_sampler.cpu_handle);
}

static uint64_t
d3d12_get_timestamp(struct pipe_context *pctx)
{
   struct d3d12_context *ctx = d3d12_context(pctx);

   if (!ctx->timestamp_query)
      ctx->timestamp_query =  pctx->create_query(pctx, PIPE_QUERY_TIMESTAMP, 0);

   pipe_query_result result;
   pctx->end_query(pctx, ctx->timestamp_query);
   pctx->get_query_result(pctx, ctx->timestamp_query, true, &result);
   return result.u64;
}

struct pipe_context *
d3d12_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags)
{
   struct d3d12_screen *screen = d3d12_screen(pscreen);

   struct d3d12_context *ctx = CALLOC_STRUCT(d3d12_context);
   if (!ctx)
      return NULL;

   ctx->base.screen = pscreen;
   ctx->base.priv = priv;

   ctx->base.destroy = d3d12_context_destroy;

   ctx->base.create_vertex_elements_state = d3d12_create_vertex_elements_state;
   ctx->base.bind_vertex_elements_state = d3d12_bind_vertex_elements_state;
   ctx->base.delete_vertex_elements_state = d3d12_delete_vertex_elements_state;

   ctx->base.create_blend_state = d3d12_create_blend_state;
   ctx->base.bind_blend_state = d3d12_bind_blend_state;
   ctx->base.delete_blend_state = d3d12_delete_blend_state;

   ctx->base.create_depth_stencil_alpha_state = d3d12_create_depth_stencil_alpha_state;
   ctx->base.bind_depth_stencil_alpha_state = d3d12_bind_depth_stencil_alpha_state;
   ctx->base.delete_depth_stencil_alpha_state = d3d12_delete_depth_stencil_alpha_state;

   ctx->base.create_rasterizer_state = d3d12_create_rasterizer_state;
   ctx->base.bind_rasterizer_state = d3d12_bind_rasterizer_state;
   ctx->base.delete_rasterizer_state = d3d12_delete_rasterizer_state;

   ctx->base.create_sampler_state = d3d12_create_sampler_state;
   ctx->base.bind_sampler_states = d3d12_bind_sampler_states;
   ctx->base.delete_sampler_state = d3d12_delete_sampler_state;

   ctx->base.create_sampler_view = d3d12_create_sampler_view;
   ctx->base.set_sampler_views = d3d12_set_sampler_views;
   ctx->base.sampler_view_destroy = d3d12_destroy_sampler_view;

   ctx->base.create_vs_state = d3d12_create_vs_state;
   ctx->base.bind_vs_state = d3d12_bind_vs_state;
   ctx->base.delete_vs_state = d3d12_delete_vs_state;

   ctx->base.create_fs_state = d3d12_create_fs_state;
   ctx->base.bind_fs_state = d3d12_bind_fs_state;
   ctx->base.delete_fs_state = d3d12_delete_fs_state;

   ctx->base.create_gs_state = d3d12_create_gs_state;
   ctx->base.bind_gs_state = d3d12_bind_gs_state;
   ctx->base.delete_gs_state = d3d12_delete_gs_state;

   ctx->base.set_polygon_stipple = d3d12_set_polygon_stipple;
   ctx->base.set_vertex_buffers = d3d12_set_vertex_buffers;
   ctx->base.set_viewport_states = d3d12_set_viewport_states;
   ctx->base.set_scissor_states = d3d12_set_scissor_states;
   ctx->base.set_constant_buffer = d3d12_set_constant_buffer;
   ctx->base.set_framebuffer_state = d3d12_set_framebuffer_state;
   ctx->base.set_clip_state = d3d12_set_clip_state;
   ctx->base.set_blend_color = d3d12_set_blend_color;
   ctx->base.set_sample_mask = d3d12_set_sample_mask;
   ctx->base.set_stencil_ref = d3d12_set_stencil_ref;

   ctx->base.create_stream_output_target = d3d12_create_stream_output_target;
   ctx->base.stream_output_target_destroy = d3d12_stream_output_target_destroy;
   ctx->base.set_stream_output_targets = d3d12_set_stream_output_targets;

   ctx->base.get_timestamp = d3d12_get_timestamp;

   ctx->base.clear = d3d12_clear;
   ctx->base.clear_render_target = d3d12_clear_render_target;
   ctx->base.clear_depth_stencil = d3d12_clear_depth_stencil;
   ctx->base.draw_vbo = d3d12_draw_vbo;
   ctx->base.flush = d3d12_flush;
   ctx->base.flush_resource = d3d12_flush_resource;

   ctx->gfx_pipeline_state.sample_mask = ~0;

   d3d12_context_surface_init(&ctx->base);
   d3d12_context_resource_init(&ctx->base);
   d3d12_context_query_init(&ctx->base);
   d3d12_context_blit_init(&ctx->base);


   slab_create_child(&ctx->transfer_pool, &d3d12_screen(pscreen)->transfer_pool);

   ctx->base.stream_uploader = u_upload_create_default(&ctx->base);
   ctx->base.const_uploader = u_upload_create_default(&ctx->base);
   u_suballocator_init(&ctx->so_allocator, &ctx->base, 4096, 0,
                       PIPE_USAGE_DEFAULT,
                       0, true);

   struct primconvert_config cfg;
   cfg.primtypes_mask = 1 << PIPE_PRIM_POINTS |
                        1 << PIPE_PRIM_LINES |
                        1 << PIPE_PRIM_LINE_STRIP |
                        1 << PIPE_PRIM_TRIANGLES |
                        1 << PIPE_PRIM_TRIANGLE_STRIP;
   cfg.fixed_prim_restart = true;
   ctx->primconvert = util_primconvert_create_config(&ctx->base, &cfg);
   if (!ctx->primconvert) {
      debug_printf("D3D12: failed to create primconvert\n");
      return NULL;
   }

   d3d12_gfx_pipeline_state_cache_init(ctx);
   d3d12_root_signature_cache_init(ctx);
   d3d12_gs_variant_cache_init(ctx);

   util_dl_library *d3d12_mod = util_dl_open(UTIL_DL_PREFIX "d3d12" UTIL_DL_EXT);
   if (!d3d12_mod) {
      debug_printf("D3D12: failed to load D3D12.DLL\n");
      return NULL;
   }
   ctx->D3D12SerializeVersionedRootSignature =
      (PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE)util_dl_get_proc_address(d3d12_mod, "D3D12SerializeVersionedRootSignature");

   if (FAILED(screen->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&ctx->cmdqueue_fence)))) {
      FREE(ctx);
      return NULL;
   }

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->batches); ++i) {
      if (!d3d12_init_batch(ctx, &ctx->batches[i])) {
         FREE(ctx);
         return NULL;
      }
   }
   d3d12_start_batch(ctx, &ctx->batches[0]);

   ctx->sampler_pool = d3d12_descriptor_pool_new(screen,
                                                 D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                                 64);
   if (!ctx->sampler_pool) {
      FREE(ctx);
      return NULL;
   }
   d3d12_init_null_sampler(ctx);

   ctx->validation_tools = d3d12_validator_create();

   ctx->blitter = util_blitter_create(&ctx->base);
   if (!ctx->blitter)
      return NULL;

   ctx->resource_state_manager = new ResourceStateManager();

   if (!d3d12_init_polygon_stipple(&ctx->base)) {
      debug_printf("D3D12: failed to initialize polygon stipple resources\n");
      FREE(ctx);
      return NULL;
   }

   return &ctx->base;
}

bool
d3d12_need_zero_one_depth_range(struct d3d12_context *ctx)
{
   struct d3d12_shader_selector *fs = ctx->gfx_stages[PIPE_SHADER_FRAGMENT];

   /**
    * OpenGL Compatibility spec, section 15.2.3 (Shader Outputs) says
    * the following:
    *
    *    For fixed-point depth buffers, the final fragment depth written by
    *    a fragment shader is first clamped to [0, 1] and then converted to
    *    fixed-point as if it were a window z value (see section 13.8.1).
    *    For floating-point depth buffers, conversion is not performed but
    *    clamping is. Note that the depth range computation is not applied
    *    here, only the conversion to fixed-point.
    *
    * However, the D3D11.3 Functional Spec, section 17.10 (Depth Clamp) says
    * the following:
    *
    *    Depth values that reach the Output Merger, whether coming from
    *    interpolation or from Pixel Shader output (replacing the
    *    interpolated z), are always clamped:
    *    z = min(Viewport.MaxDepth,max(Viewport.MinDepth,z))
    *    following the D3D11 Floating Point Rules(3.1) for min/max.
    *
    * This means that we can't always use the fixed-function viewport-mapping
    * D3D provides.
    *
    * There's only one case where the difference matters: When the fragment
    * shader writes a non-implicit value to gl_FragDepth. In all other
    * cases, the fragment either shouldn't have been rasterized in the
    * first place, or the implicit gl_FragCoord.z-value should already have
    * been clamped to the depth-range.
    *
    * For simplicity, let's assume that an explicitly written frag-result
    * doesn't simply forward the value of gl_FragCoord.z. If it does, we'll
    * end up generating needless code, but the result will be correct.
    */

   return fs->initial->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_DEPTH);
}
