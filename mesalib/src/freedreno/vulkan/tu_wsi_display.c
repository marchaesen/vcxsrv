/*
 * Copyright © 2017 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "tu_private.h"
#include "tu_cs.h"
#include "util/disk_cache.h"
#include "util/strtod.h"
#include "vk_util.h"
#include "vk_format.h"
#include "util/debug.h"
#include "wsi_common_display.h"

/* VK_EXT_display_control */

VKAPI_ATTR VkResult VKAPI_CALL
tu_RegisterDeviceEventEXT(VkDevice                    _device,
                          const VkDeviceEventInfoEXT  *device_event_info,
                          const VkAllocationCallbacks *allocator,
                          VkFence                     *out_fence)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   VkResult ret;

   VkFence _fence;
   ret = tu_CreateFence(_device, &(VkFenceCreateInfo) {}, allocator, &_fence);
   if (ret != VK_SUCCESS)
      return ret;

   TU_FROM_HANDLE(tu_syncobj, fence, _fence);

   int sync_fd = tu_syncobj_to_fd(device, fence);
   if (sync_fd >= 0) {
      ret = wsi_register_device_event(_device,
                                      &device->physical_device->wsi_device,
                                      device_event_info,
                                      allocator,
                                      NULL,
                                      sync_fd);

      close(sync_fd);
   } else {
      ret = VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   if (ret != VK_SUCCESS)
      tu_DestroyFence(_device, _fence, allocator);
   else
      *out_fence = _fence;

   return ret;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_RegisterDisplayEventEXT(VkDevice                           _device,
                           VkDisplayKHR                       display,
                           const VkDisplayEventInfoEXT        *display_event_info,
                           const VkAllocationCallbacks        *allocator,
                           VkFence                            *_fence)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   VkResult ret;

   ret = tu_CreateFence(_device, &(VkFenceCreateInfo) {}, allocator, _fence);
   if (ret != VK_SUCCESS)
      return ret;

   TU_FROM_HANDLE(tu_syncobj, fence, *_fence);

   int sync_fd = tu_syncobj_to_fd(device, fence);
   if (sync_fd >= 0) {
      ret = wsi_register_display_event(_device,
                                       &device->physical_device->wsi_device,
                                       display,
                                       display_event_info,
                                       allocator,
                                       NULL,
                                       sync_fd);

      close(sync_fd);
   } else {
      ret = VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   if (ret != VK_SUCCESS)
      tu_DestroyFence(_device, *_fence, allocator);

   return ret;
}
