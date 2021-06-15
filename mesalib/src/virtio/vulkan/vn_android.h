/*
 * Copyright 2021 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_ANDROID_H
#define VN_ANDROID_H

#include "vn_common.h"

#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vulkan.h>

/* venus implements VK_ANDROID_native_buffer up to spec version 7 */
#define VN_ANDROID_NATIVE_BUFFER_SPEC_VERSION 7

struct vn_device;
struct vn_image;

struct vn_android_wsi {
   /* command pools, one per queue family */
   VkCommandPool *cmd_pools;
   /* use one lock to simplify */
   mtx_t cmd_pools_lock;
   /* for forcing VK_SHARING_MODE_CONCURRENT */
   uint32_t *queue_family_indices;
};

VkResult
vn_image_from_anb(struct vn_device *dev,
                  const VkImageCreateInfo *image_info,
                  const VkNativeBufferANDROID *anb_info,
                  const VkAllocationCallbacks *alloc,
                  struct vn_image **out_img);

#ifdef ANDROID
VkResult
vn_android_wsi_init(struct vn_device *dev,
                    const VkAllocationCallbacks *alloc);

void
vn_android_wsi_fini(struct vn_device *dev,
                    const VkAllocationCallbacks *alloc);
#else
static inline VkResult
vn_android_wsi_init(UNUSED struct vn_device *dev,
                    UNUSED const VkAllocationCallbacks *alloc)
{
   return VK_SUCCESS;
}

static inline void
vn_android_wsi_fini(UNUSED struct vn_device *dev,
                    UNUSED const VkAllocationCallbacks *alloc)
{
   return;
}
#endif

#endif /* VN_ANDROID_H */
