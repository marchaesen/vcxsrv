/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_DEVICE_MEMORY_H
#define VN_DEVICE_MEMORY_H

#include "vn_common.h"

struct vn_device_memory_pool {
   mtx_t mutex;
   struct vn_device_memory *memory;
   VkDeviceSize used;
};

struct vn_device_memory {
   struct vn_object_base base;

   VkDeviceSize size;

   /* non-NULL when suballocated */
   struct vn_device_memory *base_memory;
   /* non-NULL when mappable or external */
   struct vn_renderer_bo *base_bo;
   VkDeviceSize base_offset;

   VkDeviceSize map_end;

   /* non-NULL when backed by AHB */
   struct AHardwareBuffer *ahb;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_device_memory,
                               base.base,
                               VkDeviceMemory,
                               VK_OBJECT_TYPE_DEVICE_MEMORY)

void
vn_device_memory_pool_fini(struct vn_device *dev, uint32_t mem_type_index);

VkResult
vn_device_memory_import_dma_buf(struct vn_device *dev,
                                struct vn_device_memory *mem,
                                const VkMemoryAllocateInfo *alloc_info,
                                bool force_unmappable,
                                int fd);

VkResult
vn_get_memory_dma_buf_properties(struct vn_device *dev,
                                 int fd,
                                 uint64_t *out_alloc_size,
                                 uint32_t *out_mem_type_bits);

#endif /* VN_DEVICE_MEMORY_H */
