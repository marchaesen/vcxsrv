/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asahi/lib/agx_device.h"
#include <sys/types.h>
#include "hk_private.h"
#include "vk_physical_device.h"
#include "vk_sync.h"
#include "wsi_common.h"

struct hk_instance;
struct hk_physical_device;

struct hk_queue_family {
   VkQueueFlags queue_flags;
   uint32_t queue_count;
};

struct hk_memory_heap {
   uint64_t size;
   uint64_t used;
   VkMemoryHeapFlags flags;
   uint64_t (*available)(struct hk_physical_device *pdev);
};

struct hk_physical_device {
   struct vk_physical_device vk;
   dev_t render_dev;
   int master_fd;

   /* Only used for VK_EXT_memory_budget */
   struct agx_device dev;

   struct wsi_device wsi_device;

   uint8_t device_uuid[VK_UUID_SIZE];

   // TODO: add mapable VRAM heap if possible
   struct hk_memory_heap mem_heaps[3];
   VkMemoryType mem_types[3];
   uint8_t mem_heap_count;
   uint8_t mem_type_count;
   uint64_t sysmem;

   struct hk_queue_family queue_families[3];
   uint8_t queue_family_count;

   struct vk_sync_type syncobj_sync_type;
   const struct vk_sync_type *sync_types[2];

   simple_mtx_t debug_compile_lock;
};

VK_DEFINE_HANDLE_CASTS(hk_physical_device, vk.base, VkPhysicalDevice,
                       VK_OBJECT_TYPE_PHYSICAL_DEVICE)

static inline struct hk_instance *
hk_physical_device_instance(struct hk_physical_device *pdev)
{
   return (struct hk_instance *)pdev->vk.instance;
}

VkResult hk_create_drm_physical_device(struct vk_instance *vk_instance,
                                       struct _drmDevice *drm_device,
                                       struct vk_physical_device **pdev_out);

void hk_physical_device_destroy(struct vk_physical_device *vk_device);

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) ||                                    \
   defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_XLIB_KHR) ||    \
   defined(VK_USE_PLATFORM_DISPLAY_KHR)
#define HK_USE_WSI_PLATFORM
#endif
