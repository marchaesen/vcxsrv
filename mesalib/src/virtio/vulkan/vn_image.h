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

enum {
   VN_IMAGE_OWNERSHIP_ACQUIRE = 0,
   VN_IMAGE_OWNERSHIP_RELEASE = 1,
};

struct vn_image_ownership_cmds {
   VkCommandBuffer cmds[2];
};

struct vn_image {
   struct vn_object_base base;

   VkMemoryRequirements2 memory_requirements[4];
   VkMemoryDedicatedRequirements dedicated_requirements[4];
   /* For VK_ANDROID_native_buffer, the WSI image owns the memory, */
   VkDeviceMemory private_memory;
   /* For queue family ownership transfer of WSI images */
   VkSharingMode sharing_mode;
   struct vn_image_ownership_cmds *ownership_cmds;
   struct vn_queue *acquire_queue;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_image,
                               base.base,
                               VkImage,
                               VK_OBJECT_TYPE_IMAGE)

struct vn_image_view {
   struct vn_object_base base;
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
vn_image_android_wsi_init(struct vn_device *dev,
                          struct vn_image *img,
                          const VkAllocationCallbacks *alloc);

#endif /* VN_IMAGE_H */
