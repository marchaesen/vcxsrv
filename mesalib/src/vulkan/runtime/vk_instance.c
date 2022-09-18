/*
 * Copyright © 2021 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "vk_instance.h"

#ifdef HAVE_LIBDRM
#include <xf86drm.h>
#endif

#include "vk_alloc.h"
#include "vk_common_entrypoints.h"
#include "vk_dispatch_trampolines.h"
#include "vk_log.h"
#include "vk_util.h"
#include "vk_debug_utils.h"
#include "vk_physical_device.h"

#include "compiler/glsl_types.h"

#define VERSION_IS_1_0(version) \
   (VK_API_VERSION_MAJOR(version) == 1 && VK_API_VERSION_MINOR(version) == 0)

VkResult
vk_instance_init(struct vk_instance *instance,
                 const struct vk_instance_extension_table *supported_extensions,
                 const struct vk_instance_dispatch_table *dispatch_table,
                 const VkInstanceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *alloc)
{
   memset(instance, 0, sizeof(*instance));
   vk_object_base_init(NULL, &instance->base, VK_OBJECT_TYPE_INSTANCE);
   instance->alloc = *alloc;

   /* VK_EXT_debug_utils */
   /* These messengers will only be used during vkCreateInstance or
    * vkDestroyInstance calls.  We do this first so that it's safe to use
    * vk_errorf and friends.
    */
   list_inithead(&instance->debug_utils.instance_callbacks);
   vk_foreach_struct_const(ext, pCreateInfo->pNext) {
      if (ext->sType ==
          VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT) {
         const VkDebugUtilsMessengerCreateInfoEXT *debugMessengerCreateInfo =
            (const VkDebugUtilsMessengerCreateInfoEXT *)ext;
         struct vk_debug_utils_messenger *messenger =
            vk_alloc2(alloc, alloc, sizeof(struct vk_debug_utils_messenger), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

         if (!messenger)
            return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

         vk_object_base_init(NULL, &messenger->base,
                             VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT);

         messenger->alloc = *alloc;
         messenger->severity = debugMessengerCreateInfo->messageSeverity;
         messenger->type = debugMessengerCreateInfo->messageType;
         messenger->callback = debugMessengerCreateInfo->pfnUserCallback;
         messenger->data = debugMessengerCreateInfo->pUserData;

         list_addtail(&messenger->link,
                      &instance->debug_utils.instance_callbacks);
      }
   }

   uint32_t instance_version = VK_API_VERSION_1_0;
   if (dispatch_table->EnumerateInstanceVersion)
      dispatch_table->EnumerateInstanceVersion(&instance_version);

   instance->app_info = (struct vk_app_info) { .api_version = 0 };
   if (pCreateInfo->pApplicationInfo) {
      const VkApplicationInfo *app = pCreateInfo->pApplicationInfo;

      instance->app_info.app_name =
         vk_strdup(&instance->alloc, app->pApplicationName,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
      instance->app_info.app_version = app->applicationVersion;

      instance->app_info.engine_name =
         vk_strdup(&instance->alloc, app->pEngineName,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
      instance->app_info.engine_version = app->engineVersion;

      instance->app_info.api_version = app->apiVersion;
   }

   /* From the Vulkan 1.2.199 spec:
    *
    *    "Note:
    *
    *    Providing a NULL VkInstanceCreateInfo::pApplicationInfo or providing
    *    an apiVersion of 0 is equivalent to providing an apiVersion of
    *    VK_MAKE_API_VERSION(0,1,0,0)."
    */
   if (instance->app_info.api_version == 0)
      instance->app_info.api_version = VK_API_VERSION_1_0;

   /* From the Vulkan 1.2.199 spec:
    *
    *    VUID-VkApplicationInfo-apiVersion-04010
    *
    *    "If apiVersion is not 0, then it must be greater than or equal to
    *    VK_API_VERSION_1_0"
    */
   assert(instance->app_info.api_version >= VK_API_VERSION_1_0);

   /* From the Vulkan 1.2.199 spec:
    *
    *    "Vulkan 1.0 implementations were required to return
    *    VK_ERROR_INCOMPATIBLE_DRIVER if apiVersion was larger than 1.0.
    *    Implementations that support Vulkan 1.1 or later must not return
    *    VK_ERROR_INCOMPATIBLE_DRIVER for any value of apiVersion."
    */
   if (VERSION_IS_1_0(instance_version) &&
       !VERSION_IS_1_0(instance->app_info.api_version))
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < VK_INSTANCE_EXTENSION_COUNT; idx++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    vk_instance_extensions[idx].extensionName) == 0)
            break;
      }

      if (idx >= VK_INSTANCE_EXTENSION_COUNT)
         return vk_errorf(instance, VK_ERROR_EXTENSION_NOT_PRESENT,
                          "%s not supported",
                          pCreateInfo->ppEnabledExtensionNames[i]);

      if (!supported_extensions->extensions[idx])
         return vk_errorf(instance, VK_ERROR_EXTENSION_NOT_PRESENT,
                          "%s not supported",
                          pCreateInfo->ppEnabledExtensionNames[i]);

#ifdef ANDROID
      if (!vk_android_allowed_instance_extensions.extensions[idx])
         return vk_errorf(instance, VK_ERROR_EXTENSION_NOT_PRESENT,
                          "%s not supported",
                          pCreateInfo->ppEnabledExtensionNames[i]);
#endif

      instance->enabled_extensions.extensions[idx] = true;
   }

   instance->dispatch_table = *dispatch_table;

   /* Add common entrypoints without overwriting driver-provided ones. */
   vk_instance_dispatch_table_from_entrypoints(
      &instance->dispatch_table, &vk_common_instance_entrypoints, false);

   if (mtx_init(&instance->debug_report.callbacks_mutex, mtx_plain) != 0)
      return vk_error(instance, VK_ERROR_INITIALIZATION_FAILED);

   list_inithead(&instance->debug_report.callbacks);

   if (mtx_init(&instance->debug_utils.callbacks_mutex, mtx_plain) != 0) {
      mtx_destroy(&instance->debug_report.callbacks_mutex);
      return vk_error(instance, VK_ERROR_INITIALIZATION_FAILED);
   }

   list_inithead(&instance->debug_utils.callbacks);

   list_inithead(&instance->physical_devices.list);

   if (mtx_init(&instance->physical_devices.mutex, mtx_plain) != 0) {
      mtx_destroy(&instance->debug_report.callbacks_mutex);
      mtx_destroy(&instance->debug_utils.callbacks_mutex);
      return vk_error(instance, VK_ERROR_INITIALIZATION_FAILED);
   }

   glsl_type_singleton_init_or_ref();

   return VK_SUCCESS;
}

static void
destroy_physical_devices(struct vk_instance *instance)
{
   list_for_each_entry_safe(struct vk_physical_device, pdevice,
                            &instance->physical_devices.list, link) {
      list_del(&pdevice->link);
      instance->physical_devices.destroy(pdevice);
   }
}

void
vk_instance_finish(struct vk_instance *instance)
{
   destroy_physical_devices(instance);

   glsl_type_singleton_decref();
   if (unlikely(!list_is_empty(&instance->debug_utils.callbacks))) {
      list_for_each_entry_safe(struct vk_debug_utils_messenger, messenger,
                               &instance->debug_utils.callbacks, link) {
         list_del(&messenger->link);
         vk_object_base_finish(&messenger->base);
         vk_free2(&instance->alloc, &messenger->alloc, messenger);
      }
   }
   if (unlikely(!list_is_empty(&instance->debug_utils.instance_callbacks))) {
      list_for_each_entry_safe(struct vk_debug_utils_messenger, messenger,
                               &instance->debug_utils.instance_callbacks,
                               link) {
         list_del(&messenger->link);
         vk_object_base_finish(&messenger->base);
         vk_free2(&instance->alloc, &messenger->alloc, messenger);
      }
   }
   mtx_destroy(&instance->debug_report.callbacks_mutex);
   mtx_destroy(&instance->debug_utils.callbacks_mutex);
   mtx_destroy(&instance->physical_devices.mutex);
   vk_free(&instance->alloc, (char *)instance->app_info.app_name);
   vk_free(&instance->alloc, (char *)instance->app_info.engine_name);
   vk_object_base_finish(&instance->base);
}

VkResult
vk_enumerate_instance_extension_properties(
    const struct vk_instance_extension_table *supported_extensions,
    uint32_t *pPropertyCount,
    VkExtensionProperties *pProperties)
{
   VK_OUTARRAY_MAKE_TYPED(VkExtensionProperties, out, pProperties, pPropertyCount);

   for (int i = 0; i < VK_INSTANCE_EXTENSION_COUNT; i++) {
      if (!supported_extensions->extensions[i])
         continue;

#ifdef ANDROID
      if (!vk_android_allowed_instance_extensions.extensions[i])
         continue;
#endif

      vk_outarray_append_typed(VkExtensionProperties, &out, prop) {
         *prop = vk_instance_extensions[i];
      }
   }

   return vk_outarray_status(&out);
}

PFN_vkVoidFunction
vk_instance_get_proc_addr(const struct vk_instance *instance,
                          const struct vk_instance_entrypoint_table *entrypoints,
                          const char *name)
{
   PFN_vkVoidFunction func;

   /* The Vulkan 1.0 spec for vkGetInstanceProcAddr has a table of exactly
    * when we have to return valid function pointers, NULL, or it's left
    * undefined.  See the table for exact details.
    */
   if (name == NULL)
      return NULL;

#define LOOKUP_VK_ENTRYPOINT(entrypoint) \
   if (strcmp(name, "vk" #entrypoint) == 0) \
      return (PFN_vkVoidFunction)entrypoints->entrypoint

   LOOKUP_VK_ENTRYPOINT(EnumerateInstanceExtensionProperties);
   LOOKUP_VK_ENTRYPOINT(EnumerateInstanceLayerProperties);
   LOOKUP_VK_ENTRYPOINT(EnumerateInstanceVersion);
   LOOKUP_VK_ENTRYPOINT(CreateInstance);

   /* GetInstanceProcAddr() can also be called with a NULL instance.
    * See https://gitlab.khronos.org/vulkan/vulkan/issues/2057
    */
   LOOKUP_VK_ENTRYPOINT(GetInstanceProcAddr);

#undef LOOKUP_VK_ENTRYPOINT

   if (instance == NULL)
      return NULL;

   func = vk_instance_dispatch_table_get_if_supported(&instance->dispatch_table,
                                                      name,
                                                      instance->app_info.api_version,
                                                      &instance->enabled_extensions);
   if (func != NULL)
      return func;

   func = vk_physical_device_dispatch_table_get_if_supported(&vk_physical_device_trampolines,
                                                             name,
                                                             instance->app_info.api_version,
                                                             &instance->enabled_extensions);
   if (func != NULL)
      return func;

   func = vk_device_dispatch_table_get_if_supported(&vk_device_trampolines,
                                                    name,
                                                    instance->app_info.api_version,
                                                    &instance->enabled_extensions,
                                                    NULL);
   if (func != NULL)
      return func;

   return NULL;
}

PFN_vkVoidFunction
vk_instance_get_proc_addr_unchecked(const struct vk_instance *instance,
                                    const char *name)
{
   PFN_vkVoidFunction func;

   if (instance == NULL || name == NULL)
      return NULL;

   func = vk_instance_dispatch_table_get(&instance->dispatch_table, name);
   if (func != NULL)
      return func;

   func = vk_physical_device_dispatch_table_get(
      &vk_physical_device_trampolines, name);
   if (func != NULL)
      return func;

   func = vk_device_dispatch_table_get(&vk_device_trampolines, name);
   if (func != NULL)
      return func;

   return NULL;
}

PFN_vkVoidFunction
vk_instance_get_physical_device_proc_addr(const struct vk_instance *instance,
                                          const char *name)
{
   if (instance == NULL || name == NULL)
      return NULL;

   return vk_physical_device_dispatch_table_get_if_supported(&vk_physical_device_trampolines,
                                                             name,
                                                             instance->app_info.api_version,
                                                             &instance->enabled_extensions);
}

static VkResult
enumerate_drm_physical_devices_locked(struct vk_instance *instance)
{
#ifdef HAVE_LIBDRM
   /* TODO: Check for more devices ? */
   drmDevicePtr devices[8];
   int max_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));

   if (max_devices < 1)
      return VK_SUCCESS;

   VkResult result;
   for (uint32_t i = 0; i < (uint32_t)max_devices; i++) {
      struct vk_physical_device *pdevice;
      result = instance->physical_devices.try_create_for_drm(instance, devices[i], &pdevice);

      /* Incompatible DRM device, skip. */
      if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
         result = VK_SUCCESS;
         continue;
      }

      /* Error creating the physical device, report the error. */
      if (result != VK_SUCCESS)
         break;

      list_addtail(&pdevice->link, &instance->physical_devices.list);
   }

   drmFreeDevices(devices, max_devices);
   return result;
#endif
   return VK_SUCCESS;
}

static VkResult
enumerate_physical_devices_locked(struct vk_instance *instance)
{
   if (instance->physical_devices.enumerate)
      return instance->physical_devices.enumerate(instance);

   VkResult result = VK_SUCCESS;

   if (instance->physical_devices.try_create_for_drm) {
      result = enumerate_drm_physical_devices_locked(instance);
      if (result != VK_SUCCESS) {
         destroy_physical_devices(instance);
         return result;
      }
   }

   return result;
}

static VkResult
enumerate_physical_devices(struct vk_instance *instance)
{
   VkResult result = VK_SUCCESS;

   mtx_lock(&instance->physical_devices.mutex);
   if (!instance->physical_devices.enumerated) {
      result = enumerate_physical_devices_locked(instance);
      if (result == VK_SUCCESS)
         instance->physical_devices.enumerated = true;
   }
   mtx_unlock(&instance->physical_devices.mutex);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_EnumeratePhysicalDevices(VkInstance _instance, uint32_t *pPhysicalDeviceCount,
                                   VkPhysicalDevice *pPhysicalDevices)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDevice, out, pPhysicalDevices, pPhysicalDeviceCount);

   VkResult result = enumerate_physical_devices(instance);
   if (result != VK_SUCCESS)
      return result;

   list_for_each_entry(struct vk_physical_device, pdevice,
                       &instance->physical_devices.list, link) {
      vk_outarray_append_typed(VkPhysicalDevice, &out, element) {
         *element = vk_physical_device_to_handle(pdevice);
      }
   }

   return vk_outarray_status(&out);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_EnumeratePhysicalDeviceGroups(VkInstance _instance, uint32_t *pGroupCount,
                                        VkPhysicalDeviceGroupProperties *pGroupProperties)
{
   VK_FROM_HANDLE(vk_instance, instance, _instance);
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDeviceGroupProperties, out, pGroupProperties,
                          pGroupCount);

   VkResult result = enumerate_physical_devices(instance);
   if (result != VK_SUCCESS)
      return result;

   list_for_each_entry(struct vk_physical_device, pdevice,
                       &instance->physical_devices.list, link) {
      vk_outarray_append_typed(VkPhysicalDeviceGroupProperties, &out, p) {
         p->physicalDeviceCount = 1;
         memset(p->physicalDevices, 0, sizeof(p->physicalDevices));
         p->physicalDevices[0] = vk_physical_device_to_handle(pdevice);
         p->subsetAllocation = false;
      }
   }

   return vk_outarray_status(&out);
}
