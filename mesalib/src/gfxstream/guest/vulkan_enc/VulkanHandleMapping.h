/*
 * Copyright 2018 Google LLC
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <vulkan/vulkan.h>

#include "VulkanHandles.h"

namespace gfxstream {
namespace vk {

class VulkanHandleMapping {
   public:
    VulkanHandleMapping() = default;
    virtual ~VulkanHandleMapping() {}

#define DECLARE_HANDLE_MAP_PURE_VIRTUAL_METHOD(type)                                 \
    virtual void mapHandles_##type(type* handles, size_t count = 1) = 0;             \
    virtual void mapHandles_##type##_u64(const type* handles, uint64_t* handle_u64s, \
                                         size_t count = 1) = 0;                      \
    virtual void mapHandles_u64_##type(const uint64_t* handle_u64s, type* handles,   \
                                       size_t count = 1) = 0;

    GOLDFISH_VK_LIST_HANDLE_TYPES(DECLARE_HANDLE_MAP_PURE_VIRTUAL_METHOD)
};

class DefaultHandleMapping : public VulkanHandleMapping {
   public:
    virtual ~DefaultHandleMapping() {}

#define DECLARE_HANDLE_MAP_OVERRIDE(type)                                                  \
    void mapHandles_##type(type* handles, size_t count) override;                          \
    void mapHandles_##type##_u64(const type* handles, uint64_t* handle_u64s, size_t count) \
        override;                                                                          \
    void mapHandles_u64_##type(const uint64_t* handle_u64s, type* handles, size_t count) override;

    GOLDFISH_VK_LIST_HANDLE_TYPES(DECLARE_HANDLE_MAP_OVERRIDE)
};

}  // namespace vk
}  // namespace gfxstream
