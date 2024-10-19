/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_descriptors.h"
#include "gfx10_format_table.h"

#include "radv_buffer.h"
#include "radv_buffer_view.h"
#include "radv_entrypoints.h"
#include "radv_formats.h"
#include "radv_image.h"

#include "vk_log.h"

void
radv_make_texel_buffer_descriptor(struct radv_device *device, uint64_t va, VkFormat vk_format, unsigned offset,
                                  unsigned range, uint32_t *state)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct util_format_description *desc;
   unsigned stride;
   enum pipe_swizzle swizzle[4];

   desc = vk_format_description(vk_format);
   stride = desc->block.bits / 8;

   radv_compose_swizzle(desc, NULL, swizzle);

   va += offset;

   if (pdev->info.gfx_level != GFX8 && stride) {
      range /= stride;
   }

   const struct ac_buffer_state ac_state = {
      .va = va,
      .size = range,
      .format = radv_format_to_pipe_format(vk_format),
      .swizzle =
         {
            swizzle[0],
            swizzle[1],
            swizzle[2],
            swizzle[3],
         },
      .stride = stride,
      .gfx10_oob_select = V_008F0C_OOB_SELECT_STRUCTURED_WITH_OFFSET,
   };

   ac_build_buffer_descriptor(pdev->info.gfx_level, &ac_state, state);
}

void
radv_buffer_view_init(struct radv_buffer_view *view, struct radv_device *device,
                      const VkBufferViewCreateInfo *pCreateInfo)
{
   VK_FROM_HANDLE(radv_buffer, buffer, pCreateInfo->buffer);
   uint64_t va = radv_buffer_get_va(buffer->bo) + buffer->offset;

   vk_buffer_view_init(&device->vk, &view->vk, pCreateInfo);

   view->bo = buffer->bo;

   radv_make_texel_buffer_descriptor(device, va, view->vk.format, view->vk.offset, view->vk.range, view->state);
}

void
radv_buffer_view_finish(struct radv_buffer_view *view)
{
   vk_buffer_view_finish(&view->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateBufferView(VkDevice _device, const VkBufferViewCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator, VkBufferView *pView)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_buffer_view *view;

   view = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*view), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_buffer_view_init(view, device, pCreateInfo);

   *pView = radv_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyBufferView(VkDevice _device, VkBufferView bufferView, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_buffer_view, view, bufferView);

   if (!view)
      return;

   radv_buffer_view_finish(view);
   vk_free2(&device->vk.alloc, pAllocator, view);
}
