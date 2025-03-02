/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_event.h"

#include "tu_cmd_buffer.h"
#include "tu_rmv.h"

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateEvent(VkDevice _device,
               const VkEventCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkEvent *pEvent)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   struct tu_event *event = (struct tu_event *)
         vk_object_alloc(&device->vk, pAllocator, sizeof(*event),
                         VK_OBJECT_TYPE_EVENT);
   if (!event)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   mtx_lock(&device->event_mutex);
   VkResult result = tu_suballoc_bo_alloc(&event->bo, &device->event_suballoc, 64, 64);
   mtx_unlock(&device->event_mutex);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   TU_RMV(event_create, device, pCreateInfo, event);

   *pEvent = tu_event_to_handle(event);

   return VK_SUCCESS;

fail_alloc:
   vk_object_free(&device->vk, pAllocator, event);
   return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyEvent(VkDevice _device,
                VkEvent _event,
                const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_event, event, _event);

   if (!event)
      return;

   TU_RMV(resource_destroy, device, event);

   mtx_lock(&device->event_mutex);
   tu_suballoc_bo_free(&device->event_suballoc, &event->bo);
   mtx_unlock(&device->event_mutex);
   vk_object_free(&device->vk, pAllocator, event);
}


static uint64_t *
tu_event_map(tu_event *event)
{
   return (uint64_t *)tu_suballoc_bo_map(&event->bo);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetEventStatus(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_event, event, _event);

   if (vk_device_is_lost(&device->vk))
      return VK_ERROR_DEVICE_LOST;

   if (*tu_event_map(event) == 1)
      return VK_EVENT_SET;
   return VK_EVENT_RESET;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_SetEvent(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(tu_event, event, _event);
   *tu_event_map(event) = 1;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_ResetEvent(VkDevice _device, VkEvent _event)
{
   VK_FROM_HANDLE(tu_event, event, _event);
   *tu_event_map(event) = 0;

   return VK_SUCCESS;
}

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdSetEvent2(VkCommandBuffer commandBuffer,
                VkEvent _event,
                const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_event, event, _event);
   VkPipelineStageFlags2 src_stage_mask = 0;

   for (uint32_t i = 0; i < pDependencyInfo->memoryBarrierCount; i++)
      src_stage_mask |= pDependencyInfo->pMemoryBarriers[i].srcStageMask;
   for (uint32_t i = 0; i < pDependencyInfo->bufferMemoryBarrierCount; i++)
      src_stage_mask |= pDependencyInfo->pBufferMemoryBarriers[i].srcStageMask;
   for (uint32_t i = 0; i < pDependencyInfo->imageMemoryBarrierCount; i++)
      src_stage_mask |= pDependencyInfo->pImageMemoryBarriers[i].srcStageMask;

   tu_write_event<CHIP>(cmd, event, src_stage_mask, 1);
}
TU_GENX(tu_CmdSetEvent2);

template <chip CHIP>
VKAPI_ATTR void VKAPI_CALL
tu_CmdResetEvent2(VkCommandBuffer commandBuffer,
                  VkEvent _event,
                  VkPipelineStageFlags2 stageMask)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(tu_event, event, _event);

   tu_write_event<CHIP>(cmd, event, stageMask, 0);
}
TU_GENX(tu_CmdResetEvent2);

VKAPI_ATTR void VKAPI_CALL
tu_CmdWaitEvents2(VkCommandBuffer commandBuffer,
                  uint32_t eventCount,
                  const VkEvent *pEvents,
                  const VkDependencyInfo* pDependencyInfos)
{
   VK_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;

   for (uint32_t i = 0; i < eventCount; i++) {
      VK_FROM_HANDLE(tu_event, event, pEvents[i]);

      tu_cs_emit_pkt7(cs, CP_WAIT_REG_MEM, 6);
      tu_cs_emit(cs, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ) |
                     CP_WAIT_REG_MEM_0_POLL(POLL_MEMORY));
      tu_cs_emit_qw(cs, event->bo.iova); /* POLL_ADDR_LO/HI */
      tu_cs_emit(cs, CP_WAIT_REG_MEM_3_REF(1));
      tu_cs_emit(cs, CP_WAIT_REG_MEM_4_MASK(~0u));
      tu_cs_emit(cs, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(20));
   }

   tu_barrier(cmd, eventCount, pDependencyInfos);
}
