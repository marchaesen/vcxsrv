/*
 * Copyright 2018 Google LLC
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <vulkan/vulkan.h>

#if VK_HEADER_VERSION < 76

typedef struct VkBaseOutStructure {
    VkStructureType sType;
    struct VkBaseOutStructure* pNext;
} VkBaseOutStructure;

typedef struct VkBaseInStructure {
    VkStructureType sType;
    const struct VkBaseInStructure* pNext;
} VkBaseInStructure;

#endif  // VK_HEADER_VERSION < 76
