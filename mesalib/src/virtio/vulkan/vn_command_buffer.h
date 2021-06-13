/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_COMMAND_BUFFER_H
#define VN_COMMAND_BUFFER_H

#include "vn_common.h"

#include "vn_cs.h"

struct vn_command_pool {
   struct vn_object_base base;

   VkAllocationCallbacks allocator;
   struct list_head command_buffers;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_command_pool,
                               base.base,
                               VkCommandPool,
                               VK_OBJECT_TYPE_COMMAND_POOL)

enum vn_command_buffer_state {
   VN_COMMAND_BUFFER_STATE_INITIAL,
   VN_COMMAND_BUFFER_STATE_RECORDING,
   VN_COMMAND_BUFFER_STATE_EXECUTABLE,
   VN_COMMAND_BUFFER_STATE_INVALID,
};

struct vn_command_buffer {
   struct vn_object_base base;

   struct vn_device *device;

   /* for scrubbing VK_IMAGE_LAYOUT_PRESENT_SRC_KHR */
   VkAllocationCallbacks allocator;
   uint32_t image_barrier_count;
   VkImageMemoryBarrier *image_barriers;

   struct list_head head;

   enum vn_command_buffer_state state;
   struct vn_cs_encoder cs;
};
VK_DEFINE_HANDLE_CASTS(vn_command_buffer,
                       base.base,
                       VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

#endif /* VN_COMMAND_BUFFER_H */
