/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_device.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/sysmacros.h>

#include "util/disk_cache.h"
#include "git_sha1.h"

#include "vk_device.h"
#include "vk_drm_syncobj.h"
#include "vk_format.h"
#include "vk_limits.h"
#include "vk_log.h"
#include "vk_shader_module.h"
#include "vk_util.h"

#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"
#include "panvk_wsi.h"

#include "pan_format.h"
#include "pan_props.h"

#include "genxml/gen_macros.h"

#define ARM_VENDOR_ID        0x13b5
#define MAX_PUSH_DESCRIPTORS 32
/* We reserve one ubo for push constant, one for sysvals and one per-set for the
 * descriptor metadata  */
#define RESERVED_UBO_COUNT                   6
#define MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS 32 - RESERVED_UBO_COUNT
#define MAX_INLINE_UNIFORM_BLOCK_SIZE        (1 << 16)

static VkResult
create_kmod_dev(struct panvk_physical_device *device,
                const struct panvk_instance *instance, drmDevicePtr drm_device)
{
   const char *path = drm_device->nodes[DRM_NODE_RENDER];
   drmVersionPtr version;
   int fd;

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      return panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "failed to open device %s", path);
   }

   version = drmGetVersion(fd);
   if (!version) {
      close(fd);
      return panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                          "failed to query kernel driver version for device %s",
                          path);
   }

   if (strcmp(version->name, "panfrost") && strcmp(version->name, "panthor")) {
      drmFreeVersion(version);
      close(fd);
      return VK_ERROR_INCOMPATIBLE_DRIVER;
   }

   drmFreeVersion(version);

   if (instance->debug_flags & PANVK_DEBUG_STARTUP)
      vk_logi(VK_LOG_NO_OBJS(instance), "Found compatible device '%s'.", path);

   device->kmod.dev = pan_kmod_dev_create(fd, PAN_KMOD_DEV_FLAG_OWNS_FD,
                                          &instance->kmod.allocator);

   if (!device->kmod.dev) {
      close(fd);
      return panvk_errorf(instance, VK_ERROR_OUT_OF_HOST_MEMORY,
                          "cannot create device");
   }

   return VK_SUCCESS;
}

static VkResult
get_drm_device_ids(struct panvk_physical_device *device,
                   const struct panvk_instance *instance,
                   drmDevicePtr drm_device)
{
   struct stat st;

   if (stat(drm_device->nodes[DRM_NODE_RENDER], &st)) {
      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "failed to query render node stat");
   }

   device->drm.render_rdev = st.st_rdev;

   if (drm_device->available_nodes & (1 << DRM_NODE_PRIMARY)) {
      if (stat(drm_device->nodes[DRM_NODE_PRIMARY], &st)) {
         return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                          "failed to query primary node stat");
      }

      device->drm.primary_rdev = st.st_rdev;
   }

   return VK_SUCCESS;
}

static int
get_cache_uuid(uint16_t family, void *uuid)
{
   uint32_t mesa_timestamp;
   uint16_t f = family;

   if (!disk_cache_get_function_timestamp(get_cache_uuid, &mesa_timestamp))
      return -1;

   memset(uuid, 0, VK_UUID_SIZE);
   memcpy(uuid, &mesa_timestamp, 4);
   memcpy((char *)uuid + 4, &f, 2);
   snprintf((char *)uuid + 6, VK_UUID_SIZE - 10, "pan");
   return 0;
}

static VkResult
get_device_sync_types(struct panvk_physical_device *device,
                      const struct panvk_instance *instance)
{
   const unsigned arch = pan_arch(device->kmod.props.gpu_prod_id);
   uint32_t sync_type_count = 0;

   device->drm_syncobj_type = vk_drm_syncobj_get_type(device->kmod.dev->fd);
   if (!device->drm_syncobj_type.features) {
      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "failed to query syncobj features");
   }

   device->sync_types[sync_type_count++] = &device->drm_syncobj_type;

   if (arch >= 10) {
      assert(device->drm_syncobj_type.features & VK_SYNC_FEATURE_TIMELINE);
   } else {
      /* We don't support timelines in the uAPI yet and we don't want it getting
       * suddenly turned on by vk_drm_syncobj_get_type() without us adding panvk
       * code for it first.
       */
      device->drm_syncobj_type.features &= ~VK_SYNC_FEATURE_TIMELINE;

      /* vk_sync_timeline requires VK_SYNC_FEATURE_GPU_MULTI_WAIT.  Panfrost
       * waits on the underlying dma-fences and supports the feature.
       */
      device->drm_syncobj_type.features |= VK_SYNC_FEATURE_GPU_MULTI_WAIT;

      device->sync_timeline_type =
         vk_sync_timeline_get_type(&device->drm_syncobj_type);
      device->sync_types[sync_type_count++] = &device->sync_timeline_type.sync;
   }

   assert(sync_type_count < ARRAY_SIZE(device->sync_types));
   device->sync_types[sync_type_count] = NULL;

   return VK_SUCCESS;
}

static void
get_device_extensions(const struct panvk_physical_device *device,
                      struct vk_device_extension_table *ext)
{
   const unsigned arch = pan_arch(device->kmod.props.gpu_prod_id);

   *ext = (struct vk_device_extension_table){
      .KHR_8bit_storage = true,
      .KHR_16bit_storage = true,
      .KHR_bind_memory2 = true,
      .KHR_buffer_device_address = true,
      .KHR_copy_commands2 = true,
      .KHR_create_renderpass2 = true,
      .KHR_dedicated_allocation = true,
      .KHR_descriptor_update_template = true,
      .KHR_depth_stencil_resolve = true,
      .KHR_device_group = true,
      .KHR_driver_properties = true,
      .KHR_dynamic_rendering = true,
      .KHR_external_fence = true,
      .KHR_external_fence_fd = true,
      .KHR_external_memory = true,
      .KHR_external_memory_fd = true,
      .KHR_external_semaphore = true,
      .KHR_external_semaphore_fd = true,
      .KHR_get_memory_requirements2 = true,
      .KHR_global_priority = true,
      .KHR_image_format_list = true,
      .KHR_imageless_framebuffer = true,
      .KHR_index_type_uint8 = true,
      .KHR_maintenance1 = true,
      .KHR_maintenance2 = true,
      .KHR_maintenance3 = true,
      .KHR_map_memory2 = true,
      .KHR_multiview = arch >= 10,
      .KHR_pipeline_executable_properties = true,
      .KHR_pipeline_library = true,
      .KHR_push_descriptor = true,
      .KHR_relaxed_block_layout = true,
      .KHR_sampler_mirror_clamp_to_edge = true,
      .KHR_sampler_ycbcr_conversion = arch >= 10,
      .KHR_separate_depth_stencil_layouts = true,
      .KHR_shader_draw_parameters = true,
      .KHR_shader_expect_assume = true,
      .KHR_shader_float16_int8 = true,
      .KHR_shader_non_semantic_info = true,
      .KHR_shader_relaxed_extended_instruction = true,
      .KHR_shader_subgroup_rotate = true,
      .KHR_storage_buffer_storage_class = true,
#ifdef PANVK_USE_WSI_PLATFORM
      .KHR_swapchain = true,
#endif
      .KHR_synchronization2 = true,
      .KHR_timeline_semaphore = true,
      .KHR_uniform_buffer_standard_layout = true,
      .KHR_variable_pointers = true,
      .KHR_vertex_attribute_divisor = true,
      .KHR_zero_initialize_workgroup_memory = true,
      .EXT_4444_formats = true,
      .EXT_buffer_device_address = true,
      .EXT_custom_border_color = true,
      .EXT_depth_clip_enable = true,
      .EXT_external_memory_dma_buf = true,
      .EXT_global_priority = true,
      .EXT_global_priority_query = true,
      .EXT_graphics_pipeline_library = true,
      .EXT_host_query_reset = true,
      .EXT_image_drm_format_modifier = true,
      .EXT_image_robustness = true,
      .EXT_index_type_uint8 = true,
      .EXT_physical_device_drm = true,
      .EXT_pipeline_creation_cache_control = true,
      .EXT_pipeline_creation_feedback = true,
      .EXT_pipeline_robustness = true,
      .EXT_private_data = true,
      .EXT_provoking_vertex = true,
      .EXT_queue_family_foreign = true,
      .EXT_sampler_filter_minmax = arch >= 10,
      .EXT_scalar_block_layout = true,
      .EXT_separate_stencil_usage = true,
      .EXT_shader_module_identifier = true,
      .EXT_subgroup_size_control = arch >= 10, /* requires vk1.1 */
      .EXT_tooling_info = true,
      .EXT_ycbcr_2plane_444_formats = arch >= 10,
      .EXT_ycbcr_image_arrays = arch >= 10,
      .GOOGLE_decorate_string = true,
      .GOOGLE_hlsl_functionality1 = true,
      .GOOGLE_user_type = true,
   };
}

static void
get_features(const struct panvk_physical_device *device,
             struct vk_features *features)
{
   unsigned arch = pan_arch(device->kmod.props.gpu_prod_id);

   *features = (struct vk_features){
      /* Vulkan 1.0 */
      .depthClamp = true,
      .depthBiasClamp = true,
      .robustBufferAccess = true,
      .fullDrawIndexUint32 = true,
      .imageCubeArray = true,
      .independentBlend = true,
      .sampleRateShading = true,
      .logicOp = true,
      .wideLines = true,
      .largePoints = true,
      .occlusionQueryPrecise = true,
      .samplerAnisotropy = true,
      .textureCompressionETC2 = true,
      .textureCompressionASTC_LDR = true,
      .fragmentStoresAndAtomics = arch >= 10,
      .shaderUniformBufferArrayDynamicIndexing = true,
      .shaderSampledImageArrayDynamicIndexing = true,
      .shaderStorageBufferArrayDynamicIndexing = true,
      .shaderStorageImageArrayDynamicIndexing = true,
      .shaderInt16 = true,
      .shaderInt64 = true,
      .drawIndirectFirstInstance = true,

      /* Vulkan 1.1 */
      .storageBuffer16BitAccess = true,
      .uniformAndStorageBuffer16BitAccess = true,
      .storagePushConstant16 = true,
      .storageInputOutput16 = true,
      .multiview = arch >= 10,
      .multiviewGeometryShader = false,
      .multiviewTessellationShader = false,
      .variablePointersStorageBuffer = true,
      .variablePointers = true,
      .protectedMemory = false,
      .samplerYcbcrConversion = arch >= 10,
      .shaderDrawParameters = true,

      /* Vulkan 1.2 */
      .samplerMirrorClampToEdge = true,
      .drawIndirectCount = false,
      .storageBuffer8BitAccess = true,
      .uniformAndStorageBuffer8BitAccess = false,
      .storagePushConstant8 = false,
      .shaderBufferInt64Atomics = false,
      .shaderSharedInt64Atomics = false,
      .shaderFloat16 = false,
      .shaderInt8 = true,

      .descriptorIndexing = false,
      .shaderInputAttachmentArrayDynamicIndexing = false,
      .shaderUniformTexelBufferArrayDynamicIndexing = false,
      .shaderStorageTexelBufferArrayDynamicIndexing = false,
      .shaderUniformBufferArrayNonUniformIndexing = false,
      .shaderSampledImageArrayNonUniformIndexing = false,
      .shaderStorageBufferArrayNonUniformIndexing = false,
      .shaderStorageImageArrayNonUniformIndexing = false,
      .shaderInputAttachmentArrayNonUniformIndexing = false,
      .shaderUniformTexelBufferArrayNonUniformIndexing = false,
      .shaderStorageTexelBufferArrayNonUniformIndexing = false,
      .descriptorBindingUniformBufferUpdateAfterBind = false,
      .descriptorBindingSampledImageUpdateAfterBind = false,
      .descriptorBindingStorageImageUpdateAfterBind = false,
      .descriptorBindingStorageBufferUpdateAfterBind = false,
      .descriptorBindingUniformTexelBufferUpdateAfterBind = false,
      .descriptorBindingStorageTexelBufferUpdateAfterBind = false,
      .descriptorBindingUpdateUnusedWhilePending = false,
      .descriptorBindingPartiallyBound = false,
      .descriptorBindingVariableDescriptorCount = false,
      .runtimeDescriptorArray = false,

      .samplerFilterMinmax = arch >= 10,
      .scalarBlockLayout = true,
      .imagelessFramebuffer = true,
      .uniformBufferStandardLayout = true,
      .shaderSubgroupExtendedTypes = false,
      .separateDepthStencilLayouts = true,
      .hostQueryReset = true,
      .timelineSemaphore = true,
      .bufferDeviceAddress = true,
      .bufferDeviceAddressCaptureReplay = false,
      .bufferDeviceAddressMultiDevice = false,
      .vulkanMemoryModel = false,
      .vulkanMemoryModelDeviceScope = false,
      .vulkanMemoryModelAvailabilityVisibilityChains = false,
      .shaderOutputViewportIndex = false,
      .shaderOutputLayer = false,
      .subgroupBroadcastDynamicId = true,

      /* Vulkan 1.3 */
      .robustImageAccess = true,
      .inlineUniformBlock = false,
      .descriptorBindingInlineUniformBlockUpdateAfterBind = false,
      .pipelineCreationCacheControl = true,
      .privateData = true,
      .shaderDemoteToHelperInvocation = false,
      .shaderTerminateInvocation = false,
      .subgroupSizeControl = true,
      .computeFullSubgroups = true,
      .synchronization2 = true,
      .textureCompressionASTC_HDR = false,
      .shaderZeroInitializeWorkgroupMemory = true,
      .dynamicRendering = true,
      .shaderIntegerDotProduct = false,
      .maintenance4 = false,

      /* Vulkan 1.4 */
      .shaderSubgroupRotate = true,
      .shaderSubgroupRotateClustered = true,

      /* VK_EXT_graphics_pipeline_library */
      .graphicsPipelineLibrary = true,

      /* VK_KHR_global_priority */
      .globalPriorityQuery = true,

      /* VK_KHR_index_type_uint8 */
      .indexTypeUint8 = true,

      /* VK_KHR_vertex_attribute_divisor */
      .vertexAttributeInstanceRateDivisor = true,
      .vertexAttributeInstanceRateZeroDivisor = true,

      /* VK_EXT_depth_clip_enable */
      .depthClipEnable = true,

      /* VK_EXT_4444_formats */
      .formatA4R4G4B4 = true,
      .formatA4B4G4R4 = true,

      /* VK_EXT_custom_border_color */
      .customBorderColors = true,

      /* VK_EXT_provoking_vertex */
      .provokingVertexLast = true,
      .transformFeedbackPreservesProvokingVertex = false,

      /* v7 doesn't support AFBC(BGR). We need to tweak the texture swizzle to
       * make it work, which forces us to apply the same swizzle on the border
       * color, meaning we need to know the format when preparing the border
       * color.
       */
      .customBorderColorWithoutFormat = arch != 7,

      /* VK_KHR_pipeline_executable_properties */
      .pipelineExecutableInfo = true,

      /* VK_EXT_pipeline_robustness */
      .pipelineRobustness = true,

      /* VK_KHR_shader_relaxed_extended_instruction */
      .shaderRelaxedExtendedInstruction = true,

      /* VK_KHR_shader_expect_assume */
      .shaderExpectAssume = true,

      /* VK_EXT_shader_module_identifier */
      .shaderModuleIdentifier = true,

      /* VK_EXT_ycbcr_2plane_444_formats */
      .ycbcr2plane444Formats = arch >= 10,

      /* VK_EXT_ycbcr_image_arrays */
      .ycbcrImageArrays = arch >= 10,
   };
}

static uint32_t
get_api_version(unsigned arch)
{
   const uint32_t version_override = vk_get_version_override();
   if (version_override)
      return version_override;

   if (arch >= 10)
      return VK_MAKE_API_VERSION(0, 1, 1, VK_HEADER_VERSION);

   return VK_MAKE_API_VERSION(0, 1, 0, VK_HEADER_VERSION);
}

static VkConformanceVersion
get_conformance_version(unsigned arch)
{
   if (arch == 10)
      return (VkConformanceVersion){1, 4, 1, 2};

   return (VkConformanceVersion){0, 0, 0, 0};
}

static void
get_device_properties(const struct panvk_instance *instance,
                      const struct panvk_physical_device *device,
                      struct vk_properties *properties)
{
   /* HW supports MSAA 4, 8 and 16, but we limit ourselves to MSAA 4 for now. */
   VkSampleCountFlags sample_counts =
      VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;

   uint64_t os_page_size = 4096;
   os_get_page_size(&os_page_size);

   unsigned arch = pan_arch(device->kmod.props.gpu_prod_id);

   /* Ensure that the max threads count per workgroup is valid for Bifrost */
   assert(arch > 8 || device->kmod.props.max_threads_per_wg <= 1024);

   *properties = (struct vk_properties){
      .apiVersion = get_api_version(arch),
      .driverVersion = vk_get_driver_version(),
      .vendorID = ARM_VENDOR_ID,

      /* Collect arch_major, arch_minor, arch_rev and product_major,
       * as done by the Arm driver.
       */
      .deviceID = device->kmod.props.gpu_prod_id << 16,
      .deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,

      /* Vulkan 1.0 limits */
      /* Maximum texture dimension is 2^16. */
      .maxImageDimension1D = (1 << 16),
      .maxImageDimension2D = (1 << 16),
      .maxImageDimension3D = (1 << 16),
      .maxImageDimensionCube = (1 << 16),
      .maxImageArrayLayers = (1 << 16),
      /* Currently limited by the 1D texture size, which is 2^16.
       * TODO: If we expose buffer views as 2D textures, we can increase the
       * limit.
       */
      .maxTexelBufferElements = (1 << 16),
      /* Each uniform entry is 16-byte and the number of entries is encoded in a
       * 12-bit field, with the minus(1) modifier, which gives 2^20.
       */
      .maxUniformBufferRange = 1 << 20,
      /* Storage buffer access is lowered to globals, so there's no limit here,
       * except for the SW-descriptor we use to encode storage buffer
       * descriptors, where the size is a 32-bit field.
       */
      .maxStorageBufferRange = UINT32_MAX,
      /* 128 bytes of push constants, so we're aligned with the minimum Vulkan
       * requirements.
       */
      .maxPushConstantsSize = 128,
      /* On our kernel drivers we're limited by the available memory rather
       * than available allocations. This is better expressed through memory
       * properties and budget queries, and by returning
       * VK_ERROR_OUT_OF_DEVICE_MEMORY when applicable, rather than
       * this limit.
       */
      .maxMemoryAllocationCount = UINT32_MAX,
      /* On Mali, VkSampler objects do not use any resources other than host
       * memory and host address space, availability of which can change
       * significantly over time.
       */
      .maxSamplerAllocationCount = UINT32_MAX,
      /* A cache line. */
      .bufferImageGranularity = 64,
      /* Sparse binding not supported yet. */
      .sparseAddressSpaceSize = 0,
      /* On Bifrost, this is a software limit. We pick the minimum required by
       * Vulkan, because Bifrost GPUs don't have unified descriptor tables,
       * which forces us to agregatte all descriptors from all sets and dispatch
       * them to per-type descriptor tables emitted at draw/dispatch time. The
       * more sets we support the more copies we are likely to have to do at
       * draw time.
       *
       * Valhall has native support for descriptor sets, and allows a maximum
       * of 16 sets, but we reserve one for our internal use, so we have 15
       * left.
       */
      .maxBoundDescriptorSets = arch <= 7 ? 4 : 15,
      /* MALI_RENDERER_STATE::sampler_count is 16-bit. */
      .maxDescriptorSetSamplers = UINT16_MAX,
      /* MALI_RENDERER_STATE::uniform_buffer_count is 8-bit. We reserve 32 slots
       * for our internal UBOs.
       */
      .maxPerStageDescriptorUniformBuffers = UINT8_MAX - 32,
      .maxDescriptorSetUniformBuffers = UINT8_MAX - 32,
      /* SSBOs are limited by the size of a uniform buffer which contains our
       * panvk_ssbo_desc objects.
       * panvk_ssbo_desc is 16-byte, and each uniform entry in the Mali UBO is
       * 16-byte too. The number of entries is encoded in a 12-bit field, with
       * a minus(1) modifier, which gives a maximum of 2^12 SSBO
       * descriptors.
       */
      .maxDescriptorSetStorageBuffers = 1 << 12,
      /* MALI_RENDERER_STATE::sampler_count is 16-bit. */
      .maxDescriptorSetSampledImages = UINT16_MAX,
      /* MALI_ATTRIBUTE::buffer_index is 9-bit, and each image takes two
       * MALI_ATTRIBUTE_BUFFER slots, which gives a maximum of (1 << 8) images.
       */
      .maxDescriptorSetStorageImages = 1 << 8,
      /* A maximum of 8 color render targets, and one depth-stencil render
       * target.
       */
      .maxDescriptorSetInputAttachments = 9,

      /* We could theoretically use the maxDescriptor values here (except for
       * UBOs where we're really limited to 256 on the shader side), but on
       * Bifrost we have to copy some tables around, which comes at an extra
       * memory/processing cost, so let's pick something smaller.
       */
      .maxPerStageDescriptorInputAttachments = 9,
      .maxPerStageDescriptorSampledImages = 256,
      .maxPerStageDescriptorSamplers = 128,
      .maxPerStageDescriptorStorageBuffers = 64,
      .maxPerStageDescriptorStorageImages = 32,
      .maxPerStageDescriptorUniformBuffers = 64,
      .maxPerStageResources = 9 + 256 + 128 + 64 + 32 + 64,

      /* Software limits to keep VkCommandBuffer tracking sane. */
      .maxDescriptorSetUniformBuffersDynamic = 16,
      .maxDescriptorSetStorageBuffersDynamic = 8,
      /* Software limit to keep VkCommandBuffer tracking sane. The HW supports
       * up to 2^9 vertex attributes.
       */
      .maxVertexInputAttributes = 16,
      .maxVertexInputBindings = 16,
      /* MALI_ATTRIBUTE::offset is 32-bit. */
      .maxVertexInputAttributeOffset = UINT32_MAX,
      /* MALI_ATTRIBUTE_BUFFER::stride is 32-bit. */
      .maxVertexInputBindingStride = MESA_VK_MAX_VERTEX_BINDING_STRIDE,
      /* 32 vec4 varyings. */
      .maxVertexOutputComponents = 128,
      /* Tesselation shaders not supported. */
      .maxTessellationGenerationLevel = 0,
      .maxTessellationPatchSize = 0,
      .maxTessellationControlPerVertexInputComponents = 0,
      .maxTessellationControlPerVertexOutputComponents = 0,
      .maxTessellationControlPerPatchOutputComponents = 0,
      .maxTessellationControlTotalOutputComponents = 0,
      .maxTessellationEvaluationInputComponents = 0,
      .maxTessellationEvaluationOutputComponents = 0,
      /* Geometry shaders not supported. */
      .maxGeometryShaderInvocations = 0,
      .maxGeometryInputComponents = 0,
      .maxGeometryOutputComponents = 0,
      .maxGeometryOutputVertices = 0,
      .maxGeometryTotalOutputComponents = 0,
      /* 32 vec4 varyings. */
      .maxFragmentInputComponents = 128,
      /* 8 render targets. */
      .maxFragmentOutputAttachments = 8,
      /* We don't support dual source blending yet. */
      .maxFragmentDualSrcAttachments = 0,
      /* 8 render targets, 2^12 storage buffers and 2^8 storage images (see
       * above).
       */
      .maxFragmentCombinedOutputResources = 8 + (1 << 12) + (1 << 8),
      /* MALI_LOCAL_STORAGE::wls_size_{base,scale} allows us to have up to
       * (7 << 30) bytes of shared memory, but we cap it to 32K as it doesn't
       * really make sense to expose this amount of memory, especially since
       * it's backed by global memory anyway.
       */
      .maxComputeSharedMemorySize = 32768,
      /* Software limit to meet Vulkan 1.0 requirements. We split the
       * dispatch in several jobs if it's too big.
       */
      .maxComputeWorkGroupCount = {65535, 65535, 65535},

      /* We could also split into serveral jobs but this has many limitations.
       * As such we limit to the max threads per workgroup supported by the GPU.
       */
      .maxComputeWorkGroupInvocations = device->kmod.props.max_threads_per_wg,
      .maxComputeWorkGroupSize = {device->kmod.props.max_threads_per_wg,
                                  device->kmod.props.max_threads_per_wg,
                                  device->kmod.props.max_threads_per_wg},
      /* 8-bit subpixel precision. */
      .subPixelPrecisionBits = 8,
      .subTexelPrecisionBits = 8,
      .mipmapPrecisionBits = 8,
      /* Software limit. */
      .maxDrawIndexedIndexValue = UINT32_MAX,
      /* Make it one for now. */
      .maxDrawIndirectCount = 1,
      .maxSamplerLodBias = (float)INT16_MAX / 256.0f,
      .maxSamplerAnisotropy = 16,
      .maxViewports = 1,
      /* Same as the framebuffer limit. */
      .maxViewportDimensions = {(1 << 14), (1 << 14)},
      /* Encoded in a 16-bit signed integer. */
      .viewportBoundsRange = {INT16_MIN, INT16_MAX},
      .viewportSubPixelBits = 0,
      /* Align on a page. */
      .minMemoryMapAlignment = os_page_size,
      /* Some compressed texture formats require 128-byte alignment. */
      .minTexelBufferOffsetAlignment = 64,
      /* Always aligned on a uniform slot (vec4). */
      .minUniformBufferOffsetAlignment = 16,
      /* Lowered to global accesses, which happen at the 32-bit granularity. */
      .minStorageBufferOffsetAlignment = 4,
      /* Signed 4-bit value. */
      .minTexelOffset = -8,
      .maxTexelOffset = 7,
      .minTexelGatherOffset = -8,
      .maxTexelGatherOffset = 7,
      .minInterpolationOffset = -0.5,
      .maxInterpolationOffset = 0.5,
      .subPixelInterpolationOffsetBits = 8,
      .maxFramebufferWidth = (1 << 14),
      .maxFramebufferHeight = (1 << 14),
      .maxFramebufferLayers = 256,
      .framebufferColorSampleCounts = sample_counts,
      .framebufferDepthSampleCounts = sample_counts,
      .framebufferStencilSampleCounts = sample_counts,
      .framebufferNoAttachmentsSampleCounts = sample_counts,
      .maxColorAttachments = 8,
      .sampledImageColorSampleCounts = sample_counts,
      .sampledImageIntegerSampleCounts = VK_SAMPLE_COUNT_1_BIT,
      .sampledImageDepthSampleCounts = sample_counts,
      .sampledImageStencilSampleCounts = sample_counts,
      .storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT,
      .maxSampleMaskWords = 1,
      .timestampComputeAndGraphics = false,
      .timestampPeriod = 0,
      .maxClipDistances = 0,
      .maxCullDistances = 0,
      .maxCombinedClipAndCullDistances = 0,
      .discreteQueuePriorities = 2,
      .pointSizeRange = {0.125, 4095.9375},
      .lineWidthRange = {0.0, 7.9921875},
      .pointSizeGranularity = (1.0 / 16.0),
      .lineWidthGranularity = (1.0 / 128.0),
      .strictLines = true,
      .standardSampleLocations = true,
      .optimalBufferCopyOffsetAlignment = 64,
      .optimalBufferCopyRowPitchAlignment = 64,
      .nonCoherentAtomSize = 64,

      /* Vulkan 1.0 sparse properties */
      .sparseResidencyNonResidentStrict = false,
      .sparseResidencyAlignedMipSize = false,
      .sparseResidencyStandard2DBlockShape = false,
      .sparseResidencyStandard2DMultisampleBlockShape = false,
      .sparseResidencyStandard3DBlockShape = false,

      /* Vulkan 1.1 properties */
      /* XXX: 1.1 support */
      .subgroupSize = pan_subgroup_size(arch),
      /* We only support VS, FS, and CS.
       *
       * The HW may spawn VS invocations for non-existing indices, which could
       * be observed through subgroup ops (though the user can observe them
       * through infinte loops anyway), so subgroup ops can't be supported in
       * VS.
       *
       * In FS, voting and potentially other subgroup ops are currently broken,
       * so we don't report support for this stage either.
       */
      .subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT,
      .subgroupSupportedOperations =
         VK_SUBGROUP_FEATURE_BASIC_BIT |
         VK_SUBGROUP_FEATURE_VOTE_BIT |
         VK_SUBGROUP_FEATURE_ARITHMETIC_BIT |
         VK_SUBGROUP_FEATURE_BALLOT_BIT |
         VK_SUBGROUP_FEATURE_SHUFFLE_BIT |
         VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT |
         VK_SUBGROUP_FEATURE_CLUSTERED_BIT |
         VK_SUBGROUP_FEATURE_QUAD_BIT |
         VK_SUBGROUP_FEATURE_ROTATE_BIT |
         VK_SUBGROUP_FEATURE_ROTATE_CLUSTERED_BIT,
      .subgroupQuadOperationsInAllStages = false,
      .pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES,
      .maxMultiviewViewCount = arch >= 10 ? 8 : 0,
      .maxMultiviewInstanceIndex = arch >= 10 ? UINT32_MAX : 0,
      .protectedNoFault = false,
      .maxPerSetDescriptors = UINT16_MAX,
      /* Our buffer size fields allow only this much */
      .maxMemoryAllocationSize = UINT32_MAX,

      /* Vulkan 1.2 properties */
      /* XXX: 1.2 support */
      .supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                    VK_RESOLVE_MODE_AVERAGE_BIT |
                                    VK_RESOLVE_MODE_MIN_BIT |
                                    VK_RESOLVE_MODE_MAX_BIT,
      .supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT |
                                      VK_RESOLVE_MODE_MIN_BIT |
                                      VK_RESOLVE_MODE_MAX_BIT,
      .independentResolveNone = true,
      .independentResolve = true,
      /* VK_KHR_driver_properties */
      .driverID = VK_DRIVER_ID_MESA_PANVK,
      .conformanceVersion = get_conformance_version(arch),
      /* XXX: VK_KHR_shader_float_controls */
      .denormBehaviorIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL,
      .roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_ALL,
      .shaderSignedZeroInfNanPreserveFloat16 = true,
      .shaderSignedZeroInfNanPreserveFloat32 = true,
      .shaderSignedZeroInfNanPreserveFloat64 = false,
      .shaderDenormPreserveFloat16 = true,
      .shaderDenormPreserveFloat32 = true,
      .shaderDenormPreserveFloat64 = false,
      .shaderDenormFlushToZeroFloat16 = true,
      .shaderDenormFlushToZeroFloat32 = true,
      .shaderDenormFlushToZeroFloat64 = false,
      .shaderRoundingModeRTEFloat16 = true,
      .shaderRoundingModeRTEFloat32 = true,
      .shaderRoundingModeRTEFloat64 = false,
      .shaderRoundingModeRTZFloat16 = true,
      .shaderRoundingModeRTZFloat32 = true,
      .shaderRoundingModeRTZFloat64 = false,
      /* XXX: VK_EXT_descriptor_indexing */
      .maxUpdateAfterBindDescriptorsInAllPools = 0,
      .shaderUniformBufferArrayNonUniformIndexingNative = false,
      .shaderSampledImageArrayNonUniformIndexingNative = false,
      .shaderStorageBufferArrayNonUniformIndexingNative = false,
      .shaderStorageImageArrayNonUniformIndexingNative = false,
      .shaderInputAttachmentArrayNonUniformIndexingNative = false,
      .robustBufferAccessUpdateAfterBind = false,
      .quadDivergentImplicitLod = false,
      .maxPerStageDescriptorUpdateAfterBindSamplers = 0,
      .maxPerStageDescriptorUpdateAfterBindUniformBuffers = 0,
      .maxPerStageDescriptorUpdateAfterBindStorageBuffers = 0,
      .maxPerStageDescriptorUpdateAfterBindSampledImages = 0,
      .maxPerStageDescriptorUpdateAfterBindStorageImages = 0,
      .maxPerStageDescriptorUpdateAfterBindInputAttachments = 0,
      .maxPerStageUpdateAfterBindResources = 0,
      .maxDescriptorSetUpdateAfterBindSamplers = 0,
      .maxDescriptorSetUpdateAfterBindUniformBuffers = 0,
      .maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 0,
      .maxDescriptorSetUpdateAfterBindStorageBuffers = 0,
      .maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 0,
      .maxDescriptorSetUpdateAfterBindSampledImages = 0,
      .maxDescriptorSetUpdateAfterBindStorageImages = 0,
      .maxDescriptorSetUpdateAfterBindInputAttachments = 0,
      .filterMinmaxSingleComponentFormats = arch >= 10,
      .filterMinmaxImageComponentMapping = arch >= 10,
      .maxTimelineSemaphoreValueDifference = INT64_MAX,
      .framebufferIntegerColorSampleCounts = sample_counts,

      /* Vulkan 1.3 properties */
      /* XXX: 1.3 support */
      /* XXX: VK_EXT_subgroup_size_control */
      .minSubgroupSize = pan_subgroup_size(arch),
      .maxSubgroupSize = pan_subgroup_size(arch),
      .maxComputeWorkgroupSubgroups =
         device->kmod.props.max_threads_per_wg / pan_subgroup_size(arch),
      .requiredSubgroupSizeStages = VK_SHADER_STAGE_COMPUTE_BIT,
      /* XXX: VK_EXT_inline_uniform_block */
      .maxInlineUniformBlockSize = MAX_INLINE_UNIFORM_BLOCK_SIZE,
      .maxPerStageDescriptorInlineUniformBlocks =
         MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS,
      .maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks =
         MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS,
      .maxDescriptorSetInlineUniformBlocks =
         MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS,
      .maxDescriptorSetUpdateAfterBindInlineUniformBlocks =
         MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS,
      .maxInlineUniformTotalSize =
         MAX_INLINE_UNIFORM_BLOCK_DESCRIPTORS * MAX_INLINE_UNIFORM_BLOCK_SIZE,
      /* XXX: VK_KHR_shader_integer_dot_product */
      .integerDotProduct8BitUnsignedAccelerated = true,
      .integerDotProduct8BitSignedAccelerated = true,
      .integerDotProduct4x8BitPackedUnsignedAccelerated = true,
      .integerDotProduct4x8BitPackedSignedAccelerated = true,
      /* XXX: VK_EXT_texel_buffer_alignment */
      .storageTexelBufferOffsetAlignmentBytes = 64,
      .storageTexelBufferOffsetSingleTexelAlignment = false,
      .uniformTexelBufferOffsetAlignmentBytes = 4,
      .uniformTexelBufferOffsetSingleTexelAlignment = true,
      /* XXX: VK_KHR_maintenance4 */
      .maxBufferSize = 1 << 30,

      /* VK_EXT_custom_border_color */
      .maxCustomBorderColorSamplers = 32768,

      /* VK_EXT_graphics_pipeline_library */
      .graphicsPipelineLibraryFastLinking = true,
      .graphicsPipelineLibraryIndependentInterpolationDecoration = true,

      /* VK_EXT_pipeline_robustness */
      .defaultRobustnessStorageBuffers =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT,
      .defaultRobustnessUniformBuffers =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT,
      .defaultRobustnessVertexInputs =
         VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT,
      .defaultRobustnessImages =
         VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_ROBUST_IMAGE_ACCESS_EXT,

      /* VK_EXT_provoking_vertex */
      .provokingVertexModePerPipeline = false,
      .transformFeedbackPreservesTriangleFanProvokingVertex = false,

      /* VK_KHR_vertex_attribute_divisor */
      /* We will have to restrict this a bit for multiview */
      .maxVertexAttribDivisor = UINT32_MAX,
      .supportsNonZeroFirstInstance = false,

      /* VK_KHR_push_descriptor */
      .maxPushDescriptors = MAX_PUSH_DESCRIPTORS,
   };

   snprintf(properties->deviceName, sizeof(properties->deviceName), "%s",
            device->name);

   memcpy(properties->pipelineCacheUUID, device->cache_uuid, VK_UUID_SIZE);

   const struct {
      uint16_t vendor_id;
      uint32_t device_id;
      uint8_t pad[8];
   } dev_uuid = {
      .vendor_id = ARM_VENDOR_ID,
      .device_id = device->model->gpu_id,
   };

   STATIC_ASSERT(sizeof(dev_uuid) == VK_UUID_SIZE);
   memcpy(properties->deviceUUID, &dev_uuid, VK_UUID_SIZE);
   STATIC_ASSERT(sizeof(instance->driver_build_sha) >= VK_UUID_SIZE);
   memcpy(properties->driverUUID, instance->driver_build_sha, VK_UUID_SIZE);

   snprintf(properties->driverName, VK_MAX_DRIVER_NAME_SIZE, "panvk");
   snprintf(properties->driverInfo, VK_MAX_DRIVER_INFO_SIZE,
            "Mesa " PACKAGE_VERSION MESA_GIT_SHA1);

   /* VK_EXT_physical_device_drm */
   if (device->drm.primary_rdev) {
      properties->drmHasPrimary = true;
      properties->drmPrimaryMajor = major(device->drm.primary_rdev);
      properties->drmPrimaryMinor = minor(device->drm.primary_rdev);
   }
   if (device->drm.render_rdev) {
      properties->drmHasRender = true;
      properties->drmRenderMajor = major(device->drm.render_rdev);
      properties->drmRenderMinor = minor(device->drm.render_rdev);
   }

   /* VK_EXT_shader_module_identifier */
   STATIC_ASSERT(sizeof(vk_shaderModuleIdentifierAlgorithmUUID) ==
                 sizeof(properties->shaderModuleIdentifierAlgorithmUUID));
   memcpy(properties->shaderModuleIdentifierAlgorithmUUID,
          vk_shaderModuleIdentifierAlgorithmUUID,
          sizeof(properties->shaderModuleIdentifierAlgorithmUUID));
}

void
panvk_physical_device_finish(struct panvk_physical_device *device)
{
   panvk_wsi_finish(device);

   pan_kmod_dev_destroy(device->kmod.dev);

   vk_physical_device_finish(&device->vk);
}

VkResult
panvk_physical_device_init(struct panvk_physical_device *device,
                           struct panvk_instance *instance,
                           drmDevicePtr drm_device)
{
   VkResult result;

   result = create_kmod_dev(device, instance, drm_device);
   if (result != VK_SUCCESS)
      return result;

   pan_kmod_dev_query_props(device->kmod.dev, &device->kmod.props);

   device->model = panfrost_get_model(device->kmod.props.gpu_prod_id,
                                      device->kmod.props.gpu_variant);

   unsigned arch = pan_arch(device->kmod.props.gpu_prod_id);

   if (!device->model) {
      result = panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                            "Unknown gpu_id (%#x) or variant (%#x)",
                            device->kmod.props.gpu_prod_id,
                            device->kmod.props.gpu_variant);
      goto fail;
   }

   switch (arch) {
   case 6:
   case 7:
      if (!getenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER")) {
         result = panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "WARNING: panvk is not well-tested on v%d, "
                               "pass PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 "
                               "if you know what you're doing.", arch);
         goto fail;
      }
      break;

   case 10:
      break;

   default:
      result = panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                            "%s not supported", device->model->name);
      goto fail;
   }

   result = get_drm_device_ids(device, instance, drm_device);
   if (result != VK_SUCCESS)
      goto fail;

   device->formats.all = panfrost_format_table(arch);
   device->formats.blendable = panfrost_blendable_format_table(arch);

   memset(device->name, 0, sizeof(device->name));
   sprintf(device->name, "%s", device->model->name);

   if (get_cache_uuid(device->kmod.props.gpu_prod_id, device->cache_uuid)) {
      result = panvk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                            "cannot generate UUID");
      goto fail;
   }

   result = get_device_sync_types(device, instance);
   if (result != VK_SUCCESS)
      goto fail;

   vk_warn_non_conformant_implementation("panvk");

   struct vk_device_extension_table supported_extensions;
   get_device_extensions(device, &supported_extensions);

   struct vk_features supported_features;
   get_features(device, &supported_features);

   struct vk_properties properties;
   get_device_properties(instance, device, &properties);

   struct vk_physical_device_dispatch_table dispatch_table;
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &panvk_physical_device_entrypoints, true);
   vk_physical_device_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_physical_device_entrypoints, false);

   result = vk_physical_device_init(&device->vk, &instance->vk,
                                    &supported_extensions, &supported_features,
                                    &properties, &dispatch_table);

   if (result != VK_SUCCESS)
      goto fail;

   device->vk.supported_sync_types = device->sync_types;

   result = panvk_wsi_init(device);
   if (result != VK_SUCCESS)
      goto fail;

   return VK_SUCCESS;

fail:
   if (device->vk.instance)
      vk_physical_device_finish(&device->vk);

   pan_kmod_dev_destroy(device->kmod.dev);

   return result;
}

static const VkQueueFamilyProperties panvk_queue_family_properties = {
   .queueFlags =
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
   .queueCount = 1,
   .timestampValidBits = 0,
   .minImageTransferGranularity = {1, 1, 1},
};

static void
panvk_fill_global_priority(const struct panvk_physical_device *physical_device,
                           VkQueueFamilyGlobalPriorityPropertiesKHR *prio)
{
   enum pan_kmod_group_allow_priority_flags prio_mask =
      physical_device->kmod.props.allowed_group_priorities_mask;
   uint32_t prio_idx = 0;

   if (prio_mask & PAN_KMOD_GROUP_ALLOW_PRIORITY_LOW)
      prio->priorities[prio_idx++] = VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR;

   if (prio_mask & PAN_KMOD_GROUP_ALLOW_PRIORITY_MEDIUM)
      prio->priorities[prio_idx++] = VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR;

   if (prio_mask & PAN_KMOD_GROUP_ALLOW_PRIORITY_HIGH)
      prio->priorities[prio_idx++] = VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR;

   if (prio_mask & PAN_KMOD_GROUP_ALLOW_PRIORITY_REALTIME)
      prio->priorities[prio_idx++] = VK_QUEUE_GLOBAL_PRIORITY_REALTIME_KHR;

   prio->priorityCount = prio_idx;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceQueueFamilyProperties2(
   VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
   VkQueueFamilyProperties2 *pQueueFamilyProperties)
{
   VK_FROM_HANDLE(panvk_physical_device, physical_device, physicalDevice);
   VK_OUTARRAY_MAKE_TYPED(VkQueueFamilyProperties2, out, pQueueFamilyProperties,
                          pQueueFamilyPropertyCount);

   vk_outarray_append_typed(VkQueueFamilyProperties2, &out, p)
   {
      p->queueFamilyProperties = panvk_queue_family_properties;

      VkQueueFamilyGlobalPriorityPropertiesKHR *prio =
         vk_find_struct(p->pNext, QUEUE_FAMILY_GLOBAL_PRIORITY_PROPERTIES_KHR);
      if (prio)
         panvk_fill_global_priority(physical_device, prio);
   }
}

static uint64_t
get_system_heap_size()
{
   struct sysinfo info;
   sysinfo(&info);

   uint64_t total_ram = (uint64_t)info.totalram * info.mem_unit;

   /* We don't want to burn too much ram with the GPU.  If the user has 4GiB
    * or less, we use at most half.  If they have more than 4GiB, we use 3/4.
    */
   uint64_t available_ram;
   if (total_ram <= 4ull * 1024 * 1024 * 1024)
      available_ram = total_ram / 2;
   else
      available_ram = total_ram * 3 / 4;

   return available_ram;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceMemoryProperties2(
   VkPhysicalDevice physicalDevice,
   VkPhysicalDeviceMemoryProperties2 *pMemoryProperties)
{
   pMemoryProperties->memoryProperties = (VkPhysicalDeviceMemoryProperties){
      .memoryHeapCount = 1,
      .memoryHeaps[0].size = get_system_heap_size(),
      .memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
      .memoryTypeCount = 1,
      .memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      .memoryTypes[0].heapIndex = 0,
   };
}

#define DEVICE_PER_ARCH_FUNCS(_ver)                                            \
   VkResult panvk_v##_ver##_create_device(                                     \
      struct panvk_physical_device *physical_device,                           \
      const VkDeviceCreateInfo *pCreateInfo,                                   \
      const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);             \
                                                                               \
   void panvk_v##_ver##_destroy_device(                                        \
      struct panvk_device *device, const VkAllocationCallbacks *pAllocator)

DEVICE_PER_ARCH_FUNCS(6);
DEVICE_PER_ARCH_FUNCS(7);
DEVICE_PER_ARCH_FUNCS(10);

VKAPI_ATTR VkResult VKAPI_CALL
panvk_CreateDevice(VkPhysicalDevice physicalDevice,
                   const VkDeviceCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   VK_FROM_HANDLE(panvk_physical_device, physical_device, physicalDevice);
   unsigned arch = pan_arch(physical_device->kmod.props.gpu_prod_id);
   VkResult result = VK_ERROR_INITIALIZATION_FAILED;

   panvk_arch_dispatch_ret(arch, create_device, result, physical_device,
                           pCreateInfo, pAllocator, pDevice);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
panvk_DestroyDevice(VkDevice _device, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_physical_device *physical_device =
      to_panvk_physical_device(device->vk.physical);
   unsigned arch = pan_arch(physical_device->kmod.props.gpu_prod_id);

   panvk_arch_dispatch(arch, destroy_device, device, pAllocator);
}

static bool
unsupported_yuv_format(enum pipe_format pfmt)
{
   switch (pfmt) {
   /* 3-plane YUV 444 and 16-bit 3-plane YUV are not supported natively by
    * the HW.
    */
   case PIPE_FORMAT_Y8_U8_V8_444_UNORM:
   case PIPE_FORMAT_Y16_U16_V16_420_UNORM:
   case PIPE_FORMAT_Y16_U16_V16_422_UNORM:
   case PIPE_FORMAT_Y16_U16_V16_444_UNORM:
      return true;
   default:
      return false;
   }
}

static bool
format_is_supported(struct panvk_physical_device *physical_device,
                    const struct panfrost_format fmt,
                    enum pipe_format pfmt)
{
   if (pfmt == PIPE_FORMAT_NONE)
      return false;

   if (unsupported_yuv_format(pfmt))
      return false;

   /* If the format ID is zero, it's not supported. */
   if (!fmt.hw)
      return false;

   /* Compressed formats (ID < 32) are optional. We need to check against
    * the supported formats reported by the GPU. */
   if (util_format_is_compressed(pfmt)) {
      uint32_t supported_compr_fmts =
         panfrost_query_compressed_formats(&physical_device->kmod.props);

      if (!(BITFIELD_BIT(fmt.texfeat_bit) & supported_compr_fmts))
         return false;
   }

   /* 3byte formats are not supported by the buffer <-> image copy helpers. */
   if (util_format_get_blocksize(pfmt) == 3)
      return false;

   return true;
}

static VkFormatFeatureFlags
get_image_plane_format_features(struct panvk_physical_device *physical_device,
                                VkFormat format)
{
   VkFormatFeatureFlags features = 0;
   enum pipe_format pfmt = vk_format_to_pipe_format(format);
   const struct panfrost_format fmt = physical_device->formats.all[pfmt];
   unsigned arch = pan_arch(physical_device->kmod.props.gpu_prod_id);

   if (!format_is_supported(physical_device, fmt, pfmt))
      return 0;

   if (fmt.bind & PAN_BIND_SAMPLER_VIEW) {
      features |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                  VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

      if (arch >= 10)
         features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT;

      /* Integer formats only support nearest filtering */
      if (!util_format_is_scaled(pfmt) && !util_format_is_pure_integer(pfmt))
         features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

      features |= VK_FORMAT_FEATURE_BLIT_SRC_BIT;
   }

   if (fmt.bind & PAN_BIND_RENDER_TARGET) {
      features |= VK_FORMAT_FEATURE_BLIT_DST_BIT;
      features |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;

      /* SNORM rendering isn't working yet (nir_lower_blend bugs), disable for
       * now.
       *
       * XXX: Enable once fixed.
       */
      if (!util_format_is_snorm(pfmt)) {
         features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
         features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
      }
   }

   if (pfmt == PIPE_FORMAT_R32_UINT || pfmt == PIPE_FORMAT_R32_SINT)
      features |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;

   if (fmt.bind & PAN_BIND_DEPTH_STENCIL)
      features |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

   return features;
}

static VkFormatFeatureFlags
get_image_format_features(struct panvk_physical_device *physical_device,
                          VkFormat format)
{
   const struct vk_format_ycbcr_info *ycbcr_info =
         vk_format_get_ycbcr_info(format);
   const unsigned arch = pan_arch(physical_device->kmod.props.gpu_prod_id);

   /* TODO: Bifrost YCbCr support */
   if (ycbcr_info && arch <= 7)
      return 0;

   if (ycbcr_info == NULL)
      return get_image_plane_format_features(physical_device, format);

   if (unsupported_yuv_format(vk_format_to_pipe_format(format)))
      return 0;

   /* For multi-plane, we get the feature flags of each plane separately,
    * then take their intersection as the overall format feature flags
    */
   VkFormatFeatureFlags features = ~0u;
   bool cosited_chroma = false;
   for (uint8_t plane = 0; plane < ycbcr_info->n_planes; plane++) {
      const struct vk_format_ycbcr_plane *plane_info =
         &ycbcr_info->planes[plane];
      features &=
         get_image_plane_format_features(physical_device, plane_info->format);
      if (plane_info->denominator_scales[0] > 1 ||
          plane_info->denominator_scales[1] > 1)
         cosited_chroma = true;
   }
   if (features == 0)
      return 0;

   /* Uh... We really should be able to sample from YCbCr */
   assert(features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
   assert(features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);

   /* Siting is handled in the YCbCr lowering pass. */
   features |= VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;
   if (cosited_chroma)
      features |= VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT;

   /* These aren't allowed for YCbCr formats */
   features &= ~(VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                 VK_FORMAT_FEATURE_BLIT_DST_BIT |
                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                 VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                 VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);

   /* This is supported on all YCbCr formats */
   features |=
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;

   if (ycbcr_info->n_planes > 1) {
      /* DISJOINT_BIT implies that each plane has its own separate binding,
       * while SEPARATE_RECONSTRUCTION_FILTER_BIT implies that luma and chroma
       * each have their own, separate filters, so these two bits make sense
       * for multi-planar formats only.
       */
      features |= VK_FORMAT_FEATURE_DISJOINT_BIT |
                  VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT;
   }

   return features;
}

static VkFormatFeatureFlags
get_buffer_format_features(struct panvk_physical_device *physical_device,
                           VkFormat format)
{
   VkFormatFeatureFlags features = 0;
   enum pipe_format pfmt = vk_format_to_pipe_format(format);
   const struct panfrost_format fmt = physical_device->formats.all[pfmt];

   if (!format_is_supported(physical_device, fmt, pfmt))
      return 0;

   /* Reject sRGB formats (see
    * https://github.com/KhronosGroup/Vulkan-Docs/issues/2214).
    */
   if ((fmt.bind & PAN_BIND_VERTEX_BUFFER) && !util_format_is_srgb(pfmt))
      features |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;

   if ((fmt.bind & PAN_BIND_SAMPLER_VIEW) &&
       !util_format_is_depth_or_stencil(pfmt))
      features |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;

   if ((fmt.bind & PAN_BIND_RENDER_TARGET) &&
       !util_format_is_depth_and_stencil(pfmt))
      features |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;

   if (pfmt == PIPE_FORMAT_R32_UINT || pfmt == PIPE_FORMAT_R32_SINT)
      features |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;

   return features;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                         VkFormat format,
                                         VkFormatProperties2 *pFormatProperties)
{
   VK_FROM_HANDLE(panvk_physical_device, physical_device, physicalDevice);

   VkFormatFeatureFlags tex =
      get_image_format_features(physical_device, format);
   VkFormatFeatureFlags buffer =
      get_buffer_format_features(physical_device, format);

   pFormatProperties->formatProperties = (VkFormatProperties){
      .linearTilingFeatures = tex,
      .optimalTilingFeatures = tex,
      .bufferFeatures = buffer,
   };

   VkDrmFormatModifierPropertiesListEXT *list = vk_find_struct(
      pFormatProperties->pNext, DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT);
   if (list && pFormatProperties->formatProperties.linearTilingFeatures) {
      VK_OUTARRAY_MAKE_TYPED(VkDrmFormatModifierPropertiesEXT, out,
                             list->pDrmFormatModifierProperties,
                             &list->drmFormatModifierCount);

      vk_outarray_append_typed(VkDrmFormatModifierPropertiesEXT, &out,
                               mod_props)
      {
         mod_props->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
         mod_props->drmFormatModifierPlaneCount = 1;
         mod_props->drmFormatModifierTilingFeatures =
            pFormatProperties->formatProperties.linearTilingFeatures;
      }
   }
}

static VkResult
get_image_format_properties(struct panvk_physical_device *physical_device,
                            const VkPhysicalDeviceImageFormatInfo2 *info,
                            VkImageFormatProperties *pImageFormatProperties,
                            VkFormatFeatureFlags *p_feature_flags)
{
   VkFormatFeatureFlags format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   enum pipe_format format = vk_format_to_pipe_format(info->format);

   const VkImageStencilUsageCreateInfo *stencil_usage_info =
      vk_find_struct_const(info->pNext, IMAGE_STENCIL_USAGE_CREATE_INFO);
   VkImageUsageFlags stencil_usage =
      stencil_usage_info ? stencil_usage_info->stencilUsage : info->usage;
   VkImageUsageFlags all_usage = info->usage | stencil_usage;
   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(info->format);

   switch (info->tiling) {
   case VK_IMAGE_TILING_LINEAR:
   case VK_IMAGE_TILING_OPTIMAL:
      break;
   case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT: {
      const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *mod_info =
         vk_find_struct_const(
            info->pNext, PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT);
      if (mod_info->drmFormatModifier != DRM_FORMAT_MOD_LINEAR)
         goto unsupported;

      /* The only difference between optimal and linear is currently whether
       * depth/stencil attachments are allowed on depth/stencil formats.
       * There's no reason to allow importing depth/stencil textures, so just
       * disallow it and then this annoying edge case goes away.
       */
      if (util_format_is_depth_or_stencil(format))
         goto unsupported;
      break;
   }
   default:
      unreachable("bad VkPhysicalDeviceImageFormatInfo2");
   }

   /* For the purposes of these checks, we don't care about all the extra
    * YCbCr features and we just want the intersection of features available
    * to all planes of the given format.
    */
   if (ycbcr_info == NULL) {
      format_feature_flags =
         get_image_format_features(physical_device, info->format);
   } else {
      format_feature_flags = ~0u;
      assert(ycbcr_info->n_planes > 0);
      for (uint8_t plane = 0; plane < ycbcr_info->n_planes; plane++) {
         const VkFormat plane_format = ycbcr_info->planes[plane].format;
         format_feature_flags &=
            get_image_format_features(physical_device, plane_format);
      }
   }

   if (format_feature_flags == 0)
      goto unsupported;

   if (ycbcr_info && info->type != VK_IMAGE_TYPE_2D)
      goto unsupported;

   switch (info->type) {
   default:
      unreachable("bad vkimage type");
   case VK_IMAGE_TYPE_1D:
      maxExtent.width = 1 << 16;
      maxExtent.height = 1;
      maxExtent.depth = 1;
      maxMipLevels = 17; /* log2(maxWidth) + 1 */
      maxArraySize = 1 << 16;
      break;
   case VK_IMAGE_TYPE_2D:
      maxExtent.width = 1 << 16;
      maxExtent.height = 1 << 16;
      maxExtent.depth = 1;
      maxMipLevels = 17; /* log2(maxWidth) + 1 */
      maxArraySize = 1 << 16;
      break;
   case VK_IMAGE_TYPE_3D:
      maxExtent.width = 1 << 16;
      maxExtent.height = 1 << 16;
      maxExtent.depth = 1 << 16;
      maxMipLevels = 17; /* log2(maxWidth) + 1 */
      maxArraySize = 1;
      break;
   }

   if (ycbcr_info)
      maxMipLevels = 1;

   if (info->tiling == VK_IMAGE_TILING_OPTIMAL &&
       info->type == VK_IMAGE_TYPE_2D && ycbcr_info == NULL &&
       (format_feature_flags &
        (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       !(all_usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
      sampleCounts |= VK_SAMPLE_COUNT_4_BIT;
   }

   /* From the Vulkan 1.2.199 spec:
   *
   *    "VK_IMAGE_CREATE_EXTENDED_USAGE_BIT specifies that the image can be
   *    created with usage flags that are not supported for the format the
   *    image is created with but are supported for at least one format a
   *    VkImageView created from the image can have."
   *
   * If VK_IMAGE_CREATE_EXTENDED_USAGE_BIT is set, views can be created with
   * different usage than the image so we can't always filter on usage.
   * There is one exception to this below for storage.
   */
   if (!(info->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT)) {
      if (all_usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
         if (!(format_feature_flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            goto unsupported;
         }
      }

      if (all_usage & VK_IMAGE_USAGE_STORAGE_BIT) {
         if (!(format_feature_flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
            goto unsupported;
         }
      }

      if (all_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ||
          ((all_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) &&
           !vk_format_is_depth_or_stencil(info->format))) {
         if (!(format_feature_flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
            goto unsupported;
         }
      }

      if ((all_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
          ((all_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) &&
           vk_format_is_depth_or_stencil(info->format))) {
         if (!(format_feature_flags &
               VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            goto unsupported;
         }
      }
   }

   *pImageFormatProperties = (VkImageFormatProperties){
      .maxExtent = maxExtent,
      .maxMipLevels = maxMipLevels,
      .maxArrayLayers = maxArraySize,
      .sampleCounts = sampleCounts,

      /* We need to limit images to 32-bit range, because the maximum
       * slice-stride is 32-bit wide, meaning that if we allocate an image
       * with the maximum width and height, we end up overflowing it.
       *
       * We get around this by simply limiting the maximum resource size.
       */
      .maxResourceSize = UINT32_MAX,
   };

   if (p_feature_flags)
      *p_feature_flags = format_feature_flags;

   return VK_SUCCESS;
unsupported:
   *pImageFormatProperties = (VkImageFormatProperties){
      .maxExtent = {0, 0, 0},
      .maxMipLevels = 0,
      .maxArrayLayers = 0,
      .sampleCounts = 0,
      .maxResourceSize = 0,
   };

   return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

static VkResult
panvk_get_external_image_format_properties(
   const struct panvk_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkExternalMemoryHandleTypeFlagBits handleType,
   VkExternalMemoryProperties *external_properties)
{
   const VkExternalMemoryHandleTypeFlags supported_handle_types =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

   if (!(handleType & supported_handle_types)) {
      return panvk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                          "VkExternalMemoryTypeFlagBits(0x%x) unsupported",
                          handleType);
   }

   /* pan_image_layout_init requires 2D for explicit layout */
   if (pImageFormatInfo->type != VK_IMAGE_TYPE_2D) {
      return panvk_errorf(
         physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
         "VkExternalMemoryTypeFlagBits(0x%x) unsupported for VkImageType(%d)",
         handleType, pImageFormatInfo->type);
   }

   /* There is no restriction on opaque fds.  But for dma-bufs, we want to
    * make sure vkGetImageSubresourceLayout can be used to query the image
    * layout of an exported dma-buf.  We also want to make sure
    * VkImageDrmFormatModifierExplicitCreateInfoEXT can be used to specify the
    * image layout of an imported dma-buf.  These add restrictions on the
    * image tilings.
    */
   VkExternalMemoryFeatureFlags features = 0;
   if (handleType == VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT ||
       pImageFormatInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      features |= VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                  VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
   } else if (pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR) {
      features |= VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT;
   }

   if (!features) {
      return panvk_errorf(
         physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
         "VkExternalMemoryTypeFlagBits(0x%x) unsupported for VkImageTiling(%d)",
         handleType, pImageFormatInfo->tiling);
   }

   *external_properties = (VkExternalMemoryProperties){
      .externalMemoryFeatures = features,
      .exportFromImportedHandleTypes = supported_handle_types,
      .compatibleHandleTypes = supported_handle_types,
   };

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *base_info,
   VkImageFormatProperties2 *base_props)
{
   VK_FROM_HANDLE(panvk_physical_device, physical_device, physicalDevice);
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   const VkPhysicalDeviceImageViewImageFormatInfoEXT *image_view_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;
   VkFilterCubicImageViewImageFormatPropertiesEXT *cubic_props = NULL;
   VkFormatFeatureFlags format_feature_flags;
   VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = NULL;
   VkResult result;

   result = get_image_format_properties(physical_device, base_info,
                                        &base_props->imageFormatProperties,
                                        &format_feature_flags);
   if (result != VK_SUCCESS)
      return result;

   /* Extract input structs */
   vk_foreach_struct_const(s, base_info->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *)s;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT:
         image_view_info = (const void *)s;
         break;
      default:
         break;
      }
   }

   /* Extract output structs */
   vk_foreach_struct(s, base_props->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *)s;
         break;
      case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT:
         cubic_props = (void *)s;
         break;
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES:
         ycbcr_props = (void *)s;
         break;
      default:
         break;
      }
   }

   /* From the Vulkan 1.0.42 spec:
    *
    *    If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2 will
    *    behave as if VkPhysicalDeviceExternalImageFormatInfo was not
    *    present and VkExternalImageFormatProperties will be ignored.
    */
   if (external_info && external_info->handleType != 0) {
      VkExternalImageFormatProperties fallback_external_props;

      if (!external_props) {
         memset(&fallback_external_props, 0, sizeof(fallback_external_props));
         external_props = &fallback_external_props;
      }

      result = panvk_get_external_image_format_properties(
         physical_device, base_info, external_info->handleType,
         &external_props->externalMemoryProperties);
      if (result != VK_SUCCESS)
         goto fail;

      /* pan_image_layout_init requirements for explicit layout */
      base_props->imageFormatProperties.maxMipLevels = 1;
      base_props->imageFormatProperties.maxArrayLayers = 1;
      base_props->imageFormatProperties.sampleCounts = 1;
   }

   if (cubic_props) {
      /* note: blob only allows cubic filtering for 2D and 2D array views
       * its likely we can enable it for 1D and CUBE, needs testing however
       */
      if ((image_view_info->imageViewType == VK_IMAGE_VIEW_TYPE_2D ||
           image_view_info->imageViewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY) &&
          (format_feature_flags &
           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT)) {
         cubic_props->filterCubic = true;
         cubic_props->filterCubicMinmax = true;
      } else {
         cubic_props->filterCubic = false;
         cubic_props->filterCubicMinmax = false;
      }
   }

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(base_info->format);
   const unsigned plane_count =
      vk_format_get_plane_count(base_info->format);

   /* From the Vulkan 1.3.259 spec, VkImageCreateInfo:
    *
    *    VUID-VkImageCreateInfo-imageCreateFormatFeatures-02260
    *
    *    "If format is a multi-planar format, and if imageCreateFormatFeatures
    *    (as defined in Image Creation Limits) does not contain
    *    VK_FORMAT_FEATURE_DISJOINT_BIT, then flags must not contain
    *    VK_IMAGE_CREATE_DISJOINT_BIT"
    *
    * This is satisfied trivially because we support DISJOINT on all
    * multi-plane formats.  Also,
    *
    *    VUID-VkImageCreateInfo-format-01577
    *
    *    "If format is not a multi-planar format, and flags does not include
    *    VK_IMAGE_CREATE_ALIAS_BIT, flags must not contain
    *    VK_IMAGE_CREATE_DISJOINT_BIT"
    */
   if (plane_count == 1 &&
       !(base_info->flags & VK_IMAGE_CREATE_ALIAS_BIT) &&
       (base_info->flags & VK_IMAGE_CREATE_DISJOINT_BIT))
      goto fail;

   if (ycbcr_info &&
       ((base_info->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) ||
       (base_info->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)))
      goto fail;

   if ((base_info->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) &&
       (base_info->usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT))
      goto fail;

   if (ycbcr_props)
      ycbcr_props->combinedImageSamplerDescriptorCount = 1;

   return VK_SUCCESS;

fail:
   if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
      /* From the Vulkan 1.0.42 spec:
       *
       *    If the combination of parameters to
       *    vkGetPhysicalDeviceImageFormatProperties2 is not supported by
       *    the implementation for use in vkCreateImage, then all members of
       *    imageFormatProperties will be filled with zero.
       */
      base_props->imageFormatProperties = (VkImageFormatProperties){};
   }

   return result;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceSparseImageFormatProperties(
   VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
   VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling,
   uint32_t *pNumProperties, VkSparseImageFormatProperties *pProperties)
{
   /* Sparse images are not yet supported. */
   *pNumProperties = 0;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount, VkSparseImageFormatProperties2 *pProperties)
{
   /* Sparse images are not yet supported. */
   *pPropertyCount = 0;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   const VkExternalMemoryHandleTypeFlags supported_handle_types =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

   /* From the Vulkan 1.3.298 spec:
    *
    *    compatibleHandleTypes must include at least handleType.
    */
   VkExternalMemoryHandleTypeFlags handle_types =
      pExternalBufferInfo->handleType;
   VkExternalMemoryFeatureFlags features = 0;
   if (pExternalBufferInfo->handleType & supported_handle_types) {
      handle_types |= supported_handle_types;
      features |= VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                  VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
   }

   pExternalBufferProperties->externalMemoryProperties =
      (VkExternalMemoryProperties){
         .externalMemoryFeatures = features,
         .exportFromImportedHandleTypes = handle_types,
         .compatibleHandleTypes = handle_types,
      };
}
