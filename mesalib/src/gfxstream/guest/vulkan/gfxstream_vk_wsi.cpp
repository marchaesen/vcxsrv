/*
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "gfxstream_vk_entrypoints.h"
#include "gfxstream_vk_private.h"
#include "wsi_common.h"

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
gfxstream_vk_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char* pName) {
    VK_FROM_HANDLE(gfxstream_vk_physical_device, pdevice, physicalDevice);
    return vk_instance_get_proc_addr_unchecked(&pdevice->instance->vk, pName);
}

VkResult gfxstream_vk_wsi_init(struct gfxstream_vk_physical_device* physical_device) {
    VkResult result = (VkResult)0;

    const struct wsi_device_options options = {.sw_device = false};
    result = wsi_device_init(
        &physical_device->wsi_device, gfxstream_vk_physical_device_to_handle(physical_device),
        gfxstream_vk_wsi_proc_addr, &physical_device->instance->vk.alloc, -1, NULL, &options);
    if (result != VK_SUCCESS) return result;

    // Allow guest-side modifier code paths
    physical_device->wsi_device.supports_modifiers = true;
    // Support wsi_image_create_info::scanout
    physical_device->wsi_device.supports_scanout = true;

    physical_device->vk.wsi_device = &physical_device->wsi_device;

    return result;
}

void gfxstream_vk_wsi_finish(struct gfxstream_vk_physical_device* physical_device) {
    physical_device->vk.wsi_device = NULL;
    wsi_device_finish(&physical_device->wsi_device, &physical_device->instance->vk.alloc);
}
