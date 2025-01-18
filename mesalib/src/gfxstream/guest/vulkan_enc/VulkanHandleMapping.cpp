/*
 * Copyright 2018 Google LLC
 * SPDX-License-Identifier: MIT
 */
#include "VulkanHandleMapping.h"

#include <vulkan/vulkan.h>

namespace gfxstream {
namespace vk {

#define DEFAULT_HANDLE_MAP_DEFINE(type)                                                            \
    void DefaultHandleMapping::mapHandles_##type(type*, size_t) { return; }                        \
    void DefaultHandleMapping::mapHandles_##type##_u64(const type* handles, uint64_t* handle_u64s, \
                                                       size_t count) {                             \
        for (size_t i = 0; i < count; ++i) {                                                       \
            handle_u64s[i] = (uint64_t)(uintptr_t)handles[i];                                      \
        }                                                                                          \
    }                                                                                              \
    void DefaultHandleMapping::mapHandles_u64_##type(const uint64_t* handle_u64s, type* handles,   \
                                                     size_t count) {                               \
        for (size_t i = 0; i < count; ++i) {                                                       \
            handles[i] = (type)(uintptr_t)handle_u64s[i];                                          \
        }                                                                                          \
    }

GOLDFISH_VK_LIST_HANDLE_TYPES(DEFAULT_HANDLE_MAP_DEFINE)

}  // namespace vk
}  // namespace gfxstream
