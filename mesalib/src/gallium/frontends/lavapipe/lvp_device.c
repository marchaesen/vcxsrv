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
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "frontend/drisw_api.h"

#include "compiler/glsl_types.h"
#include "util/u_inlines.h"
#include "util/os_memory.h"
#include "util/u_thread.h"
#include "util/u_atomic.h"
#include "util/timespec.h"
#include "os_time.h"

static VkResult
lvp_physical_device_init(struct lvp_physical_device *device,
                         struct lvp_instance *instance,
                         struct pipe_loader_device *pld)
{
   VkResult result;
   device->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
   device->instance = instance;
   device->pld = pld;

   device->pscreen = pipe_loader_create_screen(device->pld);
   if (!device->pscreen)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   fprintf(stderr, "WARNING: lavapipe is not a conformant vulkan implementation, testing use only.\n");

   device->max_images = device->pscreen->get_shader_param(device->pscreen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_MAX_SHADER_IMAGES);
   lvp_physical_device_get_supported_extensions(device, &device->supported_extensions);
   result = lvp_init_wsi(device);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto fail;
   }

   return VK_SUCCESS;
 fail:
   return result;
}

static void
lvp_physical_device_finish(struct lvp_physical_device *device)
{
   lvp_finish_wsi(device);
   device->pscreen->destroy(device->pscreen);
}

static void *
default_alloc_func(void *pUserData, size_t size, size_t align,
                   VkSystemAllocationScope allocationScope)
{
   return os_malloc_aligned(size, align);
}

static void *
default_realloc_func(void *pUserData, void *pOriginal, size_t size,
                     size_t align, VkSystemAllocationScope allocationScope)
{
   return realloc(pOriginal, size);
}

static void
default_free_func(void *pUserData, void *pMemory)
{
   os_free_aligned(pMemory);
}

static const VkAllocationCallbacks default_alloc = {
   .pUserData = NULL,
   .pfnAllocation = default_alloc_func,
   .pfnReallocation = default_realloc_func,
   .pfnFree = default_free_func,
};

VkResult lvp_CreateInstance(
   const VkInstanceCreateInfo*                 pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkInstance*                                 pInstance)
{
   struct lvp_instance *instance;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   uint32_t client_version;
   if (pCreateInfo->pApplicationInfo &&
       pCreateInfo->pApplicationInfo->apiVersion != 0) {
      client_version = pCreateInfo->pApplicationInfo->apiVersion;
   } else {
      client_version = VK_API_VERSION_1_0;
   }

   instance = vk_zalloc2(&default_alloc, pAllocator, sizeof(*instance), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(NULL, &instance->base, VK_OBJECT_TYPE_INSTANCE);

   if (pAllocator)
      instance->alloc = *pAllocator;
   else
      instance->alloc = default_alloc;

   instance->apiVersion = client_version;
   instance->physicalDeviceCount = -1;

   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < LVP_INSTANCE_EXTENSION_COUNT; idx++) {
         if (!strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                     lvp_instance_extensions[idx].extensionName))
            break;
      }

      if (idx >= LVP_INSTANCE_EXTENSION_COUNT ||
          !lvp_instance_extensions_supported.extensions[idx]) {
         vk_free2(&default_alloc, pAllocator, instance);
         return vk_error(instance, VK_ERROR_EXTENSION_NOT_PRESENT);
      }
      instance->enabled_extensions.extensions[idx] = true;
   }

   bool unchecked = instance->debug_flags & LVP_DEBUG_ALL_ENTRYPOINTS;
   for (unsigned i = 0; i < ARRAY_SIZE(instance->dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have
       * not been enabled must not be advertised.
       */
      if (!unchecked &&
          !lvp_instance_entrypoint_is_enabled(i, instance->apiVersion,
                                              &instance->enabled_extensions)) {
         instance->dispatch.entrypoints[i] = NULL;
      } else {
         instance->dispatch.entrypoints[i] =
            lvp_instance_dispatch_table.entrypoints[i];
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(instance->physical_device_dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have
       * not been enabled must not be advertised.
       */
      if (!unchecked &&
          !lvp_physical_device_entrypoint_is_enabled(i, instance->apiVersion,
                                                     &instance->enabled_extensions)) {
         instance->physical_device_dispatch.entrypoints[i] = NULL;
      } else {
         instance->physical_device_dispatch.entrypoints[i] =
            lvp_physical_device_dispatch_table.entrypoints[i];
      }
   }

   for (unsigned i = 0; i < ARRAY_SIZE(instance->device_dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have
       * not been enabled must not be advertised.
       */
      if (!unchecked &&
          !lvp_device_entrypoint_is_enabled(i, instance->apiVersion,
                                            &instance->enabled_extensions, NULL)) {
         instance->device_dispatch.entrypoints[i] = NULL;
      } else {
         instance->device_dispatch.entrypoints[i] =
            lvp_device_dispatch_table.entrypoints[i];
      }
   }

   //   _mesa_locale_init();
   glsl_type_singleton_init_or_ref();
   //   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   *pInstance = lvp_instance_to_handle(instance);

   return VK_SUCCESS;
}

void lvp_DestroyInstance(
   VkInstance                                  _instance,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);

   if (!instance)
      return;
   glsl_type_singleton_decref();
   if (instance->physicalDeviceCount > 0)
      lvp_physical_device_finish(&instance->physicalDevice);
   //   _mesa_locale_fini();

   pipe_loader_release(&instance->devs, instance->num_devices);

   vk_object_base_finish(&instance->base);
   vk_free(&instance->alloc, instance);
}

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

VkResult lvp_EnumeratePhysicalDevices(
   VkInstance                                  _instance,
   uint32_t*                                   pPhysicalDeviceCount,
   VkPhysicalDevice*                           pPhysicalDevices)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);
   VkResult result;

   if (instance->physicalDeviceCount < 0) {

      /* sw only for now */
      instance->num_devices = pipe_loader_sw_probe(NULL, 0);

      assert(instance->num_devices == 1);

      pipe_loader_sw_probe_dri(&instance->devs, &lvp_sw_lf);


      result = lvp_physical_device_init(&instance->physicalDevice,
                                        instance, &instance->devs[0]);
      if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
         instance->physicalDeviceCount = 0;
      } else if (result == VK_SUCCESS) {
         instance->physicalDeviceCount = 1;
      } else {
         return result;
      }
   }

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

void lvp_GetPhysicalDeviceFeatures(
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
      .wideLines                                = false,
      .largePoints                              = true,
      .alphaToOne                               = true,
      .multiViewport                            = true,
      .samplerAnisotropy                        = false, /* FINISHME */
      .textureCompressionETC2                   = false,
      .textureCompressionASTC_LDR               = false,
      .textureCompressionBC                     = true,
      .occlusionQueryPrecise                    = true,
      .pipelineStatisticsQuery                  = true,
      .vertexPipelineStoresAndAtomics           = (pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_VERTEX, PIPE_SHADER_CAP_MAX_SHADER_BUFFERS) != 0),
      .fragmentStoresAndAtomics                 = (pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_MAX_SHADER_BUFFERS) != 0),
      .shaderTessellationAndGeometryPointSize   = true,
      .shaderImageGatherExtended                = true,
      .shaderStorageImageExtendedFormats        = false,
      .shaderStorageImageMultisample            = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_TEXTURE_MULTISAMPLE) != 0),
      .shaderUniformBufferArrayDynamicIndexing  = indirect,
      .shaderSampledImageArrayDynamicIndexing   = indirect,
      .shaderStorageBufferArrayDynamicIndexing  = indirect,
      .shaderStorageImageArrayDynamicIndexing   = indirect,
      .shaderStorageImageReadWithoutFormat      = false,
      .shaderStorageImageWriteWithoutFormat     = true,
      .shaderClipDistance                       = true,
      .shaderCullDistance                       = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_CULL_DISTANCE) == 1),
      .shaderFloat64                            = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_DOUBLES) == 1),
      .shaderInt64                              = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_INT64) == 1),
      .shaderInt16                              = true,
      .alphaToOne                               = true,
      .variableMultisampleRate                  = false,
      .inheritedQueries                         = false,
   };
}

void lvp_GetPhysicalDeviceFeatures2(
   VkPhysicalDevice                            physicalDevice,
   VkPhysicalDeviceFeatures2                  *pFeatures)
{
   LVP_FROM_HANDLE(lvp_physical_device, pdevice, physicalDevice);
   lvp_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);

   vk_foreach_struct(ext, pFeatures->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES: {
         VkPhysicalDeviceVariablePointersFeatures *features = (void *)ext;
         features->variablePointers = true;
         features->variablePointersStorageBuffer = true;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES: {
         VkPhysicalDevice16BitStorageFeatures *features =
            (VkPhysicalDevice16BitStorageFeatures*)ext;
         features->storageBuffer16BitAccess = true;
         features->uniformAndStorageBuffer16BitAccess = true;
         features->storagePushConstant16 = true;
         features->storageInputOutput16 = false;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES_EXT: {
         VkPhysicalDevicePrivateDataFeaturesEXT *features =
            (VkPhysicalDevicePrivateDataFeaturesEXT *)ext;
         features->privateData = true;
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
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT: {
         VkPhysicalDeviceTransformFeedbackFeaturesEXT *features =
            (VkPhysicalDeviceTransformFeedbackFeaturesEXT*)ext;

         features->transformFeedback = true;
         features->geometryStreams = true;
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
   snprintf(uuid, VK_UUID_SIZE, "val-%s", MESA_GIT_SHA1 + 4);
}

void lvp_GetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice,
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

   VkPhysicalDeviceLimits limits = {
      .maxImageDimension1D                      = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_2D_SIZE),
      .maxImageDimension2D                      = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_2D_SIZE),
      .maxImageDimension3D                      = (1 << pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_3D_LEVELS)),
      .maxImageDimensionCube                    = (1 << pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_CUBE_LEVELS)),
      .maxImageArrayLayers                      = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS),
      .maxTexelBufferElements                   = 128 * 1024 * 1024,
      .maxUniformBufferRange                    = pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_MAX_CONST_BUFFER_SIZE),
      .maxStorageBufferRange                    = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_SHADER_BUFFER_SIZE),
      .maxPushConstantsSize                     = MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount                 = 4096,
      .maxSamplerAllocationCount                = 32 * 1024,
      .bufferImageGranularity                   = 64, /* A cache line */
      .sparseAddressSpaceSize                   = 0,
      .maxBoundDescriptorSets                   = MAX_SETS,
      .maxPerStageDescriptorSamplers            = 32,
      .maxPerStageDescriptorUniformBuffers      = pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_MAX_CONST_BUFFERS),
      .maxPerStageDescriptorStorageBuffers      = pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_MAX_SHADER_BUFFERS),
      .maxPerStageDescriptorSampledImages       = pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_MAX_SAMPLER_VIEWS),
      .maxPerStageDescriptorStorageImages       = pdevice->pscreen->get_shader_param(pdevice->pscreen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_MAX_SHADER_IMAGES - 8),
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
      .maxFragmentCombinedOutputResources       = 8,
      .maxComputeSharedMemorySize               = max_local_size,
      .maxComputeWorkGroupCount                 = { grid_size[0], grid_size[1], grid_size[2] },
      .maxComputeWorkGroupInvocations           = max_threads_per_block,
      .maxComputeWorkGroupSize = { block_size[0], block_size[1], block_size[2] },
      .subPixelPrecisionBits                    = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_RASTERIZER_SUBPIXEL_BITS),
      .subTexelPrecisionBits                    = 8,
      .mipmapPrecisionBits                      = 8,
      .maxDrawIndexedIndexValue                 = UINT32_MAX,
      .maxDrawIndirectCount                     = UINT32_MAX,
      .maxSamplerLodBias                        = 16,
      .maxSamplerAnisotropy                     = 16,
      .maxViewports                             = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_VIEWPORTS),
      .maxViewportDimensions                    = { (1 << 14), (1 << 14) },
      .viewportBoundsRange                      = { -32768.0, 32768.0 },
      .viewportSubPixelBits                     = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_VIEWPORT_SUBPIXEL_BITS),
      .minMemoryMapAlignment                    = 4096, /* A page */
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
      .maxColorAttachments                      = pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_MAX_RENDER_TARGETS),
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
      .pointSizeRange                           = { 0.0, pdevice->pscreen->get_paramf(pdevice->pscreen, PIPE_CAPF_MAX_POINT_WIDTH) },
      .lineWidthRange                           = { 1.0, 1.0 },
      .pointSizeGranularity                     = (1.0 / 8.0),
      .lineWidthGranularity                     = 0.0,
      .strictLines                              = false, /* FINISHME */
      .standardSampleLocations                  = true,
      .optimalBufferCopyOffsetAlignment         = 128,
      .optimalBufferCopyRowPitchAlignment       = 128,
      .nonCoherentAtomSize                      = 64,
   };

   *pProperties = (VkPhysicalDeviceProperties) {
      .apiVersion = VK_MAKE_VERSION(1, 0, 2),
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

void lvp_GetPhysicalDeviceProperties2(
   VkPhysicalDevice                            physicalDevice,
   VkPhysicalDeviceProperties2                *pProperties)
{
   LVP_FROM_HANDLE(lvp_physical_device, pdevice, physicalDevice);
   lvp_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);

   vk_foreach_struct(ext, pProperties->pNext) {
      switch (ext->sType) {

      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR: {
         VkPhysicalDevicePushDescriptorPropertiesKHR *properties =
            (VkPhysicalDevicePushDescriptorPropertiesKHR *) ext;
         properties->maxPushDescriptors = MAX_PUSH_DESCRIPTORS;
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES: {
         VkPhysicalDeviceMaintenance3Properties *properties =
            (VkPhysicalDeviceMaintenance3Properties*)ext;
         properties->maxPerSetDescriptors = 1024;
         properties->maxMemoryAllocationSize = (1u << 31);
         break;
      }
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR: {
         VkPhysicalDeviceDriverPropertiesKHR *driver_props =
            (VkPhysicalDeviceDriverPropertiesKHR *) ext;
         driver_props->driverID = VK_DRIVER_ID_MESA_LLVMPIPE;
         snprintf(driver_props->driverName, VK_MAX_DRIVER_NAME_SIZE_KHR, "llvmpipe");
         snprintf(driver_props->driverInfo, VK_MAX_DRIVER_INFO_SIZE_KHR,
                  "Mesa " PACKAGE_VERSION MESA_GIT_SHA1
#ifdef MESA_LLVM_VERSION_STRING
                  " (LLVM " MESA_LLVM_VERSION_STRING ")"
#endif
                 );
         driver_props->conformanceVersion.major = 1;
         driver_props->conformanceVersion.minor = 0;
         driver_props->conformanceVersion.subminor = 0;
         driver_props->conformanceVersion.patch = 0;;
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
      }
      default:
         break;
      }
   }
}

void lvp_GetPhysicalDeviceQueueFamilyProperties(
   VkPhysicalDevice                            physicalDevice,
   uint32_t*                                   pCount,
   VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
   if (pQueueFamilyProperties == NULL) {
      *pCount = 1;
      return;
   }

   assert(*pCount >= 1);

   *pQueueFamilyProperties = (VkQueueFamilyProperties) {
      .queueFlags = VK_QUEUE_GRAPHICS_BIT |
      VK_QUEUE_COMPUTE_BIT |
      VK_QUEUE_TRANSFER_BIT,
      .queueCount = 1,
      .timestampValidBits = 64,
      .minImageTransferGranularity = (VkExtent3D) { 1, 1, 1 },
   };
}

void lvp_GetPhysicalDeviceMemoryProperties(
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

PFN_vkVoidFunction lvp_GetInstanceProcAddr(
   VkInstance                                  _instance,
   const char*                                 pName)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);

   /* The Vulkan 1.0 spec for vkGetInstanceProcAddr has a table of exactly
    * when we have to return valid function pointers, NULL, or it's left
    * undefined.  See the table for exact details.
    */
   if (pName == NULL)
      return NULL;

#define LOOKUP_LVP_ENTRYPOINT(entrypoint)               \
   if (strcmp(pName, "vk" #entrypoint) == 0)            \
      return (PFN_vkVoidFunction)lvp_##entrypoint

   LOOKUP_LVP_ENTRYPOINT(EnumerateInstanceExtensionProperties);
   LOOKUP_LVP_ENTRYPOINT(EnumerateInstanceLayerProperties);
   LOOKUP_LVP_ENTRYPOINT(EnumerateInstanceVersion);
   LOOKUP_LVP_ENTRYPOINT(CreateInstance);

   /* GetInstanceProcAddr() can also be called with a NULL instance.
    * See https://gitlab.khronos.org/vulkan/vulkan/issues/2057
    */
   LOOKUP_LVP_ENTRYPOINT(GetInstanceProcAddr);

#undef LOOKUP_LVP_ENTRYPOINT

   if (instance == NULL)
      return NULL;

   int idx = lvp_get_instance_entrypoint_index(pName);
   if (idx >= 0)
      return instance->dispatch.entrypoints[idx];

   idx = lvp_get_physical_device_entrypoint_index(pName);
   if (idx >= 0)
      return instance->physical_device_dispatch.entrypoints[idx];

   idx = lvp_get_device_entrypoint_index(pName);
   if (idx >= 0)
      return instance->device_dispatch.entrypoints[idx];

   return NULL;
}

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

   if (!pName || !instance)
      return NULL;

   int idx = lvp_get_physical_device_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   return instance->physical_device_dispatch.entrypoints[idx];
}

PFN_vkVoidFunction lvp_GetDeviceProcAddr(
   VkDevice                                    _device,
   const char*                                 pName)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   if (!device || !pName)
      return NULL;

   int idx = lvp_get_device_entrypoint_index(pName);
   if (idx < 0)
      return NULL;

   return device->dispatch.entrypoints[idx];
}

static int queue_thread(void *data)
{
   struct lvp_queue *queue = data;

   mtx_lock(&queue->m);
   while (!queue->shutdown) {
      struct lvp_queue_work *task;
      while (list_is_empty(&queue->workqueue) && !queue->shutdown)
         cnd_wait(&queue->new_work, &queue->m);

      if (queue->shutdown)
         break;

      task = list_first_entry(&queue->workqueue, struct lvp_queue_work,
                              list);

      mtx_unlock(&queue->m);
      //execute
      for (unsigned i = 0; i < task->cmd_buffer_count; i++) {
         lvp_execute_cmds(queue->device, queue, task->fence, task->cmd_buffers[i]);
      }
      if (!task->cmd_buffer_count && task->fence)
         task->fence->signaled = true;
      p_atomic_dec(&queue->count);
      mtx_lock(&queue->m);
      list_del(&task->list);
      free(task);
   }
   mtx_unlock(&queue->m);
   return 0;
}

static VkResult
lvp_queue_init(struct lvp_device *device, struct lvp_queue *queue)
{
   queue->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
   queue->device = device;

   queue->flags = 0;
   queue->ctx = device->pscreen->context_create(device->pscreen, NULL, PIPE_CONTEXT_ROBUST_BUFFER_ACCESS);
   list_inithead(&queue->workqueue);
   p_atomic_set(&queue->count, 0);
   mtx_init(&queue->m, mtx_plain);
   queue->exec_thread = u_thread_create(queue_thread, queue);

   return VK_SUCCESS;
}

static void
lvp_queue_finish(struct lvp_queue *queue)
{
   mtx_lock(&queue->m);
   queue->shutdown = true;
   cnd_broadcast(&queue->new_work);
   mtx_unlock(&queue->m);

   thrd_join(queue->exec_thread, NULL);

   cnd_destroy(&queue->new_work);
   mtx_destroy(&queue->m);
   queue->ctx->destroy(queue->ctx);
}

static int lvp_get_device_extension_index(const char *name)
{
   for (unsigned i = 0; i < LVP_DEVICE_EXTENSION_COUNT; ++i) {
      if (strcmp(name, lvp_device_extensions[i].extensionName) == 0)
         return i;
   }
   return -1;
}

static void
lvp_device_init_dispatch(struct lvp_device *device)
{
   const struct lvp_instance *instance = device->physical_device->instance;
   const struct lvp_device_dispatch_table *dispatch_table_layer = NULL;
   bool unchecked = instance->debug_flags & LVP_DEBUG_ALL_ENTRYPOINTS;

   for (unsigned i = 0; i < ARRAY_SIZE(device->dispatch.entrypoints); i++) {
      /* Vulkan requires that entrypoints for extensions which have not been
       * enabled must not be advertised.
       */
      if (!unchecked &&
          !lvp_device_entrypoint_is_enabled(i, instance->apiVersion,
                                            &instance->enabled_extensions,
                                            &device->enabled_extensions)) {
         device->dispatch.entrypoints[i] = NULL;
      } else if (dispatch_table_layer &&
                 dispatch_table_layer->entrypoints[i]) {
         device->dispatch.entrypoints[i] =
            dispatch_table_layer->entrypoints[i];
      } else {
         device->dispatch.entrypoints[i] =
            lvp_device_dispatch_table.entrypoints[i];
      }
   }
}

VkResult lvp_CreateDevice(
   VkPhysicalDevice                            physicalDevice,
   const VkDeviceCreateInfo*                   pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkDevice*                                   pDevice)
{
   LVP_FROM_HANDLE(lvp_physical_device, physical_device, physicalDevice);
   struct lvp_device *device;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);

   /* Check enabled features */
   if (pCreateInfo->pEnabledFeatures) {
      VkPhysicalDeviceFeatures supported_features;
      lvp_GetPhysicalDeviceFeatures(physicalDevice, &supported_features);
      VkBool32 *supported_feature = (VkBool32 *)&supported_features;
      VkBool32 *enabled_feature = (VkBool32 *)pCreateInfo->pEnabledFeatures;
      unsigned num_features = sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32);
      for (uint32_t i = 0; i < num_features; i++) {
         if (enabled_feature[i] && !supported_feature[i])
            return vk_error(physical_device->instance, VK_ERROR_FEATURE_NOT_PRESENT);
      }
   }

   device = vk_zalloc2(&physical_device->instance->alloc, pAllocator,
                       sizeof(*device), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(physical_device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_device_init(&device->vk, pCreateInfo,
                  &physical_device->instance->alloc, pAllocator);

   device->instance = physical_device->instance;
   device->physical_device = physical_device;

   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      const char *ext_name = pCreateInfo->ppEnabledExtensionNames[i];
      int index = lvp_get_device_extension_index(ext_name);
      if (index < 0 || !physical_device->supported_extensions.extensions[index]) {
         vk_free(&device->vk.alloc, device);
         return vk_error(physical_device->instance, VK_ERROR_EXTENSION_NOT_PRESENT);
      }

      device->enabled_extensions.extensions[index] = true;
   }
   lvp_device_init_dispatch(device);

   mtx_init(&device->fence_lock, mtx_plain);
   device->pscreen = physical_device->pscreen;

   lvp_queue_init(device, &device->queue);

   *pDevice = lvp_device_to_handle(device);

   return VK_SUCCESS;

}

void lvp_DestroyDevice(
   VkDevice                                    _device,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);

   lvp_queue_finish(&device->queue);
   vk_free(&device->vk.alloc, device);
}

VkResult lvp_EnumerateInstanceExtensionProperties(
   const char*                                 pLayerName,
   uint32_t*                                   pPropertyCount,
   VkExtensionProperties*                      pProperties)
{
   VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

   for (int i = 0; i < LVP_INSTANCE_EXTENSION_COUNT; i++) {
      if (lvp_instance_extensions_supported.extensions[i]) {
         vk_outarray_append(&out, prop) {
            *prop = lvp_instance_extensions[i];
         }
      }
   }

   return vk_outarray_status(&out);
}

VkResult lvp_EnumerateDeviceExtensionProperties(
   VkPhysicalDevice                            physicalDevice,
   const char*                                 pLayerName,
   uint32_t*                                   pPropertyCount,
   VkExtensionProperties*                      pProperties)
{
   LVP_FROM_HANDLE(lvp_physical_device, device, physicalDevice);
   VK_OUTARRAY_MAKE(out, pProperties, pPropertyCount);

   for (int i = 0; i < LVP_DEVICE_EXTENSION_COUNT; i++) {
      if (device->supported_extensions.extensions[i]) {
         vk_outarray_append(&out, prop) {
            *prop = lvp_device_extensions[i];
         }
      }
   }
   return vk_outarray_status(&out);
}

VkResult lvp_EnumerateInstanceLayerProperties(
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

VkResult lvp_EnumerateDeviceLayerProperties(
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

void lvp_GetDeviceQueue2(
   VkDevice                                    _device,
   const VkDeviceQueueInfo2*                   pQueueInfo,
   VkQueue*                                    pQueue)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_queue *queue;

   queue = &device->queue;
   if (pQueueInfo->flags != queue->flags) {
      /* From the Vulkan 1.1.70 spec:
       *
       * "The queue returned by vkGetDeviceQueue2 must have the same
       * flags value from this structure as that used at device
       * creation time in a VkDeviceQueueCreateInfo instance. If no
       * matching flags were specified at device creation time then
       * pQueue will return VK_NULL_HANDLE."
       */
      *pQueue = VK_NULL_HANDLE;
      return;
   }

   *pQueue = lvp_queue_to_handle(queue);
}


void lvp_GetDeviceQueue(
   VkDevice                                    _device,
   uint32_t                                    queueFamilyIndex,
   uint32_t                                    queueIndex,
   VkQueue*                                    pQueue)
{
   const VkDeviceQueueInfo2 info = (VkDeviceQueueInfo2) {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
      .queueFamilyIndex = queueFamilyIndex,
      .queueIndex = queueIndex
   };

   lvp_GetDeviceQueue2(_device, &info, pQueue);
}


VkResult lvp_QueueSubmit(
   VkQueue                                     _queue,
   uint32_t                                    submitCount,
   const VkSubmitInfo*                         pSubmits,
   VkFence                                     _fence)
{
   LVP_FROM_HANDLE(lvp_queue, queue, _queue);
   LVP_FROM_HANDLE(lvp_fence, fence, _fence);

   if (submitCount == 0)
      goto just_signal_fence;
   for (uint32_t i = 0; i < submitCount; i++) {
      uint32_t task_size = sizeof(struct lvp_queue_work) + pSubmits[i].commandBufferCount * sizeof(struct lvp_cmd_buffer *);
      struct lvp_queue_work *task = malloc(task_size);

      task->cmd_buffer_count = pSubmits[i].commandBufferCount;
      task->fence = fence;
      task->cmd_buffers = (struct lvp_cmd_buffer **)(task + 1);
      for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; j++) {
         task->cmd_buffers[j] = lvp_cmd_buffer_from_handle(pSubmits[i].pCommandBuffers[j]);
      }

      mtx_lock(&queue->m);
      p_atomic_inc(&queue->count);
      list_addtail(&task->list, &queue->workqueue);
      cnd_signal(&queue->new_work);
      mtx_unlock(&queue->m);
   }
   return VK_SUCCESS;
 just_signal_fence:
   fence->signaled = true;
   return VK_SUCCESS;
}

static VkResult queue_wait_idle(struct lvp_queue *queue, uint64_t timeout)
{
   if (timeout == 0)
      return p_atomic_read(&queue->count) == 0 ? VK_SUCCESS : VK_TIMEOUT;
   if (timeout == UINT64_MAX)
      while (p_atomic_read(&queue->count))
         os_time_sleep(100);
   else {
      struct timespec t, current;
      clock_gettime(CLOCK_MONOTONIC, &current);
      timespec_add_nsec(&t, &current, timeout);
      bool timedout = false;
      while (p_atomic_read(&queue->count) && !(timedout = timespec_passed(CLOCK_MONOTONIC, &t)))
         os_time_sleep(10);
      if (timedout)
         return VK_TIMEOUT;
   }
   return VK_SUCCESS;
}

VkResult lvp_QueueWaitIdle(
   VkQueue                                     _queue)
{
   LVP_FROM_HANDLE(lvp_queue, queue, _queue);

   return queue_wait_idle(queue, UINT64_MAX);
}

VkResult lvp_DeviceWaitIdle(
   VkDevice                                    _device)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);

   return queue_wait_idle(&device->queue, UINT64_MAX);
}

VkResult lvp_AllocateMemory(
   VkDevice                                    _device,
   const VkMemoryAllocateInfo*                 pAllocateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkDeviceMemory*                             pMem)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_device_memory *mem;
   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

   if (pAllocateInfo->allocationSize == 0) {
      /* Apparently, this is allowed */
      *pMem = VK_NULL_HANDLE;
      return VK_SUCCESS;
   }

   mem = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*mem), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (mem == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &mem->base,
                       VK_OBJECT_TYPE_DEVICE_MEMORY);
   mem->pmem = device->pscreen->allocate_memory(device->pscreen, pAllocateInfo->allocationSize);
   if (!mem->pmem) {
      vk_free2(&device->vk.alloc, pAllocator, mem);
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   mem->type_index = pAllocateInfo->memoryTypeIndex;

   *pMem = lvp_device_memory_to_handle(mem);

   return VK_SUCCESS;
}

void lvp_FreeMemory(
   VkDevice                                    _device,
   VkDeviceMemory                              _mem,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_device_memory, mem, _mem);

   if (mem == NULL)
      return;

   device->pscreen->free_memory(device->pscreen, mem->pmem);
   vk_object_base_finish(&mem->base);
   vk_free2(&device->vk.alloc, pAllocator, mem);

}

VkResult lvp_MapMemory(
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

   *ppData = map + offset;
   return VK_SUCCESS;
}

void lvp_UnmapMemory(
   VkDevice                                    _device,
   VkDeviceMemory                              _memory)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_device_memory, mem, _memory);

   if (mem == NULL)
      return;

   device->pscreen->unmap_memory(device->pscreen, mem->pmem);
}

VkResult lvp_FlushMappedMemoryRanges(
   VkDevice                                    _device,
   uint32_t                                    memoryRangeCount,
   const VkMappedMemoryRange*                  pMemoryRanges)
{
   return VK_SUCCESS;
}
VkResult lvp_InvalidateMappedMemoryRanges(
   VkDevice                                    _device,
   uint32_t                                    memoryRangeCount,
   const VkMappedMemoryRange*                  pMemoryRanges)
{
   return VK_SUCCESS;
}

void lvp_GetBufferMemoryRequirements(
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

void lvp_GetBufferMemoryRequirements2(
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

void lvp_GetImageMemoryRequirements(
   VkDevice                                    device,
   VkImage                                     _image,
   VkMemoryRequirements*                       pMemoryRequirements)
{
   LVP_FROM_HANDLE(lvp_image, image, _image);
   pMemoryRequirements->memoryTypeBits = 1;

   pMemoryRequirements->size = image->size;
   pMemoryRequirements->alignment = image->alignment;
}

void lvp_GetImageMemoryRequirements2(
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

void lvp_GetImageSparseMemoryRequirements(
   VkDevice                                    device,
   VkImage                                     image,
   uint32_t*                                   pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements*            pSparseMemoryRequirements)
{
   stub();
}

void lvp_GetImageSparseMemoryRequirements2(
   VkDevice                                    device,
   const VkImageSparseMemoryRequirementsInfo2* pInfo,
   uint32_t* pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2* pSparseMemoryRequirements)
{
   stub();
}

void lvp_GetDeviceMemoryCommitment(
   VkDevice                                    device,
   VkDeviceMemory                              memory,
   VkDeviceSize*                               pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

VkResult lvp_BindBufferMemory2(VkDevice _device,
                               uint32_t bindInfoCount,
                               const VkBindBufferMemoryInfo *pBindInfos)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      LVP_FROM_HANDLE(lvp_device_memory, mem, pBindInfos[i].memory);
      LVP_FROM_HANDLE(lvp_buffer, buffer, pBindInfos[i].buffer);

      device->pscreen->resource_bind_backing(device->pscreen,
                                             buffer->bo,
                                             mem->pmem,
                                             pBindInfos[i].memoryOffset);
   }
   return VK_SUCCESS;
}

VkResult lvp_BindBufferMemory(
   VkDevice                                    _device,
   VkBuffer                                    _buffer,
   VkDeviceMemory                              _memory,
   VkDeviceSize                                memoryOffset)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_device_memory, mem, _memory);
   LVP_FROM_HANDLE(lvp_buffer, buffer, _buffer);

   device->pscreen->resource_bind_backing(device->pscreen,
                                          buffer->bo,
                                          mem->pmem,
                                          memoryOffset);
   return VK_SUCCESS;
}

VkResult lvp_BindImageMemory2(VkDevice _device,
                              uint32_t bindInfoCount,
                              const VkBindImageMemoryInfo *pBindInfos)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      LVP_FROM_HANDLE(lvp_device_memory, mem, pBindInfos[i].memory);
      LVP_FROM_HANDLE(lvp_image, image, pBindInfos[i].image);

      device->pscreen->resource_bind_backing(device->pscreen,
                                             image->bo,
                                             mem->pmem,
                                             pBindInfos[i].memoryOffset);
   }
   return VK_SUCCESS;
}

VkResult lvp_BindImageMemory(
   VkDevice                                    _device,
   VkImage                                     _image,
   VkDeviceMemory                              _memory,
   VkDeviceSize                                memoryOffset)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_device_memory, mem, _memory);
   LVP_FROM_HANDLE(lvp_image, image, _image);

   device->pscreen->resource_bind_backing(device->pscreen,
                                          image->bo,
                                          mem->pmem,
                                          memoryOffset);
   return VK_SUCCESS;
}

VkResult lvp_QueueBindSparse(
   VkQueue                                     queue,
   uint32_t                                    bindInfoCount,
   const VkBindSparseInfo*                     pBindInfo,
   VkFence                                     fence)
{
   stub_return(VK_ERROR_INCOMPATIBLE_DRIVER);
}


VkResult lvp_CreateFence(
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &fence->base, VK_OBJECT_TYPE_FENCE);
   fence->signaled = pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT;

   fence->handle = NULL;
   *pFence = lvp_fence_to_handle(fence);

   return VK_SUCCESS;
}

void lvp_DestroyFence(
   VkDevice                                    _device,
   VkFence                                     _fence,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_fence, fence, _fence);

   if (!_fence)
      return;
   if (fence->handle)
      device->pscreen->fence_reference(device->pscreen, &fence->handle, NULL);

   vk_object_base_finish(&fence->base);
   vk_free2(&device->vk.alloc, pAllocator, fence);
}

VkResult lvp_ResetFences(
   VkDevice                                    _device,
   uint32_t                                    fenceCount,
   const VkFence*                              pFences)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   for (unsigned i = 0; i < fenceCount; i++) {
      struct lvp_fence *fence = lvp_fence_from_handle(pFences[i]);

      fence->signaled = false;

      mtx_lock(&device->fence_lock);
      if (fence->handle)
         device->pscreen->fence_reference(device->pscreen, &fence->handle, NULL);
      mtx_unlock(&device->fence_lock);
   }
   return VK_SUCCESS;
}

VkResult lvp_GetFenceStatus(
   VkDevice                                    _device,
   VkFence                                     _fence)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_fence, fence, _fence);

   if (fence->signaled)
      return VK_SUCCESS;

   mtx_lock(&device->fence_lock);

   if (!fence->handle) {
      mtx_unlock(&device->fence_lock);
      return VK_NOT_READY;
   }

   bool signalled = device->pscreen->fence_finish(device->pscreen,
                                                  NULL,
                                                  fence->handle,
                                                  0);
   mtx_unlock(&device->fence_lock);
   if (signalled)
      return VK_SUCCESS;
   else
      return VK_NOT_READY;
}

VkResult lvp_CreateFramebuffer(
   VkDevice                                    _device,
   const VkFramebufferCreateInfo*              pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkFramebuffer*                              pFramebuffer)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_framebuffer *framebuffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

   size_t size = sizeof(*framebuffer) +
      sizeof(struct lvp_image_view *) * pCreateInfo->attachmentCount;
   framebuffer = vk_alloc2(&device->vk.alloc, pAllocator, size, 8,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (framebuffer == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &framebuffer->base,
                       VK_OBJECT_TYPE_FRAMEBUFFER);
   framebuffer->attachment_count = pCreateInfo->attachmentCount;
   for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
      VkImageView _iview = pCreateInfo->pAttachments[i];
      framebuffer->attachments[i] = lvp_image_view_from_handle(_iview);
   }

   framebuffer->width = pCreateInfo->width;
   framebuffer->height = pCreateInfo->height;
   framebuffer->layers = pCreateInfo->layers;

   *pFramebuffer = lvp_framebuffer_to_handle(framebuffer);

   return VK_SUCCESS;
}

void lvp_DestroyFramebuffer(
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

VkResult lvp_WaitForFences(
   VkDevice                                    _device,
   uint32_t                                    fenceCount,
   const VkFence*                              pFences,
   VkBool32                                    waitAll,
   uint64_t                                    timeout)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);

   VkResult qret = queue_wait_idle(&device->queue, timeout);
   bool timeout_status = false;
   if (qret == VK_TIMEOUT)
      return VK_TIMEOUT;

   mtx_lock(&device->fence_lock);
   for (unsigned i = 0; i < fenceCount; i++) {
      struct lvp_fence *fence = lvp_fence_from_handle(pFences[i]);

      if (fence->signaled)
         continue;
      if (!fence->handle) {
         timeout_status |= true;
         continue;
      }
      bool ret = device->pscreen->fence_finish(device->pscreen,
                                               NULL,
                                               fence->handle,
                                               timeout);
      if (ret && !waitAll) {
         timeout_status = false;
         break;
      }

      if (!ret)
         timeout_status |= true;
   }
   mtx_unlock(&device->fence_lock);
   return timeout_status ? VK_TIMEOUT : VK_SUCCESS;
}

VkResult lvp_CreateSemaphore(
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   vk_object_base_init(&device->vk, &sema->base,
                       VK_OBJECT_TYPE_SEMAPHORE);
   *pSemaphore = lvp_semaphore_to_handle(sema);

   return VK_SUCCESS;
}

void lvp_DestroySemaphore(
   VkDevice                                    _device,
   VkSemaphore                                 _semaphore,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_semaphore, semaphore, _semaphore);

   if (!_semaphore)
      return;
   vk_object_base_finish(&semaphore->base);
   vk_free2(&device->vk.alloc, pAllocator, semaphore);
}

VkResult lvp_CreateEvent(
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &event->base, VK_OBJECT_TYPE_EVENT);
   *pEvent = lvp_event_to_handle(event);

   return VK_SUCCESS;
}

void lvp_DestroyEvent(
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

VkResult lvp_GetEventStatus(
   VkDevice                                    _device,
   VkEvent                                     _event)
{
   LVP_FROM_HANDLE(lvp_event, event, _event);
   if (event->event_storage == 1)
      return VK_EVENT_SET;
   return VK_EVENT_RESET;
}

VkResult lvp_SetEvent(
   VkDevice                                    _device,
   VkEvent                                     _event)
{
   LVP_FROM_HANDLE(lvp_event, event, _event);
   event->event_storage = 1;

   return VK_SUCCESS;
}

VkResult lvp_ResetEvent(
   VkDevice                                    _device,
   VkEvent                                     _event)
{
   LVP_FROM_HANDLE(lvp_event, event, _event);
   event->event_storage = 0;

   return VK_SUCCESS;
}

VkResult lvp_CreateSampler(
   VkDevice                                    _device,
   const VkSamplerCreateInfo*                  pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkSampler*                                  pSampler)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_sampler *sampler;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   sampler = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*sampler), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sampler)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &sampler->base,
                       VK_OBJECT_TYPE_SAMPLER);
   sampler->create_info = *pCreateInfo;
   *pSampler = lvp_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

void lvp_DestroySampler(
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

VkResult lvp_CreatePrivateDataSlotEXT(
   VkDevice                                    _device,
   const VkPrivateDataSlotCreateInfoEXT*       pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkPrivateDataSlotEXT*                       pPrivateDataSlot)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   return vk_private_data_slot_create(&device->vk, pCreateInfo, pAllocator,
                                      pPrivateDataSlot);
}

void lvp_DestroyPrivateDataSlotEXT(
   VkDevice                                    _device,
   VkPrivateDataSlotEXT                        privateDataSlot,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   vk_private_data_slot_destroy(&device->vk, privateDataSlot, pAllocator);
}

VkResult lvp_SetPrivateDataEXT(
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

void lvp_GetPrivateDataEXT(
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
