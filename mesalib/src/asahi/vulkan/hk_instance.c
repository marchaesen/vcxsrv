/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_instance.h"

#include "hk_entrypoints.h"
#include "hk_physical_device.h"

#include "vulkan/wsi/wsi_common.h"

#include "util/build_id.h"
#include "util/driconf.h"
#include "util/mesa-sha1.h"

VKAPI_ATTR VkResult VKAPI_CALL
hk_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
   uint32_t version_override = vk_get_version_override();
   *pApiVersion = version_override ? version_override
                                   : VK_MAKE_VERSION(1, 3, VK_HEADER_VERSION);

   return VK_SUCCESS;
}

static const struct vk_instance_extension_table instance_extensions = {
#ifdef HK_USE_WSI_PLATFORM
   .KHR_get_surface_capabilities2 = true,
   .KHR_surface = true,
   .KHR_surface_protected_capabilities = true,
   .EXT_surface_maintenance1 = true,
   .EXT_swapchain_colorspace = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
   .EXT_acquire_xlib_display = true,
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
   .KHR_display = true,
   .KHR_get_display_properties2 = true,
   .EXT_direct_mode_display = true,
   .EXT_display_surface_counter = true,
   .EXT_acquire_drm_display = true,
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
   .EXT_headless_surface = true,
#endif
   .KHR_device_group_creation = true,
   .KHR_external_fence_capabilities = true,
   .KHR_external_memory_capabilities = true,
   .KHR_external_semaphore_capabilities = true,
   .KHR_get_physical_device_properties2 = true,
   .EXT_debug_report = true,
   .EXT_debug_utils = true,
};

VKAPI_ATTR VkResult VKAPI_CALL
hk_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                        uint32_t *pPropertyCount,
                                        VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &instance_extensions, pPropertyCount, pProperties);
}

/* clang-format off */
static const driOptionDescription hk_dri_options[] = {
   DRI_CONF_SECTION_PERFORMANCE
      DRI_CONF_ADAPTIVE_SYNC(true)
      DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(0)
      DRI_CONF_VK_X11_STRICT_IMAGE_COUNT(false)
      DRI_CONF_VK_X11_ENSURE_MIN_IMAGE_COUNT(false)
      DRI_CONF_VK_KHR_PRESENT_WAIT(false)
      DRI_CONF_VK_XWAYLAND_WAIT_READY(false)
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_DEBUG
      DRI_CONF_FORCE_VK_VENDOR()
      DRI_CONF_VK_WSI_FORCE_SWAPCHAIN_TO_CURRENT_EXTENT(false)
      DRI_CONF_VK_X11_IGNORE_SUBOPTIMAL(false)
   DRI_CONF_SECTION_END

   DRI_CONF_SECTION_MISCELLANEOUS
      DRI_CONF_HK_DISABLE_RGBA4_BORDER_COLOR_WORKAROUND(false)
      DRI_CONF_HK_DISABLE_BORDER_EMULATION(false)
   DRI_CONF_SECTION_END
};
/* clang-format on */

static void
hk_init_dri_options(struct hk_instance *instance)
{
   driParseOptionInfo(&instance->available_dri_options, hk_dri_options,
                      ARRAY_SIZE(hk_dri_options));
   driParseConfigFiles(
      &instance->dri_options, &instance->available_dri_options, 0, "hk", NULL,
      NULL, instance->vk.app_info.app_name, instance->vk.app_info.app_version,
      instance->vk.app_info.engine_name, instance->vk.app_info.engine_version);

   instance->force_vk_vendor =
      driQueryOptioni(&instance->dri_options, "force_vk_vendor");

   instance->workaround_rgba4 = !driQueryOptionb(
      &instance->dri_options, "hk_disable_rgba4_border_color_workaround");

   instance->no_border =
      driQueryOptionb(&instance->dri_options, "hk_disable_border_emulation");
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkInstance *pInstance)
{
   struct hk_instance *instance;
   VkResult result;

   if (pAllocator == NULL)
      pAllocator = vk_default_allocator();

   instance = vk_alloc(pAllocator, sizeof(*instance), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(&dispatch_table,
                                               &hk_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_instance_entrypoints, false);

   result = vk_instance_init(&instance->vk, &instance_extensions,
                             &dispatch_table, pCreateInfo, pAllocator);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   hk_init_dri_options(instance);

   instance->vk.physical_devices.try_create_for_drm =
      hk_create_drm_physical_device;
   instance->vk.physical_devices.destroy = hk_physical_device_destroy;

   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(hk_CreateInstance);
   if (!note) {
      result = vk_errorf(NULL, VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to find build-id");
      goto fail_init;
   }

   unsigned build_id_len = build_id_length(note);
   if (build_id_len < SHA1_DIGEST_LENGTH) {
      result = vk_errorf(NULL, VK_ERROR_INITIALIZATION_FAILED,
                         "build-id too short.  It needs to be a SHA");
      goto fail_init;
   }

   static_assert(sizeof(instance->driver_build_sha) == SHA1_DIGEST_LENGTH);
   memcpy(instance->driver_build_sha, build_id_data(note), SHA1_DIGEST_LENGTH);

   *pInstance = hk_instance_to_handle(instance);
   return VK_SUCCESS;

fail_init:
   vk_instance_finish(&instance->vk);
fail_alloc:
   vk_free(pAllocator, instance);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
hk_DestroyInstance(VkInstance _instance,
                   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_instance, instance, _instance);

   if (!instance)
      return;

   driDestroyOptionCache(&instance->dri_options);
   driDestroyOptionInfo(&instance->available_dri_options);

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
hk_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   VK_FROM_HANDLE(hk_instance, instance, _instance);
   return vk_instance_get_proc_addr(&instance->vk, &hk_instance_entrypoints,
                                    pName);
}

PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return hk_GetInstanceProcAddr(instance, pName);
}
