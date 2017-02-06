/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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

#include <dlfcn.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "radv_private.h"
#include "radv_cs.h"
#include "util/strtod.h"

#include <xf86drm.h>
#include <amdgpu.h>
#include <amdgpu_drm.h>
#include "amdgpu_id.h"
#include "winsys/amdgpu/radv_amdgpu_winsys_public.h"
#include "ac_llvm_util.h"
#include "vk_format.h"
#include "sid.h"
#include "util/debug.h"
struct radv_dispatch_table dtable;

static int
radv_get_function_timestamp(void *ptr, uint32_t* timestamp)
{
	Dl_info info;
	struct stat st;
	if (!dladdr(ptr, &info) || !info.dli_fname) {
		return -1;
	}
	if (stat(info.dli_fname, &st)) {
		return -1;
	}
	*timestamp = st.st_mtim.tv_sec;
	return 0;
}

static int
radv_device_get_cache_uuid(enum radeon_family family, void *uuid)
{
	uint32_t mesa_timestamp, llvm_timestamp;
	uint16_t f = family;
	memset(uuid, 0, VK_UUID_SIZE);
	if (radv_get_function_timestamp(radv_device_get_cache_uuid, &mesa_timestamp) ||
	    radv_get_function_timestamp(LLVMInitializeAMDGPUTargetInfo, &llvm_timestamp))
		return -1;

	memcpy(uuid, &mesa_timestamp, 4);
	memcpy((char*)uuid + 4, &llvm_timestamp, 4);
	memcpy((char*)uuid + 8, &f, 2);
	snprintf((char*)uuid + 10, VK_UUID_SIZE - 10, "radv");
	return 0;
}

static const VkExtensionProperties instance_extensions[] = {
	{
		.extensionName = VK_KHR_SURFACE_EXTENSION_NAME,
		.specVersion = 25,
	},
#ifdef VK_USE_PLATFORM_XCB_KHR
	{
		.extensionName = VK_KHR_XCB_SURFACE_EXTENSION_NAME,
		.specVersion = 6,
	},
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
	{
		.extensionName = VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
		.specVersion = 6,
	},
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	{
		.extensionName = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
		.specVersion = 5,
	},
#endif
};

static const VkExtensionProperties common_device_extensions[] = {
	{
		.extensionName = VK_KHR_MAINTENANCE1_EXTENSION_NAME,
		.specVersion = 1,
	},
	{
		.extensionName = VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
		.specVersion = 1,
	},
	{
		.extensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		.specVersion = 68,
	},
	{
		.extensionName = VK_AMD_DRAW_INDIRECT_COUNT_EXTENSION_NAME,
		.specVersion = 1,
	},
	{
		.extensionName = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
		.specVersion = 1,
	},
	{
		.extensionName = VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
		.specVersion = 1,
	},
};

static VkResult
radv_extensions_register(struct radv_instance *instance,
			struct radv_extensions *extensions,
			const VkExtensionProperties *new_ext,
			uint32_t num_ext)
{
	size_t new_size;
	VkExtensionProperties *new_ptr;

	assert(new_ext && num_ext > 0);

	if (!new_ext)
		return VK_ERROR_INITIALIZATION_FAILED;

	new_size = (extensions->num_ext + num_ext) * sizeof(VkExtensionProperties);
	new_ptr = vk_realloc(&instance->alloc, extensions->ext_array,
				new_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);

	/* Old array continues to be valid, update nothing */
	if (!new_ptr)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	memcpy(&new_ptr[extensions->num_ext], new_ext,
		num_ext * sizeof(VkExtensionProperties));
	extensions->ext_array = new_ptr;
	extensions->num_ext += num_ext;

	return VK_SUCCESS;
}

static void
radv_extensions_finish(struct radv_instance *instance,
			struct radv_extensions *extensions)
{
	assert(extensions);

	if (!extensions)
		radv_loge("Attemted to free invalid extension struct\n");

	if (extensions->ext_array)
		vk_free(&instance->alloc, extensions->ext_array);
}

static bool
is_extension_enabled(const VkExtensionProperties *extensions,
			size_t num_ext,
			const char *name)
{
	assert(extensions && name);

	for (uint32_t i = 0; i < num_ext; i++) {
		if (strcmp(name, extensions[i].extensionName) == 0)
			return true;
	}

	return false;
}

static VkResult
radv_physical_device_init(struct radv_physical_device *device,
			  struct radv_instance *instance,
			  const char *path)
{
	VkResult result;
	drmVersionPtr version;
	int fd;

	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return VK_ERROR_INCOMPATIBLE_DRIVER;

	version = drmGetVersion(fd);
	if (!version) {
		close(fd);
		return vk_errorf(VK_ERROR_INCOMPATIBLE_DRIVER,
				 "failed to get version %s: %m", path);
	}

	if (strcmp(version->name, "amdgpu")) {
		drmFreeVersion(version);
		close(fd);
		return VK_ERROR_INCOMPATIBLE_DRIVER;
	}
	drmFreeVersion(version);

	device->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
	device->instance = instance;
	assert(strlen(path) < ARRAY_SIZE(device->path));
	strncpy(device->path, path, ARRAY_SIZE(device->path));

	device->ws = radv_amdgpu_winsys_create(fd);
	if (!device->ws) {
		result = VK_ERROR_INCOMPATIBLE_DRIVER;
		goto fail;
	}
	device->ws->query_info(device->ws, &device->rad_info);
	result = radv_init_wsi(device);
	if (result != VK_SUCCESS) {
		device->ws->destroy(device->ws);
		goto fail;
	}

	if (radv_device_get_cache_uuid(device->rad_info.family, device->uuid)) {
		radv_finish_wsi(device);
		device->ws->destroy(device->ws);
		result = vk_errorf(VK_ERROR_INITIALIZATION_FAILED,
				   "cannot generate UUID");
		goto fail;
	}

	result = radv_extensions_register(instance,
					&device->extensions,
					common_device_extensions,
					ARRAY_SIZE(common_device_extensions));
	if (result != VK_SUCCESS)
		goto fail;

	fprintf(stderr, "WARNING: radv is not a conformant vulkan implementation, testing use only.\n");
	device->name = device->rad_info.name;
	close(fd);
	return VK_SUCCESS;

fail:
	close(fd);
	return result;
}

static void
radv_physical_device_finish(struct radv_physical_device *device)
{
	radv_extensions_finish(device->instance, &device->extensions);
	radv_finish_wsi(device);
	device->ws->destroy(device->ws);
}


static void *
default_alloc_func(void *pUserData, size_t size, size_t align,
                   VkSystemAllocationScope allocationScope)
{
	return malloc(size);
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
	free(pMemory);
}

static const VkAllocationCallbacks default_alloc = {
	.pUserData = NULL,
	.pfnAllocation = default_alloc_func,
	.pfnReallocation = default_realloc_func,
	.pfnFree = default_free_func,
};

static const struct debug_control radv_debug_options[] = {
	{"fastclears", RADV_DEBUG_FAST_CLEARS},
	{"nodcc", RADV_DEBUG_NO_DCC},
	{"shaders", RADV_DEBUG_DUMP_SHADERS},
	{"nocache", RADV_DEBUG_NO_CACHE},
	{"shaderstats", RADV_DEBUG_DUMP_SHADER_STATS},
	{"nohiz", RADV_DEBUG_NO_HIZ},
	{"nocompute", RADV_DEBUG_NO_COMPUTE_QUEUE},
	{"unsafemath", RADV_DEBUG_UNSAFE_MATH},
	{NULL, 0}
};

VkResult radv_CreateInstance(
	const VkInstanceCreateInfo*                 pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkInstance*                                 pInstance)
{
	struct radv_instance *instance;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);

	uint32_t client_version;
	if (pCreateInfo->pApplicationInfo &&
	    pCreateInfo->pApplicationInfo->apiVersion != 0) {
		client_version = pCreateInfo->pApplicationInfo->apiVersion;
	} else {
		client_version = VK_MAKE_VERSION(1, 0, 0);
	}

	if (VK_MAKE_VERSION(1, 0, 0) > client_version ||
	    client_version > VK_MAKE_VERSION(1, 0, 0xfff)) {
		return vk_errorf(VK_ERROR_INCOMPATIBLE_DRIVER,
				 "Client requested version %d.%d.%d",
				 VK_VERSION_MAJOR(client_version),
				 VK_VERSION_MINOR(client_version),
				 VK_VERSION_PATCH(client_version));
	}

	for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
		if (!is_extension_enabled(instance_extensions,
					ARRAY_SIZE(instance_extensions),
					pCreateInfo->ppEnabledExtensionNames[i]))
			return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);
	}

	instance = vk_alloc2(&default_alloc, pAllocator, sizeof(*instance), 8,
			       VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
	if (!instance)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(instance, 0, sizeof(*instance));

	instance->_loader_data.loaderMagic = ICD_LOADER_MAGIC;

	if (pAllocator)
		instance->alloc = *pAllocator;
	else
		instance->alloc = default_alloc;

	instance->apiVersion = client_version;
	instance->physicalDeviceCount = -1;

	_mesa_locale_init();

	VG(VALGRIND_CREATE_MEMPOOL(instance, 0, false));

	instance->debug_flags = parse_debug_string(getenv("RADV_DEBUG"),
						   radv_debug_options);

	*pInstance = radv_instance_to_handle(instance);

	return VK_SUCCESS;
}

void radv_DestroyInstance(
	VkInstance                                  _instance,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);

	for (int i = 0; i < instance->physicalDeviceCount; ++i) {
		radv_physical_device_finish(instance->physicalDevices + i);
	}

	VG(VALGRIND_DESTROY_MEMPOOL(instance));

	_mesa_locale_fini();

	vk_free(&instance->alloc, instance);
}

VkResult radv_EnumeratePhysicalDevices(
	VkInstance                                  _instance,
	uint32_t*                                   pPhysicalDeviceCount,
	VkPhysicalDevice*                           pPhysicalDevices)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);
	VkResult result;

	if (instance->physicalDeviceCount < 0) {
		char path[20];
		instance->physicalDeviceCount = 0;
		for (unsigned i = 0; i < RADV_MAX_DRM_DEVICES; i++) {
			snprintf(path, sizeof(path), "/dev/dri/renderD%d", 128 + i);
			result = radv_physical_device_init(instance->physicalDevices +
			                                   instance->physicalDeviceCount,
			                                   instance, path);
			if (result == VK_SUCCESS)
				++instance->physicalDeviceCount;
			else if (result != VK_ERROR_INCOMPATIBLE_DRIVER)
				return result;
		}
	}

	if (!pPhysicalDevices) {
		*pPhysicalDeviceCount = instance->physicalDeviceCount;
	} else {
		*pPhysicalDeviceCount = MIN2(*pPhysicalDeviceCount, instance->physicalDeviceCount);
		for (unsigned i = 0; i < *pPhysicalDeviceCount; ++i)
			pPhysicalDevices[i] = radv_physical_device_to_handle(instance->physicalDevices + i);
	}

	return *pPhysicalDeviceCount < instance->physicalDeviceCount ? VK_INCOMPLETE
	                                                             : VK_SUCCESS;
}

void radv_GetPhysicalDeviceFeatures(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceFeatures*                   pFeatures)
{
	//   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);

	memset(pFeatures, 0, sizeof(*pFeatures));

	*pFeatures = (VkPhysicalDeviceFeatures) {
		.robustBufferAccess                       = true,
		.fullDrawIndexUint32                      = true,
		.imageCubeArray                           = true,
		.independentBlend                         = true,
		.geometryShader                           = true,
		.tessellationShader                       = false,
		.sampleRateShading                        = false,
		.dualSrcBlend                             = true,
		.logicOp                                  = true,
		.multiDrawIndirect                        = true,
		.drawIndirectFirstInstance                = true,
		.depthClamp                               = true,
		.depthBiasClamp                           = true,
		.fillModeNonSolid                         = true,
		.depthBounds                              = true,
		.wideLines                                = true,
		.largePoints                              = true,
		.alphaToOne                               = true,
		.multiViewport                            = true,
		.samplerAnisotropy                        = true,
		.textureCompressionETC2                   = false,
		.textureCompressionASTC_LDR               = false,
		.textureCompressionBC                     = true,
		.occlusionQueryPrecise                    = true,
		.pipelineStatisticsQuery                  = false,
		.vertexPipelineStoresAndAtomics           = true,
		.fragmentStoresAndAtomics                 = true,
		.shaderTessellationAndGeometryPointSize   = true,
		.shaderImageGatherExtended                = true,
		.shaderStorageImageExtendedFormats        = true,
		.shaderStorageImageMultisample            = false,
		.shaderUniformBufferArrayDynamicIndexing  = true,
		.shaderSampledImageArrayDynamicIndexing   = true,
		.shaderStorageBufferArrayDynamicIndexing  = true,
		.shaderStorageImageArrayDynamicIndexing   = true,
		.shaderStorageImageReadWithoutFormat      = false,
		.shaderStorageImageWriteWithoutFormat     = false,
		.shaderClipDistance                       = true,
		.shaderCullDistance                       = true,
		.shaderFloat64                            = true,
		.shaderInt64                              = false,
		.shaderInt16                              = false,
		.alphaToOne                               = true,
		.variableMultisampleRate                  = false,
		.inheritedQueries                         = false,
	};
}

void radv_GetPhysicalDeviceFeatures2KHR(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceFeatures2KHR               *pFeatures)
{
	return radv_GetPhysicalDeviceFeatures(physicalDevice, &pFeatures->features);
}

void radv_GetPhysicalDeviceProperties(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceProperties*                 pProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
	VkSampleCountFlags sample_counts = 0xf;
	VkPhysicalDeviceLimits limits = {
		.maxImageDimension1D                      = (1 << 14),
		.maxImageDimension2D                      = (1 << 14),
		.maxImageDimension3D                      = (1 << 11),
		.maxImageDimensionCube                    = (1 << 14),
		.maxImageArrayLayers                      = (1 << 11),
		.maxTexelBufferElements                   = 128 * 1024 * 1024,
		.maxUniformBufferRange                    = UINT32_MAX,
		.maxStorageBufferRange                    = UINT32_MAX,
		.maxPushConstantsSize                     = MAX_PUSH_CONSTANTS_SIZE,
		.maxMemoryAllocationCount                 = UINT32_MAX,
		.maxSamplerAllocationCount                = 64 * 1024,
		.bufferImageGranularity                   = 64, /* A cache line */
		.sparseAddressSpaceSize                   = 0,
		.maxBoundDescriptorSets                   = MAX_SETS,
		.maxPerStageDescriptorSamplers            = 64,
		.maxPerStageDescriptorUniformBuffers      = 64,
		.maxPerStageDescriptorStorageBuffers      = 64,
		.maxPerStageDescriptorSampledImages       = 64,
		.maxPerStageDescriptorStorageImages       = 64,
		.maxPerStageDescriptorInputAttachments    = 64,
		.maxPerStageResources                     = 128,
		.maxDescriptorSetSamplers                 = 256,
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
		.maxTessellationGenerationLevel           = 0,
		.maxTessellationPatchSize                 = 0,
		.maxTessellationControlPerVertexInputComponents = 0,
		.maxTessellationControlPerVertexOutputComponents = 0,
		.maxTessellationControlPerPatchOutputComponents = 0,
		.maxTessellationControlTotalOutputComponents = 0,
		.maxTessellationEvaluationInputComponents = 0,
		.maxTessellationEvaluationOutputComponents = 0,
		.maxGeometryShaderInvocations             = 32,
		.maxGeometryInputComponents               = 64,
		.maxGeometryOutputComponents              = 128,
		.maxGeometryOutputVertices                = 256,
		.maxGeometryTotalOutputComponents         = 1024,
		.maxFragmentInputComponents               = 128,
		.maxFragmentOutputAttachments             = 8,
		.maxFragmentDualSrcAttachments            = 1,
		.maxFragmentCombinedOutputResources       = 8,
		.maxComputeSharedMemorySize               = 32768,
		.maxComputeWorkGroupCount                 = { 65535, 65535, 65535 },
		.maxComputeWorkGroupInvocations           = 2048,
		.maxComputeWorkGroupSize = {
			2048,
			2048,
			2048
		},
		.subPixelPrecisionBits                    = 4 /* FIXME */,
		.subTexelPrecisionBits                    = 4 /* FIXME */,
		.mipmapPrecisionBits                      = 4 /* FIXME */,
		.maxDrawIndexedIndexValue                 = UINT32_MAX,
		.maxDrawIndirectCount                     = UINT32_MAX,
		.maxSamplerLodBias                        = 16,
		.maxSamplerAnisotropy                     = 16,
		.maxViewports                             = MAX_VIEWPORTS,
		.maxViewportDimensions                    = { (1 << 14), (1 << 14) },
		.viewportBoundsRange                      = { INT16_MIN, INT16_MAX },
		.viewportSubPixelBits                     = 13, /* We take a float? */
		.minMemoryMapAlignment                    = 4096, /* A page */
		.minTexelBufferOffsetAlignment            = 1,
		.minUniformBufferOffsetAlignment          = 4,
		.minStorageBufferOffsetAlignment          = 4,
		.minTexelOffset                           = -32,
		.maxTexelOffset                           = 31,
		.minTexelGatherOffset                     = -32,
		.maxTexelGatherOffset                     = 31,
		.minInterpolationOffset                   = -2,
		.maxInterpolationOffset                   = 2,
		.subPixelInterpolationOffsetBits          = 8,
		.maxFramebufferWidth                      = (1 << 14),
		.maxFramebufferHeight                     = (1 << 14),
		.maxFramebufferLayers                     = (1 << 10),
		.framebufferColorSampleCounts             = sample_counts,
		.framebufferDepthSampleCounts             = sample_counts,
		.framebufferStencilSampleCounts           = sample_counts,
		.framebufferNoAttachmentsSampleCounts     = sample_counts,
		.maxColorAttachments                      = MAX_RTS,
		.sampledImageColorSampleCounts            = sample_counts,
		.sampledImageIntegerSampleCounts          = VK_SAMPLE_COUNT_1_BIT,
		.sampledImageDepthSampleCounts            = sample_counts,
		.sampledImageStencilSampleCounts          = sample_counts,
		.storageImageSampleCounts                 = VK_SAMPLE_COUNT_1_BIT,
		.maxSampleMaskWords                       = 1,
		.timestampComputeAndGraphics              = false,
		.timestampPeriod                          = 100000.0 / pdevice->rad_info.clock_crystal_freq,
		.maxClipDistances                         = 8,
		.maxCullDistances                         = 8,
		.maxCombinedClipAndCullDistances          = 8,
		.discreteQueuePriorities                  = 1,
		.pointSizeRange                           = { 0.125, 255.875 },
		.lineWidthRange                           = { 0.0, 7.9921875 },
		.pointSizeGranularity                     = (1.0 / 8.0),
		.lineWidthGranularity                     = (1.0 / 128.0),
		.strictLines                              = false, /* FINISHME */
		.standardSampleLocations                  = true,
		.optimalBufferCopyOffsetAlignment         = 128,
		.optimalBufferCopyRowPitchAlignment       = 128,
		.nonCoherentAtomSize                      = 64,
	};

	*pProperties = (VkPhysicalDeviceProperties) {
		.apiVersion = VK_MAKE_VERSION(1, 0, 5),
		.driverVersion = 1,
		.vendorID = 0x1002,
		.deviceID = pdevice->rad_info.pci_id,
		.deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
		.limits = limits,
		.sparseProperties = {0}, /* Broadwell doesn't do sparse. */
	};

	strcpy(pProperties->deviceName, pdevice->name);
	memcpy(pProperties->pipelineCacheUUID, pdevice->uuid, VK_UUID_SIZE);
}

void radv_GetPhysicalDeviceProperties2KHR(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceProperties2KHR             *pProperties)
{
	return radv_GetPhysicalDeviceProperties(physicalDevice, &pProperties->properties);
}

void radv_GetPhysicalDeviceQueueFamilyProperties(
	VkPhysicalDevice                            physicalDevice,
	uint32_t*                                   pCount,
	VkQueueFamilyProperties*                    pQueueFamilyProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
	int num_queue_families = 1;
	int idx;
	if (pdevice->rad_info.compute_rings > 0 &&
	    pdevice->rad_info.chip_class >= CIK &&
	    !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE))
		num_queue_families++;

	if (pQueueFamilyProperties == NULL) {
		*pCount = num_queue_families;
		return;
	}

	if (!*pCount)
		return;

	idx = 0;
	if (*pCount >= 1) {
		pQueueFamilyProperties[idx] = (VkQueueFamilyProperties) {
			.queueFlags = VK_QUEUE_GRAPHICS_BIT |
			VK_QUEUE_COMPUTE_BIT |
			VK_QUEUE_TRANSFER_BIT,
			.queueCount = 1,
			.timestampValidBits = 64,
			.minImageTransferGranularity = (VkExtent3D) { 1, 1, 1 },
		};
		idx++;
	}

	if (pdevice->rad_info.compute_rings > 0 &&
	    pdevice->rad_info.chip_class >= CIK &&
	    !(pdevice->instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE)) {
		if (*pCount > idx) {
			pQueueFamilyProperties[idx] = (VkQueueFamilyProperties) {
				.queueFlags = VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
				.queueCount = pdevice->rad_info.compute_rings,
				.timestampValidBits = 64,
				.minImageTransferGranularity = (VkExtent3D) { 1, 1, 1 },
			};
			idx++;
		}
	}
	*pCount = idx;
}

void radv_GetPhysicalDeviceQueueFamilyProperties2KHR(
	VkPhysicalDevice                            physicalDevice,
	uint32_t*                                   pCount,
	VkQueueFamilyProperties2KHR                *pQueueFamilyProperties)
{
	return radv_GetPhysicalDeviceQueueFamilyProperties(physicalDevice,
							   pCount,
							   &pQueueFamilyProperties->queueFamilyProperties);
}

void radv_GetPhysicalDeviceMemoryProperties(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceMemoryProperties           *pMemoryProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);

	STATIC_ASSERT(RADV_MEM_TYPE_COUNT <= VK_MAX_MEMORY_TYPES);

	pMemoryProperties->memoryTypeCount = RADV_MEM_TYPE_COUNT;
	pMemoryProperties->memoryTypes[RADV_MEM_TYPE_VRAM] = (VkMemoryType) {
		.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		.heapIndex = RADV_MEM_HEAP_VRAM,
	};
	pMemoryProperties->memoryTypes[RADV_MEM_TYPE_GTT_WRITE_COMBINE] = (VkMemoryType) {
		.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		.heapIndex = RADV_MEM_HEAP_GTT,
	};
	pMemoryProperties->memoryTypes[RADV_MEM_TYPE_VRAM_CPU_ACCESS] = (VkMemoryType) {
		.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		.heapIndex = RADV_MEM_HEAP_VRAM_CPU_ACCESS,
	};
	pMemoryProperties->memoryTypes[RADV_MEM_TYPE_GTT_CACHED] = (VkMemoryType) {
		.propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
		VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		.heapIndex = RADV_MEM_HEAP_GTT,
	};

	STATIC_ASSERT(RADV_MEM_HEAP_COUNT <= VK_MAX_MEMORY_HEAPS);

	pMemoryProperties->memoryHeapCount = RADV_MEM_HEAP_COUNT;
	pMemoryProperties->memoryHeaps[RADV_MEM_HEAP_VRAM] = (VkMemoryHeap) {
		.size = physical_device->rad_info.vram_size -
				physical_device->rad_info.visible_vram_size,
		.flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
	};
	pMemoryProperties->memoryHeaps[RADV_MEM_HEAP_VRAM_CPU_ACCESS] = (VkMemoryHeap) {
		.size = physical_device->rad_info.visible_vram_size,
		.flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
	};
	pMemoryProperties->memoryHeaps[RADV_MEM_HEAP_GTT] = (VkMemoryHeap) {
		.size = physical_device->rad_info.gart_size,
		.flags = 0,
	};
}

void radv_GetPhysicalDeviceMemoryProperties2KHR(
	VkPhysicalDevice                            physicalDevice,
	VkPhysicalDeviceMemoryProperties2KHR       *pMemoryProperties)
{
	return radv_GetPhysicalDeviceMemoryProperties(physicalDevice,
						      &pMemoryProperties->memoryProperties);
}

static int
radv_queue_init(struct radv_device *device, struct radv_queue *queue,
		int queue_family_index, int idx)
{
	queue->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
	queue->device = device;
	queue->queue_family_index = queue_family_index;
	queue->queue_idx = idx;

	queue->hw_ctx = device->ws->ctx_create(device->ws);
	if (!queue->hw_ctx)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	return VK_SUCCESS;
}

static void
radv_queue_finish(struct radv_queue *queue)
{
	if (queue->hw_ctx)
		queue->device->ws->ctx_destroy(queue->hw_ctx);

	if (queue->preamble_cs)
		queue->device->ws->cs_destroy(queue->preamble_cs);
	if (queue->descriptor_bo)
		queue->device->ws->buffer_destroy(queue->descriptor_bo);
	if (queue->scratch_bo)
		queue->device->ws->buffer_destroy(queue->scratch_bo);
	if (queue->esgs_ring_bo)
		queue->device->ws->buffer_destroy(queue->esgs_ring_bo);
	if (queue->gsvs_ring_bo)
		queue->device->ws->buffer_destroy(queue->gsvs_ring_bo);
	if (queue->compute_scratch_bo)
		queue->device->ws->buffer_destroy(queue->compute_scratch_bo);
}

static void
radv_device_init_gs_info(struct radv_device *device)
{
	switch (device->physical_device->rad_info.family) {
	case CHIP_OLAND:
	case CHIP_HAINAN:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_ICELAND:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		device->gs_table_depth = 16;
		return;
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
		device->gs_table_depth = 32;
		return;
	default:
		unreachable("unknown GPU");
	}
}

VkResult radv_CreateDevice(
	VkPhysicalDevice                            physicalDevice,
	const VkDeviceCreateInfo*                   pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkDevice*                                   pDevice)
{
	RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);
	VkResult result;
	struct radv_device *device;

	for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
		if (!is_extension_enabled(physical_device->extensions.ext_array,
					physical_device->extensions.num_ext,
					pCreateInfo->ppEnabledExtensionNames[i]))
			return vk_error(VK_ERROR_EXTENSION_NOT_PRESENT);
	}

	device = vk_alloc2(&physical_device->instance->alloc, pAllocator,
			     sizeof(*device), 8,
			     VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
	if (!device)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(device, 0, sizeof(*device));

	device->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
	device->instance = physical_device->instance;
	device->physical_device = physical_device;

	device->debug_flags = device->instance->debug_flags;

	device->ws = physical_device->ws;
	if (pAllocator)
		device->alloc = *pAllocator;
	else
		device->alloc = physical_device->instance->alloc;

	for (unsigned i = 0; i < pCreateInfo->queueCreateInfoCount; i++) {
		const VkDeviceQueueCreateInfo *queue_create = &pCreateInfo->pQueueCreateInfos[i];
		uint32_t qfi = queue_create->queueFamilyIndex;

		device->queues[qfi] = vk_alloc(&device->alloc,
					       queue_create->queueCount * sizeof(struct radv_queue), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
		if (!device->queues[qfi]) {
			result = VK_ERROR_OUT_OF_HOST_MEMORY;
			goto fail;
		}

		memset(device->queues[qfi], 0, queue_create->queueCount * sizeof(struct radv_queue));

		device->queue_count[qfi] = queue_create->queueCount;

		for (unsigned q = 0; q < queue_create->queueCount; q++) {
			result = radv_queue_init(device, &device->queues[qfi][q], qfi, q);
			if (result != VK_SUCCESS)
				goto fail;
		}
	}

#if HAVE_LLVM < 0x0400
	device->llvm_supports_spill = false;
#else
	device->llvm_supports_spill = true;
#endif

	/* The maximum number of scratch waves. Scratch space isn't divided
	 * evenly between CUs. The number is only a function of the number of CUs.
	 * We can decrease the constant to decrease the scratch buffer size.
	 *
	 * sctx->scratch_waves must be >= the maximum posible size of
	 * 1 threadgroup, so that the hw doesn't hang from being unable
	 * to start any.
	 *
	 * The recommended value is 4 per CU at most. Higher numbers don't
	 * bring much benefit, but they still occupy chip resources (think
	 * async compute). I've seen ~2% performance difference between 4 and 32.
	 */
	uint32_t max_threads_per_block = 2048;
	device->scratch_waves = MAX2(32 * physical_device->rad_info.num_good_compute_units,
				     max_threads_per_block / 64);

	radv_device_init_gs_info(device);

	result = radv_device_init_meta(device);
	if (result != VK_SUCCESS)
		goto fail;

	radv_device_init_msaa(device);

	for (int family = 0; family < RADV_MAX_QUEUE_FAMILIES; ++family) {
		device->empty_cs[family] = device->ws->cs_create(device->ws, family);
		switch (family) {
		case RADV_QUEUE_GENERAL:
			radeon_emit(device->empty_cs[family], PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
			radeon_emit(device->empty_cs[family], CONTEXT_CONTROL_LOAD_ENABLE(1));
			radeon_emit(device->empty_cs[family], CONTEXT_CONTROL_SHADOW_ENABLE(1));
			break;
		case RADV_QUEUE_COMPUTE:
			radeon_emit(device->empty_cs[family], PKT3(PKT3_NOP, 0, 0));
			radeon_emit(device->empty_cs[family], 0);
			break;
		}
		device->ws->cs_finalize(device->empty_cs[family]);
	}

	if (getenv("RADV_TRACE_FILE")) {
		device->trace_bo = device->ws->buffer_create(device->ws, 4096, 8,
							     RADEON_DOMAIN_VRAM, RADEON_FLAG_CPU_ACCESS);
		if (!device->trace_bo)
			goto fail;

		device->trace_id_ptr = device->ws->buffer_map(device->trace_bo);
		if (!device->trace_id_ptr)
			goto fail;
	}

	*pDevice = radv_device_to_handle(device);
	return VK_SUCCESS;

fail:
	if (device->trace_bo)
		device->ws->buffer_destroy(device->trace_bo);

	for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
		for (unsigned q = 0; q < device->queue_count[i]; q++)
			radv_queue_finish(&device->queues[i][q]);
		if (device->queue_count[i])
			vk_free(&device->alloc, device->queues[i]);
	}

	vk_free(&device->alloc, device);
	return result;
}

void radv_DestroyDevice(
	VkDevice                                    _device,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);

	if (device->trace_bo)
		device->ws->buffer_destroy(device->trace_bo);

	for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
		for (unsigned q = 0; q < device->queue_count[i]; q++)
			radv_queue_finish(&device->queues[i][q]);
		if (device->queue_count[i])
			vk_free(&device->alloc, device->queues[i]);
	}
	radv_device_finish_meta(device);

	vk_free(&device->alloc, device);
}

VkResult radv_EnumerateInstanceExtensionProperties(
	const char*                                 pLayerName,
	uint32_t*                                   pPropertyCount,
	VkExtensionProperties*                      pProperties)
{
	if (pProperties == NULL) {
		*pPropertyCount = ARRAY_SIZE(instance_extensions);
		return VK_SUCCESS;
	}

	*pPropertyCount = MIN2(*pPropertyCount, ARRAY_SIZE(instance_extensions));
	typed_memcpy(pProperties, instance_extensions, *pPropertyCount);

	if (*pPropertyCount < ARRAY_SIZE(instance_extensions))
		return VK_INCOMPLETE;

	return VK_SUCCESS;
}

VkResult radv_EnumerateDeviceExtensionProperties(
	VkPhysicalDevice                            physicalDevice,
	const char*                                 pLayerName,
	uint32_t*                                   pPropertyCount,
	VkExtensionProperties*                      pProperties)
{
	RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);

	if (pProperties == NULL) {
		*pPropertyCount = pdevice->extensions.num_ext;
		return VK_SUCCESS;
	}

	*pPropertyCount = MIN2(*pPropertyCount, pdevice->extensions.num_ext);
	typed_memcpy(pProperties, pdevice->extensions.ext_array, *pPropertyCount);

	if (*pPropertyCount < pdevice->extensions.num_ext)
		return VK_INCOMPLETE;

	return VK_SUCCESS;
}

VkResult radv_EnumerateInstanceLayerProperties(
	uint32_t*                                   pPropertyCount,
	VkLayerProperties*                          pProperties)
{
	if (pProperties == NULL) {
		*pPropertyCount = 0;
		return VK_SUCCESS;
	}

	/* None supported at this time */
	return vk_error(VK_ERROR_LAYER_NOT_PRESENT);
}

VkResult radv_EnumerateDeviceLayerProperties(
	VkPhysicalDevice                            physicalDevice,
	uint32_t*                                   pPropertyCount,
	VkLayerProperties*                          pProperties)
{
	if (pProperties == NULL) {
		*pPropertyCount = 0;
		return VK_SUCCESS;
	}

	/* None supported at this time */
	return vk_error(VK_ERROR_LAYER_NOT_PRESENT);
}

void radv_GetDeviceQueue(
	VkDevice                                    _device,
	uint32_t                                    queueFamilyIndex,
	uint32_t                                    queueIndex,
	VkQueue*                                    pQueue)
{
	RADV_FROM_HANDLE(radv_device, device, _device);

	*pQueue = radv_queue_to_handle(&device->queues[queueFamilyIndex][queueIndex]);
}

static void radv_dump_trace(struct radv_device *device,
			    struct radeon_winsys_cs *cs)
{
	const char *filename = getenv("RADV_TRACE_FILE");
	FILE *f = fopen(filename, "w");
	if (!f) {
		fprintf(stderr, "Failed to write trace dump to %s\n", filename);
		return;
	}

	fprintf(f, "Trace ID: %x\n", *device->trace_id_ptr);
	device->ws->cs_dump(cs, f, *device->trace_id_ptr);
	fclose(f);
}

static void
fill_geom_rings(struct radv_queue *queue,
		uint32_t *map,
		uint32_t esgs_ring_size,
		struct radeon_winsys_bo *esgs_ring_bo,
		uint32_t gsvs_ring_size,
		struct radeon_winsys_bo *gsvs_ring_bo)
{
	uint64_t esgs_va = 0, gsvs_va = 0;
	uint32_t *desc = &map[4];

	if (esgs_ring_bo)
		esgs_va = queue->device->ws->buffer_get_va(esgs_ring_bo);
	if (gsvs_ring_bo)
		gsvs_va = queue->device->ws->buffer_get_va(gsvs_ring_bo);

	/* stride 0, num records - size, add tid, swizzle, elsize4,
	   index stride 64 */
	desc[0] = esgs_va;
	desc[1] = S_008F04_BASE_ADDRESS_HI(esgs_va >> 32) |
		S_008F04_STRIDE(0) |
		S_008F04_SWIZZLE_ENABLE(true);
	desc[2] = esgs_ring_size;
	desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
		S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
		S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
		S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
		S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
		S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) |
		S_008F0C_ELEMENT_SIZE(1) |
		S_008F0C_INDEX_STRIDE(3) |
		S_008F0C_ADD_TID_ENABLE(true);

	desc += 4;
	/* GS entry for ES->GS ring */
	/* stride 0, num records - size, elsize0,
	   index stride 0 */
	desc[0] = esgs_va;
	desc[1] = S_008F04_BASE_ADDRESS_HI(esgs_va >> 32)|
		S_008F04_STRIDE(0) |
		S_008F04_SWIZZLE_ENABLE(false);
	desc[2] = esgs_ring_size;
	desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
		S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
		S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
		S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
		S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
		S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) |
		S_008F0C_ELEMENT_SIZE(0) |
		S_008F0C_INDEX_STRIDE(0) |
		S_008F0C_ADD_TID_ENABLE(false);

	desc += 4;
	/* VS entry for GS->VS ring */
	/* stride 0, num records - size, elsize0,
	   index stride 0 */
	desc[0] = gsvs_va;
	desc[1] = S_008F04_BASE_ADDRESS_HI(gsvs_va >> 32)|
		S_008F04_STRIDE(0) |
		S_008F04_SWIZZLE_ENABLE(false);
	desc[2] = gsvs_ring_size;
	desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
		S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
		S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
		S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
		S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
		S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) |
		S_008F0C_ELEMENT_SIZE(0) |
		S_008F0C_INDEX_STRIDE(0) |
		S_008F0C_ADD_TID_ENABLE(false);
	desc += 4;

	/* stride gsvs_itemsize, num records 64
	   elsize 4, index stride 16 */
	/* shader will patch stride and desc[2] */
	desc[0] = gsvs_va;
	desc[1] = S_008F04_BASE_ADDRESS_HI(gsvs_va >> 32)|
		S_008F04_STRIDE(0) |
		S_008F04_SWIZZLE_ENABLE(true);
	desc[2] = 0;
	desc[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
		S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
		S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
		S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
		S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
		S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32) |
		S_008F0C_ELEMENT_SIZE(1) |
		S_008F0C_INDEX_STRIDE(1) |
		S_008F0C_ADD_TID_ENABLE(true);
}

static VkResult
radv_get_preamble_cs(struct radv_queue *queue,
                     uint32_t scratch_size,
                     uint32_t compute_scratch_size,
		     uint32_t esgs_ring_size,
		     uint32_t gsvs_ring_size,
                     struct radeon_winsys_cs **preamble_cs)
{
	struct radeon_winsys_bo *scratch_bo = NULL;
	struct radeon_winsys_bo *descriptor_bo = NULL;
	struct radeon_winsys_bo *compute_scratch_bo = NULL;
	struct radeon_winsys_bo *esgs_ring_bo = NULL;
	struct radeon_winsys_bo *gsvs_ring_bo = NULL;
	struct radeon_winsys_cs *cs = NULL;

	if (!scratch_size && !compute_scratch_size && !esgs_ring_size && !gsvs_ring_size) {
		*preamble_cs = NULL;
		return VK_SUCCESS;
	}

	if (scratch_size <= queue->scratch_size &&
	    compute_scratch_size <= queue->compute_scratch_size &&
	    esgs_ring_size <= queue->esgs_ring_size &&
	    gsvs_ring_size <= queue->gsvs_ring_size) {
		*preamble_cs = queue->preamble_cs;
		return VK_SUCCESS;
	}

	if (scratch_size > queue->scratch_size) {
		scratch_bo = queue->device->ws->buffer_create(queue->device->ws,
		                                              scratch_size,
		                                              4096,
		                                              RADEON_DOMAIN_VRAM,
		                                              RADEON_FLAG_NO_CPU_ACCESS);
		if (!scratch_bo)
			goto fail;
	} else
		scratch_bo = queue->scratch_bo;

	if (compute_scratch_size > queue->compute_scratch_size) {
		compute_scratch_bo = queue->device->ws->buffer_create(queue->device->ws,
		                                                      compute_scratch_size,
		                                                      4096,
		                                                      RADEON_DOMAIN_VRAM,
		                                                      RADEON_FLAG_NO_CPU_ACCESS);
		if (!compute_scratch_bo)
			goto fail;

	} else
		compute_scratch_bo = queue->compute_scratch_bo;

	if (esgs_ring_size > queue->esgs_ring_size) {
		esgs_ring_bo = queue->device->ws->buffer_create(queue->device->ws,
								esgs_ring_size,
								4096,
								RADEON_DOMAIN_VRAM,
								RADEON_FLAG_NO_CPU_ACCESS);
		if (!esgs_ring_bo)
			goto fail;
	} else {
		esgs_ring_bo = queue->esgs_ring_bo;
		esgs_ring_size = queue->esgs_ring_size;
	}

	if (gsvs_ring_size > queue->gsvs_ring_size) {
		gsvs_ring_bo = queue->device->ws->buffer_create(queue->device->ws,
								gsvs_ring_size,
								4096,
								RADEON_DOMAIN_VRAM,
								RADEON_FLAG_NO_CPU_ACCESS);
		if (!gsvs_ring_bo)
			goto fail;
	} else {
		gsvs_ring_bo = queue->gsvs_ring_bo;
		gsvs_ring_size = queue->gsvs_ring_size;
	}

	if (scratch_bo != queue->scratch_bo ||
	    esgs_ring_bo != queue->esgs_ring_bo ||
	    gsvs_ring_bo != queue->gsvs_ring_bo) {
		uint32_t size = 0;
		if (gsvs_ring_bo || esgs_ring_bo)
			size = 80; /* 2 dword + 2 padding + 4 dword * 4 */
		else if (scratch_bo)
			size = 8; /* 2 dword */

		descriptor_bo = queue->device->ws->buffer_create(queue->device->ws,
		                                                 size,
		                                                 4096,
		                                                 RADEON_DOMAIN_VRAM,
		                                                 RADEON_FLAG_CPU_ACCESS);
		if (!descriptor_bo)
			goto fail;
	} else
		descriptor_bo = queue->descriptor_bo;

	cs = queue->device->ws->cs_create(queue->device->ws,
	                                  queue->queue_family_index ? RING_COMPUTE : RING_GFX);
	if (!cs)
		goto fail;


	if (scratch_bo)
		queue->device->ws->cs_add_buffer(cs, scratch_bo, 8);

	if (esgs_ring_bo)
		queue->device->ws->cs_add_buffer(cs, esgs_ring_bo, 8);

	if (gsvs_ring_bo)
		queue->device->ws->cs_add_buffer(cs, gsvs_ring_bo, 8);

	if (descriptor_bo)
		queue->device->ws->cs_add_buffer(cs, descriptor_bo, 8);

	if (descriptor_bo != queue->descriptor_bo) {
		uint32_t *map = (uint32_t*)queue->device->ws->buffer_map(descriptor_bo);

		if (scratch_bo) {
			uint64_t scratch_va = queue->device->ws->buffer_get_va(scratch_bo);
			uint32_t rsrc1 = S_008F04_BASE_ADDRESS_HI(scratch_va >> 32) |
				S_008F04_SWIZZLE_ENABLE(1);
			map[0] = scratch_va;
			map[1] = rsrc1;
		}

		if (esgs_ring_bo || gsvs_ring_bo)
			fill_geom_rings(queue, map, esgs_ring_size, esgs_ring_bo, gsvs_ring_size, gsvs_ring_bo);

		queue->device->ws->buffer_unmap(descriptor_bo);
	}

	if (esgs_ring_bo || gsvs_ring_bo) {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));

		if (queue->device->physical_device->rad_info.chip_class >= CIK) {
			radeon_set_uconfig_reg_seq(cs, R_030900_VGT_ESGS_RING_SIZE, 2);
			radeon_emit(cs, esgs_ring_size >> 8);
			radeon_emit(cs, gsvs_ring_size >> 8);
		} else {
			radeon_set_config_reg_seq(cs, R_0088C8_VGT_ESGS_RING_SIZE, 2);
			radeon_emit(cs, esgs_ring_size >> 8);
			radeon_emit(cs, gsvs_ring_size >> 8);
		}
	}

	if (descriptor_bo) {
		uint32_t regs[] = {R_00B030_SPI_SHADER_USER_DATA_PS_0,
		                   R_00B130_SPI_SHADER_USER_DATA_VS_0,
		                   R_00B230_SPI_SHADER_USER_DATA_GS_0,
		                   R_00B330_SPI_SHADER_USER_DATA_ES_0,
				   R_00B430_SPI_SHADER_USER_DATA_HS_0,
				   R_00B530_SPI_SHADER_USER_DATA_LS_0};

		uint64_t va = queue->device->ws->buffer_get_va(descriptor_bo);

		for (int i = 0; i < ARRAY_SIZE(regs); ++i) {
			radeon_set_sh_reg_seq(cs, regs[i], 2);
			radeon_emit(cs, va);
			radeon_emit(cs, va >> 32);
		}
	}

	if (compute_scratch_bo) {
		uint64_t scratch_va = queue->device->ws->buffer_get_va(compute_scratch_bo);
		uint32_t rsrc1 = S_008F04_BASE_ADDRESS_HI(scratch_va >> 32) |
		                 S_008F04_SWIZZLE_ENABLE(1);

		queue->device->ws->cs_add_buffer(cs, compute_scratch_bo, 8);

		radeon_set_sh_reg_seq(cs, R_00B900_COMPUTE_USER_DATA_0, 2);
		radeon_emit(cs, scratch_va);
		radeon_emit(cs, rsrc1);
	}

	if (!queue->device->ws->cs_finalize(cs))
		goto fail;

	if (queue->preamble_cs)
		queue->device->ws->cs_destroy(queue->preamble_cs);

	queue->preamble_cs = cs;

	if (scratch_bo != queue->scratch_bo) {
		if (queue->scratch_bo)
			queue->device->ws->buffer_destroy(queue->scratch_bo);
		queue->scratch_bo = scratch_bo;
		queue->scratch_size = scratch_size;
	}

	if (compute_scratch_bo != queue->compute_scratch_bo) {
		if (queue->compute_scratch_bo)
			queue->device->ws->buffer_destroy(queue->compute_scratch_bo);
		queue->compute_scratch_bo = compute_scratch_bo;
		queue->compute_scratch_size = compute_scratch_size;
	}

	if (esgs_ring_bo != queue->esgs_ring_bo) {
		if (queue->esgs_ring_bo)
			queue->device->ws->buffer_destroy(queue->esgs_ring_bo);
		queue->esgs_ring_bo = esgs_ring_bo;
		queue->esgs_ring_size = esgs_ring_size;
	}

	if (gsvs_ring_bo != queue->gsvs_ring_bo) {
		if (queue->gsvs_ring_bo)
			queue->device->ws->buffer_destroy(queue->gsvs_ring_bo);
		queue->gsvs_ring_bo = gsvs_ring_bo;
		queue->gsvs_ring_size = gsvs_ring_size;
	}

	if (descriptor_bo != queue->descriptor_bo) {
		if (queue->descriptor_bo)
			queue->device->ws->buffer_destroy(queue->descriptor_bo);

		queue->descriptor_bo = descriptor_bo;
	}

	*preamble_cs = cs;
	return VK_SUCCESS;
fail:
	if (cs)
		queue->device->ws->cs_destroy(cs);
	if (descriptor_bo && descriptor_bo != queue->descriptor_bo)
		queue->device->ws->buffer_destroy(descriptor_bo);
	if (scratch_bo && scratch_bo != queue->scratch_bo)
		queue->device->ws->buffer_destroy(scratch_bo);
	if (compute_scratch_bo && compute_scratch_bo != queue->compute_scratch_bo)
		queue->device->ws->buffer_destroy(compute_scratch_bo);
	if (esgs_ring_bo && esgs_ring_bo != queue->esgs_ring_bo)
		queue->device->ws->buffer_destroy(esgs_ring_bo);
	if (gsvs_ring_bo && gsvs_ring_bo != queue->gsvs_ring_bo)
		queue->device->ws->buffer_destroy(gsvs_ring_bo);
	return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

VkResult radv_QueueSubmit(
	VkQueue                                     _queue,
	uint32_t                                    submitCount,
	const VkSubmitInfo*                         pSubmits,
	VkFence                                     _fence)
{
	RADV_FROM_HANDLE(radv_queue, queue, _queue);
	RADV_FROM_HANDLE(radv_fence, fence, _fence);
	struct radeon_winsys_fence *base_fence = fence ? fence->fence : NULL;
	struct radeon_winsys_ctx *ctx = queue->hw_ctx;
	int ret;
	uint32_t max_cs_submission = queue->device->trace_bo ? 1 : UINT32_MAX;
	uint32_t scratch_size = 0;
	uint32_t compute_scratch_size = 0;
	uint32_t esgs_ring_size = 0, gsvs_ring_size = 0;
	struct radeon_winsys_cs *preamble_cs = NULL;
	VkResult result;

	/* Do this first so failing to allocate scratch buffers can't result in
	 * partially executed submissions. */
	for (uint32_t i = 0; i < submitCount; i++) {
		for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; j++) {
			RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer,
					 pSubmits[i].pCommandBuffers[j]);

			scratch_size = MAX2(scratch_size, cmd_buffer->scratch_size_needed);
			compute_scratch_size = MAX2(compute_scratch_size,
			                            cmd_buffer->compute_scratch_size_needed);
			esgs_ring_size = MAX2(esgs_ring_size, cmd_buffer->esgs_ring_size_needed);
			gsvs_ring_size = MAX2(gsvs_ring_size, cmd_buffer->gsvs_ring_size_needed);
		}
	}

	result = radv_get_preamble_cs(queue, scratch_size, compute_scratch_size, esgs_ring_size, gsvs_ring_size, &preamble_cs);
	if (result != VK_SUCCESS)
		return result;

	for (uint32_t i = 0; i < submitCount; i++) {
		struct radeon_winsys_cs **cs_array;
		bool can_patch = true;
		uint32_t advance;

		if (!pSubmits[i].commandBufferCount)
			continue;

		cs_array = malloc(sizeof(struct radeon_winsys_cs *) *
					        pSubmits[i].commandBufferCount);

		for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; j++) {
			RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer,
					 pSubmits[i].pCommandBuffers[j]);
			assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY);

			cs_array[j] = cmd_buffer->cs;
			if ((cmd_buffer->usage_flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT))
				can_patch = false;
		}

		for (uint32_t j = 0; j < pSubmits[i].commandBufferCount; j += advance) {
			advance = MIN2(max_cs_submission,
				       pSubmits[i].commandBufferCount - j);
			bool b = j == 0;
			bool e = j + advance == pSubmits[i].commandBufferCount;

			if (queue->device->trace_bo)
				*queue->device->trace_id_ptr = 0;

			ret = queue->device->ws->cs_submit(ctx, queue->queue_idx, cs_array + j,
							advance, preamble_cs,
							(struct radeon_winsys_sem **)pSubmits[i].pWaitSemaphores,
							b ? pSubmits[i].waitSemaphoreCount : 0,
							(struct radeon_winsys_sem **)pSubmits[i].pSignalSemaphores,
							e ? pSubmits[i].signalSemaphoreCount : 0,
							can_patch, base_fence);

			if (ret) {
				radv_loge("failed to submit CS %d\n", i);
				abort();
			}
			if (queue->device->trace_bo) {
				bool success = queue->device->ws->ctx_wait_idle(
							queue->hw_ctx,
							radv_queue_family_to_ring(
								queue->queue_family_index),
							queue->queue_idx);

				if (!success) { /* Hang */
					radv_dump_trace(queue->device, cs_array[j]);
					abort();
				}
			}
		}
		free(cs_array);
	}

	if (fence) {
		if (!submitCount)
			ret = queue->device->ws->cs_submit(ctx, queue->queue_idx,
							   &queue->device->empty_cs[queue->queue_family_index],
							   1, NULL, NULL, 0, NULL, 0,
							   false, base_fence);

		fence->submitted = true;
	}

	return VK_SUCCESS;
}

VkResult radv_QueueWaitIdle(
	VkQueue                                     _queue)
{
	RADV_FROM_HANDLE(radv_queue, queue, _queue);

	queue->device->ws->ctx_wait_idle(queue->hw_ctx,
	                                 radv_queue_family_to_ring(queue->queue_family_index),
	                                 queue->queue_idx);
	return VK_SUCCESS;
}

VkResult radv_DeviceWaitIdle(
	VkDevice                                    _device)
{
	RADV_FROM_HANDLE(radv_device, device, _device);

	for (unsigned i = 0; i < RADV_MAX_QUEUE_FAMILIES; i++) {
		for (unsigned q = 0; q < device->queue_count[i]; q++) {
			radv_QueueWaitIdle(radv_queue_to_handle(&device->queues[i][q]));
		}
	}
	return VK_SUCCESS;
}

PFN_vkVoidFunction radv_GetInstanceProcAddr(
	VkInstance                                  instance,
	const char*                                 pName)
{
	return radv_lookup_entrypoint(pName);
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
	return radv_GetInstanceProcAddr(instance, pName);
}

PFN_vkVoidFunction radv_GetDeviceProcAddr(
	VkDevice                                    device,
	const char*                                 pName)
{
	return radv_lookup_entrypoint(pName);
}

VkResult radv_AllocateMemory(
	VkDevice                                    _device,
	const VkMemoryAllocateInfo*                 pAllocateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkDeviceMemory*                             pMem)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_device_memory *mem;
	VkResult result;
	enum radeon_bo_domain domain;
	uint32_t flags = 0;
	assert(pAllocateInfo->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);

	if (pAllocateInfo->allocationSize == 0) {
		/* Apparently, this is allowed */
		*pMem = VK_NULL_HANDLE;
		return VK_SUCCESS;
	}

	mem = vk_alloc2(&device->alloc, pAllocator, sizeof(*mem), 8,
			  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (mem == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	uint64_t alloc_size = align_u64(pAllocateInfo->allocationSize, 4096);
	if (pAllocateInfo->memoryTypeIndex == RADV_MEM_TYPE_GTT_WRITE_COMBINE ||
	    pAllocateInfo->memoryTypeIndex == RADV_MEM_TYPE_GTT_CACHED)
		domain = RADEON_DOMAIN_GTT;
	else
		domain = RADEON_DOMAIN_VRAM;

	if (pAllocateInfo->memoryTypeIndex == RADV_MEM_TYPE_VRAM)
		flags |= RADEON_FLAG_NO_CPU_ACCESS;
	else
		flags |= RADEON_FLAG_CPU_ACCESS;

	if (pAllocateInfo->memoryTypeIndex == RADV_MEM_TYPE_GTT_WRITE_COMBINE)
		flags |= RADEON_FLAG_GTT_WC;

	mem->bo = device->ws->buffer_create(device->ws, alloc_size, 32768,
					       domain, flags);

	if (!mem->bo) {
		result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
		goto fail;
	}
	mem->type_index = pAllocateInfo->memoryTypeIndex;

	*pMem = radv_device_memory_to_handle(mem);

	return VK_SUCCESS;

fail:
	vk_free2(&device->alloc, pAllocator, mem);

	return result;
}

void radv_FreeMemory(
	VkDevice                                    _device,
	VkDeviceMemory                              _mem,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_device_memory, mem, _mem);

	if (mem == NULL)
		return;

	device->ws->buffer_destroy(mem->bo);
	mem->bo = NULL;

	vk_free2(&device->alloc, pAllocator, mem);
}

VkResult radv_MapMemory(
	VkDevice                                    _device,
	VkDeviceMemory                              _memory,
	VkDeviceSize                                offset,
	VkDeviceSize                                size,
	VkMemoryMapFlags                            flags,
	void**                                      ppData)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_device_memory, mem, _memory);

	if (mem == NULL) {
		*ppData = NULL;
		return VK_SUCCESS;
	}

	*ppData = device->ws->buffer_map(mem->bo);
	if (*ppData) {
		*ppData += offset;
		return VK_SUCCESS;
	}

	return VK_ERROR_MEMORY_MAP_FAILED;
}

void radv_UnmapMemory(
	VkDevice                                    _device,
	VkDeviceMemory                              _memory)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_device_memory, mem, _memory);

	if (mem == NULL)
		return;

	device->ws->buffer_unmap(mem->bo);
}

VkResult radv_FlushMappedMemoryRanges(
	VkDevice                                    _device,
	uint32_t                                    memoryRangeCount,
	const VkMappedMemoryRange*                  pMemoryRanges)
{
	return VK_SUCCESS;
}

VkResult radv_InvalidateMappedMemoryRanges(
	VkDevice                                    _device,
	uint32_t                                    memoryRangeCount,
	const VkMappedMemoryRange*                  pMemoryRanges)
{
	return VK_SUCCESS;
}

void radv_GetBufferMemoryRequirements(
	VkDevice                                    device,
	VkBuffer                                    _buffer,
	VkMemoryRequirements*                       pMemoryRequirements)
{
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);

	pMemoryRequirements->memoryTypeBits = (1u << RADV_MEM_TYPE_COUNT) - 1;

	pMemoryRequirements->size = buffer->size;
	pMemoryRequirements->alignment = 16;
}

void radv_GetImageMemoryRequirements(
	VkDevice                                    device,
	VkImage                                     _image,
	VkMemoryRequirements*                       pMemoryRequirements)
{
	RADV_FROM_HANDLE(radv_image, image, _image);

	pMemoryRequirements->memoryTypeBits = (1u << RADV_MEM_TYPE_COUNT) - 1;

	pMemoryRequirements->size = image->size;
	pMemoryRequirements->alignment = image->alignment;
}

void radv_GetImageSparseMemoryRequirements(
	VkDevice                                    device,
	VkImage                                     image,
	uint32_t*                                   pSparseMemoryRequirementCount,
	VkSparseImageMemoryRequirements*            pSparseMemoryRequirements)
{
	stub();
}

void radv_GetDeviceMemoryCommitment(
	VkDevice                                    device,
	VkDeviceMemory                              memory,
	VkDeviceSize*                               pCommittedMemoryInBytes)
{
	*pCommittedMemoryInBytes = 0;
}

VkResult radv_BindBufferMemory(
	VkDevice                                    device,
	VkBuffer                                    _buffer,
	VkDeviceMemory                              _memory,
	VkDeviceSize                                memoryOffset)
{
	RADV_FROM_HANDLE(radv_device_memory, mem, _memory);
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);

	if (mem) {
		buffer->bo = mem->bo;
		buffer->offset = memoryOffset;
	} else {
		buffer->bo = NULL;
		buffer->offset = 0;
	}

	return VK_SUCCESS;
}

VkResult radv_BindImageMemory(
	VkDevice                                    device,
	VkImage                                     _image,
	VkDeviceMemory                              _memory,
	VkDeviceSize                                memoryOffset)
{
	RADV_FROM_HANDLE(radv_device_memory, mem, _memory);
	RADV_FROM_HANDLE(radv_image, image, _image);

	if (mem) {
		image->bo = mem->bo;
		image->offset = memoryOffset;
	} else {
		image->bo = NULL;
		image->offset = 0;
	}

	return VK_SUCCESS;
}

VkResult radv_QueueBindSparse(
	VkQueue                                     queue,
	uint32_t                                    bindInfoCount,
	const VkBindSparseInfo*                     pBindInfo,
	VkFence                                     fence)
{
	stub_return(VK_ERROR_INCOMPATIBLE_DRIVER);
}

VkResult radv_CreateFence(
	VkDevice                                    _device,
	const VkFenceCreateInfo*                    pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkFence*                                    pFence)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_fence *fence = vk_alloc2(&device->alloc, pAllocator,
					       sizeof(*fence), 8,
					       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

	if (!fence)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	memset(fence, 0, sizeof(*fence));
	fence->submitted = false;
	fence->signalled = !!(pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT);
	fence->fence = device->ws->create_fence();
	if (!fence->fence) {
		vk_free2(&device->alloc, pAllocator, fence);
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	*pFence = radv_fence_to_handle(fence);

	return VK_SUCCESS;
}

void radv_DestroyFence(
	VkDevice                                    _device,
	VkFence                                     _fence,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_fence, fence, _fence);

	if (!fence)
		return;
	device->ws->destroy_fence(fence->fence);
	vk_free2(&device->alloc, pAllocator, fence);
}

static uint64_t radv_get_absolute_timeout(uint64_t timeout)
{
	uint64_t current_time;
	struct timespec tv;

	clock_gettime(CLOCK_MONOTONIC, &tv);
	current_time = tv.tv_nsec + tv.tv_sec*1000000000ull;

	timeout = MIN2(UINT64_MAX - current_time, timeout);

	return current_time + timeout;
}

VkResult radv_WaitForFences(
	VkDevice                                    _device,
	uint32_t                                    fenceCount,
	const VkFence*                              pFences,
	VkBool32                                    waitAll,
	uint64_t                                    timeout)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	timeout = radv_get_absolute_timeout(timeout);

	if (!waitAll && fenceCount > 1) {
		fprintf(stderr, "radv: WaitForFences without waitAll not implemented yet\n");
	}

	for (uint32_t i = 0; i < fenceCount; ++i) {
		RADV_FROM_HANDLE(radv_fence, fence, pFences[i]);
		bool expired = false;

		if (fence->signalled)
			continue;

		if (!fence->submitted)
			return VK_TIMEOUT;

		expired = device->ws->fence_wait(device->ws, fence->fence, true, timeout);
		if (!expired)
			return VK_TIMEOUT;

		fence->signalled = true;
	}

	return VK_SUCCESS;
}

VkResult radv_ResetFences(VkDevice device,
			  uint32_t fenceCount,
			  const VkFence *pFences)
{
	for (unsigned i = 0; i < fenceCount; ++i) {
		RADV_FROM_HANDLE(radv_fence, fence, pFences[i]);
		fence->submitted = fence->signalled = false;
	}

	return VK_SUCCESS;
}

VkResult radv_GetFenceStatus(VkDevice _device, VkFence _fence)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_fence, fence, _fence);

	if (fence->signalled)
		return VK_SUCCESS;
	if (!fence->submitted)
		return VK_NOT_READY;

	if (!device->ws->fence_wait(device->ws, fence->fence, false, 0))
		return VK_NOT_READY;

	return VK_SUCCESS;
}


// Queue semaphore functions

VkResult radv_CreateSemaphore(
	VkDevice                                    _device,
	const VkSemaphoreCreateInfo*                pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkSemaphore*                                pSemaphore)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radeon_winsys_sem *sem;

	sem = device->ws->create_sem(device->ws);
	if (!sem)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	*pSemaphore = (VkSemaphore)sem;
	return VK_SUCCESS;
}

void radv_DestroySemaphore(
	VkDevice                                    _device,
	VkSemaphore                                 _semaphore,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radeon_winsys_sem *sem;
	if (!_semaphore)
		return;

	sem = (struct radeon_winsys_sem *)_semaphore;
	device->ws->destroy_sem(sem);
}

VkResult radv_CreateEvent(
	VkDevice                                    _device,
	const VkEventCreateInfo*                    pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkEvent*                                    pEvent)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_event *event = vk_alloc2(&device->alloc, pAllocator,
					       sizeof(*event), 8,
					       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

	if (!event)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	event->bo = device->ws->buffer_create(device->ws, 8, 8,
					      RADEON_DOMAIN_GTT,
					      RADEON_FLAG_CPU_ACCESS);
	if (!event->bo) {
		vk_free2(&device->alloc, pAllocator, event);
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

	event->map = (uint64_t*)device->ws->buffer_map(event->bo);

	*pEvent = radv_event_to_handle(event);

	return VK_SUCCESS;
}

void radv_DestroyEvent(
	VkDevice                                    _device,
	VkEvent                                     _event,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_event, event, _event);

	if (!event)
		return;
	device->ws->buffer_destroy(event->bo);
	vk_free2(&device->alloc, pAllocator, event);
}

VkResult radv_GetEventStatus(
	VkDevice                                    _device,
	VkEvent                                     _event)
{
	RADV_FROM_HANDLE(radv_event, event, _event);

	if (*event->map == 1)
		return VK_EVENT_SET;
	return VK_EVENT_RESET;
}

VkResult radv_SetEvent(
	VkDevice                                    _device,
	VkEvent                                     _event)
{
	RADV_FROM_HANDLE(radv_event, event, _event);
	*event->map = 1;

	return VK_SUCCESS;
}

VkResult radv_ResetEvent(
    VkDevice                                    _device,
    VkEvent                                     _event)
{
	RADV_FROM_HANDLE(radv_event, event, _event);
	*event->map = 0;

	return VK_SUCCESS;
}

VkResult radv_CreateBuffer(
	VkDevice                                    _device,
	const VkBufferCreateInfo*                   pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkBuffer*                                   pBuffer)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_buffer *buffer;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);

	buffer = vk_alloc2(&device->alloc, pAllocator, sizeof(*buffer), 8,
			     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (buffer == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	buffer->size = pCreateInfo->size;
	buffer->usage = pCreateInfo->usage;
	buffer->bo = NULL;
	buffer->offset = 0;

	*pBuffer = radv_buffer_to_handle(buffer);

	return VK_SUCCESS;
}

void radv_DestroyBuffer(
	VkDevice                                    _device,
	VkBuffer                                    _buffer,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_buffer, buffer, _buffer);

	if (!buffer)
		return;

	vk_free2(&device->alloc, pAllocator, buffer);
}

static inline unsigned
si_tile_mode_index(const struct radv_image *image, unsigned level, bool stencil)
{
	if (stencil)
		return image->surface.stencil_tiling_index[level];
	else
		return image->surface.tiling_index[level];
}

static void
radv_initialise_color_surface(struct radv_device *device,
			      struct radv_color_buffer_info *cb,
			      struct radv_image_view *iview)
{
	const struct vk_format_description *desc;
	unsigned ntype, format, swap, endian;
	unsigned blend_clamp = 0, blend_bypass = 0;
	unsigned pitch_tile_max, slice_tile_max, tile_mode_index;
	uint64_t va;
	const struct radeon_surf *surf = &iview->image->surface;
	const struct radeon_surf_level *level_info = &surf->level[iview->base_mip];

	desc = vk_format_description(iview->vk_format);

	memset(cb, 0, sizeof(*cb));

	va = device->ws->buffer_get_va(iview->bo) + iview->image->offset;
	va += level_info->offset;
	cb->cb_color_base = va >> 8;

	/* CMASK variables */
	va = device->ws->buffer_get_va(iview->bo) + iview->image->offset;
	va += iview->image->cmask.offset;
	cb->cb_color_cmask = va >> 8;
	cb->cb_color_cmask_slice = iview->image->cmask.slice_tile_max;

	va = device->ws->buffer_get_va(iview->bo) + iview->image->offset;
	va += iview->image->dcc_offset;
	cb->cb_dcc_base = va >> 8;

	uint32_t max_slice = iview->type == VK_IMAGE_VIEW_TYPE_3D ? iview->extent.depth : iview->layer_count;
	cb->cb_color_view = S_028C6C_SLICE_START(iview->base_layer) |
		S_028C6C_SLICE_MAX(iview->base_layer + max_slice - 1);

	cb->micro_tile_mode = iview->image->surface.micro_tile_mode;
	pitch_tile_max = level_info->nblk_x / 8 - 1;
	slice_tile_max = (level_info->nblk_x * level_info->nblk_y) / 64 - 1;
	tile_mode_index = si_tile_mode_index(iview->image, iview->base_mip, false);

	cb->cb_color_pitch = S_028C64_TILE_MAX(pitch_tile_max);
	cb->cb_color_slice = S_028C68_TILE_MAX(slice_tile_max);

	/* Intensity is implemented as Red, so treat it that way. */
	cb->cb_color_attrib = S_028C74_FORCE_DST_ALPHA_1(desc->swizzle[3] == VK_SWIZZLE_1) |
		S_028C74_TILE_MODE_INDEX(tile_mode_index);

	if (iview->image->samples > 1) {
		unsigned log_samples = util_logbase2(iview->image->samples);

		cb->cb_color_attrib |= S_028C74_NUM_SAMPLES(log_samples) |
			S_028C74_NUM_FRAGMENTS(log_samples);
	}

	if (iview->image->fmask.size) {
		va = device->ws->buffer_get_va(iview->bo) + iview->image->offset + iview->image->fmask.offset;
		if (device->physical_device->rad_info.chip_class >= CIK)
			cb->cb_color_pitch |= S_028C64_FMASK_TILE_MAX(iview->image->fmask.pitch_in_pixels / 8 - 1);
		cb->cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(iview->image->fmask.tile_mode_index);
		cb->cb_color_fmask = va >> 8;
		cb->cb_color_fmask_slice = S_028C88_TILE_MAX(iview->image->fmask.slice_tile_max);
	} else {
		/* This must be set for fast clear to work without FMASK. */
		if (device->physical_device->rad_info.chip_class >= CIK)
			cb->cb_color_pitch |= S_028C64_FMASK_TILE_MAX(pitch_tile_max);
		cb->cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(tile_mode_index);
		cb->cb_color_fmask = cb->cb_color_base;
		cb->cb_color_fmask_slice = S_028C88_TILE_MAX(slice_tile_max);
	}

	ntype = radv_translate_color_numformat(iview->vk_format,
					       desc,
					       vk_format_get_first_non_void_channel(iview->vk_format));
	format = radv_translate_colorformat(iview->vk_format);
	if (format == V_028C70_COLOR_INVALID || ntype == ~0u)
		radv_finishme("Illegal color\n");
	swap = radv_translate_colorswap(iview->vk_format, FALSE);
	endian = radv_colorformat_endian_swap(format);

	/* blend clamp should be set for all NORM/SRGB types */
	if (ntype == V_028C70_NUMBER_UNORM ||
	    ntype == V_028C70_NUMBER_SNORM ||
	    ntype == V_028C70_NUMBER_SRGB)
		blend_clamp = 1;

	/* set blend bypass according to docs if SINT/UINT or
	   8/24 COLOR variants */
	if (ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT ||
	    format == V_028C70_COLOR_8_24 || format == V_028C70_COLOR_24_8 ||
	    format == V_028C70_COLOR_X24_8_32_FLOAT) {
		blend_clamp = 0;
		blend_bypass = 1;
	}
#if 0
	if ((ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT) &&
	    (format == V_028C70_COLOR_8 ||
	     format == V_028C70_COLOR_8_8 ||
	     format == V_028C70_COLOR_8_8_8_8))
		->color_is_int8 = true;
#endif
	cb->cb_color_info = S_028C70_FORMAT(format) |
		S_028C70_COMP_SWAP(swap) |
		S_028C70_BLEND_CLAMP(blend_clamp) |
		S_028C70_BLEND_BYPASS(blend_bypass) |
		S_028C70_SIMPLE_FLOAT(1) |
		S_028C70_ROUND_MODE(ntype != V_028C70_NUMBER_UNORM &&
				    ntype != V_028C70_NUMBER_SNORM &&
				    ntype != V_028C70_NUMBER_SRGB &&
				    format != V_028C70_COLOR_8_24 &&
				    format != V_028C70_COLOR_24_8) |
		S_028C70_NUMBER_TYPE(ntype) |
		S_028C70_ENDIAN(endian);
	if (iview->image->samples > 1)
		if (iview->image->fmask.size)
			cb->cb_color_info |= S_028C70_COMPRESSION(1);

	if (iview->image->cmask.size &&
	    (device->debug_flags & RADV_DEBUG_FAST_CLEARS))
		cb->cb_color_info |= S_028C70_FAST_CLEAR(1);

	if (iview->image->surface.dcc_size && level_info->dcc_enabled)
		cb->cb_color_info |= S_028C70_DCC_ENABLE(1);

	if (device->physical_device->rad_info.chip_class >= VI) {
		unsigned max_uncompressed_block_size = 2;
		if (iview->image->samples > 1) {
			if (iview->image->surface.bpe == 1)
				max_uncompressed_block_size = 0;
			else if (iview->image->surface.bpe == 2)
				max_uncompressed_block_size = 1;
		}

		cb->cb_dcc_control = S_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(max_uncompressed_block_size) |
			S_028C78_INDEPENDENT_64B_BLOCKS(1);
	}

	/* This must be set for fast clear to work without FMASK. */
	if (!iview->image->fmask.size &&
	    device->physical_device->rad_info.chip_class == SI) {
		unsigned bankh = util_logbase2(iview->image->surface.bankh);
		cb->cb_color_attrib |= S_028C74_FMASK_BANK_HEIGHT(bankh);
	}
}

static void
radv_initialise_ds_surface(struct radv_device *device,
			   struct radv_ds_buffer_info *ds,
			   struct radv_image_view *iview)
{
	unsigned level = iview->base_mip;
	unsigned format;
	uint64_t va, s_offs, z_offs;
	const struct radeon_surf_level *level_info = &iview->image->surface.level[level];
	memset(ds, 0, sizeof(*ds));
	switch (iview->vk_format) {
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
		ds->pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-24);
		ds->offset_scale = 2.0f;
		break;
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D16_UNORM_S8_UINT:
		ds->pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-16);
		ds->offset_scale = 4.0f;
		break;
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		ds->pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-23) |
			S_028B78_POLY_OFFSET_DB_IS_FLOAT_FMT(1);
		ds->offset_scale = 1.0f;
		break;
	default:
		break;
	}

	format = radv_translate_dbformat(iview->vk_format);
	if (format == V_028040_Z_INVALID) {
		fprintf(stderr, "Invalid DB format: %d, disabling DB.\n", iview->vk_format);
	}

	va = device->ws->buffer_get_va(iview->bo) + iview->image->offset;
	s_offs = z_offs = va;
	z_offs += iview->image->surface.level[level].offset;
	s_offs += iview->image->surface.stencil_level[level].offset;

	uint32_t max_slice = iview->type == VK_IMAGE_VIEW_TYPE_3D ? iview->extent.depth : iview->layer_count;
	ds->db_depth_view = S_028008_SLICE_START(iview->base_layer) |
		S_028008_SLICE_MAX(iview->base_layer + max_slice - 1);
	ds->db_depth_info = S_02803C_ADDR5_SWIZZLE_MASK(1);
	ds->db_z_info = S_028040_FORMAT(format) | S_028040_ZRANGE_PRECISION(1);

	if (iview->image->samples > 1)
		ds->db_z_info |= S_028040_NUM_SAMPLES(util_logbase2(iview->image->samples));

	if (iview->image->surface.flags & RADEON_SURF_SBUFFER)
		ds->db_stencil_info = S_028044_FORMAT(V_028044_STENCIL_8);
	else
		ds->db_stencil_info = S_028044_FORMAT(V_028044_STENCIL_INVALID);

	if (device->physical_device->rad_info.chip_class >= CIK) {
		struct radeon_info *info = &device->physical_device->rad_info;
		unsigned tiling_index = iview->image->surface.tiling_index[level];
		unsigned stencil_index = iview->image->surface.stencil_tiling_index[level];
		unsigned macro_index = iview->image->surface.macro_tile_index;
		unsigned tile_mode = info->si_tile_mode_array[tiling_index];
		unsigned stencil_tile_mode = info->si_tile_mode_array[stencil_index];
		unsigned macro_mode = info->cik_macrotile_mode_array[macro_index];

		ds->db_depth_info |=
			S_02803C_ARRAY_MODE(G_009910_ARRAY_MODE(tile_mode)) |
			S_02803C_PIPE_CONFIG(G_009910_PIPE_CONFIG(tile_mode)) |
			S_02803C_BANK_WIDTH(G_009990_BANK_WIDTH(macro_mode)) |
			S_02803C_BANK_HEIGHT(G_009990_BANK_HEIGHT(macro_mode)) |
			S_02803C_MACRO_TILE_ASPECT(G_009990_MACRO_TILE_ASPECT(macro_mode)) |
			S_02803C_NUM_BANKS(G_009990_NUM_BANKS(macro_mode));
		ds->db_z_info |= S_028040_TILE_SPLIT(G_009910_TILE_SPLIT(tile_mode));
		ds->db_stencil_info |= S_028044_TILE_SPLIT(G_009910_TILE_SPLIT(stencil_tile_mode));
	} else {
		unsigned tile_mode_index = si_tile_mode_index(iview->image, level, false);
		ds->db_z_info |= S_028040_TILE_MODE_INDEX(tile_mode_index);
		tile_mode_index = si_tile_mode_index(iview->image, level, true);
		ds->db_stencil_info |= S_028044_TILE_MODE_INDEX(tile_mode_index);
	}

	if (iview->image->htile.size && !level) {
		ds->db_z_info |= S_028040_TILE_SURFACE_ENABLE(1) |
			S_028040_ALLOW_EXPCLEAR(1);

		if (iview->image->surface.flags & RADEON_SURF_SBUFFER) {
			/* Workaround: For a not yet understood reason, the
			 * combination of MSAA, fast stencil clear and stencil
			 * decompress messes with subsequent stencil buffer
			 * uses. Problem was reproduced on Verde, Bonaire,
			 * Tonga, and Carrizo.
			 *
			 * Disabling EXPCLEAR works around the problem.
			 *
			 * Check piglit's arb_texture_multisample-stencil-clear
			 * test if you want to try changing this.
			 */
			if (iview->image->samples <= 1)
				ds->db_stencil_info |= S_028044_ALLOW_EXPCLEAR(1);
		} else
			/* Use all of the htile_buffer for depth if there's no stencil. */
			ds->db_stencil_info |= S_028044_TILE_STENCIL_DISABLE(1);

		va = device->ws->buffer_get_va(iview->bo) + iview->image->offset +
		     iview->image->htile.offset;
		ds->db_htile_data_base = va >> 8;
		ds->db_htile_surface = S_028ABC_FULL_CACHE(1);
	} else {
		ds->db_htile_data_base = 0;
		ds->db_htile_surface = 0;
	}

	ds->db_z_read_base = ds->db_z_write_base = z_offs >> 8;
	ds->db_stencil_read_base = ds->db_stencil_write_base = s_offs >> 8;

	ds->db_depth_size = S_028058_PITCH_TILE_MAX((level_info->nblk_x / 8) - 1) |
		S_028058_HEIGHT_TILE_MAX((level_info->nblk_y / 8) - 1);
	ds->db_depth_slice = S_02805C_SLICE_TILE_MAX((level_info->nblk_x * level_info->nblk_y) / 64 - 1);
}

VkResult radv_CreateFramebuffer(
	VkDevice                                    _device,
	const VkFramebufferCreateInfo*              pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkFramebuffer*                              pFramebuffer)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_framebuffer *framebuffer;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

	size_t size = sizeof(*framebuffer) +
		sizeof(struct radv_attachment_info) * pCreateInfo->attachmentCount;
	framebuffer = vk_alloc2(&device->alloc, pAllocator, size, 8,
				  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (framebuffer == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	framebuffer->attachment_count = pCreateInfo->attachmentCount;
	for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
		VkImageView _iview = pCreateInfo->pAttachments[i];
		struct radv_image_view *iview = radv_image_view_from_handle(_iview);
		framebuffer->attachments[i].attachment = iview;
		if (iview->aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT) {
			radv_initialise_color_surface(device, &framebuffer->attachments[i].cb, iview);
		} else if (iview->aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
			radv_initialise_ds_surface(device, &framebuffer->attachments[i].ds, iview);
		}
	}

	framebuffer->width = pCreateInfo->width;
	framebuffer->height = pCreateInfo->height;
	framebuffer->layers = pCreateInfo->layers;

	*pFramebuffer = radv_framebuffer_to_handle(framebuffer);
	return VK_SUCCESS;
}

void radv_DestroyFramebuffer(
	VkDevice                                    _device,
	VkFramebuffer                               _fb,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_framebuffer, fb, _fb);

	if (!fb)
		return;
	vk_free2(&device->alloc, pAllocator, fb);
}

static unsigned radv_tex_wrap(VkSamplerAddressMode address_mode)
{
	switch (address_mode) {
	case VK_SAMPLER_ADDRESS_MODE_REPEAT:
		return V_008F30_SQ_TEX_WRAP;
	case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
		return V_008F30_SQ_TEX_MIRROR;
	case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
		return V_008F30_SQ_TEX_CLAMP_LAST_TEXEL;
	case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
		return V_008F30_SQ_TEX_CLAMP_BORDER;
	case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
		return V_008F30_SQ_TEX_MIRROR_ONCE_LAST_TEXEL;
	default:
		unreachable("illegal tex wrap mode");
		break;
	}
}

static unsigned
radv_tex_compare(VkCompareOp op)
{
	switch (op) {
	case VK_COMPARE_OP_NEVER:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_NEVER;
	case VK_COMPARE_OP_LESS:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_LESS;
	case VK_COMPARE_OP_EQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_EQUAL;
	case VK_COMPARE_OP_LESS_OR_EQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_LESSEQUAL;
	case VK_COMPARE_OP_GREATER:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATER;
	case VK_COMPARE_OP_NOT_EQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_NOTEQUAL;
	case VK_COMPARE_OP_GREATER_OR_EQUAL:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATEREQUAL;
	case VK_COMPARE_OP_ALWAYS:
		return V_008F30_SQ_TEX_DEPTH_COMPARE_ALWAYS;
	default:
		unreachable("illegal compare mode");
		break;
	}
}

static unsigned
radv_tex_filter(VkFilter filter, unsigned max_ansio)
{
	switch (filter) {
	case VK_FILTER_NEAREST:
		return (max_ansio > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_POINT :
			V_008F38_SQ_TEX_XY_FILTER_POINT);
	case VK_FILTER_LINEAR:
		return (max_ansio > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_BILINEAR :
			V_008F38_SQ_TEX_XY_FILTER_BILINEAR);
	case VK_FILTER_CUBIC_IMG:
	default:
		fprintf(stderr, "illegal texture filter");
		return 0;
	}
}

static unsigned
radv_tex_mipfilter(VkSamplerMipmapMode mode)
{
	switch (mode) {
	case VK_SAMPLER_MIPMAP_MODE_NEAREST:
		return V_008F38_SQ_TEX_Z_FILTER_POINT;
	case VK_SAMPLER_MIPMAP_MODE_LINEAR:
		return V_008F38_SQ_TEX_Z_FILTER_LINEAR;
	default:
		return V_008F38_SQ_TEX_Z_FILTER_NONE;
	}
}

static unsigned
radv_tex_bordercolor(VkBorderColor bcolor)
{
	switch (bcolor) {
	case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
	case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
		return V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK;
	case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
	case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
		return V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_BLACK;
	case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
	case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
		return V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_WHITE;
	default:
		break;
	}
	return 0;
}

static unsigned
radv_tex_aniso_filter(unsigned filter)
{
	if (filter < 2)
		return 0;
	if (filter < 4)
		return 1;
	if (filter < 8)
		return 2;
	if (filter < 16)
		return 3;
	return 4;
}

static void
radv_init_sampler(struct radv_device *device,
		  struct radv_sampler *sampler,
		  const VkSamplerCreateInfo *pCreateInfo)
{
	uint32_t max_aniso = pCreateInfo->anisotropyEnable && pCreateInfo->maxAnisotropy > 1.0 ?
					(uint32_t) pCreateInfo->maxAnisotropy : 0;
	uint32_t max_aniso_ratio = radv_tex_aniso_filter(max_aniso);
	bool is_vi = (device->physical_device->rad_info.chip_class >= VI);

	sampler->state[0] = (S_008F30_CLAMP_X(radv_tex_wrap(pCreateInfo->addressModeU)) |
			     S_008F30_CLAMP_Y(radv_tex_wrap(pCreateInfo->addressModeV)) |
			     S_008F30_CLAMP_Z(radv_tex_wrap(pCreateInfo->addressModeW)) |
			     S_008F30_MAX_ANISO_RATIO(max_aniso_ratio) |
			     S_008F30_DEPTH_COMPARE_FUNC(radv_tex_compare(pCreateInfo->compareOp)) |
			     S_008F30_FORCE_UNNORMALIZED(pCreateInfo->unnormalizedCoordinates ? 1 : 0) |
			     S_008F30_ANISO_THRESHOLD(max_aniso_ratio >> 1) |
			     S_008F30_ANISO_BIAS(max_aniso_ratio) |
			     S_008F30_DISABLE_CUBE_WRAP(0) |
			     S_008F30_COMPAT_MODE(is_vi));
	sampler->state[1] = (S_008F34_MIN_LOD(S_FIXED(CLAMP(pCreateInfo->minLod, 0, 15), 8)) |
			     S_008F34_MAX_LOD(S_FIXED(CLAMP(pCreateInfo->maxLod, 0, 15), 8)) |
			     S_008F34_PERF_MIP(max_aniso_ratio ? max_aniso_ratio + 6 : 0));
	sampler->state[2] = (S_008F38_LOD_BIAS(S_FIXED(CLAMP(pCreateInfo->mipLodBias, -16, 16), 8)) |
			     S_008F38_XY_MAG_FILTER(radv_tex_filter(pCreateInfo->magFilter, max_aniso)) |
			     S_008F38_XY_MIN_FILTER(radv_tex_filter(pCreateInfo->minFilter, max_aniso)) |
			     S_008F38_MIP_FILTER(radv_tex_mipfilter(pCreateInfo->mipmapMode)) |
			     S_008F38_MIP_POINT_PRECLAMP(1) |
			     S_008F38_DISABLE_LSB_CEIL(1) |
			     S_008F38_FILTER_PREC_FIX(1) |
			     S_008F38_ANISO_OVERRIDE(is_vi));
	sampler->state[3] = (S_008F3C_BORDER_COLOR_PTR(0) |
			     S_008F3C_BORDER_COLOR_TYPE(radv_tex_bordercolor(pCreateInfo->borderColor)));
}

VkResult radv_CreateSampler(
	VkDevice                                    _device,
	const VkSamplerCreateInfo*                  pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkSampler*                                  pSampler)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_sampler *sampler;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

	sampler = vk_alloc2(&device->alloc, pAllocator, sizeof(*sampler), 8,
			      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!sampler)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	radv_init_sampler(device, sampler, pCreateInfo);
	*pSampler = radv_sampler_to_handle(sampler);

	return VK_SUCCESS;
}

void radv_DestroySampler(
	VkDevice                                    _device,
	VkSampler                                   _sampler,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_sampler, sampler, _sampler);

	if (!sampler)
		return;
	vk_free2(&device->alloc, pAllocator, sampler);
}


/* vk_icd.h does not declare this function, so we declare it here to
 * suppress Wmissing-prototypes.
 */
PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *pSupportedVersion);

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
	*/
	*pSupportedVersion = MIN2(*pSupportedVersion, 3u);
	return VK_SUCCESS;
}
