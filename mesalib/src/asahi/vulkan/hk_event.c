/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_event.h"
#include "vulkan/vulkan_core.h"

#include "agx_bo.h"
#include "hk_cmd_buffer.h"
#include "hk_device.h"
#include "hk_entrypoints.h"

#define HK_EVENT_MEM_SIZE sizeof(VkResult)

VKAPI_ATTR VkResult VKAPI_CALL
hk_CreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkEvent *pEvent)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   struct hk_event *event;

   event = vk_object_zalloc(&dev->vk, pAllocator, sizeof(*event),
                            VK_OBJECT_TYPE_EVENT);
   if (!event)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* TODO: this is really wasteful, bring back the NVK heap!
    *
    * XXX
    */
   event->bo =
      agx_bo_create(&dev->dev, HK_EVENT_MEM_SIZE, 0, AGX_BO_WRITEBACK, "Event");
   event->status = agx_bo_map(event->bo);
   event->addr = event->bo->va->addr;

   *event->status = VK_EVENT_RESET;

   *pEvent = hk_event_to_handle(event);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
hk_DestroyEvent(VkDevice device, VkEvent _event,
                const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_event, event, _event);

   if (!event)
      return;

   agx_bo_unreference(&dev->dev, event->bo);
   vk_object_free(&dev->vk, pAllocator, event);
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_GetEventStatus(VkDevice device, VkEvent _event)
{
   VK_FROM_HANDLE(hk_event, event, _event);

   return *event->status;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_SetEvent(VkDevice device, VkEvent _event)
{
   VK_FROM_HANDLE(hk_event, event, _event);

   *event->status = VK_EVENT_SET;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_ResetEvent(VkDevice device, VkEvent _event)
{
   VK_FROM_HANDLE(hk_event, event, _event);

   *event->status = VK_EVENT_RESET;

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdSetEvent2(VkCommandBuffer commandBuffer, VkEvent _event,
                const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_event, event, _event);

   perf_debug(cmd, "Set event");
   hk_cmd_buffer_end_compute(cmd);
   hk_cmd_buffer_end_graphics(cmd);
   hk_queue_write(cmd, event->bo->va->addr, VK_EVENT_SET, false);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdResetEvent2(VkCommandBuffer commandBuffer, VkEvent _event,
                  VkPipelineStageFlags2 stageMask)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_event, event, _event);

   perf_debug(cmd, "Reset event");
   hk_cmd_buffer_end_compute(cmd);
   hk_cmd_buffer_end_graphics(cmd);
   hk_queue_write(cmd, event->bo->va->addr, VK_EVENT_RESET, false);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdWaitEvents2(VkCommandBuffer commandBuffer, uint32_t eventCount,
                  const VkEvent *pEvents,
                  const VkDependencyInfo *pDependencyInfos)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   perf_debug(cmd, "Wait events");

   /* The big hammer. Need to check if this is actually needed.
    *
    * XXX: perf.
    */
   hk_cmd_buffer_end_compute(cmd);
   hk_cmd_buffer_end_graphics(cmd);
}
