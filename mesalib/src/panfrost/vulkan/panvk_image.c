/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "pan_props.h"

#include "panvk_device.h"
#include "panvk_device_memory.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"

#include "drm-uapi/drm_fourcc.h"
#include "util/u_atomic.h"
#include "util/u_debug.h"
#include "util/u_drm.h"

#include "vk_format.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vk_util.h"

#define PANVK_MAX_PLANES 1

static bool
panvk_image_can_use_mod(struct panvk_image *image, uint64_t mod)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(image->vk.base.device->physical);
   unsigned arch = pan_arch(phys_dev->kmod.props.gpu_prod_id);
   struct panvk_instance *instance =
      to_panvk_instance(image->vk.base.device->physical->instance);
   enum pipe_format pfmt = vk_format_to_pipe_format(image->vk.format);
   bool forced_linear = (instance->debug_flags & PANVK_DEBUG_LINEAR) ||
                        image->vk.tiling == VK_IMAGE_TILING_LINEAR ||
                        image->vk.image_type == VK_IMAGE_TYPE_1D;

   /* If the image is meant to be linear, don't bother testing the
    * other cases. */
   if (forced_linear)
      return mod == DRM_FORMAT_MOD_LINEAR;

   if (drm_is_afbc(mod)) {
      /* Disallow AFBC if either of these is true
       * - PANVK_DEBUG does not have the 'afbc' flag set
       * - storage image views are requested
       * - this is a multisample image
       * - the GPU doesn't support AFBC
       * - the format is not AFBC-able
       * - tiling is set to linear
       * - this is a 1D image
       * - this is a 3D image on a pre-v7 GPU
       */
      if (!(instance->debug_flags & PANVK_DEBUG_AFBC) ||
          ((image->vk.usage | image->vk.stencil_usage) &
           VK_IMAGE_USAGE_STORAGE_BIT) ||
          image->vk.samples > 1 ||
          !panfrost_query_afbc(&phys_dev->kmod.props) ||
          !panfrost_format_supports_afbc(arch, pfmt) ||
          image->vk.tiling == VK_IMAGE_TILING_LINEAR ||
          image->vk.image_type == VK_IMAGE_TYPE_1D ||
          (image->vk.image_type == VK_IMAGE_TYPE_3D && arch < 7))
         return false;

      const struct util_format_description *fdesc =
         util_format_description(pfmt);
      bool is_rgb = fdesc->colorspace == UTIL_FORMAT_COLORSPACE_RGB ||
                    fdesc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB;

      if ((mod & AFBC_FORMAT_MOD_YTR) && (!is_rgb || fdesc->nr_channels >= 3))
         return false;

      /* We assume all other unsupported AFBC modes have been filtered out
       * through pan_best_modifiers[]. */
      return true;
   }

   if (mod == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED) {
      /* If we're dealing with a compressed format that requires non-compressed
       * views we can't use U_INTERLEAVED tiling because the tiling is different
       * between compressed and non-compressed formats. If we wanted to support
       * format re-interpretation we would have to specialize the shaders
       * accessing non-compressed image views (coordinate patching for
       * sampled/storage image, frag_coord patching for color attachments). Let's
       * keep things simple for now and make all compressed images that
       * have VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT set linear. */
      return !(image->vk.create_flags &
               VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT);
   }

   /* If we get there, it must be linear to be supported. */
   return mod == DRM_FORMAT_MOD_LINEAR;
}

static void
panvk_image_apply_explicit_mod(
   struct panvk_image *image,
   const VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(image->vk.base.device->physical);
   unsigned arch = pan_arch(phys_dev->kmod.props.gpu_prod_id);
   uint64_t mod = explicit->drmFormatModifier;

   /* TODO: support arrays, 3D, multisample and depth-stencil. */
   struct pan_image_explicit_layout plane0_layout = {
      .offset = explicit->pPlaneLayouts[0].offset,
      .row_stride = explicit->pPlaneLayouts[0].rowPitch,
   };

   assert(!vk_format_is_depth_or_stencil(image->vk.format));
   assert(image->vk.samples == 1);
   assert(image->vk.array_layers == 1);
   assert(image->vk.image_type != VK_IMAGE_TYPE_3D);
   assert(explicit->drmFormatModifierPlaneCount == 1);
   assert(panvk_image_can_use_mod(image, mod));

   image->pimage.layout.modifier = mod;
   pan_image_layout_init(arch, &image->pimage.layout, &plane0_layout);
}

static void
panvk_image_select_mod_from_list(struct panvk_image *image,
                                 const uint64_t *mods, uint32_t mod_count)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(image->vk.base.device->physical);
   unsigned arch = pan_arch(phys_dev->kmod.props.gpu_prod_id);

   for (unsigned i = 0; i < PAN_MODIFIER_COUNT; ++i) {
      if (!panvk_image_can_use_mod(image, pan_best_modifiers[i]))
         continue;

      if (!mod_count ||
          drm_find_modifier(pan_best_modifiers[i], mods, mod_count)) {
         image->pimage.layout.modifier = pan_best_modifiers[i];
         pan_image_layout_init(arch, &image->pimage.layout, NULL);
         return;
      }
   }

   /* If we reached that point without finding a proper modifier, there's
    * a serious issue. */
   image->pimage.layout.modifier = DRM_FORMAT_MOD_INVALID;
   assert(!"Invalid modifier");
}

static void
panvk_image_select_mod(struct panvk_image *image,
                       const VkImageCreateInfo *pCreateInfo)
{
   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      const VkImageDrmFormatModifierListCreateInfoEXT *mod_list =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
      const VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit_mod =
         vk_find_struct_const(
            pCreateInfo->pNext,
            IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);

      if (explicit_mod)
         panvk_image_apply_explicit_mod(image, explicit_mod);
      else if (mod_list)
         panvk_image_select_mod_from_list(image, mod_list->pDrmFormatModifiers,
                                          mod_list->drmFormatModifierCount);
      else
         assert(!"Missing modifier info");

      return;
   }

   panvk_image_select_mod_from_list(image, NULL, 0);
}

static void
panvk_image_pre_mod_select_meta_adjustments(struct panvk_image *image)
{
   const VkImageAspectFlags aspects = vk_format_aspects(image->vk.format);

   /* We do image blit/resolve with vk_meta, so when an image is flagged as
    * being a potential transfer source, we also need to add the sampled usage.
    */
   if (image->vk.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
      if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
         image->vk.stencil_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
   }

   if (image->vk.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
      /* Similarly, image that can be a transfer destination can be attached
       * as a color or depth-stencil attachment by vk_meta. */
      if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
         image->vk.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

      if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
         image->vk.stencil_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

      if (aspects & VK_IMAGE_ASPECT_COLOR_BIT) {
         image->vk.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
         image->vk.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
      }

      /* vk_meta creates 2D array views of 3D images. */
      if (image->vk.image_type == VK_IMAGE_TYPE_3D)
         image->vk.create_flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
   }

   /* Needed for resolve operations. */
   if (image->vk.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

   if (image->vk.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT)
         image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

      if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT)
         image->vk.stencil_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
   }

   if ((image->vk.usage &
        (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) &&
       util_format_is_compressed(image->pimage.layout.format)) {
      /* We need to be able to create RGBA views of compressed formats for
       * vk_meta copies. */
      image->vk.create_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT |
                                VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT;
   }
}

static uint64_t
panvk_image_get_total_size(const struct panvk_image *image)
{
   assert(util_format_get_num_planes(image->pimage.layout.format) == 1);
   return image->pimage.layout.data_size;
}

static enum mali_texture_dimension
panvk_image_type_to_mali_tex_dim(VkImageType type)
{
   switch (type) {
   case VK_IMAGE_TYPE_1D:
      return MALI_TEXTURE_DIMENSION_1D;
   case VK_IMAGE_TYPE_2D:
      return MALI_TEXTURE_DIMENSION_2D;
   case VK_IMAGE_TYPE_3D:
      return MALI_TEXTURE_DIMENSION_3D;
   default:
      unreachable("Invalid image type");
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_CreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
   VK_FROM_HANDLE(panvk_device, dev, device);

   struct panvk_image *image =
      vk_image_create(&dev->vk, pCreateInfo, pAllocator, sizeof(*image));
   if (!image)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   image->pimage.layout = (struct pan_image_layout){
      .format = vk_format_to_pipe_format(image->vk.format),
      .dim = panvk_image_type_to_mali_tex_dim(image->vk.image_type),
      .width = image->vk.extent.width,
      .height = image->vk.extent.height,
      .depth = image->vk.extent.depth,
      .array_size = image->vk.array_layers,
      .nr_samples = image->vk.samples,
      .nr_slices = image->vk.mip_levels,
   };

   /* Add any create/usage flags that might be needed for meta operations.
    * This is run before the modifier selection because some
    * usage/create_flags influence the modifier selection logic. */
   panvk_image_pre_mod_select_meta_adjustments(image);

   /* Now that we've patched the create/usage flags, we can proceed with the
    * modifier selection. */
   panvk_image_select_mod(image, pCreateInfo);

   *pImage = panvk_image_to_handle(image);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_DestroyImage(VkDevice _device, VkImage _image,
                   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_image, image, _image);

   if (!image)
      return;

   if (image->bo)
      pan_kmod_bo_put(image->bo);

   vk_image_destroy(&device->vk, pAllocator, &image->vk);
}

static unsigned
panvk_plane_index(VkFormat format, VkImageAspectFlags aspect_mask)
{
   switch (aspect_mask) {
   default:
      return 0;
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
      return 1;
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
      return 2;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      return format == VK_FORMAT_D32_SFLOAT_S8_UINT;
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetImageSubresourceLayout(VkDevice _device, VkImage _image,
                                const VkImageSubresource *pSubresource,
                                VkSubresourceLayout *pLayout)
{
   VK_FROM_HANDLE(panvk_image, image, _image);

   unsigned plane =
      panvk_plane_index(image->vk.format, pSubresource->aspectMask);
   assert(plane < PANVK_MAX_PLANES);

   const struct pan_image_slice_layout *slice_layout =
      &image->pimage.layout.slices[pSubresource->mipLevel];

   pLayout->offset = slice_layout->offset + (pSubresource->arrayLayer *
                                             image->pimage.layout.array_stride);
   pLayout->size = slice_layout->size;
   pLayout->rowPitch = slice_layout->row_stride;
   pLayout->arrayPitch = image->pimage.layout.array_stride;
   pLayout->depthPitch = slice_layout->surface_stride;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetImageMemoryRequirements2(VkDevice device,
                                  const VkImageMemoryRequirementsInfo2 *pInfo,
                                  VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(panvk_image, image, pInfo->image);

   const uint64_t alignment = 4096;
   const uint64_t size = panvk_image_get_total_size(image);

   pMemoryRequirements->memoryRequirements.memoryTypeBits = 1;
   pMemoryRequirements->memoryRequirements.alignment = alignment;
   pMemoryRequirements->memoryRequirements.size = size;
}

VKAPI_ATTR void VKAPI_CALL
panvk_GetImageSparseMemoryRequirements2(
   VkDevice device, const VkImageSparseMemoryRequirementsInfo2 *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   /* Sparse images are not yet supported. */
   *pSparseMemoryRequirementCount = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_BindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                       const VkBindImageMemoryInfo *pBindInfos)
{
   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      VK_FROM_HANDLE(panvk_image, image, pBindInfos[i].image);
      VK_FROM_HANDLE(panvk_device_memory, mem, pBindInfos[i].memory);
      struct pan_kmod_bo *old_bo = image->bo;

      assert(mem);
      image->bo = pan_kmod_bo_get(mem->bo);
      image->pimage.data.base = mem->addr.dev;
      image->pimage.data.offset = pBindInfos[i].memoryOffset;
      /* Reset the AFBC headers */
      if (drm_is_afbc(image->pimage.layout.modifier)) {
         /* Transient CPU mapping */
         void *base = pan_kmod_bo_mmap(mem->bo, 0, pan_kmod_bo_size(mem->bo),
                                       PROT_WRITE, MAP_SHARED, NULL);

         assert(base != MAP_FAILED);

         for (unsigned layer = 0; layer < image->pimage.layout.array_size;
              layer++) {
            for (unsigned level = 0; level < image->pimage.layout.nr_slices;
                 level++) {
               void *header = base + image->pimage.data.offset +
                              (layer * image->pimage.layout.array_stride) +
                              image->pimage.layout.slices[level].offset;
               memset(header, 0,
                      image->pimage.layout.slices[level].afbc.header_size);
            }
         }

         ASSERTED int ret = os_munmap(base, pan_kmod_bo_size(mem->bo));
         assert(!ret);
      }

      pan_kmod_bo_put(old_bo);
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_GetImageDrmFormatModifierPropertiesEXT(
   VkDevice device, VkImage _image,
   VkImageDrmFormatModifierPropertiesEXT *pProperties)
{
   VK_FROM_HANDLE(panvk_image, image, _image);

   assert(pProperties->sType ==
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT);

   pProperties->drmFormatModifier = image->pimage.layout.modifier;
   return VK_SUCCESS;
}
