/*
 * Copyright © 2020 Intel Corporation
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

#include "vk_device.h"

#include "vk_common_entrypoints.h"
#include "vk_instance.h"
#include "vk_log.h"
#include "vk_physical_device.h"
#include "vk_queue.h"
#include "vk_sync.h"
#include "vk_sync_timeline.h"
#include "vk_util.h"
#include "util/debug.h"
#include "util/hash_table.h"
#include "util/ralloc.h"

static enum vk_device_timeline_mode
get_timeline_mode(struct vk_physical_device *physical_device)
{
   if (physical_device->supported_sync_types == NULL)
      return VK_DEVICE_TIMELINE_MODE_NONE;

   const struct vk_sync_type *timeline_type = NULL;
   for (const struct vk_sync_type *const *t =
        physical_device->supported_sync_types; *t; t++) {
      if ((*t)->features & VK_SYNC_FEATURE_TIMELINE) {
         /* We can only have one timeline mode */
         assert(timeline_type == NULL);
         timeline_type = *t;
      }
   }

   if (timeline_type == NULL)
      return VK_DEVICE_TIMELINE_MODE_NONE;

   if (vk_sync_type_is_vk_sync_timeline(timeline_type))
      return VK_DEVICE_TIMELINE_MODE_EMULATED;

   if (timeline_type->features & VK_SYNC_FEATURE_WAIT_BEFORE_SIGNAL)
      return VK_DEVICE_TIMELINE_MODE_NATIVE;

   /* For assisted mode, we require a few additional things of all sync types
    * which may be used as semaphores.
    */
   for (const struct vk_sync_type *const *t =
        physical_device->supported_sync_types; *t; t++) {
      if ((*t)->features & VK_SYNC_FEATURE_GPU_WAIT) {
         assert((*t)->features & VK_SYNC_FEATURE_WAIT_PENDING);
         if ((*t)->features & VK_SYNC_FEATURE_BINARY)
            assert((*t)->features & VK_SYNC_FEATURE_CPU_RESET);
      }
   }

   return VK_DEVICE_TIMELINE_MODE_ASSISTED;
}

static void
collect_enabled_features(struct vk_device *device,
                         const VkDeviceCreateInfo *pCreateInfo)
{
   if (pCreateInfo->pEnabledFeatures) {
      if (pCreateInfo->pEnabledFeatures->robustBufferAccess)
         device->enabled_features.robustBufferAccess = true;
   }

   vk_foreach_struct_const(ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: {
         const VkPhysicalDeviceFeatures2 *features = (const void *)ext;
         if (features->features.robustBufferAccess)
            device->enabled_features.robustBufferAccess = true;
         break;
      }

      default:
         /* Don't warn */
         break;
      }
   }
}

VkResult
vk_device_init(struct vk_device *device,
               struct vk_physical_device *physical_device,
               const struct vk_device_dispatch_table *dispatch_table,
               const VkDeviceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *alloc)
{
   memset(device, 0, sizeof(*device));
   vk_object_base_init(device, &device->base, VK_OBJECT_TYPE_DEVICE);
   if (alloc != NULL)
      device->alloc = *alloc;
   else
      device->alloc = physical_device->instance->alloc;

   device->physical = physical_device;

   device->dispatch_table = *dispatch_table;

   /* Add common entrypoints without overwriting driver-provided ones. */
   vk_device_dispatch_table_from_entrypoints(
      &device->dispatch_table, &vk_common_device_entrypoints, false);

   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < VK_DEVICE_EXTENSION_COUNT; idx++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    vk_device_extensions[idx].extensionName) == 0)
            break;
      }

      if (idx >= VK_DEVICE_EXTENSION_COUNT)
         return vk_errorf(physical_device, VK_ERROR_EXTENSION_NOT_PRESENT,
                          "%s not supported",
                          pCreateInfo->ppEnabledExtensionNames[i]);

      if (!physical_device->supported_extensions.extensions[idx])
         return vk_errorf(physical_device, VK_ERROR_EXTENSION_NOT_PRESENT,
                          "%s not supported",
                          pCreateInfo->ppEnabledExtensionNames[i]);

#ifdef ANDROID
      if (!vk_android_allowed_device_extensions.extensions[idx])
         return vk_errorf(physical_device, VK_ERROR_EXTENSION_NOT_PRESENT,
                          "%s not supported",
                          pCreateInfo->ppEnabledExtensionNames[i]);
#endif

      device->enabled_extensions.extensions[idx] = true;
   }

   VkResult result =
      vk_physical_device_check_device_features(physical_device,
                                               pCreateInfo);
   if (result != VK_SUCCESS)
      return result;

   collect_enabled_features(device, pCreateInfo);

   p_atomic_set(&device->private_data_next_index, 0);

   list_inithead(&device->queues);

   device->drm_fd = -1;

   device->timeline_mode = get_timeline_mode(physical_device);

   switch (device->timeline_mode) {
   case VK_DEVICE_TIMELINE_MODE_NONE:
   case VK_DEVICE_TIMELINE_MODE_NATIVE:
      device->submit_mode = VK_QUEUE_SUBMIT_MODE_IMMEDIATE;
      break;

   case VK_DEVICE_TIMELINE_MODE_EMULATED:
      device->submit_mode = VK_QUEUE_SUBMIT_MODE_DEFERRED;
      break;

   case VK_DEVICE_TIMELINE_MODE_ASSISTED:
      if (env_var_as_boolean("MESA_VK_ENABLE_SUBMIT_THREAD", false)) {
         device->submit_mode = VK_QUEUE_SUBMIT_MODE_THREADED;
      } else {
         device->submit_mode = VK_QUEUE_SUBMIT_MODE_THREADED_ON_DEMAND;
      }
      break;

   default:
      unreachable("Invalid timeline mode");
   }

#ifdef ANDROID
   mtx_init(&device->swapchain_private_mtx, mtx_plain);
   device->swapchain_private = NULL;
#endif /* ANDROID */

   return VK_SUCCESS;
}

void
vk_device_finish(UNUSED struct vk_device *device)
{
   /* Drivers should tear down their own queues */
   assert(list_is_empty(&device->queues));

#ifdef ANDROID
   if (device->swapchain_private) {
      hash_table_foreach(device->swapchain_private, entry)
         util_sparse_array_finish(entry->data);
      ralloc_free(device->swapchain_private);
   }
#endif /* ANDROID */

   vk_object_base_finish(&device->base);
}

void
vk_device_enable_threaded_submit(struct vk_device *device)
{
   /* This must be called before any queues are created */
   assert(list_is_empty(&device->queues));

   /* In order to use threaded submit, we need every sync type that can be
    * used as a wait fence for vkQueueSubmit() to support WAIT_PENDING.
    * It's required for cross-thread/process submit re-ordering.
    */
   for (const struct vk_sync_type *const *t =
        device->physical->supported_sync_types; *t; t++) {
      if ((*t)->features & VK_SYNC_FEATURE_GPU_WAIT)
         assert((*t)->features & VK_SYNC_FEATURE_WAIT_PENDING);
   }

   /* Any binary vk_sync types which will be used as permanent semaphore
    * payloads also need to support vk_sync_type::move, but that's a lot
    * harder to assert since it only applies to permanent semaphore payloads.
    */

   if (device->submit_mode != VK_QUEUE_SUBMIT_MODE_THREADED)
      device->submit_mode = VK_QUEUE_SUBMIT_MODE_THREADED_ON_DEMAND;
}

VkResult
vk_device_flush(struct vk_device *device)
{
   if (device->submit_mode != VK_QUEUE_SUBMIT_MODE_DEFERRED)
      return VK_SUCCESS;

   bool progress;
   do {
      progress = false;

      vk_foreach_queue(queue, device) {
         uint32_t queue_submit_count;
         VkResult result = vk_queue_flush(queue, &queue_submit_count);
         if (unlikely(result != VK_SUCCESS))
            return result;

         if (queue_submit_count)
            progress = true;
      }
   } while (progress);

   return VK_SUCCESS;
}

static const char *
timeline_mode_str(struct vk_device *device)
{
   switch (device->timeline_mode) {
#define CASE(X) case VK_DEVICE_TIMELINE_MODE_##X: return #X;
   CASE(NONE)
   CASE(EMULATED)
   CASE(ASSISTED)
   CASE(NATIVE)
#undef CASE
   default: return "UNKNOWN";
   }
}

void
_vk_device_report_lost(struct vk_device *device)
{
   assert(p_atomic_read(&device->_lost.lost) > 0);

   device->_lost.reported = true;

   vk_foreach_queue(queue, device) {
      if (queue->_lost.lost) {
         __vk_errorf(queue, VK_ERROR_DEVICE_LOST,
                     queue->_lost.error_file, queue->_lost.error_line,
                     "%s", queue->_lost.error_msg);
      }
   }

   vk_logd(VK_LOG_OBJS(device), "Timeline mode is %s.",
           timeline_mode_str(device));
}

VkResult
_vk_device_set_lost(struct vk_device *device,
                    const char *file, int line,
                    const char *msg, ...)
{
   /* This flushes out any per-queue device lost messages */
   if (vk_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   p_atomic_inc(&device->_lost.lost);
   device->_lost.reported = true;

   va_list ap;
   va_start(ap, msg);
   __vk_errorv(device, VK_ERROR_DEVICE_LOST, file, line, msg, ap);
   va_end(ap);

   vk_logd(VK_LOG_OBJS(device), "Timeline mode is %s.",
           timeline_mode_str(device));

   if (env_var_as_boolean("MESA_VK_ABORT_ON_DEVICE_LOSS", false))
      abort();

   return VK_ERROR_DEVICE_LOST;
}

PFN_vkVoidFunction
vk_device_get_proc_addr(const struct vk_device *device,
                        const char *name)
{
   if (device == NULL || name == NULL)
      return NULL;

   struct vk_instance *instance = device->physical->instance;
   return vk_device_dispatch_table_get_if_supported(&device->dispatch_table,
                                                    name,
                                                    instance->app_info.api_version,
                                                    &instance->enabled_extensions,
                                                    &device->enabled_extensions);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_common_GetDeviceProcAddr(VkDevice _device,
                            const char *pName)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   return vk_device_get_proc_addr(device, pName);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_GetDeviceQueue(VkDevice _device,
                         uint32_t queueFamilyIndex,
                         uint32_t queueIndex,
                         VkQueue *pQueue)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   const VkDeviceQueueInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
      .pNext = NULL,
      /* flags = 0 because (Vulkan spec 1.2.170 - vkGetDeviceQueue):
       *
       *    "vkGetDeviceQueue must only be used to get queues that were
       *     created with the flags parameter of VkDeviceQueueCreateInfo set
       *     to zero. To get queues that were created with a non-zero flags
       *     parameter use vkGetDeviceQueue2."
       */
      .flags = 0,
      .queueFamilyIndex = queueFamilyIndex,
      .queueIndex = queueIndex,
   };

   device->dispatch_table.GetDeviceQueue2(_device, &info, pQueue);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_GetDeviceQueue2(VkDevice _device,
                          const VkDeviceQueueInfo2 *pQueueInfo,
                          VkQueue *pQueue)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   struct vk_queue *queue = NULL;
   vk_foreach_queue(iter, device) {
      if (iter->queue_family_index == pQueueInfo->queueFamilyIndex &&
          iter->index_in_family == pQueueInfo->queueIndex) {
         queue = iter;
         break;
      }
   }

   /* From the Vulkan 1.1.70 spec:
    *
    *    "The queue returned by vkGetDeviceQueue2 must have the same flags
    *    value from this structure as that used at device creation time in a
    *    VkDeviceQueueCreateInfo instance. If no matching flags were specified
    *    at device creation time then pQueue will return VK_NULL_HANDLE."
    */
   if (queue && queue->flags == pQueueInfo->flags)
      *pQueue = vk_queue_to_handle(queue);
   else
      *pQueue = VK_NULL_HANDLE;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_GetBufferMemoryRequirements(VkDevice _device,
                                      VkBuffer buffer,
                                      VkMemoryRequirements *pMemoryRequirements)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   VkBufferMemoryRequirementsInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
      .buffer = buffer,
   };
   VkMemoryRequirements2 reqs = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };
   device->dispatch_table.GetBufferMemoryRequirements2(_device, &info, &reqs);

   *pMemoryRequirements = reqs.memoryRequirements;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_BindBufferMemory(VkDevice _device,
                           VkBuffer buffer,
                           VkDeviceMemory memory,
                           VkDeviceSize memoryOffset)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   VkBindBufferMemoryInfo bind = {
      .sType         = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
      .buffer        = buffer,
      .memory        = memory,
      .memoryOffset  = memoryOffset,
   };

   return device->dispatch_table.BindBufferMemory2(_device, 1, &bind);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_GetImageMemoryRequirements(VkDevice _device,
                                     VkImage image,
                                     VkMemoryRequirements *pMemoryRequirements)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   VkImageMemoryRequirementsInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      .image = image,
   };
   VkMemoryRequirements2 reqs = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };
   device->dispatch_table.GetImageMemoryRequirements2(_device, &info, &reqs);

   *pMemoryRequirements = reqs.memoryRequirements;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_BindImageMemory(VkDevice _device,
                          VkImage image,
                          VkDeviceMemory memory,
                          VkDeviceSize memoryOffset)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   VkBindImageMemoryInfo bind = {
      .sType         = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
      .image         = image,
      .memory        = memory,
      .memoryOffset  = memoryOffset,
   };

   return device->dispatch_table.BindImageMemory2(_device, 1, &bind);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_GetImageSparseMemoryRequirements(VkDevice _device,
                                           VkImage image,
                                           uint32_t *pSparseMemoryRequirementCount,
                                           VkSparseImageMemoryRequirements *pSparseMemoryRequirements)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   VkImageSparseMemoryRequirementsInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2,
      .image = image,
   };

   if (!pSparseMemoryRequirements) {
      device->dispatch_table.GetImageSparseMemoryRequirements2(_device,
                                                               &info,
                                                               pSparseMemoryRequirementCount,
                                                               NULL);
      return;
   }

   STACK_ARRAY(VkSparseImageMemoryRequirements2, mem_reqs2, *pSparseMemoryRequirementCount);

   for (unsigned i = 0; i < *pSparseMemoryRequirementCount; ++i) {
      mem_reqs2[i].sType = VK_STRUCTURE_TYPE_SPARSE_IMAGE_MEMORY_REQUIREMENTS_2;
      mem_reqs2[i].pNext = NULL;
   }

   device->dispatch_table.GetImageSparseMemoryRequirements2(_device,
                                                            &info,
                                                            pSparseMemoryRequirementCount,
                                                            mem_reqs2);

   for (unsigned i = 0; i < *pSparseMemoryRequirementCount; ++i)
      pSparseMemoryRequirements[i] = mem_reqs2[i].memoryRequirements;

   STACK_ARRAY_FINISH(mem_reqs2);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_DeviceWaitIdle(VkDevice _device)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   const struct vk_device_dispatch_table *disp = &device->dispatch_table;

   vk_foreach_queue(queue, device) {
      VkResult result = disp->QueueWaitIdle(vk_queue_to_handle(queue));
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

#ifndef _WIN32

uint64_t
vk_clock_gettime(clockid_t clock_id)
{
   struct timespec current;
   int ret;

   ret = clock_gettime(clock_id, &current);
#ifdef CLOCK_MONOTONIC_RAW
   if (ret < 0 && clock_id == CLOCK_MONOTONIC_RAW)
      ret = clock_gettime(CLOCK_MONOTONIC, &current);
#endif
   if (ret < 0)
      return 0;

   return (uint64_t)current.tv_sec * 1000000000ULL + current.tv_nsec;
}

#endif //!_WIN32

#define CORE_FEATURE(feature) features->feature = core->feature

bool
vk_get_physical_device_core_1_1_feature_ext(struct VkBaseOutStructure *ext,
                                            const VkPhysicalDeviceVulkan11Features *core)
{

   switch (ext->sType) {
   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
      VkPhysicalDevice16BitStorageFeatures *features = (void *)ext;
      CORE_FEATURE(storageBuffer16BitAccess);
      CORE_FEATURE(uniformAndStorageBuffer16BitAccess);
      CORE_FEATURE(storagePushConstant16);
      CORE_FEATURE(storageInputOutput16);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES: {
      VkPhysicalDeviceMultiviewFeatures *features = (void *)ext;
      CORE_FEATURE(multiview);
      CORE_FEATURE(multiviewGeometryShader);
      CORE_FEATURE(multiviewTessellationShader);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES: {
      VkPhysicalDeviceProtectedMemoryFeatures *features = (void *)ext;
      CORE_FEATURE(protectedMemory);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES: {
      VkPhysicalDeviceSamplerYcbcrConversionFeatures *features = (void *) ext;
      CORE_FEATURE(samplerYcbcrConversion);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES: {
      VkPhysicalDeviceShaderDrawParametersFeatures *features = (void *)ext;
      CORE_FEATURE(shaderDrawParameters);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: {
      VkPhysicalDeviceVariablePointersFeatures *features = (void *)ext;
      CORE_FEATURE(variablePointersStorageBuffer);
      CORE_FEATURE(variablePointers);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
      vk_copy_struct_guts(ext, (void *)core, sizeof(*core));
      return true;

   default:
      return false;
   }
}

bool
vk_get_physical_device_core_1_2_feature_ext(struct VkBaseOutStructure *ext,
                                            const VkPhysicalDeviceVulkan12Features *core)
{

   switch (ext->sType) {
   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES: {
      VkPhysicalDevice8BitStorageFeatures *features = (void *)ext;
      CORE_FEATURE(storageBuffer8BitAccess);
      CORE_FEATURE(uniformAndStorageBuffer8BitAccess);
      CORE_FEATURE(storagePushConstant8);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES: {
      VkPhysicalDeviceBufferDeviceAddressFeatures *features = (void *)ext;
      CORE_FEATURE(bufferDeviceAddress);
      CORE_FEATURE(bufferDeviceAddressCaptureReplay);
      CORE_FEATURE(bufferDeviceAddressMultiDevice);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES: {
      VkPhysicalDeviceDescriptorIndexingFeatures *features = (void *)ext;
      CORE_FEATURE(shaderInputAttachmentArrayDynamicIndexing);
      CORE_FEATURE(shaderUniformTexelBufferArrayDynamicIndexing);
      CORE_FEATURE(shaderStorageTexelBufferArrayDynamicIndexing);
      CORE_FEATURE(shaderUniformBufferArrayNonUniformIndexing);
      CORE_FEATURE(shaderSampledImageArrayNonUniformIndexing);
      CORE_FEATURE(shaderStorageBufferArrayNonUniformIndexing);
      CORE_FEATURE(shaderStorageImageArrayNonUniformIndexing);
      CORE_FEATURE(shaderInputAttachmentArrayNonUniformIndexing);
      CORE_FEATURE(shaderUniformTexelBufferArrayNonUniformIndexing);
      CORE_FEATURE(shaderStorageTexelBufferArrayNonUniformIndexing);
      CORE_FEATURE(descriptorBindingUniformBufferUpdateAfterBind);
      CORE_FEATURE(descriptorBindingSampledImageUpdateAfterBind);
      CORE_FEATURE(descriptorBindingStorageImageUpdateAfterBind);
      CORE_FEATURE(descriptorBindingStorageBufferUpdateAfterBind);
      CORE_FEATURE(descriptorBindingUniformTexelBufferUpdateAfterBind);
      CORE_FEATURE(descriptorBindingStorageTexelBufferUpdateAfterBind);
      CORE_FEATURE(descriptorBindingUpdateUnusedWhilePending);
      CORE_FEATURE(descriptorBindingPartiallyBound);
      CORE_FEATURE(descriptorBindingVariableDescriptorCount);
      CORE_FEATURE(runtimeDescriptorArray);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: {
      VkPhysicalDeviceShaderFloat16Int8Features *features = (void *)ext;
      CORE_FEATURE(shaderFloat16);
      CORE_FEATURE(shaderInt8);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES: {
      VkPhysicalDeviceHostQueryResetFeatures *features = (void *)ext;
      CORE_FEATURE(hostQueryReset);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES: {
      VkPhysicalDeviceImagelessFramebufferFeatures *features = (void *)ext;
      CORE_FEATURE(imagelessFramebuffer);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES: {
      VkPhysicalDeviceScalarBlockLayoutFeatures *features =(void *)ext;
      CORE_FEATURE(scalarBlockLayout);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES: {
      VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures *features = (void *)ext;
      CORE_FEATURE(separateDepthStencilLayouts);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES: {
      VkPhysicalDeviceShaderAtomicInt64Features *features = (void *)ext;
      CORE_FEATURE(shaderBufferInt64Atomics);
      CORE_FEATURE(shaderSharedInt64Atomics);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES: {
      VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures *features = (void *)ext;
      CORE_FEATURE(shaderSubgroupExtendedTypes);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: {
      VkPhysicalDeviceTimelineSemaphoreFeatures *features = (void *) ext;
      CORE_FEATURE(timelineSemaphore);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES: {
      VkPhysicalDeviceUniformBufferStandardLayoutFeatures *features = (void *)ext;
      CORE_FEATURE(uniformBufferStandardLayout);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES: {
      VkPhysicalDeviceVulkanMemoryModelFeatures *features = (void *)ext;
      CORE_FEATURE(vulkanMemoryModel);
      CORE_FEATURE(vulkanMemoryModelDeviceScope);
      CORE_FEATURE(vulkanMemoryModelAvailabilityVisibilityChains);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
      vk_copy_struct_guts(ext, (void *)core, sizeof(*core));
      return true;

   default:
      return false;
   }
}

bool
vk_get_physical_device_core_1_3_feature_ext(struct VkBaseOutStructure *ext,
                                            const VkPhysicalDeviceVulkan13Features *core)
{
   switch (ext->sType) {
   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES: {
      VkPhysicalDeviceDynamicRenderingFeatures *features = (void *)ext;
      CORE_FEATURE(dynamicRendering);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_ROBUSTNESS_FEATURES: {
      VkPhysicalDeviceImageRobustnessFeatures *features = (void *)ext;
      CORE_FEATURE(robustImageAccess);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES: {
      VkPhysicalDeviceInlineUniformBlockFeatures *features = (void *)ext;
      CORE_FEATURE(inlineUniformBlock);
      CORE_FEATURE(descriptorBindingInlineUniformBlockUpdateAfterBind);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES: {
      VkPhysicalDeviceMaintenance4Features *features = (void *)ext;
      CORE_FEATURE(maintenance4);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES: {
      VkPhysicalDevicePipelineCreationCacheControlFeatures *features = (void *)ext;
      CORE_FEATURE(pipelineCreationCacheControl);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES: {
      VkPhysicalDevicePrivateDataFeatures *features = (void *)ext;
      CORE_FEATURE(privateData);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES: {
      VkPhysicalDeviceShaderDemoteToHelperInvocationFeatures *features = (void *)ext;
      CORE_FEATURE(shaderDemoteToHelperInvocation);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_FEATURES: {
      VkPhysicalDeviceShaderIntegerDotProductFeatures *features = (void *)ext;
      CORE_FEATURE(shaderIntegerDotProduct);
      return true;
   };

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_TERMINATE_INVOCATION_FEATURES: {
      VkPhysicalDeviceShaderTerminateInvocationFeatures *features = (void *)ext;
      CORE_FEATURE(shaderTerminateInvocation);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES: {
      VkPhysicalDeviceSubgroupSizeControlFeatures *features = (void *)ext;
      CORE_FEATURE(subgroupSizeControl);
      CORE_FEATURE(computeFullSubgroups);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES: {
      VkPhysicalDeviceSynchronization2Features *features = (void *)ext;
      CORE_FEATURE(synchronization2);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES: {
      VkPhysicalDeviceTextureCompressionASTCHDRFeatures *features = (void *)ext;
      CORE_FEATURE(textureCompressionASTC_HDR);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES: {
      VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeatures *features = (void *)ext;
      CORE_FEATURE(shaderZeroInitializeWorkgroupMemory);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
      vk_copy_struct_guts(ext, (void *)core, sizeof(*core));
      return true;

   default:
      return false;
   }
}

#undef CORE_FEATURE

#define CORE_RENAMED_PROPERTY(ext_property, core_property) \
   memcpy(&properties->ext_property, &core->core_property, sizeof(core->core_property))

#define CORE_PROPERTY(property) CORE_RENAMED_PROPERTY(property, property)

bool
vk_get_physical_device_core_1_1_property_ext(struct VkBaseOutStructure *ext,
                                             const VkPhysicalDeviceVulkan11Properties *core)
{
   switch (ext->sType) {
   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
      VkPhysicalDeviceIDProperties *properties = (void *)ext;
      CORE_PROPERTY(deviceUUID);
      CORE_PROPERTY(driverUUID);
      CORE_PROPERTY(deviceLUID);
      CORE_PROPERTY(deviceNodeMask);
      CORE_PROPERTY(deviceLUIDValid);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: {
      VkPhysicalDeviceMaintenance3Properties *properties = (void *)ext;
      CORE_PROPERTY(maxPerSetDescriptors);
      CORE_PROPERTY(maxMemoryAllocationSize);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES: {
      VkPhysicalDeviceMultiviewProperties *properties = (void *)ext;
      CORE_PROPERTY(maxMultiviewViewCount);
      CORE_PROPERTY(maxMultiviewInstanceIndex);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES: {
      VkPhysicalDevicePointClippingProperties *properties = (void *) ext;
      CORE_PROPERTY(pointClippingBehavior);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_PROPERTIES: {
      VkPhysicalDeviceProtectedMemoryProperties *properties = (void *)ext;
      CORE_PROPERTY(protectedNoFault);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
      VkPhysicalDeviceSubgroupProperties *properties = (void *)ext;
      CORE_PROPERTY(subgroupSize);
      CORE_RENAMED_PROPERTY(supportedStages,
                                    subgroupSupportedStages);
      CORE_RENAMED_PROPERTY(supportedOperations,
                                    subgroupSupportedOperations);
      CORE_RENAMED_PROPERTY(quadOperationsInAllStages,
                                    subgroupQuadOperationsInAllStages);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES:
      vk_copy_struct_guts(ext, (void *)core, sizeof(*core));
      return true;

   default:
      return false;
   }
}

bool
vk_get_physical_device_core_1_2_property_ext(struct VkBaseOutStructure *ext,
                                             const VkPhysicalDeviceVulkan12Properties *core)
{
   switch (ext->sType) {
   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES: {
      VkPhysicalDeviceDepthStencilResolveProperties *properties = (void *)ext;
      CORE_PROPERTY(supportedDepthResolveModes);
      CORE_PROPERTY(supportedStencilResolveModes);
      CORE_PROPERTY(independentResolveNone);
      CORE_PROPERTY(independentResolve);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES: {
      VkPhysicalDeviceDescriptorIndexingProperties *properties = (void *)ext;
      CORE_PROPERTY(maxUpdateAfterBindDescriptorsInAllPools);
      CORE_PROPERTY(shaderUniformBufferArrayNonUniformIndexingNative);
      CORE_PROPERTY(shaderSampledImageArrayNonUniformIndexingNative);
      CORE_PROPERTY(shaderStorageBufferArrayNonUniformIndexingNative);
      CORE_PROPERTY(shaderStorageImageArrayNonUniformIndexingNative);
      CORE_PROPERTY(shaderInputAttachmentArrayNonUniformIndexingNative);
      CORE_PROPERTY(robustBufferAccessUpdateAfterBind);
      CORE_PROPERTY(quadDivergentImplicitLod);
      CORE_PROPERTY(maxPerStageDescriptorUpdateAfterBindSamplers);
      CORE_PROPERTY(maxPerStageDescriptorUpdateAfterBindUniformBuffers);
      CORE_PROPERTY(maxPerStageDescriptorUpdateAfterBindStorageBuffers);
      CORE_PROPERTY(maxPerStageDescriptorUpdateAfterBindSampledImages);
      CORE_PROPERTY(maxPerStageDescriptorUpdateAfterBindStorageImages);
      CORE_PROPERTY(maxPerStageDescriptorUpdateAfterBindInputAttachments);
      CORE_PROPERTY(maxPerStageUpdateAfterBindResources);
      CORE_PROPERTY(maxDescriptorSetUpdateAfterBindSamplers);
      CORE_PROPERTY(maxDescriptorSetUpdateAfterBindUniformBuffers);
      CORE_PROPERTY(maxDescriptorSetUpdateAfterBindUniformBuffersDynamic);
      CORE_PROPERTY(maxDescriptorSetUpdateAfterBindStorageBuffers);
      CORE_PROPERTY(maxDescriptorSetUpdateAfterBindStorageBuffersDynamic);
      CORE_PROPERTY(maxDescriptorSetUpdateAfterBindSampledImages);
      CORE_PROPERTY(maxDescriptorSetUpdateAfterBindStorageImages);
      CORE_PROPERTY(maxDescriptorSetUpdateAfterBindInputAttachments);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES: {
      VkPhysicalDeviceDriverProperties *properties = (void *) ext;
      CORE_PROPERTY(driverID);
      CORE_PROPERTY(driverName);
      CORE_PROPERTY(driverInfo);
      CORE_PROPERTY(conformanceVersion);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES: {
      VkPhysicalDeviceSamplerFilterMinmaxProperties *properties = (void *)ext;
      CORE_PROPERTY(filterMinmaxImageComponentMapping);
      CORE_PROPERTY(filterMinmaxSingleComponentFormats);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES : {
      VkPhysicalDeviceFloatControlsProperties *properties = (void *)ext;
      CORE_PROPERTY(denormBehaviorIndependence);
      CORE_PROPERTY(roundingModeIndependence);
      CORE_PROPERTY(shaderDenormFlushToZeroFloat16);
      CORE_PROPERTY(shaderDenormPreserveFloat16);
      CORE_PROPERTY(shaderRoundingModeRTEFloat16);
      CORE_PROPERTY(shaderRoundingModeRTZFloat16);
      CORE_PROPERTY(shaderSignedZeroInfNanPreserveFloat16);
      CORE_PROPERTY(shaderDenormFlushToZeroFloat32);
      CORE_PROPERTY(shaderDenormPreserveFloat32);
      CORE_PROPERTY(shaderRoundingModeRTEFloat32);
      CORE_PROPERTY(shaderRoundingModeRTZFloat32);
      CORE_PROPERTY(shaderSignedZeroInfNanPreserveFloat32);
      CORE_PROPERTY(shaderDenormFlushToZeroFloat64);
      CORE_PROPERTY(shaderDenormPreserveFloat64);
      CORE_PROPERTY(shaderRoundingModeRTEFloat64);
      CORE_PROPERTY(shaderRoundingModeRTZFloat64);
      CORE_PROPERTY(shaderSignedZeroInfNanPreserveFloat64);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES: {
      VkPhysicalDeviceTimelineSemaphoreProperties *properties = (void *) ext;
      CORE_PROPERTY(maxTimelineSemaphoreValueDifference);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES:
      vk_copy_struct_guts(ext, (void *)core, sizeof(*core));
      return true;

   default:
      return false;
   }
}

bool
vk_get_physical_device_core_1_3_property_ext(struct VkBaseOutStructure *ext,
                                             const VkPhysicalDeviceVulkan13Properties *core)
{
   switch (ext->sType) {
   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES: {
      VkPhysicalDeviceInlineUniformBlockProperties *properties = (void *)ext;
      CORE_PROPERTY(maxInlineUniformBlockSize);
      CORE_PROPERTY(maxPerStageDescriptorInlineUniformBlocks);
      CORE_PROPERTY(maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks);
      CORE_PROPERTY(maxDescriptorSetInlineUniformBlocks);
      CORE_PROPERTY(maxDescriptorSetUpdateAfterBindInlineUniformBlocks);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES: {
      VkPhysicalDeviceMaintenance4Properties *properties = (void *)ext;
      CORE_PROPERTY(maxBufferSize);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_INTEGER_DOT_PRODUCT_PROPERTIES: {
      VkPhysicalDeviceShaderIntegerDotProductProperties *properties = (void *)ext;

#define IDP_PROPERTY(x) CORE_PROPERTY(integerDotProduct##x)
      IDP_PROPERTY(8BitUnsignedAccelerated);
      IDP_PROPERTY(8BitSignedAccelerated);
      IDP_PROPERTY(8BitMixedSignednessAccelerated);
      IDP_PROPERTY(4x8BitPackedUnsignedAccelerated);
      IDP_PROPERTY(4x8BitPackedSignedAccelerated);
      IDP_PROPERTY(4x8BitPackedMixedSignednessAccelerated);
      IDP_PROPERTY(16BitUnsignedAccelerated);
      IDP_PROPERTY(16BitSignedAccelerated);
      IDP_PROPERTY(16BitMixedSignednessAccelerated);
      IDP_PROPERTY(32BitUnsignedAccelerated);
      IDP_PROPERTY(32BitSignedAccelerated);
      IDP_PROPERTY(32BitMixedSignednessAccelerated);
      IDP_PROPERTY(64BitUnsignedAccelerated);
      IDP_PROPERTY(64BitSignedAccelerated);
      IDP_PROPERTY(64BitMixedSignednessAccelerated);
      IDP_PROPERTY(AccumulatingSaturating8BitUnsignedAccelerated);
      IDP_PROPERTY(AccumulatingSaturating8BitSignedAccelerated);
      IDP_PROPERTY(AccumulatingSaturating8BitMixedSignednessAccelerated);
      IDP_PROPERTY(AccumulatingSaturating4x8BitPackedUnsignedAccelerated);
      IDP_PROPERTY(AccumulatingSaturating4x8BitPackedSignedAccelerated);
      IDP_PROPERTY(AccumulatingSaturating4x8BitPackedMixedSignednessAccelerated);
      IDP_PROPERTY(AccumulatingSaturating16BitUnsignedAccelerated);
      IDP_PROPERTY(AccumulatingSaturating16BitSignedAccelerated);
      IDP_PROPERTY(AccumulatingSaturating16BitMixedSignednessAccelerated);
      IDP_PROPERTY(AccumulatingSaturating32BitUnsignedAccelerated);
      IDP_PROPERTY(AccumulatingSaturating32BitSignedAccelerated);
      IDP_PROPERTY(AccumulatingSaturating32BitMixedSignednessAccelerated);
      IDP_PROPERTY(AccumulatingSaturating64BitUnsignedAccelerated);
      IDP_PROPERTY(AccumulatingSaturating64BitSignedAccelerated);
      IDP_PROPERTY(AccumulatingSaturating64BitMixedSignednessAccelerated);
#undef IDP_PROPERTY
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES: {
      VkPhysicalDeviceSubgroupSizeControlProperties *properties = (void *)ext;
      CORE_PROPERTY(minSubgroupSize);
      CORE_PROPERTY(maxSubgroupSize);
      CORE_PROPERTY(maxComputeWorkgroupSubgroups);
      CORE_PROPERTY(requiredSubgroupSizeStages);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXEL_BUFFER_ALIGNMENT_PROPERTIES: {
      VkPhysicalDeviceTexelBufferAlignmentProperties *properties = (void *)ext;
      CORE_PROPERTY(storageTexelBufferOffsetAlignmentBytes);
      CORE_PROPERTY(storageTexelBufferOffsetSingleTexelAlignment);
      CORE_PROPERTY(uniformTexelBufferOffsetAlignmentBytes);
      CORE_PROPERTY(uniformTexelBufferOffsetSingleTexelAlignment);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES:
      vk_copy_struct_guts(ext, (void *)core, sizeof(*core));
      return true;

   default:
      return false;
   }
}

#undef CORE_RENAMED_PROPERTY
#undef CORE_PROPERTY

