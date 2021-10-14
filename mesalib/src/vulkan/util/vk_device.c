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
#include "vk_util.h"
#include "util/hash_table.h"
#include "util/ralloc.h"

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

   p_atomic_set(&device->private_data_next_index, 0);

   list_inithead(&device->queues);

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

static void
copy_vk_struct_guts(VkBaseOutStructure *dst, VkBaseInStructure *src, size_t struct_size)
{
   STATIC_ASSERT(sizeof(*dst) == sizeof(*src));
   memcpy(dst + 1, src + 1, struct_size - sizeof(VkBaseOutStructure));
}

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
      copy_vk_struct_guts(ext, (void *)core, sizeof(*core));
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
   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR: {
      VkPhysicalDevice8BitStorageFeaturesKHR *features = (void *)ext;
      CORE_FEATURE(storageBuffer8BitAccess);
      CORE_FEATURE(uniformAndStorageBuffer8BitAccess);
      CORE_FEATURE(storagePushConstant8);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR: {
      VkPhysicalDeviceBufferDeviceAddressFeaturesKHR *features = (void *)ext;
      CORE_FEATURE(bufferDeviceAddress);
      CORE_FEATURE(bufferDeviceAddressCaptureReplay);
      CORE_FEATURE(bufferDeviceAddressMultiDevice);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT: {
      VkPhysicalDeviceDescriptorIndexingFeaturesEXT *features = (void *)ext;
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

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT16_INT8_FEATURES_KHR: {
      VkPhysicalDeviceFloat16Int8FeaturesKHR *features = (void *)ext;
      CORE_FEATURE(shaderFloat16);
      CORE_FEATURE(shaderInt8);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT: {
      VkPhysicalDeviceHostQueryResetFeaturesEXT *features = (void *)ext;
      CORE_FEATURE(hostQueryReset);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES_KHR: {
      VkPhysicalDeviceImagelessFramebufferFeaturesKHR *features = (void *)ext;
      CORE_FEATURE(imagelessFramebuffer);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES_EXT: {
      VkPhysicalDeviceScalarBlockLayoutFeaturesEXT *features =(void *)ext;
      CORE_FEATURE(scalarBlockLayout);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SEPARATE_DEPTH_STENCIL_LAYOUTS_FEATURES_KHR: {
      VkPhysicalDeviceSeparateDepthStencilLayoutsFeaturesKHR *features = (void *)ext;
      CORE_FEATURE(separateDepthStencilLayouts);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES_KHR: {
      VkPhysicalDeviceShaderAtomicInt64FeaturesKHR *features = (void *)ext;
      CORE_FEATURE(shaderBufferInt64Atomics);
      CORE_FEATURE(shaderSharedInt64Atomics);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_SUBGROUP_EXTENDED_TYPES_FEATURES_KHR: {
      VkPhysicalDeviceShaderSubgroupExtendedTypesFeaturesKHR *features = (void *)ext;
      CORE_FEATURE(shaderSubgroupExtendedTypes);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR: {
      VkPhysicalDeviceTimelineSemaphoreFeaturesKHR *features = (void *) ext;
      CORE_FEATURE(timelineSemaphore);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES_KHR: {
      VkPhysicalDeviceUniformBufferStandardLayoutFeaturesKHR *features = (void *)ext;
      CORE_FEATURE(uniformBufferStandardLayout);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES_KHR: {
      VkPhysicalDeviceVulkanMemoryModelFeaturesKHR *features = (void *)ext;
      CORE_FEATURE(vulkanMemoryModel);
      CORE_FEATURE(vulkanMemoryModelDeviceScope);
      CORE_FEATURE(vulkanMemoryModelAvailabilityVisibilityChains);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
      copy_vk_struct_guts(ext, (void *)core, sizeof(*core));
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
      copy_vk_struct_guts(ext, (void *)core, sizeof(*core));
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
   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES_KHR: {
      VkPhysicalDeviceDepthStencilResolvePropertiesKHR *properties = (void *)ext;
      CORE_PROPERTY(supportedDepthResolveModes);
      CORE_PROPERTY(supportedStencilResolveModes);
      CORE_PROPERTY(independentResolveNone);
      CORE_PROPERTY(independentResolve);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES_EXT: {
      VkPhysicalDeviceDescriptorIndexingPropertiesEXT *properties = (void *)ext;
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

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR: {
      VkPhysicalDeviceDriverPropertiesKHR *properties = (void *) ext;
      CORE_PROPERTY(driverID);
      CORE_PROPERTY(driverName);
      CORE_PROPERTY(driverInfo);
      CORE_PROPERTY(conformanceVersion);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES_EXT: {
      VkPhysicalDeviceSamplerFilterMinmaxPropertiesEXT *properties = (void *)ext;
      CORE_PROPERTY(filterMinmaxImageComponentMapping);
      CORE_PROPERTY(filterMinmaxSingleComponentFormats);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR : {
      VkPhysicalDeviceFloatControlsPropertiesKHR *properties = (void *)ext;
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

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES_KHR: {
      VkPhysicalDeviceTimelineSemaphorePropertiesKHR *properties = (void *) ext;
      CORE_PROPERTY(maxTimelineSemaphoreValueDifference);
      return true;
   }

   case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES:
      copy_vk_struct_guts(ext, (void *)core, sizeof(*core));
      return true;

   default:
      return false;
   }
}

#undef CORE_RENAMED_PROPERTY
#undef CORE_PROPERTY

