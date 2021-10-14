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

#include <amdgpu.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "drm-uapi/amdgpu_drm.h"
#include "util/debug.h"
#include "util/disk_cache.h"
#include "util/strtod.h"
#include "winsys/amdgpu/radv_amdgpu_winsys_public.h"
#include "radv_cs.h"
#include "radv_private.h"
#include "sid.h"
#include "vk_format.h"
#include "vk_util.h"
#include "wsi_common_display.h"

#define MM_PER_PIXEL (1.0 / 96.0 * 25.4)

/* VK_EXT_display_control */

VkResult
radv_RegisterDeviceEventEXT(VkDevice _device, const VkDeviceEventInfoEXT *device_event_info,
                            const VkAllocationCallbacks *allocator, VkFence *_fence)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkResult ret;
   int fd;

   ret = radv_CreateFence(_device,
                          &(VkFenceCreateInfo){
                             .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                             .pNext =
                                &(VkExportFenceCreateInfo){
                                   .sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
                                   .handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT,
                                },
                          },
                          allocator, _fence);
   if (ret != VK_SUCCESS)
      return ret;

   RADV_FROM_HANDLE(radv_fence, fence, *_fence);

   assert(fence->permanent.kind == RADV_FENCE_SYNCOBJ);

   if (device->ws->export_syncobj(device->ws, fence->permanent.syncobj, &fd)) {
      ret = VK_ERROR_OUT_OF_HOST_MEMORY;
   } else {
      ret = wsi_register_device_event(_device, &device->physical_device->wsi_device,
                                      device_event_info, allocator, NULL, fd);
      close(fd);
   }

   if (ret != VK_SUCCESS)
      radv_DestroyFence(_device, *_fence, allocator);

   return ret;
}

VkResult
radv_RegisterDisplayEventEXT(VkDevice _device, VkDisplayKHR display,
                             const VkDisplayEventInfoEXT *display_event_info,
                             const VkAllocationCallbacks *allocator, VkFence *_fence)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   VkResult ret;
   int fd;

   ret = radv_CreateFence(_device,
                          &(VkFenceCreateInfo){
                             .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                             .pNext =
                                &(VkExportFenceCreateInfo){
                                   .sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
                                   .handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT,
                                },
                          },
                          allocator, _fence);
   if (ret != VK_SUCCESS)
      return ret;

   RADV_FROM_HANDLE(radv_fence, fence, *_fence);

   assert(fence->permanent.kind == RADV_FENCE_SYNCOBJ);

   if (device->ws->export_syncobj(device->ws, fence->permanent.syncobj, &fd)) {
      ret = VK_ERROR_OUT_OF_HOST_MEMORY;
   } else {
      ret = wsi_register_display_event(_device, &device->physical_device->wsi_device, display,
                                       display_event_info, allocator, NULL, fd);
      close(fd);
   }

   if (ret != VK_SUCCESS)
      radv_DestroyFence(_device, *_fence, allocator);

   return ret;
}
