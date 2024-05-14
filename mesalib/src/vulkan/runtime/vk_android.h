/*
 * Copyright © 2023 Collabora, Ltd.
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
#ifndef VK_ANDROID_H
#define VK_ANDROID_H

#include <stdbool.h>

#include "vulkan/vulkan_core.h"

#include "util/detect_os.h"

#ifdef __cplusplus
extern "C" {
#endif

struct u_gralloc;
struct vk_device;
struct vk_image;
struct AHardwareBuffer;

#if DETECT_OS_ANDROID
struct u_gralloc *vk_android_get_ugralloc(void);
struct u_gralloc *vk_android_init_ugralloc(void);
void vk_android_destroy_ugralloc(void);
VkResult vk_android_import_anb(struct vk_device *device,
                               const VkImageCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *alloc,
                               struct vk_image *image);
VkResult vk_android_get_anb_layout(
   const VkImageCreateInfo *pCreateInfo,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes);
VkResult vk_android_get_ahb_layout(
   struct AHardwareBuffer *ahardware_buffer,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes);
#else
static inline struct u_gralloc *
vk_android_get_ugralloc(void)
{
   return NULL;
}

static inline struct u_gralloc *
vk_android_init_ugralloc(void)
{
   return NULL;
}

static inline void
vk_android_destroy_ugralloc(void)
{
}

static inline VkResult
vk_android_import_anb(struct vk_device *device,
                      const VkImageCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *alloc,
                      struct vk_image *image)
{
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

static inline VkResult
vk_android_get_anb_layout(
   const VkImageCreateInfo *pCreateInfo,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes)
{
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

static inline VkResult
vk_android_get_ahb_layout(
   struct AHardwareBuffer *ahardware_buffer,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes)
{
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

#endif

#if DETECT_OS_ANDROID && ANDROID_API_LEVEL >= 26

VkFormat vk_ahb_format_to_image_format(uint32_t ahb_format);

uint32_t vk_image_format_to_ahb_format(VkFormat vk_format);

uint64_t vk_image_usage_to_ahb_usage(const VkImageCreateFlags vk_create,
                                     const VkImageUsageFlags vk_usage);

bool vk_ahb_probe_format(VkFormat vk_format,
                         VkImageCreateFlags vk_create,
                         VkImageUsageFlags vk_usage);

struct AHardwareBuffer *
vk_alloc_ahardware_buffer(const VkMemoryAllocateInfo *pAllocateInfo);

#else /* DETECT_OS_ANDROID && ANDROID_API_LEVEL >= 26 */

static inline VkFormat
vk_ahb_format_to_image_format(uint32_t ahb_format)
{
   return VK_FORMAT_UNDEFINED;
}

static inline uint32_t
vk_image_format_to_ahb_format(VkFormat vk_format)
{
   return 0;
}

static inline uint64_t
vk_image_usage_to_ahb_usage(const VkImageCreateFlags vk_create,
                            const VkImageUsageFlags vk_usage)
{
   return 0;
}

static inline bool
vk_ahb_probe_format(VkFormat vk_format,
                    VkImageCreateFlags vk_create,
                    VkImageUsageFlags vk_usage)
{
   return false;
}

static inline struct AHardwareBuffer *
vk_alloc_ahardware_buffer(const VkMemoryAllocateInfo *pAllocateInfo)
{
   return NULL;
}

#endif /* ANDROID_API_LEVEL >= 26 */

#ifdef __cplusplus
}
#endif

#endif /* VK_ANDROID_H */
