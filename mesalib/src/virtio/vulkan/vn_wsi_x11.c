/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include <X11/Xlib-xcb.h>

#include "wsi_common_x11.h"

#include "vn_instance.h"
#include "vn_physical_device.h"
#include "vn_wsi.h"

/* XCB surface commands */

VkResult
vn_CreateXcbSurfaceKHR(VkInstance _instance,
                       const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkSurfaceKHR *pSurface)
{
   struct vn_instance *instance = vn_instance_from_handle(_instance);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &instance->base.base.alloc;

   VkResult result = wsi_create_xcb_surface(alloc, pCreateInfo, pSurface);

   return vn_result(instance, result);
}

VkBool32
vn_GetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice,
                                              uint32_t queueFamilyIndex,
                                              xcb_connection_t *connection,
                                              xcb_visualid_t visual_id)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   return wsi_get_physical_device_xcb_presentation_support(
      &physical_dev->wsi_device, queueFamilyIndex, connection, visual_id);
}

/* Xlib surface commands */

VkResult
vn_CreateXlibSurfaceKHR(VkInstance _instance,
                        const VkXlibSurfaceCreateInfoKHR *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkSurfaceKHR *pSurface)
{
   struct vn_instance *instance = vn_instance_from_handle(_instance);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &instance->base.base.alloc;

   VkResult result = wsi_create_xlib_surface(alloc, pCreateInfo, pSurface);

   return vn_result(instance, result);
}

VkBool32
vn_GetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice,
                                               uint32_t queueFamilyIndex,
                                               Display *dpy,
                                               VisualID visualID)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   return wsi_get_physical_device_xcb_presentation_support(
      &physical_dev->wsi_device, queueFamilyIndex, XGetXCBConnection(dpy),
      visualID);
}
