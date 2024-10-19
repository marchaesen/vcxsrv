/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_buffer.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_event.h"
#include "panvk_mempool.h"

#include "vk_log.h"

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateEvent)(VkDevice _device,
                            const VkEventCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkEvent *pEvent)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_event *event = vk_object_zalloc(
      &device->vk, pAllocator, sizeof(*event), VK_OBJECT_TYPE_EVENT);
   if (!event)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct panvk_pool_alloc_info info = {
      .size = sizeof(struct panvk_cs_sync32) * PANVK_SUBQUEUE_COUNT,
      .alignment = 64,
   };

   event->syncobjs = panvk_pool_alloc_mem(&device->mempools.rw_nc, info);
   if (!panvk_priv_mem_host_addr(event->syncobjs)) {
      vk_object_free(&device->vk, pAllocator, event);
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   memset(panvk_priv_mem_host_addr(event->syncobjs), 0,
          sizeof(struct panvk_cs_sync32) * PANVK_SUBQUEUE_COUNT);

   *pEvent = panvk_event_to_handle(event);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroyEvent)(VkDevice _device, VkEvent _event,
                             const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_event, event, _event);

   if (!event)
      return;

   panvk_pool_free_mem(&event->syncobjs);

   vk_object_free(&device->vk, pAllocator, event);
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(GetEventStatus)(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(panvk_event, event, _event);

   struct panvk_cs_sync32 *syncobjs = panvk_priv_mem_host_addr(event->syncobjs);

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++) {
      if (!syncobjs[i].seqno)
         return VK_EVENT_RESET;
   }

   return VK_EVENT_SET;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(SetEvent)(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(panvk_event, event, _event);

   struct panvk_cs_sync32 *syncobjs = panvk_priv_mem_host_addr(event->syncobjs);

   for (uint32_t i = 0; i < PANVK_SUBQUEUE_COUNT; i++)
      syncobjs[i].seqno = 1;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(ResetEvent)(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(panvk_event, event, _event);

   struct panvk_cs_sync32 *syncobjs = panvk_priv_mem_host_addr(event->syncobjs);

   memset(syncobjs, 0, sizeof(*syncobjs) * PANVK_SUBQUEUE_COUNT);
   return VK_SUCCESS;
}
