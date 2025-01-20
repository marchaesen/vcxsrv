/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_cmd_pool.h"
#include "asahi/lib/agx_bo.h"

#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_physical_device.h"

static VkResult
hk_cmd_bo_create(struct hk_cmd_pool *pool, bool usc, struct hk_cmd_bo **bo_out)
{
   struct hk_device *dev = hk_cmd_pool_device(pool);
   struct hk_cmd_bo *bo;

   bo = vk_zalloc(&pool->vk.alloc, sizeof(*bo), 8,
                  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (bo == NULL)
      return vk_error(pool, VK_ERROR_OUT_OF_HOST_MEMORY);

   bo->bo = agx_bo_create(&dev->dev, HK_CMD_BO_SIZE, 0, usc ? AGX_BO_LOW_VA : 0,
                          "Command pool");
   if (bo->bo == NULL) {
      vk_free(&pool->vk.alloc, bo);
      return vk_error(pool, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   bo->map = agx_bo_map(bo->bo);

   *bo_out = bo;
   return VK_SUCCESS;
}

static void
hk_cmd_bo_destroy(struct hk_cmd_pool *pool, struct hk_cmd_bo *bo)
{
   struct hk_device *dev = hk_cmd_pool_device(pool);
   agx_bo_unreference(&dev->dev, bo->bo);
   vk_free(&pool->vk.alloc, bo);
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CreateCommandPool(VkDevice _device,
                     const VkCommandPoolCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkCommandPool *pCmdPool)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   struct hk_cmd_pool *pool;

   pool = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result =
      vk_command_pool_init(&device->vk, &pool->vk, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pool);
      return result;
   }

   list_inithead(&pool->free_bos);
   list_inithead(&pool->free_usc_bos);

   *pCmdPool = hk_cmd_pool_to_handle(pool);

   return VK_SUCCESS;
}

static void
hk_cmd_pool_destroy_bos(struct hk_cmd_pool *pool)
{
   list_for_each_entry_safe(struct hk_cmd_bo, bo, &pool->free_bos, link)
      hk_cmd_bo_destroy(pool, bo);

   list_inithead(&pool->free_bos);

   list_for_each_entry_safe(struct hk_cmd_bo, bo, &pool->free_usc_bos, link)
      hk_cmd_bo_destroy(pool, bo);

   list_inithead(&pool->free_usc_bos);
}

VkResult
hk_cmd_pool_alloc_bo(struct hk_cmd_pool *pool, bool usc,
                     struct hk_cmd_bo **bo_out)
{
   struct hk_cmd_bo *bo = NULL;
   if (usc) {
      if (!list_is_empty(&pool->free_usc_bos)) {
         bo = list_first_entry(&pool->free_usc_bos, struct hk_cmd_bo, link);
         pool->num_free_usc_bos--;
      }
   } else {
      if (!list_is_empty(&pool->free_bos)) {
         bo = list_first_entry(&pool->free_bos, struct hk_cmd_bo, link);
         pool->num_free_bos--;
      }
   }
   if (bo) {
      list_del(&bo->link);
      *bo_out = bo;
      return VK_SUCCESS;
   }

   return hk_cmd_bo_create(pool, usc, bo_out);
}

void
hk_cmd_pool_free_bo_list(struct hk_cmd_pool *pool, struct list_head *bos)
{
   list_for_each_entry_safe(struct hk_cmd_bo, bo, bos, link) {
      list_del(&bo->link);
      if (pool->num_free_bos > HK_CMD_POOL_BO_MAX) {
         hk_cmd_bo_destroy(pool, bo);
      } else {
         list_addtail(&bo->link, &pool->free_bos);
         pool->num_free_bos++;
      }
   }
}

void
hk_cmd_pool_free_usc_bo_list(struct hk_cmd_pool *pool, struct list_head *bos)
{
   list_for_each_entry_safe(struct hk_cmd_bo, bo, bos, link) {
      list_del(&bo->link);
      if (pool->num_free_usc_bos > HK_CMD_POOL_BO_MAX) {
         hk_cmd_bo_destroy(pool, bo);
      } else {
         list_addtail(&bo->link, &pool->free_usc_bos);
         pool->num_free_usc_bos++;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
hk_DestroyCommandPool(VkDevice _device, VkCommandPool commandPool,
                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   VK_FROM_HANDLE(hk_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   vk_command_pool_finish(&pool->vk);
   hk_cmd_pool_destroy_bos(pool);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VKAPI_ATTR void VKAPI_CALL
hk_TrimCommandPool(VkDevice device, VkCommandPool commandPool,
                   VkCommandPoolTrimFlags flags)
{
   VK_FROM_HANDLE(hk_cmd_pool, pool, commandPool);

   vk_command_pool_trim(&pool->vk, flags);
   hk_cmd_pool_destroy_bos(pool);
}
