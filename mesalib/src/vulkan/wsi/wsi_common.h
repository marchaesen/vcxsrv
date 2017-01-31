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
#ifndef WSI_COMMON_H
#define WSI_COMMON_H

#include <stdint.h>
#include <stdbool.h>

#include "util/vk_alloc.h"
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

struct wsi_device;
struct wsi_image_fns {
   VkResult (*create_wsi_image)(VkDevice device_h,
                                const VkSwapchainCreateInfoKHR *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkImage *image_p,
                                VkDeviceMemory *memory_p,
                                uint32_t *size_p,
                                uint32_t *offset_p,
                                uint32_t *row_pitch_p,
                                int *fd_p);
   void (*free_wsi_image)(VkDevice device,
                          const VkAllocationCallbacks *pAllocator,
                          VkImage image_h,
                          VkDeviceMemory memory_h);
};

struct wsi_swapchain {

   VkDevice device;
   VkAllocationCallbacks alloc;
   const struct wsi_image_fns *image_fns;
   VkFence fences[3];
   VkPresentModeKHR present_mode;

   VkResult (*destroy)(struct wsi_swapchain *swapchain,
                       const VkAllocationCallbacks *pAllocator);
   VkResult (*get_images)(struct wsi_swapchain *swapchain,
                          uint32_t *pCount, VkImage *pSwapchainImages);
   VkResult (*acquire_next_image)(struct wsi_swapchain *swap_chain,
                                  uint64_t timeout, VkSemaphore semaphore,
                                  uint32_t *image_index);
   VkResult (*queue_present)(struct wsi_swapchain *swap_chain,
                             uint32_t image_index);
};

struct wsi_interface {
   VkResult (*get_support)(VkIcdSurfaceBase *surface,
                           struct wsi_device *wsi_device,
                           const VkAllocationCallbacks *alloc,
                           uint32_t queueFamilyIndex,
                           VkBool32* pSupported);
   VkResult (*get_capabilities)(VkIcdSurfaceBase *surface,
                                VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);
   VkResult (*get_formats)(VkIcdSurfaceBase *surface,
                           struct wsi_device *wsi_device,
                           uint32_t* pSurfaceFormatCount,
                           VkSurfaceFormatKHR* pSurfaceFormats);
   VkResult (*get_present_modes)(VkIcdSurfaceBase *surface,
                                 uint32_t* pPresentModeCount,
                                 VkPresentModeKHR* pPresentModes);
   VkResult (*create_swapchain)(VkIcdSurfaceBase *surface,
                                VkDevice device,
                                struct wsi_device *wsi_device,
                                const VkSwapchainCreateInfoKHR* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                const struct wsi_image_fns *image_fns,
                                struct wsi_swapchain **swapchain);
};

#define VK_ICD_WSI_PLATFORM_MAX 5

struct wsi_device {
    struct wsi_interface *                  wsi[VK_ICD_WSI_PLATFORM_MAX];
};

struct wsi_callbacks {
   void (*get_phys_device_format_properties)(VkPhysicalDevice physicalDevice,
                                             VkFormat format,
                                             VkFormatProperties *pFormatProperties);
};

#define WSI_DEFINE_NONDISP_HANDLE_CASTS(__wsi_type, __VkType)              \
                                                                           \
   static inline struct __wsi_type *                                       \
   __wsi_type ## _from_handle(__VkType _handle)                            \
   {                                                                       \
      return (struct __wsi_type *)(uintptr_t) _handle;                     \
   }                                                                       \
                                                                           \
   static inline __VkType                                                  \
   __wsi_type ## _to_handle(struct __wsi_type *_obj)                       \
   {                                                                       \
      return (__VkType)(uintptr_t) _obj;                                   \
   }

WSI_DEFINE_NONDISP_HANDLE_CASTS(wsi_swapchain, VkSwapchainKHR)

#define ICD_DEFINE_NONDISP_HANDLE_CASTS(__VkIcdType, __VkType)             \
                                                                           \
   static inline __VkIcdType *                                             \
   __VkIcdType ## _from_handle(__VkType _handle)                           \
   {                                                                       \
      return (__VkIcdType *)(uintptr_t) _handle;                           \
   }                                                                       \
                                                                           \
   static inline __VkType                                                  \
   __VkIcdType ## _to_handle(__VkIcdType *_obj)                            \
   {                                                                       \
      return (__VkType)(uintptr_t) _obj;                                   \
   }

#define ICD_FROM_HANDLE(__VkIcdType, __name, __handle) \
   __VkIcdType *__name = __VkIcdType ## _from_handle(__handle)

ICD_DEFINE_NONDISP_HANDLE_CASTS(VkIcdSurfaceBase, VkSurfaceKHR)

VkResult wsi_x11_init_wsi(struct wsi_device *wsi_device,
                          const VkAllocationCallbacks *alloc);
void wsi_x11_finish_wsi(struct wsi_device *wsi_device,
                        const VkAllocationCallbacks *alloc);
VkResult wsi_wl_init_wsi(struct wsi_device *wsi_device,
                         const VkAllocationCallbacks *alloc,
                         VkPhysicalDevice physical_device,
                         const struct wsi_callbacks *cbs);
void wsi_wl_finish_wsi(struct wsi_device *wsi_device,
                       const VkAllocationCallbacks *alloc);


#endif
