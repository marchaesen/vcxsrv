/*
 * Copyright © 2024 Valentine Burley
 * SPDX-License-Identifier: MIT
 */

#include "tu_buffer_view.h"

#include "tu_buffer.h"
#include "tu_device.h"
#include "tu_formats.h"

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateBufferView(VkDevice _device,
                    const VkBufferViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkBufferView *pView)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_buffer, buffer, pCreateInfo->buffer);
   struct tu_buffer_view *view;

   view = (struct tu_buffer_view *) vk_buffer_view_create(
      &device->vk, pCreateInfo, pAllocator, sizeof(*view));

   if (!view)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   uint8_t swiz[4] = { PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z,
                       PIPE_SWIZZLE_W };

   fdl6_buffer_view_init(
      view->descriptor, vk_format_to_pipe_format(view->vk.format),
      swiz, buffer->iova + view->vk.offset, view->vk.range);

   *pView = tu_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyBufferView(VkDevice _device,
                     VkBufferView bufferView,
                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_buffer_view, view, bufferView);

   if (!view)
      return;

   vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
}
