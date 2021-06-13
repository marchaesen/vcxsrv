/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_state.h"

#include "zink_context.h"
#include "zink_screen.h"

#include "compiler/shader_enums.h"
#include "util/u_dual_blend.h"
#include "util/u_memory.h"

#include <math.h>

static void *
zink_create_vertex_elements_state(struct pipe_context *pctx,
                                  unsigned num_elements,
                                  const struct pipe_vertex_element *elements)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   unsigned int i;
   struct zink_vertex_elements_state *ves = CALLOC_STRUCT(zink_vertex_elements_state);
   if (!ves)
      return NULL;

   int buffer_map[PIPE_MAX_ATTRIBS];
   for (int i = 0; i < ARRAY_SIZE(buffer_map); ++i)
      buffer_map[i] = -1;

   int num_bindings = 0;
   for (i = 0; i < num_elements; ++i) {
      const struct pipe_vertex_element *elem = elements + i;

      int binding = elem->vertex_buffer_index;
      if (buffer_map[binding] < 0) {
         ves->binding_map[num_bindings] = binding;
         buffer_map[binding] = num_bindings++;
      }
      binding = buffer_map[binding];


      ves->bindings[binding].binding = binding;
      ves->bindings[binding].inputRate = elem->instance_divisor ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;

      assert(!elem->instance_divisor || zink_screen(pctx->screen)->info.have_EXT_vertex_attribute_divisor);
      ves->divisor[binding] = elem->instance_divisor;
      assert(elem->instance_divisor <= screen->info.vdiv_props.maxVertexAttribDivisor);

      ves->hw_state.attribs[i].binding = binding;
      ves->hw_state.attribs[i].location = i;
      ves->hw_state.attribs[i].format = zink_get_format(screen,
                                                        elem->src_format);
      assert(ves->hw_state.attribs[i].format != VK_FORMAT_UNDEFINED);
      ves->hw_state.attribs[i].offset = elem->src_offset;
   }

   ves->hw_state.num_bindings = num_bindings;
   ves->hw_state.num_attribs = num_elements;
   return ves;
}

static void
zink_bind_vertex_elements_state(struct pipe_context *pctx,
                                void *cso)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_gfx_pipeline_state *state = &ctx->gfx_pipeline_state;
   ctx->element_state = cso;
   state->dirty = true;
   state->divisors_present = 0;
   if (cso) {
      state->element_state = &ctx->element_state->hw_state;
      struct zink_vertex_elements_state *ves = cso;
      for (int i = 0; i < state->element_state->num_bindings; ++i) {
         state->bindings[i].binding = ves->bindings[i].binding;
         state->bindings[i].inputRate = ves->bindings[i].inputRate;
         if (ves->divisor[i]) {
            state->divisors[state->divisors_present].divisor = ves->divisor[i];
            state->divisors[state->divisors_present].binding = state->bindings[i].binding;
            state->divisors_present++;
         }
      }
   } else
     state->element_state = NULL;
}

static void
zink_delete_vertex_elements_state(struct pipe_context *pctx,
                                  void *ves)
{
   FREE(ves);
}

static VkBlendFactor
blend_factor(enum pipe_blendfactor factor)
{
   switch (factor) {
   case PIPE_BLENDFACTOR_ONE: return VK_BLEND_FACTOR_ONE;
   case PIPE_BLENDFACTOR_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
   case PIPE_BLENDFACTOR_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
   case PIPE_BLENDFACTOR_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
      return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
   case PIPE_BLENDFACTOR_CONST_COLOR: return VK_BLEND_FACTOR_CONSTANT_COLOR;
   case PIPE_BLENDFACTOR_CONST_ALPHA: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
   case PIPE_BLENDFACTOR_SRC1_COLOR: return VK_BLEND_FACTOR_SRC1_COLOR;
   case PIPE_BLENDFACTOR_SRC1_ALPHA: return VK_BLEND_FACTOR_SRC1_ALPHA;

   case PIPE_BLENDFACTOR_ZERO: return VK_BLEND_FACTOR_ZERO;

   case PIPE_BLENDFACTOR_INV_SRC_COLOR:
      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA:
      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_COLOR:
      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;

   case PIPE_BLENDFACTOR_INV_CONST_COLOR:
      return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
      return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
   case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
      return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
      return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
   }
   unreachable("unexpected blend factor");
}


static bool
need_blend_constants(enum pipe_blendfactor factor)
{
   switch (factor) {
   case PIPE_BLENDFACTOR_CONST_COLOR:
   case PIPE_BLENDFACTOR_CONST_ALPHA:
   case PIPE_BLENDFACTOR_INV_CONST_COLOR:
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
      return true;

   default:
      return false;
   }
}

static VkBlendOp
blend_op(enum pipe_blend_func func)
{
   switch (func) {
   case PIPE_BLEND_ADD: return VK_BLEND_OP_ADD;
   case PIPE_BLEND_SUBTRACT: return VK_BLEND_OP_SUBTRACT;
   case PIPE_BLEND_REVERSE_SUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
   case PIPE_BLEND_MIN: return VK_BLEND_OP_MIN;
   case PIPE_BLEND_MAX: return VK_BLEND_OP_MAX;
   }
   unreachable("unexpected blend function");
}

static VkLogicOp
logic_op(enum pipe_logicop func)
{
   switch (func) {
   case PIPE_LOGICOP_CLEAR: return VK_LOGIC_OP_CLEAR;
   case PIPE_LOGICOP_NOR: return VK_LOGIC_OP_NOR;
   case PIPE_LOGICOP_AND_INVERTED: return VK_LOGIC_OP_AND_INVERTED;
   case PIPE_LOGICOP_COPY_INVERTED: return VK_LOGIC_OP_COPY_INVERTED;
   case PIPE_LOGICOP_AND_REVERSE: return VK_LOGIC_OP_AND_REVERSE;
   case PIPE_LOGICOP_INVERT: return VK_LOGIC_OP_INVERT;
   case PIPE_LOGICOP_XOR: return VK_LOGIC_OP_XOR;
   case PIPE_LOGICOP_NAND: return VK_LOGIC_OP_NAND;
   case PIPE_LOGICOP_AND: return VK_LOGIC_OP_AND;
   case PIPE_LOGICOP_EQUIV: return VK_LOGIC_OP_EQUIVALENT;
   case PIPE_LOGICOP_NOOP: return VK_LOGIC_OP_NO_OP;
   case PIPE_LOGICOP_OR_INVERTED: return VK_LOGIC_OP_OR_INVERTED;
   case PIPE_LOGICOP_COPY: return VK_LOGIC_OP_COPY;
   case PIPE_LOGICOP_OR_REVERSE: return VK_LOGIC_OP_OR_REVERSE;
   case PIPE_LOGICOP_OR: return VK_LOGIC_OP_OR;
   case PIPE_LOGICOP_SET: return VK_LOGIC_OP_SET;
   }
   unreachable("unexpected logicop function");
}

/* from iris */
static enum pipe_blendfactor
fix_blendfactor(enum pipe_blendfactor f, bool alpha_to_one)
{
   if (alpha_to_one) {
      if (f == PIPE_BLENDFACTOR_SRC1_ALPHA)
         return PIPE_BLENDFACTOR_ONE;

      if (f == PIPE_BLENDFACTOR_INV_SRC1_ALPHA)
         return PIPE_BLENDFACTOR_ZERO;
   }

   return f;
}

static void *
zink_create_blend_state(struct pipe_context *pctx,
                        const struct pipe_blend_state *blend_state)
{
   struct zink_blend_state *cso = CALLOC_STRUCT(zink_blend_state);
   if (!cso)
      return NULL;

   if (blend_state->logicop_enable) {
      cso->logicop_enable = VK_TRUE;
      cso->logicop_func = logic_op(blend_state->logicop_func);
   }

   /* TODO: figure out what to do with dither (nothing is probably "OK" for now,
    *       as dithering is undefined in GL
    */

   /* TODO: these are multisampling-state, and should be set there instead of
    *       here, as that's closer tied to the update-frequency
    */
   cso->alpha_to_coverage = blend_state->alpha_to_coverage;
   cso->alpha_to_one = blend_state->alpha_to_one;

   cso->need_blend_constants = false;

   for (int i = 0; i < PIPE_MAX_COLOR_BUFS; ++i) {
      const struct pipe_rt_blend_state *rt = blend_state->rt;
      if (blend_state->independent_blend_enable)
         rt = blend_state->rt + i;

      VkPipelineColorBlendAttachmentState att = { };

      if (rt->blend_enable) {
         att.blendEnable = VK_TRUE;
         att.srcColorBlendFactor = blend_factor(fix_blendfactor(rt->rgb_src_factor, cso->alpha_to_one));
         att.dstColorBlendFactor = blend_factor(fix_blendfactor(rt->rgb_dst_factor, cso->alpha_to_one));
         att.colorBlendOp = blend_op(rt->rgb_func);
         att.srcAlphaBlendFactor = blend_factor(fix_blendfactor(rt->alpha_src_factor, cso->alpha_to_one));
         att.dstAlphaBlendFactor = blend_factor(fix_blendfactor(rt->alpha_dst_factor, cso->alpha_to_one));
         att.alphaBlendOp = blend_op(rt->alpha_func);

         if (need_blend_constants(rt->rgb_src_factor) ||
             need_blend_constants(rt->rgb_dst_factor) ||
             need_blend_constants(rt->alpha_src_factor) ||
             need_blend_constants(rt->alpha_dst_factor))
            cso->need_blend_constants = true;
      }

      if (rt->colormask & PIPE_MASK_R)
         att.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
      if (rt->colormask & PIPE_MASK_G)
         att.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
      if (rt->colormask & PIPE_MASK_B)
         att.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
      if (rt->colormask & PIPE_MASK_A)
         att.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;

      cso->attachments[i] = att;
   }
   cso->dual_src_blend = util_blend_state_is_dual(blend_state, 0);

   return cso;
}

static void
zink_bind_blend_state(struct pipe_context *pctx, void *cso)
{
   struct zink_gfx_pipeline_state* state = &zink_context(pctx)->gfx_pipeline_state;

   if (state->blend_state != cso) {
      state->blend_state = cso;
      state->dirty = true;
   }
}

static void
zink_delete_blend_state(struct pipe_context *pctx, void *blend_state)
{
   FREE(blend_state);
}

static VkCompareOp
compare_op(enum pipe_compare_func func)
{
   switch (func) {
   case PIPE_FUNC_NEVER: return VK_COMPARE_OP_NEVER;
   case PIPE_FUNC_LESS: return VK_COMPARE_OP_LESS;
   case PIPE_FUNC_EQUAL: return VK_COMPARE_OP_EQUAL;
   case PIPE_FUNC_LEQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
   case PIPE_FUNC_GREATER: return VK_COMPARE_OP_GREATER;
   case PIPE_FUNC_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
   case PIPE_FUNC_GEQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
   case PIPE_FUNC_ALWAYS: return VK_COMPARE_OP_ALWAYS;
   }
   unreachable("unexpected func");
}

static VkStencilOp
stencil_op(enum pipe_stencil_op op)
{
   switch (op) {
   case PIPE_STENCIL_OP_KEEP: return VK_STENCIL_OP_KEEP;
   case PIPE_STENCIL_OP_ZERO: return VK_STENCIL_OP_ZERO;
   case PIPE_STENCIL_OP_REPLACE: return VK_STENCIL_OP_REPLACE;
   case PIPE_STENCIL_OP_INCR: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
   case PIPE_STENCIL_OP_DECR: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
   case PIPE_STENCIL_OP_INCR_WRAP: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
   case PIPE_STENCIL_OP_DECR_WRAP: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
   case PIPE_STENCIL_OP_INVERT: return VK_STENCIL_OP_INVERT;
   }
   unreachable("unexpected op");
}

static VkStencilOpState
stencil_op_state(const struct pipe_stencil_state *src)
{
   VkStencilOpState ret;
   ret.failOp = stencil_op(src->fail_op);
   ret.passOp = stencil_op(src->zpass_op);
   ret.depthFailOp = stencil_op(src->zfail_op);
   ret.compareOp = compare_op(src->func);
   ret.compareMask = src->valuemask;
   ret.writeMask = src->writemask;
   ret.reference = 0; // not used: we'll use a dynamic state for this
   return ret;
}

static void *
zink_create_depth_stencil_alpha_state(struct pipe_context *pctx,
                                      const struct pipe_depth_stencil_alpha_state *depth_stencil_alpha)
{
   struct zink_depth_stencil_alpha_state *cso = CALLOC_STRUCT(zink_depth_stencil_alpha_state);
   if (!cso)
      return NULL;

   cso->base = *depth_stencil_alpha;

   if (depth_stencil_alpha->depth_enabled) {
      cso->hw_state.depth_test = VK_TRUE;
      cso->hw_state.depth_compare_op = compare_op(depth_stencil_alpha->depth_func);
   }

   if (depth_stencil_alpha->depth_bounds_test) {
      cso->hw_state.depth_bounds_test = VK_TRUE;
      cso->hw_state.min_depth_bounds = depth_stencil_alpha->depth_bounds_min;
      cso->hw_state.max_depth_bounds = depth_stencil_alpha->depth_bounds_max;
   }

   if (depth_stencil_alpha->stencil[0].enabled) {
      cso->hw_state.stencil_test = VK_TRUE;
      cso->hw_state.stencil_front = stencil_op_state(depth_stencil_alpha->stencil);
   }

   if (depth_stencil_alpha->stencil[1].enabled)
      cso->hw_state.stencil_back = stencil_op_state(depth_stencil_alpha->stencil + 1);
   else
      cso->hw_state.stencil_back = cso->hw_state.stencil_front;

   cso->hw_state.depth_write = depth_stencil_alpha->depth_writemask;

   return cso;
}

static void
zink_bind_depth_stencil_alpha_state(struct pipe_context *pctx, void *cso)
{
   struct zink_context *ctx = zink_context(pctx);

   ctx->dsa_state = cso;

   if (cso) {
      struct zink_gfx_pipeline_state *state = &ctx->gfx_pipeline_state;
      if (state->depth_stencil_alpha_state != &ctx->dsa_state->hw_state) {
         state->depth_stencil_alpha_state = &ctx->dsa_state->hw_state;
         state->dirty = true;
      }
   }
}

static void
zink_delete_depth_stencil_alpha_state(struct pipe_context *pctx,
                                      void *depth_stencil_alpha)
{
   FREE(depth_stencil_alpha);
}

static float
round_to_granularity(float value, float granularity)
{
   return roundf(value / granularity) * granularity;
}

static float
line_width(float width, float granularity, const float range[2])
{
   assert(granularity >= 0);
   assert(range[0] <= range[1]);

   if (granularity > 0)
      width = round_to_granularity(width, granularity);

   return CLAMP(width, range[0], range[1]);
}

static void *
zink_create_rasterizer_state(struct pipe_context *pctx,
                             const struct pipe_rasterizer_state *rs_state)
{
   struct zink_screen *screen = zink_screen(pctx->screen);

   struct zink_rasterizer_state *state = CALLOC_STRUCT(zink_rasterizer_state);
   if (!state)
      return NULL;

   state->base = *rs_state;

   assert(rs_state->depth_clip_far == rs_state->depth_clip_near);
   state->hw_state.depth_clamp = rs_state->depth_clip_near == 0;
   state->hw_state.rasterizer_discard = rs_state->rasterizer_discard;
   state->hw_state.force_persample_interp = rs_state->force_persample_interp;

   assert(rs_state->fill_front <= PIPE_POLYGON_MODE_POINT);
   if (rs_state->fill_back != rs_state->fill_front)
      debug_printf("BUG: vulkan doesn't support different front and back fill modes\n");
   state->hw_state.polygon_mode = (VkPolygonMode)rs_state->fill_front; // same values
   state->hw_state.cull_mode = (VkCullModeFlags)rs_state->cull_face; // same bits

   state->hw_state.front_face = rs_state->front_ccw ?
                                VK_FRONT_FACE_COUNTER_CLOCKWISE :
                                VK_FRONT_FACE_CLOCKWISE;

   state->offset_point = rs_state->offset_point;
   state->offset_line = rs_state->offset_line;
   state->offset_tri = rs_state->offset_tri;
   state->offset_units = rs_state->offset_units;
   state->offset_clamp = rs_state->offset_clamp;
   state->offset_scale = rs_state->offset_scale;

   state->line_width = line_width(rs_state->line_width,
                                  screen->info.props.limits.lineWidthGranularity,
                                  screen->info.props.limits.lineWidthRange);

   return state;
}

static void
zink_bind_rasterizer_state(struct pipe_context *pctx, void *cso)
{
   struct zink_context *ctx = zink_context(pctx);
   bool clip_halfz = ctx->rast_state ? ctx->rast_state->base.clip_halfz : false;
   bool point_quad_rasterization = ctx->rast_state ? ctx->rast_state->base.point_quad_rasterization : false;
   ctx->rast_state = cso;

   if (ctx->rast_state) {
      if (ctx->gfx_pipeline_state.rast_state != &ctx->rast_state->hw_state) {
         ctx->gfx_pipeline_state.rast_state = &ctx->rast_state->hw_state;
         ctx->gfx_pipeline_state.dirty = true;
      }

      if (clip_halfz != ctx->rast_state->base.clip_halfz)
         ctx->last_vertex_stage_dirty = true;

      if (ctx->line_width != ctx->rast_state->line_width) {
         ctx->line_width = ctx->rast_state->line_width;
         ctx->gfx_pipeline_state.dirty = true;
      }
      if (ctx->rast_state->base.point_quad_rasterization != point_quad_rasterization)
         ctx->dirty_shader_stages |= BITFIELD_BIT(PIPE_SHADER_FRAGMENT);
   }
}

static void
zink_delete_rasterizer_state(struct pipe_context *pctx, void *rs_state)
{
   FREE(rs_state);
}

void
zink_context_state_init(struct pipe_context *pctx)
{
   pctx->create_vertex_elements_state = zink_create_vertex_elements_state;
   pctx->bind_vertex_elements_state = zink_bind_vertex_elements_state;
   pctx->delete_vertex_elements_state = zink_delete_vertex_elements_state;

   pctx->create_blend_state = zink_create_blend_state;
   pctx->bind_blend_state = zink_bind_blend_state;
   pctx->delete_blend_state = zink_delete_blend_state;

   pctx->create_depth_stencil_alpha_state = zink_create_depth_stencil_alpha_state;
   pctx->bind_depth_stencil_alpha_state = zink_bind_depth_stencil_alpha_state;
   pctx->delete_depth_stencil_alpha_state = zink_delete_depth_stencil_alpha_state;

   pctx->create_rasterizer_state = zink_create_rasterizer_state;
   pctx->bind_rasterizer_state = zink_bind_rasterizer_state;
   pctx->delete_rasterizer_state = zink_delete_rasterizer_state;
}
