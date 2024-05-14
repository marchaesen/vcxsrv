/*
 * Copyright © 2017, Google Inc.
 * SPDX-License-Identifier: MIT
 */

#include "tu_android.h"

#include <android/hardware_buffer.h>
#include <hardware/hardware.h>
#include <hardware/hwvulkan.h>
#include <stdbool.h>

#include "util/u_gralloc/u_gralloc.h"
#include "vk_android.h"

#include "tu_device.h"

static int
tu_hal_open(const struct hw_module_t *mod,
            const char *id,
            struct hw_device_t **dev);
static int
tu_hal_close(struct hw_device_t *dev);

static_assert(HWVULKAN_DISPATCH_MAGIC == ICD_LOADER_MAGIC, "");

struct hw_module_methods_t HAL_MODULE_METHODS = {
   .open = tu_hal_open,
};

PUBLIC struct hwvulkan_module_t HAL_MODULE_INFO_SYM = {
   .common =
     {
       .tag = HARDWARE_MODULE_TAG,
       .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
       .hal_api_version = HARDWARE_MAKE_API_VERSION(1, 0),
       .id = HWVULKAN_HARDWARE_MODULE_ID,
       .name = "Turnip Vulkan HAL",
       .author = "Google",
       .methods = &HAL_MODULE_METHODS,
     },
};

static int
tu_hal_open(const struct hw_module_t *mod,
            const char *id,
            struct hw_device_t **dev)
{
   assert(mod == &HAL_MODULE_INFO_SYM.common);
   assert(strcmp(id, HWVULKAN_DEVICE_0) == 0);

   hwvulkan_device_t *hal_dev = (hwvulkan_device_t *) malloc(sizeof(*hal_dev));
   if (!hal_dev)
      return -1;

   *hal_dev = (hwvulkan_device_t){
      .common =
        {
          .tag = HARDWARE_DEVICE_TAG,
          .version = HWVULKAN_DEVICE_API_VERSION_0_1,
          .module = &HAL_MODULE_INFO_SYM.common,
          .close = tu_hal_close,
        },
      .EnumerateInstanceExtensionProperties =
        tu_EnumerateInstanceExtensionProperties,
      .CreateInstance = tu_CreateInstance,
      .GetInstanceProcAddr = tu_GetInstanceProcAddr,
   };

   vk_android_init_ugralloc();

   *dev = &hal_dev->common;
   return 0;
}

static int
tu_hal_close(struct hw_device_t *dev)
{
   /* hwvulkan.h claims that hw_device_t::close() is never called. */
   vk_android_destroy_ugralloc();
   return -1;
}
