/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "hk_private.h"

#include "vk_command_pool.h"

#define HK_CMD_BO_SIZE     1024 * 128
#define HK_CMD_POOL_BO_MAX 32

/* Recyclable command buffer BO, used for both push buffers and upload */
struct hk_cmd_bo {
   struct agx_bo *bo;

   void *map;

   /** Link in hk_cmd_pool::free_bos or hk_cmd_buffer::bos */
   struct list_head link;
};

struct hk_cmd_pool {
   struct vk_command_pool vk;

   /** List of hk_cmd_bo */
   struct list_head free_bos;
   struct list_head free_usc_bos;
   uint32_t num_free_bos;
   uint32_t num_free_usc_bos;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(hk_cmd_pool, vk.base, VkCommandPool,
                               VK_OBJECT_TYPE_COMMAND_POOL)

static inline struct hk_device *
hk_cmd_pool_device(struct hk_cmd_pool *pool)
{
   return (struct hk_device *)pool->vk.base.device;
}

VkResult hk_cmd_pool_alloc_bo(struct hk_cmd_pool *pool, bool force_usc,
                              struct hk_cmd_bo **bo_out);

void hk_cmd_pool_free_bo_list(struct hk_cmd_pool *pool, struct list_head *bos);
void hk_cmd_pool_free_usc_bo_list(struct hk_cmd_pool *pool,
                                  struct list_head *bos);
