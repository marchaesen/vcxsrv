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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "v3dv_private.h"
#include <hardware/gralloc.h>

#if ANDROID_API_LEVEL >= 26
#include <hardware/gralloc1.h>
#endif

#include "drm-uapi/drm_fourcc.h"
#include <hardware/hardware.h>
#include <hardware/hwvulkan.h>

#include <vulkan/vk_android_native_buffer.h>
#include <vulkan/vk_icd.h>

#include "util/libsync.h"
#include "util/log.h"
#include "util/os_file.h"

static int
v3dv_hal_open(const struct hw_module_t *mod,
              const char *id,
              struct hw_device_t **dev);
static int
v3dv_hal_close(struct hw_device_t *dev);

static void UNUSED
static_asserts(void)
{
   STATIC_ASSERT(HWVULKAN_DISPATCH_MAGIC == ICD_LOADER_MAGIC);
}

PUBLIC struct hwvulkan_module_t HAL_MODULE_INFO_SYM = {
   .common =
     {
       .tag = HARDWARE_MODULE_TAG,
       .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
       .hal_api_version = HARDWARE_MAKE_API_VERSION(1, 0),
       .id = HWVULKAN_HARDWARE_MODULE_ID,
       .name = "Broadcom Vulkan HAL",
       .author = "Mesa3D",
       .methods =
         &(hw_module_methods_t) {
           .open = v3dv_hal_open,
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
v3dv_hal_open(const struct hw_module_t *mod,
              const char *id,
              struct hw_device_t **dev)
{
   assert(mod == &HAL_MODULE_INFO_SYM.common);
   assert(strcmp(id, HWVULKAN_DEVICE_0) == 0);

   hwvulkan_device_t *hal_dev = malloc(sizeof(*hal_dev));
   if (!hal_dev)
      return -1;

   *hal_dev = (hwvulkan_device_t){
      .common =
        {
          .tag = HARDWARE_DEVICE_TAG,
          .version = HWVULKAN_DEVICE_API_VERSION_0_1,
          .module = &HAL_MODULE_INFO_SYM.common,
          .close = v3dv_hal_close,
        },
     .EnumerateInstanceExtensionProperties =
        v3dv_EnumerateInstanceExtensionProperties,
     .CreateInstance = v3dv_CreateInstance,
     .GetInstanceProcAddr = v3dv_GetInstanceProcAddr,
   };

   mesa_logi("v3dv: Warning: Android Vulkan implementation is experimental");

   *dev = &hal_dev->common;
   return 0;
}

static int
v3dv_hal_close(struct hw_device_t *dev)
{
   /* hwvulkan.h claims that hw_device_t::close() is never called. */
   return -1;
}

static int
get_format_bpp(int native)
{
   int bpp;

   switch (native) {
   case HAL_PIXEL_FORMAT_RGBA_FP16:
      bpp = 8;
      break;
   case HAL_PIXEL_FORMAT_RGBA_8888:
   case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
   case HAL_PIXEL_FORMAT_RGBX_8888:
   case HAL_PIXEL_FORMAT_BGRA_8888:
   case HAL_PIXEL_FORMAT_RGBA_1010102:
      bpp = 4;
      break;
   case HAL_PIXEL_FORMAT_RGB_565:
      bpp = 2;
      break;
   default:
      bpp = 0;
      break;
   }

   return bpp;
}

/* get buffer info from VkNativeBufferANDROID */
static VkResult
v3dv_gralloc_info_other(struct v3dv_device *device,
                        const VkNativeBufferANDROID *native_buffer,
                        int *out_stride,
                        uint64_t *out_modifier)
{
   *out_stride = native_buffer->stride /*in pixels*/ *
                 get_format_bpp(native_buffer->format);
   *out_modifier = DRM_FORMAT_MOD_LINEAR;
   return VK_SUCCESS;
}

static const char cros_gralloc_module_name[] = "CrOS Gralloc";

#define CROS_GRALLOC_DRM_GET_BUFFER_INFO 4

struct cros_gralloc0_buffer_info
{
   uint32_t drm_fourcc;
   int num_fds;
   int fds[4];
   uint64_t modifier;
   int offset[4];
   int stride[4];
};

static VkResult
v3dv_gralloc_info_cros(struct v3dv_device *device,
                       const VkNativeBufferANDROID *native_buffer,
                       int *out_stride,
                       uint64_t *out_modifier)
{
   const gralloc_module_t *gralloc = device->gralloc;
   struct cros_gralloc0_buffer_info info;
   int ret;

   ret = gralloc->perform(gralloc, CROS_GRALLOC_DRM_GET_BUFFER_INFO,
                          native_buffer->handle, &info);
   if (ret)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   *out_stride = info.stride[0];
   *out_modifier = info.modifier;

   return VK_SUCCESS;
}

VkResult
v3dv_gralloc_info(struct v3dv_device *device,
                  const VkNativeBufferANDROID *native_buffer,
                  int *out_dmabuf,
                  int *out_stride,
                  int *out_size,
                  uint64_t *out_modifier)
{
   if (device->gralloc_type == V3DV_GRALLOC_UNKNOWN) {
      /* get gralloc module for gralloc buffer info query */
      int err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                              (const hw_module_t **) &device->gralloc);

      device->gralloc_type = V3DV_GRALLOC_OTHER;

      if (err == 0) {
         const gralloc_module_t *gralloc = device->gralloc;
         mesa_logi("opened gralloc module name: %s", gralloc->common.name);

         if (strcmp(gralloc->common.name, cros_gralloc_module_name) == 0 &&
             gralloc->perform) {
            device->gralloc_type = V3DV_GRALLOC_CROS;
         }
      }
   }

   *out_dmabuf = native_buffer->handle->data[0];
   *out_size = lseek(*out_dmabuf, 0, SEEK_END);

   if (device->gralloc_type == V3DV_GRALLOC_CROS) {
      return v3dv_gralloc_info_cros(device, native_buffer, out_stride,
                                    out_modifier);
   } else {
      return v3dv_gralloc_info_other(device, native_buffer, out_stride,
                                     out_modifier);
   }
}

VkResult
v3dv_import_native_buffer_fd(VkDevice device_h,
                             int native_buffer_fd,
                             const VkAllocationCallbacks *alloc,
                             VkImage image_h)
{
   struct v3dv_image *image = NULL;
   VkResult result;

   image = v3dv_image_from_handle(image_h);

   VkDeviceMemory memory_h;

   const VkMemoryDedicatedAllocateInfo ded_alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = NULL,
      .buffer = VK_NULL_HANDLE,
      .image = image_h
   };

   const VkImportMemoryFdInfoKHR import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = &ded_alloc,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
      .fd = os_dupfd_cloexec(native_buffer_fd),
   };

   result =
      v3dv_AllocateMemory(device_h,
                          &(VkMemoryAllocateInfo) {
                             .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                             .pNext = &import_info,
                             .allocationSize = image->size,
                             .memoryTypeIndex = 0,
                          },
                          alloc, &memory_h);

   if (result != VK_SUCCESS)
      goto fail_create_image;

   VkBindImageMemoryInfo bind_info = {
      .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
      .image = image_h,
      .memory = memory_h,
      .memoryOffset = 0,
   };
   v3dv_BindImageMemory2(device_h, 1, &bind_info);

   image->is_native_buffer_memory = true;

   return VK_SUCCESS;

fail_create_image:
   close(import_info.fd);

   return result;
}

static VkResult
format_supported_with_usage(VkDevice device_h,
                            VkFormat format,
                            VkImageUsageFlags imageUsage)
{
   V3DV_FROM_HANDLE(v3dv_device, device, device_h);
   struct v3dv_physical_device *phys_dev = device->pdevice;
   VkPhysicalDevice phys_dev_h = v3dv_physical_device_to_handle(phys_dev);
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
   result = v3dv_GetPhysicalDeviceImageFormatProperties2(
      phys_dev_h, &image_format_info, &image_format_props);
   if (result != VK_SUCCESS) {
      return vk_errorf(device, result,
                       "v3dv_GetPhysicalDeviceImageFormatProperties2 failed "
                       "inside %s",
                       __func__);
   }

   return VK_SUCCESS;
}

static VkResult
setup_gralloc0_usage(struct v3dv_device *device,
                     VkFormat format,
                     VkImageUsageFlags imageUsage,
                     int *grallocUsage)
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

   /* Swapchain assumes direct displaying, therefore enable COMPOSER flag,
    * In case format is not supported by display controller, gralloc will
    * drop this flag and still allocate the buffer in VRAM
    */
   *grallocUsage |= GRALLOC_USAGE_HW_COMPOSER;

   if (*grallocUsage == 0)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetSwapchainGrallocUsageANDROID(VkDevice device_h,
                                     VkFormat format,
                                     VkImageUsageFlags imageUsage,
                                     int *grallocUsage)
{
   V3DV_FROM_HANDLE(v3dv_device, device, device_h);
   VkResult result;

   result = format_supported_with_usage(device_h, format, imageUsage);
   if (result != VK_SUCCESS)
      return result;

   *grallocUsage = 0;
   return setup_gralloc0_usage(device, format, imageUsage, grallocUsage);
}

#if ANDROID_API_LEVEL >= 26
VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetSwapchainGrallocUsage2ANDROID(
   VkDevice device_h,
   VkFormat format,
   VkImageUsageFlags imageUsage,
   VkSwapchainImageUsageFlagsANDROID swapchainImageUsage,
   uint64_t *grallocConsumerUsage,
   uint64_t *grallocProducerUsage)
{
   V3DV_FROM_HANDLE(v3dv_device, device, device_h);
   VkResult result;

   *grallocConsumerUsage = 0;
   *grallocProducerUsage = 0;
   mesa_logd("%s: format=%d, usage=0x%x", __func__, format, imageUsage);

   result = format_supported_with_usage(device_h, format, imageUsage);
   if (result != VK_SUCCESS)
      return result;

   int32_t grallocUsage = 0;
   result = setup_gralloc0_usage(device, format, imageUsage, &grallocUsage);
   if (result != VK_SUCCESS)
      return result;

   /* Setup gralloc1 usage flags from gralloc0 flags. */

   if (grallocUsage & GRALLOC_USAGE_HW_RENDER) {
      *grallocProducerUsage |= GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;
   }

   if (grallocUsage & GRALLOC_USAGE_HW_TEXTURE) {
      *grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
   }

   if (grallocUsage & GRALLOC_USAGE_HW_COMPOSER) {
      /* GPU composing case */
      *grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
      /* Hardware composing case */
      *grallocConsumerUsage |= GRALLOC1_CONSUMER_USAGE_HWCOMPOSER;
   }

   return VK_SUCCESS;
}
#endif
