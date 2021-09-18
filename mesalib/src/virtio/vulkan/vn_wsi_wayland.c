/*
 * Copyright 2020 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "wsi_common_wayland.h"

#include "vn_instance.h"
#include "vn_physical_device.h"
#include "vn_wsi.h"

VkResult
vn_CreateWaylandSurfaceKHR(VkInstance _instance,
                           const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkSurfaceKHR *pSurface)
{
   struct vn_instance *instance = vn_instance_from_handle(_instance);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &instance->base.base.alloc;

   VkResult result = wsi_create_wl_surface(alloc, pCreateInfo, pSurface);

   return vn_result(instance, result);
}

VkBool32
vn_GetPhysicalDeviceWaylandPresentationSupportKHR(
   VkPhysicalDevice physicalDevice,
   uint32_t queueFamilyIndex,
   struct wl_display *display)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   return wsi_wl_get_presentation_support(&physical_dev->wsi_device, display);
}
