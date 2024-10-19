/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_ALLOC_H
#define PANVK_CMD_ALLOC_H

#include "panvk_cmd_buffer.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"

static inline struct panfrost_ptr
panvk_cmd_alloc_from_pool(struct panvk_cmd_buffer *cmdbuf,
                          struct panvk_pool *pool,
                          struct panvk_pool_alloc_info info)
{
   if (!info.size)
      return (struct panfrost_ptr){0};

   struct panfrost_ptr ptr =
      pan_pool_alloc_aligned(&pool->base, info.size, info.alignment);

   if (!ptr.gpu) {
      VkResult error =
         panvk_catch_indirect_alloc_failure(VK_ERROR_OUT_OF_DEVICE_MEMORY);
      vk_command_buffer_set_error(&cmdbuf->vk, error);
   }

   return ptr;
}

#define panvk_cmd_alloc_dev_mem(__cmdbuf, __poolnm, __sz, __alignment)         \
   panvk_cmd_alloc_from_pool(__cmdbuf, &(__cmdbuf)->__poolnm##_pool,           \
                             (struct panvk_pool_alloc_info){                   \
                                .size = __sz,                                  \
                                .alignment = __alignment,                      \
                             })

#define panvk_cmd_alloc_desc_aggregate(__cmdbuf, ...)                          \
   panvk_cmd_alloc_from_pool(                                                  \
      __cmdbuf, &(__cmdbuf)->desc_pool,                                        \
      panvk_pool_descs_to_alloc_info(PAN_DESC_AGGREGATE(__VA_ARGS__)))

#define panvk_cmd_alloc_desc(__cmdbuf, __desc)                                 \
   panvk_cmd_alloc_desc_aggregate(__cmdbuf, PAN_DESC(__desc))

#define panvk_cmd_alloc_desc_array(__cmdbuf, __count, __desc)                  \
   panvk_cmd_alloc_desc_aggregate(__cmdbuf, PAN_DESC_ARRAY(__count, __desc))

#endif
