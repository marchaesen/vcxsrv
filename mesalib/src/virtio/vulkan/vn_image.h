/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_IMAGE_H
#define VN_IMAGE_H

#include "vn_common.h"

/* changing this to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR disables ownership
 * transfers and can be useful for debugging
 */
#define VN_PRESENT_SRC_INTERNAL_LAYOUT VK_IMAGE_LAYOUT_GENERAL

struct vn_image_create_deferred_info {
   VkImageCreateInfo create;
   VkImageFormatListCreateInfo list;
   VkImageStencilUsageCreateInfo stencil;
};

struct vn_image {
   struct vn_object_base base;

   VkSharingMode sharing_mode;

   VkMemoryRequirements2 memory_requirements[4];
   VkMemoryDedicatedRequirements dedicated_requirements[4];

   bool is_wsi;
   bool is_prime_blit_src;

   /* For VK_ANDROID_native_buffer, the WSI image owns the memory, */
   VkDeviceMemory private_memory;
   /* For VK_ANDROID_external_memory_android_hardware_buffer, real image
    * creation is deferred until bind image memory.
    */
   struct vn_image_create_deferred_info *deferred_info;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_image,
                               base.base,
                               VkImage,
                               VK_OBJECT_TYPE_IMAGE)

struct vn_image_view {
   struct vn_object_base base;

   const struct vn_image *image;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_image_view,
                               base.base,
                               VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW)

struct vn_sampler {
   struct vn_object_base base;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_sampler,
                               base.base,
                               VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)

struct vn_sampler_ycbcr_conversion {
   struct vn_object_base base;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_sampler_ycbcr_conversion,
                               base.base,
                               VkSamplerYcbcrConversion,
                               VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION)

VkResult
vn_image_create(struct vn_device *dev,
                const VkImageCreateInfo *create_info,
                const VkAllocationCallbacks *alloc,
                struct vn_image **out_img);

VkResult
vn_image_init_deferred(struct vn_device *dev,
                       const VkImageCreateInfo *create_info,
                       struct vn_image *img);

VkResult
vn_image_create_deferred(struct vn_device *dev,
                         const VkImageCreateInfo *create_info,
                         const VkAllocationCallbacks *alloc,
                         struct vn_image **out_img);

#endif /* VN_IMAGE_H */
