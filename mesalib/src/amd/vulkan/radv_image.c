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

#include "ac_drm_fourcc.h"
#include "util/debug.h"
#include "util/u_atomic.h"
#include "vulkan/util/vk_format.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_radeon_winsys.h"
#include "sid.h"
#include "vk_format.h"
#include "vk_util.h"

#include "gfx10_format_table.h"

static const VkImageUsageFlagBits RADV_IMAGE_USAGE_WRITE_BITS =
   VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

static unsigned
radv_choose_tiling(struct radv_device *device, const VkImageCreateInfo *pCreateInfo,
                   VkFormat format)
{
   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR) {
      assert(pCreateInfo->samples <= 1);
      return RADEON_SURF_MODE_LINEAR_ALIGNED;
   }

   /* MSAA resources must be 2D tiled. */
   if (pCreateInfo->samples > 1)
      return RADEON_SURF_MODE_2D;

   if (!vk_format_is_compressed(format) && !vk_format_is_depth_or_stencil(format) &&
       device->physical_device->rad_info.chip_class <= GFX8) {
      /* this causes hangs in some VK CTS tests on GFX9. */
      /* Textures with a very small height are recommended to be linear. */
      if (pCreateInfo->imageType == VK_IMAGE_TYPE_1D ||
          /* Only very thin and long 2D textures should benefit from
           * linear_aligned. */
          (pCreateInfo->extent.width > 8 && pCreateInfo->extent.height <= 2))
         return RADEON_SURF_MODE_LINEAR_ALIGNED;
   }

   return RADEON_SURF_MODE_2D;
}

static bool
radv_use_tc_compat_htile_for_image(struct radv_device *device, const VkImageCreateInfo *pCreateInfo,
                                   VkFormat format)
{
   /* TC-compat HTILE is only available for GFX8+. */
   if (device->physical_device->rad_info.chip_class < GFX8)
      return false;

   if ((pCreateInfo->usage & VK_IMAGE_USAGE_STORAGE_BIT))
      return false;

   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR)
      return false;

   /* Do not enable TC-compatible HTILE if the image isn't readable by a
    * shader because no texture fetches will happen.
    */
   if (!(pCreateInfo->usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT)))
      return false;

   if (device->physical_device->rad_info.chip_class < GFX9) {
      /* TC-compat HTILE for MSAA depth/stencil images is broken
       * on GFX8 because the tiling doesn't match.
       */
      if (pCreateInfo->samples >= 2 && format == VK_FORMAT_D32_SFLOAT_S8_UINT)
         return false;

      /* GFX9+ supports compression for both 32-bit and 16-bit depth
       * surfaces, while GFX8 only supports 32-bit natively. Though,
       * the driver allows TC-compat HTILE for 16-bit depth surfaces
       * with no Z planes compression.
       */
      if (format != VK_FORMAT_D32_SFLOAT_S8_UINT && format != VK_FORMAT_D32_SFLOAT &&
          format != VK_FORMAT_D16_UNORM)
         return false;
   }

   return true;
}

static bool
radv_surface_has_scanout(struct radv_device *device, const struct radv_image_create_info *info)
{
   if (info->bo_metadata) {
      if (device->physical_device->rad_info.chip_class >= GFX9)
         return info->bo_metadata->u.gfx9.scanout;
      else
         return info->bo_metadata->u.legacy.scanout;
   }

   return info->scanout;
}

static bool
radv_image_use_fast_clear_for_image_early(const struct radv_device *device,
                                          const struct radv_image *image)
{
   if (device->instance->debug_flags & RADV_DEBUG_FORCE_COMPRESS)
      return true;

   if (image->info.samples <= 1 && image->info.width * image->info.height <= 512 * 512) {
      /* Do not enable CMASK or DCC for small surfaces where the cost
       * of the eliminate pass can be higher than the benefit of fast
       * clear. RadeonSI does this, but the image threshold is
       * different.
       */
      return false;
   }

   return !!(image->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
}

static bool
radv_image_use_fast_clear_for_image(const struct radv_device *device,
                                    const struct radv_image *image)
{
   if (device->instance->debug_flags & RADV_DEBUG_FORCE_COMPRESS)
      return true;

   return radv_image_use_fast_clear_for_image_early(device, image) &&
          (image->exclusive ||
           /* Enable DCC for concurrent images if stores are
            * supported because that means we can keep DCC compressed on
            * all layouts/queues.
            */
           radv_image_use_dcc_image_stores(device, image));
}

bool
radv_are_formats_dcc_compatible(const struct radv_physical_device *pdev, const void *pNext,
                                VkFormat format, VkImageCreateFlags flags, bool *sign_reinterpret)
{
   bool blendable;

   if (!radv_is_colorbuffer_format_supported(pdev, format, &blendable))
      return false;

   if (sign_reinterpret != NULL)
      *sign_reinterpret = false;

   if (flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
      const struct VkImageFormatListCreateInfo *format_list =
         (const struct VkImageFormatListCreateInfo *)vk_find_struct_const(
            pNext, IMAGE_FORMAT_LIST_CREATE_INFO);

      /* We have to ignore the existence of the list if viewFormatCount = 0 */
      if (format_list && format_list->viewFormatCount) {
         /* compatibility is transitive, so we only need to check
          * one format with everything else. */
         for (unsigned i = 0; i < format_list->viewFormatCount; ++i) {
            if (format_list->pViewFormats[i] == VK_FORMAT_UNDEFINED)
               continue;

            if (!radv_dcc_formats_compatible(format, format_list->pViewFormats[i],
                                             sign_reinterpret))
               return false;
         }
      } else {
         return false;
      }
   }

   return true;
}

static bool
radv_format_is_atomic_allowed(struct radv_device *device, VkFormat format)
{
   if (format == VK_FORMAT_R32_SFLOAT && !device->image_float32_atomics)
      return false;

   return radv_is_atomic_format_supported(format);
}

static bool
radv_formats_is_atomic_allowed(struct radv_device *device, const void *pNext, VkFormat format,
                               VkImageCreateFlags flags)
{
   if (radv_format_is_atomic_allowed(device, format))
      return true;

   if (flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
      const struct VkImageFormatListCreateInfo *format_list =
         (const struct VkImageFormatListCreateInfo *)vk_find_struct_const(
            pNext, IMAGE_FORMAT_LIST_CREATE_INFO);

      /* We have to ignore the existence of the list if viewFormatCount = 0 */
      if (format_list && format_list->viewFormatCount) {
         for (unsigned i = 0; i < format_list->viewFormatCount; ++i) {
            if (radv_format_is_atomic_allowed(device, format_list->pViewFormats[i]))
               return true;
         }
      }
   }

   return false;
}

static bool
radv_use_dcc_for_image_early(struct radv_device *device, struct radv_image *image,
                             const VkImageCreateInfo *pCreateInfo, VkFormat format,
                             bool *sign_reinterpret)
{
   /* DCC (Delta Color Compression) is only available for GFX8+. */
   if (device->physical_device->rad_info.chip_class < GFX8)
      return false;

   if (device->instance->debug_flags & RADV_DEBUG_NO_DCC)
      return false;

   if (image->shareable && image->tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
      return false;

   /*
    * TODO: Enable DCC for storage images on GFX9 and earlier.
    *
    * Also disable DCC with atomics because even when DCC stores are
    * supported atomics will always decompress. So if we are
    * decompressing a lot anyway we might as well not have DCC.
    */
   if ((pCreateInfo->usage & VK_IMAGE_USAGE_STORAGE_BIT) &&
       (device->physical_device->rad_info.chip_class < GFX10 ||
        radv_formats_is_atomic_allowed(device, pCreateInfo->pNext, format, pCreateInfo->flags)))
      return false;

   /* Do not enable DCC for fragment shading rate attachments. */
   if (pCreateInfo->usage & VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR)
      return false;

   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR)
      return false;

   if (vk_format_is_subsampled(format) || vk_format_get_plane_count(format) > 1)
      return false;

   if (!radv_image_use_fast_clear_for_image_early(device, image) &&
       image->tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
      return false;

   /* Do not enable DCC for mipmapped arrays because performance is worse. */
   if (pCreateInfo->arrayLayers > 1 && pCreateInfo->mipLevels > 1)
      return false;

   if (device->physical_device->rad_info.chip_class < GFX10) {
      /* TODO: Add support for DCC MSAA on GFX8-9. */
      if (pCreateInfo->samples > 1 && !device->physical_device->dcc_msaa_allowed)
         return false;

      /* TODO: Add support for DCC layers/mipmaps on GFX9. */
      if ((pCreateInfo->arrayLayers > 1 || pCreateInfo->mipLevels > 1) &&
          device->physical_device->rad_info.chip_class == GFX9)
         return false;
   }

   return radv_are_formats_dcc_compatible(device->physical_device, pCreateInfo->pNext, format,
                                          pCreateInfo->flags, sign_reinterpret);
}

static bool
radv_use_dcc_for_image_late(struct radv_device *device, struct radv_image *image)
{
   if (!radv_image_has_dcc(image))
      return false;

   if (image->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
      return true;

   if (!radv_image_use_fast_clear_for_image(device, image))
      return false;

   /* TODO: Fix storage images with DCC without DCC image stores.
    * Disabling it for now. */
   if ((image->usage & VK_IMAGE_USAGE_STORAGE_BIT) && !radv_image_use_dcc_image_stores(device, image))
      return false;

   return true;
}

/*
 * Whether to enable image stores with DCC compression for this image. If
 * this function returns false the image subresource should be decompressed
 * before using it with image stores.
 *
 * Note that this can have mixed performance implications, see
 * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/6796#note_643299
 *
 * This function assumes the image uses DCC compression.
 */
bool
radv_image_use_dcc_image_stores(const struct radv_device *device, const struct radv_image *image)
{
   return ac_surface_supports_dcc_image_stores(device->physical_device->rad_info.chip_class,
                                               &image->planes[0].surface);
}

/*
 * Whether to use a predicate to determine whether DCC is in a compressed
 * state. This can be used to avoid decompressing an image multiple times.
 */
bool
radv_image_use_dcc_predication(const struct radv_device *device, const struct radv_image *image)
{
   return radv_image_has_dcc(image) && !radv_image_use_dcc_image_stores(device, image);
}

static inline bool
radv_use_fmask_for_image(const struct radv_device *device, const struct radv_image *image)
{
   return image->info.samples > 1 && ((image->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
                                      (device->instance->debug_flags & RADV_DEBUG_FORCE_COMPRESS));
}

static inline bool
radv_use_htile_for_image(const struct radv_device *device, const struct radv_image *image)
{
   /* TODO:
    * - Investigate about mips+layers.
    * - Enable on other gens.
    */
   bool use_htile_for_mips =
      image->info.array_size == 1 && device->physical_device->rad_info.chip_class >= GFX10;

   /* Stencil texturing with HTILE doesn't work with mipmapping on Navi10-14. */
   if (device->physical_device->rad_info.chip_class == GFX10 &&
       image->vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT && image->info.levels > 1)
      return false;

   /* Do not enable HTILE for very small images because it seems less performant but make sure it's
    * allowed with VRS attachments because we need HTILE.
    */
   if (image->info.width * image->info.height < 8 * 8 &&
       !(device->instance->debug_flags & RADV_DEBUG_FORCE_COMPRESS) &&
       !device->attachment_vrs_enabled)
      return false;

   if (device->instance->disable_htile_layers && image->info.array_size > 1)
      return false;

   return (image->info.levels == 1 || use_htile_for_mips) && !image->shareable;
}

static bool
radv_use_tc_compat_cmask_for_image(struct radv_device *device, struct radv_image *image)
{
   /* TC-compat CMASK is only available for GFX8+. */
   if (device->physical_device->rad_info.chip_class < GFX8)
      return false;

   if (device->instance->debug_flags & RADV_DEBUG_NO_TC_COMPAT_CMASK)
      return false;

   /* TC-compat CMASK with storage images is supported on GFX10+. */
   if ((image->usage & VK_IMAGE_USAGE_STORAGE_BIT) &&
       device->physical_device->rad_info.chip_class < GFX10)
      return false;

   /* Do not enable TC-compatible if the image isn't readable by a shader
    * because no texture fetches will happen.
    */
   if (!(image->usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT)))
      return false;

   /* If the image doesn't have FMASK, it can't be fetchable. */
   if (!radv_image_has_fmask(image))
      return false;

   return true;
}

static uint32_t
si_get_bo_metadata_word1(const struct radv_device *device)
{
   return (ATI_VENDOR_ID << 16) | device->physical_device->rad_info.pci_id;
}

static bool
radv_is_valid_opaque_metadata(const struct radv_device *device, const struct radeon_bo_metadata *md)
{
   if (md->metadata[0] != 1 || md->metadata[1] != si_get_bo_metadata_word1(device))
      return false;

   if (md->size_metadata < 40)
      return false;

   return true;
}

static void
radv_patch_surface_from_metadata(struct radv_device *device, struct radeon_surf *surface,
                                 const struct radeon_bo_metadata *md)
{
   surface->flags = RADEON_SURF_CLR(surface->flags, MODE);

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      if (md->u.gfx9.swizzle_mode > 0)
         surface->flags |= RADEON_SURF_SET(RADEON_SURF_MODE_2D, MODE);
      else
         surface->flags |= RADEON_SURF_SET(RADEON_SURF_MODE_LINEAR_ALIGNED, MODE);

      surface->u.gfx9.swizzle_mode = md->u.gfx9.swizzle_mode;
   } else {
      surface->u.legacy.pipe_config = md->u.legacy.pipe_config;
      surface->u.legacy.bankw = md->u.legacy.bankw;
      surface->u.legacy.bankh = md->u.legacy.bankh;
      surface->u.legacy.tile_split = md->u.legacy.tile_split;
      surface->u.legacy.mtilea = md->u.legacy.mtilea;
      surface->u.legacy.num_banks = md->u.legacy.num_banks;

      if (md->u.legacy.macrotile == RADEON_LAYOUT_TILED)
         surface->flags |= RADEON_SURF_SET(RADEON_SURF_MODE_2D, MODE);
      else if (md->u.legacy.microtile == RADEON_LAYOUT_TILED)
         surface->flags |= RADEON_SURF_SET(RADEON_SURF_MODE_1D, MODE);
      else
         surface->flags |= RADEON_SURF_SET(RADEON_SURF_MODE_LINEAR_ALIGNED, MODE);
   }
}

static VkResult
radv_patch_image_dimensions(struct radv_device *device, struct radv_image *image,
                            const struct radv_image_create_info *create_info,
                            struct ac_surf_info *image_info)
{
   unsigned width = image->info.width;
   unsigned height = image->info.height;

   /*
    * minigbm sometimes allocates bigger images which is going to result in
    * weird strides and other properties. Lets be lenient where possible and
    * fail it on GFX10 (as we cannot cope there).
    *
    * Example hack: https://chromium-review.googlesource.com/c/chromiumos/platform/minigbm/+/1457777/
    */
   if (create_info->bo_metadata &&
       radv_is_valid_opaque_metadata(device, create_info->bo_metadata)) {
      const struct radeon_bo_metadata *md = create_info->bo_metadata;

      if (device->physical_device->rad_info.chip_class >= GFX10) {
         width = G_00A004_WIDTH_LO(md->metadata[3]) + (G_00A008_WIDTH_HI(md->metadata[4]) << 2) + 1;
         height = G_00A008_HEIGHT(md->metadata[4]) + 1;
      } else {
         width = G_008F18_WIDTH(md->metadata[4]) + 1;
         height = G_008F18_HEIGHT(md->metadata[4]) + 1;
      }
   }

   if (image->info.width == width && image->info.height == height)
      return VK_SUCCESS;

   if (width < image->info.width || height < image->info.height) {
      fprintf(stderr,
              "The imported image has smaller dimensions than the internal\n"
              "dimensions. Using it is going to fail badly, so we reject\n"
              "this import.\n"
              "(internal dimensions: %d x %d, external dimensions: %d x %d)\n",
              image->info.width, image->info.height, width, height);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   } else if (device->physical_device->rad_info.chip_class >= GFX10) {
      fprintf(stderr,
              "Tried to import an image with inconsistent width on GFX10.\n"
              "As GFX10 has no separate stride fields we cannot cope with\n"
              "an inconsistency in width and will fail this import.\n"
              "(internal dimensions: %d x %d, external dimensions: %d x %d)\n",
              image->info.width, image->info.height, width, height);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   } else {
      fprintf(stderr,
              "Tried to import an image with inconsistent width on pre-GFX10.\n"
              "As GFX10 has no separate stride fields we cannot cope with\n"
              "an inconsistency and would fail on GFX10.\n"
              "(internal dimensions: %d x %d, external dimensions: %d x %d)\n",
              image->info.width, image->info.height, width, height);
   }
   image_info->width = width;
   image_info->height = height;

   return VK_SUCCESS;
}

static VkResult
radv_patch_image_from_extra_info(struct radv_device *device, struct radv_image *image,
                                 const struct radv_image_create_info *create_info,
                                 struct ac_surf_info *image_info)
{
   VkResult result = radv_patch_image_dimensions(device, image, create_info, image_info);
   if (result != VK_SUCCESS)
      return result;

   for (unsigned plane = 0; plane < image->plane_count; ++plane) {
      if (create_info->bo_metadata) {
         radv_patch_surface_from_metadata(device, &image->planes[plane].surface,
                                          create_info->bo_metadata);
      }

      if (radv_surface_has_scanout(device, create_info)) {
         image->planes[plane].surface.flags |= RADEON_SURF_SCANOUT;
         if (device->instance->debug_flags & RADV_DEBUG_NO_DISPLAY_DCC)
            image->planes[plane].surface.flags |= RADEON_SURF_DISABLE_DCC;

         image->info.surf_index = NULL;
      }

      if (create_info->prime_blit_src && device->physical_device->rad_info.chip_class == GFX9) {
         /* Older SDMA hw can't handle DCC */
         image->planes[plane].surface.flags |= RADEON_SURF_DISABLE_DCC;
      }
   }
   return VK_SUCCESS;
}

static VkFormat
etc2_emulation_format(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
   case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
   case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
      return VK_FORMAT_R8G8B8A8_UNORM;
   case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
   case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
   case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
      return VK_FORMAT_R8G8B8A8_SRGB;
   case VK_FORMAT_EAC_R11_UNORM_BLOCK:
      return VK_FORMAT_R16_UNORM;
   case VK_FORMAT_EAC_R11_SNORM_BLOCK:
      return VK_FORMAT_R16_SNORM;
   case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
      return VK_FORMAT_R16G16_UNORM;
   case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
      return VK_FORMAT_R16G16_SNORM;
   default:
      unreachable("Unhandled ETC format");
   }
}

static VkFormat
radv_image_get_plane_format(const struct radv_physical_device *pdev, const struct radv_image *image,
                            unsigned plane)
{
   if (pdev->emulate_etc2 &&
       vk_format_description(image->vk_format)->layout == UTIL_FORMAT_LAYOUT_ETC) {
      if (plane == 0)
         return image->vk_format;
      return etc2_emulation_format(image->vk_format);
   }
   return vk_format_get_plane_format(image->vk_format, plane);
}

static uint64_t
radv_get_surface_flags(struct radv_device *device, struct radv_image *image, unsigned plane_id,
                       const VkImageCreateInfo *pCreateInfo, VkFormat image_format)
{
   uint64_t flags;
   unsigned array_mode = radv_choose_tiling(device, pCreateInfo, image_format);
   VkFormat format = radv_image_get_plane_format(device->physical_device, image, plane_id);
   const struct util_format_description *desc = vk_format_description(format);
   bool is_depth, is_stencil;

   is_depth = util_format_has_depth(desc);
   is_stencil = util_format_has_stencil(desc);

   flags = RADEON_SURF_SET(array_mode, MODE);

   switch (pCreateInfo->imageType) {
   case VK_IMAGE_TYPE_1D:
      if (pCreateInfo->arrayLayers > 1)
         flags |= RADEON_SURF_SET(RADEON_SURF_TYPE_1D_ARRAY, TYPE);
      else
         flags |= RADEON_SURF_SET(RADEON_SURF_TYPE_1D, TYPE);
      break;
   case VK_IMAGE_TYPE_2D:
      if (pCreateInfo->arrayLayers > 1)
         flags |= RADEON_SURF_SET(RADEON_SURF_TYPE_2D_ARRAY, TYPE);
      else
         flags |= RADEON_SURF_SET(RADEON_SURF_TYPE_2D, TYPE);
      break;
   case VK_IMAGE_TYPE_3D:
      flags |= RADEON_SURF_SET(RADEON_SURF_TYPE_3D, TYPE);
      break;
   default:
      unreachable("unhandled image type");
   }

   /* Required for clearing/initializing a specific layer on GFX8. */
   flags |= RADEON_SURF_CONTIGUOUS_DCC_LAYERS;

   if (is_depth) {
      flags |= RADEON_SURF_ZBUFFER;

      if (radv_use_htile_for_image(device, image) &&
          !(device->instance->debug_flags & RADV_DEBUG_NO_HIZ)) {
         if (radv_use_tc_compat_htile_for_image(device, pCreateInfo, image_format))
            flags |= RADEON_SURF_TC_COMPATIBLE_HTILE;
      } else {
         flags |= RADEON_SURF_NO_HTILE;
      }
   }

   if (is_stencil)
      flags |= RADEON_SURF_SBUFFER;

   if (device->physical_device->rad_info.chip_class >= GFX9 &&
       pCreateInfo->imageType == VK_IMAGE_TYPE_3D &&
       vk_format_get_blocksizebits(image_format) == 128 && vk_format_is_compressed(image_format))
      flags |= RADEON_SURF_NO_RENDER_TARGET;

   if (!radv_use_dcc_for_image_early(device, image, pCreateInfo, image_format,
                                     &image->dcc_sign_reinterpret))
      flags |= RADEON_SURF_DISABLE_DCC;

   if (!radv_use_fmask_for_image(device, image))
      flags |= RADEON_SURF_NO_FMASK;

   if (pCreateInfo->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) {
      flags |=
         RADEON_SURF_PRT | RADEON_SURF_NO_FMASK | RADEON_SURF_NO_HTILE | RADEON_SURF_DISABLE_DCC;
   }

   return flags;
}

static inline unsigned
si_tile_mode_index(const struct radv_image_plane *plane, unsigned level, bool stencil)
{
   if (stencil)
      return plane->surface.u.legacy.zs.stencil_tiling_index[level];
   else
      return plane->surface.u.legacy.tiling_index[level];
}

static unsigned
radv_map_swizzle(unsigned swizzle)
{
   switch (swizzle) {
   case PIPE_SWIZZLE_Y:
      return V_008F0C_SQ_SEL_Y;
   case PIPE_SWIZZLE_Z:
      return V_008F0C_SQ_SEL_Z;
   case PIPE_SWIZZLE_W:
      return V_008F0C_SQ_SEL_W;
   case PIPE_SWIZZLE_0:
      return V_008F0C_SQ_SEL_0;
   case PIPE_SWIZZLE_1:
      return V_008F0C_SQ_SEL_1;
   default: /* PIPE_SWIZZLE_X */
      return V_008F0C_SQ_SEL_X;
   }
}

static void
radv_compose_swizzle(const struct util_format_description *desc, const VkComponentMapping *mapping,
                     enum pipe_swizzle swizzle[4])
{
   if (desc->format == PIPE_FORMAT_R64_UINT || desc->format == PIPE_FORMAT_R64_SINT) {
      /* 64-bit formats only support storage images and storage images
       * require identity component mappings. We use 32-bit
       * instructions to access 64-bit images, so we need a special
       * case here.
       *
       * The zw components are 1,0 so that they can be easily be used
       * by loads to create the w component, which has to be 0 for
       * NULL descriptors.
       */
      swizzle[0] = PIPE_SWIZZLE_X;
      swizzle[1] = PIPE_SWIZZLE_Y;
      swizzle[2] = PIPE_SWIZZLE_1;
      swizzle[3] = PIPE_SWIZZLE_0;
   } else if (!mapping) {
      for (unsigned i = 0; i < 4; i++)
         swizzle[i] = desc->swizzle[i];
   } else if (desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS) {
      const unsigned char swizzle_xxxx[4] = {PIPE_SWIZZLE_X, PIPE_SWIZZLE_0, PIPE_SWIZZLE_0,
                                             PIPE_SWIZZLE_1};
      vk_format_compose_swizzles(mapping, swizzle_xxxx, swizzle);
   } else {
      vk_format_compose_swizzles(mapping, desc->swizzle, swizzle);
   }
}

static void
radv_make_buffer_descriptor(struct radv_device *device, struct radv_buffer *buffer,
                            VkFormat vk_format, unsigned offset, unsigned range, uint32_t *state)
{
   const struct util_format_description *desc;
   unsigned stride;
   uint64_t gpu_address = radv_buffer_get_va(buffer->bo);
   uint64_t va = gpu_address + buffer->offset;
   unsigned num_format, data_format;
   int first_non_void;
   enum pipe_swizzle swizzle[4];
   desc = vk_format_description(vk_format);
   first_non_void = vk_format_get_first_non_void_channel(vk_format);
   stride = desc->block.bits / 8;

   radv_compose_swizzle(desc, NULL, swizzle);

   va += offset;
   state[0] = va;
   state[1] = S_008F04_BASE_ADDRESS_HI(va >> 32) | S_008F04_STRIDE(stride);

   if (device->physical_device->rad_info.chip_class != GFX8 && stride) {
      range /= stride;
   }

   state[2] = range;
   state[3] = S_008F0C_DST_SEL_X(radv_map_swizzle(swizzle[0])) |
              S_008F0C_DST_SEL_Y(radv_map_swizzle(swizzle[1])) |
              S_008F0C_DST_SEL_Z(radv_map_swizzle(swizzle[2])) |
              S_008F0C_DST_SEL_W(radv_map_swizzle(swizzle[3]));

   if (device->physical_device->rad_info.chip_class >= GFX10) {
      const struct gfx10_format *fmt = &gfx10_format_table[vk_format_to_pipe_format(vk_format)];

      /* OOB_SELECT chooses the out-of-bounds check:
       *  - 0: (index >= NUM_RECORDS) || (offset >= STRIDE)
       *  - 1: index >= NUM_RECORDS
       *  - 2: NUM_RECORDS == 0
       *  - 3: if SWIZZLE_ENABLE == 0: offset >= NUM_RECORDS
       *       else: swizzle_address >= NUM_RECORDS
       */
      state[3] |= S_008F0C_FORMAT(fmt->img_format) |
                  S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_STRUCTURED_WITH_OFFSET) |
                  S_008F0C_RESOURCE_LEVEL(1);
   } else {
      num_format = radv_translate_buffer_numformat(desc, first_non_void);
      data_format = radv_translate_buffer_dataformat(desc, first_non_void);

      assert(data_format != V_008F0C_BUF_DATA_FORMAT_INVALID);
      assert(num_format != ~0);

      state[3] |= S_008F0C_NUM_FORMAT(num_format) | S_008F0C_DATA_FORMAT(data_format);
   }
}

static void
si_set_mutable_tex_desc_fields(struct radv_device *device, struct radv_image *image,
                               const struct legacy_surf_level *base_level_info, unsigned plane_id,
                               unsigned base_level, unsigned first_level, unsigned block_width,
                               bool is_stencil, bool is_storage_image, bool disable_compression,
                               bool enable_write_compression, uint32_t *state)
{
   struct radv_image_plane *plane = &image->planes[plane_id];
   uint64_t gpu_address = image->bo ? radv_buffer_get_va(image->bo) + image->offset : 0;
   uint64_t va = gpu_address;
   enum chip_class chip_class = device->physical_device->rad_info.chip_class;
   uint64_t meta_va = 0;
   if (chip_class >= GFX9) {
      if (is_stencil)
         va += plane->surface.u.gfx9.zs.stencil_offset;
      else
         va += plane->surface.u.gfx9.surf_offset;
   } else
      va += (uint64_t)base_level_info->offset_256B * 256;

   state[0] = va >> 8;
   if (chip_class >= GFX9 || base_level_info->mode == RADEON_SURF_MODE_2D)
      state[0] |= plane->surface.tile_swizzle;
   state[1] &= C_008F14_BASE_ADDRESS_HI;
   state[1] |= S_008F14_BASE_ADDRESS_HI(va >> 40);

   if (chip_class >= GFX8) {
      state[6] &= C_008F28_COMPRESSION_EN;
      state[7] = 0;
      if (!disable_compression && radv_dcc_enabled(image, first_level)) {
         meta_va = gpu_address + plane->surface.meta_offset;
         if (chip_class <= GFX8)
            meta_va += plane->surface.u.legacy.color.dcc_level[base_level].dcc_offset;

         unsigned dcc_tile_swizzle = plane->surface.tile_swizzle << 8;
         dcc_tile_swizzle &= (1 << plane->surface.meta_alignment_log2) - 1;
         meta_va |= dcc_tile_swizzle;
      } else if (!disable_compression && radv_image_is_tc_compat_htile(image)) {
         meta_va = gpu_address + plane->surface.meta_offset;
      }

      if (meta_va) {
         state[6] |= S_008F28_COMPRESSION_EN(1);
         if (chip_class <= GFX9)
            state[7] = meta_va >> 8;
      }
   }

   if (chip_class >= GFX10) {
      state[3] &= C_00A00C_SW_MODE;

      if (is_stencil) {
         state[3] |= S_00A00C_SW_MODE(plane->surface.u.gfx9.zs.stencil_swizzle_mode);
      } else {
         state[3] |= S_00A00C_SW_MODE(plane->surface.u.gfx9.swizzle_mode);
      }

      state[6] &= C_00A018_META_DATA_ADDRESS_LO & C_00A018_META_PIPE_ALIGNED;

      if (meta_va) {
         struct gfx9_surf_meta_flags meta = {
            .rb_aligned = 1,
            .pipe_aligned = 1,
         };

         if (!(plane->surface.flags & RADEON_SURF_Z_OR_SBUFFER))
            meta = plane->surface.u.gfx9.color.dcc;

         if (radv_dcc_enabled(image, first_level) && is_storage_image && enable_write_compression)
            state[6] |= S_00A018_WRITE_COMPRESS_ENABLE(1);

         state[6] |= S_00A018_META_PIPE_ALIGNED(meta.pipe_aligned) |
                     S_00A018_META_DATA_ADDRESS_LO(meta_va >> 8);
      }

      state[7] = meta_va >> 16;
   } else if (chip_class == GFX9) {
      state[3] &= C_008F1C_SW_MODE;
      state[4] &= C_008F20_PITCH;

      if (is_stencil) {
         state[3] |= S_008F1C_SW_MODE(plane->surface.u.gfx9.zs.stencil_swizzle_mode);
         state[4] |= S_008F20_PITCH(plane->surface.u.gfx9.zs.stencil_epitch);
      } else {
         state[3] |= S_008F1C_SW_MODE(plane->surface.u.gfx9.swizzle_mode);
         state[4] |= S_008F20_PITCH(plane->surface.u.gfx9.epitch);
      }

      state[5] &=
         C_008F24_META_DATA_ADDRESS & C_008F24_META_PIPE_ALIGNED & C_008F24_META_RB_ALIGNED;
      if (meta_va) {
         struct gfx9_surf_meta_flags meta = {
            .rb_aligned = 1,
            .pipe_aligned = 1,
         };

         if (!(plane->surface.flags & RADEON_SURF_Z_OR_SBUFFER))
            meta = plane->surface.u.gfx9.color.dcc;

         state[5] |= S_008F24_META_DATA_ADDRESS(meta_va >> 40) |
                     S_008F24_META_PIPE_ALIGNED(meta.pipe_aligned) |
                     S_008F24_META_RB_ALIGNED(meta.rb_aligned);
      }
   } else {
      /* GFX6-GFX8 */
      unsigned pitch = base_level_info->nblk_x * block_width;
      unsigned index = si_tile_mode_index(plane, base_level, is_stencil);

      state[3] &= C_008F1C_TILING_INDEX;
      state[3] |= S_008F1C_TILING_INDEX(index);
      state[4] &= C_008F20_PITCH;
      state[4] |= S_008F20_PITCH(pitch - 1);
   }
}

static unsigned
radv_tex_dim(VkImageType image_type, VkImageViewType view_type, unsigned nr_layers,
             unsigned nr_samples, bool is_storage_image, bool gfx9)
{
   if (view_type == VK_IMAGE_VIEW_TYPE_CUBE || view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
      return is_storage_image ? V_008F1C_SQ_RSRC_IMG_2D_ARRAY : V_008F1C_SQ_RSRC_IMG_CUBE;

   /* GFX9 allocates 1D textures as 2D. */
   if (gfx9 && image_type == VK_IMAGE_TYPE_1D)
      image_type = VK_IMAGE_TYPE_2D;
   switch (image_type) {
   case VK_IMAGE_TYPE_1D:
      return nr_layers > 1 ? V_008F1C_SQ_RSRC_IMG_1D_ARRAY : V_008F1C_SQ_RSRC_IMG_1D;
   case VK_IMAGE_TYPE_2D:
      if (nr_samples > 1)
         return nr_layers > 1 ? V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY : V_008F1C_SQ_RSRC_IMG_2D_MSAA;
      else
         return nr_layers > 1 ? V_008F1C_SQ_RSRC_IMG_2D_ARRAY : V_008F1C_SQ_RSRC_IMG_2D;
   case VK_IMAGE_TYPE_3D:
      if (view_type == VK_IMAGE_VIEW_TYPE_3D)
         return V_008F1C_SQ_RSRC_IMG_3D;
      else
         return V_008F1C_SQ_RSRC_IMG_2D_ARRAY;
   default:
      unreachable("illegal image type");
   }
}

static unsigned
gfx9_border_color_swizzle(const struct util_format_description *desc)
{
   unsigned bc_swizzle = V_008F20_BC_SWIZZLE_XYZW;

   if (desc->swizzle[3] == PIPE_SWIZZLE_X) {
      /* For the pre-defined border color values (white, opaque
       * black, transparent black), the only thing that matters is
       * that the alpha channel winds up in the correct place
       * (because the RGB channels are all the same) so either of
       * these enumerations will work.
       */
      if (desc->swizzle[2] == PIPE_SWIZZLE_Y)
         bc_swizzle = V_008F20_BC_SWIZZLE_WZYX;
      else
         bc_swizzle = V_008F20_BC_SWIZZLE_WXYZ;
   } else if (desc->swizzle[0] == PIPE_SWIZZLE_X) {
      if (desc->swizzle[1] == PIPE_SWIZZLE_Y)
         bc_swizzle = V_008F20_BC_SWIZZLE_XYZW;
      else
         bc_swizzle = V_008F20_BC_SWIZZLE_XWYZ;
   } else if (desc->swizzle[1] == PIPE_SWIZZLE_X) {
      bc_swizzle = V_008F20_BC_SWIZZLE_YXWZ;
   } else if (desc->swizzle[2] == PIPE_SWIZZLE_X) {
      bc_swizzle = V_008F20_BC_SWIZZLE_ZYXW;
   }

   return bc_swizzle;
}

bool
vi_alpha_is_on_msb(struct radv_device *device, VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);

   if (device->physical_device->rad_info.chip_class >= GFX10 && desc->nr_channels == 1)
      return desc->swizzle[3] == PIPE_SWIZZLE_X;

   return radv_translate_colorswap(format, false) <= 1;
}
/**
 * Build the sampler view descriptor for a texture (GFX10).
 */
static void
gfx10_make_texture_descriptor(struct radv_device *device, struct radv_image *image,
                              bool is_storage_image, VkImageViewType view_type, VkFormat vk_format,
                              const VkComponentMapping *mapping, unsigned first_level,
                              unsigned last_level, unsigned first_layer, unsigned last_layer,
                              unsigned width, unsigned height, unsigned depth, float min_lod,
                              uint32_t *state, uint32_t *fmask_state)
{
   const struct util_format_description *desc;
   enum pipe_swizzle swizzle[4];
   unsigned img_format;
   unsigned type;

   desc = vk_format_description(vk_format);

   /* For emulated ETC2 without alpha we need to override the format to a 3-componenent format, so
    * that border colors work correctly (alpha forced to 1). Since Vulkan has no such format,
    * this uses the Gallium formats to set the description. */
   if (image->vk_format == VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK &&
       vk_format == VK_FORMAT_R8G8B8A8_UNORM) {
      desc = util_format_description(PIPE_FORMAT_R8G8B8X8_UNORM);
   } else if (image->vk_format == VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK &&
              vk_format == VK_FORMAT_R8G8B8A8_SRGB) {
      desc = util_format_description(PIPE_FORMAT_R8G8B8X8_SRGB);
   }

   img_format = gfx10_format_table[vk_format_to_pipe_format(vk_format)].img_format;

   radv_compose_swizzle(desc, mapping, swizzle);

   type = radv_tex_dim(image->type, view_type, image->info.array_size, image->info.samples,
                       is_storage_image, device->physical_device->rad_info.chip_class == GFX9);
   if (type == V_008F1C_SQ_RSRC_IMG_1D_ARRAY) {
      height = 1;
      depth = image->info.array_size;
   } else if (type == V_008F1C_SQ_RSRC_IMG_2D_ARRAY || type == V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY) {
      if (view_type != VK_IMAGE_VIEW_TYPE_3D)
         depth = image->info.array_size;
   } else if (type == V_008F1C_SQ_RSRC_IMG_CUBE)
      depth = image->info.array_size / 6;

   state[0] = 0;
   state[1] = S_00A004_MIN_LOD(radv_float_to_ufixed(CLAMP(min_lod, 0, 15), 8)) |
              S_00A004_FORMAT(img_format) |
              S_00A004_WIDTH_LO(width - 1);
   state[2] = S_00A008_WIDTH_HI((width - 1) >> 2) | S_00A008_HEIGHT(height - 1) |
              S_00A008_RESOURCE_LEVEL(1);
   state[3] = S_00A00C_DST_SEL_X(radv_map_swizzle(swizzle[0])) |
              S_00A00C_DST_SEL_Y(radv_map_swizzle(swizzle[1])) |
              S_00A00C_DST_SEL_Z(radv_map_swizzle(swizzle[2])) |
              S_00A00C_DST_SEL_W(radv_map_swizzle(swizzle[3])) |
              S_00A00C_BASE_LEVEL(image->info.samples > 1 ? 0 : first_level) |
              S_00A00C_LAST_LEVEL(image->info.samples > 1 ? util_logbase2(image->info.samples)
                                                          : last_level) |
              S_00A00C_BC_SWIZZLE(gfx9_border_color_swizzle(desc)) | S_00A00C_TYPE(type);
   /* Depth is the the last accessible layer on gfx9+. The hw doesn't need
    * to know the total number of layers.
    */
   state[4] = S_00A010_DEPTH(type == V_008F1C_SQ_RSRC_IMG_3D ? depth - 1 : last_layer) |
              S_00A010_BASE_ARRAY(first_layer);
   state[5] = S_00A014_ARRAY_PITCH(0) |
              S_00A014_MAX_MIP(image->info.samples > 1 ? util_logbase2(image->info.samples)
                                                       : image->info.levels - 1) |
              S_00A014_PERF_MOD(4);
   state[6] = 0;
   state[7] = 0;

   if (radv_dcc_enabled(image, first_level)) {
      state[6] |= S_00A018_MAX_UNCOMPRESSED_BLOCK_SIZE(V_028C78_MAX_BLOCK_SIZE_256B) |
                  S_00A018_MAX_COMPRESSED_BLOCK_SIZE(
                     image->planes[0].surface.u.gfx9.color.dcc.max_compressed_block_size) |
                  S_00A018_ALPHA_IS_ON_MSB(vi_alpha_is_on_msb(device, vk_format));
   }

   if (radv_image_get_iterate256(device, image)) {
      state[6] |= S_00A018_ITERATE_256(1);
   }

   /* Initialize the sampler view for FMASK. */
   if (fmask_state) {
      if (radv_image_has_fmask(image)) {
         uint64_t gpu_address = radv_buffer_get_va(image->bo);
         uint32_t format;
         uint64_t va;

         assert(image->plane_count == 1);

         va = gpu_address + image->offset + image->planes[0].surface.fmask_offset;

         switch (image->info.samples) {
         case 2:
            format = V_008F0C_GFX10_FORMAT_FMASK8_S2_F2;
            break;
         case 4:
            format = V_008F0C_GFX10_FORMAT_FMASK8_S4_F4;
            break;
         case 8:
            format = V_008F0C_GFX10_FORMAT_FMASK32_S8_F8;
            break;
         default:
            unreachable("invalid nr_samples");
         }

         fmask_state[0] = (va >> 8) | image->planes[0].surface.fmask_tile_swizzle;
         fmask_state[1] = S_00A004_BASE_ADDRESS_HI(va >> 40) | S_00A004_FORMAT(format) |
                          S_00A004_WIDTH_LO(width - 1);
         fmask_state[2] = S_00A008_WIDTH_HI((width - 1) >> 2) | S_00A008_HEIGHT(height - 1) |
                          S_00A008_RESOURCE_LEVEL(1);
         fmask_state[3] =
            S_00A00C_DST_SEL_X(V_008F1C_SQ_SEL_X) | S_00A00C_DST_SEL_Y(V_008F1C_SQ_SEL_X) |
            S_00A00C_DST_SEL_Z(V_008F1C_SQ_SEL_X) | S_00A00C_DST_SEL_W(V_008F1C_SQ_SEL_X) |
            S_00A00C_SW_MODE(image->planes[0].surface.u.gfx9.color.fmask_swizzle_mode) |
            S_00A00C_TYPE(
               radv_tex_dim(image->type, view_type, image->info.array_size, 0, false, false));
         fmask_state[4] = S_00A010_DEPTH(last_layer) | S_00A010_BASE_ARRAY(first_layer);
         fmask_state[5] = 0;
         fmask_state[6] = S_00A018_META_PIPE_ALIGNED(1);
         fmask_state[7] = 0;

         if (radv_image_is_tc_compat_cmask(image)) {
            va = gpu_address + image->offset + image->planes[0].surface.cmask_offset;

            fmask_state[6] |= S_00A018_COMPRESSION_EN(1);
            fmask_state[6] |= S_00A018_META_DATA_ADDRESS_LO(va >> 8);
            fmask_state[7] |= va >> 16;
         }
      } else
         memset(fmask_state, 0, 8 * 4);
   }
}

/**
 * Build the sampler view descriptor for a texture (SI-GFX9)
 */
static void
si_make_texture_descriptor(struct radv_device *device, struct radv_image *image,
                           bool is_storage_image, VkImageViewType view_type, VkFormat vk_format,
                           const VkComponentMapping *mapping, unsigned first_level,
                           unsigned last_level, unsigned first_layer, unsigned last_layer,
                           unsigned width, unsigned height, unsigned depth, float min_lod,
                           uint32_t *state, uint32_t *fmask_state)
{
   const struct util_format_description *desc;
   enum pipe_swizzle swizzle[4];
   int first_non_void;
   unsigned num_format, data_format, type;

   desc = vk_format_description(vk_format);

   /* For emulated ETC2 without alpha we need to override the format to a 3-componenent format, so
    * that border colors work correctly (alpha forced to 1). Since Vulkan has no such format,
    * this uses the Gallium formats to set the description. */
   if (image->vk_format == VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK &&
       vk_format == VK_FORMAT_R8G8B8A8_UNORM) {
      desc = util_format_description(PIPE_FORMAT_R8G8B8X8_UNORM);
   } else if (image->vk_format == VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK &&
              vk_format == VK_FORMAT_R8G8B8A8_SRGB) {
      desc = util_format_description(PIPE_FORMAT_R8G8B8X8_SRGB);
   }

   radv_compose_swizzle(desc, mapping, swizzle);

   first_non_void = vk_format_get_first_non_void_channel(vk_format);

   num_format = radv_translate_tex_numformat(vk_format, desc, first_non_void);
   if (num_format == ~0) {
      num_format = 0;
   }

   data_format = radv_translate_tex_dataformat(vk_format, desc, first_non_void);
   if (data_format == ~0) {
      data_format = 0;
   }

   /* S8 with either Z16 or Z32 HTILE need a special format. */
   if (device->physical_device->rad_info.chip_class == GFX9 && vk_format == VK_FORMAT_S8_UINT &&
       radv_image_is_tc_compat_htile(image)) {
      if (image->vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT)
         data_format = V_008F14_IMG_DATA_FORMAT_S8_32;
      else if (image->vk_format == VK_FORMAT_D16_UNORM_S8_UINT)
         data_format = V_008F14_IMG_DATA_FORMAT_S8_16;
   }
   type = radv_tex_dim(image->type, view_type, image->info.array_size, image->info.samples,
                       is_storage_image, device->physical_device->rad_info.chip_class == GFX9);
   if (type == V_008F1C_SQ_RSRC_IMG_1D_ARRAY) {
      height = 1;
      depth = image->info.array_size;
   } else if (type == V_008F1C_SQ_RSRC_IMG_2D_ARRAY || type == V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY) {
      if (view_type != VK_IMAGE_VIEW_TYPE_3D)
         depth = image->info.array_size;
   } else if (type == V_008F1C_SQ_RSRC_IMG_CUBE)
      depth = image->info.array_size / 6;

   state[0] = 0;
   state[1] = (S_008F14_MIN_LOD(radv_float_to_ufixed(CLAMP(min_lod, 0, 15), 8)) |
               S_008F14_DATA_FORMAT(data_format) |
               S_008F14_NUM_FORMAT(num_format));
   state[2] = (S_008F18_WIDTH(width - 1) | S_008F18_HEIGHT(height - 1) | S_008F18_PERF_MOD(4));
   state[3] = (S_008F1C_DST_SEL_X(radv_map_swizzle(swizzle[0])) |
               S_008F1C_DST_SEL_Y(radv_map_swizzle(swizzle[1])) |
               S_008F1C_DST_SEL_Z(radv_map_swizzle(swizzle[2])) |
               S_008F1C_DST_SEL_W(radv_map_swizzle(swizzle[3])) |
               S_008F1C_BASE_LEVEL(image->info.samples > 1 ? 0 : first_level) |
               S_008F1C_LAST_LEVEL(image->info.samples > 1 ? util_logbase2(image->info.samples)
                                                           : last_level) |
               S_008F1C_TYPE(type));
   state[4] = 0;
   state[5] = S_008F24_BASE_ARRAY(first_layer);
   state[6] = 0;
   state[7] = 0;

   if (device->physical_device->rad_info.chip_class == GFX9) {
      unsigned bc_swizzle = gfx9_border_color_swizzle(desc);

      /* Depth is the last accessible layer on Gfx9.
       * The hw doesn't need to know the total number of layers.
       */
      if (type == V_008F1C_SQ_RSRC_IMG_3D)
         state[4] |= S_008F20_DEPTH(depth - 1);
      else
         state[4] |= S_008F20_DEPTH(last_layer);

      state[4] |= S_008F20_BC_SWIZZLE(bc_swizzle);
      state[5] |= S_008F24_MAX_MIP(image->info.samples > 1 ? util_logbase2(image->info.samples)
                                                           : image->info.levels - 1);
   } else {
      state[3] |= S_008F1C_POW2_PAD(image->info.levels > 1);
      state[4] |= S_008F20_DEPTH(depth - 1);
      state[5] |= S_008F24_LAST_ARRAY(last_layer);
   }
   if (!(image->planes[0].surface.flags & RADEON_SURF_Z_OR_SBUFFER) &&
       image->planes[0].surface.meta_offset) {
      state[6] = S_008F28_ALPHA_IS_ON_MSB(vi_alpha_is_on_msb(device, vk_format));
   } else {
      if (device->instance->disable_aniso_single_level) {
         /* The last dword is unused by hw. The shader uses it to clear
          * bits in the first dword of sampler state.
          */
         if (device->physical_device->rad_info.chip_class <= GFX7 && image->info.samples <= 1) {
            if (first_level == last_level)
               state[7] = C_008F30_MAX_ANISO_RATIO;
            else
               state[7] = 0xffffffff;
         }
      }
   }

   /* Initialize the sampler view for FMASK. */
   if (fmask_state) {
      if (radv_image_has_fmask(image)) {
         uint32_t fmask_format;
         uint64_t gpu_address = radv_buffer_get_va(image->bo);
         uint64_t va;

         assert(image->plane_count == 1);

         va = gpu_address + image->offset + image->planes[0].surface.fmask_offset;

         if (device->physical_device->rad_info.chip_class == GFX9) {
            fmask_format = V_008F14_IMG_DATA_FORMAT_FMASK;
            switch (image->info.samples) {
            case 2:
               num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_2_2;
               break;
            case 4:
               num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_4_4;
               break;
            case 8:
               num_format = V_008F14_IMG_NUM_FORMAT_FMASK_32_8_8;
               break;
            default:
               unreachable("invalid nr_samples");
            }
         } else {
            switch (image->info.samples) {
            case 2:
               fmask_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S2_F2;
               break;
            case 4:
               fmask_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F4;
               break;
            case 8:
               fmask_format = V_008F14_IMG_DATA_FORMAT_FMASK32_S8_F8;
               break;
            default:
               assert(0);
               fmask_format = V_008F14_IMG_DATA_FORMAT_INVALID;
            }
            num_format = V_008F14_IMG_NUM_FORMAT_UINT;
         }

         fmask_state[0] = va >> 8;
         fmask_state[0] |= image->planes[0].surface.fmask_tile_swizzle;
         fmask_state[1] = S_008F14_BASE_ADDRESS_HI(va >> 40) | S_008F14_DATA_FORMAT(fmask_format) |
                          S_008F14_NUM_FORMAT(num_format);
         fmask_state[2] = S_008F18_WIDTH(width - 1) | S_008F18_HEIGHT(height - 1);
         fmask_state[3] =
            S_008F1C_DST_SEL_X(V_008F1C_SQ_SEL_X) | S_008F1C_DST_SEL_Y(V_008F1C_SQ_SEL_X) |
            S_008F1C_DST_SEL_Z(V_008F1C_SQ_SEL_X) | S_008F1C_DST_SEL_W(V_008F1C_SQ_SEL_X) |
            S_008F1C_TYPE(
               radv_tex_dim(image->type, view_type, image->info.array_size, 0, false, false));
         fmask_state[4] = 0;
         fmask_state[5] = S_008F24_BASE_ARRAY(first_layer);
         fmask_state[6] = 0;
         fmask_state[7] = 0;

         if (device->physical_device->rad_info.chip_class == GFX9) {
            fmask_state[3] |= S_008F1C_SW_MODE(image->planes[0].surface.u.gfx9.color.fmask_swizzle_mode);
            fmask_state[4] |= S_008F20_DEPTH(last_layer) |
                              S_008F20_PITCH(image->planes[0].surface.u.gfx9.color.fmask_epitch);
            fmask_state[5] |= S_008F24_META_PIPE_ALIGNED(1) | S_008F24_META_RB_ALIGNED(1);

            if (radv_image_is_tc_compat_cmask(image)) {
               va = gpu_address + image->offset + image->planes[0].surface.cmask_offset;

               fmask_state[5] |= S_008F24_META_DATA_ADDRESS(va >> 40);
               fmask_state[6] |= S_008F28_COMPRESSION_EN(1);
               fmask_state[7] |= va >> 8;
            }
         } else {
            fmask_state[3] |=
               S_008F1C_TILING_INDEX(image->planes[0].surface.u.legacy.color.fmask.tiling_index);
            fmask_state[4] |=
               S_008F20_DEPTH(depth - 1) |
               S_008F20_PITCH(image->planes[0].surface.u.legacy.color.fmask.pitch_in_pixels - 1);
            fmask_state[5] |= S_008F24_LAST_ARRAY(last_layer);

            if (radv_image_is_tc_compat_cmask(image)) {
               va = gpu_address + image->offset + image->planes[0].surface.cmask_offset;

               fmask_state[6] |= S_008F28_COMPRESSION_EN(1);
               fmask_state[7] |= va >> 8;
            }
         }
      } else
         memset(fmask_state, 0, 8 * 4);
   }
}

static void
radv_make_texture_descriptor(struct radv_device *device, struct radv_image *image,
                             bool is_storage_image, VkImageViewType view_type, VkFormat vk_format,
                             const VkComponentMapping *mapping, unsigned first_level,
                             unsigned last_level, unsigned first_layer, unsigned last_layer,
                             unsigned width, unsigned height, unsigned depth, float min_lod, uint32_t *state,
                             uint32_t *fmask_state)
{
   if (device->physical_device->rad_info.chip_class >= GFX10) {
      gfx10_make_texture_descriptor(device, image, is_storage_image, view_type, vk_format, mapping,
                                    first_level, last_level, first_layer, last_layer, width, height,
                                    depth, min_lod, state, fmask_state);
   } else {
      si_make_texture_descriptor(device, image, is_storage_image, view_type, vk_format, mapping,
                                 first_level, last_level, first_layer, last_layer, width, height,
                                 depth, min_lod, state, fmask_state);
   }
}

static void
radv_query_opaque_metadata(struct radv_device *device, struct radv_image *image,
                           struct radeon_bo_metadata *md)
{
   static const VkComponentMapping fixedmapping;
   uint32_t desc[8];

   assert(image->plane_count == 1);

   radv_make_texture_descriptor(device, image, false, (VkImageViewType)image->type,
                                image->vk_format, &fixedmapping, 0, image->info.levels - 1, 0,
                                image->info.array_size - 1, image->info.width, image->info.height,
                                image->info.depth, 0.0f, desc, NULL);

   si_set_mutable_tex_desc_fields(device, image, &image->planes[0].surface.u.legacy.level[0], 0, 0,
                                  0, image->planes[0].surface.blk_w, false, false, false, false,
                                  desc);

   ac_surface_get_umd_metadata(&device->physical_device->rad_info, &image->planes[0].surface,
                               image->info.levels, desc, &md->size_metadata, md->metadata);
}

void
radv_init_metadata(struct radv_device *device, struct radv_image *image,
                   struct radeon_bo_metadata *metadata)
{
   struct radeon_surf *surface = &image->planes[0].surface;

   memset(metadata, 0, sizeof(*metadata));

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      uint64_t dcc_offset =
         image->offset +
         (surface->display_dcc_offset ? surface->display_dcc_offset : surface->meta_offset);
      metadata->u.gfx9.swizzle_mode = surface->u.gfx9.swizzle_mode;
      metadata->u.gfx9.dcc_offset_256b = dcc_offset >> 8;
      metadata->u.gfx9.dcc_pitch_max = surface->u.gfx9.color.display_dcc_pitch_max;
      metadata->u.gfx9.dcc_independent_64b_blocks = surface->u.gfx9.color.dcc.independent_64B_blocks;
      metadata->u.gfx9.dcc_independent_128b_blocks = surface->u.gfx9.color.dcc.independent_128B_blocks;
      metadata->u.gfx9.dcc_max_compressed_block_size =
         surface->u.gfx9.color.dcc.max_compressed_block_size;
      metadata->u.gfx9.scanout = (surface->flags & RADEON_SURF_SCANOUT) != 0;
   } else {
      metadata->u.legacy.microtile = surface->u.legacy.level[0].mode >= RADEON_SURF_MODE_1D
                                        ? RADEON_LAYOUT_TILED
                                        : RADEON_LAYOUT_LINEAR;
      metadata->u.legacy.macrotile = surface->u.legacy.level[0].mode >= RADEON_SURF_MODE_2D
                                        ? RADEON_LAYOUT_TILED
                                        : RADEON_LAYOUT_LINEAR;
      metadata->u.legacy.pipe_config = surface->u.legacy.pipe_config;
      metadata->u.legacy.bankw = surface->u.legacy.bankw;
      metadata->u.legacy.bankh = surface->u.legacy.bankh;
      metadata->u.legacy.tile_split = surface->u.legacy.tile_split;
      metadata->u.legacy.mtilea = surface->u.legacy.mtilea;
      metadata->u.legacy.num_banks = surface->u.legacy.num_banks;
      metadata->u.legacy.stride = surface->u.legacy.level[0].nblk_x * surface->bpe;
      metadata->u.legacy.scanout = (surface->flags & RADEON_SURF_SCANOUT) != 0;
   }
   radv_query_opaque_metadata(device, image, metadata);
}

void
radv_image_override_offset_stride(struct radv_device *device, struct radv_image *image,
                                  uint64_t offset, uint32_t stride)
{
   ac_surface_override_offset_stride(&device->physical_device->rad_info, &image->planes[0].surface,
                                     image->info.levels, offset, stride);
}

static void
radv_image_alloc_single_sample_cmask(const struct radv_device *device,
                                     const struct radv_image *image, struct radeon_surf *surf)
{
   if (!surf->cmask_size || surf->cmask_offset || surf->bpe > 8 || image->info.levels > 1 ||
       image->info.depth > 1 || radv_image_has_dcc(image) ||
       !radv_image_use_fast_clear_for_image(device, image) ||
       (image->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT))
      return;

   assert(image->info.storage_samples == 1);

   surf->cmask_offset = align64(surf->total_size, 1 << surf->cmask_alignment_log2);
   surf->total_size = surf->cmask_offset + surf->cmask_size;
   surf->alignment_log2 = MAX2(surf->alignment_log2, surf->cmask_alignment_log2);
}

static void
radv_image_alloc_values(const struct radv_device *device, struct radv_image *image)
{
   /* images with modifiers can be potentially imported */
   if (image->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
      return;

   if (radv_image_has_cmask(image) || (radv_image_has_dcc(image) && !image->support_comp_to_single)) {
      image->fce_pred_offset = image->size;
      image->size += 8 * image->info.levels;
   }

   if (radv_image_use_dcc_predication(device, image)) {
      image->dcc_pred_offset = image->size;
      image->size += 8 * image->info.levels;
   }

   if ((radv_image_has_dcc(image) && !image->support_comp_to_single) ||
       radv_image_has_cmask(image) || radv_image_has_htile(image)) {
      image->clear_value_offset = image->size;
      image->size += 8 * image->info.levels;
   }

   if (radv_image_is_tc_compat_htile(image) &&
       device->physical_device->rad_info.has_tc_compat_zrange_bug) {
      /* Metadata for the TC-compatible HTILE hardware bug which
       * have to be fixed by updating ZRANGE_PRECISION when doing
       * fast depth clears to 0.0f.
       */
      image->tc_compat_zrange_offset = image->size;
      image->size += image->info.levels * 4;
   }
}

/* Determine if the image is affected by the pipe misaligned metadata issue
 * which requires to invalidate L2.
 */
static bool
radv_image_is_pipe_misaligned(const struct radv_device *device, const struct radv_image *image)
{
   struct radeon_info *rad_info = &device->physical_device->rad_info;
   int log2_samples = util_logbase2(image->info.samples);

   assert(rad_info->chip_class >= GFX10);

   for (unsigned i = 0; i < image->plane_count; ++i) {
      VkFormat fmt = radv_image_get_plane_format(device->physical_device, image, i);
      int log2_bpp = util_logbase2(vk_format_get_blocksize(fmt));
      int log2_bpp_and_samples;

      if (rad_info->chip_class >= GFX10_3) {
         log2_bpp_and_samples = log2_bpp + log2_samples;
      } else {
         if (vk_format_has_depth(image->vk_format) && image->info.array_size >= 8) {
            log2_bpp = 2;
         }

         log2_bpp_and_samples = MIN2(6, log2_bpp + log2_samples);
      }

      int num_pipes = G_0098F8_NUM_PIPES(rad_info->gb_addr_config);
      int overlap = MAX2(0, log2_bpp_and_samples + num_pipes - 8);

      if (vk_format_has_depth(image->vk_format)) {
         if (radv_image_is_tc_compat_htile(image) && overlap) {
            return true;
         }
      } else {
         int max_compressed_frags = G_0098F8_MAX_COMPRESSED_FRAGS(rad_info->gb_addr_config);
         int log2_samples_frag_diff = MAX2(0, log2_samples - max_compressed_frags);
         int samples_overlap = MIN2(log2_samples, overlap);

         /* TODO: It shouldn't be necessary if the image has DCC but
          * not readable by shader.
          */
         if ((radv_image_has_dcc(image) || radv_image_is_tc_compat_cmask(image)) &&
             (samples_overlap > log2_samples_frag_diff)) {
            return true;
         }
      }
   }

   return false;
}

static bool
radv_image_is_l2_coherent(const struct radv_device *device, const struct radv_image *image)
{
   if (device->physical_device->rad_info.chip_class >= GFX10) {
      return !device->physical_device->rad_info.tcc_rb_non_coherent &&
             !radv_image_is_pipe_misaligned(device, image);
   } else if (device->physical_device->rad_info.chip_class == GFX9) {
      if (image->info.samples == 1 &&
          (image->usage &
           (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
          !vk_format_has_stencil(image->vk_format)) {
         /* Single-sample color and single-sample depth
          * (not stencil) are coherent with shaders on
          * GFX9.
          */
         return true;
      }
   }

   return false;
}

/**
 * Determine if the given image can be fast cleared.
 */
static bool
radv_image_can_fast_clear(const struct radv_device *device, const struct radv_image *image)
{
   if (device->instance->debug_flags & RADV_DEBUG_NO_FAST_CLEARS)
      return false;

   if (vk_format_is_color(image->vk_format)) {
      if (!radv_image_has_cmask(image) && !radv_image_has_dcc(image))
         return false;

      /* RB+ doesn't work with CMASK fast clear on Stoney. */
      if (!radv_image_has_dcc(image) && device->physical_device->rad_info.family == CHIP_STONEY)
         return false;
   } else {
      if (!radv_image_has_htile(image))
         return false;
   }

   /* Do not fast clears 3D images. */
   if (image->type == VK_IMAGE_TYPE_3D)
      return false;

   return true;
}

/**
 * Determine if the given image can be fast cleared using comp-to-single.
 */
static bool
radv_image_use_comp_to_single(const struct radv_device *device, const struct radv_image *image)
{
   /* comp-to-single is only available for GFX10+. */
   if (device->physical_device->rad_info.chip_class < GFX10)
      return false;

   /* If the image can't be fast cleared, comp-to-single can't be used. */
   if (!radv_image_can_fast_clear(device, image))
      return false;

   /* If the image doesn't have DCC, it can't be fast cleared using comp-to-single */
   if (!radv_image_has_dcc(image))
      return false;

   /* It seems 8bpp and 16bpp require RB+ to work. */
   unsigned bytes_per_pixel = vk_format_get_blocksize(image->vk_format);
   if (bytes_per_pixel <= 2 && !device->physical_device->rad_info.rbplus_allowed)
      return false;

   return true;
}

static unsigned
radv_get_internal_plane_count(const struct radv_physical_device *pdev, VkFormat fmt)
{
   if (pdev->emulate_etc2 && vk_format_description(fmt)->layout == UTIL_FORMAT_LAYOUT_ETC)
      return 2;
   return vk_format_get_plane_count(fmt);
}

static void
radv_image_reset_layout(const struct radv_physical_device *pdev, struct radv_image *image)
{
   image->size = 0;
   image->alignment = 1;

   image->tc_compatible_cmask = 0;
   image->fce_pred_offset = image->dcc_pred_offset = 0;
   image->clear_value_offset = image->tc_compat_zrange_offset = 0;

   unsigned plane_count = radv_get_internal_plane_count(pdev, image->vk_format);
   for (unsigned i = 0; i < plane_count; ++i) {
      VkFormat format = radv_image_get_plane_format(pdev, image, i);
      if (vk_format_has_depth(format))
         format = vk_format_depth_only(format);

      uint64_t flags = image->planes[i].surface.flags;
      uint64_t modifier = image->planes[i].surface.modifier;
      memset(image->planes + i, 0, sizeof(image->planes[i]));

      image->planes[i].surface.flags = flags;
      image->planes[i].surface.modifier = modifier;
      image->planes[i].surface.blk_w = vk_format_get_blockwidth(format);
      image->planes[i].surface.blk_h = vk_format_get_blockheight(format);
      image->planes[i].surface.bpe = vk_format_get_blocksize(format);

      /* align byte per element on dword */
      if (image->planes[i].surface.bpe == 3) {
         image->planes[i].surface.bpe = 4;
      }
   }
}

VkResult
radv_image_create_layout(struct radv_device *device, struct radv_image_create_info create_info,
                         const struct VkImageDrmFormatModifierExplicitCreateInfoEXT *mod_info,
                         struct radv_image *image)
{
   /* Clear the pCreateInfo pointer so we catch issues in the delayed case when we test in the
    * common internal case. */
   create_info.vk_info = NULL;

   struct ac_surf_info image_info = image->info;
   VkResult result = radv_patch_image_from_extra_info(device, image, &create_info, &image_info);
   if (result != VK_SUCCESS)
      return result;

   assert(!mod_info || mod_info->drmFormatModifierPlaneCount >= image->plane_count);

   radv_image_reset_layout(device->physical_device, image);

   unsigned plane_count = radv_get_internal_plane_count(device->physical_device, image->vk_format);
   for (unsigned plane = 0; plane < plane_count; ++plane) {
      struct ac_surf_info info = image_info;
      uint64_t offset;
      unsigned stride;

      info.width = vk_format_get_plane_width(image->vk_format, plane, info.width);
      info.height = vk_format_get_plane_height(image->vk_format, plane, info.height);

      if (create_info.no_metadata_planes || plane_count > 1) {
         image->planes[plane].surface.flags |=
            RADEON_SURF_DISABLE_DCC | RADEON_SURF_NO_FMASK | RADEON_SURF_NO_HTILE;
      }

      device->ws->surface_init(device->ws, &info, &image->planes[plane].surface);

      if (plane == 0) {
         if (!radv_use_dcc_for_image_late(device, image))
            ac_surface_zero_dcc_fields(&image->planes[0].surface);
      }

      if (create_info.bo_metadata && !mod_info &&
          !ac_surface_set_umd_metadata(&device->physical_device->rad_info,
                                       &image->planes[plane].surface, image_info.storage_samples,
                                       image_info.levels, create_info.bo_metadata->size_metadata,
                                       create_info.bo_metadata->metadata))
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;

      if (!create_info.no_metadata_planes && !create_info.bo_metadata && plane_count == 1 &&
          !mod_info)
         radv_image_alloc_single_sample_cmask(device, image, &image->planes[plane].surface);

      if (mod_info) {
         if (mod_info->pPlaneLayouts[plane].rowPitch % image->planes[plane].surface.bpe ||
             !mod_info->pPlaneLayouts[plane].rowPitch)
            return VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT;

         offset = mod_info->pPlaneLayouts[plane].offset;
         stride = mod_info->pPlaneLayouts[plane].rowPitch / image->planes[plane].surface.bpe;
      } else {
         offset = align64(image->size, 1 << image->planes[plane].surface.alignment_log2);
         stride = 0; /* 0 means no override */
      }

      if (!ac_surface_override_offset_stride(&device->physical_device->rad_info,
                                             &image->planes[plane].surface, image->info.levels,
                                             offset, stride))
         return VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT;

      /* Validate DCC offsets in modifier layout. */
      if (plane_count == 1 && mod_info) {
         unsigned mem_planes = ac_surface_get_nplanes(&image->planes[plane].surface);
         if (mod_info->drmFormatModifierPlaneCount != mem_planes)
            return VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT;

         for (unsigned i = 1; i < mem_planes; ++i) {
            if (ac_surface_get_plane_offset(device->physical_device->rad_info.chip_class,
                                            &image->planes[plane].surface, i,
                                            0) != mod_info->pPlaneLayouts[i].offset)
               return VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT;
         }
      }

      image->size = MAX2(image->size, offset + image->planes[plane].surface.total_size);
      image->alignment = MAX2(image->alignment, 1 << image->planes[plane].surface.alignment_log2);

      image->planes[plane].format =
         radv_image_get_plane_format(device->physical_device, image, plane);
   }

   image->tc_compatible_cmask =
      radv_image_has_cmask(image) && radv_use_tc_compat_cmask_for_image(device, image);

   image->l2_coherent = radv_image_is_l2_coherent(device, image);

   image->support_comp_to_single = radv_image_use_comp_to_single(device, image);

   radv_image_alloc_values(device, image);

   assert(image->planes[0].surface.surf_size);
   assert(image->planes[0].surface.modifier == DRM_FORMAT_MOD_INVALID ||
          ac_modifier_has_dcc(image->planes[0].surface.modifier) == radv_image_has_dcc(image));
   return VK_SUCCESS;
}

static void
radv_destroy_image(struct radv_device *device, const VkAllocationCallbacks *pAllocator,
                   struct radv_image *image)
{
   if ((image->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) && image->bo)
      device->ws->buffer_destroy(device->ws, image->bo);

   if (image->owned_memory != VK_NULL_HANDLE) {
      RADV_FROM_HANDLE(radv_device_memory, mem, image->owned_memory);
      radv_free_memory(device, pAllocator, mem);
   }

   vk_object_base_finish(&image->base);
   vk_free2(&device->vk.alloc, pAllocator, image);
}

static void
radv_image_print_info(struct radv_device *device, struct radv_image *image)
{
   fprintf(stderr, "Image:\n");
   fprintf(stderr,
           "  Info: size=%" PRIu64 ", alignment=%" PRIu32 ", "
           "width=%" PRIu32 ", height=%" PRIu32 ", "
           "offset=%" PRIu64 ", array_size=%" PRIu32 ", levels=%" PRIu32 "\n",
           image->size, image->alignment, image->info.width, image->info.height, image->offset,
           image->info.array_size, image->info.levels);
   for (unsigned i = 0; i < image->plane_count; ++i) {
      const struct radv_image_plane *plane = &image->planes[i];
      const struct radeon_surf *surf = &plane->surface;
      const struct util_format_description *desc = vk_format_description(plane->format);
      uint64_t offset = ac_surface_get_plane_offset(device->physical_device->rad_info.chip_class,
                                                    &plane->surface, 0, 0);

      fprintf(stderr, "  Plane[%u]: vkformat=%s, offset=%" PRIu64 "\n", i, desc->name, offset);

      ac_surface_print_info(stderr, &device->physical_device->rad_info, surf);
   }
}

static uint64_t
radv_select_modifier(const struct radv_device *dev, VkFormat format,
                     const struct VkImageDrmFormatModifierListCreateInfoEXT *mod_list)
{
   const struct radv_physical_device *pdev = dev->physical_device;
   unsigned mod_count;

   assert(mod_list->drmFormatModifierCount);

   /* We can allow everything here as it does not affect order and the application
    * is only allowed to specify modifiers that we support. */
   const struct ac_modifier_options modifier_options = {
      .dcc = true,
      .dcc_retile = true,
   };

   ac_get_supported_modifiers(&pdev->rad_info, &modifier_options, vk_format_to_pipe_format(format),
                              &mod_count, NULL);

   uint64_t *mods = calloc(mod_count, sizeof(*mods));

   /* If allocations fail, fall back to a dumber solution. */
   if (!mods)
      return mod_list->pDrmFormatModifiers[0];

   ac_get_supported_modifiers(&pdev->rad_info, &modifier_options, vk_format_to_pipe_format(format),
                              &mod_count, mods);

   for (unsigned i = 0; i < mod_count; ++i) {
      for (uint32_t j = 0; j < mod_list->drmFormatModifierCount; ++j) {
         if (mods[i] == mod_list->pDrmFormatModifiers[j]) {
            free(mods);
            return mod_list->pDrmFormatModifiers[j];
         }
      }
   }
   unreachable("App specified an invalid modifier");
}

VkResult
radv_image_create(VkDevice _device, const struct radv_image_create_info *create_info,
                  const VkAllocationCallbacks *alloc, VkImage *pImage)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   const VkImageCreateInfo *pCreateInfo = create_info->vk_info;
   uint64_t modifier = DRM_FORMAT_MOD_INVALID;
   struct radv_image *image = NULL;
   VkFormat format = radv_select_android_external_format(pCreateInfo->pNext, pCreateInfo->format);
   const struct VkImageDrmFormatModifierListCreateInfoEXT *mod_list =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
   const struct VkImageDrmFormatModifierExplicitCreateInfoEXT *explicit_mod =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);

   unsigned plane_count = radv_get_internal_plane_count(device->physical_device, format);

   const size_t image_struct_size = sizeof(*image) + sizeof(struct radv_image_plane) * plane_count;

   radv_assert(pCreateInfo->mipLevels > 0);
   radv_assert(pCreateInfo->arrayLayers > 0);
   radv_assert(pCreateInfo->samples > 0);
   radv_assert(pCreateInfo->extent.width > 0);
   radv_assert(pCreateInfo->extent.height > 0);
   radv_assert(pCreateInfo->extent.depth > 0);

   image =
      vk_zalloc2(&device->vk.alloc, alloc, image_struct_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &image->base, VK_OBJECT_TYPE_IMAGE);

   image->type = pCreateInfo->imageType;
   image->info.width = pCreateInfo->extent.width;
   image->info.height = pCreateInfo->extent.height;
   image->info.depth = pCreateInfo->extent.depth;
   image->info.samples = pCreateInfo->samples;
   image->info.storage_samples = pCreateInfo->samples;
   image->info.array_size = pCreateInfo->arrayLayers;
   image->info.levels = pCreateInfo->mipLevels;
   image->info.num_channels = vk_format_get_nr_components(format);

   image->vk_format = format;
   image->tiling = pCreateInfo->tiling;
   image->usage = pCreateInfo->usage;
   image->flags = pCreateInfo->flags;
   image->plane_count = vk_format_get_plane_count(format);

   image->exclusive = pCreateInfo->sharingMode == VK_SHARING_MODE_EXCLUSIVE;
   if (pCreateInfo->sharingMode == VK_SHARING_MODE_CONCURRENT) {
      for (uint32_t i = 0; i < pCreateInfo->queueFamilyIndexCount; ++i)
         if (pCreateInfo->pQueueFamilyIndices[i] == VK_QUEUE_FAMILY_EXTERNAL ||
             pCreateInfo->pQueueFamilyIndices[i] == VK_QUEUE_FAMILY_FOREIGN_EXT)
            image->queue_family_mask |= (1u << RADV_MAX_QUEUE_FAMILIES) - 1u;
         else
            image->queue_family_mask |= 1u << pCreateInfo->pQueueFamilyIndices[i];
   }

   const VkExternalMemoryImageCreateInfo *external_info =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_MEMORY_IMAGE_CREATE_INFO);

   image->shareable = external_info;
   if (!vk_format_is_depth_or_stencil(format) && !image->shareable &&
       !(image->flags & VK_IMAGE_CREATE_SPARSE_ALIASED_BIT) &&
       pCreateInfo->tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      image->info.surf_index = &device->image_mrt_offset_counter;
   }

   if (mod_list)
      modifier = radv_select_modifier(device, format, mod_list);
   else if (explicit_mod)
      modifier = explicit_mod->drmFormatModifier;

   for (unsigned plane = 0; plane < plane_count; ++plane) {
      image->planes[plane].surface.flags =
         radv_get_surface_flags(device, image, plane, pCreateInfo, format);
      image->planes[plane].surface.modifier = modifier;
   }

   bool delay_layout =
      external_info && (external_info->handleTypes &
                        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID);

   if (delay_layout) {
      *pImage = radv_image_to_handle(image);
      assert(!(image->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT));
      return VK_SUCCESS;
   }

   VkResult result = radv_image_create_layout(device, *create_info, explicit_mod, image);
   if (result != VK_SUCCESS) {
      radv_destroy_image(device, alloc, image);
      return result;
   }

   if (image->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) {
      image->alignment = MAX2(image->alignment, 4096);
      image->size = align64(image->size, image->alignment);
      image->offset = 0;

      result =
         device->ws->buffer_create(device->ws, image->size, image->alignment, 0,
                                   RADEON_FLAG_VIRTUAL, RADV_BO_PRIORITY_VIRTUAL, 0, &image->bo);
      if (result != VK_SUCCESS) {
         radv_destroy_image(device, alloc, image);
         return vk_error(device, result);
      }
   }

   if (device->instance->debug_flags & RADV_DEBUG_IMG) {
      radv_image_print_info(device, image);
   }

   *pImage = radv_image_to_handle(image);

   return VK_SUCCESS;
}

static void
radv_image_view_make_descriptor(struct radv_image_view *iview, struct radv_device *device,
                                VkFormat vk_format, const VkComponentMapping *components,
                                float min_lod,
                                bool is_storage_image, bool disable_compression,
                                bool enable_compression, unsigned plane_id,
                                unsigned descriptor_plane_id)
{
   struct radv_image *image = iview->image;
   struct radv_image_plane *plane = &image->planes[plane_id];
   bool is_stencil = iview->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT;
   uint32_t blk_w;
   union radv_descriptor *descriptor;
   uint32_t hw_level = 0;

   if (is_storage_image) {
      descriptor = &iview->storage_descriptor;
   } else {
      descriptor = &iview->descriptor;
   }

   assert(vk_format_get_plane_count(vk_format) == 1);
   assert(plane->surface.blk_w % vk_format_get_blockwidth(plane->format) == 0);
   blk_w = plane->surface.blk_w / vk_format_get_blockwidth(plane->format) *
           vk_format_get_blockwidth(vk_format);

   if (device->physical_device->rad_info.chip_class >= GFX9)
      hw_level = iview->base_mip;
   radv_make_texture_descriptor(
      device, image, is_storage_image, iview->type, vk_format, components, hw_level,
      hw_level + iview->level_count - 1, iview->base_layer,
      iview->base_layer + iview->layer_count - 1,
      vk_format_get_plane_width(image->vk_format, plane_id, iview->extent.width),
      vk_format_get_plane_height(image->vk_format, plane_id, iview->extent.height),
      iview->extent.depth, min_lod, descriptor->plane_descriptors[descriptor_plane_id],
      descriptor_plane_id || is_storage_image ? NULL : descriptor->fmask_descriptor);

   const struct legacy_surf_level *base_level_info = NULL;
   if (device->physical_device->rad_info.chip_class <= GFX9) {
      if (is_stencil)
         base_level_info = &plane->surface.u.legacy.zs.stencil_level[iview->base_mip];
      else
         base_level_info = &plane->surface.u.legacy.level[iview->base_mip];
   }

   bool enable_write_compression = radv_image_use_dcc_image_stores(device, image);
   if (is_storage_image && !(enable_write_compression || enable_compression))
      disable_compression = true;
   si_set_mutable_tex_desc_fields(device, image, base_level_info, plane_id, iview->base_mip,
                                  iview->base_mip, blk_w, is_stencil, is_storage_image,
                                  disable_compression, enable_write_compression,
                                  descriptor->plane_descriptors[descriptor_plane_id]);
}

static unsigned
radv_plane_from_aspect(VkImageAspectFlags mask)
{
   switch (mask) {
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT:
      return 1;
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT:
      return 2;
   case VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT:
      return 3;
   default:
      return 0;
   }
}

VkFormat
radv_get_aspect_format(struct radv_image *image, VkImageAspectFlags mask)
{
   switch (mask) {
   case VK_IMAGE_ASPECT_PLANE_0_BIT:
      return image->planes[0].format;
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
      return image->planes[1].format;
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
      return image->planes[2].format;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      return vk_format_stencil_only(image->vk_format);
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      return vk_format_depth_only(image->vk_format);
   case VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT:
      return vk_format_depth_only(image->vk_format);
   default:
      return image->vk_format;
   }
}

/**
 * Determine if the given image view can be fast cleared.
 */
static bool
radv_image_view_can_fast_clear(const struct radv_device *device,
                               const struct radv_image_view *iview)
{
   struct radv_image *image;

   if (!iview)
      return false;
   image = iview->image;

   /* Only fast clear if the image itself can be fast cleared. */
   if (!radv_image_can_fast_clear(device, image))
      return false;

   /* Only fast clear if all layers are bound. */
   if (iview->base_layer > 0 || iview->layer_count != image->info.array_size)
      return false;

   /* Only fast clear if the view covers the whole image. */
   if (!radv_image_extent_compare(image, &iview->extent))
      return false;

   return true;
}

void
radv_image_view_init(struct radv_image_view *iview, struct radv_device *device,
                     const VkImageViewCreateInfo *pCreateInfo,
                     const struct radv_image_view_extra_create_info *extra_create_info)
{
   RADV_FROM_HANDLE(radv_image, image, pCreateInfo->image);
   const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;
   uint32_t plane_count = 1;
   float min_lod = 0.0f;

   const struct VkImageViewMinLodCreateInfoEXT *min_lod_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_VIEW_MIN_LOD_CREATE_INFO_EXT);

   if (min_lod_info)
      min_lod = min_lod_info->minLod;

   vk_object_base_init(&device->vk, &iview->base, VK_OBJECT_TYPE_IMAGE_VIEW);

   switch (image->type) {
   case VK_IMAGE_TYPE_1D:
   case VK_IMAGE_TYPE_2D:
      assert(range->baseArrayLayer + radv_get_layerCount(image, range) - 1 <=
             image->info.array_size);
      break;
   case VK_IMAGE_TYPE_3D:
      assert(range->baseArrayLayer + radv_get_layerCount(image, range) - 1 <=
             radv_minify(image->info.depth, range->baseMipLevel));
      break;
   default:
      unreachable("bad VkImageType");
   }
   iview->image = image;
   iview->type = pCreateInfo->viewType;
   iview->plane_id = radv_plane_from_aspect(pCreateInfo->subresourceRange.aspectMask);
   iview->aspect_mask = pCreateInfo->subresourceRange.aspectMask;
   iview->base_layer = range->baseArrayLayer;
   iview->layer_count = radv_get_layerCount(image, range);
   iview->base_mip = range->baseMipLevel;
   iview->level_count = radv_get_levelCount(image, range);

   iview->vk_format = pCreateInfo->format;

   /* If the image has an Android external format, pCreateInfo->format will be
    * VK_FORMAT_UNDEFINED. */
   if (iview->vk_format == VK_FORMAT_UNDEFINED)
      iview->vk_format = image->vk_format;

   /* Split out the right aspect. Note that for internal meta code we sometimes
    * use an equivalent color format for the aspect so we first have to check
    * if we actually got depth/stencil formats. */
   if (iview->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT) {
      if (vk_format_has_stencil(iview->vk_format))
         iview->vk_format = vk_format_stencil_only(iview->vk_format);
   } else if (iview->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT) {
      if (vk_format_has_depth(iview->vk_format))
         iview->vk_format = vk_format_depth_only(iview->vk_format);
   }

   if (vk_format_get_plane_count(image->vk_format) > 1 &&
       iview->aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT) {
      plane_count = vk_format_get_plane_count(iview->vk_format);
   }

   if (device->physical_device->emulate_etc2 &&
       vk_format_description(image->vk_format)->layout == UTIL_FORMAT_LAYOUT_ETC) {
      const struct util_format_description *desc = vk_format_description(iview->vk_format);
      assert(desc);
      if (desc->layout == UTIL_FORMAT_LAYOUT_ETC) {
         iview->plane_id = 1;
         iview->vk_format = etc2_emulation_format(iview->vk_format);
      }

      plane_count = 1;
   }

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      iview->extent = (VkExtent3D){
         .width = image->info.width,
         .height = image->info.height,
         .depth = image->info.depth,
      };
   } else {
      iview->extent = (VkExtent3D){
         .width = radv_minify(image->info.width, range->baseMipLevel),
         .height = radv_minify(image->info.height, range->baseMipLevel),
         .depth = radv_minify(image->info.depth, range->baseMipLevel),
      };
   }

   if (iview->vk_format != image->planes[iview->plane_id].format) {
      unsigned view_bw = vk_format_get_blockwidth(iview->vk_format);
      unsigned view_bh = vk_format_get_blockheight(iview->vk_format);
      unsigned img_bw = vk_format_get_blockwidth(image->planes[iview->plane_id].format);
      unsigned img_bh = vk_format_get_blockheight(image->planes[iview->plane_id].format);

      iview->extent.width = round_up_u32(iview->extent.width * view_bw, img_bw);
      iview->extent.height = round_up_u32(iview->extent.height * view_bh, img_bh);

      /* Comment ported from amdvlk -
       * If we have the following image:
       *              Uncompressed pixels   Compressed block sizes (4x4)
       *      mip0:       22 x 22                   6 x 6
       *      mip1:       11 x 11                   3 x 3
       *      mip2:        5 x  5                   2 x 2
       *      mip3:        2 x  2                   1 x 1
       *      mip4:        1 x  1                   1 x 1
       *
       * On GFX9 the descriptor is always programmed with the WIDTH and HEIGHT of the base level and
       * the HW is calculating the degradation of the block sizes down the mip-chain as follows
       * (straight-up divide-by-two integer math): mip0:  6x6 mip1:  3x3 mip2:  1x1 mip3:  1x1
       *
       * This means that mip2 will be missing texels.
       *
       * Fix this by calculating the base mip's width and height, then convert
       * that, and round it back up to get the level 0 size. Clamp the
       * converted size between the original values, and the physical extent
       * of the base mipmap.
       *
       * On GFX10 we have to take care to not go over the physical extent
       * of the base mipmap as otherwise the GPU computes a different layout.
       * Note that the GPU does use the same base-mip dimensions for both a
       * block compatible format and the compressed format, so even if we take
       * the plain converted dimensions the physical layout is correct.
       */
      if (device->physical_device->rad_info.chip_class >= GFX9 &&
          vk_format_is_compressed(image->vk_format) && !vk_format_is_compressed(iview->vk_format)) {
         /* If we have multiple levels in the view we should ideally take the last level,
          * but the mip calculation has a max(..., 1) so walking back to the base mip in an
          * useful way is hard. */
         if (iview->level_count > 1) {
            iview->extent.width = iview->image->planes[0].surface.u.gfx9.base_mip_width;
            iview->extent.height = iview->image->planes[0].surface.u.gfx9.base_mip_height;
         } else {
            unsigned lvl_width = radv_minify(image->info.width, range->baseMipLevel);
            unsigned lvl_height = radv_minify(image->info.height, range->baseMipLevel);

            lvl_width = round_up_u32(lvl_width * view_bw, img_bw);
            lvl_height = round_up_u32(lvl_height * view_bh, img_bh);

            lvl_width <<= range->baseMipLevel;
            lvl_height <<= range->baseMipLevel;

            iview->extent.width = CLAMP(lvl_width, iview->extent.width,
                                        iview->image->planes[0].surface.u.gfx9.base_mip_width);
            iview->extent.height = CLAMP(lvl_height, iview->extent.height,
                                         iview->image->planes[0].surface.u.gfx9.base_mip_height);
         }
      }
   }

   iview->support_fast_clear = radv_image_view_can_fast_clear(device, iview);

   bool disable_compression = extra_create_info ? extra_create_info->disable_compression : false;
   bool enable_compression = extra_create_info ? extra_create_info->enable_compression : false;
   for (unsigned i = 0; i < plane_count; ++i) {
      VkFormat format = vk_format_get_plane_format(iview->vk_format, i);
      radv_image_view_make_descriptor(iview, device, format, &pCreateInfo->components, min_lod, false,
                                      disable_compression, enable_compression, iview->plane_id + i,
                                      i);
      radv_image_view_make_descriptor(iview, device, format, &pCreateInfo->components, min_lod, true,
                                      disable_compression, enable_compression, iview->plane_id + i,
                                      i);
   }
}

void
radv_image_view_finish(struct radv_image_view *iview)
{
   vk_object_base_finish(&iview->base);
}

bool
radv_layout_is_htile_compressed(const struct radv_device *device, const struct radv_image *image,
                                VkImageLayout layout, bool in_render_loop, unsigned queue_mask)
{
   switch (layout) {
   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
   case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
   case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
   case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR:
      return radv_image_has_htile(image);
   case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      return radv_image_is_tc_compat_htile(image) ||
             (radv_image_has_htile(image) && queue_mask == (1u << RADV_QUEUE_GENERAL));
   case VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR:
   case VK_IMAGE_LAYOUT_GENERAL:
      /* It should be safe to enable TC-compat HTILE with
       * VK_IMAGE_LAYOUT_GENERAL if we are not in a render loop and
       * if the image doesn't have the storage bit set. This
       * improves performance for apps that use GENERAL for the main
       * depth pass because this allows compression and this reduces
       * the number of decompressions from/to GENERAL.
       */
      /* FIXME: Enabling TC-compat HTILE in GENERAL on the compute
       * queue is likely broken for eg. depth/stencil copies.
       */
      if (radv_image_is_tc_compat_htile(image) && queue_mask & (1u << RADV_QUEUE_GENERAL) &&
          !in_render_loop && !device->instance->disable_tc_compat_htile_in_general) {
         return true;
      } else {
         return false;
      }
   case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
      if (radv_image_is_tc_compat_htile(image) ||
          (radv_image_has_htile(image) &&
           !(image->usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)))) {
         /* Keep HTILE compressed if the image is only going to
          * be used as a depth/stencil read-only attachment.
          */
         return true;
      } else {
         return false;
      }
      break;
   default:
      return radv_image_is_tc_compat_htile(image);
   }
}

bool
radv_layout_can_fast_clear(const struct radv_device *device, const struct radv_image *image,
                           unsigned level, VkImageLayout layout, bool in_render_loop,
                           unsigned queue_mask)
{
   if (radv_dcc_enabled(image, level) &&
       !radv_layout_dcc_compressed(device, image, level, layout, in_render_loop, queue_mask))
      return false;

   if (!(image->usage & RADV_IMAGE_USAGE_WRITE_BITS))
      return false;

   if (layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
       layout != VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR)
      return false;

   /* Exclusive images with CMASK or DCC can always be fast-cleared on the gfx queue. Concurrent
    * images can only be fast-cleared if comp-to-single is supported because we don't yet support
    * FCE on the compute queue.
    */
   return queue_mask == (1u << RADV_QUEUE_GENERAL) || radv_image_use_comp_to_single(device, image);
}

bool
radv_layout_dcc_compressed(const struct radv_device *device, const struct radv_image *image,
                           unsigned level, VkImageLayout layout, bool in_render_loop,
                           unsigned queue_mask)
{
   if (!radv_dcc_enabled(image, level))
      return false;

   if (image->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT && queue_mask & (1u << RADV_QUEUE_FOREIGN))
      return true;

   /* If the image is read-only, we can always just keep it compressed */
   if (!(image->usage & RADV_IMAGE_USAGE_WRITE_BITS))
      return true;

   /* Don't compress compute transfer dst when image stores are not supported. */
   if ((layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || layout == VK_IMAGE_LAYOUT_GENERAL) &&
       (queue_mask & (1u << RADV_QUEUE_COMPUTE)) && !radv_image_use_dcc_image_stores(device, image))
      return false;

   return device->physical_device->rad_info.chip_class >= GFX10 || layout != VK_IMAGE_LAYOUT_GENERAL;
}

bool
radv_layout_fmask_compressed(const struct radv_device *device, const struct radv_image *image,
                             VkImageLayout layout, unsigned queue_mask)
{
   if (!radv_image_has_fmask(image))
      return false;

   /* Don't compress compute transfer dst because image stores ignore FMASK and it needs to be
    * expanded before.
    */
   if ((layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL || layout == VK_IMAGE_LAYOUT_GENERAL) &&
       (queue_mask & (1u << RADV_QUEUE_COMPUTE)))
      return false;

   /* Only compress concurrent images if TC-compat CMASK is enabled (no FMASK decompression). */
   return layout != VK_IMAGE_LAYOUT_GENERAL &&
          (queue_mask == (1u << RADV_QUEUE_GENERAL) || radv_image_is_tc_compat_cmask(image));
}

unsigned
radv_image_queue_family_mask(const struct radv_image *image, uint32_t family, uint32_t queue_family)
{
   if (!image->exclusive)
      return image->queue_family_mask;
   if (family == VK_QUEUE_FAMILY_EXTERNAL || family == VK_QUEUE_FAMILY_FOREIGN_EXT)
      return ((1u << RADV_MAX_QUEUE_FAMILIES) - 1u) | (1u << RADV_QUEUE_FOREIGN);
   if (family == VK_QUEUE_FAMILY_IGNORED)
      return 1u << queue_family;
   return 1u << family;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
#ifdef ANDROID
   const VkNativeBufferANDROID *gralloc_info =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);

   if (gralloc_info)
      return radv_image_from_gralloc(device, pCreateInfo, gralloc_info, pAllocator, pImage);
#endif

   const struct wsi_image_create_info *wsi_info =
      vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
   bool scanout = wsi_info && wsi_info->scanout;
   bool prime_blit_src = wsi_info && wsi_info->prime_blit_src;

   return radv_image_create(device,
                            &(struct radv_image_create_info){
                               .vk_info = pCreateInfo,
                               .scanout = scanout,
                               .prime_blit_src = prime_blit_src,
                            },
                            pAllocator, pImage);
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyImage(VkDevice _device, VkImage _image, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_image, image, _image);

   if (!image)
      return;

   radv_destroy_image(device, pAllocator, image);
}

VKAPI_ATTR void VKAPI_CALL
radv_GetImageSubresourceLayout(VkDevice _device, VkImage _image,
                               const VkImageSubresource *pSubresource, VkSubresourceLayout *pLayout)
{
   RADV_FROM_HANDLE(radv_image, image, _image);
   RADV_FROM_HANDLE(radv_device, device, _device);
   int level = pSubresource->mipLevel;
   int layer = pSubresource->arrayLayer;

   unsigned plane_id = 0;
   if (vk_format_get_plane_count(image->vk_format) > 1)
      plane_id = radv_plane_from_aspect(pSubresource->aspectMask);

   struct radv_image_plane *plane = &image->planes[plane_id];
   struct radeon_surf *surface = &plane->surface;

   if (image->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      unsigned mem_plane_id = radv_plane_from_aspect(pSubresource->aspectMask);

      assert(level == 0);
      assert(layer == 0);

      pLayout->offset = ac_surface_get_plane_offset(device->physical_device->rad_info.chip_class,
                                                    surface, mem_plane_id, 0);
      pLayout->rowPitch = ac_surface_get_plane_stride(device->physical_device->rad_info.chip_class,
                                                      surface, mem_plane_id, level);
      pLayout->arrayPitch = 0;
      pLayout->depthPitch = 0;
      pLayout->size = ac_surface_get_plane_size(surface, mem_plane_id);
   } else if (device->physical_device->rad_info.chip_class >= GFX9) {
      uint64_t level_offset = surface->is_linear ? surface->u.gfx9.offset[level] : 0;

      pLayout->offset = ac_surface_get_plane_offset(device->physical_device->rad_info.chip_class,
                                                    &plane->surface, 0, layer) +
                        level_offset;
      if (image->vk_format == VK_FORMAT_R32G32B32_UINT ||
          image->vk_format == VK_FORMAT_R32G32B32_SINT ||
          image->vk_format == VK_FORMAT_R32G32B32_SFLOAT) {
         /* Adjust the number of bytes between each row because
          * the pitch is actually the number of components per
          * row.
          */
         pLayout->rowPitch = surface->u.gfx9.surf_pitch * surface->bpe / 3;
      } else {
         uint32_t pitch =
            surface->is_linear ? surface->u.gfx9.pitch[level] : surface->u.gfx9.surf_pitch;

         assert(util_is_power_of_two_nonzero(surface->bpe));
         pLayout->rowPitch = pitch * surface->bpe;
      }

      pLayout->arrayPitch = surface->u.gfx9.surf_slice_size;
      pLayout->depthPitch = surface->u.gfx9.surf_slice_size;
      pLayout->size = surface->u.gfx9.surf_slice_size;
      if (image->type == VK_IMAGE_TYPE_3D)
         pLayout->size *= u_minify(image->info.depth, level);
   } else {
      pLayout->offset = (uint64_t)surface->u.legacy.level[level].offset_256B * 256 +
                        (uint64_t)surface->u.legacy.level[level].slice_size_dw * 4 * layer;
      pLayout->rowPitch = surface->u.legacy.level[level].nblk_x * surface->bpe;
      pLayout->arrayPitch = (uint64_t)surface->u.legacy.level[level].slice_size_dw * 4;
      pLayout->depthPitch = (uint64_t)surface->u.legacy.level[level].slice_size_dw * 4;
      pLayout->size = (uint64_t)surface->u.legacy.level[level].slice_size_dw * 4;
      if (image->type == VK_IMAGE_TYPE_3D)
         pLayout->size *= u_minify(image->info.depth, level);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_GetImageDrmFormatModifierPropertiesEXT(VkDevice _device, VkImage _image,
                                            VkImageDrmFormatModifierPropertiesEXT *pProperties)
{
   RADV_FROM_HANDLE(radv_image, image, _image);

   pProperties->drmFormatModifier = image->planes[0].surface.modifier;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateImageView(VkDevice _device, const VkImageViewCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator, VkImageView *pView)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_image_view *view;

   view =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*view), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (view == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_image_view_init(view, device, pCreateInfo, NULL);

   *pView = radv_image_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyImageView(VkDevice _device, VkImageView _iview, const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_image_view, iview, _iview);

   if (!iview)
      return;

   radv_image_view_finish(iview);
   vk_free2(&device->vk.alloc, pAllocator, iview);
}

void
radv_buffer_view_init(struct radv_buffer_view *view, struct radv_device *device,
                      const VkBufferViewCreateInfo *pCreateInfo)
{
   RADV_FROM_HANDLE(radv_buffer, buffer, pCreateInfo->buffer);

   vk_object_base_init(&device->vk, &view->base, VK_OBJECT_TYPE_BUFFER_VIEW);

   view->bo = buffer->bo;
   view->range =
      pCreateInfo->range == VK_WHOLE_SIZE ? buffer->size - pCreateInfo->offset : pCreateInfo->range;
   view->vk_format = pCreateInfo->format;

   radv_make_buffer_descriptor(device, buffer, view->vk_format, pCreateInfo->offset, view->range,
                               view->state);
}

void
radv_buffer_view_finish(struct radv_buffer_view *view)
{
   vk_object_base_finish(&view->base);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateBufferView(VkDevice _device, const VkBufferViewCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator, VkBufferView *pView)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_buffer_view *view;

   view =
      vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*view), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_buffer_view_init(view, device, pCreateInfo);

   *pView = radv_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
radv_DestroyBufferView(VkDevice _device, VkBufferView bufferView,
                       const VkAllocationCallbacks *pAllocator)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_buffer_view, view, bufferView);

   if (!view)
      return;

   radv_buffer_view_finish(view);
   vk_free2(&device->vk.alloc, pAllocator, view);
}
