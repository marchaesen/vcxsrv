/*
 * Copyright © 2019 Red Hat.
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

#include "lvp_private.h"

#include "pipe-loader/pipe_loader.h"
#include "git_sha1.h"
#include "vk_util.h"
#include "pipe/p_config.h"
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "frontend/drisw_api.h"

#include "util/u_inlines.h"
#include "util/os_memory.h"
#include "util/u_thread.h"
#include "util/u_atomic.h"
#include "util/timespec.h"
#include "os_time.h"

#if defined(VK_USE_PLATFORM_WAYLAND_KHR) || \
    defined(VK_USE_PLATFORM_WIN32_KHR) || \
    defined(VK_USE_PLATFORM_XCB_KHR) || \
    defined(VK_USE_PLATFORM_XLIB_KHR)
#define LVP_USE_WSI_PLATFORM
#endif
#define LVP_API_VERSION VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION)

VKAPI_ATTR VkResult VKAPI_CALL lvp_EnumerateInstanceVersion(uint32_t* pApiVersion)
{
   *pApiVersion = LVP_API_VERSION;
   return VK_SUCCESS;
}

static const struct vk_instance_extension_table lvp_instance_extensions_supported = {
   .KHR_device_group_creation                = true,
   .KHR_external_fence_capabilities          = true,
   .KHR_external_memory_capabilities         = true,
   .KHR_external_semaphore_capabilities      = true,
   .KHR_get_physical_device_properties2      = true,
   .EXT_debug_report                         = true,
#ifdef LVP_USE_WSI_PLATFORM
   .KHR_get_surface_capabilities2            = true,
   .KHR_surface                              = true,
   .KHR_surface_protected_capabilities       = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
   .KHR_wayland_surface                      = true,
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
   .KHR_win32_surface                        = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
   .KHR_xcb_surface                          = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
   .KHR_xlib_surface                         = true,
#endif
};

static const struct vk_device_extension_table lvp_device_extensions_supported = {
   .KHR_8bit_storage                      = true,
   .KHR_16bit_storage                     = true,
   .KHR_bind_memory2                      = true,
   .KHR_buffer_device_address             = true,
   .KHR_create_renderpass2                = true,
   .KHR_copy_commands2                    = true,
   .KHR_dedicated_allocation              = true,
   .KHR_depth_stencil_resolve             = true,
   .KHR_descriptor_update_template        = true,
   .KHR_device_group                      = true,
   .KHR_draw_indirect_count               = true,
   .KHR_driver_properties                 = true,
   .KHR_dynamic_rendering                 = true,
   .KHR_external_fence                    = true,
   .KHR_external_memory                   = true,
#ifdef PIPE_MEMORY_FD
   .KHR_external_memory_fd                = true,
#endif
   .KHR_external_semaphore                = true,
   .KHR_shader_float_controls             = true,
   .KHR_get_memory_requirements2          = true,
#ifdef LVP_USE_WSI_PLATFORM
   .KHR_incremental_present               = true,
#endif
   .KHR_image_format_list                 = true,
   .KHR_imageless_framebuffer             = true,
   .KHR_maintenance1                      = true,
   .KHR_maintenance2                      = true,
   .KHR_maintenance3                      = true,
   .KHR_multiview                         = true,
   .KHR_push_descriptor                   = true,
   .KHR_relaxed_block_layout              = true,
   .KHR_sampler_mirror_clamp_to_edge      = true,
   .KHR_separate_depth_stencil_layouts    = true,
   .KHR_shader_atomic_int64               = true,
   .KHR_shader_draw_parameters            = true,
   .KHR_shader_float16_int8               = true,
   .KHR_shader_subgroup_extended_types    = true,
   .KHR_spirv_1_4                         = true,
   .KHR_storage_buffer_storage_class      = true,
#ifdef LVP_USE_WSI_PLATFORM
   .KHR_swapchain                         = true,
#endif
   .KHR_timeline_semaphore                = true,
   .KHR_uniform_buffer_standard_layout    = true,
   .KHR_variable_pointers                 = true,
   .EXT_4444_formats                      = true,
   .EXT_calibrated_timestamps             = true,
   .EXT_color_write_enable                = true,
   .EXT_conditional_rendering             = true,
   .EXT_depth_clip_enable                 = true,
   .EXT_extended_dynamic_state            = true,
   .EXT_extended_dynamic_state2           = true,
   .EXT_external_memory_host              = true,
   .EXT_host_query_reset                  = true,
   .EXT_index_type_uint8                  = true,
   .EXT_multi_draw                        = true,
   .EXT_post_depth_coverage               = true,
   .EXT_private_data                      = true,
   .EXT_primitive_topology_list_restart   = true,
   .EXT_sampler_filter_minmax             = true,
   .EXT_scalar_block_layout               = true,
   .EXT_separate_stencil_usage            = true,
   .EXT_shader_stencil_export             = true,
   .EXT_shader_viewport_index_layer       = true,
   .EXT_transform_feedback                = true,
   .EXT_vertex_attribute_divisor          = true,
   .EXT_vertex_input_dynamic_state        = true,
   .EXT_custom_border_color               = true,
   .EXT_provoking_vertex                  = true,
   .EXT_line_rasterization                = true,
   .GOOGLE_decorate_string                = true,
   .GOOGLE_hlsl_functionality1            = true,
};

static VkResult VKAPI_CALL
lvp_physical_device_init(struct lvp_physical_device *device,
                         struct lvp_instance *instance,
                         struct pipe_loader_device *pld)
{
   VkResult result;

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &lvp_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_physical_device_entrypoints, false);
   result = vk_physical_device_init(&device->vk, &instance->vk,
                                    NULL, &dispatch_table);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto fail;
   }
   device->pld = pld;

   device->pscreen = pipe_loader_create_screen_vk(device->pld, true);
   if (!device->pscreen)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   device->max_images = device->pscreen->get_shader_param(device->pscreen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_MAX_SHADER_IMAGES);
   device->vk.supported_extensions = lvp_device_extensions_supported;
   result = lvp_init_wsi(device);
   if (result != VK_SUCCESS) {
      vk_physical_device_finish(&device->vk);
      vk_error(instance, result);
      goto fail;
   }

   return VK_SUCCESS;
 fail:
   return result;
}

static void VKAPI_CALL
lvp_physical_device_finish(struct lvp_physical_device *device)
{
   lvp_finish_wsi(device);
   device->pscreen->destroy(device->pscreen);
   vk_physical_device_finish(&device->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateInstance(
   const VkInstanceCreateInfo*                 pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkInstance*                                 pInstance)
{
   struct lvp_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   if (pAllocator == NULL)
      pAllocator = vk_default_allocator();

   instance = vk_zalloc(pAllocator, sizeof(*instance), 8,
                        VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &lvp_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_instance_entrypoints, false);

   result = vk_instance_init(&instance->vk,
                             &lvp_instance_extensions_supported,
                             &dispatch_table,
                             pCreateInfo,
                             pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return vk_error(instance, result);
   }

   instance->apiVersion = LVP_API_VERSION;
   instance->physicalDeviceCount = -1;

   //   _mesa_locale_init();
   //   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   *pInstance = lvp_instance_to_handle(instance);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyInstance(
   VkInstance                                  _instance,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);

   if (!instance)
      return;
   if (instance->physicalDeviceCount > 0)
      lvp_physical_device_finish(&instance->physicalDevice);
   //   _mesa_locale_fini();

   pipe_loader_release(&instance->devs, instance->num_devices);

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

#if defined(HAVE_PIPE_LOADER_DRI)
static void lvp_get_image(struct dri_drawable *dri_drawable,
                          int x, int y, unsigned width, unsigned height, unsigned stride,
                          void *data)
{

}

static void lvp_put_image(struct dri_drawable *dri_drawable,
                          void *data, unsigned width, unsigned height)
{
   fprintf(stderr, "put image %dx%d\n", width, height);
}

static void lvp_put_image2(struct dri_drawable *dri_drawable,
                           void *data, int x, int y, unsigned width, unsigned height,
                           unsigned stride)
{
   fprintf(stderr, "put image 2 %d,%d %dx%d\n", x, y, width, height);
}

static struct drisw_loader_funcs lvp_sw_lf = {
   .get_image = lvp_get_image,
   .put_image = lvp_put_image,
   .put_image2 = lvp_put_image2,
};
#endif

static VkResult
lvp_enumerate_physical_devices(struct lvp_instance *instance)
{
   VkResult result;

   if (instance->physicalDeviceCount != -1)
      return VK_SUCCESS;

   /* sw only for now */
   instance->num_devices = pipe_loader_sw_probe(NULL, 0);

   assert(instance->num_devices == 1);

#if defined(HAVE_PIPE_LOADER_DRI)
   pipe_loader_sw_probe_dri(&instance->devs, &lvp_sw_lf);
#else
   pipe_loader_sw_probe_null(&instance->devs);
#endif

   result = lvp_physical_device_init(&instance->physicalDevice,
                                     instance, &instance->devs[0]);
   if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
      instance->physicalDeviceCount = 0;
   } else if (result == VK_SUCCESS) {
      instance->physicalDeviceCount = 1;
   }

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_EnumeratePhysicalDevices(
   VkInstance                                  _instance,
   uint32_t*                                   pPhysicalDeviceCount,
   VkPhysicalDevice*                           pPhysicalDevices)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);
   VkResult result;

   result = lvp_enumerate_physical_devices(instance);
   if (result != VK_SUCCESS)
      return result;

   if (!pPhysicalDevices) {
      *pPhysicalDeviceCount = instance->physicalDeviceCount;
   } else if (*pPhysicalDeviceCount >= 1) {
      pPhysicalDevices[0] = lvp_physical_device_to_handle(&instance->physicalDevice);
      *pPhysicalDeviceCount = 1;
   } else {
      *pPhysicalDeviceCount = 0;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_EnumeratePhysicalDeviceGroups(
   VkInstance                                 _instance,
   uint32_t*                                   pPhysicalDeviceGroupCount,
   VkPhysicalDeviceGroupProperties*            pPhysicalDeviceGroupProperties)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDeviceGroupProperties, out,
                          pPhysicalDeviceGroupProperties,
                          pPhysicalDeviceGroupCount);

   VkResult result = lvp_enumerate_physical_devices(instance);
   if (result != VK_SUCCESS)
      return result;

   vk_outarray_append_typed(VkPhysicalDeviceGroupProperties, &out, p) {
      p->physicalDeviceCount = 1;
      memset(p->physicalDevices, 0, sizeof(p->physicalDevices));
      p->physicalDevices[0] = lvp_physical_device_to_handle(&instance->physicalDevice);
      p->subsetAllocation = false;
   }

   return vk_outarray_status(&out);
}

static int
min_vertex_pipeline_param(struct pipe_screen *pscreen, enum pipe_shader_cap param)
{
   int val = INT_MAX;
   for (int i = 0; i < PIPE_SHADER_COMPUTE; ++i) {
      if (i == PIPE_SHADER_FRAGMENT ||
          !pscreen->get_shader_param(pscreen, i,
                                     PIPE_SHADER_CAP_MAX_INSTRUCTIONS))
         continue;

      val = MAX2(val, pscreen->get_shader_param(pscreen, i, param));
   }
   return val;
}

static int
min_shader_param(struct pipe_screen *pscreen, enum pipe_shader_cap param)
{
   return MIN3(min_vertex_pipeline_param(pscreen, param),
               pscreen->get_shader_param(pscreen, PIPE_SHADER_FRAGMENT, param),
               pscreen->get_shader_param(pscreen, PIPE_SHADER_COMPUTE, param));
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPhysicalDeviceFeatures(
   VkPhysicalDevice                            physicalDevice,
   VkPhysicalDeviceFeatures*                   pFeatures)
{
   LVP_FROM_HANDLE(lvp_physical_device, pdevice, physicalDevice);
   bool indirect = false;//pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_GLSL_FEATURE_LEVEL) >= 400;
   memset(pFeatures, 0, sizeof(*pFeatures));
   *pFeatures = (VkPhysicalDeviceFeatures) {
      .robustBufferAccess                       = true,
      .fullDrawIndexUint32                      = true,
      .imageCubeArray                           = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_CUBE_MAP_ARRAY) != 0),
      .independentBlend                         = true,
      .geometryShader                           = (pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_GEOMETRY, PIPE_SHADER_CAP_MAX_INSTRUCTIONS) != 0),
      .tessellationShader                       = (pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_TESS_EVAL, PIPE_SHADER_CAP_MAX_INSTRUCTIONS) != 0),
      .sampleRateShading                        = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_SAMPLE_SHADING) != 0),
      .dualSrcBlend                             = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_DUAL_SOURCE_RENDER_TARGETS) != 0),
      .logicOp                                  = true,
      .multiDrawIndirect                        = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MULTI_DRAW_INDIRECT) != 0),
      .drawIndirectFirstInstance                = true,
      .depthClamp                               = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_DEPTH_CLIP_DISABLE) != 0),
      .depthBiasClamp                           = true,
      .fillModeNonSolid                         = true,
      .depthBounds                              = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_DEPTH_BOUNDS_TEST) != 0),
      .wideLines                                = true,
      .largePoints                              = true,
      .alphaToOne                               = true,
      .multiViewport                            = true,
      .samplerAnisotropy                        = true,
      .textureCompressionETC2                   = false,
      .textureCompressionASTC_LDR               = false,
      .textureCompressionBC                     = true,
      .occlusionQueryPrecise                    = true,
      .pipelineStatisticsQuery                  = true,
      .vertexPipelineStoresAndAtomics           = (min_vertex_pipeline_param(pdevice->pscreen, PIPE_SHADER_CAP_MAX_SHADER_BUFFERS) != 0),
      .fragmentStoresAndAtomics                 = (pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_MAX_SHADER_BUFFERS) != 0),
      .shaderTessellationAndGeometryPointSize   = true,
      .shaderImageGatherExtended                = true,
      .shaderStorageImageExtendedFormats        = (min_shader_param(pdevice->pscreen, PIPE_SHADER_CAP_MAX_SHADER_IMAGES) != 0),
      .shaderStorageImageMultisample            = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_TEXTURE_MULTISAMPLE) != 0),
      .shaderUniformBufferArrayDynamicIndexing  = true,
      .shaderSampledImageArrayDynamicIndexing   = indirect,
      .shaderStorageBufferArrayDynamicIndexing  = true,
      .shaderStorageImageArrayDynamicIndexing   = indirect,
      .shaderStorageImageReadWithoutFormat      = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_IMAGE_LOAD_FORMATTED) != 0),
      .shaderStorageImageWriteWithoutFormat     = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_IMAGE_STORE_FORMATTED) != 0),
      .shaderClipDistance                       = true,
      .shaderCullDistance                       = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_CULL_DISTANCE) == 1),
      .shaderFloat64                            = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_DOUBLES) == 1),
      .shaderInt64                              = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_INT64) == 1),
      .shaderInt16                              = (min_shader_param(pdevice->pscreen, PIPE_SHADER_CAP_INT16) == 1),
      .variableMultisampleRate                  = false,
      .inheritedQueries                         = false,
   };
}

static void
lvp_get_physical_device_features_1_1(struct lvp_physical_device *pdevice,
                                     VkPhysicalDeviceVulkan11Features *f)
{
   assert(f->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);

   f->storageBuffer16BitAccess            = true;
   f->uniformAndStorageBuffer16BitAccess  = true;
   f->storagePushConstant16               = true;
   f->storageInputOutput16                = false;
   f->multiview                           = true;
   f->multiviewGeometryShader             = true;
   f->multiviewTessellationShader         = true;
   f->variablePointersStorageBuffer       = true;
   f->variablePointers                    = false;
   f->protectedMemory                     = false;
   f->samplerYcbcrConversion              = false;
   f->shaderDrawParameters                = true;
}

static void
lvp_get_physical_device_features_1_2(struct lvp_physical_device *pdevice,
                                     VkPhysicalDeviceVulkan12Features *f)
{
   assert(f->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);

   f->samplerMirrorClampToEdge = true;
   f->drawIndirectCount = true;
   f->storageBuffer8BitAccess = true;
   f->uniformAndStorageBuffer8BitAccess = true;
   f->storagePushConstant8 = true;
   f->shaderBufferInt64Atomics = true;
   f->shaderSharedInt64Atomics = true;
   f->shaderFloat16 = pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_FP16) != 0;
   f->shaderInt8 = true;

   f->descriptorIndexing = false;
   f->shaderInputAttachmentArrayDynamicIndexing = false;
   f->shaderUniformTexelBufferArrayDynamicIndexing = false;
   f->shaderStorageTexelBufferArrayDynamicIndexing = false;
   f->shaderUniformBufferArrayNonUniformIndexing = false;
   f->shaderSampledImageArrayNonUniformIndexing = false;
   f->shaderStorageBufferArrayNonUniformIndexing = false;
   f->shaderStorageImageArrayNonUniformIndexing = false;
   f->shaderInputAttachmentArrayNonUniformIndexing = false;
   f->shaderUniformTexelBufferArrayNonUniformIndexing = false;
   f->shaderStorageTexelBufferArrayNonUniformIndexing = false;
   f->descriptorBindingUniformBufferUpdateAfterBind = false;
   f->descriptorBindingSampledImageUpdateAfterBind = false;
   f->descriptorBindingStorageImageUpdateAfterBind = false;
   f->descriptorBindingStorageBufferUpdateAfterBind = false;
   f->descriptorBindingUniformTexelBufferUpdateAfterBind = false;
   f->descriptorBindingStorageTexelBufferUpdateAfterBind = false;
   f->descriptorBindingUpdateUnusedWhilePending = false;
   f->descriptorBindingPartiallyBound = false;
   f->descriptorBindingVariableDescriptorCount = false;
   f->runtimeDescriptorArray = false;

   f->samplerFilterMinmax = true;
   f->scalarBlockLayout = true;
   f->imagelessFramebuffer = true;
   f->uniformBufferStandardLayout = true;
   f->shaderSubgroupExtendedTypes = true;
   f->separateDepthStencilLayouts = true;
   f->hostQueryReset = true;
   f->timelineSemaphore = true;
   f->bufferDeviceAddress = true;
   f->bufferDeviceAddressCaptureReplay = false;
   f->bufferDeviceAddressMultiDevice = false;
   f->vulkanMemoryModel = false;
   f->vulkanMemoryModelDeviceScope = false;
   f->vulkanMemoryModelAvailabilityVisibilityChains = false;
   f->shaderOutputViewportIndex = true;
   f->shaderOutputLayer = true;
   f->subgroupBroadcastDynamicId = true;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPhysicalDeviceFeatures2(
   VkPhysicalDevice                            physicalDevice,
   VkPhysicalDeviceFeatures2                  *pFeatures)
{
   LVP_FROM_HANDLE(lvp_physical_device, pdevice, physicalDevice);
   lvp_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);

   VkPhysicalDeviceVulkan11Features core_1_1 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
   };
   lvp_get_physical_device_features_1_1(pdevice, &core_1_1);

   VkPhysicalDeviceVulkan12Features core_1_2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
   };
   lvp_get_physical_device_features_1_2(pdevice, &core_1_2);

   vk_foreach_struct(ext, pFeatures->pNext) {

      if (vk_get_physical_device_core_1_1_feature_ext(ext, &core_1_1))
         continue;
      if (vk_get_physical_device_core_1_2_feature_ext(ext, &core_1_2))
         continue;

      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES_EXT: {
         VkPhysicalDevicePrivateDataFeaturesEXT *features =
            (VkPhysicalDevicePrivateDataFeaturesEXT *)ext;
         features->privateData = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT: {
         VkPhysicalDeviceLineRasterizationFeaturesEXT *features =
            (VkPhysicalDeviceLineRasterizationFeaturesEXT *)ext;
         features->rectangularLines = true;
         features->bresenhamLines = true;
         features->smoothLines = true;
         features->stippledRectangularLines = true;
         features->stippledBresenhamLines = true;
         features->stippledSmoothLines = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *features =
            (VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT *)ext;
         features->vertexAttributeInstanceRateZeroDivisor = false;
         if (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR) != 0) {
            features->vertexAttributeInstanceRateDivisor = true;
         } else {
            features->vertexAttributeInstanceRateDivisor = false;
         }
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT: {
         VkPhysicalDeviceIndexTypeUint8FeaturesEXT *features =
            (VkPhysicalDeviceIndexTypeUint8FeaturesEXT *)ext;
         features->indexTypeUint8 = true;
         break;
      }

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT: {
         VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *features =
            (VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT *)ext;
         features->vertexInputDynamicState = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: {
         VkPhysicalDeviceTransformFeedbackFeaturesEXT *features =
            (VkPhysicalDeviceTransformFeedbackFeaturesEXT*)ext;

         features->transformFeedback = true;
         features->geometryStreams = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT: {
         VkPhysicalDeviceConditionalRenderingFeaturesEXT *features =
            (VkPhysicalDeviceConditionalRenderingFeaturesEXT*)ext;
         features->conditionalRendering = true;
         features->inheritedConditionalRendering = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT: {
         VkPhysicalDeviceExtendedDynamicStateFeaturesEXT *features =
            (VkPhysicalDeviceExtendedDynamicStateFeaturesEXT*)ext;
         features->extendedDynamicState = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_4444_FORMATS_FEATURES_EXT: {
         VkPhysicalDevice4444FormatsFeaturesEXT *features =
            (VkPhysicalDevice4444FormatsFeaturesEXT*)ext;
         features->formatA4R4G4B4 = true;
         features->formatA4B4G4R4 = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT: {
         VkPhysicalDeviceCustomBorderColorFeaturesEXT *features =
            (VkPhysicalDeviceCustomBorderColorFeaturesEXT *)ext;
         features->customBorderColors = true;
         features->customBorderColorWithoutFormat = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT: {
         VkPhysicalDeviceColorWriteEnableFeaturesEXT *features =
            (VkPhysicalDeviceColorWriteEnableFeaturesEXT *)ext;
         features->colorWriteEnable = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT: {
         VkPhysicalDeviceProvokingVertexFeaturesEXT *features =
            (VkPhysicalDeviceProvokingVertexFeaturesEXT*)ext;
         features->provokingVertexLast = true;
         features->transformFeedbackPreservesProvokingVertex = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT: {
         VkPhysicalDeviceMultiDrawFeaturesEXT *features = (VkPhysicalDeviceMultiDrawFeaturesEXT *)ext;
         features->multiDraw = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT: {
         VkPhysicalDeviceDepthClipEnableFeaturesEXT *features =
            (VkPhysicalDeviceDepthClipEnableFeaturesEXT *)ext;
         if (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_DEPTH_CLAMP_ENABLE) != 0)
            features->depthClipEnable = true;
         else
            features->depthClipEnable = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT: {
         VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *features = (VkPhysicalDeviceExtendedDynamicState2FeaturesEXT *)ext;
         features->extendedDynamicState2 = true;
         features->extendedDynamicState2LogicOp = true;
         features->extendedDynamicState2PatchControlPoints = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIMITIVE_TOPOLOGY_LIST_RESTART_FEATURES_EXT: {
         VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *features = (VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT *)ext;
         features->primitiveTopologyListRestart = true;
         features->primitiveTopologyPatchListRestart = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR: {
         VkPhysicalDeviceDynamicRenderingFeaturesKHR *features = (VkPhysicalDeviceDynamicRenderingFeaturesKHR *)ext;
         features->dynamicRendering = VK_TRUE;
         break;
      }
      default:
         break;
      }
   }
}

void
lvp_device_get_cache_uuid(void *uuid)
{
   memset(uuid, 0, VK_UUID_SIZE);
   snprintf(uuid, VK_UUID_SIZE, "val-%s", &MESA_GIT_SHA1[4]);
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
                                     VkPhysicalDeviceProperties *pProperties)
{
   LVP_FROM_HANDLE(lvp_physical_device, pdevice, physicalDevice);

   VkSampleCountFlags sample_counts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;

   uint64_t grid_size[3], block_size[3];
   uint64_t max_threads_per_block, max_local_size;

   pdevice->pscreen->get_compute_param(pdevice->pscreen, PIPE_SHADER_IR_NIR,
                                       PIPE_COMPUTE_CAP_MAX_GRID_SIZE, grid_size);
   pdevice->pscreen->get_compute_param(pdevice->pscreen, PIPE_SHADER_IR_NIR,
                                       PIPE_COMPUTE_CAP_MAX_BLOCK_SIZE, block_size);
   pdevice->pscreen->get_compute_param(pdevice->pscreen, PIPE_SHADER_IR_NIR,
                                       PIPE_COMPUTE_CAP_MAX_THREADS_PER_BLOCK,
                                       &max_threads_per_block);
   pdevice->pscreen->get_compute_param(pdevice->pscreen, PIPE_SHADER_IR_NIR,
                                       PIPE_COMPUTE_CAP_MAX_LOCAL_SIZE,
                                       &max_local_size);

   const uint64_t max_render_targets = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_RENDER_TARGETS);

   VkPhysicalDeviceLimits limits = {
      .maxImageDimension1D                      = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_2D_SIZE),
      .maxImageDimension2D                      = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_2D_SIZE),
      .maxImageDimension3D                      = (1 << pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_3D_LEVELS)),
      .maxImageDimensionCube                    = (1 << pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS)),
      .maxImageArrayLayers                      = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS),
      .maxTexelBufferElements                   = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_BUFFER_SIZE),
      .maxUniformBufferRange                    = min_shader_param(pdevice->pscreen, PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE),
      .maxStorageBufferRange                    = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_SHADER_BUFFER_SIZE),
      .maxPushConstantsSize                     = MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount                 = UINT32_MAX,
      .maxSamplerAllocationCount                = 32 * 1024,
      .bufferImageGranularity                   = 64, /* A cache line */
      .sparseAddressSpaceSize                   = 0,
      .maxBoundDescriptorSets                   = MAX_SETS,
      .maxPerStageDescriptorSamplers            = min_shader_param(pdevice->pscreen, PIPE_SHADER_CAP_MAX_TEXTURE_SAMPLERS),
      .maxPerStageDescriptorUniformBuffers      = min_shader_param(pdevice->pscreen, PIPE_SHADER_CAP_MAX_CONST_BUFFERS) - 1,
      .maxPerStageDescriptorStorageBuffers      = min_shader_param(pdevice->pscreen, PIPE_SHADER_CAP_MAX_SHADER_BUFFERS),
      .maxPerStageDescriptorSampledImages       = min_shader_param(pdevice->pscreen, PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS),
      .maxPerStageDescriptorStorageImages       = min_shader_param(pdevice->pscreen, PIPE_SHADER_CAP_MAX_SHADER_IMAGES),
      .maxPerStageDescriptorInputAttachments    = 8,
      .maxPerStageResources                     = 128,
      .maxDescriptorSetSamplers                 = 32 * 1024,
      .maxDescriptorSetUniformBuffers           = 256,
      .maxDescriptorSetUniformBuffersDynamic    = 256,
      .maxDescriptorSetStorageBuffers           = 256,
      .maxDescriptorSetStorageBuffersDynamic    = 256,
      .maxDescriptorSetSampledImages            = 256,
      .maxDescriptorSetStorageImages            = 256,
      .maxDescriptorSetInputAttachments         = 256,
      .maxVertexInputAttributes                 = 32,
      .maxVertexInputBindings                   = 32,
      .maxVertexInputAttributeOffset            = 2047,
      .maxVertexInputBindingStride              = 2048,
      .maxVertexOutputComponents                = 128,
      .maxTessellationGenerationLevel           = 64,
      .maxTessellationPatchSize                 = 32,
      .maxTessellationControlPerVertexInputComponents = 128,
      .maxTessellationControlPerVertexOutputComponents = 128,
      .maxTessellationControlPerPatchOutputComponents = 128,
      .maxTessellationControlTotalOutputComponents = 4096,
      .maxTessellationEvaluationInputComponents = 128,
      .maxTessellationEvaluationOutputComponents = 128,
      .maxGeometryShaderInvocations             = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_GS_INVOCATIONS),
      .maxGeometryInputComponents               = 64,
      .maxGeometryOutputComponents              = 128,
      .maxGeometryOutputVertices                = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES),
      .maxGeometryTotalOutputComponents         = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS),
      .maxFragmentInputComponents               = 128,
      .maxFragmentOutputAttachments             = 8,
      .maxFragmentDualSrcAttachments            = 2,
      .maxFragmentCombinedOutputResources       = max_render_targets +
                                                  pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_FRAGMENT,
                                                     PIPE_SHADER_CAP_MAX_SHADER_BUFFERS) +
                                                  pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_FRAGMENT,
                                                     PIPE_SHADER_CAP_MAX_SHADER_IMAGES),
      .maxComputeSharedMemorySize               = max_local_size,
      .maxComputeWorkGroupCount                 = { grid_size[0], grid_size[1], grid_size[2] },
      .maxComputeWorkGroupInvocations           = max_threads_per_block,
      .maxComputeWorkGroupSize                  = { block_size[0], block_size[1], block_size[2] },
      .subPixelPrecisionBits                    = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_RASTERIZER_SUBPIXEL_BITS),
      .subTexelPrecisionBits                    = 8,
      .mipmapPrecisionBits                      = 4,
      .maxDrawIndexedIndexValue                 = UINT32_MAX,
      .maxDrawIndirectCount                     = UINT32_MAX,
      .maxSamplerLodBias                        = 16,
      .maxSamplerAnisotropy                     = 16,
      .maxViewports                             = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_VIEWPORTS),
      .maxViewportDimensions                    = { (1 << 14), (1 << 14) },
      .viewportBoundsRange                      = { -32768.0, 32768.0 },
      .viewportSubPixelBits                     = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_VIEWPORT_SUBPIXEL_BITS),
      .minMemoryMapAlignment                    = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT),
      .minTexelBufferOffsetAlignment            = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT),
      .minUniformBufferOffsetAlignment          = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_CONSTANT_BUFFER_OFFSET_ALIGNMENT),
      .minStorageBufferOffsetAlignment          = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_SHADER_BUFFER_OFFSET_ALIGNMENT),
      .minTexelOffset                           = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MIN_TEXEL_OFFSET),
      .maxTexelOffset                           = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXEL_OFFSET),
      .minTexelGatherOffset                     = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MIN_TEXTURE_GATHER_OFFSET),
      .maxTexelGatherOffset                     = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_GATHER_OFFSET),
      .minInterpolationOffset                   = -2, /* FIXME */
      .maxInterpolationOffset                   = 2, /* FIXME */
      .subPixelInterpolationOffsetBits          = 8, /* FIXME */
      .maxFramebufferWidth                      = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_2D_SIZE),
      .maxFramebufferHeight                     = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_2D_SIZE),
      .maxFramebufferLayers                     = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS),
      .framebufferColorSampleCounts             = sample_counts,
      .framebufferDepthSampleCounts             = sample_counts,
      .framebufferStencilSampleCounts           = sample_counts,
      .framebufferNoAttachmentsSampleCounts     = sample_counts,
      .maxColorAttachments                      = max_render_targets,
      .sampledImageColorSampleCounts            = sample_counts,
      .sampledImageIntegerSampleCounts          = sample_counts,
      .sampledImageDepthSampleCounts            = sample_counts,
      .sampledImageStencilSampleCounts          = sample_counts,
      .storageImageSampleCounts                 = sample_counts,
      .maxSampleMaskWords                       = 1,
      .timestampComputeAndGraphics              = true,
      .timestampPeriod                          = 1,
      .maxClipDistances                         = 8,
      .maxCullDistances                         = 8,
      .maxCombinedClipAndCullDistances          = 8,
      .discreteQueuePriorities                  = 2,
      .pointSizeRange                           = { 0.0, pdevice->pscreen->get_paramf(pdevice->pscreen, PIPE_CAPF_MAX_POINT_SIZE) },
      .lineWidthRange                           = { 1.0, pdevice->pscreen->get_paramf(pdevice->pscreen, PIPE_CAPF_MAX_LINE_WIDTH) },
      .pointSizeGranularity                     = (1.0 / 8.0),
      .lineWidthGranularity                     = 1.0 / 128.0,
      .strictLines                              = true,
      .standardSampleLocations                  = true,
      .optimalBufferCopyOffsetAlignment         = 128,
      .optimalBufferCopyRowPitchAlignment       = 128,
      .nonCoherentAtomSize                      = 64,
   };

   *pProperties = (VkPhysicalDeviceProperties) {
      .apiVersion = LVP_API_VERSION,
      .driverVersion = 1,
      .vendorID = VK_VENDOR_ID_MESA,
      .deviceID = 0,
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU,
      .limits = limits,
      .sparseProperties = {0},
   };

   strcpy(pProperties->deviceName, pdevice->pscreen->get_name(pdevice->pscreen));
   lvp_device_get_cache_uuid(pProperties->pipelineCacheUUID);

}

extern unsigned lp_native_vector_width;
static void
lvp_get_physical_device_properties_1_1(struct lvp_physical_device *pdevice,
                                       VkPhysicalDeviceVulkan11Properties *p)
{
   assert(p->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES);

   memset(p->deviceUUID, 0, VK_UUID_SIZE);
   memset(p->driverUUID, 0, VK_UUID_SIZE);
   memset(p->deviceLUID, 0, VK_LUID_SIZE);
   /* The LUID is for Windows. */
   p->deviceLUIDValid = false;
   p->deviceNodeMask = 0;

   p->subgroupSize = lp_native_vector_width / 32;
   p->subgroupSupportedStages = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
   p->subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_VOTE_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT;
   p->subgroupQuadOperationsInAllStages = false;

   p->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
   p->maxMultiviewViewCount = 6;
   p->maxMultiviewInstanceIndex = INT_MAX;
   p->protectedNoFault = false;
   p->maxPerSetDescriptors = 1024;
   p->maxMemoryAllocationSize = (1u << 31);
}

static void
lvp_get_physical_device_properties_1_2(struct lvp_physical_device *pdevice,
                                       VkPhysicalDeviceVulkan12Properties *p)
{
   p->driverID = VK_DRIVER_ID_MESA_LLVMPIPE;
   snprintf(p->driverName, VK_MAX_DRIVER_NAME_SIZE, "llvmpipe");
   snprintf(p->driverInfo, VK_MAX_DRIVER_INFO_SIZE, "Mesa " PACKAGE_VERSION MESA_GIT_SHA1
#ifdef MESA_LLVM_VERSION_STRING
                  " (LLVM " MESA_LLVM_VERSION_STRING ")"
#endif
            );

   p->conformanceVersion = (VkConformanceVersion){
      .major = 0,
      .minor = 0,
      .subminor = 0,
      .patch = 0,
   };

   p->denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL_KHR;
   p->roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL_KHR;
   p->shaderDenormFlushToZeroFloat16 = false;
   p->shaderDenormPreserveFloat16 = false;
   p->shaderRoundingModeRTEFloat16 = true;
   p->shaderRoundingModeRTZFloat16 = false;
   p->shaderSignedZeroInfNanPreserveFloat16 = true;

   p->shaderDenormFlushToZeroFloat32 = false;
   p->shaderDenormPreserveFloat32 = false;
   p->shaderRoundingModeRTEFloat32 = true;
   p->shaderRoundingModeRTZFloat32 = false;
   p->shaderSignedZeroInfNanPreserveFloat32 = true;

   p->shaderDenormFlushToZeroFloat64 = false;
   p->shaderDenormPreserveFloat64 = false;
   p->shaderRoundingModeRTEFloat64 = true;
   p->shaderRoundingModeRTZFloat64 = false;
   p->shaderSignedZeroInfNanPreserveFloat64 = true;

   p->maxUpdateAfterBindDescriptorsInAllPools = UINT32_MAX / 64;
   p->shaderUniformBufferArrayNonUniformIndexingNative = false;
   p->shaderSampledImageArrayNonUniformIndexingNative = false;
   p->shaderStorageBufferArrayNonUniformIndexingNative = false;
   p->shaderStorageImageArrayNonUniformIndexingNative = false;
   p->shaderInputAttachmentArrayNonUniformIndexingNative = false;
   p->robustBufferAccessUpdateAfterBind = true;
   p->quadDivergentImplicitLod = false;

   size_t max_descriptor_set_size = 65536; //TODO
   p->maxPerStageDescriptorUpdateAfterBindSamplers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindUniformBuffers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindStorageBuffers = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindSampledImages = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindStorageImages = max_descriptor_set_size;
   p->maxPerStageDescriptorUpdateAfterBindInputAttachments = max_descriptor_set_size;
   p->maxPerStageUpdateAfterBindResources = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindSamplers = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindUniformBuffers = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 16;
   p->maxDescriptorSetUpdateAfterBindStorageBuffers = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 16;
   p->maxDescriptorSetUpdateAfterBindSampledImages = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindStorageImages = max_descriptor_set_size;
   p->maxDescriptorSetUpdateAfterBindInputAttachments = max_descriptor_set_size;

   p->supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT | VK_RESOLVE_MODE_AVERAGE_BIT;
   p->supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
   p->independentResolveNone = false;
   p->independentResolve = false;

   p->filterMinmaxImageComponentMapping = true;
   p->filterMinmaxSingleComponentFormats = true;

   p->maxTimelineSemaphoreValueDifference = UINT64_MAX;
   p->framebufferIntegerColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPhysicalDeviceProperties2(
   VkPhysicalDevice                            physicalDevice,
   VkPhysicalDeviceProperties2                *pProperties)
{
   LVP_FROM_HANDLE(lvp_physical_device, pdevice, physicalDevice);
   lvp_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

   VkPhysicalDeviceVulkan11Properties core_1_1 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES,
   };
   lvp_get_physical_device_properties_1_1(pdevice, &core_1_1);

   VkPhysicalDeviceVulkan12Properties core_1_2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
   };
   lvp_get_physical_device_properties_1_2(pdevice, &core_1_2);

   vk_foreach_struct(ext, pProperties->pNext) {

      if (vk_get_physical_device_core_1_1_property_ext(ext, &core_1_1))
         continue;
      if (vk_get_physical_device_core_1_2_property_ext(ext, &core_1_2))
         continue;
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
         VkPhysicalDevicePushDescriptorPropertiesKHR *properties =
            (VkPhysicalDevicePushDescriptorPropertiesKHR *) ext;
         properties->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_POINT_CLIPPING_PROPERTIES: {
         VkPhysicalDevicePointClippingProperties *properties =
            (VkPhysicalDevicePointClippingProperties*)ext;
         properties->pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_PROPERTIES_EXT: {
         VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *props =
            (VkPhysicalDeviceVertexAttributeDivisorPropertiesEXT *)ext;
         if (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_VERTEX_ELEMENT_INSTANCE_DIVISOR) != 0)
            props->maxVertexAttribDivisor = UINT32_MAX;
         else
            props->maxVertexAttribDivisor = 1;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT: {
         VkPhysicalDeviceTransformFeedbackPropertiesEXT *properties =
            (VkPhysicalDeviceTransformFeedbackPropertiesEXT*)ext;
         properties->maxTransformFeedbackStreams = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_VERTEX_STREAMS);
         properties->maxTransformFeedbackBuffers = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_STREAM_OUTPUT_BUFFERS);
         properties->maxTransformFeedbackBufferSize = UINT32_MAX;
         properties->maxTransformFeedbackStreamDataSize = 512;
         properties->maxTransformFeedbackBufferDataSize = 512;
         properties->maxTransformFeedbackBufferDataStride = 512;
         properties->transformFeedbackQueries = true;
         properties->transformFeedbackStreamsLinesTriangles = false;
         properties->transformFeedbackRasterizationStreamSelect = false;
         properties->transformFeedbackDraw = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_PROPERTIES_EXT: {
         VkPhysicalDeviceLineRasterizationPropertiesEXT *properties =
            (VkPhysicalDeviceLineRasterizationPropertiesEXT *)ext;
         properties->lineSubPixelPrecisionBits =
            pdevice->pscreen->get_param(pdevice->pscreen,
                                        PIPE_CAP_RASTERIZER_SUBPIXEL_BITS);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT: {
         VkPhysicalDeviceExternalMemoryHostPropertiesEXT *properties =
            (VkPhysicalDeviceExternalMemoryHostPropertiesEXT *)ext;
         properties->minImportedHostPointerAlignment = 4096;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_PROPERTIES_EXT: {
         VkPhysicalDeviceCustomBorderColorPropertiesEXT *properties =
            (VkPhysicalDeviceCustomBorderColorPropertiesEXT *)ext;
         properties->maxCustomBorderColorSamplers = 32 * 1024;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_PROPERTIES_EXT: {
         VkPhysicalDeviceProvokingVertexPropertiesEXT *properties =
            (VkPhysicalDeviceProvokingVertexPropertiesEXT*)ext;
         properties->provokingVertexModePerPipeline = true;
         properties->transformFeedbackPreservesTriangleFanProvokingVertex = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_PROPERTIES_EXT: {
         VkPhysicalDeviceMultiDrawPropertiesEXT *props = (VkPhysicalDeviceMultiDrawPropertiesEXT *)ext;
         props->maxMultiDrawCount = 2048;
         break;
      }
      default:
         break;
      }
   }
}

static void lvp_get_physical_device_queue_family_properties(
   VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
   *pQueueFamilyProperties = (VkQueueFamilyProperties) {
      .queueFlags = VK_QUEUE_GRAPHICS_BIT |
      VK_QUEUE_COMPUTE_BIT |
      VK_QUEUE_TRANSFER_BIT,
      .queueCount = 1,
      .timestampValidBits = 64,
      .minImageTransferGranularity = (VkExtent3D) { 1, 1, 1 },
   };
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPhysicalDeviceQueueFamilyProperties(
   VkPhysicalDevice                            physicalDevice,
   uint32_t*                                   pCount,
   VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
   if (pQueueFamilyProperties == NULL) {
      *pCount = 1;
      return;
   }

   assert(*pCount >= 1);
   lvp_get_physical_device_queue_family_properties(pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice                            physicalDevice,
   uint32_t*                                   pCount,
   VkQueueFamilyProperties2                   *pQueueFamilyProperties)
{
   if (pQueueFamilyProperties == NULL) {
      *pCount = 1;
      return;
   }

   assert(*pCount >= 1);
   lvp_get_physical_device_queue_family_properties(&pQueueFamilyProperties->queueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPhysicalDeviceMemoryProperties(
   VkPhysicalDevice                            physicalDevice,
   VkPhysicalDeviceMemoryProperties*           pMemoryProperties)
{
   pMemoryProperties->memoryTypeCount = 1;
   pMemoryProperties->memoryTypes[0] = (VkMemoryType) {
      .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
      VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
      .heapIndex = 0,
   };

   pMemoryProperties->memoryHeapCount = 1;
   pMemoryProperties->memoryHeaps[0] = (VkMemoryHeap) {
      .size = 2ULL*1024*1024*1024,
      .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
   };
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPhysicalDeviceMemoryProperties2(
   VkPhysicalDevice                            physicalDevice,
   VkPhysicalDeviceMemoryProperties2          *pMemoryProperties)
{
   lvp_GetPhysicalDeviceMemoryProperties(physicalDevice,
                                         &pMemoryProperties->memoryProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
lvp_GetMemoryHostPointerPropertiesEXT(
   VkDevice _device,
   VkExternalMemoryHandleTypeFlagBits handleType,
   const void *pHostPointer,
   VkMemoryHostPointerPropertiesEXT *pMemoryHostPointerProperties)
{
   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT: {
      pMemoryHostPointerProperties->memoryTypeBits = 1;
      return VK_SUCCESS;
   }
   default:
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL lvp_GetInstanceProcAddr(
   VkInstance                                  _instance,
   const char*                                 pName)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);
   return vk_instance_get_proc_addr(&instance->vk,
                                    &lvp_instance_entrypoints,
                                    pName);
}

/* Windows will use a dll definition file to avoid build errors. */
#ifdef _WIN32
#undef PUBLIC
#define PUBLIC
#endif

/* The loader wants us to expose a second GetInstanceProcAddr function
 * to work around certain LD_PRELOAD issues seen in apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
   VkInstance                                  instance,
   const char*                                 pName);

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
   VkInstance                                  instance,
   const char*                                 pName)
{
   return lvp_GetInstanceProcAddr(instance, pName);
}

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(
   VkInstance                                  _instance,
   const char*                                 pName);

PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(
   VkInstance                                  _instance,
   const char*                                 pName)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);
   return vk_instance_get_physical_device_proc_addr(&instance->vk, pName);
}

static void
set_last_fence(struct lvp_device *device, struct pipe_fence_handle *handle, uint64_t timeline)
{
   simple_mtx_lock(&device->queue.last_lock);
   device->queue.last_fence_timeline = timeline;
   device->pscreen->fence_reference(device->pscreen, &device->queue.last_fence, handle);
   simple_mtx_unlock(&device->queue.last_lock);
}

static void
thread_flush(struct lvp_device *device, struct lvp_fence *fence, uint64_t timeline,
             unsigned num_timelines, struct lvp_semaphore_timeline **timelines)
{
   struct pipe_fence_handle *handle = NULL;
   device->queue.ctx->flush(device->queue.ctx, &handle, 0);
   if (fence)
      fence->handle = handle;
   set_last_fence(device, handle, timeline);
   /* this is the array of signaling timeline semaphore links */
   for (unsigned i = 0; i < num_timelines; i++)
      timelines[i]->fence = handle;
}

/* get a new timeline link for creating a new signal event
 * sema->lock MUST be locked before calling
 */
static struct lvp_semaphore_timeline *
get_semaphore_link(struct lvp_semaphore *sema)
{
   if (!util_dynarray_num_elements(&sema->links, struct lvp_semaphore_timeline*)) {
#define NUM_LINKS 50
      /* bucket allocate using the ralloc ctx because I like buckets */
      struct lvp_semaphore_timeline *link = ralloc_array(sema->mem, struct lvp_semaphore_timeline, NUM_LINKS);
      for (unsigned i = 0; i < NUM_LINKS; i++) {
         link[i].next = NULL;
         link[i].fence = NULL;
         util_dynarray_append(&sema->links, struct lvp_semaphore_timeline*, &link[i]);
      }
   }
   struct lvp_semaphore_timeline *tl = util_dynarray_pop(&sema->links, struct lvp_semaphore_timeline*);
   if (sema->timeline)
      sema->latest->next = tl;
   else
      sema->timeline = tl;
   sema->latest = tl;
   return tl;
}

/* prune any timeline links which are older than the current device timeline id
 * sema->lock MUST be locked before calling
 */
static void
prune_semaphore_links(struct lvp_semaphore *sema, uint64_t timeline)
{
   if (!timeline)
      /* zero isn't a valid id to prune with */
      return;
   struct lvp_semaphore_timeline *tl = sema->timeline;
   /* walk the timeline links and pop all the ones that are old */
   while (tl && ((tl->timeline <= timeline) || (tl->signal <= sema->current))) {
      struct lvp_semaphore_timeline *cur = tl;
      /* only update current timeline id if the update is monotonic */
      if (sema->current < tl->signal)
         sema->current = tl->signal;
      util_dynarray_append(&sema->links, struct lvp_semaphore_timeline*, tl);
      tl = tl->next;
      cur->next = NULL;
      cur->fence = NULL;
   }
   /* this is now the current timeline link */
   sema->timeline = tl;
}

/* find a timeline id that can be waited on to satisfy the signal condition
 * sema->lock MUST be locked before calling
 */
static struct lvp_semaphore_timeline *
find_semaphore_timeline(struct lvp_semaphore *sema, uint64_t signal)
{
   for (struct lvp_semaphore_timeline *tl = sema->timeline; tl; tl = tl->next) {
      if (tl->signal >= signal)
         return tl;
   }
   /* never submitted or is completed */
   return NULL;
}

struct timeline_wait {
   bool done;
   struct lvp_semaphore_timeline *tl;
};

static VkResult wait_semaphores(struct lvp_device *device,
    const VkSemaphoreWaitInfo*                  pWaitInfo,
    uint64_t                                    timeout)
{
   /* build array of timeline links to poll */
   VkResult ret = VK_TIMEOUT;
   bool any = (pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT) == VK_SEMAPHORE_WAIT_ANY_BIT;
   unsigned num_remaining = any ? 1 : pWaitInfo->semaphoreCount;
   /* just allocate an array for simplicity */
   struct timeline_wait *tl_array = calloc(pWaitInfo->semaphoreCount, sizeof(struct timeline_wait));

   int64_t abs_timeout = os_time_get_absolute_timeout(timeout);
   /* UINT64_MAX will always overflow, so special case it
    * otherwise, calculate ((timeout / num_semaphores) / 10) to allow waiting 10 times on every semaphore
    */
   uint64_t wait_interval = timeout == UINT64_MAX ? 5000 : timeout / pWaitInfo->semaphoreCount / 10;
   while (num_remaining) {
      for (unsigned i = 0; num_remaining && i < pWaitInfo->semaphoreCount; i++) {
         if (tl_array[i].done) //completed
            continue;
         if (timeout && timeout != UINT64_MAX) {
            /* update remaining timeout on every loop */
            int64_t time_ns = os_time_get_nano();
            if (abs_timeout <= time_ns)
               goto end;
            timeout = abs_timeout > time_ns ? abs_timeout - time_ns : 0;
         }
         const uint64_t waitval = pWaitInfo->pValues[i];
         LVP_FROM_HANDLE(lvp_semaphore, sema, pWaitInfo->pSemaphores[i]);
         if (sema->current >= waitval) {
            tl_array[i].done = true;
            num_remaining--;
            continue;
         }
         if (!tl_array[i].tl) {
            /* no timeline link was available yet: try to find one */
            simple_mtx_lock(&sema->lock);
            /* always prune first to update current timeline id */
            prune_semaphore_links(sema, device->queue.last_finished);
            tl_array[i].tl = find_semaphore_timeline(sema, waitval);
            if (timeout && !tl_array[i].tl) {
               /* still no timeline link available:
                * try waiting on the conditional for a broadcast instead of melting the cpu
                */
               mtx_lock(&sema->submit_lock);
               struct timespec t;
               t.tv_nsec = wait_interval % 1000000000u;
               t.tv_sec = (wait_interval - t.tv_nsec) / 1000000000u;
               cnd_timedwait(&sema->submit, &sema->submit_lock, &t);
               mtx_unlock(&sema->submit_lock);
               tl_array[i].tl = find_semaphore_timeline(sema, waitval);
            }
            simple_mtx_unlock(&sema->lock);
         }
         /* mark semaphore as done if:
          * - timeline id comparison passes
          * - fence for timeline id exists and completes
          */
         if (sema->current >= waitval ||
             (tl_array[i].tl &&
              tl_array[i].tl->fence &&
              device->pscreen->fence_finish(device->pscreen, NULL, tl_array[i].tl->fence, wait_interval))) {
            tl_array[i].done = true;
            num_remaining--;
         }
      }
      if (!timeout)
         break;
   }
   if (!num_remaining)
      ret = VK_SUCCESS;

end:
   free(tl_array);
   return ret;
}

void
queue_thread_noop(void *data, void *gdata, int thread_index)
{
   struct lvp_device *device = gdata;
   struct lvp_fence *fence = data;
   thread_flush(device, fence, fence->timeline, 0, NULL);
}

static void
queue_thread(void *data, void *gdata, int thread_index)
{
   struct lvp_queue_work *task = data;
   struct lvp_device *device = gdata;
   struct lvp_queue *queue = &device->queue;

   if (task->wait_count) {
      /* identical to WaitSemaphores */
      VkSemaphoreWaitInfo wait;
      wait.flags = 0; //wait on all semaphores
      wait.semaphoreCount = task->wait_count;
      wait.pSemaphores = task->waits;
      wait.pValues = task->wait_vals;
      //wait
      wait_semaphores(device, &wait, UINT64_MAX);
   }

   //execute
   for (unsigned i = 0; i < task->cmd_buffer_count; i++) {
      lvp_execute_cmds(queue->device, queue, task->cmd_buffers[i]);
   }

   thread_flush(device, task->fence, task->timeline, task->timeline_count, task->timelines);
   free(task);
}

static VkResult
lvp_queue_init(struct lvp_device *device, struct lvp_queue *queue,
               const VkDeviceQueueCreateInfo *create_info,
               uint32_t index_in_family)
{
   VkResult result = vk_queue_init(&queue->vk, &device->vk, create_info,
                                   index_in_family);
   if (result != VK_SUCCESS)
      return result;

   queue->device = device;

   simple_mtx_init(&queue->last_lock, mtx_plain);
   queue->timeline = 0;
   queue->ctx = device->pscreen->context_create(device->pscreen, NULL, PIPE_CONTEXT_ROBUST_BUFFER_ACCESS);
   queue->cso = cso_create_context(queue->ctx, CSO_NO_VBUF);
   util_queue_init(&queue->queue, "lavapipe", 8, 1, UTIL_QUEUE_INIT_RESIZE_IF_FULL, device);
   p_atomic_set(&queue->count, 0);

   return VK_SUCCESS;
}

static void
lvp_queue_finish(struct lvp_queue *queue)
{
   util_queue_finish(&queue->queue);
   util_queue_destroy(&queue->queue);

   cso_destroy_context(queue->cso);
   queue->ctx->destroy(queue->ctx);
   simple_mtx_destroy(&queue->last_lock);

   vk_queue_finish(&queue->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateDevice(
   VkPhysicalDevice                            physicalDevice,
   const VkDeviceCreateInfo*                   pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkDevice*                                   pDevice)
{
   fprintf(stderr, "WARNING: lavapipe is not a conformant vulkan implementation, testing use only.\n");

   LVP_FROM_HANDLE(lvp_physical_device, physical_device, physicalDevice);
   struct lvp_device *device;
   struct lvp_instance *instance = (struct lvp_instance *)physical_device->vk.instance;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);

   device = vk_zalloc2(&physical_device->vk.instance->alloc, pAllocator,
                       sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_device_dispatch_table dispatch_table;
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
      &lvp_device_entrypoints, true);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
      &wsi_device_entrypoints, false);
   VkResult result = vk_device_init(&device->vk,
                                    &physical_device->vk,
                                    &dispatch_table, pCreateInfo,
                                    pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, device);
      return result;
   }

   device->instance = (struct lvp_instance *)physical_device->vk.instance;
   device->physical_device = physical_device;

   device->pscreen = physical_device->pscreen;

   assert(pCreateInfo->queueCreateInfoCount == 1);
   assert(pCreateInfo->pQueueCreateInfos[0].queueFamilyIndex == 0);
   assert(pCreateInfo->pQueueCreateInfos[0].queueCount == 1);
   lvp_queue_init(device, &device->queue, pCreateInfo->pQueueCreateInfos, 0);

   *pDevice = lvp_device_to_handle(device);

   return VK_SUCCESS;

}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyDevice(
   VkDevice                                    _device,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);

   if (device->queue.last_fence)
      device->pscreen->fence_reference(device->pscreen, &device->queue.last_fence, NULL);
   lvp_queue_finish(&device->queue);
   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_EnumerateInstanceExtensionProperties(
   const char*                                 pLayerName,
   uint32_t*                                   pPropertyCount,
   VkExtensionProperties*                      pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &lvp_instance_extensions_supported, pPropertyCount, pProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_EnumerateInstanceLayerProperties(
   uint32_t*                                   pPropertyCount,
   VkLayerProperties*                          pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_EnumerateDeviceLayerProperties(
   VkPhysicalDevice                            physicalDevice,
   uint32_t*                                   pPropertyCount,
   VkLayerProperties*                          pProperties)
{
   if (pProperties == NULL) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   /* None supported at this time */
   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_QueueSubmit(
   VkQueue                                     _queue,
   uint32_t                                    submitCount,
   const VkSubmitInfo*                         pSubmits,
   VkFence                                     _fence)
{
   LVP_FROM_HANDLE(lvp_queue, queue, _queue);
   LVP_FROM_HANDLE(lvp_fence, fence, _fence);

   /* each submit is a separate job to simplify/streamline semaphore waits */
   for (uint32_t i = 0; i < submitCount; i++) {
      uint64_t timeline = ++queue->timeline;
      struct lvp_queue_work *task = malloc(sizeof(struct lvp_queue_work) +
                                           pSubmits[i].commandBufferCount * sizeof(struct lvp_cmd_buffer *) +
                                           pSubmits[i].signalSemaphoreCount * sizeof(struct lvp_semaphore_timeline*) +
                                           pSubmits[i].waitSemaphoreCount * (sizeof(VkSemaphore) + sizeof(uint64_t)));
      task->cmd_buffer_count = pSubmits[i].commandBufferCount;
      task->timeline_count = pSubmits[i].signalSemaphoreCount;
      task->wait_count = pSubmits[i].waitSemaphoreCount;
      task->fence = fence;
      task->timeline = timeline;
      task->cmd_buffers = (struct lvp_cmd_buffer **)(task + 1);
      task->timelines = (struct lvp_semaphore_timeline**)((uint8_t*)task->cmd_buffers + pSubmits[i].commandBufferCount * sizeof(struct lvp_cmd_buffer *));
      task->waits = (VkSemaphore*)((uint8_t*)task->timelines + pSubmits[i].signalSemaphoreCount * sizeof(struct lvp_semaphore_timeline *));
      task->wait_vals = (uint64_t*)((uint8_t*)task->waits + pSubmits[i].waitSemaphoreCount * sizeof(VkSemaphore));

      unsigned c = 0;
      for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; j++) {
         task->cmd_buffers[c++] = lvp_cmd_buffer_from_handle(pSubmits[i].pCommandBuffers[j]);
      }
      const VkTimelineSemaphoreSubmitInfo *info = vk_find_struct_const(pSubmits[i].pNext, TIMELINE_SEMAPHORE_SUBMIT_INFO);
      unsigned s = 0;
      for (unsigned j = 0; j < pSubmits[i].signalSemaphoreCount; j++) {
         LVP_FROM_HANDLE(lvp_semaphore, sema, pSubmits[i].pSignalSemaphores[j]);
         if (!sema->is_timeline) {
            /* non-timeline semaphores never matter to lavapipe */
            task->timeline_count--;
            continue;
         }
         simple_mtx_lock(&sema->lock);
         /* always prune first to make links available and update timeline id */
         prune_semaphore_links(sema, queue->last_finished);
         if (sema->current < info->pSignalSemaphoreValues[j]) {
            /* only signal semaphores if the new id is >= the current one */
            struct lvp_semaphore_timeline *tl = get_semaphore_link(sema);
            tl->signal = info->pSignalSemaphoreValues[j];
            tl->timeline = timeline;
            task->timelines[s] = tl;
            s++;
         } else
            task->timeline_count--;
         simple_mtx_unlock(&sema->lock);
      }
      unsigned w = 0;
      for (unsigned j = 0; j < pSubmits[i].waitSemaphoreCount; j++) {
         LVP_FROM_HANDLE(lvp_semaphore, sema, pSubmits[i].pWaitSemaphores[j]);
         if (!sema->is_timeline) {
            /* non-timeline semaphores never matter to lavapipe */
            task->wait_count--;
            continue;
         }
         simple_mtx_lock(&sema->lock);
         /* always prune first to update timeline id */
         prune_semaphore_links(sema, queue->last_finished);
         if (info->pWaitSemaphoreValues[j] &&
             pSubmits[i].pWaitDstStageMask && pSubmits[i].pWaitDstStageMask[j] &&
             sema->current < info->pWaitSemaphoreValues[j]) {
            /* only wait on semaphores if the new id is > the current one and a wait mask is set
             * 
             * technically the mask could be used to check whether there's gfx/compute ops on a cmdbuf and no-op,
             * but probably that's not worth the complexity
             */
            task->waits[w] = pSubmits[i].pWaitSemaphores[j];
            task->wait_vals[w] = info->pWaitSemaphoreValues[j];
            w++;
         } else
            task->wait_count--;
         simple_mtx_unlock(&sema->lock);
      }
      if (fence && i == submitCount - 1) {
         /* u_queue fences should only be signaled for the last submit, as this is the one that
          * the vk fence represents
          */
         fence->timeline = timeline;
         util_queue_add_job(&queue->queue, task, &fence->fence, queue_thread, NULL, 0);
      } else
         util_queue_add_job(&queue->queue, task, NULL, queue_thread, NULL, 0);
   }
   if (!submitCount && fence) {
      /* special case where a fence is created to use as a synchronization point */
      fence->timeline = p_atomic_inc_return(&queue->timeline);
      util_queue_add_job(&queue->queue, fence, &fence->fence, queue_thread_noop, NULL, 0);
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_QueueWaitIdle(
   VkQueue                                     _queue)
{
   LVP_FROM_HANDLE(lvp_queue, queue, _queue);

   util_queue_finish(&queue->queue);
   simple_mtx_lock(&queue->last_lock);
   uint64_t timeline = queue->last_fence_timeline;
   if (queue->last_fence) {
      queue->device->pscreen->fence_finish(queue->device->pscreen, NULL, queue->last_fence, PIPE_TIMEOUT_INFINITE);
      queue->device->pscreen->fence_reference(queue->device->pscreen, &queue->device->queue.last_fence, NULL);
      queue->last_finished = timeline;
   }
   simple_mtx_unlock(&queue->last_lock);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_DeviceWaitIdle(
   VkDevice                                    _device)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);

   lvp_QueueWaitIdle(lvp_queue_to_handle(&device->queue));

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_AllocateMemory(
   VkDevice                                    _device,
   const VkMemoryAllocateInfo*                 pAllocateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkDeviceMemory*                             pMem)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_device_memory *mem;
   ASSERTED const VkExportMemoryAllocateInfo *export_info = NULL;
   ASSERTED const VkImportMemoryFdInfoKHR *import_info = NULL;
   const VkImportMemoryHostPointerInfoEXT *host_ptr_info = NULL;
   VkResult error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   if (pAllocateInfo->allocationSize == 0) {
      /* Apparently, this is allowed */
      *pMem = VK_NULL_HANDLE;
      return VK_SUCCESS;
   }

   vk_foreach_struct_const(ext, pAllocateInfo->pNext) {
      switch ((unsigned)ext->sType) {
      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT:
         host_ptr_info = (VkImportMemoryHostPointerInfoEXT*)ext;
         assert(host_ptr_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT);
         break;
      case VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO:
         export_info = (VkExportMemoryAllocateInfo*)ext;
         assert(export_info->handleTypes == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
         break;
      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR:
         import_info = (VkImportMemoryFdInfoKHR*)ext;
         assert(import_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);
         break;
      default:
         break;
      }
   }

#ifdef PIPE_MEMORY_FD
   if (import_info != NULL && import_info->fd < 0) {
      return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
#endif

   mem = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*mem), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (mem == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &mem->base,
                       VK_OBJECT_TYPE_DEVICE_MEMORY);

   mem->memory_type = LVP_DEVICE_MEMORY_TYPE_DEFAULT;
   mem->backed_fd = -1;

   if (host_ptr_info) {
      mem->pmem = host_ptr_info->pHostPointer;
      mem->memory_type = LVP_DEVICE_MEMORY_TYPE_USER_PTR;
   }
#ifdef PIPE_MEMORY_FD
   else if(import_info) {
      uint64_t size;
      if(!device->pscreen->import_memory_fd(device->pscreen, import_info->fd, &mem->pmem, &size)) {
         close(import_info->fd);
         error = VK_ERROR_INVALID_EXTERNAL_HANDLE;
         goto fail;
      }
      if(size < pAllocateInfo->allocationSize) {
         device->pscreen->free_memory_fd(device->pscreen, mem->pmem);
         close(import_info->fd);
         goto fail;
      }
      if (export_info) {
         mem->backed_fd = import_info->fd;
      }
      else {
         close(import_info->fd);
      }
      mem->memory_type = LVP_DEVICE_MEMORY_TYPE_OPAQUE_FD;
   }
   else if (export_info) {
      mem->pmem = device->pscreen->allocate_memory_fd(device->pscreen, pAllocateInfo->allocationSize, &mem->backed_fd);
      if (!mem->pmem || mem->backed_fd < 0) {
         goto fail;
      }
      mem->memory_type = LVP_DEVICE_MEMORY_TYPE_OPAQUE_FD;
   }
#endif
   else {
      mem->pmem = device->pscreen->allocate_memory(device->pscreen, pAllocateInfo->allocationSize);
      if (!mem->pmem) {
         goto fail;
      }
   }

   mem->type_index = pAllocateInfo->memoryTypeIndex;

   *pMem = lvp_device_memory_to_handle(mem);

   return VK_SUCCESS;

fail:
   vk_free2(&device->vk.alloc, pAllocator, mem);
   return vk_error(device, error);
}

VKAPI_ATTR void VKAPI_CALL lvp_FreeMemory(
   VkDevice                                    _device,
   VkDeviceMemory                              _mem,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_device_memory, mem, _mem);

   if (mem == NULL)
      return;

   switch(mem->memory_type) {
   case LVP_DEVICE_MEMORY_TYPE_DEFAULT:
      device->pscreen->free_memory(device->pscreen, mem->pmem);
      break;
#ifdef PIPE_MEMORY_FD
   case LVP_DEVICE_MEMORY_TYPE_OPAQUE_FD:
      device->pscreen->free_memory_fd(device->pscreen, mem->pmem);
      if(mem->backed_fd >= 0)
         close(mem->backed_fd);
      break;
#endif
   case LVP_DEVICE_MEMORY_TYPE_USER_PTR:
   default:
      break;
   }
   vk_object_base_finish(&mem->base);
   vk_free2(&device->vk.alloc, pAllocator, mem);

}

VKAPI_ATTR VkResult VKAPI_CALL lvp_MapMemory(
   VkDevice                                    _device,
   VkDeviceMemory                              _memory,
   VkDeviceSize                                offset,
   VkDeviceSize                                size,
   VkMemoryMapFlags                            flags,
   void**                                      ppData)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_device_memory, mem, _memory);
   void *map;
   if (mem == NULL) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   map = device->pscreen->map_memory(device->pscreen, mem->pmem);

   *ppData = (char *)map + offset;
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_UnmapMemory(
   VkDevice                                    _device,
   VkDeviceMemory                              _memory)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_device_memory, mem, _memory);

   if (mem == NULL)
      return;

   device->pscreen->unmap_memory(device->pscreen, mem->pmem);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_FlushMappedMemoryRanges(
   VkDevice                                    _device,
   uint32_t                                    memoryRangeCount,
   const VkMappedMemoryRange*                  pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_InvalidateMappedMemoryRanges(
   VkDevice                                    _device,
   uint32_t                                    memoryRangeCount,
   const VkMappedMemoryRange*                  pMemoryRanges)
{
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetBufferMemoryRequirements(
   VkDevice                                    device,
   VkBuffer                                    _buffer,
   VkMemoryRequirements*                       pMemoryRequirements)
{
   LVP_FROM_HANDLE(lvp_buffer, buffer, _buffer);

   /* The Vulkan spec (git aaed022) says:
    *
    *    memoryTypeBits is a bitfield and contains one bit set for every
    *    supported memory type for the resource. The bit `1<<i` is set if and
    *    only if the memory type `i` in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported.
    *
    * We support exactly one memory type.
    */
   pMemoryRequirements->memoryTypeBits = 1;

   pMemoryRequirements->size = buffer->total_size;
   pMemoryRequirements->alignment = 64;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetBufferMemoryRequirements2(
   VkDevice                                     device,
   const VkBufferMemoryRequirementsInfo2       *pInfo,
   VkMemoryRequirements2                       *pMemoryRequirements)
{
   lvp_GetBufferMemoryRequirements(device, pInfo->buffer,
                                   &pMemoryRequirements->memoryRequirements);
   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req =
            (VkMemoryDedicatedRequirements *) ext;
         req->requiresDedicatedAllocation = false;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL lvp_GetImageMemoryRequirements(
   VkDevice                                    device,
   VkImage                                     _image,
   VkMemoryRequirements*                       pMemoryRequirements)
{
   LVP_FROM_HANDLE(lvp_image, image, _image);
   pMemoryRequirements->memoryTypeBits = 1;

   pMemoryRequirements->size = image->size;
   pMemoryRequirements->alignment = image->alignment;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetImageMemoryRequirements2(
   VkDevice                                    device,
   const VkImageMemoryRequirementsInfo2       *pInfo,
   VkMemoryRequirements2                      *pMemoryRequirements)
{
   lvp_GetImageMemoryRequirements(device, pInfo->image,
                                  &pMemoryRequirements->memoryRequirements);

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req =
            (VkMemoryDedicatedRequirements *) ext;
         req->requiresDedicatedAllocation = false;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL lvp_GetImageSparseMemoryRequirements(
   VkDevice                                    device,
   VkImage                                     image,
   uint32_t*                                   pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements*            pSparseMemoryRequirements)
{
   stub();
}

VKAPI_ATTR void VKAPI_CALL lvp_GetImageSparseMemoryRequirements2(
   VkDevice                                    device,
   const VkImageSparseMemoryRequirementsInfo2* pInfo,
   uint32_t* pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2* pSparseMemoryRequirements)
{
   stub();
}

VKAPI_ATTR void VKAPI_CALL lvp_GetDeviceMemoryCommitment(
   VkDevice                                    device,
   VkDeviceMemory                              memory,
   VkDeviceSize*                               pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_BindBufferMemory2(VkDevice _device,
                               uint32_t bindInfoCount,
                               const VkBindBufferMemoryInfo *pBindInfos)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      LVP_FROM_HANDLE(lvp_device_memory, mem, pBindInfos[i].memory);
      LVP_FROM_HANDLE(lvp_buffer, buffer, pBindInfos[i].buffer);

      buffer->pmem = mem->pmem;
      device->pscreen->resource_bind_backing(device->pscreen,
                                             buffer->bo,
                                             mem->pmem,
                                             pBindInfos[i].memoryOffset);
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_BindImageMemory2(VkDevice _device,
                              uint32_t bindInfoCount,
                              const VkBindImageMemoryInfo *pBindInfos)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      const VkBindImageMemoryInfo *bind_info = &pBindInfos[i];
      LVP_FROM_HANDLE(lvp_device_memory, mem, bind_info->memory);
      LVP_FROM_HANDLE(lvp_image, image, bind_info->image);
      bool did_bind = false;

      vk_foreach_struct_const(s, bind_info->pNext) {
         switch (s->sType) {
         case VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR: {
            const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
               (const VkBindImageMemorySwapchainInfoKHR *) s;
            struct lvp_image *swapchain_image =
               lvp_swapchain_get_image(swapchain_info->swapchain,
                                       swapchain_info->imageIndex);

            image->pmem = swapchain_image->pmem;
            image->memory_offset = swapchain_image->memory_offset;
            device->pscreen->resource_bind_backing(device->pscreen,
                                                   image->bo,
                                                   image->pmem,
                                                   image->memory_offset);
            did_bind = true;
            break;
         }
         default:
            break;
         }
      }

      if (!did_bind) {
         if (!device->pscreen->resource_bind_backing(device->pscreen,
                                                     image->bo,
                                                     mem->pmem,
                                                     bind_info->memoryOffset)) {
            /* This is probably caused by the texture being too large, so let's
             * report this as the *closest* allowed error-code. It's not ideal,
             * but it's unlikely that anyone will care too much.
             */
            return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         }
         image->pmem = mem->pmem;
         image->memory_offset = bind_info->memoryOffset;
      }
   }
   return VK_SUCCESS;
}

#ifdef PIPE_MEMORY_FD

VkResult
lvp_GetMemoryFdKHR(VkDevice _device, const VkMemoryGetFdInfoKHR *pGetFdInfo, int *pFD)
{
   LVP_FROM_HANDLE(lvp_device_memory, memory, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);
   assert(pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT);

   *pFD = dup(memory->backed_fd);
   assert(*pFD >= 0);
   return VK_SUCCESS;
}

VkResult
lvp_GetMemoryFdPropertiesKHR(VkDevice _device,
                             VkExternalMemoryHandleTypeFlagBits handleType,
                             int fd,
                             VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);

   assert(pMemoryFdProperties->sType == VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR);

   if(handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT) {
      // There is only one memoryType so select this one
      pMemoryFdProperties->memoryTypeBits = 1;
   }
   else
      return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   return VK_SUCCESS;
}

#endif

VKAPI_ATTR VkResult VKAPI_CALL lvp_QueueBindSparse(
   VkQueue                                     queue,
   uint32_t                                    bindInfoCount,
   const VkBindSparseInfo*                     pBindInfo,
   VkFence                                     fence)
{
   stub_return(VK_ERROR_INCOMPATIBLE_DRIVER);
}


VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateFence(
   VkDevice                                    _device,
   const VkFenceCreateInfo*                    pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkFence*                                    pFence)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_fence *fence;

   fence = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*fence), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (fence == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   vk_object_base_init(&device->vk, &fence->base, VK_OBJECT_TYPE_FENCE);
   util_queue_fence_init(&fence->fence);
   fence->signalled = (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) == VK_FENCE_CREATE_SIGNALED_BIT;

   fence->handle = NULL;
   fence->timeline = 0;
   *pFence = lvp_fence_to_handle(fence);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyFence(
   VkDevice                                    _device,
   VkFence                                     _fence,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_fence, fence, _fence);

   if (!_fence)
      return;
   /* evade annoying destroy assert */
   util_queue_fence_init(&fence->fence);
   util_queue_fence_destroy(&fence->fence);
   if (fence->handle)
      device->pscreen->fence_reference(device->pscreen, &fence->handle, NULL);

   vk_object_base_finish(&fence->base);
   vk_free2(&device->vk.alloc, pAllocator, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_ResetFences(
   VkDevice                                    _device,
   uint32_t                                    fenceCount,
   const VkFence*                              pFences)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   for (unsigned i = 0; i < fenceCount; i++) {
      struct lvp_fence *fence = lvp_fence_from_handle(pFences[i]);
      /* ensure u_queue doesn't explode when submitting a completed lvp_fence
       * which has not yet signalled its u_queue fence
       */
      util_queue_fence_wait(&fence->fence);

      if (fence->handle) {
         simple_mtx_lock(&device->queue.last_lock);
         if (fence->handle == device->queue.last_fence)
            device->pscreen->fence_reference(device->pscreen, &device->queue.last_fence, NULL);
         simple_mtx_unlock(&device->queue.last_lock);
         device->pscreen->fence_reference(device->pscreen, &fence->handle, NULL);
      }
      fence->signalled = false;
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_GetFenceStatus(
   VkDevice                                    _device,
   VkFence                                     _fence)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_fence, fence, _fence);

   if (fence->signalled)
      return VK_SUCCESS;

   if (!util_queue_fence_is_signalled(&fence->fence) ||
       !fence->handle ||
       !device->pscreen->fence_finish(device->pscreen, NULL, fence->handle, 0))
      return VK_NOT_READY;

   fence->signalled = true;
   simple_mtx_lock(&device->queue.last_lock);
   if (fence->handle == device->queue.last_fence) {
      device->pscreen->fence_reference(device->pscreen, &device->queue.last_fence, NULL);
      device->queue.last_finished = fence->timeline;
   }
   simple_mtx_unlock(&device->queue.last_lock);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateFramebuffer(
   VkDevice                                    _device,
   const VkFramebufferCreateInfo*              pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkFramebuffer*                              pFramebuffer)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_framebuffer *framebuffer;
   const VkFramebufferAttachmentsCreateInfo *imageless_create_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           FRAMEBUFFER_ATTACHMENTS_CREATE_INFO);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

   size_t size = sizeof(*framebuffer);

   if (!imageless_create_info)
      size += sizeof(struct lvp_image_view *) * pCreateInfo->attachmentCount;
   framebuffer = vk_alloc2(&device->vk.alloc, pAllocator, size, 8,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (framebuffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &framebuffer->base,
                       VK_OBJECT_TYPE_FRAMEBUFFER);

   if (!imageless_create_info) {
      framebuffer->attachment_count = pCreateInfo->attachmentCount;
      for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
         VkImageView _iview = pCreateInfo->pAttachments[i];
         framebuffer->attachments[i] = lvp_image_view_from_handle(_iview);
      }
   }

   framebuffer->width = pCreateInfo->width;
   framebuffer->height = pCreateInfo->height;
   framebuffer->layers = pCreateInfo->layers;
   framebuffer->imageless = !!imageless_create_info;

   *pFramebuffer = lvp_framebuffer_to_handle(framebuffer);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyFramebuffer(
   VkDevice                                    _device,
   VkFramebuffer                               _fb,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_framebuffer, fb, _fb);

   if (!fb)
      return;
   vk_object_base_finish(&fb->base);
   vk_free2(&device->vk.alloc, pAllocator, fb);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_WaitForFences(
   VkDevice                                    _device,
   uint32_t                                    fenceCount,
   const VkFence*                              pFences,
   VkBool32                                    waitAll,
   uint64_t                                    timeout)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_fence *fence = NULL;

   /* lavapipe is completely synchronous, so only one fence needs to be waited on */
   if (waitAll) {
      /* find highest timeline id */
      for (unsigned i = 0; i < fenceCount; i++) {
         struct lvp_fence *f = lvp_fence_from_handle(pFences[i]);

         /* this is an unsubmitted fence: immediately bail out */
         if (!f->timeline && !f->signalled)
            return VK_TIMEOUT;
         if (!fence || f->timeline > fence->timeline)
            fence = f;
      }
   } else {
      /* find lowest timeline id */
      for (unsigned i = 0; i < fenceCount; i++) {
         struct lvp_fence *f = lvp_fence_from_handle(pFences[i]);
         if (f->signalled)
            return VK_SUCCESS;
         if (f->timeline && (!fence || f->timeline < fence->timeline))
            fence = f;
      }
   }
   if (!fence)
      return VK_TIMEOUT;
   if (fence->signalled)
      return VK_SUCCESS;

   if (!util_queue_fence_is_signalled(&fence->fence)) {
      int64_t abs_timeout = os_time_get_absolute_timeout(timeout);
      if (!util_queue_fence_wait_timeout(&fence->fence, abs_timeout))
         return VK_TIMEOUT;

      int64_t time_ns = os_time_get_nano();
      timeout = abs_timeout > time_ns ? abs_timeout - time_ns : 0;
   }

   if (!fence->handle ||
       !device->pscreen->fence_finish(device->pscreen, NULL, fence->handle, timeout))
      return VK_TIMEOUT;
   simple_mtx_lock(&device->queue.last_lock);
   if (fence->handle == device->queue.last_fence) {
      device->pscreen->fence_reference(device->pscreen, &device->queue.last_fence, NULL);
      device->queue.last_finished = fence->timeline;
   }
   simple_mtx_unlock(&device->queue.last_lock);
   fence->signalled = true;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateSemaphore(
   VkDevice                                    _device,
   const VkSemaphoreCreateInfo*                pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkSemaphore*                                pSemaphore)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);

   struct lvp_semaphore *sema = vk_alloc2(&device->vk.alloc, pAllocator,
                                          sizeof(*sema), 8,
                                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!sema)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   vk_object_base_init(&device->vk, &sema->base,
                       VK_OBJECT_TYPE_SEMAPHORE);

   const VkSemaphoreTypeCreateInfo *info = vk_find_struct_const(pCreateInfo->pNext, SEMAPHORE_TYPE_CREATE_INFO);
   sema->is_timeline = info && info->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE;
   if (sema->is_timeline) {
      sema->is_timeline = true;
      sema->timeline = NULL;
      sema->current = info->initialValue;
      sema->mem = ralloc_context(NULL);
      util_dynarray_init(&sema->links, sema->mem);
      simple_mtx_init(&sema->lock, mtx_plain);
      mtx_init(&sema->submit_lock, mtx_plain);
      cnd_init(&sema->submit);
   }

   *pSemaphore = lvp_semaphore_to_handle(sema);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroySemaphore(
   VkDevice                                    _device,
   VkSemaphore                                 _semaphore,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_semaphore, sema, _semaphore);

   if (!_semaphore)
      return;
   if (sema->is_timeline) {
      ralloc_free(sema->mem);
      simple_mtx_destroy(&sema->lock);
      mtx_destroy(&sema->submit_lock);
      cnd_destroy(&sema->submit);
   }
   vk_object_base_finish(&sema->base);
   vk_free2(&device->vk.alloc, pAllocator, sema);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_WaitSemaphores(
    VkDevice                                    _device,
    const VkSemaphoreWaitInfo*                  pWaitInfo,
    uint64_t                                    timeout)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   /* same mechanism as used by queue submit */
   return wait_semaphores(device, pWaitInfo, timeout);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_GetSemaphoreCounterValue(
    VkDevice                                    _device,
    VkSemaphore                                 _semaphore,
    uint64_t*                                   pValue)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_semaphore, sema, _semaphore);
   simple_mtx_lock(&sema->lock);
   prune_semaphore_links(sema, device->queue.last_finished);
   *pValue = sema->current;
   simple_mtx_unlock(&sema->lock);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_SignalSemaphore(
    VkDevice                                    _device,
    const VkSemaphoreSignalInfo*                pSignalInfo)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_semaphore, sema, pSignalInfo->semaphore);

   /* try to remain monotonic */
   if (sema->current < pSignalInfo->value)
      sema->current = pSignalInfo->value;
   cnd_broadcast(&sema->submit);
   simple_mtx_lock(&sema->lock);
   prune_semaphore_links(sema, device->queue.last_finished);
   simple_mtx_unlock(&sema->lock);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateEvent(
   VkDevice                                    _device,
   const VkEventCreateInfo*                    pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkEvent*                                    pEvent)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_event *event = vk_alloc2(&device->vk.alloc, pAllocator,
                                       sizeof(*event), 8,
                                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!event)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &event->base, VK_OBJECT_TYPE_EVENT);
   *pEvent = lvp_event_to_handle(event);
   event->event_storage = 0;

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyEvent(
   VkDevice                                    _device,
   VkEvent                                     _event,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_event, event, _event);

   if (!event)
      return;

   vk_object_base_finish(&event->base);
   vk_free2(&device->vk.alloc, pAllocator, event);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_GetEventStatus(
   VkDevice                                    _device,
   VkEvent                                     _event)
{
   LVP_FROM_HANDLE(lvp_event, event, _event);
   if (event->event_storage == 1)
      return VK_EVENT_SET;
   return VK_EVENT_RESET;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_SetEvent(
   VkDevice                                    _device,
   VkEvent                                     _event)
{
   LVP_FROM_HANDLE(lvp_event, event, _event);
   event->event_storage = 1;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_ResetEvent(
   VkDevice                                    _device,
   VkEvent                                     _event)
{
   LVP_FROM_HANDLE(lvp_event, event, _event);
   event->event_storage = 0;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateSampler(
   VkDevice                                    _device,
   const VkSamplerCreateInfo*                  pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkSampler*                                  pSampler)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_sampler *sampler;
   const VkSamplerReductionModeCreateInfo *reduction_mode_create_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           SAMPLER_REDUCTION_MODE_CREATE_INFO);
   const VkSamplerCustomBorderColorCreateInfoEXT *custom_border_color_create_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   sampler = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*sampler), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &sampler->base,
                       VK_OBJECT_TYPE_SAMPLER);
   sampler->create_info = *pCreateInfo;

   switch (pCreateInfo->borderColor) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
   default:
      memset(&sampler->border_color, 0, sizeof(union pipe_color_union));
      break;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
      sampler->border_color.f[0] = sampler->border_color.f[1] =
      sampler->border_color.f[2] = 0.0f;
      sampler->border_color.f[3] = 1.0f;
      break;
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
      sampler->border_color.i[0] = sampler->border_color.i[1] =
      sampler->border_color.i[2] = 0;
      sampler->border_color.i[3] = 1;
      break;
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
      sampler->border_color.f[0] = sampler->border_color.f[1] =
      sampler->border_color.f[2] = 1.0f;
      sampler->border_color.f[3] = 1.0f;
      break;
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
      sampler->border_color.i[0] = sampler->border_color.i[1] =
      sampler->border_color.i[2] = 1;
      sampler->border_color.i[3] = 1;
      break;
   case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
   case VK_BORDER_COLOR_INT_CUSTOM_EXT:
      assert(custom_border_color_create_info != NULL);
      memcpy(&sampler->border_color,
             &custom_border_color_create_info->customBorderColor,
             sizeof(union pipe_color_union));
      break;
   }

   sampler->reduction_mode = VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
   if (reduction_mode_create_info)
      sampler->reduction_mode = reduction_mode_create_info->reductionMode;

   *pSampler = lvp_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroySampler(
   VkDevice                                    _device,
   VkSampler                                   _sampler,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_sampler, sampler, _sampler);

   if (!_sampler)
      return;
   vk_object_base_finish(&sampler->base);
   vk_free2(&device->vk.alloc, pAllocator, sampler);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateSamplerYcbcrConversionKHR(
    VkDevice                                    device,
    const VkSamplerYcbcrConversionCreateInfo*   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSamplerYcbcrConversion*                   pYcbcrConversion)
{
   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroySamplerYcbcrConversionKHR(
    VkDevice                                    device,
    VkSamplerYcbcrConversion                    ycbcrConversion,
    const VkAllocationCallbacks*                pAllocator)
{
}

/* vk_icd.h does not declare this function, so we declare it here to
 * suppress Wmissing-prototypes.
 */
PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion);

PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion)
{
   /* For the full details on loader interface versioning, see
    * <https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/blob/master/loader/LoaderAndLayerInterface.md>.
    * What follows is a condensed summary, to help you navigate the large and
    * confusing official doc.
    *
    *   - Loader interface v0 is incompatible with later versions. We don't
    *     support it.
    *
    *   - In loader interface v1:
    *       - The first ICD entrypoint called by the loader is
    *         vk_icdGetInstanceProcAddr(). The ICD must statically expose this
    *         entrypoint.
    *       - The ICD must statically expose no other Vulkan symbol unless it is
    *         linked with -Bsymbolic.
    *       - Each dispatchable Vulkan handle created by the ICD must be
    *         a pointer to a struct whose first member is VK_LOADER_DATA. The
    *         ICD must initialize VK_LOADER_DATA.loadMagic to ICD_LOADER_MAGIC.
    *       - The loader implements vkCreate{PLATFORM}SurfaceKHR() and
    *         vkDestroySurfaceKHR(). The ICD must be capable of working with
    *         such loader-managed surfaces.
    *
    *    - Loader interface v2 differs from v1 in:
    *       - The first ICD entrypoint called by the loader is
    *         vk_icdNegotiateLoaderICDInterfaceVersion(). The ICD must
    *         statically expose this entrypoint.
    *
    *    - Loader interface v3 differs from v2 in:
    *        - The ICD must implement vkCreate{PLATFORM}SurfaceKHR(),
    *          vkDestroySurfaceKHR(), and other API which uses VKSurfaceKHR,
    *          because the loader no longer does so.
    *
    *    - Loader interface v4 differs from v3 in:
    *        - The ICD must implement vk_icdGetPhysicalDeviceProcAddr().
    * 
    *    - Loader interface v5 differs from v4 in:
    *        - The ICD must support Vulkan API version 1.1 and must not return 
    *          VK_ERROR_INCOMPATIBLE_DRIVER from vkCreateInstance() unless a
    *          Vulkan Loader with interface v4 or smaller is being used and the
    *          application provides an API version that is greater than 1.0.
    */
   *pSupportedVersion = MIN2(*pSupportedVersion, 5u);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreatePrivateDataSlotEXT(
   VkDevice                                    _device,
   const VkPrivateDataSlotCreateInfoEXT*       pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkPrivateDataSlotEXT*                       pPrivateDataSlot)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   return vk_private_data_slot_create(&device->vk, pCreateInfo, pAllocator,
                                      pPrivateDataSlot);
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyPrivateDataSlotEXT(
   VkDevice                                    _device,
   VkPrivateDataSlotEXT                        privateDataSlot,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   vk_private_data_slot_destroy(&device->vk, privateDataSlot, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_SetPrivateDataEXT(
   VkDevice                                    _device,
   VkObjectType                                objectType,
   uint64_t                                    objectHandle,
   VkPrivateDataSlotEXT                        privateDataSlot,
   uint64_t                                    data)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   return vk_object_base_set_private_data(&device->vk, objectType,
                                          objectHandle, privateDataSlot,
                                          data);
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPrivateDataEXT(
   VkDevice                                    _device,
   VkObjectType                                objectType,
   uint64_t                                    objectHandle,
   VkPrivateDataSlotEXT                        privateDataSlot,
   uint64_t*                                   pData)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   vk_object_base_get_private_data(&device->vk, objectType, objectHandle,
                                   privateDataSlot, pData);
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPhysicalDeviceExternalFenceProperties(
   VkPhysicalDevice                           physicalDevice,
   const VkPhysicalDeviceExternalFenceInfo    *pExternalFenceInfo,
   VkExternalFenceProperties                  *pExternalFenceProperties)
{
   pExternalFenceProperties->exportFromImportedHandleTypes = 0;
   pExternalFenceProperties->compatibleHandleTypes = 0;
   pExternalFenceProperties->externalFenceFeatures = 0;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetPhysicalDeviceExternalSemaphoreProperties(
   VkPhysicalDevice                            physicalDevice,
   const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
   VkExternalSemaphoreProperties               *pExternalSemaphoreProperties)
{
   pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
   pExternalSemaphoreProperties->compatibleHandleTypes = 0;
   pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;
}

static const VkTimeDomainEXT lvp_time_domains[] = {
        VK_TIME_DOMAIN_DEVICE_EXT,
        VK_TIME_DOMAIN_CLOCK_MONOTONIC_EXT,
};

VKAPI_ATTR VkResult VKAPI_CALL lvp_GetPhysicalDeviceCalibrateableTimeDomainsEXT(
   VkPhysicalDevice physicalDevice,
   uint32_t *pTimeDomainCount,
   VkTimeDomainEXT *pTimeDomains)
{
   int d;
   VK_OUTARRAY_MAKE_TYPED(VkTimeDomainEXT, out, pTimeDomains,
                          pTimeDomainCount);

   for (d = 0; d < ARRAY_SIZE(lvp_time_domains); d++) {
      vk_outarray_append_typed(VkTimeDomainEXT, &out, i) {
         *i = lvp_time_domains[d];
      }
    }

    return vk_outarray_status(&out);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_GetCalibratedTimestampsEXT(
   VkDevice device,
   uint32_t timestampCount,
   const VkCalibratedTimestampInfoEXT *pTimestampInfos,
   uint64_t *pTimestamps,
   uint64_t *pMaxDeviation)
{
   *pMaxDeviation = 1;

   uint64_t now = os_time_get_nano();
   for (unsigned i = 0; i < timestampCount; i++) {
      pTimestamps[i] = now;
   }
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetDeviceGroupPeerMemoryFeaturesKHR(
    VkDevice device,
    uint32_t heapIndex,
    uint32_t localDeviceIndex,
    uint32_t remoteDeviceIndex,
    VkPeerMemoryFeatureFlags *pPeerMemoryFeatures)
{
   *pPeerMemoryFeatures = 0;
}
