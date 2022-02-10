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

#ifndef ZINK_STATE_H
#define ZINK_STATE_H

#include <vulkan/vulkan.h>

#include "pipe/p_state.h"
#include "util/set.h"

struct zink_vertex_elements_hw_state {
   uint32_t hash;
   uint32_t num_bindings, num_attribs;
   union {
      VkVertexInputAttributeDescription attribs[PIPE_MAX_ATTRIBS];
      VkVertexInputAttributeDescription2EXT dynattribs[PIPE_MAX_ATTRIBS];
   };
   union {
      struct {
         VkVertexInputBindingDivisorDescriptionEXT divisors[PIPE_MAX_ATTRIBS];
         VkVertexInputBindingDescription bindings[PIPE_MAX_ATTRIBS]; // combination of element_state and stride
         uint8_t divisors_present;
      } b;
      VkVertexInputBindingDescription2EXT dynbindings[PIPE_MAX_ATTRIBS];
   };
};

struct zink_vertex_elements_state {
   struct {
      uint32_t binding;
      VkVertexInputRate inputRate;
   } bindings[PIPE_MAX_ATTRIBS];
   uint32_t divisor[PIPE_MAX_ATTRIBS];
   uint8_t binding_map[PIPE_MAX_ATTRIBS];
   uint32_t decomposed_attrs;
   unsigned decomposed_attrs_size;
   uint32_t decomposed_attrs_without_w;
   unsigned decomposed_attrs_without_w_size;
   struct zink_vertex_elements_hw_state hw_state;
};

struct zink_vertex_state {
   struct pipe_vertex_state b;
   struct zink_vertex_elements_state velems;
   struct set masks;
};

struct zink_rasterizer_hw_state {
   unsigned polygon_mode : 2; //VkPolygonMode
   unsigned cull_mode : 2; //VkCullModeFlags
   unsigned line_mode : 2; //VkLineRasterizationModeEXT
   unsigned depth_clamp:1;
   unsigned rasterizer_discard:1;
   unsigned pv_last:1;
   unsigned line_stipple_enable:1;
   unsigned force_persample_interp:1;
   unsigned clip_halfz:1;
};
#define ZINK_RAST_HW_STATE_SIZE 12


struct zink_rasterizer_state {
   struct pipe_rasterizer_state base;
   bool offset_point, offset_line, offset_tri;
   float offset_units, offset_clamp, offset_scale;
   float line_width;
   VkFrontFace front_face;
   struct zink_rasterizer_hw_state hw_state;
};

struct zink_blend_state {
   uint32_t hash;
   VkPipelineColorBlendAttachmentState attachments[PIPE_MAX_COLOR_BUFS];

   VkBool32 logicop_enable;
   VkLogicOp logicop_func;

   VkBool32 alpha_to_coverage;
   VkBool32 alpha_to_one;

   bool need_blend_constants;
   bool dual_src_blend;
};

struct zink_depth_stencil_alpha_hw_state {
   VkBool32 depth_test;
   VkCompareOp depth_compare_op;

   VkBool32 depth_bounds_test;
   float min_depth_bounds, max_depth_bounds;

   VkBool32 stencil_test;
   VkStencilOpState stencil_front;
   VkStencilOpState stencil_back;

   VkBool32 depth_write;
};

struct zink_depth_stencil_alpha_state {
   struct pipe_depth_stencil_alpha_state base;
   struct zink_depth_stencil_alpha_hw_state hw_state;
};

#ifdef __cplusplus
extern "C" {
#endif

void
zink_context_state_init(struct pipe_context *pctx);


struct pipe_vertex_state *
zink_create_vertex_state(struct pipe_screen *pscreen,
                          struct pipe_vertex_buffer *buffer,
                          const struct pipe_vertex_element *elements,
                          unsigned num_elements,
                          struct pipe_resource *indexbuf,
                          uint32_t full_velem_mask);
void
zink_vertex_state_destroy(struct pipe_screen *pscreen, struct pipe_vertex_state *vstate);
struct pipe_vertex_state *
zink_cache_create_vertex_state(struct pipe_screen *pscreen,
                               struct pipe_vertex_buffer *buffer,
                               const struct pipe_vertex_element *elements,
                               unsigned num_elements,
                               struct pipe_resource *indexbuf,
                               uint32_t full_velem_mask);
void
zink_cache_vertex_state_destroy(struct pipe_screen *pscreen, struct pipe_vertex_state *vstate);

const struct zink_vertex_elements_hw_state *
zink_vertex_state_mask(struct pipe_vertex_state *vstate, uint32_t partial_velem_mask, bool have_EXT_vertex_input_dynamic_state);

#ifdef __cplusplus
}
#endif

#endif
