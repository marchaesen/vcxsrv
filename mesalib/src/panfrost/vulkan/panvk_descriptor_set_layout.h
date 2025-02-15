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

   /* if textures are present, maximum number of planes required per texture;
    * 0 otherwise
    */
   unsigned textures_per_desc;

   /* if samplers are present, maximum number of planes required per sampler;
    * 0 otherwise
    */
   unsigned samplers_per_desc;

   struct panvk_sampler **immutable_samplers;
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
panvk_get_desc_stride(const struct panvk_descriptor_set_binding_layout *layout)
{
   /* One descriptor for each sampler plane, and one for each texture. */
   return layout->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
      ? layout->textures_per_desc + layout->samplers_per_desc : 1;
}

struct panvk_subdesc_info {
   VkDescriptorType type;
   uint8_t plane;
};

#define IMPLICIT_SUBDESC_TYPE (VkDescriptorType)-1
#define NO_SUBDESC (struct panvk_subdesc_info){ \
   .type = IMPLICIT_SUBDESC_TYPE, \
}
#define TEX_SUBDESC(__plane) (struct panvk_subdesc_info){ \
   .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, \
   .plane = __plane, \
}
#define SAMPLER_SUBDESC(__plane) (struct panvk_subdesc_info){ \
   .type = VK_DESCRIPTOR_TYPE_SAMPLER, \
   .plane = __plane, \
}

static inline struct panvk_subdesc_info
get_tex_subdesc_info(VkDescriptorType type, uint8_t plane)
{
   return (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
      ? TEX_SUBDESC(plane) : NO_SUBDESC;
}

static inline struct panvk_subdesc_info
get_sampler_subdesc_info(VkDescriptorType type, uint8_t plane)
{
   return (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
      ? SAMPLER_SUBDESC(plane) : NO_SUBDESC;
}

static inline uint32_t
get_subdesc_idx(const struct panvk_descriptor_set_binding_layout *layout,
                struct panvk_subdesc_info subdesc)
{
   assert((subdesc.type == IMPLICIT_SUBDESC_TYPE) ||
          (layout->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
           (subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLER ||
            subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)));

   uint32_t subdesc_idx = 0;

   /* In case of combined image-sampler, we put the texture first. */
   if (subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLER)
      subdesc_idx += layout->textures_per_desc +
                     MIN2(subdesc.plane, layout->samplers_per_desc - 1);
   else if (subdesc.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
      subdesc_idx += MIN2(subdesc.plane, layout->textures_per_desc - 1);

   return subdesc_idx;
}

static inline uint32_t
panvk_get_desc_index(const struct panvk_descriptor_set_binding_layout *layout,
                     uint32_t elem, struct panvk_subdesc_info subdesc)
{
   assert(!vk_descriptor_type_is_dynamic(layout->type));

   return layout->desc_idx + elem * panvk_get_desc_stride(layout) +
      get_subdesc_idx(layout, subdesc);
}

#endif /* PANVK_VX_DESCRIPTOR_SET_LAYOUT_H */
