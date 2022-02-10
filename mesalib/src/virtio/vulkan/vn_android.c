/*
 * Copyright 2021 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_android.h"

#include <dlfcn.h>
#include <hardware/gralloc.h>
#include <hardware/hwvulkan.h>
#include <vndk/hardware_buffer.h>
#include <vulkan/vk_icd.h>

#include "drm-uapi/drm_fourcc.h"
#include "util/libsync.h"
#include "util/os_file.h"

#include "vn_buffer.h"
#include "vn_device.h"
#include "vn_device_memory.h"
#include "vn_image.h"
#include "vn_instance.h"
#include "vn_physical_device.h"
#include "vn_queue.h"

static int
vn_hal_open(const struct hw_module_t *mod,
            const char *id,
            struct hw_device_t **dev);

static void UNUSED
static_asserts(void)
{
   STATIC_ASSERT(HWVULKAN_DISPATCH_MAGIC == ICD_LOADER_MAGIC);
}

PUBLIC struct hwvulkan_module_t HAL_MODULE_INFO_SYM = {
   .common = {
      .tag = HARDWARE_MODULE_TAG,
      .module_api_version = HWVULKAN_MODULE_API_VERSION_0_1,
      .hal_api_version = HARDWARE_HAL_API_VERSION,
      .id = HWVULKAN_HARDWARE_MODULE_ID,
      .name = "Venus Vulkan HAL",
      .author = "Google LLC",
      .methods = &(hw_module_methods_t) {
         .open = vn_hal_open,
      },
   },
};

static const gralloc_module_t *gralloc = NULL;

static int
vn_hal_close(UNUSED struct hw_device_t *dev)
{
   dlclose(gralloc->common.dso);
   return 0;
}

static hwvulkan_device_t vn_hal_dev = {
  .common = {
     .tag = HARDWARE_DEVICE_TAG,
     .version = HWVULKAN_DEVICE_API_VERSION_0_1,
     .module = &HAL_MODULE_INFO_SYM.common,
     .close = vn_hal_close,
  },
 .EnumerateInstanceExtensionProperties = vn_EnumerateInstanceExtensionProperties,
 .CreateInstance = vn_CreateInstance,
 .GetInstanceProcAddr = vn_GetInstanceProcAddr,
};

static int
vn_hal_open(const struct hw_module_t *mod,
            const char *id,
            struct hw_device_t **dev)
{
   static const char CROS_GRALLOC_MODULE_NAME[] = "CrOS Gralloc";

   assert(mod == &HAL_MODULE_INFO_SYM.common);
   assert(strcmp(id, HWVULKAN_DEVICE_0) == 0);

   /* get gralloc module for gralloc buffer info query */
   int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                           (const hw_module_t **)&gralloc);
   if (ret) {
      if (VN_DEBUG(WSI))
         vn_log(NULL, "failed to open gralloc module(ret=%d)", ret);
      return ret;
   }

   if (VN_DEBUG(WSI))
      vn_log(NULL, "opened gralloc module name: %s", gralloc->common.name);

   if (strcmp(gralloc->common.name, CROS_GRALLOC_MODULE_NAME) != 0 ||
       !gralloc->perform) {
      dlclose(gralloc->common.dso);
      return -1;
   }

   *dev = &vn_hal_dev.common;

   return 0;
}

static uint32_t
vn_android_ahb_format_from_vk_format(VkFormat format)
{
   switch (format) {
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
      return 0;
   }
}

VkFormat
vn_android_drm_format_to_vk_format(uint32_t format)
{
   switch (format) {
   case DRM_FORMAT_ABGR8888:
   case DRM_FORMAT_XBGR8888:
      return VK_FORMAT_R8G8B8A8_UNORM;
   case DRM_FORMAT_BGR888:
      return VK_FORMAT_R8G8B8_UNORM;
   case DRM_FORMAT_RGB565:
      return VK_FORMAT_R5G6B5_UNORM_PACK16;
   case DRM_FORMAT_ABGR16161616F:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
   case DRM_FORMAT_ABGR2101010:
      return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
   case DRM_FORMAT_YVU420:
   case DRM_FORMAT_NV12:
      return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
   default:
      return VK_FORMAT_UNDEFINED;
   }
}

static bool
vn_android_drm_format_is_yuv(uint32_t format)
{
   assert(vn_android_drm_format_to_vk_format(format) != VK_FORMAT_UNDEFINED);

   switch (format) {
   case DRM_FORMAT_YVU420:
   case DRM_FORMAT_NV12:
      return true;
   default:
      return false;
   }
}

uint64_t
vn_android_get_ahb_usage(const VkImageUsageFlags usage,
                         const VkImageCreateFlags flags)
{
   uint64_t ahb_usage = 0;
   if (usage &
       (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   if (usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;

   if (flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_GPU_CUBE_MAP;

   if (flags & VK_IMAGE_CREATE_PROTECTED_BIT)
      ahb_usage |= AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT;

   /* must include at least one GPU usage flag */
   if (ahb_usage == 0)
      ahb_usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   return ahb_usage;
}

VkResult
vn_GetSwapchainGrallocUsage2ANDROID(
   VkDevice device,
   VkFormat format,
   VkImageUsageFlags imageUsage,
   VkSwapchainImageUsageFlagsANDROID swapchainImageUsage,
   uint64_t *grallocConsumerUsage,
   uint64_t *grallocProducerUsage)
{
   struct vn_device *dev = vn_device_from_handle(device);
   *grallocConsumerUsage = 0;
   *grallocProducerUsage = 0;

   if (swapchainImageUsage & VK_SWAPCHAIN_IMAGE_USAGE_SHARED_BIT_ANDROID)
      return vn_error(dev->instance, VK_ERROR_INITIALIZATION_FAILED);

   if (VN_DEBUG(WSI))
      vn_log(dev->instance, "format=%d, imageUsage=0x%x", format, imageUsage);

   if (imageUsage & (VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
      *grallocProducerUsage |= AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER;

   if (imageUsage &
       (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT))
      *grallocConsumerUsage |= AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

   return VK_SUCCESS;
}

struct cros_gralloc0_buffer_info {
   uint32_t drm_fourcc;
   int num_fds; /* ignored */
   int fds[4];  /* ignored */
   uint64_t modifier;
   uint32_t offset[4];
   uint32_t stride[4];
};

struct vn_android_gralloc_buffer_properties {
   uint32_t drm_fourcc;
   uint64_t modifier;
   uint32_t offset[4];
   uint32_t stride[4];
};

static VkResult
vn_android_get_dma_buf_from_native_handle(const native_handle_t *handle,
                                          int *out_dma_buf)
{
   /* There can be multiple fds wrapped inside a native_handle_t, but we
    * expect only the 1st one points to the dma_buf. For multi-planar format,
    * there should only exist one dma_buf as well. The other fd(s) may point
    * to shared memory used to store buffer metadata or other vendor specific
    * bits.
    */
   if (handle->numFds < 1) {
      vn_log(NULL, "handle->numFds is %d, expected >= 1", handle->numFds);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   if (handle->data[0] < 0) {
      vn_log(NULL, "handle->data[0] < 0");
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   *out_dma_buf = handle->data[0];
   return VK_SUCCESS;
}

static bool
vn_android_get_gralloc_buffer_properties(
   buffer_handle_t handle,
   struct vn_android_gralloc_buffer_properties *out_props)
{
   static const int32_t CROS_GRALLOC_DRM_GET_BUFFER_INFO = 4;
   struct cros_gralloc0_buffer_info info;
   if (gralloc->perform(gralloc, CROS_GRALLOC_DRM_GET_BUFFER_INFO, handle,
                        &info) != 0) {
      vn_log(NULL, "CROS_GRALLOC_DRM_GET_BUFFER_INFO failed");
      return false;
   }

   if (info.modifier == DRM_FORMAT_MOD_INVALID) {
      vn_log(NULL, "Unexpected DRM_FORMAT_MOD_INVALID");
      return false;
   }

   out_props->drm_fourcc = info.drm_fourcc;
   for (uint32_t i = 0; i < 4; i++) {
      out_props->stride[i] = info.stride[i];
      out_props->offset[i] = info.offset[i];
   }
   out_props->modifier = info.modifier;

   return true;
}

static VkResult
vn_android_get_modifier_properties(struct vn_device *dev,
                                   VkFormat format,
                                   uint64_t modifier,
                                   const VkAllocationCallbacks *alloc,
                                   VkDrmFormatModifierPropertiesEXT *out_props)
{
   VkPhysicalDevice physical_device =
      vn_physical_device_to_handle(dev->physical_device);
   VkDrmFormatModifierPropertiesListEXT mod_prop_list = {
      .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
      .pNext = NULL,
      .drmFormatModifierCount = 0,
      .pDrmFormatModifierProperties = NULL,
   };
   VkFormatProperties2 format_prop = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
      .pNext = &mod_prop_list,
   };
   VkDrmFormatModifierPropertiesEXT *mod_props = NULL;
   bool modifier_found = false;

   vn_GetPhysicalDeviceFormatProperties2(physical_device, format,
                                         &format_prop);

   if (!mod_prop_list.drmFormatModifierCount) {
      vn_log(dev->instance, "No compatible modifier for VkFormat(%u)",
             format);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   mod_props = vk_zalloc(
      alloc, sizeof(*mod_props) * mod_prop_list.drmFormatModifierCount,
      VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!mod_props)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   mod_prop_list.pDrmFormatModifierProperties = mod_props;
   vn_GetPhysicalDeviceFormatProperties2(physical_device, format,
                                         &format_prop);

   for (uint32_t i = 0; i < mod_prop_list.drmFormatModifierCount; i++) {
      if (mod_props[i].drmFormatModifier == modifier) {
         *out_props = mod_props[i];
         modifier_found = true;
         break;
      }
   }

   vk_free(alloc, mod_props);

   if (!modifier_found) {
      vn_log(dev->instance,
             "No matching modifier(%" PRIu64 ") properties for VkFormat(%u)",
             modifier, format);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   return VK_SUCCESS;
}

struct vn_android_image_builder {
   VkImageCreateInfo create;
   VkSubresourceLayout layouts[4];
   VkImageDrmFormatModifierExplicitCreateInfoEXT modifier;
   VkExternalMemoryImageCreateInfo external;
};

static VkResult
vn_android_get_image_builder(struct vn_device *dev,
                             const VkImageCreateInfo *create_info,
                             const native_handle_t *handle,
                             const VkAllocationCallbacks *alloc,
                             struct vn_android_image_builder *out_builder)
{
   VkResult result = VK_SUCCESS;
   struct vn_android_gralloc_buffer_properties buf_props;
   VkDrmFormatModifierPropertiesEXT mod_props;

   if (!vn_android_get_gralloc_buffer_properties(handle, &buf_props))
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   result = vn_android_get_modifier_properties(
      dev, create_info->format, buf_props.modifier, alloc, &mod_props);
   if (result != VK_SUCCESS)
      return result;

   memset(out_builder->layouts, 0, sizeof(out_builder->layouts));
   for (uint32_t i = 0; i < mod_props.drmFormatModifierPlaneCount; i++) {
      out_builder->layouts[i].offset = buf_props.offset[i];
      out_builder->layouts[i].rowPitch = buf_props.stride[i];
   }
   out_builder->modifier = (VkImageDrmFormatModifierExplicitCreateInfoEXT){
      .sType =
         VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
      .pNext = create_info->pNext,
      .drmFormatModifier = buf_props.modifier,
      .drmFormatModifierPlaneCount = mod_props.drmFormatModifierPlaneCount,
      .pPlaneLayouts = out_builder->layouts,
   };
   out_builder->external = (VkExternalMemoryImageCreateInfo){
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = &out_builder->modifier,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   out_builder->create = *create_info;
   out_builder->create.pNext = &out_builder->external;
   out_builder->create.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;

   return VK_SUCCESS;
}

VkResult
vn_android_image_from_anb(struct vn_device *dev,
                          const VkImageCreateInfo *create_info,
                          const VkNativeBufferANDROID *anb_info,
                          const VkAllocationCallbacks *alloc,
                          struct vn_image **out_img)
{
   /* If anb_info->handle points to a classic resouce created from
    * virtio_gpu_cmd_resource_create_3d, anb_info->stride is the stride of the
    * guest shadow storage other than the host gpu storage.
    *
    * We also need to pass the correct stride to vn_CreateImage, which will be
    * done via VkImageDrmFormatModifierExplicitCreateInfoEXT and will require
    * VK_EXT_image_drm_format_modifier support in the host driver. The struct
    * needs host storage info which can be queried from cros gralloc.
    */
   VkResult result = VK_SUCCESS;
   VkDevice device = vn_device_to_handle(dev);
   VkDeviceMemory memory = VK_NULL_HANDLE;
   VkImage image = VK_NULL_HANDLE;
   struct vn_image *img = NULL;
   uint64_t alloc_size = 0;
   uint32_t mem_type_bits = 0;
   int dma_buf_fd = -1;
   int dup_fd = -1;
   struct vn_android_image_builder builder;

   result = vn_android_get_dma_buf_from_native_handle(anb_info->handle,
                                                      &dma_buf_fd);
   if (result != VK_SUCCESS)
      goto fail;

   result = vn_android_get_image_builder(dev, create_info, anb_info->handle,
                                         alloc, &builder);
   if (result != VK_SUCCESS)
      goto fail;

   /* encoder will strip the Android specific pNext structs */
   result = vn_image_create(dev, &builder.create, alloc, &img);
   if (result != VK_SUCCESS) {
      if (VN_DEBUG(WSI))
         vn_log(dev->instance, "vn_image_create failed");
      goto fail;
   }

   image = vn_image_to_handle(img);
   VkMemoryRequirements mem_req;
   vn_GetImageMemoryRequirements(device, image, &mem_req);
   if (!mem_req.memoryTypeBits) {
      if (VN_DEBUG(WSI))
         vn_log(dev->instance, "mem_req.memoryTypeBits cannot be zero");
      result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
      goto fail;
   }

   result = vn_get_memory_dma_buf_properties(dev, dma_buf_fd, &alloc_size,
                                             &mem_type_bits);
   if (result != VK_SUCCESS)
      goto fail;

   if (VN_DEBUG(WSI)) {
      vn_log(dev->instance,
             "size = img(%" PRIu64 ") fd(%" PRIu64 "), "
             "memoryTypeBits = img(0x%X) & fd(0x%X)",
             mem_req.size, alloc_size, mem_req.memoryTypeBits, mem_type_bits);
   }

   if (alloc_size < mem_req.size) {
      if (VN_DEBUG(WSI)) {
         vn_log(dev->instance,
                "alloc_size(%" PRIu64 ") mem_req.size(%" PRIu64 ")",
                alloc_size, mem_req.size);
      }
      result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
      goto fail;
   }

   mem_type_bits &= mem_req.memoryTypeBits;
   if (!mem_type_bits) {
      result = VK_ERROR_INVALID_EXTERNAL_HANDLE;
      goto fail;
   }

   dup_fd = os_dupfd_cloexec(dma_buf_fd);
   if (dup_fd < 0) {
      result = (errno == EMFILE) ? VK_ERROR_TOO_MANY_OBJECTS
                                 : VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   const VkImportMemoryFdInfoKHR import_fd_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = NULL,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
      .fd = dup_fd,
   };
   const VkMemoryAllocateInfo memory_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &import_fd_info,
      .allocationSize = mem_req.size,
      .memoryTypeIndex = ffs(mem_type_bits) - 1,
   };
   result = vn_AllocateMemory(device, &memory_info, alloc, &memory);
   if (result != VK_SUCCESS) {
      /* only need to close the dup_fd on import failure */
      close(dup_fd);
      goto fail;
   }

   result = vn_BindImageMemory(device, image, memory, 0);
   if (result != VK_SUCCESS)
      goto fail;

   img->wsi.is_wsi = true;
   img->wsi.tiling_override = builder.create.tiling;
   img->wsi.drm_format_modifier = builder.modifier.drmFormatModifier;
   /* Android WSI image owns the memory */
   img->wsi.memory = vn_device_memory_from_handle(memory);
   img->wsi.memory_owned = true;
   *out_img = img;

   return VK_SUCCESS;

fail:
   if (image != VK_NULL_HANDLE)
      vn_DestroyImage(device, image, alloc);
   if (memory != VK_NULL_HANDLE)
      vn_FreeMemory(device, memory, alloc);
   return vn_error(dev->instance, result);
}

VkResult
vn_AcquireImageANDROID(VkDevice device,
                       UNUSED VkImage image,
                       int nativeFenceFd,
                       VkSemaphore semaphore,
                       VkFence fence)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   VkResult result = VK_SUCCESS;

   if (dev->instance->experimental.globalFencing == VK_FALSE) {
      /* Fallback when VkVenusExperimentalFeatures100000MESA::globalFencing is
       * VK_FALSE, out semaphore and fence are filled with already signaled
       * payloads, and the native fence fd is waited inside until signaled.
       */
      if (nativeFenceFd >= 0) {
         int ret = sync_wait(nativeFenceFd, -1);
         /* Android loader expects the ICD to always close the fd */
         close(nativeFenceFd);
         if (ret)
            return vn_error(dev->instance, VK_ERROR_SURFACE_LOST_KHR);
      }

      if (semaphore != VK_NULL_HANDLE)
         vn_semaphore_signal_wsi(dev, vn_semaphore_from_handle(semaphore));

      if (fence != VK_NULL_HANDLE)
         vn_fence_signal_wsi(dev, vn_fence_from_handle(fence));

      return VK_SUCCESS;
   }

   int semaphore_fd = -1;
   int fence_fd = -1;
   if (nativeFenceFd >= 0) {
      if (semaphore != VK_NULL_HANDLE && fence != VK_NULL_HANDLE) {
         semaphore_fd = nativeFenceFd;
         fence_fd = os_dupfd_cloexec(nativeFenceFd);
         if (fence_fd < 0) {
            result = (errno == EMFILE) ? VK_ERROR_TOO_MANY_OBJECTS
                                       : VK_ERROR_OUT_OF_HOST_MEMORY;
            close(nativeFenceFd);
            return vn_error(dev->instance, result);
         }
      } else if (semaphore != VK_NULL_HANDLE) {
         semaphore_fd = nativeFenceFd;
      } else if (fence != VK_NULL_HANDLE) {
         fence_fd = nativeFenceFd;
      } else {
         close(nativeFenceFd);
      }
   }

   if (semaphore != VK_NULL_HANDLE) {
      const VkImportSemaphoreFdInfoKHR info = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
         .pNext = NULL,
         .semaphore = semaphore,
         .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
         .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
         .fd = semaphore_fd,
      };
      result = vn_ImportSemaphoreFdKHR(device, &info);
      if (result == VK_SUCCESS)
         semaphore_fd = -1;
   }

   if (result == VK_SUCCESS && fence != VK_NULL_HANDLE) {
      const VkImportFenceFdInfoKHR info = {
         .sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR,
         .pNext = NULL,
         .fence = fence,
         .flags = VK_FENCE_IMPORT_TEMPORARY_BIT,
         .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT,
         .fd = fence_fd,
      };
      result = vn_ImportFenceFdKHR(device, &info);
      if (result == VK_SUCCESS)
         fence_fd = -1;
   }

   if (semaphore_fd >= 0)
      close(semaphore_fd);
   if (fence_fd >= 0)
      close(fence_fd);

   return vn_result(dev->instance, result);
}

VkResult
vn_QueueSignalReleaseImageANDROID(VkQueue queue,
                                  uint32_t waitSemaphoreCount,
                                  const VkSemaphore *pWaitSemaphores,
                                  VkImage image,
                                  int *pNativeFenceFd)
{
   VN_TRACE_FUNC();
   struct vn_queue *que = vn_queue_from_handle(queue);
   struct vn_device *dev = que->device;
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;
   VkDevice device = vn_device_to_handle(dev);
   VkPipelineStageFlags local_stage_masks[8];
   VkPipelineStageFlags *stage_masks = local_stage_masks;
   VkResult result = VK_SUCCESS;
   int fd = -1;

   if (waitSemaphoreCount == 0) {
      *pNativeFenceFd = -1;
      return VK_SUCCESS;
   }

   if (waitSemaphoreCount > ARRAY_SIZE(local_stage_masks)) {
      stage_masks =
         vk_alloc(alloc, sizeof(*stage_masks) * waitSemaphoreCount,
                  VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!stage_masks)
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   for (uint32_t i = 0; i < waitSemaphoreCount; i++)
      stage_masks[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

   const VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = NULL,
      .waitSemaphoreCount = waitSemaphoreCount,
      .pWaitSemaphores = pWaitSemaphores,
      .pWaitDstStageMask = stage_masks,
      .commandBufferCount = 0,
      .pCommandBuffers = NULL,
      .signalSemaphoreCount = 0,
      .pSignalSemaphores = NULL,
   };
   /* XXX When globalFencing is supported, our implementation is not able to
    * reset the fence during vn_GetFenceFdKHR currently. Thus to ensure proper
    * host driver behavior, we pass VK_NULL_HANDLE here.
    */
   result = vn_QueueSubmit(
      queue, 1, &submit_info,
      dev->instance->experimental.globalFencing == VK_TRUE ? VK_NULL_HANDLE
                                                           : que->wait_fence);

   if (stage_masks != local_stage_masks)
      vk_free(alloc, stage_masks);

   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   if (dev->instance->experimental.globalFencing == VK_TRUE) {
      /* XXX With globalFencing, the external queue fence was not passed in the
       * above vn_QueueSubmit to hint it to be synchronous. So we need to wait
       * for the ring here before vn_GetFenceFdKHR which is pure kernel ops.
       */
      vn_instance_ring_wait(dev->instance);

      const VkFenceGetFdInfoKHR fd_info = {
         .sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR,
         .pNext = NULL,
         .fence = que->wait_fence,
         .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT,
      };
      result = vn_GetFenceFdKHR(device, &fd_info, &fd);
   } else {
      result =
         vn_WaitForFences(device, 1, &que->wait_fence, VK_TRUE, UINT64_MAX);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);

      result = vn_ResetFences(device, 1, &que->wait_fence);
   }

   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   *pNativeFenceFd = fd;

   return VK_SUCCESS;
}

static VkResult
vn_android_get_ahb_format_properties(
   struct vn_device *dev,
   const struct AHardwareBuffer *ahb,
   VkAndroidHardwareBufferFormatPropertiesANDROID *out_props)
{
   AHardwareBuffer_Desc desc;
   VkFormat format;
   struct vn_android_gralloc_buffer_properties buf_props;
   VkDrmFormatModifierPropertiesEXT mod_props;

   AHardwareBuffer_describe(ahb, &desc);
   if (!(desc.usage & (AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                       AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER |
                       AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER))) {
      vn_log(dev->instance,
             "AHB usage(%" PRIu64 ") must include at least one GPU bit",
             desc.usage);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   /* Handle the special AHARDWAREBUFFER_FORMAT_BLOB for VkBuffer case. */
   if (desc.format == AHARDWAREBUFFER_FORMAT_BLOB) {
      out_props->format = VK_FORMAT_UNDEFINED;
      return VK_SUCCESS;
   }

   if (!vn_android_get_gralloc_buffer_properties(
          AHardwareBuffer_getNativeHandle(ahb), &buf_props))
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   /* We implement AHB extension support with EXT_image_drm_format_modifier.
    * It requires us to have a compatible VkFormat but not DRM formats. So if
    * the ahb is not intended for backing a VkBuffer, error out early if the
    * format is VK_FORMAT_UNDEFINED.
    */
   format = vn_android_drm_format_to_vk_format(buf_props.drm_fourcc);
   if (format == VK_FORMAT_UNDEFINED) {
      vn_log(dev->instance, "Unknown drm_fourcc(%u) from AHB format(0x%X)",
             buf_props.drm_fourcc, desc.format);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   VkResult result = vn_android_get_modifier_properties(
      dev, format, buf_props.modifier, &dev->base.base.alloc, &mod_props);
   if (result != VK_SUCCESS)
      return result;

   /* The spec requires that formatFeatures must include at least one of
    * VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT or
    * VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT.
    */
   const VkFormatFeatureFlags format_features =
      mod_props.drmFormatModifierTilingFeatures |
      VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;

   /* 11.2.7. Android Hardware Buffer External Memory
    *
    * Implementations may not always be able to determine the color model,
    * numerical range, or chroma offsets of the image contents, so the values
    * in VkAndroidHardwareBufferFormatPropertiesANDROID are only suggestions.
    * Applications should treat these values as sensible defaults to use in the
    * absence of more reliable information obtained through some other means.
    */
   const VkSamplerYcbcrModelConversion model =
      vn_android_drm_format_is_yuv(buf_props.drm_fourcc)
         ? VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601
         : VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
   *out_props = (VkAndroidHardwareBufferFormatPropertiesANDROID) {
      .sType = out_props->sType,
      .pNext = out_props->pNext,
      .format = format,
      .externalFormat = buf_props.drm_fourcc,
      .formatFeatures = format_features,
      .samplerYcbcrConversionComponents = {
         .r = VK_COMPONENT_SWIZZLE_IDENTITY,
         .g = VK_COMPONENT_SWIZZLE_IDENTITY,
         .b = VK_COMPONENT_SWIZZLE_IDENTITY,
         .a = VK_COMPONENT_SWIZZLE_IDENTITY,
      },
      .suggestedYcbcrModel = model,
      .suggestedYcbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL,
      .suggestedXChromaOffset = VK_CHROMA_LOCATION_MIDPOINT,
      .suggestedYChromaOffset = VK_CHROMA_LOCATION_MIDPOINT,
   };

   return VK_SUCCESS;
}

VkResult
vn_GetAndroidHardwareBufferPropertiesANDROID(
   VkDevice device,
   const struct AHardwareBuffer *buffer,
   VkAndroidHardwareBufferPropertiesANDROID *pProperties)
{
   struct vn_device *dev = vn_device_from_handle(device);
   VkResult result = VK_SUCCESS;
   int dma_buf_fd = -1;
   uint64_t alloc_size = 0;
   uint32_t mem_type_bits = 0;

   VkAndroidHardwareBufferFormatPropertiesANDROID *format_props =
      vk_find_struct(pProperties->pNext,
                     ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID);
   if (format_props) {
      result =
         vn_android_get_ahb_format_properties(dev, buffer, format_props);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   const native_handle_t *handle = AHardwareBuffer_getNativeHandle(buffer);
   result = vn_android_get_dma_buf_from_native_handle(handle, &dma_buf_fd);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   result = vn_get_memory_dma_buf_properties(dev, dma_buf_fd, &alloc_size,
                                             &mem_type_bits);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   pProperties->allocationSize = alloc_size;
   pProperties->memoryTypeBits = mem_type_bits;

   return VK_SUCCESS;
}

static AHardwareBuffer *
vn_android_ahb_allocate(uint32_t width,
                        uint32_t height,
                        uint32_t layers,
                        uint32_t format,
                        uint64_t usage)
{
   AHardwareBuffer *ahb = NULL;
   AHardwareBuffer_Desc desc;
   int ret = 0;

   memset(&desc, 0, sizeof(desc));
   desc.width = width;
   desc.height = height;
   desc.layers = layers;
   desc.format = format;
   desc.usage = usage;

   ret = AHardwareBuffer_allocate(&desc, &ahb);
   if (ret) {
      /* We just log the error code here for now since the platform falsely
       * maps all gralloc allocation failures to oom.
       */
      vn_log(NULL, "AHB alloc(w=%u,h=%u,l=%u,f=%u,u=%" PRIu64 ") failed(%d)",
             width, height, layers, format, usage, ret);
      return NULL;
   }

   return ahb;
}

bool
vn_android_get_drm_format_modifier_info(
   const VkPhysicalDeviceImageFormatInfo2 *format_info,
   VkPhysicalDeviceImageDrmFormatModifierInfoEXT *out_info)
{
   /* To properly fill VkPhysicalDeviceImageDrmFormatModifierInfoEXT, we have
    * to allocate an ahb to retrieve the drm format modifier. For the image
    * sharing mode, we assume VK_SHARING_MODE_EXCLUSIVE for now.
    */
   AHardwareBuffer *ahb = NULL;
   uint32_t format = 0;
   uint64_t usage = 0;
   struct vn_android_gralloc_buffer_properties buf_props;

   assert(format_info->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);

   format = vn_android_ahb_format_from_vk_format(format_info->format);
   if (!format)
      return false;

   usage = vn_android_get_ahb_usage(format_info->usage, format_info->flags);
   ahb = vn_android_ahb_allocate(16, 16, 1, format, usage);
   if (!ahb)
      return false;

   if (!vn_android_get_gralloc_buffer_properties(
          AHardwareBuffer_getNativeHandle(ahb), &buf_props)) {
      AHardwareBuffer_release(ahb);
      return false;
   }

   *out_info = (VkPhysicalDeviceImageDrmFormatModifierInfoEXT){
      .sType =
         VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
      .pNext = NULL,
      .drmFormatModifier = buf_props.modifier,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = NULL,
   };

   AHardwareBuffer_release(ahb);
   return true;
}

VkResult
vn_android_image_from_ahb(struct vn_device *dev,
                          const VkImageCreateInfo *create_info,
                          const VkAllocationCallbacks *alloc,
                          struct vn_image **out_img)
{
   const VkExternalFormatANDROID *ext_info =
      vk_find_struct_const(create_info->pNext, EXTERNAL_FORMAT_ANDROID);

   VkImageCreateInfo local_info;
   if (ext_info && ext_info->externalFormat) {
      assert(create_info->format == VK_FORMAT_UNDEFINED);
      assert(create_info->imageType == VK_IMAGE_TYPE_2D);
      assert(create_info->usage == VK_IMAGE_USAGE_SAMPLED_BIT);
      assert(create_info->tiling == VK_IMAGE_TILING_OPTIMAL);

      local_info = *create_info;
      local_info.format =
         vn_android_drm_format_to_vk_format(ext_info->externalFormat);
      create_info = &local_info;
   }

   return vn_image_create_deferred(dev, create_info, alloc, out_img);
}

VkResult
vn_android_device_import_ahb(struct vn_device *dev,
                             struct vn_device_memory *mem,
                             const VkMemoryAllocateInfo *alloc_info,
                             const VkAllocationCallbacks *alloc,
                             struct AHardwareBuffer *ahb,
                             bool internal_ahb)
{
   VkDevice device = vn_device_to_handle(dev);
   const VkMemoryDedicatedAllocateInfo *dedicated_info =
      vk_find_struct_const(alloc_info->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);
   const native_handle_t *handle = NULL;
   int dma_buf_fd = -1;
   int dup_fd = -1;
   uint64_t alloc_size = 0;
   uint32_t mem_type_bits = 0;
   uint32_t mem_type_index = alloc_info->memoryTypeIndex;
   bool force_unmappable = false;
   VkResult result = VK_SUCCESS;

   handle = AHardwareBuffer_getNativeHandle(ahb);
   result = vn_android_get_dma_buf_from_native_handle(handle, &dma_buf_fd);
   if (result != VK_SUCCESS)
      return result;

   result = vn_get_memory_dma_buf_properties(dev, dma_buf_fd, &alloc_size,
                                             &mem_type_bits);
   if (result != VK_SUCCESS)
      return result;

   /* If ahb is for an image, finish the deferred image creation first */
   if (dedicated_info && dedicated_info->image != VK_NULL_HANDLE) {
      struct vn_image *img = vn_image_from_handle(dedicated_info->image);
      struct vn_android_image_builder builder;

      result = vn_android_get_image_builder(dev, &img->deferred_info->create,
                                            handle, alloc, &builder);
      if (result != VK_SUCCESS)
         return result;

      result = vn_image_init_deferred(dev, &builder.create, img);
      if (result != VK_SUCCESS)
         return result;

      VkMemoryRequirements mem_req;
      vn_GetImageMemoryRequirements(device, dedicated_info->image, &mem_req);
      if (alloc_size < mem_req.size) {
         vn_log(dev->instance,
                "alloc_size(%" PRIu64 ") mem_req.size(%" PRIu64 ")",
                alloc_size, mem_req.size);
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;
      }

      alloc_size = mem_req.size;

      /* XXX Workaround before spec issue #2762 gets resolved. If importing an
       * internally allocated AHB from the exportable path, memoryTypeIndex is
       * undefined while defaulting to zero, which can be incompatible with
       * the queried memoryTypeBits from the combined memory requirement and
       * dma_buf fd properties. Thus we override the requested memoryTypeIndex
       * to an applicable one if existed.
       */
      if (internal_ahb) {
         if ((mem_type_bits & mem_req.memoryTypeBits) == 0) {
            vn_log(dev->instance, "memoryTypeBits: img(0x%X) fd(0x%X)",
                   mem_req.memoryTypeBits, mem_type_bits);
            return VK_ERROR_INVALID_EXTERNAL_HANDLE;
         }

         mem_type_index = ffs(mem_type_bits & mem_req.memoryTypeBits) - 1;
      }

      /* XXX Workaround before we use cross-domain backend in minigbm. The
       * blob_mem allocated from virgl backend can have a queried guest
       * mappable size smaller than the size returned from image memory
       * requirement.
       */
      force_unmappable = true;
   }

   if (dedicated_info && dedicated_info->buffer != VK_NULL_HANDLE) {
      VkMemoryRequirements mem_req;
      vn_GetBufferMemoryRequirements(device, dedicated_info->buffer,
                                     &mem_req);
      if (alloc_size < mem_req.size) {
         vn_log(dev->instance,
                "alloc_size(%" PRIu64 ") mem_req.size(%" PRIu64 ")",
                alloc_size, mem_req.size);
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;
      }

      alloc_size = mem_req.size;

      assert((1 << mem_type_index) & mem_req.memoryTypeBits);
   }

   assert((1 << mem_type_index) & mem_type_bits);

   errno = 0;
   dup_fd = os_dupfd_cloexec(dma_buf_fd);
   if (dup_fd < 0)
      return (errno == EMFILE) ? VK_ERROR_TOO_MANY_OBJECTS
                               : VK_ERROR_OUT_OF_HOST_MEMORY;

   /* Spec requires AHB export info to be present, so we must strip it. In
    * practice, the AHB import path here only needs the main allocation info
    * and the dedicated_info.
    */
   VkMemoryDedicatedAllocateInfo local_dedicated_info;
   /* Override when dedicated_info exists and is not the tail struct. */
   if (dedicated_info && dedicated_info->pNext) {
      local_dedicated_info = *dedicated_info;
      local_dedicated_info.pNext = NULL;
      dedicated_info = &local_dedicated_info;
   }
   const VkMemoryAllocateInfo local_alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = dedicated_info,
      .allocationSize = alloc_size,
      .memoryTypeIndex = mem_type_index,
   };
   result = vn_device_memory_import_dma_buf(dev, mem, &local_alloc_info,
                                            force_unmappable, dup_fd);
   if (result != VK_SUCCESS) {
      close(dup_fd);
      return result;
   }

   AHardwareBuffer_acquire(ahb);
   mem->ahb = ahb;

   return VK_SUCCESS;
}

VkResult
vn_android_device_allocate_ahb(struct vn_device *dev,
                               struct vn_device_memory *mem,
                               const VkMemoryAllocateInfo *alloc_info,
                               const VkAllocationCallbacks *alloc)
{
   const VkMemoryDedicatedAllocateInfo *dedicated_info =
      vk_find_struct_const(alloc_info->pNext, MEMORY_DEDICATED_ALLOCATE_INFO);
   uint32_t width = 0;
   uint32_t height = 1;
   uint32_t layers = 1;
   uint32_t format = 0;
   uint64_t usage = 0;
   struct AHardwareBuffer *ahb = NULL;

   if (dedicated_info && dedicated_info->image != VK_NULL_HANDLE) {
      const VkImageCreateInfo *image_info =
         &vn_image_from_handle(dedicated_info->image)->deferred_info->create;
      assert(image_info);
      width = image_info->extent.width;
      height = image_info->extent.height;
      layers = image_info->arrayLayers;
      format = vn_android_ahb_format_from_vk_format(image_info->format);
      usage = vn_android_get_ahb_usage(image_info->usage, image_info->flags);
   } else {
      const VkPhysicalDeviceMemoryProperties *mem_props =
         &dev->physical_device->memory_properties.memoryProperties;

      assert(alloc_info->memoryTypeIndex < mem_props->memoryTypeCount);

      width = alloc_info->allocationSize;
      format = AHARDWAREBUFFER_FORMAT_BLOB;
      usage = AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER;
      if (mem_props->memoryTypes[alloc_info->memoryTypeIndex].propertyFlags &
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
         usage |= AHARDWAREBUFFER_USAGE_CPU_READ_RARELY |
                  AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY;
      }
   }

   ahb = vn_android_ahb_allocate(width, height, layers, format, usage);
   if (!ahb)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   VkResult result =
      vn_android_device_import_ahb(dev, mem, alloc_info, alloc, ahb, true);

   /* ahb alloc has already acquired a ref and import will acquire another,
    * must release one here to avoid leak.
    */
   AHardwareBuffer_release(ahb);

   return result;
}

void
vn_android_release_ahb(struct AHardwareBuffer *ahb)
{
   AHardwareBuffer_release(ahb);
}

VkResult
vn_GetMemoryAndroidHardwareBufferANDROID(
   VkDevice device,
   const VkMemoryGetAndroidHardwareBufferInfoANDROID *pInfo,
   struct AHardwareBuffer **pBuffer)
{
   struct vn_device_memory *mem = vn_device_memory_from_handle(pInfo->memory);

   AHardwareBuffer_acquire(mem->ahb);
   *pBuffer = mem->ahb;

   return VK_SUCCESS;
}

struct vn_android_buffer_create_info {
   VkBufferCreateInfo create;
   VkExternalMemoryBufferCreateInfo external;
   VkBufferOpaqueCaptureAddressCreateInfo address;
};

static const VkBufferCreateInfo *
vn_android_fix_buffer_create_info(
   const VkBufferCreateInfo *create_info,
   struct vn_android_buffer_create_info *local_info)
{
   local_info->create = *create_info;
   VkBaseOutStructure *dst = (void *)&local_info->create;

   vk_foreach_struct_const(src, create_info->pNext) {
      void *pnext = NULL;
      switch (src->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO:
         memcpy(&local_info->external, src, sizeof(local_info->external));
         local_info->external.handleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         pnext = &local_info->external;
         break;
      case VK_STRUCTURE_TYPE_BUFFER_OPAQUE_CAPTURE_ADDRESS_CREATE_INFO:
         memcpy(&local_info->address, src, sizeof(local_info->address));
         pnext = &local_info->address;
         break;
      default:
         break;
      }

      if (pnext) {
         dst->pNext = pnext;
         dst = pnext;
      }
   }

   dst->pNext = NULL;

   return &local_info->create;
}

VkResult
vn_android_get_ahb_buffer_memory_type_bits(struct vn_device *dev,
                                           uint32_t *out_mem_type_bits)
{
   const uint32_t format = AHARDWAREBUFFER_FORMAT_BLOB;
   /* ensure dma_buf_memory_type_bits covers host visible usage */
   const uint64_t usage = AHARDWAREBUFFER_USAGE_GPU_DATA_BUFFER |
                          AHARDWAREBUFFER_USAGE_CPU_READ_RARELY |
                          AHARDWAREBUFFER_USAGE_CPU_WRITE_RARELY;
   AHardwareBuffer *ahb = NULL;
   int dma_buf_fd = -1;
   uint64_t alloc_size = 0;
   uint32_t mem_type_bits = 0;
   VkResult result;

   ahb = vn_android_ahb_allocate(4096, 1, 1, format, usage);
   if (!ahb)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   result = vn_android_get_dma_buf_from_native_handle(
      AHardwareBuffer_getNativeHandle(ahb), &dma_buf_fd);
   if (result != VK_SUCCESS) {
      AHardwareBuffer_release(ahb);
      return result;
   }

   result = vn_get_memory_dma_buf_properties(dev, dma_buf_fd, &alloc_size,
                                             &mem_type_bits);

   AHardwareBuffer_release(ahb);

   if (result != VK_SUCCESS)
      return result;

   *out_mem_type_bits = mem_type_bits;

   return VK_SUCCESS;
}

VkResult
vn_android_buffer_from_ahb(struct vn_device *dev,
                           const VkBufferCreateInfo *create_info,
                           const VkAllocationCallbacks *alloc,
                           struct vn_buffer **out_buf)
{
   struct vn_android_buffer_create_info local_info;
   VkResult result;

   create_info = vn_android_fix_buffer_create_info(create_info, &local_info);
   result = vn_buffer_create(dev, create_info, alloc, out_buf);
   if (result != VK_SUCCESS)
      return result;

   /* AHB backed buffer layers on top of dma_buf, so here we must comine the
    * queried type bits from both buffer memory requirement and dma_buf fd
    * properties.
    */
   (*out_buf)->requirements.memory.memoryRequirements.memoryTypeBits &=
      dev->buffer_cache.ahb_mem_type_bits;

   assert((*out_buf)->requirements.memory.memoryRequirements.memoryTypeBits);

   return VK_SUCCESS;
}
