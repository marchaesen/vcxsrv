/*
 * Copyright © 2019 Red Hat.
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
 */

#pragma once

static inline unsigned vk_cull_to_pipe(uint32_t vk_cull)
{
   /* these correspond */
   return vk_cull;
}

static inline unsigned vk_polygon_mode_to_pipe(uint32_t vk_poly_mode)
{
   /* these correspond */
   return vk_poly_mode;
}

static inline unsigned vk_conv_stencil_op(uint32_t vk_stencil_op)
{
   switch (vk_stencil_op) {
   case VK_STENCIL_OP_KEEP:
      return PIPE_STENCIL_OP_KEEP;
   case VK_STENCIL_OP_ZERO:
      return PIPE_STENCIL_OP_ZERO;
   case VK_STENCIL_OP_REPLACE:
      return PIPE_STENCIL_OP_REPLACE;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP:
      return PIPE_STENCIL_OP_INCR;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP:
      return PIPE_STENCIL_OP_DECR;
   case VK_STENCIL_OP_INVERT:
      return PIPE_STENCIL_OP_INVERT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP:
      return PIPE_STENCIL_OP_INCR_WRAP;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP:
      return PIPE_STENCIL_OP_DECR_WRAP;
   default:
      assert(0);
      return 0;
   }
}

static inline unsigned vk_conv_topology(VkPrimitiveTopology topology)
{
   switch (topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return PIPE_PRIM_POINTS;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return PIPE_PRIM_LINES;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return PIPE_PRIM_LINE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      return PIPE_PRIM_TRIANGLES;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return PIPE_PRIM_TRIANGLE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return PIPE_PRIM_TRIANGLE_FAN;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
      return PIPE_PRIM_LINES_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
      return PIPE_PRIM_LINE_STRIP_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
      return PIPE_PRIM_TRIANGLES_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
      return PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY;
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      return PIPE_PRIM_PATCHES;
   default:
      assert(0);
      return 0;
   }
}

static inline unsigned vk_conv_wrap_mode(enum VkSamplerAddressMode addr_mode)
{
   switch (addr_mode) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT:
      return PIPE_TEX_WRAP_REPEAT;
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
      return PIPE_TEX_WRAP_MIRROR_REPEAT;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
      return PIPE_TEX_WRAP_CLAMP_TO_EDGE;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
      return PIPE_TEX_WRAP_CLAMP_TO_BORDER;
   case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
      return PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE;
   default:
      assert(0);
      return 0;
   }
}

static inline unsigned vk_conv_blend_factor(enum VkBlendFactor vk_factor)
{
   switch (vk_factor) {
   case VK_BLEND_FACTOR_ZERO:
      return PIPE_BLENDFACTOR_ZERO;
   case VK_BLEND_FACTOR_ONE:
      return PIPE_BLENDFACTOR_ONE;
   case VK_BLEND_FACTOR_SRC_COLOR:
      return PIPE_BLENDFACTOR_SRC_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
      return PIPE_BLENDFACTOR_INV_SRC_COLOR;
   case VK_BLEND_FACTOR_DST_COLOR:
      return PIPE_BLENDFACTOR_DST_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
      return PIPE_BLENDFACTOR_INV_DST_COLOR;
   case VK_BLEND_FACTOR_SRC_ALPHA:
      return PIPE_BLENDFACTOR_SRC_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
      return PIPE_BLENDFACTOR_INV_SRC_ALPHA;
   case VK_BLEND_FACTOR_DST_ALPHA:
      return PIPE_BLENDFACTOR_DST_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
      return PIPE_BLENDFACTOR_INV_DST_ALPHA;
   case VK_BLEND_FACTOR_CONSTANT_COLOR:
      return PIPE_BLENDFACTOR_CONST_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
      return PIPE_BLENDFACTOR_INV_CONST_COLOR;
   case VK_BLEND_FACTOR_CONSTANT_ALPHA:
      return PIPE_BLENDFACTOR_CONST_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
      return PIPE_BLENDFACTOR_INV_CONST_ALPHA;
   case VK_BLEND_FACTOR_SRC1_COLOR:
      return PIPE_BLENDFACTOR_SRC1_COLOR;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
      return PIPE_BLENDFACTOR_INV_SRC1_COLOR;
   case VK_BLEND_FACTOR_SRC1_ALPHA:
      return PIPE_BLENDFACTOR_SRC1_ALPHA;
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
      return PIPE_BLENDFACTOR_INV_SRC1_ALPHA;
   case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
      return PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE;
   default:
      assert(0);
      return 0;
   }
}

static inline unsigned vk_conv_blend_func(enum VkBlendOp op)
{
   switch (op) {
   case VK_BLEND_OP_ADD:
      return PIPE_BLEND_ADD;
   case VK_BLEND_OP_SUBTRACT:
      return PIPE_BLEND_SUBTRACT;
   case VK_BLEND_OP_REVERSE_SUBTRACT:
      return PIPE_BLEND_REVERSE_SUBTRACT;
   case VK_BLEND_OP_MIN:
      return PIPE_BLEND_MIN;
   case VK_BLEND_OP_MAX:
      return PIPE_BLEND_MAX;
   default:
      assert(0);
      return 0;
   }
}

static inline enum pipe_swizzle vk_conv_swizzle(VkComponentSwizzle swiz)
{
   switch (swiz) {
   case VK_COMPONENT_SWIZZLE_ZERO:
      return PIPE_SWIZZLE_0;
   case VK_COMPONENT_SWIZZLE_ONE:
      return PIPE_SWIZZLE_1;
   case VK_COMPONENT_SWIZZLE_R:
      return PIPE_SWIZZLE_X;
   case VK_COMPONENT_SWIZZLE_G:
      return PIPE_SWIZZLE_Y;
   case VK_COMPONENT_SWIZZLE_B:
      return PIPE_SWIZZLE_Z;
   case VK_COMPONENT_SWIZZLE_A:
      return PIPE_SWIZZLE_W;
   case VK_COMPONENT_SWIZZLE_IDENTITY:
   default:
      return PIPE_SWIZZLE_NONE;
   }
}
