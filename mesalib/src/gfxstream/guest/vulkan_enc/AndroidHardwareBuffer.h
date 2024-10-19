/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 */
#pragma once

#if defined(ANDROID)

#include <vulkan/vulkan.h>

#include "gfxstream/guest/GfxStreamGralloc.h"
#include "HostVisibleMemoryVirtualization.h"

// Structure similar to
// https://github.com/mesa3d/mesa/blob/master/src/intel/vulkan/anv_android.c

namespace gfxstream {
namespace vk {

uint64_t getAndroidHardwareBufferUsageFromVkUsage(const VkImageCreateFlags vk_create,
                                                  const VkImageUsageFlags vk_usage);

void updateMemoryTypeBits(uint32_t* memoryTypeBits, uint32_t colorBufferMemoryIndex);

VkResult getAndroidHardwareBufferPropertiesANDROID(
    gfxstream::Gralloc* grallocHelper, const AHardwareBuffer* buffer,
    VkAndroidHardwareBufferPropertiesANDROID* pProperties);

VkResult getMemoryAndroidHardwareBufferANDROID(gfxstream::Gralloc* grallocHelper,
                                               struct AHardwareBuffer** pBuffer);

VkResult importAndroidHardwareBuffer(gfxstream::Gralloc* grallocHelper,
                                     const VkImportAndroidHardwareBufferInfoANDROID* info,
                                     struct AHardwareBuffer** importOut);

VkResult createAndroidHardwareBuffer(gfxstream::Gralloc* grallocHelper, bool hasDedicatedImage,
                                     bool hasDedicatedBuffer, const VkExtent3D& imageExtent,
                                     uint32_t imageLayers, VkFormat imageFormat,
                                     VkImageUsageFlags imageUsage,
                                     VkImageCreateFlags imageCreateFlags, VkDeviceSize bufferSize,
                                     VkDeviceSize allocationInfoAllocSize,
                                     struct AHardwareBuffer** out);

}  // namespace vk
}  // namespace gfxstream

#endif  // ANDROID
