/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_WSI_H
#define VN_WSI_H

#include "vn_common.h"

#include "wsi_common.h"

#ifdef VN_USE_WSI_PLATFORM

VkResult
vn_wsi_init(struct vn_physical_device *physical_dev);

void
vn_wsi_fini(struct vn_physical_device *physical_dev);

VkResult
vn_wsi_create_scanout_image(struct vn_device *dev,
                            const VkImageCreateInfo *create_info,
                            const VkAllocationCallbacks *alloc,
                            struct vn_image **out_img);

#else

static inline VkResult
vn_wsi_init(UNUSED struct vn_physical_device *physical_dev)
{
   return VK_SUCCESS;
}

static inline void
vn_wsi_fini(UNUSED struct vn_physical_device *physical_dev)
{
}

#endif

#endif /* VN_WSI_H */
