/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
#include "Validation.h"

#include "ResourceTracker.h"
#include "Resources.h"

namespace gfxstream {
namespace vk {

VkResult Validation::on_vkFlushMappedMemoryRanges(void*, VkResult, VkDevice,
                                                  uint32_t memoryRangeCount,
                                                  const VkMappedMemoryRange* pMemoryRanges) {
    auto resources = ResourceTracker::get();

    for (uint32_t i = 0; i < memoryRangeCount; ++i) {
        if (!resources->isValidMemoryRange(pMemoryRanges[i])) {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
    }

    return VK_SUCCESS;
}

VkResult Validation::on_vkInvalidateMappedMemoryRanges(void*, VkResult, VkDevice,
                                                       uint32_t memoryRangeCount,
                                                       const VkMappedMemoryRange* pMemoryRanges) {
    auto resources = ResourceTracker::get();

    for (uint32_t i = 0; i < memoryRangeCount; ++i) {
        if (!resources->isValidMemoryRange(pMemoryRanges[i])) {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
    }

    return VK_SUCCESS;
}

}  // namespace vk
}  // namespace gfxstream
