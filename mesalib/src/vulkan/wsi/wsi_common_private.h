/*
 * Copyright © 2017 Intel Corporation
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
#ifndef WSI_COMMON_PRIVATE_H
#define WSI_COMMON_PRIVATE_H

#include "wsi_common.h"

struct wsi_image {
   VkImage image;
   VkDeviceMemory memory;

   struct {
      VkBuffer buffer;
      VkDeviceMemory memory;
      VkCommandBuffer *blit_cmd_buffers;
   } prime;

   uint64_t drm_modifier;
   int num_planes;
   uint32_t sizes[4];
   uint32_t offsets[4];
   uint32_t row_pitches[4];
   int fds[4];
};

struct wsi_swapchain {
   const struct wsi_device *wsi;

   VkDevice device;
   VkAllocationCallbacks alloc;
   VkFence fences[3];
   VkPresentModeKHR present_mode;
   uint32_t image_count;

   bool use_prime_blit;

   /* Command pools, one per queue family */
   VkCommandPool *cmd_pools;

   VkResult (*destroy)(struct wsi_swapchain *swapchain,
                       const VkAllocationCallbacks *pAllocator);
   struct wsi_image *(*get_wsi_image)(struct wsi_swapchain *swapchain,
                                      uint32_t image_index);
   VkResult (*acquire_next_image)(struct wsi_swapchain *swap_chain,
                                  uint64_t timeout, VkSemaphore semaphore,
                                  uint32_t *image_index);
   VkResult (*queue_present)(struct wsi_swapchain *swap_chain,
                             uint32_t image_index,
                             const VkPresentRegionKHR *damage);
};

VkResult
wsi_swapchain_init(const struct wsi_device *wsi,
                   struct wsi_swapchain *chain,
                   VkDevice device,
                   const VkSwapchainCreateInfoKHR *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator);

void wsi_swapchain_finish(struct wsi_swapchain *chain);

VkResult
wsi_create_native_image(const struct wsi_swapchain *chain,
                        const VkSwapchainCreateInfoKHR *pCreateInfo,
                        uint32_t num_modifier_lists,
                        const uint32_t *num_modifiers,
                        const uint64_t *const *modifiers,
                        struct wsi_image *image);

VkResult
wsi_create_prime_image(const struct wsi_swapchain *chain,
                       const VkSwapchainCreateInfoKHR *pCreateInfo,
                       struct wsi_image *image);

void
wsi_destroy_image(const struct wsi_swapchain *chain,
                  struct wsi_image *image);


struct wsi_interface {
   VkResult (*get_support)(VkIcdSurfaceBase *surface,
                           struct wsi_device *wsi_device,
                           const VkAllocationCallbacks *alloc,
                           uint32_t queueFamilyIndex,
                           int local_fd,
                           VkBool32* pSupported);
   VkResult (*get_capabilities)(VkIcdSurfaceBase *surface,
                                VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);
   VkResult (*get_capabilities2)(VkIcdSurfaceBase *surface,
                                 const void *info_next,
                                 VkSurfaceCapabilities2KHR* pSurfaceCapabilities);
   VkResult (*get_formats)(VkIcdSurfaceBase *surface,
                           struct wsi_device *wsi_device,
                           uint32_t* pSurfaceFormatCount,
                           VkSurfaceFormatKHR* pSurfaceFormats);
   VkResult (*get_formats2)(VkIcdSurfaceBase *surface,
                            struct wsi_device *wsi_device,
                            const void *info_next,
                            uint32_t* pSurfaceFormatCount,
                            VkSurfaceFormat2KHR* pSurfaceFormats);
   VkResult (*get_present_modes)(VkIcdSurfaceBase *surface,
                                 uint32_t* pPresentModeCount,
                                 VkPresentModeKHR* pPresentModes);
   VkResult (*create_swapchain)(VkIcdSurfaceBase *surface,
                                VkDevice device,
                                struct wsi_device *wsi_device,
                                int local_fd,
                                const VkSwapchainCreateInfoKHR* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                struct wsi_swapchain **swapchain);
};

VkResult wsi_x11_init_wsi(struct wsi_device *wsi_device,
                          const VkAllocationCallbacks *alloc);
void wsi_x11_finish_wsi(struct wsi_device *wsi_device,
                        const VkAllocationCallbacks *alloc);
VkResult wsi_wl_init_wsi(struct wsi_device *wsi_device,
                         const VkAllocationCallbacks *alloc,
                         VkPhysicalDevice physical_device);
void wsi_wl_finish_wsi(struct wsi_device *wsi_device,
                       const VkAllocationCallbacks *alloc);


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

#define WSI_FROM_HANDLE(__wsi_type, __name, __handle) \
   struct __wsi_type *__name = __wsi_type ## _from_handle(__handle)

WSI_DEFINE_NONDISP_HANDLE_CASTS(wsi_swapchain, VkSwapchainKHR)

#endif /* WSI_COMMON_PRIVATE_H */
