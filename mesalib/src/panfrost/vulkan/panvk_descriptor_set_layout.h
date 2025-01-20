/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_DESCRIPTOR_SET_LAYOUT_H
#define PANVK_DESCRIPTOR_SET_LAYOUT_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vk_descriptor_set_layout.h"
#include "vk_util.h"

#include "util/mesa-blake3.h"

#include "genxml/gen_macros.h"

#define PANVK_DESCRIPTOR_SIZE       32
#define MAX_DYNAMIC_UNIFORM_BUFFERS 16
#define MAX_DYNAMIC_STORAGE_BUFFERS 8
#define MAX_PUSH_DESCS              32
#define MAX_DYNAMIC_BUFFERS                                                    \
   (MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS)

#if PAN_ARCH <= 7
#define MAX_SETS 4
#else
#define MAX_SETS 15
#endif

struct panvk_descriptor_set_binding_layout {
   VkDescriptorType type;
   VkDescriptorBindingFlags flags;
   unsigned desc_count;
   unsigned desc_idx;
   struct mali_sampler_packed *immutable_samplers;
};

struct panvk_descriptor_set_layout {
   struct vk_descriptor_set_layout vk;
   VkDescriptorSetLayoutCreateFlagBits flags;
   unsigned desc_count;
   unsigned dyn_buf_count;

   /* Number of bindings in this descriptor set */
   uint32_t binding_count;

   /* Bindings in this descriptor set */
   struct panvk_descriptor_set_binding_layout *bindings;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_descriptor_set_layout, vk.base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)

static inline const struct panvk_descriptor_set_layout *
to_panvk_descriptor_set_layout(const struct vk_descriptor_set_layout *layout)
{
   return container_of(layout, const struct panvk_descriptor_set_layout, vk);
}

static inline const uint32_t
panvk_get_desc_stride(VkDescriptorType type)
{
   /* One descriptor for the sampler, and one for the texture. */
   return type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ? 2 : 1;
}

static inline uint32_t
panvk_get_desc_index(const struct panvk_descriptor_set_binding_layout *layout,
                     uint32_t elem, VkDescriptorType type)
{
   assert(layout->type == type ||
          (layout->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
           (type == VK_DESCRIPTOR_TYPE_SAMPLER ||
            type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)));

   assert(!vk_descriptor_type_is_dynamic(layout->type));

   uint32_t desc_idx =
      layout->desc_idx + elem * panvk_get_desc_stride(layout->type);

   /* In case of combined image-sampler, we put the texture first. */
   if (layout->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
       type == VK_DESCRIPTOR_TYPE_SAMPLER)
      desc_idx++;

   return desc_idx;
}

#endif /* PANVK_VX_DESCRIPTOR_SET_LAYOUT_H */
