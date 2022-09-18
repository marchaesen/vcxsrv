/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on v3dv driver which is:
 * Copyright © 2019 Raspberry Pi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <xf86drm.h>

#include "hwdef/rogue_hw_utils.h"
#include "pipe/p_defines.h"
#include "pvr_bo.h"
#include "pvr_csb.h"
#include "pvr_csb_enum_helpers.h"
#include "pvr_debug.h"
#include "pvr_device_info.h"
#include "pvr_hardcode.h"
#include "pvr_job_render.h"
#include "pvr_limits.h"
#include "pvr_nop_usc.h"
#include "pvr_pds.h"
#include "pvr_private.h"
#include "pvr_tex_state.h"
#include "pvr_types.h"
#include "pvr_winsys.h"
#include "rogue/rogue_compiler.h"
#include "util/build_id.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/mesa-sha1.h"
#include "util/os_misc.h"
#include "util/u_math.h"
#include "vk_alloc.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_util.h"

#define PVR_GLOBAL_FREE_LIST_INITIAL_SIZE (2U * 1024U * 1024U)
#define PVR_GLOBAL_FREE_LIST_MAX_SIZE (256U * 1024U * 1024U)
#define PVR_GLOBAL_FREE_LIST_GROW_SIZE (1U * 1024U * 1024U)

/* The grow threshold is a percentage. This is intended to be 12.5%, but has
 * been rounded up since the percentage is treated as an integer.
 */
#define PVR_GLOBAL_FREE_LIST_GROW_THRESHOLD 13U

#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
#   define PVR_USE_WSI_PLATFORM
#endif

#define PVR_API_VERSION VK_MAKE_VERSION(1, 0, VK_HEADER_VERSION)

#define DEF_DRIVER(str_name)                        \
   {                                                \
      .name = str_name, .len = sizeof(str_name) - 1 \
   }

struct pvr_drm_device_info {
   const char *name;
   size_t len;
};

/* This is the list of supported DRM display drivers. */
static const struct pvr_drm_device_info pvr_display_devices[] = {
   DEF_DRIVER("mediatek-drm"),
   DEF_DRIVER("ti,am65x-dss"),
};

/* This is the list of supported DRM render drivers. */
static const struct pvr_drm_device_info pvr_render_devices[] = {
   DEF_DRIVER("mediatek,mt8173-gpu"),
   DEF_DRIVER("ti,am62-gpu"),
};

#undef DEF_DRIVER

static const struct vk_instance_extension_table pvr_instance_extensions = {
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
   .KHR_display = true,
#endif
   .KHR_external_memory_capabilities = true,
   .KHR_get_physical_device_properties2 = true,
#if defined(PVR_USE_WSI_PLATFORM)
   .KHR_surface = true,
#endif
   .EXT_debug_report = true,
   .EXT_debug_utils = true,
};

static void pvr_physical_device_get_supported_extensions(
   const struct pvr_physical_device *pdevice,
   struct vk_device_extension_table *extensions)
{
   /* clang-format off */
   *extensions = (struct vk_device_extension_table){
      .KHR_external_memory = true,
      .KHR_external_memory_fd = true,
#if defined(PVR_USE_WSI_PLATFORM)
      .KHR_swapchain = true,
#endif
      .EXT_external_memory_dma_buf = true,
      .EXT_private_data = true,
   };
   /* clang-format on */
}

VkResult pvr_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
   *pApiVersion = PVR_API_VERSION;
   return VK_SUCCESS;
}

VkResult
pvr_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                         uint32_t *pPropertyCount,
                                         VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(&pvr_instance_extensions,
                                                     pPropertyCount,
                                                     pProperties);
}

VkResult pvr_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkInstance *pInstance)
{
   struct vk_instance_dispatch_table dispatch_table;
   struct pvr_instance *instance;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

   if (!pAllocator)
      pAllocator = vk_default_allocator();

   instance = vk_alloc(pAllocator,
                       sizeof(*instance),
                       8,
                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_instance_dispatch_table_from_entrypoints(&dispatch_table,
                                               &pvr_instance_entrypoints,
                                               true);

   vk_instance_dispatch_table_from_entrypoints(&dispatch_table,
                                               &wsi_instance_entrypoints,
                                               false);

   result = vk_instance_init(&instance->vk,
                             &pvr_instance_extensions,
                             &dispatch_table,
                             pCreateInfo,
                             pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, instance);
      return vk_error(NULL, result);
   }

   pvr_process_debug_variable();

   instance->physical_devices_count = -1;

   VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

   *pInstance = pvr_instance_to_handle(instance);

   return VK_SUCCESS;
}

static void pvr_physical_device_finish(struct pvr_physical_device *pdevice)
{
   /* Be careful here. The device might not have been initialized. This can
    * happen since initialization is done in vkEnumeratePhysicalDevices() but
    * finish is done in vkDestroyInstance(). Make sure that you check for NULL
    * before freeing or that the freeing functions accept NULL pointers.
    */

   if (pdevice->compiler)
      rogue_compiler_destroy(pdevice->compiler);

   pvr_wsi_finish(pdevice);

   free(pdevice->name);

   if (pdevice->ws)
      pvr_winsys_destroy(pdevice->ws);

   if (pdevice->master_fd >= 0) {
      vk_free(&pdevice->vk.instance->alloc, pdevice->master_path);
      close(pdevice->master_fd);
   }

   if (pdevice->render_fd >= 0) {
      vk_free(&pdevice->vk.instance->alloc, pdevice->render_path);
      close(pdevice->render_fd);
   }
   vk_physical_device_finish(&pdevice->vk);
}

void pvr_DestroyInstance(VkInstance _instance,
                         const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_instance, instance, _instance);

   if (!instance)
      return;

   if (instance->physical_devices_count > 0)
      pvr_physical_device_finish(&instance->physical_device);

   VG(VALGRIND_DESTROY_MEMPOOL(instance));

   vk_instance_finish(&instance->vk);
   vk_free(&instance->vk.alloc, instance);
}

static VkResult
pvr_physical_device_init_uuids(struct pvr_physical_device *pdevice)
{
   struct mesa_sha1 sha1_ctx;
   unsigned build_id_len;
   uint8_t sha1[20];
   uint64_t bvnc;

   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(pvr_physical_device_init_uuids);
   if (!note) {
      return vk_errorf(pdevice,
                       VK_ERROR_INITIALIZATION_FAILED,
                       "Failed to find build-id");
   }

   build_id_len = build_id_length(note);
   if (build_id_len < 20) {
      return vk_errorf(pdevice,
                       VK_ERROR_INITIALIZATION_FAILED,
                       "Build-id too short. It needs to be a SHA");
   }

   bvnc = pvr_get_packed_bvnc(&pdevice->dev_info);

   _mesa_sha1_init(&sha1_ctx);
   _mesa_sha1_update(&sha1_ctx, build_id_data(note), build_id_len);
   _mesa_sha1_update(&sha1_ctx, &bvnc, sizeof(bvnc));
   _mesa_sha1_final(&sha1_ctx, sha1);
   memcpy(pdevice->pipeline_cache_uuid, sha1, VK_UUID_SIZE);

   return VK_SUCCESS;
}

static uint64_t pvr_compute_heap_size(void)
{
   /* Query the total ram from the system */
   uint64_t total_ram;
   if (!os_get_total_physical_memory(&total_ram))
      return 0;

   /* We don't want to burn too much ram with the GPU. If the user has 4GiB
    * or less, we use at most half. If they have more than 4GiB, we use 3/4.
    */
   uint64_t available_ram;
   if (total_ram <= 4ULL * 1024ULL * 1024ULL * 1024ULL)
      available_ram = total_ram / 2U;
   else
      available_ram = total_ram * 3U / 4U;

   return available_ram;
}

static VkResult pvr_physical_device_init(struct pvr_physical_device *pdevice,
                                         struct pvr_instance *instance,
                                         drmDevicePtr drm_render_device,
                                         drmDevicePtr drm_primary_device)
{
   const char *path = drm_render_device->nodes[DRM_NODE_RENDER];
   struct vk_device_extension_table supported_extensions;
   struct vk_physical_device_dispatch_table dispatch_table;
   const char *primary_path;
   VkResult result;
   int ret;

   if (!getenv("PVR_I_WANT_A_BROKEN_VULKAN_DRIVER")) {
      return vk_errorf(instance,
                       VK_ERROR_INCOMPATIBLE_DRIVER,
                       "WARNING: powervr is not a conformant Vulkan "
                       "implementation. Pass "
                       "PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1 if you know "
                       "what you're doing.");
   }

   pvr_physical_device_get_supported_extensions(pdevice, &supported_extensions);

   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table,
      &pvr_physical_device_entrypoints,
      true);

   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table,
      &wsi_physical_device_entrypoints,
      false);

   result = vk_physical_device_init(&pdevice->vk,
                                    &instance->vk,
                                    &supported_extensions,
                                    &dispatch_table);
   if (result != VK_SUCCESS)
      return result;

   pdevice->instance = instance;

   pdevice->render_fd = open(path, O_RDWR | O_CLOEXEC);
   if (pdevice->render_fd < 0) {
      result = vk_errorf(instance,
                         VK_ERROR_INCOMPATIBLE_DRIVER,
                         "Failed to open device %s",
                         path);
      goto err_vk_physical_device_finish;
   }

   pdevice->render_path = vk_strdup(&pdevice->vk.instance->alloc,
                                    path,
                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!pdevice->render_path) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto err_close_render_fd;
   }

   if (instance->vk.enabled_extensions.KHR_display) {
      primary_path = drm_primary_device->nodes[DRM_NODE_PRIMARY];

      pdevice->master_fd = open(primary_path, O_RDWR | O_CLOEXEC);
   } else {
      pdevice->master_fd = -1;
   }

   if (pdevice->master_fd >= 0) {
      pdevice->master_path = vk_strdup(&pdevice->vk.instance->alloc,
                                       primary_path,
                                       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
      if (!pdevice->master_path) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto err_close_master_fd;
      }
   } else {
      pdevice->master_path = NULL;
   }

   pdevice->ws = pvr_winsys_create(pdevice->master_fd,
                                   pdevice->render_fd,
                                   &pdevice->vk.instance->alloc);
   if (!pdevice->ws) {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto err_vk_free_master_path;
   }

   pdevice->vk.supported_sync_types = pdevice->ws->sync_types;

   ret = pdevice->ws->ops->device_info_init(pdevice->ws,
                                            &pdevice->dev_info,
                                            &pdevice->dev_runtime_info);
   if (ret) {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto err_pvr_winsys_destroy;
   }

   result = pvr_physical_device_init_uuids(pdevice);
   if (result != VK_SUCCESS)
      goto err_pvr_winsys_destroy;

   if (asprintf(&pdevice->name,
                "Imagination PowerVR %s %s",
                pdevice->dev_info.ident.series_name,
                pdevice->dev_info.ident.public_name) < 0) {
      result = vk_errorf(instance,
                         VK_ERROR_OUT_OF_HOST_MEMORY,
                         "Unable to allocate memory to store device name");
      goto err_pvr_winsys_destroy;
   }

   /* Setup available memory heaps and types */
   pdevice->memory.memoryHeapCount = 1;
   pdevice->memory.memoryHeaps[0].size = pvr_compute_heap_size();
   pdevice->memory.memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

   pdevice->memory.memoryTypeCount = 1;
   pdevice->memory.memoryTypes[0].propertyFlags =
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   pdevice->memory.memoryTypes[0].heapIndex = 0;

   result = pvr_wsi_init(pdevice);
   if (result != VK_SUCCESS) {
      vk_error(instance, result);
      goto err_free_name;
   }

   pdevice->compiler = rogue_compiler_create(&pdevice->dev_info);
   if (!pdevice->compiler) {
      result = vk_errorf(instance,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to initialize Rogue compiler");
      goto err_wsi_finish;
   }

   return VK_SUCCESS;

err_wsi_finish:
   pvr_wsi_finish(pdevice);

err_free_name:
   free(pdevice->name);

err_pvr_winsys_destroy:
   pvr_winsys_destroy(pdevice->ws);

err_vk_free_master_path:
   vk_free(&pdevice->vk.instance->alloc, pdevice->master_path);

err_close_master_fd:
   if (pdevice->master_fd >= 0)
      close(pdevice->master_fd);

   vk_free(&pdevice->vk.instance->alloc, pdevice->render_path);

err_close_render_fd:
   close(pdevice->render_fd);

err_vk_physical_device_finish:
   vk_physical_device_finish(&pdevice->vk);

   return result;
}

static bool pvr_drm_device_is_supported(drmDevicePtr drm_dev, int node_type)
{
   char **compat = drm_dev->deviceinfo.platform->compatible;

   if (!(drm_dev->available_nodes & BITFIELD_BIT(node_type))) {
      assert(node_type == DRM_NODE_RENDER || node_type == DRM_NODE_PRIMARY);
      return false;
   }

   if (node_type == DRM_NODE_RENDER) {
      while (*compat) {
         for (size_t i = 0U; i < ARRAY_SIZE(pvr_render_devices); i++) {
            const char *const name = pvr_render_devices[i].name;
            const size_t len = pvr_render_devices[i].len;

            if (strncmp(*compat, name, len) == 0)
               return true;
         }

         compat++;
      }

      return false;
   } else if (node_type == DRM_NODE_PRIMARY) {
      while (*compat) {
         for (size_t i = 0U; i < ARRAY_SIZE(pvr_display_devices); i++) {
            const char *const name = pvr_display_devices[i].name;
            const size_t len = pvr_display_devices[i].len;

            if (strncmp(*compat, name, len) == 0)
               return true;
         }

         compat++;
      }

      return false;
   }

   unreachable("Incorrect node_type.");
}

static VkResult pvr_enumerate_devices(struct pvr_instance *instance)
{
   /* FIXME: It should be possible to query the number of devices via
    * drmGetDevices2 by passing in NULL for the 'devices' parameter. However,
    * this was broken by libdrm commit
    * 8cb12a2528d795c45bba5f03b3486b4040fb0f45, so, until this is fixed in
    * upstream, hard-code the maximum number of devices.
    */
   drmDevicePtr drm_primary_device = NULL;
   drmDevicePtr drm_render_device = NULL;
   drmDevicePtr drm_devices[8];
   int max_drm_devices;
   VkResult result;

   instance->physical_devices_count = 0;

   max_drm_devices = drmGetDevices2(0, drm_devices, ARRAY_SIZE(drm_devices));
   if (max_drm_devices < 1)
      return VK_SUCCESS;

   for (unsigned i = 0; i < (unsigned)max_drm_devices; i++) {
      if (drm_devices[i]->bustype != DRM_BUS_PLATFORM)
         continue;

      if (pvr_drm_device_is_supported(drm_devices[i], DRM_NODE_RENDER)) {
         drm_render_device = drm_devices[i];

         mesa_logd("Found compatible render device '%s'.",
                   drm_render_device->nodes[DRM_NODE_RENDER]);
      } else if (pvr_drm_device_is_supported(drm_devices[i],
                                             DRM_NODE_PRIMARY)) {
         drm_primary_device = drm_devices[i];

         mesa_logd("Found compatible primary device '%s'.",
                   drm_primary_device->nodes[DRM_NODE_PRIMARY]);
      }
   }

   if (drm_render_device && drm_primary_device) {
      result = pvr_physical_device_init(&instance->physical_device,
                                        instance,
                                        drm_render_device,
                                        drm_primary_device);
      if (result == VK_SUCCESS)
         instance->physical_devices_count = 1;
      else if (result == VK_ERROR_INCOMPATIBLE_DRIVER)
         result = VK_SUCCESS;
   } else {
      result = VK_SUCCESS;
   }

   drmFreeDevices(drm_devices, max_drm_devices);

   return result;
}

VkResult pvr_EnumeratePhysicalDevices(VkInstance _instance,
                                      uint32_t *pPhysicalDeviceCount,
                                      VkPhysicalDevice *pPhysicalDevices)
{
   VK_OUTARRAY_MAKE_TYPED(VkPhysicalDevice,
                          out,
                          pPhysicalDevices,
                          pPhysicalDeviceCount);
   PVR_FROM_HANDLE(pvr_instance, instance, _instance);
   VkResult result;

   if (instance->physical_devices_count < 0) {
      result = pvr_enumerate_devices(instance);
      if (result != VK_SUCCESS)
         return result;
   }

   if (instance->physical_devices_count == 0)
      return VK_SUCCESS;

   assert(instance->physical_devices_count == 1);
   vk_outarray_append_typed (VkPhysicalDevice, &out, p) {
      *p = pvr_physical_device_to_handle(&instance->physical_device);
   }

   return vk_outarray_status(&out);
}

void pvr_GetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice,
                                    VkPhysicalDeviceFeatures2 *pFeatures)
{
   PVR_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);

   pFeatures->features = (VkPhysicalDeviceFeatures){
      .robustBufferAccess =
         PVR_HAS_FEATURE(&pdevice->dev_info, robust_buffer_access),
      .fullDrawIndexUint32 = true,
      .imageCubeArray = true,
      .independentBlend = true,
      .geometryShader = false,
      .tessellationShader = false,
      .sampleRateShading = true,
      .dualSrcBlend = false,
      .logicOp = true,
      .multiDrawIndirect = true,
      .drawIndirectFirstInstance = true,
      .depthClamp = true,
      .depthBiasClamp = true,
      .fillModeNonSolid = false,
      .depthBounds = false,
      .wideLines = true,
      .largePoints = true,
      .alphaToOne = true,
      .multiViewport = false,
      .samplerAnisotropy = false,
      .textureCompressionETC2 = true,
      .textureCompressionASTC_LDR = PVR_HAS_FEATURE(&pdevice->dev_info, astc),
      .textureCompressionBC = false,
      .occlusionQueryPrecise = true,
      .pipelineStatisticsQuery = false,
      .vertexPipelineStoresAndAtomics = true,
      .fragmentStoresAndAtomics = true,
      .shaderTessellationAndGeometryPointSize = false,
      .shaderImageGatherExtended = false,
      .shaderStorageImageExtendedFormats = true,
      .shaderStorageImageMultisample = false,
      .shaderStorageImageReadWithoutFormat = true,
      .shaderStorageImageWriteWithoutFormat = false,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderClipDistance = true,
      .shaderCullDistance = true,
      .shaderFloat64 = false,
      .shaderInt64 = true,
      .shaderInt16 = true,
      .shaderResourceResidency = false,
      .shaderResourceMinLod = false,
      .sparseBinding = false,
      .sparseResidencyBuffer = false,
      .sparseResidencyImage2D = false,
      .sparseResidencyImage3D = false,
      .sparseResidency2Samples = false,
      .sparseResidency4Samples = false,
      .sparseResidency8Samples = false,
      .sparseResidency16Samples = false,
      .sparseResidencyAliased = false,
      .variableMultisampleRate = false,
      .inheritedQueries = false,
   };

   vk_foreach_struct (ext, pFeatures->pNext) {
      pvr_debug_ignored_stype(ext->sType);
   }
}

static uint32_t
pvr_get_simultanous_num_allocs(const struct pvr_physical_device *pdevice)
{
   const struct pvr_device_runtime_info *dev_runtime_info =
      &pdevice->dev_runtime_info;
   const struct pvr_device_info *dev_info = &pdevice->dev_info;
   uint32_t min_cluster_per_phantom;

   if (PVR_HAS_FEATURE(dev_info, s8xe))
      return PVR_GET_FEATURE_VALUE(dev_info, num_raster_pipes, 0U);

   assert(dev_runtime_info->num_phantoms == 1);
   min_cluster_per_phantom = PVR_GET_FEATURE_VALUE(dev_info, num_clusters, 1U);

   if (min_cluster_per_phantom >= 4)
      return 1;
   else if (min_cluster_per_phantom == 2)
      return 2;
   else
      return 4;
}

uint32_t pvr_calc_fscommon_size_and_tiles_in_flight(
   const struct pvr_physical_device *pdevice,
   uint32_t fs_common_size,
   uint32_t min_tiles_in_flight)
{
   const struct pvr_device_runtime_info *dev_runtime_info =
      &pdevice->dev_runtime_info;
   const uint32_t available_shareds =
      dev_runtime_info->reserved_shared_size - dev_runtime_info->max_coeffs;
   const struct pvr_device_info *dev_info = &pdevice->dev_info;
   const uint32_t max_tiles_in_flight =
      PVR_GET_FEATURE_VALUE(dev_info, isp_max_tiles_in_flight, 1U);
   uint32_t num_tile_in_flight;
   uint32_t num_allocs;

   if (fs_common_size == 0)
      return max_tiles_in_flight;

   num_allocs = pvr_get_simultanous_num_allocs(pdevice);

   if (fs_common_size == UINT32_MAX) {
      uint32_t max_common_size = available_shareds;

      num_allocs *= MIN2(min_tiles_in_flight, max_tiles_in_flight);

      if (!PVR_HAS_ERN(dev_info, 38748)) {
         /* Hardware needs space for one extra shared allocation. */
         num_allocs += 1;
      }

      /* Double resource requirements to deal with fragmentation. */
      max_common_size /= num_allocs * 2;
      max_common_size = MIN2(max_common_size, ROGUE_MAX_PIXEL_SHARED_REGISTERS);
      max_common_size =
         ROUND_DOWN_TO(max_common_size,
                       PVRX(TA_STATE_PDS_SIZEINFO2_USC_SHAREDSIZE_UNIT_SIZE));

      return max_common_size;
   }

   num_tile_in_flight = available_shareds / (fs_common_size * 2);

   if (!PVR_HAS_ERN(dev_info, 38748))
      num_tile_in_flight -= 1;

   num_tile_in_flight /= num_allocs;

#if defined(DEBUG)
   /* Validate the above result. */

   assert(num_tile_in_flight >= MIN2(num_tile_in_flight, max_tiles_in_flight));
   num_allocs *= num_tile_in_flight;

   if (!PVR_HAS_ERN(dev_info, 38748)) {
      /* Hardware needs space for one extra shared allocation. */
      num_allocs += 1;
   }

   assert(fs_common_size <= available_shareds / (num_allocs * 2));
#endif

   return MIN2(num_tile_in_flight, max_tiles_in_flight);
}

struct pvr_descriptor_limits {
   uint32_t max_per_stage_resources;
   uint32_t max_per_stage_samplers;
   uint32_t max_per_stage_uniform_buffers;
   uint32_t max_per_stage_storage_buffers;
   uint32_t max_per_stage_sampled_images;
   uint32_t max_per_stage_storage_images;
   uint32_t max_per_stage_input_attachments;
};

static const struct pvr_descriptor_limits *
pvr_get_physical_device_descriptor_limits(struct pvr_physical_device *pdevice)
{
   enum pvr_descriptor_cs_level {
      /* clang-format off */
      CS4096, /* 6XT and some XE cores with large CS. */
      CS2560, /* Mid range Rogue XE cores. */
      CS2048, /* Low end Rogue XE cores. */
      CS1536, /* Ultra-low-end 9XEP. */
      CS680,  /* lower limits for older devices. */
      CS408,  /* 7XE. */
      /* clang-format on */
   };

   static const struct pvr_descriptor_limits descriptor_limits[] = {
      [CS4096] = { 1160U, 256U, 192U, 144U, 256U, 256U, 8U, },
      [CS2560] = {  648U, 128U, 128U, 128U, 128U, 128U, 8U, },
      [CS2048] = {  584U, 128U,  96U,  64U, 128U, 128U, 8U, },
      [CS1536] = {  456U,  64U,  96U,  64U, 128U,  64U, 8U, },
      [CS680]  = {  224U,  32U,  64U,  36U,  48U,   8U, 8U, },
      [CS408]  = {  128U,  16U,  40U,  28U,  16U,   8U, 8U, },
   };

   const uint32_t common_size =
      pvr_calc_fscommon_size_and_tiles_in_flight(pdevice, UINT32_MAX, 1);
   enum pvr_descriptor_cs_level cs_level;

   if (common_size >= 2048) {
      cs_level = CS2048;
   } else if (common_size >= 1526) {
      cs_level = CS1536;
   } else if (common_size >= 680) {
      cs_level = CS680;
   } else if (common_size >= 408) {
      cs_level = CS408;
   } else {
      mesa_loge("This core appears to have a very limited amount of shared "
                "register space and may not meet the Vulkan spec limits.");
      abort();
   }

   return &descriptor_limits[cs_level];
}

void pvr_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice,
                                      VkPhysicalDeviceProperties2 *pProperties)
{
   PVR_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);
   const struct pvr_descriptor_limits *descriptor_limits =
      pvr_get_physical_device_descriptor_limits(pdevice);

   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t max_multisample =
      PVR_GET_FEATURE_VALUE(&pdevice->dev_info, max_multisample, 4);

   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t uvs_banks =
      PVR_GET_FEATURE_VALUE(&pdevice->dev_info, uvs_banks, 2);

   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t uvs_pba_entries =
      PVR_GET_FEATURE_VALUE(&pdevice->dev_info, uvs_pba_entries, 160);

   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t num_user_clip_planes =
      PVR_GET_FEATURE_VALUE(&pdevice->dev_info, num_user_clip_planes, 8);

   const uint32_t sub_pixel_precision =
      PVR_HAS_FEATURE(&pdevice->dev_info, simple_internal_parameter_format)
         ? 4U
         : 8U;

   const uint32_t max_render_size =
      rogue_get_render_size_max(&pdevice->dev_info);

   const uint32_t max_sample_bits = ((max_multisample << 1) - 1);

   const uint32_t max_user_vertex_components =
      ((uvs_banks <= 8U) && (uvs_pba_entries == 160U)) ? 64U : 128U;

   /* The workgroup invocations are limited by the case where we have a compute
    * barrier - each slot has a fixed number of invocations, the whole workgroup
    * may need to span multiple slots. As each slot will WAIT at the barrier
    * until the last invocation completes, all have to be schedulable at the
    * same time.
    *
    * Typically all Rogue cores have 16 slots. Some of the smallest cores are
    * reduced to 14.
    *
    * The compute barrier slot exhaustion scenario can be tested with:
    * dEQP-VK.memory_model.message_passing*u32.coherent.fence_fence
    *    .atomicwrite*guard*comp
    */

   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t usc_slots =
      PVR_GET_FEATURE_VALUE(&pdevice->dev_info, usc_slots, 14);

   /* Default value based on the minimum value found in all existing cores. */
   const uint32_t max_instances_per_pds_task =
      PVR_GET_FEATURE_VALUE(&pdevice->dev_info,
                            max_instances_per_pds_task,
                            32U);

   const uint32_t max_compute_work_group_invocations =
      (usc_slots * max_instances_per_pds_task >= 512U) ? 512U : 384U;

   VkPhysicalDeviceLimits limits = {
      .maxImageDimension1D = max_render_size,
      .maxImageDimension2D = max_render_size,
      .maxImageDimension3D = PVR_MAX_TEXTURE_EXTENT_Z,
      .maxImageDimensionCube = max_render_size,
      .maxImageArrayLayers = PVR_MAX_ARRAY_LAYERS,
      .maxTexelBufferElements = 64U * 1024U,
      .maxUniformBufferRange = 128U * 1024U * 1024U,
      .maxStorageBufferRange = 128U * 1024U * 1024U,
      .maxPushConstantsSize = PVR_MAX_PUSH_CONSTANTS_SIZE,
      .maxMemoryAllocationCount = UINT32_MAX,
      .maxSamplerAllocationCount = UINT32_MAX,
      .bufferImageGranularity = 1U,
      .sparseAddressSpaceSize = 256ULL * 1024ULL * 1024ULL * 1024ULL,

      /* Maximum number of descriptor sets that can be bound at the same time.
       */
      .maxBoundDescriptorSets = PVR_MAX_DESCRIPTOR_SETS,

      .maxPerStageResources = descriptor_limits->max_per_stage_resources,
      .maxPerStageDescriptorSamplers =
         descriptor_limits->max_per_stage_samplers,
      .maxPerStageDescriptorUniformBuffers =
         descriptor_limits->max_per_stage_uniform_buffers,
      .maxPerStageDescriptorStorageBuffers =
         descriptor_limits->max_per_stage_storage_buffers,
      .maxPerStageDescriptorSampledImages =
         descriptor_limits->max_per_stage_sampled_images,
      .maxPerStageDescriptorStorageImages =
         descriptor_limits->max_per_stage_storage_images,
      .maxPerStageDescriptorInputAttachments =
         descriptor_limits->max_per_stage_input_attachments,

      .maxDescriptorSetSamplers = 256U,
      .maxDescriptorSetUniformBuffers = 256U,
      .maxDescriptorSetUniformBuffersDynamic = 8U,
      .maxDescriptorSetStorageBuffers = 256U,
      .maxDescriptorSetStorageBuffersDynamic = 8U,
      .maxDescriptorSetSampledImages = 256U,
      .maxDescriptorSetStorageImages = 256U,
      .maxDescriptorSetInputAttachments = 256U,

      /* Vertex Shader Limits */
      .maxVertexInputAttributes = PVR_MAX_VERTEX_INPUT_BINDINGS,
      .maxVertexInputBindings = PVR_MAX_VERTEX_INPUT_BINDINGS,
      .maxVertexInputAttributeOffset = 0xFFFF,
      .maxVertexInputBindingStride = 1024U * 1024U * 1024U * 2U,
      .maxVertexOutputComponents = max_user_vertex_components,

      /* Tessellation Limits */
      .maxTessellationGenerationLevel = 0,
      .maxTessellationPatchSize = 0,
      .maxTessellationControlPerVertexInputComponents = 0,
      .maxTessellationControlPerVertexOutputComponents = 0,
      .maxTessellationControlPerPatchOutputComponents = 0,
      .maxTessellationControlTotalOutputComponents = 0,
      .maxTessellationEvaluationInputComponents = 0,
      .maxTessellationEvaluationOutputComponents = 0,

      /* Geometry Shader Limits */
      .maxGeometryShaderInvocations = 0,
      .maxGeometryInputComponents = 0,
      .maxGeometryOutputComponents = 0,
      .maxGeometryOutputVertices = 0,
      .maxGeometryTotalOutputComponents = 0,

      /* Fragment Shader Limits */
      .maxFragmentInputComponents = max_user_vertex_components,
      .maxFragmentOutputAttachments = PVR_MAX_COLOR_ATTACHMENTS,
      .maxFragmentDualSrcAttachments = 0,
      .maxFragmentCombinedOutputResources =
         descriptor_limits->max_per_stage_storage_buffers +
         descriptor_limits->max_per_stage_storage_images +
         PVR_MAX_COLOR_ATTACHMENTS,

      /* Compute Shader Limits */
      .maxComputeSharedMemorySize = 16U * 1024U,
      .maxComputeWorkGroupCount = { 64U * 1024U, 64U * 1024U, 64U * 1024U },
      .maxComputeWorkGroupInvocations = max_compute_work_group_invocations,
      .maxComputeWorkGroupSize = { max_compute_work_group_invocations,
                                   max_compute_work_group_invocations,
                                   64U },

      /* Rasterization Limits */
      .subPixelPrecisionBits = sub_pixel_precision,
      .subTexelPrecisionBits = 8U,
      .mipmapPrecisionBits = 8U,

      .maxDrawIndexedIndexValue = UINT32_MAX,
      .maxDrawIndirectCount = 2U * 1024U * 1024U * 1024U,
      .maxSamplerLodBias = 16.0f,
      .maxSamplerAnisotropy = 1.0f,
      .maxViewports = PVR_MAX_VIEWPORTS,

      .maxViewportDimensions[0] = max_render_size,
      .maxViewportDimensions[1] = max_render_size,
      .viewportBoundsRange[0] = -(int32_t)(2U * max_render_size),
      .viewportBoundsRange[1] = 2U * max_render_size,

      .viewportSubPixelBits = 0,
      .minMemoryMapAlignment = 64U,
      .minTexelBufferOffsetAlignment = 16U,
      .minUniformBufferOffsetAlignment = 4U,
      .minStorageBufferOffsetAlignment = 4U,

      .minTexelOffset = -8,
      .maxTexelOffset = 7U,
      .minTexelGatherOffset = -8,
      .maxTexelGatherOffset = 7,
      .minInterpolationOffset = -0.5,
      .maxInterpolationOffset = 0.5,
      .subPixelInterpolationOffsetBits = 4U,

      .maxFramebufferWidth = max_render_size,
      .maxFramebufferHeight = max_render_size,
      .maxFramebufferLayers = PVR_MAX_FRAMEBUFFER_LAYERS,

      .framebufferColorSampleCounts = max_sample_bits,
      .framebufferDepthSampleCounts = max_sample_bits,
      .framebufferStencilSampleCounts = max_sample_bits,
      .framebufferNoAttachmentsSampleCounts = max_sample_bits,
      .maxColorAttachments = PVR_MAX_COLOR_ATTACHMENTS,
      .sampledImageColorSampleCounts = max_sample_bits,
      .sampledImageIntegerSampleCounts = max_sample_bits,
      .sampledImageDepthSampleCounts = max_sample_bits,
      .sampledImageStencilSampleCounts = max_sample_bits,
      .storageImageSampleCounts = max_sample_bits,
      .maxSampleMaskWords = 1U,
      .timestampComputeAndGraphics = false,
      .timestampPeriod = 0.0f,
      .maxClipDistances = num_user_clip_planes,
      .maxCullDistances = num_user_clip_planes,
      .maxCombinedClipAndCullDistances = num_user_clip_planes,
      .discreteQueuePriorities = 2U,
      .pointSizeRange[0] = 1.0f,
      .pointSizeRange[1] = 511.0f,
      .pointSizeGranularity = 0.0625f,
      .lineWidthRange[0] = 1.0f / 16.0f,
      .lineWidthRange[1] = 16.0f,
      .lineWidthGranularity = 1.0f / 16.0f,
      .strictLines = false,
      .standardSampleLocations = true,
      .optimalBufferCopyOffsetAlignment = 4U,
      .optimalBufferCopyRowPitchAlignment = 4U,
      .nonCoherentAtomSize = 1U,
   };

   pProperties->properties = (VkPhysicalDeviceProperties){
      .apiVersion = PVR_API_VERSION,
      .driverVersion = vk_get_driver_version(),
      .vendorID = VK_VENDOR_ID_IMAGINATION,
      .deviceID = pdevice->dev_info.ident.device_id,
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
      .limits = limits,
      .sparseProperties = { 0 },
   };

   snprintf(pProperties->properties.deviceName,
            sizeof(pProperties->properties.deviceName),
            "%s",
            pdevice->name);

   memcpy(pProperties->properties.pipelineCacheUUID,
          pdevice->pipeline_cache_uuid,
          VK_UUID_SIZE);

   vk_foreach_struct (ext, pProperties->pNext) {
      pvr_debug_ignored_stype(ext->sType);
   }
}

const static VkQueueFamilyProperties pvr_queue_family_properties = {
   .queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT |
                 VK_QUEUE_TRANSFER_BIT,
   .queueCount = PVR_MAX_QUEUES,
   .timestampValidBits = 0,
   .minImageTransferGranularity = { 1, 1, 1 },
};

void pvr_GetPhysicalDeviceQueueFamilyProperties(
   VkPhysicalDevice physicalDevice,
   uint32_t *pCount,
   VkQueueFamilyProperties *pQueueFamilyProperties)
{
   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties,
                          out,
                          pQueueFamilyProperties,
                          pCount);

   vk_outarray_append_typed (VkQueueFamilyProperties, &out, p) {
      *p = pvr_queue_family_properties;
   }
}

void pvr_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice physicalDevice,
   uint32_t *pQueueFamilyPropertyCount,
   VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2,
                          out,
                          pQueueFamilyProperties,
                          pQueueFamilyPropertyCount);

   vk_outarray_append_typed (VkQueueFamilyProperties2, &out, p) {
      p->queueFamilyProperties = pvr_queue_family_properties;

      vk_foreach_struct (ext, p->pNext) {
         pvr_debug_ignored_stype(ext->sType);
      }
   }
}

void pvr_GetPhysicalDeviceMemoryProperties2(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   PVR_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);

   pMemoryProperties->memoryProperties = pdevice->memory;

   vk_foreach_struct (ext, pMemoryProperties->pNext) {
      pvr_debug_ignored_stype(ext->sType);
   }
}

PFN_vkVoidFunction pvr_GetInstanceProcAddr(VkInstance _instance,
                                           const char *pName)
{
   PVR_FROM_HANDLE(pvr_instance, instance, _instance);
   return vk_instance_get_proc_addr(&instance->vk,
                                    &pvr_instance_entrypoints,
                                    pName);
}

/* With version 1+ of the loader interface the ICD should expose
 * vk_icdGetInstanceProcAddr to work around certain LD_PRELOAD issues seen in
 * apps.
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetInstanceProcAddr(VkInstance instance, const char *pName)
{
   return pvr_GetInstanceProcAddr(instance, pName);
}

/* With version 4+ of the loader interface the ICD should expose
 * vk_icdGetPhysicalDeviceProcAddr().
 */
PUBLIC
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_icdGetPhysicalDeviceProcAddr(VkInstance _instance, const char *pName)
{
   PVR_FROM_HANDLE(pvr_instance, instance, _instance);
   return vk_instance_get_physical_device_proc_addr(&instance->vk, pName);
}

static VkResult pvr_device_init_compute_fence_program(struct pvr_device *device)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   const uint32_t cache_line_size = rogue_get_slc_cache_line_size(dev_info);
   struct pvr_pds_compute_shader_program program = { 0U };
   size_t staging_buffer_size;
   uint32_t *staging_buffer;
   uint32_t *data_buffer;
   uint32_t *code_buffer;
   VkResult result;

   STATIC_ASSERT(ARRAY_SIZE(program.local_input_regs) ==
                 ARRAY_SIZE(program.work_group_input_regs));
   STATIC_ASSERT(ARRAY_SIZE(program.local_input_regs) ==
                 ARRAY_SIZE(program.global_input_regs));

   /* Initialize PDS structure. */
   for (uint32_t i = 0U; i < ARRAY_SIZE(program.local_input_regs); i++) {
      program.local_input_regs[i] = PVR_PDS_COMPUTE_INPUT_REG_UNUSED;
      program.work_group_input_regs[i] = PVR_PDS_COMPUTE_INPUT_REG_UNUSED;
      program.global_input_regs[i] = PVR_PDS_COMPUTE_INPUT_REG_UNUSED;
   }

   program.barrier_coefficient = PVR_PDS_COMPUTE_INPUT_REG_UNUSED;

   /* Fence kernel. */
   program.fence = true;
   program.clear_pds_barrier = true;

   /* Calculate how much space we'll need for the compute shader PDS program.
    */
   pvr_pds_set_sizes_compute_shader(&program, dev_info);

   /* FIXME: Fix the below inconsistency of code size being in bytes whereas
    * data size being in dwords.
    */
   /* Code size is in bytes, data size in dwords. */
   staging_buffer_size =
      program.data_size * sizeof(uint32_t) + program.code_size;

   staging_buffer = vk_alloc(&device->vk.alloc,
                             staging_buffer_size,
                             8U,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   data_buffer = staging_buffer;
   code_buffer = pvr_pds_generate_compute_shader_data_segment(&program,
                                                              data_buffer,
                                                              dev_info);
   pvr_pds_generate_compute_shader_code_segment(&program,
                                                code_buffer,
                                                dev_info);
   result = pvr_gpu_upload_pds(device,
                               data_buffer,
                               program.data_size,
                               PVRX(CDMCTRL_KERNEL1_DATA_ADDR_ALIGNMENT),
                               code_buffer,
                               program.code_size / sizeof(uint32_t),
                               PVRX(CDMCTRL_KERNEL2_CODE_ADDR_ALIGNMENT),
                               cache_line_size,
                               &device->pds_compute_fence_program);

   vk_free(&device->vk.alloc, staging_buffer);

   return result;
}

static VkResult pvr_pds_idfwdf_programs_create_and_upload(
   struct pvr_device *device,
   pvr_dev_addr_t usc_addr,
   uint32_t shareds,
   uint32_t temps,
   pvr_dev_addr_t shareds_buffer_addr,
   struct pvr_pds_upload *const upload_out,
   struct pvr_pds_upload *const sw_compute_barrier_upload_out)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   struct pvr_pds_vertex_shader_sa_program program = {
      .kick_usc = true,
      .clear_pds_barrier = PVR_NEED_SW_COMPUTE_PDS_BARRIER(dev_info),
   };
   size_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   /* We'll need to DMA the shareds into the USC's Common Store. */
   program.num_dma_kicks = pvr_pds_encode_dma_burst(program.dma_control,
                                                    program.dma_address,
                                                    0,
                                                    shareds,
                                                    shareds_buffer_addr.addr,
                                                    dev_info);

   /* DMA temp regs. */
   pvr_pds_setup_doutu(&program.usc_task_control,
                       usc_addr.addr,
                       temps,
                       PVRX(PDSINST_DOUTU_SAMPLE_RATE_INSTANCE),
                       false);

   pvr_pds_vertex_shader_sa(&program, NULL, PDS_GENERATE_SIZES, dev_info);

   staging_buffer_size =
      (program.code_size + program.data_size) * sizeof(*staging_buffer);

   staging_buffer = vk_alloc(&device->vk.alloc,
                             staging_buffer_size,
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* FIXME: Add support for PDS_GENERATE_CODEDATA_SEGMENTS? */
   pvr_pds_vertex_shader_sa(&program,
                            staging_buffer,
                            PDS_GENERATE_DATA_SEGMENT,
                            dev_info);
   pvr_pds_vertex_shader_sa(&program,
                            &staging_buffer[program.data_size],
                            PDS_GENERATE_CODE_SEGMENT,
                            dev_info);

   /* At the time of writing, the SW_COMPUTE_PDS_BARRIER variant of the program
    * is bigger so we handle it first (if needed) and realloc() for a smaller
    * size.
    */
   if (PVR_NEED_SW_COMPUTE_PDS_BARRIER(dev_info)) {
      /* FIXME: Figure out the define for alignment of 16. */
      result = pvr_gpu_upload_pds(device,
                                  &staging_buffer[0],
                                  program.data_size,
                                  16,
                                  &staging_buffer[program.data_size],
                                  program.code_size,
                                  16,
                                  16,
                                  sw_compute_barrier_upload_out);
      if (result != VK_SUCCESS) {
         vk_free(&device->vk.alloc, staging_buffer);
         return result;
      }

      program.clear_pds_barrier = false;

      pvr_pds_vertex_shader_sa(&program, NULL, PDS_GENERATE_SIZES, dev_info);

      staging_buffer_size =
         (program.code_size + program.data_size) * sizeof(*staging_buffer);

      staging_buffer = vk_realloc(&device->vk.alloc,
                                  staging_buffer,
                                  staging_buffer_size,
                                  8,
                                  VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!staging_buffer) {
         pvr_bo_free(device, sw_compute_barrier_upload_out->pvr_bo);

         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      /* FIXME: Add support for PDS_GENERATE_CODEDATA_SEGMENTS? */
      pvr_pds_vertex_shader_sa(&program,
                               staging_buffer,
                               PDS_GENERATE_DATA_SEGMENT,
                               dev_info);
      pvr_pds_vertex_shader_sa(&program,
                               &staging_buffer[program.data_size],
                               PDS_GENERATE_CODE_SEGMENT,
                               dev_info);
   } else {
      *sw_compute_barrier_upload_out = (struct pvr_pds_upload){
         .pvr_bo = NULL,
      };
   }

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               &staging_buffer[0],
                               program.data_size,
                               16,
                               &staging_buffer[program.data_size],
                               program.code_size,
                               16,
                               16,
                               upload_out);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, staging_buffer);
      pvr_bo_free(device, sw_compute_barrier_upload_out->pvr_bo);

      return result;
   }

   vk_free(&device->vk.alloc, staging_buffer);

   return VK_SUCCESS;
}

static VkResult pvr_device_init_compute_idfwdf_state(struct pvr_device *device)
{
   uint64_t sampler_state[ROGUE_NUM_TEXSTATE_SAMPLER_WORDS];
   uint64_t image_state[ROGUE_NUM_TEXSTATE_IMAGE_WORDS];
   const struct rogue_shader_binary *usc_program;
   struct pvr_texture_state_info tex_info;
   uint32_t *dword_ptr;
   uint32_t usc_shareds;
   uint32_t usc_temps;
   VkResult result;

   pvr_hard_code_get_idfwdf_program(&device->pdevice->dev_info,
                                    &usc_program,
                                    &usc_shareds,
                                    &usc_temps);

   device->idfwdf_state.usc_shareds = usc_shareds;

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_usc(device,
                               usc_program->data,
                               usc_program->size,
                               16,
                               &device->idfwdf_state.usc);
   if (result != VK_SUCCESS)
      return result;

   /* TODO: Get the store buffer size from the compiler? */
   /* TODO: How was the size derived here? */
   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         4 * sizeof(float) * 4 * 2,
                         4,
                         0,
                         &device->idfwdf_state.store_bo);
   if (result != VK_SUCCESS)
      goto err_free_usc_program;

   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         usc_shareds * ROGUE_REG_SIZE_BYTES,
                         ROGUE_REG_SIZE_BYTES,
                         PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                         &device->idfwdf_state.shareds_bo);
   if (result != VK_SUCCESS)
      goto err_free_store_buffer;

   /* Pack state words. */

   pvr_csb_pack (&sampler_state[0], TEXSTATE_SAMPLER, sampler) {
      sampler.dadjust = PVRX(TEXSTATE_DADJUST_ZERO_UINT);
      sampler.magfilter = PVRX(TEXSTATE_FILTER_POINT);
      sampler.addrmode_u = PVRX(TEXSTATE_ADDRMODE_CLAMP_TO_EDGE);
      sampler.addrmode_v = PVRX(TEXSTATE_ADDRMODE_CLAMP_TO_EDGE);
   }

   /* clang-format off */
   pvr_csb_pack (&sampler_state[1], TEXSTATE_SAMPLER_WORD1, sampler_word1) {}
   /* clang-format on */

   STATIC_ASSERT(1 + 1 == ROGUE_NUM_TEXSTATE_SAMPLER_WORDS);

   tex_info = (struct pvr_texture_state_info){
      .format = VK_FORMAT_R32G32B32A32_SFLOAT,
      .mem_layout = PVR_MEMLAYOUT_LINEAR,
      .flags = PVR_TEXFLAGS_INDEX_LOOKUP,
      .type = VK_IMAGE_VIEW_TYPE_2D,
      .extent = { .width = 4, .height = 2, .depth = 0 },
      .mip_levels = 1,
      .sample_count = 1,
      .stride = 4,
      .swizzle = { PIPE_SWIZZLE_X,
                   PIPE_SWIZZLE_Y,
                   PIPE_SWIZZLE_Z,
                   PIPE_SWIZZLE_W },
      .addr = device->idfwdf_state.store_bo->vma->dev_addr,
   };

   result = pvr_pack_tex_state(device, &tex_info, image_state);
   if (result != VK_SUCCESS)
      goto err_free_shareds_buffer;

   /* Fill the shareds buffer. */

   dword_ptr = (uint32_t *)device->idfwdf_state.shareds_bo->bo->map;

#define HIGH_32(val) ((uint32_t)((val) >> 32U))
#define LOW_32(val) ((uint32_t)(val))

   /* TODO: Should we use compiler info to setup the shareds data instead of
    * assuming there's always 12 and this is how they should be setup?
    */

   dword_ptr[0] = HIGH_32(device->idfwdf_state.store_bo->vma->dev_addr.addr);
   dword_ptr[1] = LOW_32(device->idfwdf_state.store_bo->vma->dev_addr.addr);

   /* Pad the shareds as the texture/sample state words are 128 bit aligned. */
   dword_ptr[2] = 0U;
   dword_ptr[3] = 0U;

   dword_ptr[4] = LOW_32(image_state[0]);
   dword_ptr[5] = HIGH_32(image_state[0]);
   dword_ptr[6] = LOW_32(image_state[1]);
   dword_ptr[7] = HIGH_32(image_state[1]);

   dword_ptr[8] = LOW_32(sampler_state[0]);
   dword_ptr[9] = HIGH_32(sampler_state[0]);
   dword_ptr[10] = LOW_32(sampler_state[1]);
   dword_ptr[11] = HIGH_32(sampler_state[1]);
   assert(11 + 1 == usc_shareds);

#undef HIGH_32
#undef LOW_32

   pvr_bo_cpu_unmap(device, device->idfwdf_state.shareds_bo);
   dword_ptr = NULL;

   /* Generate and upload PDS programs. */
   result = pvr_pds_idfwdf_programs_create_and_upload(
      device,
      device->idfwdf_state.usc->vma->dev_addr,
      usc_shareds,
      usc_temps,
      device->idfwdf_state.shareds_bo->vma->dev_addr,
      &device->idfwdf_state.pds,
      &device->idfwdf_state.sw_compute_barrier_pds);
   if (result != VK_SUCCESS)
      goto err_free_shareds_buffer;

   return VK_SUCCESS;

err_free_shareds_buffer:
   pvr_bo_free(device, device->idfwdf_state.shareds_bo);

err_free_store_buffer:
   pvr_bo_free(device, device->idfwdf_state.store_bo);

err_free_usc_program:
   pvr_bo_free(device, device->idfwdf_state.usc);

   return result;
}

static void pvr_device_finish_compute_idfwdf_state(struct pvr_device *device)
{
   pvr_bo_free(device, device->idfwdf_state.pds.pvr_bo);
   pvr_bo_free(device, device->idfwdf_state.sw_compute_barrier_pds.pvr_bo);
   pvr_bo_free(device, device->idfwdf_state.shareds_bo);
   pvr_bo_free(device, device->idfwdf_state.store_bo);
   pvr_bo_free(device, device->idfwdf_state.usc);
}

static void pvr_device_setup_graphics_static_clear_ppp_base(
   struct pvr_static_clear_ppp_base *const base)
{
   pvr_csb_pack (&base->wclamp, TA_WCLAMP, wclamp) {
      wclamp.val = fui(0.00001f);
   }

   /* clang-format off */
   pvr_csb_pack (&base->varying_word[0], TA_STATE_VARYING0, varying0);
   pvr_csb_pack (&base->varying_word[1], TA_STATE_VARYING1, varying1);
   pvr_csb_pack (&base->varying_word[2], TA_STATE_VARYING2, varying2);
   /* clang-format on */

   pvr_csb_pack (&base->ppp_ctrl, TA_STATE_PPP_CTRL, ppp_ctrl) {
      ppp_ctrl.pretransform = true;
      ppp_ctrl.cullmode = PVRX(TA_CULLMODE_NO_CULLING);
   }

   /* clang-format off */
   pvr_csb_pack (&base->stream_out0, TA_STATE_STREAM_OUT0, stream_out0);
   /* clang-format on */
}

static void pvr_device_setup_graphics_static_clear_ppp_templates(
   struct pvr_static_clear_ppp_template
      templates[static PVR_STATIC_CLEAR_VARIANT_COUNT])
{
   for (uint32_t i = 0; i < PVR_STATIC_CLEAR_VARIANT_COUNT; i++) {
      const bool has_depth = !!(i & PVR_STATIC_CLEAR_DEPTH_BIT);
      const bool has_stencil = !!(i & PVR_STATIC_CLEAR_STENCIL_BIT);
      const bool has_color = !!(i & PVR_STATIC_CLEAR_COLOR_BIT);

      struct pvr_static_clear_ppp_template *const template = &templates[i];

      template->requires_pds_state = has_color;

      pvr_csb_pack (&template->header, TA_STATE_HEADER, header) {
         header.pres_stream_out_size = true;
         header.pres_ppp_ctrl = true;
         header.pres_varying_word2 = true;
         header.pres_varying_word1 = true;
         header.pres_varying_word0 = true;
         header.pres_outselects = true;
         header.pres_wclamp = true;
         header.pres_region_clip = true;
         header.pres_pds_state_ptr2 = template->requires_pds_state;
         header.pres_pds_state_ptr1 = template->requires_pds_state;
         header.pres_pds_state_ptr0 = template->requires_pds_state;
         header.pres_ispctl_fb = true;
         header.pres_ispctl_fa = true;
         header.pres_ispctl = true;
      }

#define CS_HEADER(cs)    \
   (struct PVRX(cs))     \
   {                     \
      pvr_cmd_header(cs) \
   }

      template->config.ispctl = CS_HEADER(TA_STATE_ISPCTL);
      template->config.ispctl.tagwritedisable = !has_color;
      template->config.ispctl.bpres = true;

      template->config.ispa = CS_HEADER(TA_STATE_ISPA);
      template->config.ispa.objtype = PVRX(TA_OBJTYPE_TRIANGLE);
      template->config.ispa.passtype = PVRX(TA_PASSTYPE_TRANSLUCENT);
      template->config.ispa.dwritedisable = !has_depth;
      template->config.ispa.dcmpmode = (i == 0) ? PVRX(TA_CMPMODE_NEVER)
                                                : PVRX(TA_CMPMODE_ALWAYS);
      template->config.ispa.sref =
         has_stencil ? PVRX(TA_STATE_ISPA_SREF_SIZE_MAX) : 0;

      pvr_csb_pack (&template->ispb, TA_STATE_ISPB, ispb) {
         ispb.scmpmode = PVRX(TA_CMPMODE_ALWAYS);
         ispb.sop1 = PVRX(TA_ISPB_STENCILOP_KEEP);
         ispb.sop2 = PVRX(TA_ISPB_STENCILOP_KEEP);

         ispb.sop3 = has_stencil ? PVRX(TA_ISPB_STENCILOP_REPLACE)
                                 : PVRX(TA_ISPB_STENCILOP_KEEP);

         ispb.swmask = has_stencil ? 0xFF : 0;
      }

      template->config.pds_state = NULL;

      template->config.region_clip0 = CS_HEADER(TA_REGION_CLIP0);
      template->config.region_clip0.mode = PVRX(TA_REGION_CLIP_MODE_NONE);

      template->config.region_clip1 = CS_HEADER(TA_REGION_CLIP1);

      template->config.output_sel = CS_HEADER(TA_OUTPUT_SEL);
      template->config.output_sel.vtxsize = 4;
      template->config.output_sel.rhw_pres = true;

#undef CS_HEADER
   }
}

/**
 * \brief Emit geom state from a configurable template.
 *
 * Note that the state is emitted by joining the template with a base so the
 * base must have been setup before calling this.
 *
 * \param[in] csb          Control stream to emit to.
 * \param[in] template     The configured template.
 * \param[out] pvr_bo_out  Uploaded state's pvr_bo object.
 *
 * \return   VK_SUCCESS if the state was successfully uploaded.
 */
VkResult pvr_emit_ppp_from_template(
   struct pvr_csb *const csb,
   const struct pvr_static_clear_ppp_template *const template,
   struct pvr_bo **const pvr_bo_out)
{
   const uint32_t dword_count =
      pvr_cmd_length(TA_STATE_HEADER) + pvr_cmd_length(TA_STATE_ISPCTL) +
      pvr_cmd_length(TA_STATE_ISPA) + pvr_cmd_length(TA_STATE_ISPB) +
      (template->requires_pds_state ? PVR_STATIC_CLEAR_PDS_STATE_COUNT : 0) +
      pvr_cmd_length(TA_REGION_CLIP0) + pvr_cmd_length(TA_REGION_CLIP1) +
      pvr_cmd_length(TA_WCLAMP) + pvr_cmd_length(TA_OUTPUT_SEL) +
      pvr_cmd_length(TA_STATE_VARYING0) + pvr_cmd_length(TA_STATE_VARYING1) +
      pvr_cmd_length(TA_STATE_VARYING2) + pvr_cmd_length(TA_STATE_PPP_CTRL) +
      pvr_cmd_length(TA_STATE_STREAM_OUT0);

   struct pvr_device *const device = csb->device;
   const uint32_t cache_line_size =
      rogue_get_slc_cache_line_size(&device->pdevice->dev_info);
   const struct pvr_static_clear_ppp_base *const base =
      &device->static_clear_state.ppp_base;
   struct pvr_bo *pvr_bo;
   uint32_t *stream;
   VkResult result;

   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         dword_count * sizeof(uint32_t),
                         cache_line_size,
                         PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                         &pvr_bo);
   if (result != VK_SUCCESS) {
      *pvr_bo_out = NULL;
      return result;
   }

#define CS_WRITE(_dst, cmd, val)                                      \
   do {                                                               \
      static_assert(sizeof(*(_dst)) == pvr_cmd_length(cmd) * 4,       \
                    "Size mismatch");                                 \
      static_assert(sizeof(*(_dst)) == sizeof(val), "Size mismatch"); \
      *(_dst) = val;                                                  \
      (_dst) += pvr_cmd_length(cmd);                                  \
   } while (0)

#define CS_PACK_WRITE(_dst, cmd, val)                           \
   do {                                                         \
      static_assert(sizeof(*(_dst)) == pvr_cmd_length(cmd) * 4, \
                    "Size mismatch");                           \
      pvr_cmd_pack(cmd)((_dst), val);                           \
      (_dst) += pvr_cmd_length(cmd);                            \
   } while (0)

   stream = (uint32_t *)pvr_bo->bo->map;

   CS_WRITE(stream, TA_STATE_HEADER, template->header);
   CS_PACK_WRITE(stream, TA_STATE_ISPCTL, &template->config.ispctl);
   CS_PACK_WRITE(stream, TA_STATE_ISPA, &template->config.ispa);
   CS_WRITE(stream, TA_STATE_ISPB, template->ispb);

   if (template->requires_pds_state) {
      static_assert(sizeof(*stream) == sizeof((*template->config.pds_state)[0]),
                    "Size mismatch");
      for (uint32_t i = 0; i < PVR_STATIC_CLEAR_PDS_STATE_COUNT; i++)
         *stream++ = (*template->config.pds_state)[i];
   }

   CS_PACK_WRITE(stream, TA_REGION_CLIP0, &template->config.region_clip0);
   CS_PACK_WRITE(stream, TA_REGION_CLIP1, &template->config.region_clip1);
   CS_WRITE(stream, TA_WCLAMP, base->wclamp);
   CS_PACK_WRITE(stream, TA_OUTPUT_SEL, &template->config.output_sel);
   CS_WRITE(stream, TA_STATE_VARYING0, base->varying_word[0]);
   CS_WRITE(stream, TA_STATE_VARYING1, base->varying_word[1]);
   CS_WRITE(stream, TA_STATE_VARYING2, base->varying_word[2]);
   CS_WRITE(stream, TA_STATE_PPP_CTRL, base->ppp_ctrl);
   CS_WRITE(stream, TA_STATE_STREAM_OUT0, base->stream_out0);

   assert((uint64_t)(stream - (uint32_t *)pvr_bo->bo->map) == dword_count);

   pvr_bo_cpu_unmap(device, pvr_bo);
   stream = NULL;

#undef CS_PACK_WRITE
#undef CS_WRITE

   pvr_csb_emit (csb, VDMCTRL_PPP_STATE0, state) {
      state.word_count = dword_count;
      state.addrmsb = pvr_bo->vma->dev_addr;
   }

   pvr_csb_emit (csb, VDMCTRL_PPP_STATE1, state) {
      state.addrlsb = pvr_bo->vma->dev_addr;
   }

   *pvr_bo_out = pvr_bo;

   return VK_SUCCESS;
}

static void pvr_device_setup_graphics_static_clear_vdm_state(
   const struct pvr_device_info *const dev_info,
   const struct pvr_pds_upload *const program,
   uint32_t temps,
   uint32_t index_count,
   uint32_t vs_output_size_in_bytes,
   uint32_t state_buffer[const static PVR_CLEAR_VDM_STATE_DWORD_COUNT])
{
   const uint32_t vs_output_size =
      DIV_ROUND_UP(vs_output_size_in_bytes,
                   PVRX(VDMCTRL_VDM_STATE4_VS_OUTPUT_SIZE_UNIT_SIZE));
   uint32_t *stream = state_buffer;
   uint32_t max_instances;
   uint32_t cam_size;

   pvr_calculate_vertex_cam_size(dev_info,
                                 vs_output_size,
                                 true,
                                 &cam_size,
                                 &max_instances);

   pvr_csb_pack (stream, VDMCTRL_VDM_STATE0, state0) {
      state0.vs_data_addr_present = true;
      state0.vs_other_present = true;
      state0.cam_size = cam_size;
      state0.uvs_scratch_size_select =
         PVRX(VDMCTRL_UVS_SCRATCH_SIZE_SELECT_FIVE);
      state0.flatshade_control = PVRX(VDMCTRL_FLATSHADE_CONTROL_VERTEX_0);
   }
   stream += pvr_cmd_length(VDMCTRL_VDM_STATE0);

   pvr_csb_pack (stream, VDMCTRL_VDM_STATE2, state2) {
      state2.vs_pds_data_base_addr = PVR_DEV_ADDR(program->data_offset);
   }
   stream += pvr_cmd_length(VDMCTRL_VDM_STATE2);

   pvr_csb_pack (stream, VDMCTRL_VDM_STATE3, state3) {
      state3.vs_pds_code_base_addr = PVR_DEV_ADDR(program->code_offset);
   }
   stream += pvr_cmd_length(VDMCTRL_VDM_STATE3);

   pvr_csb_pack (stream, VDMCTRL_VDM_STATE4, state4) {
      state4.vs_output_size = vs_output_size;
   }
   stream += pvr_cmd_length(VDMCTRL_VDM_STATE4);

   pvr_csb_pack (stream, VDMCTRL_VDM_STATE5, state5) {
      state5.vs_max_instances = max_instances;
      /* TODO: Where does the 3 * sizeof(uint32_t) come from? */
      state5.vs_usc_unified_size =
         DIV_ROUND_UP(3 * sizeof(uint32_t),
                      PVRX(VDMCTRL_VDM_STATE5_VS_USC_UNIFIED_SIZE_UNIT_SIZE));
      state5.vs_pds_temp_size =
         DIV_ROUND_UP(temps,
                      PVRX(VDMCTRL_VDM_STATE5_VS_PDS_TEMP_SIZE_UNIT_SIZE));
      state5.vs_pds_data_size =
         DIV_ROUND_UP(program->data_size << 2,
                      PVRX(VDMCTRL_VDM_STATE5_VS_PDS_DATA_SIZE_UNIT_SIZE));
   }
   stream += pvr_cmd_length(VDMCTRL_VDM_STATE5);

   pvr_csb_pack (stream, VDMCTRL_INDEX_LIST0, index_list0) {
      index_list0.index_count_present = true;
      index_list0.primitive_topology =
         PVRX(VDMCTRL_PRIMITIVE_TOPOLOGY_TRI_STRIP);
   }
   stream += pvr_cmd_length(VDMCTRL_INDEX_LIST0);

   pvr_csb_pack (stream, VDMCTRL_INDEX_LIST2, index_list3) {
      index_list3.index_count = index_count;
   }
   stream += pvr_cmd_length(VDMCTRL_INDEX_LIST2);

   assert((uint64_t)(stream - state_buffer) == PVR_CLEAR_VDM_STATE_DWORD_COUNT);
}

static VkResult
pvr_device_init_graphics_static_clear_state(struct pvr_device *device)
{
   struct pvr_device_static_clear_state *state = &device->static_clear_state;
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   const uint32_t cache_line_size = rogue_get_slc_cache_line_size(dev_info);
   const float vf_x_max = (float)rogue_get_param_vf_max_x(dev_info);
   const float vf_y_max = (float)rogue_get_param_vf_max_y(dev_info);
   const struct rogue_shader_binary *passthrough_vert_shader;
   struct pvr_pds_vertex_shader_program pds_program;
   size_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   const float vertices[4][3] = { { 0.0f, 0.0f, 0.0f },
                                  { vf_x_max, 0.0f, 0.0f },
                                  { 0.0f, vf_y_max, 0.0f },
                                  { vf_x_max, vf_y_max, 0.0f } };

   pvr_hard_code_get_passthrough_vertex_shader(dev_info,
                                               &passthrough_vert_shader);

   result = pvr_gpu_upload_usc(device,
                               passthrough_vert_shader->data,
                               passthrough_vert_shader->size,
                               cache_line_size,
                               &state->usc_vertex_shader_bo);
   if (result != VK_SUCCESS)
      return result;

   result = pvr_gpu_upload(device,
                           device->heaps.general_heap,
                           vertices,
                           sizeof(vertices),
                           sizeof(vertices[0][0]),
                           &state->vertices_bo);
   if (result != VK_SUCCESS)
      goto err_free_usc_shader;

   pds_program = (struct pvr_pds_vertex_shader_program) {
      .num_streams = 1,
      .streams = {
         [0] = {
            .address = state->vertices_bo->vma->dev_addr.addr,
            .stride = sizeof(vertices[0]),
            .num_elements = 1,
            .elements = {
               [0] = {
                  .size = sizeof(vertices[0]),
               },
            },
         },
      },
   };

   pvr_pds_setup_doutu(&pds_program.usc_task_control,
                       state->usc_vertex_shader_bo->vma->dev_addr.addr,
                       0,
                       PVRX(PDSINST_DOUTU_SAMPLE_RATE_INSTANCE),
                       false);

   pvr_pds_vertex_shader(&pds_program, NULL, PDS_GENERATE_SIZES, dev_info);

   staging_buffer_size =
      (pds_program.code_size + pds_program.data_size) * sizeof(*staging_buffer);

   staging_buffer = vk_alloc(&device->vk.alloc,
                             staging_buffer_size,
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer)
      goto err_free_verices_buffer;

   pvr_pds_vertex_shader(&pds_program,
                         staging_buffer,
                         PDS_GENERATE_DATA_SEGMENT,
                         dev_info);
   pvr_pds_vertex_shader(&pds_program,
                         &staging_buffer[pds_program.data_size],
                         PDS_GENERATE_CODE_SEGMENT,
                         dev_info);

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               &staging_buffer[0],
                               pds_program.data_size,
                               16,
                               &staging_buffer[pds_program.data_size],
                               pds_program.code_size,
                               16,
                               16,
                               &state->pds);
   if (result != VK_SUCCESS)
      goto err_free_staging_buffer;

   vk_free(&device->vk.alloc, staging_buffer);

   pvr_device_setup_graphics_static_clear_ppp_base(&state->ppp_base);
   pvr_device_setup_graphics_static_clear_ppp_templates(state->ppp_templates);

   assert(pds_program.code_size <= state->pds.code_size);

   /* TODO: The difference between the large and normal words is only the last
    * word. The value is 3 or 4 depending on the amount of indices. Should we
    * dedup this?
    */

   /* TODO: Figure out where the 4 * sizeof(uint32_t) comes from. */
   pvr_device_setup_graphics_static_clear_vdm_state(&device->pdevice->dev_info,
                                                    &state->pds,
                                                    pds_program.temps_used,
                                                    3,
                                                    4 * sizeof(uint32_t),
                                                    state->vdm_words);

   /* TODO: Figure out where the 4 * sizeof(uint32_t) comes from. */
   pvr_device_setup_graphics_static_clear_vdm_state(
      &device->pdevice->dev_info,
      &state->pds,
      pds_program.temps_used,
      4,
      4 * sizeof(uint32_t),
      state->large_clear_vdm_words);

   return VK_SUCCESS;

err_free_staging_buffer:
   vk_free(&device->vk.alloc, staging_buffer);

err_free_verices_buffer:
   pvr_bo_free(device, state->vertices_bo);

err_free_usc_shader:
   pvr_bo_free(device, state->usc_vertex_shader_bo);

   return result;
}

static void
pvr_device_finish_graphics_static_clear_state(struct pvr_device *device)
{
   struct pvr_device_static_clear_state *state = &device->static_clear_state;

   pvr_bo_free(device, state->pds.pvr_bo);
   pvr_bo_free(device, state->vertices_bo);
   pvr_bo_free(device, state->usc_vertex_shader_bo);
}

/* FIXME: We should be calculating the size when we upload the code in
 * pvr_srv_setup_static_pixel_event_program().
 */
static void pvr_device_get_pixel_event_pds_program_data_size(
   const struct pvr_device_info *dev_info,
   uint32_t *const data_size_in_dwords_out)
{
   struct pvr_pds_event_program program = {
      /* No data to DMA, just a DOUTU needed. */
      .num_emit_word_pairs = 0,
   };

   pvr_pds_set_sizes_pixel_event(&program, dev_info);

   *data_size_in_dwords_out = program.data_size;
}

static VkResult pvr_device_init_nop_program(struct pvr_device *device)
{
   const uint32_t cache_line_size =
      rogue_get_slc_cache_line_size(&device->pdevice->dev_info);
   struct pvr_pds_kickusc_program program = { 0 };
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   result = pvr_gpu_upload_usc(device,
                               pvr_nop_usc_code,
                               sizeof(pvr_nop_usc_code),
                               cache_line_size,
                               &device->nop_program.usc);
   if (result != VK_SUCCESS)
      return result;

   /* Setup a PDS program that kicks the static USC program. */
   pvr_pds_setup_doutu(&program.usc_task_control,
                       device->nop_program.usc->vma->dev_addr.addr,
                       0U,
                       PVRX(PDSINST_DOUTU_SAMPLE_RATE_INSTANCE),
                       false);

   pvr_pds_set_sizes_pixel_shader(&program);

   staging_buffer_size =
      (program.code_size + program.data_size) * sizeof(*staging_buffer);

   staging_buffer = vk_alloc(&device->vk.alloc,
                             staging_buffer_size,
                             8U,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_nop_usc_bo;
   }

   pvr_pds_generate_pixel_shader_program(&program, staging_buffer);

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               staging_buffer,
                               program.data_size,
                               16U,
                               &staging_buffer[program.data_size],
                               program.code_size,
                               16U,
                               16U,
                               &device->nop_program.pds);
   if (result != VK_SUCCESS)
      goto err_free_staging_buffer;

   vk_free(&device->vk.alloc, staging_buffer);

   return VK_SUCCESS;

err_free_staging_buffer:
   vk_free(&device->vk.alloc, staging_buffer);

err_free_nop_usc_bo:
   pvr_bo_free(device, device->nop_program.usc);

   return result;
}

static void pvr_device_init_default_sampler_state(struct pvr_device *device)
{
   pvr_csb_pack (&device->input_attachment_sampler, TEXSTATE_SAMPLER, sampler) {
      sampler.addrmode_u = PVRX(TEXSTATE_ADDRMODE_CLAMP_TO_EDGE);
      sampler.addrmode_v = PVRX(TEXSTATE_ADDRMODE_CLAMP_TO_EDGE);
      sampler.addrmode_w = PVRX(TEXSTATE_ADDRMODE_CLAMP_TO_EDGE);
      sampler.dadjust = PVRX(TEXSTATE_DADJUST_ZERO_UINT);
      sampler.magfilter = PVRX(TEXSTATE_FILTER_POINT);
      sampler.minfilter = PVRX(TEXSTATE_FILTER_POINT);
      sampler.anisoctl = PVRX(TEXSTATE_ANISOCTL_DISABLED);
      sampler.non_normalized_coords = true;
   }
}

VkResult pvr_CreateDevice(VkPhysicalDevice physicalDevice,
                          const VkDeviceCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkDevice *pDevice)
{
   PVR_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);
   struct pvr_instance *instance = pdevice->instance;
   struct vk_device_dispatch_table dispatch_table;
   struct pvr_device *device;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);

   device = vk_alloc2(&pdevice->vk.instance->alloc,
                      pAllocator,
                      sizeof(*device),
                      8,
                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device)
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &pvr_device_entrypoints,
                                             true);

   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &wsi_device_entrypoints,
                                             false);

   result = vk_device_init(&device->vk,
                           &pdevice->vk,
                           &dispatch_table,
                           pCreateInfo,
                           pAllocator);
   if (result != VK_SUCCESS)
      goto err_free_device;

   device->render_fd = open(pdevice->render_path, O_RDWR | O_CLOEXEC);
   if (device->render_fd < 0) {
      result = vk_errorf(instance,
                         VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to open device %s",
                         pdevice->render_path);
      goto err_vk_device_finish;
   }

   if (pdevice->master_path)
      device->master_fd = open(pdevice->master_path, O_RDWR | O_CLOEXEC);
   else
      device->master_fd = -1;

   vk_device_set_drm_fd(&device->vk, device->render_fd);

   device->instance = instance;
   device->pdevice = pdevice;

   device->ws = pvr_winsys_create(device->master_fd,
                                  device->render_fd,
                                  &device->vk.alloc);
   if (!device->ws) {
      result = VK_ERROR_INITIALIZATION_FAILED;
      goto err_close_master_fd;
   }

   device->ws->ops->get_heaps_info(device->ws, &device->heaps);

   result = pvr_free_list_create(device,
                                 PVR_GLOBAL_FREE_LIST_INITIAL_SIZE,
                                 PVR_GLOBAL_FREE_LIST_MAX_SIZE,
                                 PVR_GLOBAL_FREE_LIST_GROW_SIZE,
                                 PVR_GLOBAL_FREE_LIST_GROW_THRESHOLD,
                                 NULL /* parent_free_list */,
                                 &device->global_free_list);
   if (result != VK_SUCCESS)
      goto err_pvr_winsys_destroy;

   result = pvr_device_init_nop_program(device);
   if (result != VK_SUCCESS)
      goto err_pvr_free_list_destroy;

   result = pvr_device_init_compute_fence_program(device);
   if (result != VK_SUCCESS)
      goto err_pvr_free_nop_program;

   result = pvr_device_init_compute_idfwdf_state(device);
   if (result != VK_SUCCESS)
      goto err_pvr_free_compute_fence;

   result = pvr_device_init_graphics_static_clear_state(device);
   if (result != VK_SUCCESS)
      goto err_pvr_finish_compute_idfwdf;

   result = pvr_queues_create(device, pCreateInfo);
   if (result != VK_SUCCESS)
      goto err_pvr_finish_graphics_static_clear;

   pvr_device_init_default_sampler_state(device);

   if (pCreateInfo->pEnabledFeatures)
      memcpy(&device->features,
             pCreateInfo->pEnabledFeatures,
             sizeof(device->features));

   /* FIXME: Move this to a later stage and possibly somewhere other than
    * pvr_device. The purpose of this is so that we don't have to get the size
    * on each kick.
    */
   pvr_device_get_pixel_event_pds_program_data_size(
      &pdevice->dev_info,
      &device->pixel_event_data_size_in_dwords);

   device->global_queue_job_count = 0;
   device->global_queue_present_count = 0;

   *pDevice = pvr_device_to_handle(device);

   return VK_SUCCESS;

err_pvr_finish_graphics_static_clear:
   pvr_device_finish_graphics_static_clear_state(device);

err_pvr_finish_compute_idfwdf:
   pvr_device_finish_compute_idfwdf_state(device);

err_pvr_free_compute_fence:
   pvr_bo_free(device, device->pds_compute_fence_program.pvr_bo);

err_pvr_free_nop_program:
   pvr_bo_free(device, device->nop_program.pds.pvr_bo);
   pvr_bo_free(device, device->nop_program.usc);

err_pvr_free_list_destroy:
   pvr_free_list_destroy(device->global_free_list);

err_pvr_winsys_destroy:
   pvr_winsys_destroy(device->ws);

err_close_master_fd:
   if (device->master_fd >= 0)
      close(device->master_fd);

   close(device->render_fd);

err_vk_device_finish:
   vk_device_finish(&device->vk);

err_free_device:
   vk_free(&device->vk.alloc, device);

   return result;
}

void pvr_DestroyDevice(VkDevice _device,
                       const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);

   pvr_queues_destroy(device);
   pvr_device_finish_graphics_static_clear_state(device);
   pvr_device_finish_compute_idfwdf_state(device);
   pvr_bo_free(device, device->pds_compute_fence_program.pvr_bo);
   pvr_bo_free(device, device->nop_program.pds.pvr_bo);
   pvr_bo_free(device, device->nop_program.usc);
   pvr_free_list_destroy(device->global_free_list);
   pvr_winsys_destroy(device->ws);

   if (device->master_fd >= 0)
      close(device->master_fd);

   close(device->render_fd);
   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}

VkResult pvr_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                              VkLayerProperties *pProperties)
{
   if (!pProperties) {
      *pPropertyCount = 0;
      return VK_SUCCESS;
   }

   return vk_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);
}

VkResult pvr_AllocateMemory(VkDevice _device,
                            const VkMemoryAllocateInfo *pAllocateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkDeviceMemory *pMem)
{
   const VkImportMemoryFdInfoKHR *fd_info = NULL;
   PVR_FROM_HANDLE(pvr_device, device, _device);
   enum pvr_winsys_bo_type type = PVR_WINSYS_BO_TYPE_GPU;
   struct pvr_device_memory *mem;
   VkResult result;

   assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
   assert(pAllocateInfo->allocationSize > 0);

   mem = vk_object_alloc(&device->vk,
                         pAllocator,
                         sizeof(*mem),
                         VK_OBJECT_TYPE_DEVICE_MEMORY);
   if (!mem)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_foreach_struct_const (ext, pAllocateInfo->pNext) {
      switch ((unsigned)ext->sType) {
      case VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA:
         type = PVR_WINSYS_BO_TYPE_DISPLAY;
         break;
      case VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR:
         fd_info = (void *)ext;
         break;
      default:
         pvr_debug_ignored_stype(ext->sType);
         break;
      }
   }

   if (fd_info && fd_info->handleType) {
      VkDeviceSize aligned_alloc_size =
         ALIGN_POT(pAllocateInfo->allocationSize, device->ws->page_size);

      assert(
         fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
         fd_info->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

      result = device->ws->ops->buffer_create_from_fd(device->ws,
                                                      fd_info->fd,
                                                      &mem->bo);
      if (result != VK_SUCCESS)
         goto err_vk_object_free_mem;

      /* For security purposes, we reject importing the bo if it's smaller
       * than the requested allocation size. This prevents a malicious client
       * from passing a buffer to a trusted client, lying about the size, and
       * telling the trusted client to try and texture from an image that goes
       * out-of-bounds. This sort of thing could lead to GPU hangs or worse
       * in the trusted client. The trusted client can protect itself against
       * this sort of attack but only if it can trust the buffer size.
       */
      if (aligned_alloc_size > mem->bo->size) {
         result = vk_errorf(device,
                            VK_ERROR_INVALID_EXTERNAL_HANDLE,
                            "Aligned requested size too large for the given fd "
                            "%" PRIu64 "B > %" PRIu64 "B",
                            pAllocateInfo->allocationSize,
                            mem->bo->size);
         device->ws->ops->buffer_destroy(mem->bo);
         goto err_vk_object_free_mem;
      }

      /* From the Vulkan spec:
       *
       *    "Importing memory from a file descriptor transfers ownership of
       *    the file descriptor from the application to the Vulkan
       *    implementation. The application must not perform any operations on
       *    the file descriptor after a successful import."
       *
       * If the import fails, we leave the file descriptor open.
       */
      close(fd_info->fd);
   } else {
      /* Align physical allocations to the page size of the heap that will be
       * used when binding device memory (see pvr_bind_memory()) to ensure the
       * entire allocation can be mapped.
       */
      const uint64_t alignment = device->heaps.general_heap->page_size;

      /* FIXME: Need to determine the flags based on
       * device->pdevice->memory.memoryTypes[pAllocateInfo->memoryTypeIndex].propertyFlags.
       *
       * The alternative would be to store the flags alongside the memory
       * types as an array that's indexed by pAllocateInfo->memoryTypeIndex so
       * that they can be looked up.
       */
      result = device->ws->ops->buffer_create(device->ws,
                                              pAllocateInfo->allocationSize,
                                              alignment,
                                              type,
                                              PVR_WINSYS_BO_FLAG_CPU_ACCESS,
                                              &mem->bo);
      if (result != VK_SUCCESS)
         goto err_vk_object_free_mem;
   }

   *pMem = pvr_device_memory_to_handle(mem);

   return VK_SUCCESS;

err_vk_object_free_mem:
   vk_object_free(&device->vk, pAllocator, mem);

   return result;
}

VkResult pvr_GetMemoryFdKHR(VkDevice _device,
                            const VkMemoryGetFdInfoKHR *pGetFdInfo,
                            int *pFd)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_device_memory, mem, pGetFdInfo->memory);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR);

   assert(
      pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
      pGetFdInfo->handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT);

   return device->ws->ops->buffer_get_fd(mem->bo, pFd);
}

VkResult
pvr_GetMemoryFdPropertiesKHR(VkDevice _device,
                             VkExternalMemoryHandleTypeFlagBits handleType,
                             int fd,
                             VkMemoryFdPropertiesKHR *pMemoryFdProperties)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      /* FIXME: This should only allow memory types having
       * VK_MEMORY_PROPERTY_HOST_CACHED_BIT flag set, as
       * dma-buf should be imported using cacheable memory types,
       * given exporter's mmap will always map it as cacheable.
       * Ref:
       * https://www.kernel.org/doc/html/latest/driver-api/dma-buf.html#c.dma_buf_ops
       */
      pMemoryFdProperties->memoryTypeBits =
         (1 << device->pdevice->memory.memoryTypeCount) - 1;
      return VK_SUCCESS;
   default:
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }
}

void pvr_FreeMemory(VkDevice _device,
                    VkDeviceMemory _mem,
                    const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_device_memory, mem, _mem);

   if (!mem)
      return;

   device->ws->ops->buffer_destroy(mem->bo);

   vk_object_free(&device->vk, pAllocator, mem);
}

VkResult pvr_MapMemory(VkDevice _device,
                       VkDeviceMemory _memory,
                       VkDeviceSize offset,
                       VkDeviceSize size,
                       VkMemoryMapFlags flags,
                       void **ppData)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_device_memory, mem, _memory);
   void *map;

   if (!mem) {
      *ppData = NULL;
      return VK_SUCCESS;
   }

   if (size == VK_WHOLE_SIZE)
      size = mem->bo->size - offset;

   /* From the Vulkan spec version 1.0.32 docs for MapMemory:
    *
    *  * If size is not equal to VK_WHOLE_SIZE, size must be greater than 0
    *    assert(size != 0);
    *  * If size is not equal to VK_WHOLE_SIZE, size must be less than or
    *    equal to the size of the memory minus offset
    */

   assert(size > 0);
   assert(offset + size <= mem->bo->size);

   /* Check if already mapped */
   if (mem->bo->map) {
      *ppData = mem->bo->map + offset;
      return VK_SUCCESS;
   }

   /* Map it all at once */
   map = device->ws->ops->buffer_map(mem->bo);
   if (!map)
      return vk_error(device, VK_ERROR_MEMORY_MAP_FAILED);

   *ppData = map + offset;

   return VK_SUCCESS;
}

void pvr_UnmapMemory(VkDevice _device, VkDeviceMemory _memory)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_device_memory, mem, _memory);

   if (!mem || !mem->bo->map)
      return;

   device->ws->ops->buffer_unmap(mem->bo);
}

VkResult pvr_FlushMappedMemoryRanges(VkDevice _device,
                                     uint32_t memoryRangeCount,
                                     const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

VkResult
pvr_InvalidateMappedMemoryRanges(VkDevice _device,
                                 uint32_t memoryRangeCount,
                                 const VkMappedMemoryRange *pMemoryRanges)
{
   return VK_SUCCESS;
}

void pvr_GetImageSparseMemoryRequirements2(
   VkDevice device,
   const VkImageSparseMemoryRequirementsInfo2 *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   *pSparseMemoryRequirementCount = 0;
}

void pvr_GetDeviceMemoryCommitment(VkDevice device,
                                   VkDeviceMemory memory,
                                   VkDeviceSize *pCommittedMemoryInBytes)
{
   *pCommittedMemoryInBytes = 0;
}

VkResult pvr_bind_memory(struct pvr_device *device,
                         struct pvr_device_memory *mem,
                         VkDeviceSize offset,
                         VkDeviceSize size,
                         VkDeviceSize alignment,
                         struct pvr_winsys_vma **const vma_out,
                         pvr_dev_addr_t *const dev_addr_out)
{
   VkDeviceSize virt_size =
      size + (offset & (device->heaps.general_heap->page_size - 1));
   struct pvr_winsys_vma *vma;
   pvr_dev_addr_t dev_addr;

   /* Valid usage:
    *
    *   "memoryOffset must be an integer multiple of the alignment member of
    *    the VkMemoryRequirements structure returned from a call to
    *    vkGetBufferMemoryRequirements with buffer"
    *
    *   "memoryOffset must be an integer multiple of the alignment member of
    *    the VkMemoryRequirements structure returned from a call to
    *    vkGetImageMemoryRequirements with image"
    */
   assert(offset % alignment == 0);
   assert(offset < mem->bo->size);

   vma = device->ws->ops->heap_alloc(device->heaps.general_heap,
                                     virt_size,
                                     alignment);
   if (!vma)
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   dev_addr = device->ws->ops->vma_map(vma, mem->bo, offset, size);
   if (!dev_addr.addr) {
      device->ws->ops->heap_free(vma);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   *dev_addr_out = dev_addr;
   *vma_out = vma;

   return VK_SUCCESS;
}

void pvr_unbind_memory(struct pvr_device *device, struct pvr_winsys_vma *vma)
{
   device->ws->ops->vma_unmap(vma);
   device->ws->ops->heap_free(vma);
}

VkResult pvr_BindBufferMemory2(VkDevice _device,
                               uint32_t bindInfoCount,
                               const VkBindBufferMemoryInfo *pBindInfos)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   uint32_t i;

   for (i = 0; i < bindInfoCount; i++) {
      PVR_FROM_HANDLE(pvr_device_memory, mem, pBindInfos[i].memory);
      PVR_FROM_HANDLE(pvr_buffer, buffer, pBindInfos[i].buffer);

      VkResult result = pvr_bind_memory(device,
                                        mem,
                                        pBindInfos[i].memoryOffset,
                                        buffer->vk.size,
                                        buffer->alignment,
                                        &buffer->vma,
                                        &buffer->dev_addr);
      if (result != VK_SUCCESS) {
         while (i--) {
            PVR_FROM_HANDLE(pvr_buffer, buffer, pBindInfos[i].buffer);
            pvr_unbind_memory(device, buffer->vma);
         }

         return result;
      }
   }

   return VK_SUCCESS;
}

VkResult pvr_QueueBindSparse(VkQueue _queue,
                             uint32_t bindInfoCount,
                             const VkBindSparseInfo *pBindInfo,
                             VkFence fence)
{
   return VK_SUCCESS;
}

/* Event functions. */

VkResult pvr_CreateEvent(VkDevice _device,
                         const VkEventCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkEvent *pEvent)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);

   struct pvr_event *event = vk_object_alloc(&device->vk,
                                             pAllocator,
                                             sizeof(*event),
                                             VK_OBJECT_TYPE_EVENT);
   if (!event)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   event->sync = NULL;
   event->state = PVR_EVENT_STATE_RESET_BY_HOST;

   *pEvent = pvr_event_to_handle(event);

   return VK_SUCCESS;
}

void pvr_DestroyEvent(VkDevice _device,
                      VkEvent _event,
                      const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_event, event, _event);

   if (!event)
      return;

   if (event->sync)
      vk_sync_destroy(&device->vk, event->sync);

   vk_object_free(&device->vk, pAllocator, event);
}

VkResult pvr_GetEventStatus(VkDevice _device, VkEvent _event)
{
   assert(!"Unimplemented");
   return VK_SUCCESS;
}

VkResult pvr_SetEvent(VkDevice _device, VkEvent _event)
{
   assert(!"Unimplemented");
   return VK_SUCCESS;
}

VkResult pvr_ResetEvent(VkDevice _device, VkEvent _event)
{
   assert(!"Unimplemented");
   return VK_SUCCESS;
}

/* Buffer functions. */

VkResult pvr_CreateBuffer(VkDevice _device,
                          const VkBufferCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkBuffer *pBuffer)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   const uint32_t alignment = 4096;
   struct pvr_buffer *buffer;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
   assert(pCreateInfo->usage != 0);

   /* We check against (ULONG_MAX - alignment) to prevent overflow issues */
   if (pCreateInfo->size >= ULONG_MAX - alignment)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   buffer =
      vk_buffer_create(&device->vk, pCreateInfo, pAllocator, sizeof(*buffer));
   if (!buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   buffer->alignment = alignment;

   *pBuffer = pvr_buffer_to_handle(buffer);

   return VK_SUCCESS;
}

void pvr_DestroyBuffer(VkDevice _device,
                       VkBuffer _buffer,
                       const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_buffer, buffer, _buffer);

   if (!buffer)
      return;

   if (buffer->vma)
      pvr_unbind_memory(device, buffer->vma);

   vk_buffer_destroy(&device->vk, pAllocator, &buffer->vk);
}

VkResult pvr_gpu_upload(struct pvr_device *device,
                        struct pvr_winsys_heap *heap,
                        const void *data,
                        size_t size,
                        uint64_t alignment,
                        struct pvr_bo **const pvr_bo_out)
{
   struct pvr_bo *pvr_bo = NULL;
   VkResult result;

   assert(size > 0);

   result = pvr_bo_alloc(device,
                         heap,
                         size,
                         alignment,
                         PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                         &pvr_bo);
   if (result != VK_SUCCESS)
      return result;

   memcpy(pvr_bo->bo->map, data, size);
   pvr_bo_cpu_unmap(device, pvr_bo);

   *pvr_bo_out = pvr_bo;

   return VK_SUCCESS;
}

VkResult pvr_gpu_upload_usc(struct pvr_device *device,
                            const void *code,
                            size_t code_size,
                            uint64_t code_alignment,
                            struct pvr_bo **const pvr_bo_out)
{
   struct pvr_bo *pvr_bo = NULL;
   VkResult result;

   assert(code_size > 0);

   /* The USC will prefetch the next instruction, so over allocate by 1
    * instruction to prevent reading off the end of a page into a potentially
    * unallocated page.
    */
   result = pvr_bo_alloc(device,
                         device->heaps.usc_heap,
                         code_size + ROGUE_MAX_INSTR_BYTES,
                         code_alignment,
                         PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                         &pvr_bo);
   if (result != VK_SUCCESS)
      return result;

   memcpy(pvr_bo->bo->map, code, code_size);
   pvr_bo_cpu_unmap(device, pvr_bo);

   *pvr_bo_out = pvr_bo;

   return VK_SUCCESS;
}

/**
 * \brief Upload PDS program data and code segments from host memory to device
 * memory.
 *
 * \param[in] device            Logical device pointer.
 * \param[in] data              Pointer to PDS data segment to upload.
 * \param[in] data_size_dwords  Size of PDS data segment in dwords.
 * \param[in] data_alignment    Required alignment of the PDS data segment in
 *                              bytes. Must be a power of two.
 * \param[in] code              Pointer to PDS code segment to upload.
 * \param[in] code_size_dwords  Size of PDS code segment in dwords.
 * \param[in] code_alignment    Required alignment of the PDS code segment in
 *                              bytes. Must be a power of two.
 * \param[in] min_alignment     Minimum alignment of the bo holding the PDS
 *                              program in bytes.
 * \param[out] pds_upload_out   On success will be initialized based on the
 *                              uploaded PDS program.
 * \return VK_SUCCESS on success, or error code otherwise.
 */
VkResult pvr_gpu_upload_pds(struct pvr_device *device,
                            const uint32_t *data,
                            uint32_t data_size_dwords,
                            uint32_t data_alignment,
                            const uint32_t *code,
                            uint32_t code_size_dwords,
                            uint32_t code_alignment,
                            uint64_t min_alignment,
                            struct pvr_pds_upload *const pds_upload_out)
{
   /* All alignment and sizes below are in bytes. */
   const size_t data_size = data_size_dwords * sizeof(*data);
   const size_t code_size = code_size_dwords * sizeof(*code);
   const uint64_t data_aligned_size = ALIGN_POT(data_size, data_alignment);
   const uint64_t code_aligned_size = ALIGN_POT(code_size, code_alignment);
   const uint32_t code_offset = ALIGN_POT(data_aligned_size, code_alignment);
   const uint64_t bo_alignment = MAX2(min_alignment, data_alignment);
   const uint64_t bo_size = (!!code) ? (code_offset + code_aligned_size)
                                     : data_aligned_size;
   const uint64_t bo_flags = PVR_BO_ALLOC_FLAG_CPU_MAPPED |
                             PVR_BO_ALLOC_FLAG_ZERO_ON_ALLOC;
   VkResult result;

   assert(code || data);
   assert(!code || (code_size_dwords != 0 && code_alignment != 0));
   assert(!data || (data_size_dwords != 0 && data_alignment != 0));

   result = pvr_bo_alloc(device,
                         device->heaps.pds_heap,
                         bo_size,
                         bo_alignment,
                         bo_flags,
                         &pds_upload_out->pvr_bo);
   if (result != VK_SUCCESS)
      return result;

   if (data) {
      memcpy(pds_upload_out->pvr_bo->bo->map, data, data_size);

      pds_upload_out->data_offset = pds_upload_out->pvr_bo->vma->dev_addr.addr -
                                    device->heaps.pds_heap->base_addr.addr;

      /* Store data size in dwords. */
      assert(data_aligned_size % 4 == 0);
      pds_upload_out->data_size = data_aligned_size / 4;
   } else {
      pds_upload_out->data_offset = 0;
      pds_upload_out->data_size = 0;
   }

   if (code) {
      memcpy((uint8_t *)pds_upload_out->pvr_bo->bo->map + code_offset,
             code,
             code_size);

      pds_upload_out->code_offset =
         (pds_upload_out->pvr_bo->vma->dev_addr.addr + code_offset) -
         device->heaps.pds_heap->base_addr.addr;

      /* Store code size in dwords. */
      assert(code_aligned_size % 4 == 0);
      pds_upload_out->code_size = code_aligned_size / 4;
   } else {
      pds_upload_out->code_offset = 0;
      pds_upload_out->code_size = 0;
   }

   pvr_bo_cpu_unmap(device, pds_upload_out->pvr_bo);

   return VK_SUCCESS;
}

static VkResult
pvr_framebuffer_create_ppp_state(struct pvr_device *device,
                                 struct pvr_framebuffer *framebuffer)
{
   const uint32_t cache_line_size =
      rogue_get_slc_cache_line_size(&device->pdevice->dev_info);
   uint32_t ppp_state[3];
   VkResult result;

   pvr_csb_pack (&ppp_state[0], TA_STATE_HEADER, header) {
      header.pres_terminate = true;
   }

   pvr_csb_pack (&ppp_state[1], TA_STATE_TERMINATE0, term0) {
      term0.clip_right =
         DIV_ROUND_UP(
            framebuffer->width,
            PVRX(TA_STATE_TERMINATE0_CLIP_RIGHT_BLOCK_SIZE_IN_PIXELS)) -
         1;
      term0.clip_bottom =
         DIV_ROUND_UP(
            framebuffer->height,
            PVRX(TA_STATE_TERMINATE0_CLIP_BOTTOM_BLOCK_SIZE_IN_PIXELS)) -
         1;
   }

   pvr_csb_pack (&ppp_state[2], TA_STATE_TERMINATE1, term1) {
      term1.render_target = 0;
      term1.clip_left = 0;
   }

   result = pvr_gpu_upload(device,
                           device->heaps.general_heap,
                           ppp_state,
                           sizeof(ppp_state),
                           cache_line_size,
                           &framebuffer->ppp_state_bo);
   if (result != VK_SUCCESS)
      return result;

   /* Calculate the size of PPP state in dwords. */
   framebuffer->ppp_state_size = sizeof(ppp_state) / sizeof(uint32_t);

   return VK_SUCCESS;
}

static bool pvr_render_targets_init(struct pvr_render_target *render_targets,
                                    uint32_t render_targets_count)
{
   uint32_t i;

   for (i = 0; i < render_targets_count; i++) {
      if (pthread_mutex_init(&render_targets[i].mutex, NULL))
         goto err_mutex_destroy;
   }

   return true;

err_mutex_destroy:
   while (i--)
      pthread_mutex_destroy(&render_targets[i].mutex);

   return false;
}

static void pvr_render_targets_fini(struct pvr_render_target *render_targets,
                                    uint32_t render_targets_count)
{
   for (uint32_t i = 0; i < render_targets_count; i++) {
      if (render_targets[i].valid) {
         pvr_render_target_dataset_destroy(render_targets[i].rt_dataset);
         render_targets[i].valid = false;
      }

      pthread_mutex_destroy(&render_targets[i].mutex);
   }
}

VkResult pvr_CreateFramebuffer(VkDevice _device,
                               const VkFramebufferCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkFramebuffer *pFramebuffer)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_render_target *render_targets;
   struct pvr_framebuffer *framebuffer;
   struct pvr_image_view **attachments;
   uint32_t render_targets_count;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

   render_targets_count =
      PVR_RENDER_TARGETS_PER_FRAMEBUFFER(&device->pdevice->dev_info);

   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma, &framebuffer, __typeof__(*framebuffer), 1);
   vk_multialloc_add(&ma,
                     &attachments,
                     __typeof__(*attachments),
                     pCreateInfo->attachmentCount);
   vk_multialloc_add(&ma,
                     &render_targets,
                     __typeof__(*render_targets),
                     render_targets_count);

   if (!vk_multialloc_zalloc2(&ma,
                              &device->vk.alloc,
                              pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk,
                       &framebuffer->base,
                       VK_OBJECT_TYPE_FRAMEBUFFER);

   framebuffer->width = pCreateInfo->width;
   framebuffer->height = pCreateInfo->height;
   framebuffer->layers = pCreateInfo->layers;

   framebuffer->attachments = attachments;
   framebuffer->attachment_count = pCreateInfo->attachmentCount;
   for (uint32_t i = 0; i < framebuffer->attachment_count; i++) {
      framebuffer->attachments[i] =
         pvr_image_view_from_handle(pCreateInfo->pAttachments[i]);
   }

   result = pvr_framebuffer_create_ppp_state(device, framebuffer);
   if (result != VK_SUCCESS)
      goto err_free_framebuffer;

   framebuffer->render_targets = render_targets;
   framebuffer->render_targets_count = render_targets_count;
   if (!pvr_render_targets_init(framebuffer->render_targets,
                                render_targets_count)) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_ppp_state_bo;
   }

   *pFramebuffer = pvr_framebuffer_to_handle(framebuffer);

   return VK_SUCCESS;

err_free_ppp_state_bo:
   pvr_bo_free(device, framebuffer->ppp_state_bo);

err_free_framebuffer:
   vk_object_base_finish(&framebuffer->base);
   vk_free2(&device->vk.alloc, pAllocator, framebuffer);

   return result;
}

void pvr_DestroyFramebuffer(VkDevice _device,
                            VkFramebuffer _fb,
                            const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_framebuffer, framebuffer, _fb);

   if (!framebuffer)
      return;

   pvr_render_targets_fini(framebuffer->render_targets,
                           framebuffer->render_targets_count);
   pvr_bo_free(device, framebuffer->ppp_state_bo);
   vk_object_base_finish(&framebuffer->base);
   vk_free2(&device->vk.alloc, pAllocator, framebuffer);
}

PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion)
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
    *       - The ICD must statically expose no other Vulkan symbol unless it
    *         is linked with -Bsymbolic.
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
    */
   *pSupportedVersion = MIN2(*pSupportedVersion, 4u);
   return VK_SUCCESS;
}

static uint32_t
pvr_sampler_get_hw_filter_from_vk(const struct pvr_device_info *dev_info,
                                  VkFilter filter)
{
   switch (filter) {
   case VK_FILTER_NEAREST:
      return PVRX(TEXSTATE_FILTER_POINT);
   case VK_FILTER_LINEAR:
      return PVRX(TEXSTATE_FILTER_LINEAR);
   default:
      unreachable("Unknown filter type.");
   }
}

static uint32_t
pvr_sampler_get_hw_addr_mode_from_vk(VkSamplerAddressMode addr_mode)
{
   switch (addr_mode) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT:
      return PVRX(TEXSTATE_ADDRMODE_REPEAT);
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
      return PVRX(TEXSTATE_ADDRMODE_FLIP);
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
      return PVRX(TEXSTATE_ADDRMODE_CLAMP_TO_EDGE);
   case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
      return PVRX(TEXSTATE_ADDRMODE_FLIP_ONCE_THEN_CLAMP);
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
      return PVRX(TEXSTATE_ADDRMODE_CLAMP_TO_BORDER);
   default:
      unreachable("Invalid sampler address mode.");
   }
}

VkResult pvr_CreateSampler(VkDevice _device,
                           const VkSamplerCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkSampler *pSampler)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_sampler *sampler;
   float lod_rounding_bias;
   VkFilter min_filter;
   VkFilter mag_filter;
   float min_lod;
   float max_lod;

   STATIC_ASSERT(sizeof(((union pvr_sampler_descriptor *)NULL)->data) ==
                 sizeof(((union pvr_sampler_descriptor *)NULL)->words));

   sampler = vk_object_alloc(&device->vk,
                             pAllocator,
                             sizeof(*sampler),
                             VK_OBJECT_TYPE_SAMPLER);
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   mag_filter = pCreateInfo->magFilter;
   min_filter = pCreateInfo->minFilter;

   if (PVR_HAS_QUIRK(&device->pdevice->dev_info, 51025)) {
      /* The min/mag filters may need adjustment here, the GPU should decide
       * which of the two filters to use based on the clamped LOD value: LOD
       * <= 0 implies magnification, while LOD > 0 implies minification.
       *
       * As a workaround, we override magFilter with minFilter if we know that
       * the magnification filter will never be used due to clamping anyway
       * (i.e. minLod > 0). Conversely, we override minFilter with magFilter
       * if maxLod <= 0.
       */
      if (pCreateInfo->minLod > 0.0f) {
         /* The clamped LOD will always be positive => always minify. */
         mag_filter = pCreateInfo->minFilter;
      }

      if (pCreateInfo->maxLod <= 0.0f) {
         /* The clamped LOD will always be negative or zero => always
          * magnify.
          */
         min_filter = pCreateInfo->magFilter;
      }
   }

   if (pCreateInfo->compareEnable) {
      sampler->descriptor.data.compare_op =
         (uint32_t)pvr_texstate_cmpmode(pCreateInfo->compareOp);
   } else {
      sampler->descriptor.data.compare_op =
         (uint32_t)pvr_texstate_cmpmode(VK_COMPARE_OP_NEVER);
   }

   sampler->descriptor.data.word3 = 0;
   pvr_csb_pack (&sampler->descriptor.data.sampler_word,
                 TEXSTATE_SAMPLER,
                 word) {
      const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
      const float lod_clamp_max = (float)PVRX(TEXSTATE_CLAMP_MAX) /
                                  (1 << PVRX(TEXSTATE_CLAMP_FRACTIONAL_BITS));
      const float max_dadjust = ((float)(PVRX(TEXSTATE_DADJUST_MAX_UINT) -
                                         PVRX(TEXSTATE_DADJUST_ZERO_UINT))) /
                                (1 << PVRX(TEXSTATE_DADJUST_FRACTIONAL_BITS));
      const float min_dadjust = ((float)(PVRX(TEXSTATE_DADJUST_MIN_UINT) -
                                         PVRX(TEXSTATE_DADJUST_ZERO_UINT))) /
                                (1 << PVRX(TEXSTATE_DADJUST_FRACTIONAL_BITS));

      word.magfilter = pvr_sampler_get_hw_filter_from_vk(dev_info, mag_filter);
      word.minfilter = pvr_sampler_get_hw_filter_from_vk(dev_info, min_filter);

      if (pCreateInfo->mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR)
         word.mipfilter = true;

      word.addrmode_u =
         pvr_sampler_get_hw_addr_mode_from_vk(pCreateInfo->addressModeU);
      word.addrmode_v =
         pvr_sampler_get_hw_addr_mode_from_vk(pCreateInfo->addressModeV);
      word.addrmode_w =
         pvr_sampler_get_hw_addr_mode_from_vk(pCreateInfo->addressModeW);

      /* TODO: Figure out defines for these. */
      if (word.addrmode_u == PVRX(TEXSTATE_ADDRMODE_FLIP))
         sampler->descriptor.data.word3 |= 0x40000000;

      if (word.addrmode_v == PVRX(TEXSTATE_ADDRMODE_FLIP))
         sampler->descriptor.data.word3 |= 0x20000000;

      /* The Vulkan 1.0.205 spec says:
       *
       *    The absolute value of mipLodBias must be less than or equal to
       *    VkPhysicalDeviceLimits::maxSamplerLodBias.
       */
      word.dadjust =
         PVRX(TEXSTATE_DADJUST_ZERO_UINT) +
         util_signed_fixed(
            CLAMP(pCreateInfo->mipLodBias, min_dadjust, max_dadjust),
            PVRX(TEXSTATE_DADJUST_FRACTIONAL_BITS));

      /* Anisotropy is not supported for now. */
      word.anisoctl = PVRX(TEXSTATE_ANISOCTL_DISABLED);

      if (PVR_HAS_QUIRK(&device->pdevice->dev_info, 51025) &&
          pCreateInfo->mipmapMode == VK_SAMPLER_MIPMAP_MODE_NEAREST) {
         /* When MIPMAP_MODE_NEAREST is enabled, the LOD level should be
          * selected by adding 0.5 and then truncating the input LOD value.
          * This hardware adds the 0.5 bias before clamping against
          * lodmin/lodmax, while Vulkan specifies the bias to be added after
          * clamping. We compensate for this difference by adding the 0.5
          * bias to the LOD bounds, too.
          */
         lod_rounding_bias = 0.5f;
      } else {
         lod_rounding_bias = 0.0f;
      }

      min_lod = pCreateInfo->minLod + lod_rounding_bias;
      word.minlod = util_unsigned_fixed(CLAMP(min_lod, 0.0f, lod_clamp_max),
                                        PVRX(TEXSTATE_CLAMP_FRACTIONAL_BITS));

      max_lod = pCreateInfo->maxLod + lod_rounding_bias;
      word.maxlod = util_unsigned_fixed(CLAMP(max_lod, 0.0f, lod_clamp_max),
                                        PVRX(TEXSTATE_CLAMP_FRACTIONAL_BITS));

      word.bordercolor_index = pCreateInfo->borderColor;

      if (pCreateInfo->unnormalizedCoordinates)
         word.non_normalized_coords = true;
   }

   *pSampler = pvr_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

void pvr_DestroySampler(VkDevice _device,
                        VkSampler _sampler,
                        const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_sampler, sampler, _sampler);

   if (!sampler)
      return;

   vk_object_free(&device->vk, pAllocator, sampler);
}

void pvr_GetBufferMemoryRequirements2(
   VkDevice _device,
   const VkBufferMemoryRequirementsInfo2 *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   PVR_FROM_HANDLE(pvr_buffer, buffer, pInfo->buffer);
   PVR_FROM_HANDLE(pvr_device, device, _device);

   /* The Vulkan 1.0.166 spec says:
    *
    *    memoryTypeBits is a bitmask and contains one bit set for every
    *    supported memory type for the resource. Bit 'i' is set if and only
    *    if the memory type 'i' in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported for the resource.
    *
    * All types are currently supported for buffers.
    */
   pMemoryRequirements->memoryRequirements.memoryTypeBits =
      (1ul << device->pdevice->memory.memoryTypeCount) - 1;

   pMemoryRequirements->memoryRequirements.alignment = buffer->alignment;
   pMemoryRequirements->memoryRequirements.size =
      ALIGN_POT(buffer->vk.size, buffer->alignment);
}

void pvr_GetImageMemoryRequirements2(VkDevice _device,
                                     const VkImageMemoryRequirementsInfo2 *pInfo,
                                     VkMemoryRequirements2 *pMemoryRequirements)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_image, image, pInfo->image);

   /* The Vulkan 1.0.166 spec says:
    *
    *    memoryTypeBits is a bitmask and contains one bit set for every
    *    supported memory type for the resource. Bit 'i' is set if and only
    *    if the memory type 'i' in the VkPhysicalDeviceMemoryProperties
    *    structure for the physical device is supported for the resource.
    *
    * All types are currently supported for images.
    */
   const uint32_t memory_types =
      (1ul << device->pdevice->memory.memoryTypeCount) - 1;

   /* TODO: The returned size is aligned here in case of arrays/CEM (as is done
    * in GetImageMemoryRequirements()), but this should be known at image
    * creation time (pCreateInfo->arrayLayers > 1). This is confirmed in
    * ImageCreate()/ImageGetMipMapOffsetInBytes() where it aligns the size to
    * 4096 if pCreateInfo->arrayLayers > 1. So is the alignment here actually
    * necessary? If not, what should it be when pCreateInfo->arrayLayers == 1?
    *
    * Note: Presumably the 4096 alignment requirement comes from the Vulkan
    * driver setting RGX_CR_TPU_TAG_CEM_4K_FACE_PACKING_EN when setting up
    * render and compute jobs.
    */
   pMemoryRequirements->memoryRequirements.alignment = image->alignment;
   pMemoryRequirements->memoryRequirements.size =
      ALIGN(image->size, image->alignment);
   pMemoryRequirements->memoryRequirements.memoryTypeBits = memory_types;
}
