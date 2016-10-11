/*
 * Copyright © 2016 Bas Nieuwenhuizen
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

#ifndef RADV_DESCRIPTOR_SET_H
#define RADV_DESCRIPTOR_SET_H

#include <vulkan/vulkan.h>

#define MAX_SETS         8

struct radv_descriptor_set_binding_layout {
   VkDescriptorType type;

   /* Number of array elements in this binding */
   uint16_t array_size;

   uint16_t offset;
   uint16_t buffer_offset;
   uint16_t dynamic_offset_offset;

   /* redundant with the type, each for a single array element */
   uint16_t size;
   uint16_t buffer_count;
   uint16_t dynamic_offset_count;

   /* Immutable samplers (or NULL if no immutable samplers) */
   struct radv_sampler **immutable_samplers;
};

struct radv_descriptor_set_layout {
   /* Number of bindings in this descriptor set */
   uint16_t binding_count;

   /* Total size of the descriptor set with room for all array entries */
   uint16_t size;

   /* Shader stages affected by this descriptor set */
   uint16_t shader_stages;
   uint16_t dynamic_shader_stages;

   /* Number of buffers in this descriptor set */
   uint16_t buffer_count;

   /* Number of dynamic offsets used by this descriptor set */
   uint16_t dynamic_offset_count;

   /* Bindings in this descriptor set */
   struct radv_descriptor_set_binding_layout binding[0];
};

struct radv_pipeline_layout {
   struct {
      struct radv_descriptor_set_layout *layout;
      uint32_t size;
      uint32_t dynamic_offset_start;
   } set[MAX_SETS];

   uint32_t num_sets;
   uint32_t push_constant_size;
   uint32_t dynamic_offset_count;

   unsigned char sha1[20];
};

#endif /* RADV_DESCRIPTOR_SET_H */
