/*
 * Copyright © 2020 Raspberry Pi
 * based on KHR_display extension code:
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
#include "v3dv_private.h"
#include "wsi_common_display.h"

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physical_device,
                                           uint32_t *property_count,
                                           VkDisplayPropertiesKHR *properties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physical_device);

   return wsi_display_get_physical_device_display_properties(
      physical_device,
      &pdevice->wsi_device,
      property_count,
      properties);
}

VkResult
v3dv_GetPhysicalDeviceDisplayProperties2KHR(
    VkPhysicalDevice physical_device,
    uint32_t *pPropertyCount,
    VkDisplayProperties2KHR *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physical_device);

   return wsi_display_get_physical_device_display_properties2(
      physical_device,
      &pdevice->wsi_device,
      pPropertyCount,
      pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetPhysicalDeviceDisplayPlanePropertiesKHR(
   VkPhysicalDevice physical_device,
   uint32_t *property_count,
   VkDisplayPlanePropertiesKHR *properties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physical_device);

   return wsi_display_get_physical_device_display_plane_properties(
      physical_device,
      &pdevice->wsi_device,
      property_count,
      properties);
}

VkResult
v3dv_GetPhysicalDeviceDisplayPlaneProperties2KHR(
    VkPhysicalDevice physical_device,
    uint32_t *pPropertyCount,
    VkDisplayPlaneProperties2KHR *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physical_device);

   return wsi_display_get_physical_device_display_plane_properties2(
      physical_device,
      &pdevice->wsi_device,
      pPropertyCount,
      pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physical_device,
                                         uint32_t plane_index,
                                         uint32_t *display_count,
                                         VkDisplayKHR *displays)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physical_device);

   return wsi_display_get_display_plane_supported_displays(
      physical_device,
      &pdevice->wsi_device,
      plane_index,
      display_count,
      displays);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetDisplayModePropertiesKHR(VkPhysicalDevice physical_device,
                                 VkDisplayKHR display,
                                 uint32_t *property_count,
                                 VkDisplayModePropertiesKHR *properties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physical_device);

   return wsi_display_get_display_mode_properties(physical_device,
                                                  &pdevice->wsi_device,
                                                  display,
                                                  property_count,
                                                  properties);
}

VkResult
v3dv_GetDisplayModeProperties2KHR(VkPhysicalDevice physical_device,
                                  VkDisplayKHR display,
                                  uint32_t *pPropertyCount,
                                  VkDisplayModeProperties2KHR *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physical_device);

   return wsi_display_get_display_mode_properties2(physical_device,
                                                   &pdevice->wsi_device,
                                                   display,
                                                   pPropertyCount,
                                                   pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateDisplayModeKHR(VkPhysicalDevice physical_device,
                          VkDisplayKHR display,
                          const VkDisplayModeCreateInfoKHR *create_info,
                          const VkAllocationCallbacks *allocator,
                          VkDisplayModeKHR *mode)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physical_device);

   return wsi_display_create_display_mode(physical_device,
                                          &pdevice->wsi_device,
                                          display,
                                          create_info,
                                          allocator,
                                          mode);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physical_device,
                                    VkDisplayModeKHR mode_khr,
                                    uint32_t plane_index,
                                    VkDisplayPlaneCapabilitiesKHR *capabilities)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physical_device);

   return wsi_get_display_plane_capabilities(physical_device,
                                             &pdevice->wsi_device,
                                             mode_khr,
                                             plane_index,
                                             capabilities);
}

VkResult
v3dv_GetDisplayPlaneCapabilities2KHR(
   VkPhysicalDevice physical_device,
   const VkDisplayPlaneInfo2KHR *pDisplayPlaneInfo,
   VkDisplayPlaneCapabilities2KHR *pCapabilities)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physical_device);

   return wsi_get_display_plane_capabilities2(physical_device,
                                              &pdevice->wsi_device,
                                              pDisplayPlaneInfo,
                                              pCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateDisplayPlaneSurfaceKHR(
    VkInstance _instance,
    const VkDisplaySurfaceCreateInfoKHR *create_info,
    const VkAllocationCallbacks *allocator,
    VkSurfaceKHR *surface)
{
   V3DV_FROM_HANDLE(v3dv_instance, instance, _instance);
   const VkAllocationCallbacks *alloc;

   if (allocator)
      alloc = allocator;
   else
      alloc = &instance->vk.alloc;

   return wsi_create_display_surface(_instance, alloc,
                                     create_info, surface);
}
