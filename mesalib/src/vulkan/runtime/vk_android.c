/*
 * Copyright © 2022 Intel Corporation
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

#include "vk_android.h"

#include "vk_alloc.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"
#include "vk_physical_device.h"
#include "vk_image.h"
#include "vk_log.h"
#include "vk_queue.h"
#include "vk_util.h"

#include "vk_enum_defines.h"

#include "drm-uapi/drm_fourcc.h"
#include "util/libsync.h"
#include "util/os_file.h"
#include "util/u_gralloc/u_gralloc.h"
#include "util/log.h"

#include <hardware/gralloc.h>

#if ANDROID_API_LEVEL >= 26
#include <hardware/gralloc1.h>
#endif

#include <unistd.h>

static struct u_gralloc *u_gralloc;

struct u_gralloc *
vk_android_get_ugralloc(void)
{
   return u_gralloc;
}

struct u_gralloc *
vk_android_init_ugralloc(void)
{
   u_gralloc = u_gralloc_create(U_GRALLOC_TYPE_AUTO);

   return u_gralloc;
}

void
vk_android_destroy_ugralloc(void)
{
   u_gralloc_destroy(&u_gralloc);
}

/* If any bits in test_mask are set, then unset them and return true. */
static inline bool
unmask32(uint32_t *inout_mask, uint32_t test_mask)
{
   uint32_t orig_mask = *inout_mask;
   *inout_mask &= ~test_mask;
   return *inout_mask != orig_mask;
}

static VkResult
format_supported_with_usage(struct vk_device *device, VkFormat format,
                            VkImageUsageFlags imageUsage)
{
   struct vk_physical_device *physical = device->physical;
   VkResult result;

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
   result = physical->dispatch_table.GetPhysicalDeviceImageFormatProperties2(
      (VkPhysicalDevice)physical, &image_format_info, &image_format_props);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static VkResult
setup_gralloc0_usage(struct vk_device *device, VkFormat format,
                     VkImageUsageFlags imageUsage, int *grallocUsage)
{
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
      return vk_errorf(device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                       "unsupported VkImageUsageFlags(0x%x) for gralloc "
                       "swapchain",
                       imageUsage);
   }

   *grallocUsage |= GRALLOC_USAGE_HW_COMPOSER;

   if (*grallocUsage == 0)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetSwapchainGrallocUsageANDROID(VkDevice device_h, VkFormat format,
                                          VkImageUsageFlags imageUsage,
                                          int *grallocUsage)
{
   VK_FROM_HANDLE(vk_device, device, device_h);
   VkResult result;

   result = format_supported_with_usage(device, format, imageUsage);
   if (result != VK_SUCCESS)
      return result;

   *grallocUsage = 0;
   return setup_gralloc0_usage(device, format, imageUsage, grallocUsage);
}

#if ANDROID_API_LEVEL >= 26
#include <vndk/hardware_buffer.h>

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetSwapchainGrallocUsage2ANDROID(
   VkDevice device_h, VkFormat format, VkImageUsageFlags imageUsage,
   VkSwapchainImageUsageFlagsANDROID swapchainImageUsage,
   uint64_t *grallocConsumerUsage, uint64_t *grallocProducerUsage)
{
   VK_FROM_HANDLE(vk_device, device, device_h);
   VkResult result;

   *grallocConsumerUsage = 0;
   *grallocProducerUsage = 0;

   result = format_supported_with_usage(device, format, imageUsage);
   if (result != VK_SUCCESS)
      return result;

   int32_t grallocUsage = 0;
   result = setup_gralloc0_usage(device, format, imageUsage, &grallocUsage);
   if (result != VK_SUCCESS)
      return result;

   /* Setup gralloc1 usage flags from gralloc0 flags. */

   if (grallocUsage & GRALLOC_USAGE_HW_RENDER)
      *grallocProducerUsage |= GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;

   if (grallocUsage & GRALLOC_USAGE_HW_TEXTURE)
      *grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;

   if (grallocUsage & GRALLOC_USAGE_HW_COMPOSER) {
      /* GPU composing case */
      *grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
      /* Hardware composing case */
      *grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_HWCOMPOSER;
   }

   if ((swapchainImageUsage & VK_SWAPCHAIN_IMAGE_USAGE_SHARED_BIT_ANDROID) &&
       vk_android_get_ugralloc() != NULL) {
      uint64_t front_rendering_usage = 0;
      u_gralloc_get_front_rendering_usage(vk_android_get_ugralloc(),
                                          &front_rendering_usage);
      *grallocProducerUsage |= front_rendering_usage;
   }

   return VK_SUCCESS;
}

static VkResult
vk_gralloc_to_drm_explicit_layout(
   struct u_gralloc_buffer_handle *in_hnd,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes)
{
   struct u_gralloc_buffer_basic_info info;
   struct u_gralloc *u_gralloc = vk_android_get_ugralloc();
   assert(u_gralloc);

   if (u_gralloc_get_buffer_basic_info(u_gralloc, in_hnd, &info) != 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   if (info.num_planes > max_planes)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   bool is_disjoint = false;
   for (size_t i = 1; i < info.num_planes; i++) {
      if (info.offsets[i] == 0) {
         is_disjoint = true;
         break;
      }
   }

   if (is_disjoint) {
      /* We don't support disjoint planes yet */
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   memset(out, 0, sizeof(*out));
   memset(out_layouts, 0, sizeof(*out_layouts) * max_planes);

   out->sType =
      VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
   out->pPlaneLayouts = out_layouts;

   out->drmFormatModifier = info.modifier;
   out->drmFormatModifierPlaneCount = info.num_planes;
   for (size_t i = 0; i < info.num_planes; i++) {
      out_layouts[i].offset = info.offsets[i];
      out_layouts[i].rowPitch = info.strides[i];
   }

   if (info.drm_fourcc == DRM_FORMAT_YVU420) {
      /* Swap the U and V planes to match the
       * VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM */
      VkSubresourceLayout tmp = out_layouts[1];
      out_layouts[1] = out_layouts[2];
      out_layouts[2] = tmp;
   }

   return VK_SUCCESS;
}

VkResult
vk_android_import_anb(struct vk_device *device,
                      const VkImageCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *alloc,
                      struct vk_image *image)
{
   VkResult result;

   const VkNativeBufferANDROID *native_buffer =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);

   assert(native_buffer);
   assert(native_buffer->handle);
   assert(native_buffer->handle->numFds > 0);

   const VkMemoryDedicatedAllocateInfo ded_alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = NULL,
      .buffer = VK_NULL_HANDLE,
      .image = (VkImage)image};

   const VkImportMemoryFdInfoKHR import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = &ded_alloc,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      .fd = os_dupfd_cloexec(native_buffer->handle->data[0]),
   };

   result = device->dispatch_table.AllocateMemory(
      (VkDevice)device,
      &(VkMemoryAllocateInfo){
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .pNext = &import_info,
         .allocationSize = lseek(import_info.fd, 0, SEEK_END),
         .memoryTypeIndex = 0, /* Should we be smarter here? */
      },
      alloc, &image->anb_memory);

   if (result != VK_SUCCESS) {
      close(import_info.fd);
      return result;
   }

   VkBindImageMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
      .image = (VkImage)image,
      .memory = image->anb_memory,
      .memoryOffset = 0,
   };

   return device->dispatch_table.BindImageMemory2((VkDevice)device, 1, &bind_info);
}

VkResult
vk_android_get_anb_layout(
   const VkImageCreateInfo *pCreateInfo,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes)
{
   const VkNativeBufferANDROID *native_buffer =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);

   struct u_gralloc_buffer_handle gr_handle = {
      .handle = native_buffer->handle,
      .hal_format = native_buffer->format,
      .pixel_stride = native_buffer->stride,
   };

   return vk_gralloc_to_drm_explicit_layout(&gr_handle, out,
                                            out_layouts, max_planes);
}

VkResult
vk_android_get_ahb_layout(
   struct AHardwareBuffer *ahardware_buffer,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes)
{
   AHardwareBuffer_Desc description;
   const native_handle_t *handle =
      AHardwareBuffer_getNativeHandle(ahardware_buffer);

   AHardwareBuffer_describe(ahardware_buffer, &description);

   struct u_gralloc_buffer_handle gr_handle = {
      .handle = handle,
      .pixel_stride = description.stride,
      .hal_format = description.format,
   };

   return vk_gralloc_to_drm_explicit_layout(&gr_handle, out,
                                            out_layouts, max_planes);
}

/* From the Android hardware_buffer.h header:
 *
 *    "The buffer will be written to by the GPU as a framebuffer attachment.
 *
 *    Note that the name of this flag is somewhat misleading: it does not
 *    imply that the buffer contains a color format. A buffer with depth or
 *    stencil format that will be used as a framebuffer attachment should
 *    also have this flag. Use the equivalent flag
 *    AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER to avoid this confusion."
 *
 * The flag was renamed from COLOR_OUTPUT to FRAMEBUFFER at Android API
 * version 29.
 */
#if ANDROID_API_LEVEL < 29
#define AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT
#endif

/* Convert an AHB format to a VkFormat, based on the "AHardwareBuffer Format
 * Equivalence" table in Vulkan spec.
 *
 * Note that this only covers a subset of AHB formats defined in NDK.  Drivers
 * can support more AHB formats, including private ones.
 */
VkFormat
vk_ahb_format_to_image_format(uint32_t ahb_format)
{
   switch (ahb_format) {
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
   case AHARDWAREBUFFER_FORMAT_D16_UNORM:
      return VK_FORMAT_D16_UNORM;
   case AHARDWAREBUFFER_FORMAT_D24_UNORM:
      return VK_FORMAT_X8_D24_UNORM_PACK32;
   case AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT:
      return VK_FORMAT_D24_UNORM_S8_UINT;
   case AHARDWAREBUFFER_FORMAT_D32_FLOAT:
      return VK_FORMAT_D32_SFLOAT;
   case AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT:
      return VK_FORMAT_D32_SFLOAT_S8_UINT;
   case AHARDWAREBUFFER_FORMAT_S8_UINT:
      return VK_FORMAT_S8_UINT;
#if ANDROID_API_LEVEL >= 33
   case AHARDWAREBUFFER_FORMAT_R8_UNORM:
      return VK_FORMAT_R8_UNORM;
#endif
   case AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM:
      return VK_FORMAT_B8G8R8A8_UNORM;
   default:
      return VK_FORMAT_UNDEFINED;
   }
}

/* Convert a VkFormat to an AHB format, based on the "AHardwareBuffer Format
 * Equivalence" table in Vulkan spec.
 *
 * Note that this only covers a subset of AHB formats defined in NDK.  Drivers
 * can support more AHB formats, including private ones.
 */
uint32_t
vk_image_format_to_ahb_format(VkFormat vk_format)
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
   case VK_FORMAT_D16_UNORM:
      return AHARDWAREBUFFER_FORMAT_D16_UNORM;
   case VK_FORMAT_X8_D24_UNORM_PACK32:
      return AHARDWAREBUFFER_FORMAT_D24_UNORM;
   case VK_FORMAT_D24_UNORM_S8_UINT:
      return AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT;
   case VK_FORMAT_D32_SFLOAT:
      return AHARDWAREBUFFER_FORMAT_D32_FLOAT;
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT;
   case VK_FORMAT_S8_UINT:
      return AHARDWAREBUFFER_FORMAT_S8_UINT;
#if ANDROID_API_LEVEL >= 33
   case VK_FORMAT_R8_UNORM:
      return AHARDWAREBUFFER_FORMAT_R8_UNORM;
#endif
   case VK_FORMAT_B8G8R8A8_UNORM:
      return AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM;
   default:
      return 0;
   }
}

/* Construct ahw usage mask from image usage bits, see
 * 'AHardwareBuffer Usage Equivalence' in Vulkan spec.
 */
uint64_t
vk_image_usage_to_ahb_usage(const VkImageCreateFlags vk_create,
                            const VkImageUsageFlags vk_usage)
{
   uint64_t ahb_usage = 0;
   if (vk_usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   if (vk_usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;

   if (vk_usage & VK_IMAGE_USAGE_STORAGE_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;

   if (vk_create & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_CUBE_MAP;

   if (vk_create & VK_IMAGE_CREATE_PROTECTED_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT;

   /* No usage bits set - set at least one GPU usage. */
   if (ahb_usage == 0)
      ahb_usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   return ahb_usage;
}

/* Probe gralloc implementation to test whether it can allocate a buffer
 * for the given format and usage.  Vk drivers must not advertise support
 * for AHB backed VkImage's if the gralloc implementation is not able to
 * perform the allocation.
 */
bool
vk_ahb_probe_format(VkFormat vk_format,
                    VkImageCreateFlags vk_create,
                    VkImageUsageFlags vk_usage)
{
   AHardwareBuffer_Desc desc = {
      .width = 16,
      .height = 16,
      .layers = 1,
      .format = vk_image_format_to_ahb_format(vk_format),
      .usage = vk_image_usage_to_ahb_usage(vk_create, vk_usage),
   };
#if ANDROID_API_LEVEL >= 29
   return AHardwareBuffer_isSupported(&desc);
#else
   AHardwareBuffer *ahb = NULL;
   int ret = 0;

   ret = AHardwareBuffer_allocate(&desc, &ahb);
   if (ret)
      return false;

   AHardwareBuffer_release(ahb);

   return true;
#endif
}

struct AHardwareBuffer *
vk_alloc_ahardware_buffer(const VkMemoryAllocateInfo *pAllocateInfo)
{
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
      VK_FROM_HANDLE(vk_image, image, dedicated_info->image);

      if (!image->ahb_format)
         return NULL;

      w = image->extent.width;
      h = image->extent.height;
      layers = image->array_layers;
      format = image->ahb_format;
      usage = vk_image_usage_to_ahb_usage(image->create_flags,
                                          image->usage);
   } else {
      /* AHB export allocation for VkBuffer requires a valid allocationSize */
      assert(pAllocateInfo->allocationSize);
      w = pAllocateInfo->allocationSize;
      format = AHARDWAREBUFFER_FORMAT_BLOB;
      usage = AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER |
              AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
              AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
   }

   struct AHardwareBuffer_Desc desc = {
      .width = w,
      .height = h,
      .layers = layers,
      .format = format,
      .usage = usage,
    };

   struct AHardwareBuffer *ahb;
   if (AHardwareBuffer_allocate(&desc, &ahb) != 0)
      return NULL;

   return ahb;
}

static VkResult
get_ahb_buffer_format_properties2(
   struct vk_device *device, const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferFormatProperties2ANDROID *pProperties)
{
   /* Get a description of buffer contents . */
   AHardwareBuffer_Desc desc;
   AHardwareBuffer_describe(buffer, &desc);

   /* Verify description. */
   bool gpu_usage = desc.usage & (AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                                  AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                                  AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER);

   /* "Buffer must be a valid Android hardware buffer object with at least
    * one of the AHARDWAREBUFFER_USAGE_GPU_* usage flags."
    */
   if (!gpu_usage)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* Fill properties fields based on description. */
   VkAndroidHardwareBufferFormatProperties2ANDROID *p = pProperties;

   p->samplerYcbcrConversionComponents.r = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.g = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.b = VK_COMPONENT_SWIZZLE_IDENTITY;
   p->samplerYcbcrConversionComponents.a = VK_COMPONENT_SWIZZLE_IDENTITY;

   p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
   p->suggestedYcbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;

   p->suggestedXChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;
   p->suggestedYChromaOffset = VK_CHROMA_LOCATION_MIDPOINT;

   VkFormatProperties2 format_properties = {.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};

   p->format = vk_ahb_format_to_image_format(desc.format);

   VkFormat external_format = p->format;

   if (p->format != VK_FORMAT_UNDEFINED)
      goto finish;

   /* External format only case
    *
    * From vkGetAndroidHardwareBufferPropertiesANDROID spec:
    * "If the Android hardware buffer has one of the formats listed in the Format
    * Equivalence table (see spec.), then format must have the equivalent Vulkan
    * format listed in the table. Otherwise, format may be VK_FORMAT_UNDEFINED,
    * indicating the Android hardware buffer can only be used with an external format."
    *
    * From SKIA source code analysis: p->format MUST be VK_FORMAT_UNDEFINED, if the
    * format is not in the Equivalence table.
    */

   struct u_gralloc_buffer_handle gr_handle = {
      .handle = AHardwareBuffer_getNativeHandle(buffer),
      .pixel_stride = desc.stride,
      .hal_format = desc.format,
   };

   struct u_gralloc_buffer_basic_info info;

   if (u_gralloc_get_buffer_basic_info(vk_android_get_ugralloc(), &gr_handle, &info) != 0)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   switch (info.drm_fourcc) {
   case DRM_FORMAT_YVU420:
      /* Assuming that U and V planes are swapped earlier */
      external_format = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
      break;
   case DRM_FORMAT_NV12:
      external_format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
      break;
   default:;
      mesa_loge("Unsupported external DRM format: %d", info.drm_fourcc);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   struct u_gralloc_buffer_color_info color_info;
   if (u_gralloc_get_buffer_color_info(vk_android_get_ugralloc(), &gr_handle, &color_info) == 0) {
      switch (color_info.yuv_color_space) {
      case __DRI_YUV_COLOR_SPACE_ITU_REC601:
         p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
         break;
      case __DRI_YUV_COLOR_SPACE_ITU_REC709:
         p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
         break;
      case __DRI_YUV_COLOR_SPACE_ITU_REC2020:
         p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020;
         break;
      default:
         break;
      }

      p->suggestedYcbcrRange = (color_info.sample_range == __DRI_YUV_NARROW_RANGE) ?
         VK_SAMPLER_YCBCR_RANGE_ITU_NARROW : VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
      p->suggestedXChromaOffset = (color_info.horizontal_siting == __DRI_YUV_CHROMA_SITING_0_5) ?
         VK_CHROMA_LOCATION_MIDPOINT : VK_CHROMA_LOCATION_COSITED_EVEN;
      p->suggestedYChromaOffset = (color_info.vertical_siting == __DRI_YUV_CHROMA_SITING_0_5) ?
         VK_CHROMA_LOCATION_MIDPOINT : VK_CHROMA_LOCATION_COSITED_EVEN;
   } else {
      p->suggestedYcbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601;
      p->suggestedYcbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
   }

finish:

   device->physical->dispatch_table.GetPhysicalDeviceFormatProperties2(
      (VkPhysicalDevice)device->physical, external_format, &format_properties);

   p->formatFeatures = format_properties.formatProperties.optimalTilingFeatures;
   p->externalFormat = external_format;

   /* From vkGetAndroidHardwareBufferPropertiesANDROID spec:
    * "The formatFeatures member *must* include
    *  VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT and at least one of
    *  VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT or
    *  VK_FORMAT_FEATURE_2_COSITED_CHROMA_SAMPLES_BIT"
    */
   p->formatFeatures |= VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT_KHR;

   return VK_SUCCESS;
}

VkResult
vk_common_GetAndroidHardwareBufferPropertiesANDROID(
   VkDevice device_h, const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferPropertiesANDROID *pProperties)
{
   VK_FROM_HANDLE(vk_device, device, device_h);
   struct vk_physical_device *pdevice = device->physical;

   VkResult result;

   VkAndroidHardwareBufferFormatPropertiesANDROID *format_prop =
      vk_find_struct(pProperties->pNext, ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID);

   /* Fill format properties of an Android hardware buffer. */
   if (format_prop) {
      VkAndroidHardwareBufferFormatProperties2ANDROID format_prop2 = {
         .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_2_ANDROID,
      };
      result = get_ahb_buffer_format_properties2(device, buffer, &format_prop2);
      if (result != VK_SUCCESS)
         return result;

      format_prop->format                 = format_prop2.format;
      format_prop->externalFormat         = format_prop2.externalFormat;
      format_prop->formatFeatures         =
         vk_format_features2_to_features(format_prop2.formatFeatures);
      format_prop->samplerYcbcrConversionComponents =
         format_prop2.samplerYcbcrConversionComponents;
      format_prop->suggestedYcbcrModel    = format_prop2.suggestedYcbcrModel;
      format_prop->suggestedYcbcrRange    = format_prop2.suggestedYcbcrRange;
      format_prop->suggestedXChromaOffset = format_prop2.suggestedXChromaOffset;
      format_prop->suggestedYChromaOffset = format_prop2.suggestedYChromaOffset;
   }

   VkAndroidHardwareBufferFormatProperties2ANDROID *format_prop2 =
      vk_find_struct(pProperties->pNext, ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_2_ANDROID);
   if (format_prop2) {
      result = get_ahb_buffer_format_properties2(device, buffer, format_prop2);
      if (result != VK_SUCCESS)
         return result;
   }

   const native_handle_t *handle = AHardwareBuffer_getNativeHandle(buffer);
   assert(handle && handle->numFds > 0);
   pProperties->allocationSize = lseek(handle->data[0], 0, SEEK_END);

   VkPhysicalDeviceMemoryProperties mem_props;

   device->physical->dispatch_table.GetPhysicalDeviceMemoryProperties(
      (VkPhysicalDevice)pdevice, &mem_props);

   /* All memory types. (Should we be smarter than this?) */
   pProperties->memoryTypeBits = (1u << mem_props.memoryTypeCount) - 1;

   return VK_SUCCESS;
}
#endif /* ANDROID_API_LEVEL >= 26 */

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_AcquireImageANDROID(VkDevice _device,
                              VkImage image,
                              int nativeFenceFd,
                              VkSemaphore semaphore,
                              VkFence fence)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   VkResult result = VK_SUCCESS;

   /* From https://source.android.com/devices/graphics/implement-vulkan :
    *
    *    "The driver takes ownership of the fence file descriptor and closes
    *    the fence file descriptor when no longer needed. The driver must do
    *    so even if neither a semaphore or fence object is provided, or even
    *    if vkAcquireImageANDROID fails and returns an error."
    *
    * The Vulkan spec for VkImportFence/SemaphoreFdKHR(), however, requires
    * the file descriptor to be left alone on failure.
    */
   int semaphore_fd = -1, fence_fd = -1;
   if (nativeFenceFd >= 0) {
      if (semaphore != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
         /* We have both so we have to import the sync file twice. One of
          * them needs to be a dup.
          */
         semaphore_fd = nativeFenceFd;
         fence_fd = dup(nativeFenceFd);
         if (fence_fd < 0) {
            VkResult err = (errno == EMFILE) ? VK_ERROR_TOO_MANY_OBJECTS :
                                               VK_ERROR_OUT_OF_HOST_MEMORY;
            close(nativeFenceFd);
            return vk_error(device, err);
         }
      } else if (semaphore != VK_NULL_HANDLE) {
         semaphore_fd = nativeFenceFd;
      } else if (fence != VK_NULL_HANDLE) {
         fence_fd = nativeFenceFd;
      } else {
         /* Nothing to import into so we have to close the file */
         close(nativeFenceFd);
      }
   }

   if (semaphore != VK_NULL_HANDLE) {
      const VkImportSemaphoreFdInfoKHR info = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
         .semaphore = semaphore,
         .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
         .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
         .fd = semaphore_fd,
      };
      result = device->dispatch_table.ImportSemaphoreFdKHR(_device, &info);
      if (result == VK_SUCCESS)
         semaphore_fd = -1; /* The driver took ownership */
   }

   if (result == VK_SUCCESS && fence != VK_NULL_HANDLE) {
      const VkImportFenceFdInfoKHR info = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR,
         .fence = fence,
         .flags = VK_FENCE_IMPORT_TEMPORARY_BIT,
         .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT,
         .fd = fence_fd,
      };
      result = device->dispatch_table.ImportFenceFdKHR(_device, &info);
      if (result == VK_SUCCESS)
         fence_fd = -1; /* The driver took ownership */
   }

   if (semaphore_fd >= 0)
      close(semaphore_fd);
   if (fence_fd >= 0)
      close(fence_fd);

   return result;
}

static VkResult
vk_anb_semaphore_init_once(struct vk_queue *queue, struct vk_device *device)
{
   if (queue->anb_semaphore != VK_NULL_HANDLE)
      return VK_SUCCESS;

   const VkExportSemaphoreCreateInfo export_info = {
      .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
   };
   const VkSemaphoreCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &export_info,
   };
   return device->dispatch_table.CreateSemaphore(vk_device_to_handle(device),
                                                 &create_info, NULL,
                                                 &queue->anb_semaphore);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_QueueSignalReleaseImageANDROID(VkQueue _queue,
                                         uint32_t waitSemaphoreCount,
                                         const VkSemaphore *pWaitSemaphores,
                                         VkImage image,
                                         int *pNativeFenceFd)
{
   VK_FROM_HANDLE(vk_queue, queue, _queue);
   struct vk_device *device = queue->base.device;
   VkResult result = VK_SUCCESS;

   STACK_ARRAY(VkPipelineStageFlags, stage_flags, MAX2(1, waitSemaphoreCount));
   for (uint32_t i = 0; i < MAX2(1, waitSemaphoreCount); i++)
      stage_flags[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

   result = vk_anb_semaphore_init_once(queue, device);
   if (result != VK_SUCCESS) {
      STACK_ARRAY_FINISH(stage_flags);
      return result;
   }

   const VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .waitSemaphoreCount = waitSemaphoreCount,
      .pWaitSemaphores = pWaitSemaphores,
      .pWaitDstStageMask = stage_flags,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores = &queue->anb_semaphore,
   };
   result = device->dispatch_table.QueueSubmit(_queue, 1, &submit_info,
                                               VK_NULL_HANDLE);
   STACK_ARRAY_FINISH(stage_flags);
   if (result != VK_SUCCESS)
      return result;

   const VkSemaphoreGetFdInfoKHR get_fd = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
      .semaphore = queue->anb_semaphore,
   };
   return device->dispatch_table.GetSemaphoreFdKHR(vk_device_to_handle(device),
                                                   &get_fd, pNativeFenceFd);
}
