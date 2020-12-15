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

VkResult
tu_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physical_device,
                                         uint32_t *property_count,
                                         VkDisplayPropertiesKHR *properties)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_display_get_physical_device_display_properties(
             physical_device,
             &pdevice->wsi_device,
             property_count,
             properties);
}

VkResult
tu_GetPhysicalDeviceDisplayProperties2KHR(VkPhysicalDevice physical_device,
                                          uint32_t *property_count,
                                          VkDisplayProperties2KHR *properties)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_display_get_physical_device_display_properties2(
             physical_device,
             &pdevice->wsi_device,
             property_count,
             properties);
}

VkResult
tu_GetPhysicalDeviceDisplayPlanePropertiesKHR(
   VkPhysicalDevice physical_device,
   uint32_t *property_count,
   VkDisplayPlanePropertiesKHR *properties)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_display_get_physical_device_display_plane_properties(
             physical_device,
             &pdevice->wsi_device,
             property_count,
             properties);
}

VkResult
tu_GetPhysicalDeviceDisplayPlaneProperties2KHR(
   VkPhysicalDevice physical_device,
   uint32_t *property_count,
   VkDisplayPlaneProperties2KHR *properties)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_display_get_physical_device_display_plane_properties2(
             physical_device,
             &pdevice->wsi_device,
             property_count,
             properties);
}

VkResult
tu_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physical_device,
                                       uint32_t plane_index,
                                       uint32_t *display_count,
                                       VkDisplayKHR *displays)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_display_get_display_plane_supported_displays(
             physical_device,
             &pdevice->wsi_device,
             plane_index,
             display_count,
             displays);
}


VkResult
tu_GetDisplayModePropertiesKHR(VkPhysicalDevice physical_device,
                               VkDisplayKHR display,
                               uint32_t *property_count,
                               VkDisplayModePropertiesKHR *properties)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_display_get_display_mode_properties(physical_device,
                                                  &pdevice->wsi_device,
                                                  display,
                                                  property_count,
                                                  properties);
}

VkResult
tu_GetDisplayModeProperties2KHR(VkPhysicalDevice physical_device,
                                VkDisplayKHR display,
                                uint32_t *property_count,
                                VkDisplayModeProperties2KHR *properties)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_display_get_display_mode_properties2(physical_device,
                                                   &pdevice->wsi_device,
                                                   display,
                                                   property_count,
                                                   properties);
}

VkResult
tu_CreateDisplayModeKHR(VkPhysicalDevice physical_device,
                        VkDisplayKHR display,
                        const VkDisplayModeCreateInfoKHR *create_info,
                        const VkAllocationCallbacks *allocator,
                        VkDisplayModeKHR *mode)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_display_create_display_mode(physical_device,
                                          &pdevice->wsi_device,
                                          display,
                                          create_info,
                                          allocator,
                                          mode);
}

VkResult
tu_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physical_device,
                                  VkDisplayModeKHR mode_khr,
                                  uint32_t plane_index,
                                  VkDisplayPlaneCapabilitiesKHR *capabilities)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_get_display_plane_capabilities(physical_device,
                                             &pdevice->wsi_device,
                                             mode_khr,
                                             plane_index,
                                             capabilities);
}

VkResult
tu_GetDisplayPlaneCapabilities2KHR(VkPhysicalDevice physical_device,
                                   const VkDisplayPlaneInfo2KHR *pDisplayPlaneInfo,
                                   VkDisplayPlaneCapabilities2KHR *capabilities)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_get_display_plane_capabilities2(physical_device,
                                              &pdevice->wsi_device,
                                              pDisplayPlaneInfo,
                                              capabilities);
}

VkResult
tu_CreateDisplayPlaneSurfaceKHR(
   VkInstance _instance,
   const VkDisplaySurfaceCreateInfoKHR *create_info,
   const VkAllocationCallbacks *allocator,
   VkSurfaceKHR *surface)
{
   TU_FROM_HANDLE(tu_instance, instance, _instance);
   const VkAllocationCallbacks *alloc;

   if (allocator)
      alloc = allocator;
   else
      alloc = &instance->alloc;

   return wsi_create_display_surface(_instance, alloc,
                                     create_info, surface);
}

VkResult
tu_ReleaseDisplayEXT(VkPhysicalDevice physical_device,
                     VkDisplayKHR     display)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_release_display(physical_device,
                              &pdevice->wsi_device,
                              display);
}

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
VkResult
tu_AcquireXlibDisplayEXT(VkPhysicalDevice     physical_device,
                         Display              *dpy,
                         VkDisplayKHR         display)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_acquire_xlib_display(physical_device,
                                   &pdevice->wsi_device,
                                   dpy,
                                   display);
}

VkResult
tu_GetRandROutputDisplayEXT(VkPhysicalDevice  physical_device,
                            Display           *dpy,
                            RROutput          output,
                            VkDisplayKHR      *display)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physical_device);

   return wsi_get_randr_output_display(physical_device,
                                       &pdevice->wsi_device,
                                       dpy,
                                       output,
                                       display);
}
#endif /* VK_USE_PLATFORM_XLIB_XRANDR_EXT */

/* VK_EXT_display_control */

VkResult
tu_DisplayPowerControlEXT(VkDevice                    _device,
                          VkDisplayKHR                display,
                          const VkDisplayPowerInfoEXT *display_power_info)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   return wsi_display_power_control(_device,
                                    &device->physical_device->wsi_device,
                                    display,
                                    display_power_info);
}

VkResult
tu_RegisterDeviceEventEXT(VkDevice                    _device,
                          const VkDeviceEventInfoEXT  *device_event_info,
                          const VkAllocationCallbacks *allocator,
                          VkFence                     *_fence)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   VkResult ret;

   ret = tu_CreateFence(_device, &(VkFenceCreateInfo) {}, allocator, _fence);
   if (ret != VK_SUCCESS)
      return ret;

   TU_FROM_HANDLE(tu_syncobj, fence, *_fence);

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
      tu_DestroyFence(_device, *_fence, allocator);

   return ret;
}

VkResult
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

VkResult
tu_GetSwapchainCounterEXT(VkDevice                    _device,
                          VkSwapchainKHR              swapchain,
                          VkSurfaceCounterFlagBitsEXT flag_bits,
                          uint64_t                    *value)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   return wsi_get_swapchain_counter(_device,
                                    &device->physical_device->wsi_device,
                                    swapchain,
                                    flag_bits,
                                    value);
}

