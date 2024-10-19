/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "hk_device_memory.h"
#include "hk_private.h"

#include "vk_buffer.h"

struct hk_device_memory;
struct hk_physical_device;

struct hk_buffer {
   struct vk_buffer vk;
   uint64_t addr;

   /** Reserved VA for sparse buffers, NULL otherwise. */
   struct agx_va *va;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(hk_buffer, vk.base, VkBuffer,
                               VK_OBJECT_TYPE_BUFFER)

static inline uint64_t
hk_buffer_address(const struct hk_buffer *buffer, uint64_t offset)
{
   return buffer->addr + offset;
}

static inline struct hk_addr_range
hk_buffer_addr_range(const struct hk_buffer *buffer, uint64_t offset,
                     uint64_t range)
{
   /* If range == 0, return a NULL pointer. Thanks to soft fault, that allows
    * eliding robustness2 bounds checks for index = 0, as the bottom of VA space
    * is reserved.
    */
   if (buffer == NULL || range == 0)
      return (struct hk_addr_range){.range = 0};

   return (struct hk_addr_range){
      .addr = hk_buffer_address(buffer, offset),
      .range = vk_buffer_range(&buffer->vk, offset, range),
   };
}
