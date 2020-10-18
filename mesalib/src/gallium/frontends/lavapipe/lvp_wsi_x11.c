/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <X11/Xlib-xcb.h>
#include <xcb/xcb.h>

#include "wsi_common_x11.h"
#include "lvp_private.h"

VkBool32 lvp_GetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    xcb_connection_t*                           connection,
    xcb_visualid_t                              visual_id)
{
   LVP_FROM_HANDLE(lvp_physical_device, device, physicalDevice);

   return wsi_get_physical_device_xcb_presentation_support(
      &device->wsi_device,
      queueFamilyIndex,
      connection, visual_id);
}

VkBool32 lvp_GetPhysicalDeviceXlibPresentationSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    Display*                                    dpy,
    VisualID                                    visualID)
{
   LVP_FROM_HANDLE(lvp_physical_device, device, physicalDevice);

   return wsi_get_physical_device_xcb_presentation_support(
      &device->wsi_device,
      queueFamilyIndex,
      XGetXCBConnection(dpy), visualID);
}

VkResult lvp_CreateXcbSurfaceKHR(
    VkInstance                                  _instance,
    const VkXcbSurfaceCreateInfoKHR*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);
   const VkAllocationCallbacks *alloc;
   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR);

   if (pAllocator)
     alloc = pAllocator;
   else
     alloc = &instance->alloc;

   return wsi_create_xcb_surface(alloc, pCreateInfo, pSurface);
}

VkResult lvp_CreateXlibSurfaceKHR(
    VkInstance                                  _instance,
    const VkXlibSurfaceCreateInfoKHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);
   const VkAllocationCallbacks *alloc;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR);

   if (pAllocator)
     alloc = pAllocator;
   else
     alloc = &instance->alloc;

   return wsi_create_xlib_surface(alloc, pCreateInfo, pSurface);
}
