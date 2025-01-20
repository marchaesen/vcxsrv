/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#ifndef VK_RADIX_SORT_U64
#define VK_RADIX_SORT_U64

#include "radix_sort_vk.h"

#ifdef __cplusplus
extern "C" {
#endif

radix_sort_vk_t *
vk_create_radix_sort_u64(VkDevice device, VkAllocationCallbacks const *ac,
                         VkPipelineCache pc,
                         struct radix_sort_vk_target_config config);

#ifdef __cplusplus
}
#endif

#endif
