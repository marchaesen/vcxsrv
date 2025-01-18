/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <hardware/hardware.h>
#include <hardware/hwvulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vk_icd.h>

#include "gfxstream_vk_entrypoints.h"
#include "util/macros.h"

static int gfxstream_vk_hal_open(const struct hw_module_t* mod, const char* id,
                                 struct hw_device_t** dev);
static int gfxstream_vk_hal_close(struct hw_device_t* dev);

static_assert(HWVULKAN_DISPATCH_MAGIC == ICD_LOADER_MAGIC, "");

hw_module_methods_t gfxstream_vk_hal_ops = {
    .open = gfxstream_vk_hal_open,
};

PUBLIC struct hwvulkan_module_t HAL_MODULE_INFO_SYM = {
    .common =
        {
            .tag = HARDWARE_MODULE_TAG,
            .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
            .hal_api_version = HARDWARE_MAKE_API_VERSION(1, 0),
            .id = HWVULKAN_HARDWARE_MODULE_ID,
            .name = "gfxstream Vulkan HAL",
            .author = "Android Open Source Project",
            .methods = &(gfxstream_vk_hal_ops),
        },
};

static int gfxstream_vk_hal_open(const struct hw_module_t* mod, const char* id,
                                 struct hw_device_t** dev) {
    assert(mod == &HAL_MODULE_INFO_SYM.common);
    assert(strcmp(id, HWVULKAN_DEVICE_0) == 0);

    hwvulkan_device_t* hal_dev = (hwvulkan_device_t*)calloc(1, sizeof(*hal_dev));
    if (!hal_dev) return -1;

    *hal_dev = (hwvulkan_device_t){
        .common =
            {
                .tag = HARDWARE_DEVICE_TAG,
                .version = HWVULKAN_DEVICE_API_VERSION_0_1,
                .module = &HAL_MODULE_INFO_SYM.common,
                .close = gfxstream_vk_hal_close,
            },
        .EnumerateInstanceExtensionProperties = gfxstream_vk_EnumerateInstanceExtensionProperties,
        .CreateInstance = gfxstream_vk_CreateInstance,
        .GetInstanceProcAddr = gfxstream_vk_GetInstanceProcAddr,
    };

    *dev = &hal_dev->common;
    return 0;
}

static int gfxstream_vk_hal_close(struct hw_device_t* dev) {
    /* hwvulkan.h claims that hw_device_t::close() is never called. */
    return -1;
}
