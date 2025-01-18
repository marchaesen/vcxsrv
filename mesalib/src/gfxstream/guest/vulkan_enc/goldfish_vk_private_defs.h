/*
 * Copyright 2018 Google
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <vulkan/vulkan.h>

#ifdef __cplusplus
#include <algorithm>
#endif

// VulkanStream features
#define VULKAN_STREAM_FEATURE_NULL_OPTIONAL_STRINGS_BIT (1 << 0)
#define VULKAN_STREAM_FEATURE_IGNORED_HANDLES_BIT (1 << 1)
#define VULKAN_STREAM_FEATURE_SHADER_FLOAT16_INT8_BIT (1 << 2)
#define VULKAN_STREAM_FEATURE_QUEUE_SUBMIT_WITH_COMMANDS_BIT (1 << 3)

#define VK_YCBCR_CONVERSION_DO_NOTHING ((VkSamplerYcbcrConversion)0x1111111111111111)

#ifdef __cplusplus

template <class T, typename F>
bool arrayany(const T* arr, uint32_t begin, uint32_t end, const F& func) {
    const T* e = arr + end;
    return std::find_if(arr + begin, e, func) != e;
}

#define DEFINE_ALIAS_FUNCTION(ORIGINAL_FN, ALIAS_FN)                                             \
    template <typename... Args>                                                                  \
    inline auto ALIAS_FN(Args&&... args) -> decltype(ORIGINAL_FN(std::forward<Args>(args)...)) { \
        return ORIGINAL_FN(std::forward<Args>(args)...);                                         \
    }

#endif
