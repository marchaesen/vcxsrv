/*
 * Copyright © 2017, Google Inc.
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

#ifdef ANDROID
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <hardware/hwvulkan.h>
#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vk_icd.h>
#include <libsync.h>

#if ANDROID_API_LEVEL >= 26
#include <hardware/gralloc1.h>
#include <vndk/hardware_buffer.h>
#endif
#endif

#include "util/os_file.h"

#include "radv_private.h"
#include "vk_util.h"

#ifdef ANDROID

static int radv_hal_open(const struct hw_module_t* mod, const char* id, struct hw_device_t** dev);
static int radv_hal_close(struct hw_device_t *dev);

static void UNUSED
static_asserts(void)
{
	STATIC_ASSERT(HWVULKAN_DISPATCH_MAGIC == ICD_LOADER_MAGIC);
}

PUBLIC struct hwvulkan_module_t HAL_MODULE_INFO_SYM = {
	.common = {
		.tag = HARDWARE_MODULE_TAG,
		.module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
		.hal_api_version = HARDWARE_MAKE_API_VERSION(1, 0),
		.id = HWVULKAN_HARDWARE_MODULE_ID,
		.name = "AMD Vulkan HAL",
		.author = "Google",
		.methods = &(hw_module_methods_t) {
			.open = radv_hal_open,
		},
	},
};

/* If any bits in test_mask are set, then unset them and return true. */
static inline bool
unmask32(uint32_t *inout_mask, uint32_t test_mask)
{
	uint32_t orig_mask = *inout_mask;
	*inout_mask &= ~test_mask;
	return *inout_mask != orig_mask;
}

static int
radv_hal_open(const struct hw_module_t* mod, const char* id,
             struct hw_device_t** dev)
{
	assert(mod == &HAL_MODULE_INFO_SYM.common);
	assert(strcmp(id, HWVULKAN_DEVICE_0) == 0);

	hwvulkan_device_t *hal_dev = malloc(sizeof(*hal_dev));
	if (!hal_dev)
		return -1;

	*hal_dev = (hwvulkan_device_t) {
		.common = {
			.tag = HARDWARE_DEVICE_TAG,
			.version = HWVULKAN_DEVICE_API_VERSION_0_1,
			.module = &HAL_MODULE_INFO_SYM.common,
			.close = radv_hal_close,
		},
		.EnumerateInstanceExtensionProperties = radv_EnumerateInstanceExtensionProperties,
		.CreateInstance = radv_CreateInstance,
		.GetInstanceProcAddr = radv_GetInstanceProcAddr,
	};

	*dev = &hal_dev->common;
	return 0;
}

static int
radv_hal_close(struct hw_device_t *dev)
{
	/* hwvulkan.h claims that hw_device_t::close() is never called. */
	return -1;
}

VkResult
radv_image_from_gralloc(VkDevice device_h,
                       const VkImageCreateInfo *base_info,
                       const VkNativeBufferANDROID *gralloc_info,
                       const VkAllocationCallbacks *alloc,
                       VkImage *out_image_h)

{
	RADV_FROM_HANDLE(radv_device, device, device_h);
	VkImage image_h = VK_NULL_HANDLE;
	struct radv_image *image = NULL;
	VkResult result;

	if (gralloc_info->handle->numFds != 1) {
		return vk_errorf(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE,
		                 "VkNativeBufferANDROID::handle::numFds is %d, "
		                 "expected 1", gralloc_info->handle->numFds);
	}

	/* Do not close the gralloc handle's dma_buf. The lifetime of the dma_buf
	 * must exceed that of the gralloc handle, and we do not own the gralloc
	 * handle.
	 */
	int dma_buf = gralloc_info->handle->data[0];

	VkDeviceMemory memory_h;

	const VkImportMemoryFdInfoKHR import_info = {
		.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
		.fd = os_dupfd_cloexec(dma_buf),
	};

	/* Find the first VRAM memory type, or GART for PRIME images. */
	int memory_type_index = -1;
	for (int i = 0; i < device->physical_device->memory_properties.memoryTypeCount; ++i) {
		bool is_local = !!(device->physical_device->memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (is_local) {
			memory_type_index = i;
			break;
		}
	}

	/* fallback */
	if (memory_type_index == -1)
		memory_type_index = 0;

	result = radv_AllocateMemory(device_h,
				     &(VkMemoryAllocateInfo) {
					     .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
					     .pNext = &import_info,
					     /* Max buffer size, unused for imports */
					     .allocationSize = 0x7FFFFFFF,
					     .memoryTypeIndex = memory_type_index,
				     },
				     alloc,
				     &memory_h);
	if (result != VK_SUCCESS)
		return result;

	struct radeon_bo_metadata md;
	device->ws->buffer_get_metadata(device->ws, radv_device_memory_from_handle(memory_h)->bo, &md);

	VkImageCreateInfo updated_base_info = *base_info;

	VkExternalMemoryImageCreateInfo external_memory_info = {
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
		.pNext = updated_base_info.pNext,
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};

	updated_base_info.pNext = &external_memory_info;

	result = radv_image_create(device_h,
	                           &(struct radv_image_create_info) {
	                               .vk_info = &updated_base_info,
	                               .no_metadata_planes = true,
	                               .bo_metadata = &md,
	                           },
	                           alloc,
	                           &image_h);

	if (result != VK_SUCCESS)
		goto fail_create_image;

	image = radv_image_from_handle(image_h);

	radv_image_override_offset_stride(device, image, 0, gralloc_info->stride);

	radv_BindImageMemory(device_h, image_h, memory_h, 0);

	image->owned_memory = memory_h;
	/* Don't clobber the out-parameter until success is certain. */
	*out_image_h = image_h;

	return VK_SUCCESS;

fail_create_image:
	radv_FreeMemory(device_h, memory_h, alloc);
	return result;
}

VkResult radv_GetSwapchainGrallocUsageANDROID(
    VkDevice            device_h,
    VkFormat            format,
    VkImageUsageFlags   imageUsage,
    int*                grallocUsage)
{
	RADV_FROM_HANDLE(radv_device, device, device_h);
	struct radv_physical_device *phys_dev = device->physical_device;
	VkPhysicalDevice phys_dev_h = radv_physical_device_to_handle(phys_dev);
	VkResult result;

	*grallocUsage = 0;

	/* WARNING: Android Nougat's libvulkan.so hardcodes the VkImageUsageFlags
	 * returned to applications via VkSurfaceCapabilitiesKHR::supportedUsageFlags.
	 * The relevant code in libvulkan/swapchain.cpp contains this fun comment:
	 *
	 *     TODO(jessehall): I think these are right, but haven't thought hard
	 *     about it. Do we need to query the driver for support of any of
	 *     these?
	 *
	 * Any disagreement between this function and the hardcoded
	 * VkSurfaceCapabilitiesKHR:supportedUsageFlags causes tests
	 * dEQP-VK.wsi.android.swapchain.*.image_usage to fail.
	 */

	const VkPhysicalDeviceImageFormatInfo2 image_format_info = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
		.format = format,
		.type = VK_IMAGE_TYPE_2D,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = imageUsage,
	};

	VkImageFormatProperties2 image_format_props = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
	};

	/* Check that requested format and usage are supported. */
	result = radv_GetPhysicalDeviceImageFormatProperties2(phys_dev_h,
	                                                      &image_format_info, &image_format_props);
	if (result != VK_SUCCESS) {
		return vk_errorf(device->instance, result,
		                 "radv_GetPhysicalDeviceImageFormatProperties2 failed "
		                 "inside %s", __func__);
	}

	if (unmask32(&imageUsage, VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
		*grallocUsage |= GRALLOC_USAGE_HW_RENDER;

	if (unmask32(&imageUsage, VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
	                          VK_IMAGE_USAGE_SAMPLED_BIT |
	                          VK_IMAGE_USAGE_STORAGE_BIT |
	                          VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
		*grallocUsage |= GRALLOC_USAGE_HW_TEXTURE;

	/* All VkImageUsageFlags not explicitly checked here are unsupported for
	 * gralloc swapchains.
	 */
	if (imageUsage != 0) {
	return vk_errorf(device->instance, VK_ERROR_FORMAT_NOT_SUPPORTED,
	                "unsupported VkImageUsageFlags(0x%x) for gralloc "
	                "swapchain", imageUsage);
	}

	/*
	* FINISHME: Advertise all display-supported formats. Mostly
	* DRM_FORMAT_ARGB2101010 and DRM_FORMAT_ABGR2101010, but need to check
	* what we need for 30-bit colors.
	*/
	if (format == VK_FORMAT_B8G8R8A8_UNORM ||
	    format == VK_FORMAT_B5G6R5_UNORM_PACK16) {
		*grallocUsage |= GRALLOC_USAGE_HW_FB |
		                 GRALLOC_USAGE_HW_COMPOSER |
		                 GRALLOC_USAGE_EXTERNAL_DISP;
	}

	if (*grallocUsage == 0)
		return VK_ERROR_FORMAT_NOT_SUPPORTED;

	return VK_SUCCESS;
}

VkResult radv_GetSwapchainGrallocUsage2ANDROID(
    VkDevice            device_h,
    VkFormat            format,
    VkImageUsageFlags   imageUsage,
    VkSwapchainImageUsageFlagsANDROID swapchainImageUsage,
    uint64_t*           grallocConsumerUsage,
    uint64_t*           grallocProducerUsage)
{
	/* Before level 26 (Android 8.0/Oreo) the loader uses
	 * vkGetSwapchainGrallocUsageANDROID. */
#if ANDROID_API_LEVEL >= 26
	RADV_FROM_HANDLE(radv_device, device, device_h);
	struct radv_physical_device *phys_dev = device->physical_device;
	VkPhysicalDevice phys_dev_h = radv_physical_device_to_handle(phys_dev);
	VkResult result;

	*grallocConsumerUsage = 0;
	*grallocProducerUsage = 0;

	if (swapchainImageUsage & VK_SWAPCHAIN_IMAGE_USAGE_SHARED_BIT_ANDROID)
		return vk_errorf(device->instance, VK_ERROR_FORMAT_NOT_SUPPORTED,
		                 "The Vulkan loader tried to query shared presentable image support");

	const VkPhysicalDeviceImageFormatInfo2 image_format_info = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
		.format = format,
		.type = VK_IMAGE_TYPE_2D,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = imageUsage,
	};

	VkImageFormatProperties2 image_format_props = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
	};

	/* Check that requested format and usage are supported. */
	result = radv_GetPhysicalDeviceImageFormatProperties2(phys_dev_h,
	                                                      &image_format_info, &image_format_props);
	if (result != VK_SUCCESS) {
		return vk_errorf(device->instance, result,
		                 "radv_GetPhysicalDeviceImageFormatProperties2 failed "
		                 "inside %s", __func__);
	}

	if (unmask32(&imageUsage, VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
		*grallocProducerUsage |= GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;
		*grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_CLIENT_TARGET;
	}

	if (unmask32(&imageUsage, VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
	                          VK_IMAGE_USAGE_SAMPLED_BIT |
	                          VK_IMAGE_USAGE_STORAGE_BIT |
	                          VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
		*grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
	}

	if (imageUsage != 0) {
		return vk_errorf(device->instance, VK_ERROR_FORMAT_NOT_SUPPORTED,
		                "unsupported VkImageUsageFlags(0x%x) for gralloc "
		                "swapchain", imageUsage);
	}

	/*
	* FINISHME: Advertise all display-supported formats. Mostly
	* DRM_FORMAT_ARGB2101010 and DRM_FORMAT_ABGR2101010, but need to check
	* what we need for 30-bit colors.
	*/
	if (format == VK_FORMAT_B8G8R8A8_UNORM ||
	    format == VK_FORMAT_B5G6R5_UNORM_PACK16) {
		*grallocProducerUsage |= GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;
		*grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_HWCOMPOSER;
	}

	if (!*grallocProducerUsage && !*grallocConsumerUsage)
		return VK_ERROR_FORMAT_NOT_SUPPORTED;

	return VK_SUCCESS;
#else
	*grallocConsumerUsage = 0;
	*grallocProducerUsage = 0;
	return VK_ERROR_FORMAT_NOT_SUPPORTED;
#endif
}

VkResult
radv_AcquireImageANDROID(
      VkDevice            device,
      VkImage             image_h,
      int                 nativeFenceFd,
      VkSemaphore         semaphore,
      VkFence             fence)
{
	VkResult semaphore_result = VK_SUCCESS, fence_result = VK_SUCCESS;

	if (semaphore != VK_NULL_HANDLE) {
		int semaphore_fd = nativeFenceFd >= 0 ? os_dupfd_cloexec(nativeFenceFd) : nativeFenceFd;
		semaphore_result = radv_ImportSemaphoreFdKHR(device,
		                                             &(VkImportSemaphoreFdInfoKHR) {
		                                                 .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
		                                                 .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
		                                                 .fd = semaphore_fd,
		                                                 .semaphore = semaphore,
		                                                 .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
		                                            });
	}

	if (fence != VK_NULL_HANDLE) {
		int fence_fd = nativeFenceFd >= 0 ? os_dupfd_cloexec(nativeFenceFd) : nativeFenceFd;
		fence_result = radv_ImportFenceFdKHR(device,
		                                     &(VkImportFenceFdInfoKHR) {
		                                         .sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR,
		                                         .flags = VK_FENCE_IMPORT_TEMPORARY_BIT,
		                                         .fd = fence_fd,
		                                         .fence = fence,
		                                         .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT,
		                                     });
	}

	close(nativeFenceFd);

	if (semaphore_result != VK_SUCCESS)
		return semaphore_result;
	return fence_result;
}

VkResult
radv_QueueSignalReleaseImageANDROID(
      VkQueue             _queue,
      uint32_t            waitSemaphoreCount,
      const VkSemaphore*  pWaitSemaphores,
      VkImage             image,
      int*                pNativeFenceFd)
{
	RADV_FROM_HANDLE(radv_queue, queue, _queue);
	VkResult result = VK_SUCCESS;

	if (waitSemaphoreCount == 0) {
		if (pNativeFenceFd)
			*pNativeFenceFd = -1;
		return VK_SUCCESS;
	}

	int fd = -1;

	for (uint32_t i = 0; i < waitSemaphoreCount; ++i) {
		int tmp_fd;
		result = radv_GetSemaphoreFdKHR(radv_device_to_handle(queue->device),
		                                &(VkSemaphoreGetFdInfoKHR) {
		                                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
		                                    .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
		                                    .semaphore = pWaitSemaphores[i],
		                            }, &tmp_fd);
		if (result != VK_SUCCESS) {
			if (fd >= 0)
				close (fd);
			return result;
		}

		if (fd < 0)
			fd = tmp_fd;
		else if (tmp_fd >= 0) {
			sync_accumulate("radv", &fd, tmp_fd);
			close(tmp_fd);
		}
	}

	if (pNativeFenceFd) {
		*pNativeFenceFd = fd;
	} else if (fd >= 0) {
		close(fd);
		/* We still need to do the exports, to reset the semaphores, but
		 * otherwise we don't wait on them. */
	}
	return VK_SUCCESS;
}
#endif

#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER

enum {
   /* Usage bit equal to GRALLOC_USAGE_HW_CAMERA_MASK */
   AHARDWAREBUFFER_USAGE_CAMERA_MASK = 0x00060000U,
};

static inline VkFormat
vk_format_from_android(unsigned android_format, unsigned android_usage)
{
	switch (android_format) {
	case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
	case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
		return VK_FORMAT_R8G8B8A8_UNORM;
	case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:
		return VK_FORMAT_R8G8B8_UNORM;
	case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:
		return VK_FORMAT_R5G6B5_UNORM_PACK16;
	case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT:
		return VK_FORMAT_R16G16B16A16_SFLOAT;
	case AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:
		return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	case AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420:
		return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
	case AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED:
		if (android_usage & AHARDWAREBUFFER_USAGE_CAMERA_MASK)
			return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
		else
			return VK_FORMAT_R8G8B8_UNORM;
	case AHARDWAREBUFFER_FORMAT_BLOB:
	default:
		return VK_FORMAT_UNDEFINED;
	}
}

static inline unsigned
android_format_from_vk(unsigned vk_format)
{
   switch (vk_format) {
   case VK_FORMAT_R8G8B8A8_UNORM:
      return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
   case VK_FORMAT_R8G8B8_UNORM:
      return AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM;
   case VK_FORMAT_R5G6B5_UNORM_PACK16:
      return AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
   case VK_FORMAT_R16G16B16A16_SFLOAT:
      return AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT;
   case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
      return AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM;
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      return AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;
   default:
      return AHARDWAREBUFFER_FORMAT_BLOB;
   }
}

uint64_t
radv_ahb_usage_from_vk_usage(const VkImageCreateFlags vk_create,
                             const VkImageUsageFlags vk_usage)
{
   uint64_t ahb_usage = 0;
   if (vk_usage & VK_IMAGE_USAGE_SAMPLED_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   if (vk_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   if (vk_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;

   if (vk_create & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_CUBE_MAP;

   if (vk_create & VK_IMAGE_CREATE_PROTECTED_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT;

   /* No usage bits set - set at least one GPU usage. */
   if (ahb_usage == 0)
      ahb_usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
   return ahb_usage;
}

static VkResult
get_ahb_buffer_format_properties(
   VkDevice device_h,
   const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferFormatPropertiesANDROID *pProperties)
{
	RADV_FROM_HANDLE(radv_device, device, device_h);

	/* Get a description of buffer contents . */
	AHardwareBuffer_Desc desc;
	AHardwareBuffer_describe(buffer, &desc);

	/* Verify description. */
	const uint64_t gpu_usage =
		AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
		AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
		AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;

	/* "Buffer must be a valid Android hardware buffer object with at least
	 * one of the AHARDWAREBUFFER_USAGE_GPU_* usage flags."
	 */
	if (!(desc.usage & (gpu_usage)))
		return VK_ERROR_INVALID_EXTERNAL_HANDLE;

	/* Fill properties fields based on description. */
	VkAndroidHardwareBufferFormatPropertiesANDROID *p = pProperties;

	p->format = vk_format_from_android(desc.format, desc.usage);
	p->externalFormat = (uint64_t) (uintptr_t) p->format;

	VkFormatProperties format_properties;
	radv_GetPhysicalDeviceFormatProperties(
		radv_physical_device_to_handle(device->physical_device),
		p->format, &format_properties);

	if (desc.usage & AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER)
		p->formatFeatures = format_properties.linearTilingFeatures;
	else
		p->formatFeatures = format_properties.optimalTilingFeatures;

	/* "Images can be created with an external format even if the Android hardware
	 *  buffer has a format which has an equivalent Vulkan format to enable
	 *  consistent handling of images from sources that might use either category
	 *  of format. However, all images created with an external format are subject
	 *  to the valid usage requirements associated with external formats, even if
	 *  the Android hardware buffer’s format has a Vulkan equivalent."
	 *
	 * "The formatFeatures member *must* include
	 *  VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT and at least one of
	 *  VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT or
	 *  VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT"
	 */
	assert(p->formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

	p->formatFeatures |= VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;

	/* "Implementations may not always be able to determine the color model,
	 *  numerical range, or chroma offsets of the image contents, so the values
	 *  in VkAndroidHardwareBufferFormatPropertiesANDROID are only suggestions.
	 *  Applications should treat these values as sensible defaults to use in
	 *  the absence of more reliable information obtained through some other
	 *  means."
	 */
	p->samplerYcbcrConversionComponents.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	p->samplerYcbcrConversionComponents.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	p->samplerYcbcrConversionComponents.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	p->samplerYcbcrConversionComponents.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
	p->suggestedYcbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;

	p->suggestedXChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
	p->suggestedYChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;

	return VK_SUCCESS;
}

VkResult
radv_GetAndroidHardwareBufferPropertiesANDROID(
   VkDevice device_h,
   const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferPropertiesANDROID *pProperties)
{
	RADV_FROM_HANDLE(radv_device, dev, device_h);
	struct radv_physical_device *pdevice = dev->physical_device;

	VkAndroidHardwareBufferFormatPropertiesANDROID *format_prop =
		vk_find_struct(pProperties->pNext,
			ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID);

	/* Fill format properties of an Android hardware buffer. */
	if (format_prop)
		get_ahb_buffer_format_properties(device_h, buffer, format_prop);

	/* NOTE - We support buffers with only one handle but do not error on
	 * multiple handle case. Reason is that we want to support YUV formats
	 * where we have many logical planes but they all point to the same
	 * buffer, like is the case with VK_FORMAT_G8_B8R8_2PLANE_420_UNORM.
	 */
	const native_handle_t *handle =
		AHardwareBuffer_getNativeHandle(buffer);
	int dma_buf = (handle && handle->numFds) ? handle->data[0] : -1;
	if (dma_buf < 0)
		return VK_ERROR_INVALID_EXTERNAL_HANDLE;

	/* All memory types. */
	uint32_t memory_types = (1u << pdevice->memory_properties.memoryTypeCount) - 1;

	pProperties->allocationSize = lseek(dma_buf, 0, SEEK_END);
	pProperties->memoryTypeBits = memory_types;

	return VK_SUCCESS;
}

VkResult
radv_GetMemoryAndroidHardwareBufferANDROID(
   VkDevice device_h,
   const VkMemoryGetAndroidHardwareBufferInfoANDROID *pInfo,
   struct AHardwareBuffer **pBuffer)
{
	RADV_FROM_HANDLE(radv_device_memory, mem, pInfo->memory);

	/* This should always be set due to the export handle types being set on
	 * allocation. */
	assert(mem->android_hardware_buffer);

	/* Some quotes from Vulkan spec:
	 *
	 * "If the device memory was created by importing an Android hardware
	 * buffer, vkGetMemoryAndroidHardwareBufferANDROID must return that same
	 * Android hardware buffer object."
	 *
	 * "VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID must
	 * have been included in VkExportMemoryAllocateInfo::handleTypes when
	 * memory was created."
	 */
	*pBuffer = mem->android_hardware_buffer;
	/* Increase refcount. */
	AHardwareBuffer_acquire(mem->android_hardware_buffer);
	return VK_SUCCESS;
}

#endif

VkFormat
radv_select_android_external_format(const void *next, VkFormat default_format)
{
#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
	const VkExternalFormatANDROID *android_format =
		vk_find_struct_const(next, EXTERNAL_FORMAT_ANDROID);

	if (android_format && android_format->externalFormat) {
		return (VkFormat)android_format->externalFormat;
	}
#endif

	return default_format;
}


VkResult
radv_import_ahb_memory(struct radv_device *device,
                       struct radv_device_memory *mem,
                       unsigned priority,
                       const VkImportAndroidHardwareBufferInfoANDROID *info)
{
#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
	/* Import from AHardwareBuffer to radv_device_memory. */
	const native_handle_t *handle =
		AHardwareBuffer_getNativeHandle(info->buffer);

	/* NOTE - We support buffers with only one handle but do not error on
	 * multiple handle case. Reason is that we want to support YUV formats
	 * where we have many logical planes but they all point to the same
	 * buffer, like is the case with VK_FORMAT_G8_B8R8_2PLANE_420_UNORM.
	 */
	int dma_buf = (handle && handle->numFds) ? handle->data[0] : -1;
	if (dma_buf < 0)
		return VK_ERROR_INVALID_EXTERNAL_HANDLE;

	uint64_t alloc_size = 0;
	mem->bo = device->ws->buffer_from_fd(device->ws, dma_buf,
	                                     priority, &alloc_size);
	if (!mem->bo)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	if (mem->image) {
		struct radeon_bo_metadata metadata;
		device->ws->buffer_get_metadata(device->ws, mem->bo, &metadata);

		struct radv_image_create_info create_info = {
			.no_metadata_planes = true,
			.bo_metadata = &metadata
		};

		VkResult result = radv_image_create_layout(device, create_info, NULL, mem->image);
		if (result != VK_SUCCESS) {
			device->ws->buffer_destroy(device->ws, mem->bo);
			mem->bo = NULL;
			return result;
		}

		if (alloc_size < mem->image->size) {
			device->ws->buffer_destroy(device->ws, mem->bo);
			mem->bo = NULL;
			return VK_ERROR_INVALID_EXTERNAL_HANDLE;
		}
	} else if (mem->buffer) {
		if (alloc_size < mem->buffer->size) {
			device->ws->buffer_destroy(device->ws, mem->bo);
			mem->bo = NULL;
			return VK_ERROR_INVALID_EXTERNAL_HANDLE;
		}
	}

	/* "If the vkAllocateMemory command succeeds, the implementation must
	 * acquire a reference to the imported hardware buffer, which it must
	 * release when the device memory object is freed. If the command fails,
	 * the implementation must not retain a reference."
	 */
	AHardwareBuffer_acquire(info->buffer);
	mem->android_hardware_buffer = info->buffer;

	return VK_SUCCESS;
#else /* RADV_SUPPORT_ANDROID_HARDWARE_BUFFER */
	return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

VkResult
radv_create_ahb_memory(struct radv_device *device,
                       struct radv_device_memory *mem,
                       unsigned priority,
                       const VkMemoryAllocateInfo *pAllocateInfo)
{
#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
	const VkMemoryDedicatedAllocateInfo *dedicated_info =
		vk_find_struct_const(pAllocateInfo->pNext,
		                     MEMORY_DEDICATED_ALLOCATE_INFO);

	uint32_t w = 0;
	uint32_t h = 1;
	uint32_t layers = 1;
	uint32_t format = 0;
	uint64_t usage = 0;

	/* If caller passed dedicated information. */
	if (dedicated_info && dedicated_info->image) {
		RADV_FROM_HANDLE(radv_image, image, dedicated_info->image);
		w = image->info.width;
		h = image->info.height;
		layers = image->info.array_size;
		format = android_format_from_vk(image->vk_format);
		usage = radv_ahb_usage_from_vk_usage(image->flags, image->usage);
	} else if (dedicated_info && dedicated_info->buffer) {
		RADV_FROM_HANDLE(radv_buffer, buffer, dedicated_info->buffer);
		w = buffer->size;
		format = AHARDWAREBUFFER_FORMAT_BLOB;
		usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
		        AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
	} else {
		w = pAllocateInfo->allocationSize;
		format = AHARDWAREBUFFER_FORMAT_BLOB;
		usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
		        AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
	}

	struct AHardwareBuffer *android_hardware_buffer = NULL;
	struct AHardwareBuffer_Desc desc = {
		.width = w,
		.height = h,
		.layers = layers,
		.format = format,
		.usage = usage,
	};

	if (AHardwareBuffer_allocate(&desc, &android_hardware_buffer) != 0)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	mem->android_hardware_buffer = android_hardware_buffer;

	const struct VkImportAndroidHardwareBufferInfoANDROID import_info = {
		.buffer = mem->android_hardware_buffer,
	};

	VkResult result = radv_import_ahb_memory(device, mem, priority, &import_info);
	if (result != VK_SUCCESS)
		AHardwareBuffer_release(mem->android_hardware_buffer);
	return result;
#else /* RADV_SUPPORT_ANDROID_HARDWARE_BUFFER */
	return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

bool radv_android_gralloc_supports_format(VkFormat format, VkImageUsageFlagBits usage) {
#if RADV_SUPPORT_ANDROID_HARDWARE_BUFFER
	/* Ideally we check Gralloc for what it supports and then merge that with the radv
	   format support, but there is no easy gralloc query besides just creating an image.
	   That seems a bit on the expensive side, so just hardcode for now. */
	/* TODO: Add multi-plane formats after confirming everything works between radeonsi
	   and radv. */
	switch(format) {
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R5G6B5_UNORM_PACK16:
		return true;
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8G8_UNORM:
		return !(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	default:
		return false;
	}
#else
	(void)format;
	(void)usage;
	return false;
#endif
}
