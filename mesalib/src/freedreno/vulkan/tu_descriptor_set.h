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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef TU_DESCRIPTOR_SET_H
#define TU_DESCRIPTOR_SET_H

#include <vulkan/vulkan.h>

/* The hardware supports 5 descriptor sets, but we reserve 1 for dynamic
 * descriptors and input attachments.
 */
#define MAX_SETS 4

struct tu_descriptor_set_binding_layout
{
   VkDescriptorType type;

   /* Number of array elements in this binding */
   uint32_t array_size;

   /* The size in bytes of each Vulkan descriptor. */
   uint32_t size;

   uint32_t offset;

   /* Index into the pDynamicOffsets array for dynamic descriptors, as well as
    * the array of dynamic descriptors (offsetted by
    * tu_pipeline_layout::set::dynamic_offset_start).
    */
   uint32_t dynamic_offset_offset;

   /* Offset in the tu_descriptor_set_layout of the immutable samplers, or 0
    * if there are no immutable samplers. */
   uint32_t immutable_samplers_offset;

   /* Offset in the tu_descriptor_set_layout of the ycbcr samplers, or 0
    * if there are no immutable samplers. */
   uint32_t ycbcr_samplers_offset;

   /* Shader stages that use this binding */
   uint32_t shader_stages;
};

struct tu_descriptor_set_layout
{
   struct vk_object_base base;

   /* Descriptor set layouts can be destroyed at almost any time */
   uint32_t ref_cnt;

   /* The create flags for this descriptor set layout */
   VkDescriptorSetLayoutCreateFlags flags;

   /* Number of bindings in this descriptor set */
   uint32_t binding_count;

   /* Total size of the descriptor set with room for all array entries */
   uint32_t size;

   /* Shader stages affected by this descriptor set */
   uint16_t shader_stages;

   /* Number of dynamic offsets used by this descriptor set */
   uint16_t dynamic_offset_count;

   /* A bitfield of which dynamic buffers are ubo's, to make the
    * descriptor-binding-time patching easier.
    */
   uint32_t dynamic_ubo;

   bool has_immutable_samplers;
   bool has_variable_descriptors;

   /* Bindings in this descriptor set */
   struct tu_descriptor_set_binding_layout binding[0];
};

struct tu_device;

void tu_descriptor_set_layout_destroy(struct tu_device *device,
                                      struct tu_descriptor_set_layout *layout);

static inline void
tu_descriptor_set_layout_ref(struct tu_descriptor_set_layout *layout)
{
   assert(layout && layout->ref_cnt >= 1);
   p_atomic_inc(&layout->ref_cnt);
}

static inline void
tu_descriptor_set_layout_unref(struct tu_device *device,
                               struct tu_descriptor_set_layout *layout)
{
   assert(layout && layout->ref_cnt >= 1);
   if (p_atomic_dec_zero(&layout->ref_cnt))
      tu_descriptor_set_layout_destroy(device, layout);
}

struct tu_pipeline_layout
{
   struct vk_object_base base;

   struct
   {
      struct tu_descriptor_set_layout *layout;
      uint32_t size;
      uint32_t dynamic_offset_start;
   } set[MAX_SETS];

   uint32_t num_sets;
   uint32_t push_constant_size;
   uint32_t dynamic_offset_count;
};

static inline const struct tu_sampler *
tu_immutable_samplers(const struct tu_descriptor_set_layout *set,
                      const struct tu_descriptor_set_binding_layout *binding)
{
   return (void *) ((const char *) set + binding->immutable_samplers_offset);
}

static inline const struct tu_sampler_ycbcr_conversion *
tu_immutable_ycbcr_samplers(const struct tu_descriptor_set_layout *set,
                            const struct tu_descriptor_set_binding_layout *binding)
{
   if (!binding->ycbcr_samplers_offset)
      return NULL;

   return (void *) ((const char *) set + binding->ycbcr_samplers_offset);
}

#endif /* TU_DESCRIPTOR_SET_H */
