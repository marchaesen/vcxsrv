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

#ifndef ZINK_PIPELINE_H
#define ZINK_PIPELINE_H

#include <vulkan/vulkan.h>

#include "pipe/p_state.h"

struct zink_blend_state;
struct zink_depth_stencil_alpha_state;
struct zink_gfx_program;
struct zink_rasterizer_state;
struct zink_render_pass;
struct zink_screen;
struct zink_vertex_elements_state;

struct zink_gfx_pipeline_state {
   struct zink_render_pass *render_pass;

   struct zink_vertex_elements_hw_state *element_state;
   VkVertexInputBindingDescription bindings[PIPE_MAX_ATTRIBS]; // combination of element_state and stride
   VkVertexInputBindingDivisorDescriptionEXT divisors[PIPE_MAX_ATTRIBS];
   uint8_t divisors_present;

   uint32_t num_attachments;
   struct zink_blend_state *blend_state;

   struct zink_rasterizer_hw_state *rast_state;

   struct zink_depth_stencil_alpha_hw_state *depth_stencil_alpha_state;

   VkSampleMask sample_mask;
   uint8_t rast_samples;
   uint8_t vertices_per_patch;

   unsigned num_viewports;

   bool primitive_restart;

   VkShaderModule modules[PIPE_SHADER_TYPES - 1];

   /* Pre-hashed value for table lookup, invalid when zero.
    * Members after this point are not included in pipeline state hash key */
   uint32_t hash;
   bool dirty;
};

VkPipeline
zink_create_gfx_pipeline(struct zink_screen *screen,
                         struct zink_gfx_program *prog,
                         struct zink_gfx_pipeline_state *state,
                         VkPrimitiveTopology primitive_topology);

#endif
