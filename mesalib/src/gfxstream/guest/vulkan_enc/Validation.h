/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <vulkan/vulkan.h>

namespace gfxstream {
namespace vk {

class Validation {
   public:
    VkResult on_vkFlushMappedMemoryRanges(void* context, VkResult input_result, VkDevice device,
                                          uint32_t memoryRangeCount,
                                          const VkMappedMemoryRange* pMemoryRanges);
    VkResult on_vkInvalidateMappedMemoryRanges(void* context, VkResult input_result,
                                               VkDevice device, uint32_t memoryRangeCount,
                                               const VkMappedMemoryRange* pMemoryRanges);
};

}  // namespace vk
}  // namespace gfxstream
