/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_image.h"

#include "fdl/fd6_format_table.h"
#include "common/freedreno_lrz.h"

#include "util/u_debug.h"
#include "util/format/u_format.h"
#include "vulkan/vulkan_android.h"
#include "vk_android.h"
#include "vk_debug_utils.h"
#include "vk_util.h"
#include "drm-uapi/drm_fourcc.h"
#include "vulkan/vulkan_core.h"

#include "tu_buffer.h"
#include "tu_cs.h"
#include "tu_descriptor_set.h"
#include "tu_device.h"
#include "tu_formats.h"
#include "tu_lrz.h"
#include "tu_rmv.h"
#include "tu_wsi.h"

uint32_t
tu6_plane_count(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      /* We do not support interleaved depth/stencil. Instead, we decompose to
       * a depth plane and a stencil plane.
       */
      return 2;

   default:
      return vk_format_get_plane_count(format);
   }
}

enum pipe_format
tu6_plane_format(VkFormat format, uint32_t plane)
{
   switch (format) {
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      /* See tu6_plane_count above */
      return !plane ? PIPE_FORMAT_Z32_FLOAT : PIPE_FORMAT_S8_UINT;

   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      /* The 0'th plane of this format has a different UBWC compression */
      return !plane ? PIPE_FORMAT_Y8_UNORM : PIPE_FORMAT_R8G8_UNORM;

   default:
      return vk_format_to_pipe_format(vk_format_get_plane_format(format, plane));
   }
}

uint32_t
tu6_plane_index(VkFormat format, VkImageAspectFlags aspect_mask)
{
   /* Must only be one aspect unless it's depth/stencil */
   assert(aspect_mask ==
             (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) ||
          util_bitcount(aspect_mask) == 1);

   switch (aspect_mask) {
   default:
      assert(aspect_mask != VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT);
      return 0;

   case VK_IMAGE_ASPECT_PLANE_1_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT:
      return 1;

   case VK_IMAGE_ASPECT_PLANE_2_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT:
      return 2;

   case VK_IMAGE_ASPECT_STENCIL_BIT:
      return format == VK_FORMAT_D32_SFLOAT_S8_UINT;
   }
}

enum pipe_format
tu_format_for_aspect(enum pipe_format format, VkImageAspectFlags aspect_mask)
{
   switch (format) {
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      /* VK_IMAGE_ASPECT_COLOR_BIT is used internally for blits (despite we
       * also incorrectly advertise VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT for
       * depth formats).  Return PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8 in
       * this case.
       *
       * Otherwise, return the appropriate pipe format and let fdl6_view_init
       * take care of the rest.
       */
      if (aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
         return PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
      if (aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) {
         if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
            return PIPE_FORMAT_Z24_UNORM_S8_UINT;
         else
            return PIPE_FORMAT_X24S8_UINT;
      } else {
         return PIPE_FORMAT_Z24X8_UNORM;
      }
   case PIPE_FORMAT_Z24X8_UNORM:
      if (aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT)
         return PIPE_FORMAT_Z24_UNORM_S8_UINT_AS_R8G8B8A8;
      return PIPE_FORMAT_Z24X8_UNORM;
   default:
      return format;
   }
}

static bool
tu_is_r8g8(enum pipe_format format)
{
   return (util_format_get_blocksize(format) == 2) &&
          (util_format_get_nr_components(format) == 2);
}

static bool
tu_is_r8g8_compatible(enum pipe_format format)
{
   return (util_format_get_blocksize(format) == 2) &&
          !util_format_is_depth_or_stencil(format);
}

uint64_t
tu_layer_address(const struct fdl6_view *iview, uint32_t layer)
{
   return iview->base_addr + iview->layer_size * layer;
}

void
tu_cs_image_ref(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer)
{
   tu_cs_emit(cs, A6XX_RB_MRT_PITCH(0, iview->pitch).value);
   tu_cs_emit(cs, iview->layer_size >> 6);
   tu_cs_emit_qw(cs, tu_layer_address(iview, layer));
}

void
tu_cs_image_stencil_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu_cs_emit(cs, A6XX_RB_STENCIL_BUFFER_PITCH(iview->stencil_pitch).value);
   tu_cs_emit(cs, iview->stencil_layer_size >> 6);
   tu_cs_emit_qw(cs, iview->stencil_base_addr + iview->stencil_layer_size * layer);
}

void
tu_cs_image_depth_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu_cs_emit(cs, A6XX_RB_DEPTH_BUFFER_PITCH(iview->depth_pitch).value);
   tu_cs_emit(cs, iview->depth_layer_size >> 6);
   tu_cs_emit_qw(cs, iview->depth_base_addr + iview->depth_layer_size * layer);
}

template <chip CHIP>
void
tu_cs_image_ref_2d(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer, bool src)
{
   tu_cs_emit_qw(cs, iview->base_addr + iview->layer_size * layer);
   /* SP_PS_2D_SRC_PITCH has shifted pitch field */
   if (src)
      tu_cs_emit(cs, SP_PS_2D_SRC_PITCH(CHIP, .pitch = iview->pitch).value);
   else
      tu_cs_emit(cs, A6XX_RB_2D_DST_PITCH(iview->pitch).value);
}
TU_GENX(tu_cs_image_ref_2d);

void
tu_cs_image_flag_ref(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer)
{
   tu_cs_emit_qw(cs, iview->ubwc_addr + iview->ubwc_layer_size * layer);
   tu_cs_emit(cs, iview->FLAG_BUFFER_PITCH);
}

static void
tu_image_view_init(struct tu_device *device,
                   struct tu_image_view *iview,
                   const VkImageViewCreateInfo *pCreateInfo,
                   bool has_z24uint_s8uint)
{
   VK_FROM_HANDLE(tu_image, image, pCreateInfo->image);
   const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;
   VkFormat vk_format =
      vk_select_android_external_format(pCreateInfo->pNext, pCreateInfo->format);

   /* With AHB, the app may be using an external format but not necessarily
    * chain the VkExternalFormatANDROID.  In this case, just take the format
    * from the image.
    */
   if ((vk_format == VK_FORMAT_UNDEFINED) &&
       (image->vk.external_handle_types & VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID))
       vk_format = image->vk.format;

   VkImageAspectFlags aspect_mask = pCreateInfo->subresourceRange.aspectMask;

   const struct VkSamplerYcbcrConversionInfo *ycbcr_conversion =
      vk_find_struct_const(pCreateInfo->pNext, SAMPLER_YCBCR_CONVERSION_INFO);
   const struct vk_ycbcr_conversion *conversion = ycbcr_conversion ?
      vk_ycbcr_conversion_from_handle(ycbcr_conversion->conversion) : NULL;

   vk_image_view_init(&device->vk, &iview->vk, false, pCreateInfo);

   iview->image = image;

   const struct fdl_layout *layouts[3];

   layouts[0] = &image->layout[tu6_plane_index(image->vk.format, aspect_mask)];

   enum pipe_format format;
   if (vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT)
      format = tu_aspects_to_plane(vk_format, aspect_mask);
   else
      format = vk_format_to_pipe_format(vk_format);

   if (image->vk.format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM &&
       aspect_mask == VK_IMAGE_ASPECT_PLANE_0_BIT) {
      if (vk_format == VK_FORMAT_R8_UNORM) {
         /* The 0'th plane of this format has a different UBWC compression. */
         format = PIPE_FORMAT_Y8_UNORM;
      } else {
         /* If the user wants to reinterpret this plane, then they should've
          * set MUTABLE_FORMAT_BIT which should disable UBWC and tiling.
          */
         assert(!layouts[0]->ubwc);
      }
   }

   if (aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT &&
       vk_format_get_plane_count(vk_format) > 1) {
      layouts[1] = &image->layout[1];
      layouts[2] = &image->layout[2];
   }

   vk_component_mapping_to_pipe_swizzle(pCreateInfo->components,
                                        iview->swizzle);

   struct fdl_view_args args = {};
   args.chip = device->physical_device->info->chip;
   args.iova = image->iova;
   args.base_array_layer = range->baseArrayLayer;
   args.base_miplevel = range->baseMipLevel;
   args.layer_count = vk_image_subresource_layer_count(&image->vk, range);
   args.level_count = vk_image_subresource_level_count(&image->vk, range);
   args.min_lod_clamp = iview->vk.min_lod;
   args.format = tu_format_for_aspect(format, aspect_mask);
   args.ubwc_fc_mutable = image->ubwc_fc_mutable;
   vk_component_mapping_to_pipe_swizzle(pCreateInfo->components, args.swiz);
   if (conversion) {
      unsigned char conversion_swiz[4], create_swiz[4];
      memcpy(create_swiz, args.swiz, sizeof(create_swiz));

      VkComponentMapping component = {
         .r = conversion->state.mapping[0],
         .g = conversion->state.mapping[1],
         .b = conversion->state.mapping[2],
         .a = conversion->state.mapping[3]
      };
      vk_component_mapping_to_pipe_swizzle(component, conversion_swiz);
      util_format_compose_swizzles(create_swiz, conversion_swiz, args.swiz);
   }

   switch (pCreateInfo->viewType) {
   case VK_IMAGE_VIEW_TYPE_1D:
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      args.type = FDL_VIEW_TYPE_1D;
      break;
   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
      args.type = FDL_VIEW_TYPE_2D;
      break;
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      args.type = FDL_VIEW_TYPE_CUBE;
      break;
   case VK_IMAGE_VIEW_TYPE_3D:
      args.type = FDL_VIEW_TYPE_3D;
      break;
   default:
      unreachable("unknown view type");
   }

   STATIC_ASSERT((unsigned)VK_CHROMA_LOCATION_COSITED_EVEN == (unsigned)FDL_CHROMA_LOCATION_COSITED_EVEN);
   STATIC_ASSERT((unsigned)VK_CHROMA_LOCATION_MIDPOINT == (unsigned)FDL_CHROMA_LOCATION_MIDPOINT);
   if (conversion) {
      args.chroma_offsets[0] = (enum fdl_chroma_location) conversion->state.chroma_offsets[0];
      args.chroma_offsets[1] = (enum fdl_chroma_location) conversion->state.chroma_offsets[1];
   }

   fdl6_view_init(&iview->view, layouts, &args, has_z24uint_s8uint);

   if (image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      struct fdl_layout *layout = &image->layout[0];
      iview->depth_base_addr = image->iova +
         fdl_surface_offset(layout, range->baseMipLevel, range->baseArrayLayer);
      iview->depth_layer_size = fdl_layer_stride(layout, range->baseMipLevel);
      iview->depth_pitch = fdl_pitch(layout, range->baseMipLevel);

      layout = &image->layout[1];
      iview->stencil_base_addr = image->iova +
         fdl_surface_offset(layout, range->baseMipLevel, range->baseArrayLayer);
      iview->stencil_layer_size = fdl_layer_stride(layout, range->baseMipLevel);
      iview->stencil_pitch = fdl_pitch(layout, range->baseMipLevel);
   }
}

bool
tiling_possible(VkFormat format)
{
   if (format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM ||
       format == VK_FORMAT_G8B8G8R8_422_UNORM ||
       format == VK_FORMAT_B8G8R8G8_422_UNORM)
      return false;

   return true;
}

/* Checks if we should advertise UBWC support for the given usage.
 *
 * Used by both vkCreateImage and vkGetPhysicalDeviceFormatProperties2, so the
 * logical tu_device may be NULL.
 */
bool
ubwc_possible(struct tu_device *device,
              VkFormat format,
              VkImageType type,
              VkImageUsageFlags usage,
              VkImageUsageFlags stencil_usage,
              const struct fd_dev_info *info,
              VkSampleCountFlagBits samples,
              bool use_z24uint_s8uint)
{
   /* no UBWC with compressed formats, E5B9G9R9, S8_UINT
    * (S8_UINT because separate stencil doesn't have UBWC-enable bit)
    */
   if (vk_format_is_compressed(format) ||
       format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 ||
       format == VK_FORMAT_S8_UINT)
      return false;

   /* In copy_format, we treat snorm as unorm to avoid clamping.  But snorm
    * and unorm are UBWC incompatible for special values such as all 0's or
    * all 1's prior to a740.  Disable UBWC for snorm.
    */
   if (vk_format_is_snorm(format) &&
       !info->a7xx.ubwc_unorm_snorm_int_compatible)
      return false;

   if (!info->a6xx.has_8bpp_ubwc &&
       vk_format_get_blocksizebits(format) == 8 &&
       vk_format_get_plane_count(format) == 1)
      return false;

   if (type == VK_IMAGE_TYPE_3D) {
      if (device) {
         perf_debug(device,
                    "Disabling UBWC for %s 3D image, but it should be "
                    "possible to support.",
                    util_format_name(vk_format_to_pipe_format(format)));
      }
      return false;
   }

   /* Disable UBWC for storage images when not supported.
    *
    * Prior to a7xx, storage images must be readonly or writeonly to use UBWC.
    * Freedreno can determine when this isn't the case and decompress the
    * image on-the-fly, but we don't know which image a binding corresponds to
    * and we can't change the descriptor so we can't do this.
    */
   if (((usage | stencil_usage) & VK_IMAGE_USAGE_STORAGE_BIT) &&
       !info->a7xx.supports_ibo_ubwc) {
      return false;
   }

   /* A690 seem to have broken UBWC for depth/stencil, it requires
    * depth flushing where we cannot realistically place it, like between
    * ordinary draw calls writing read/depth. WSL blob seem to use ubwc
    * sometimes for depth/stencil.
    */
   if (info->a6xx.broken_ds_ubwc_quirk &&
       vk_format_is_depth_or_stencil(format))
      return false;

   /* We don't support compressing or decompressing on the CPU */
   if ((usage | stencil_usage) & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT) {
      return false;
   }

   /* Disable UBWC for D24S8 on A630 in some cases
    *
    * VK_IMAGE_ASPECT_STENCIL_BIT image view requires to be able to sample
    * from the stencil component as UINT, however no format allows this
    * on a630 (the special FMT6_Z24_UINT_S8_UINT format is missing)
    *
    * It must be sampled as FMT6_8_8_8_8_UINT, which is not UBWC-compatible
    *
    * If we wish to get the border colors correct without knowing the format
    * when creating the sampler, we also have to use the A630 workaround.
    */
   if (!use_z24uint_s8uint &&
       format == VK_FORMAT_D24_UNORM_S8_UINT &&
       (stencil_usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)))
      return false;

   if (!info->a6xx.has_z24uint_s8uint &&
       (format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_X8_D24_UNORM_PACK32) &&
       samples > VK_SAMPLE_COUNT_1_BIT) {
      return false;
   }

   return true;
}

/* R8G8 have a different block width/height and height alignment from other
 * formats that would normally be compatible (like R16), and so if we are
 * trying to, for example, sample R16 as R8G8 we need to demote to linear.
 */
static bool
format_list_reinterprets_r8g8_r16(enum pipe_format format, const VkImageFormatListCreateInfo *fmt_list)
{
   /* Check if it's actually a 2-cpp color format. */
   if (!tu_is_r8g8_compatible(format))
      return false;

   /* If there's no format list, then the app may reinterpret to any compatible
    * format.
    */
   if (!fmt_list || !fmt_list->viewFormatCount)
      return true;

   bool has_r8g8 = false;
   bool has_non_r8g8 = false;
   for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
      enum pipe_format format =
         vk_format_to_pipe_format(fmt_list->pViewFormats[i]);
      if (tu_is_r8g8(format))
         has_r8g8 = true;
      else
         has_non_r8g8 = true;
   }
   return has_r8g8 && has_non_r8g8;
}

static bool
format_list_has_swaps(const VkImageFormatListCreateInfo *fmt_list)
{
   /* If there's no format list, then the app may reinterpret to any compatible
    * format, and presumably one would have the swap set.
    */
   if (!fmt_list || !fmt_list->viewFormatCount)
      return true;

   for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
      enum pipe_format format =
         vk_format_to_pipe_format(fmt_list->pViewFormats[i]);

      if (tu6_format_texture(format, TILE6_LINEAR).swap)
         return true;
   }
   return false;
}

template <chip CHIP>
VkResult
tu_image_update_layout(struct tu_device *device, struct tu_image *image,
                       uint64_t modifier, const VkSubresourceLayout *plane_layouts)
{
   enum a6xx_tile_mode tile_mode = TILE6_3;
#if DETECT_OS_LINUX || DETECT_OS_BSD
   image->vk.drm_format_mod = modifier;
#endif

   if (modifier == DRM_FORMAT_MOD_LINEAR) {
      image->force_linear_tile = true;
   }

   if (image->force_linear_tile) {
      tile_mode = TILE6_LINEAR;
      image->ubwc_enabled = false;
   }

   /* Whether a view of the image with an R8G8 format could be made. */
   bool has_r8g8 = tu_is_r8g8(vk_format_to_pipe_format(image->vk.format));

   /* With AHB, we could be asked to create an image with VK_IMAGE_TILING_LINEAR
    * but gralloc doesn't know this.  So if we are explicitly told that it is
    * UBWC, then override how the image was created.
    */
   if (modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED) {
      assert(!image->force_linear_tile);
      image->ubwc_enabled = true;
   }

   /* Non-UBWC tiled R8G8 is probably buggy since media formats are always
    * either linear or UBWC. There is no simple test to reproduce the bug.
    * However it was observed in the wild leading to an unrecoverable hang
    * on a650/a660.
    */
   if (has_r8g8 && tile_mode == TILE6_3 && !image->ubwc_enabled) {
      tile_mode = TILE6_LINEAR;
   }

   for (uint32_t i = 0; i < tu6_plane_count(image->vk.format); i++) {
      struct fdl_layout *layout = &image->layout[i];
      enum pipe_format format = tu6_plane_format(image->vk.format, i);
      uint32_t width0 = vk_format_get_plane_width(image->vk.format, i, image->vk.extent.width);
      uint32_t height0 = vk_format_get_plane_height(image->vk.format, i, image->vk.extent.height);

      if (i == 1 && image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT)
         /* no UBWC for separate stencil */
         image->ubwc_enabled = false;

      struct fdl_explicit_layout plane_layout;

      if (plane_layouts) {
         /* only expect simple 2D images for now */
         if (image->vk.mip_levels != 1 ||
            image->vk.array_layers != 1 ||
            image->vk.extent.depth != 1)
            return vk_error(device, VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);

         plane_layout.offset = plane_layouts[i].offset;
         plane_layout.pitch = plane_layouts[i].rowPitch;
         /* note: use plane_layouts[0].arrayPitch to support array formats */
      }

      layout->tile_mode = tile_mode;
      layout->ubwc = image->ubwc_enabled;

      if (!fdl6_layout(layout, format,
                       image->vk.samples,
                       width0, height0,
                       image->vk.extent.depth,
                       image->vk.mip_levels,
                       image->vk.array_layers,
                       image->vk.image_type == VK_IMAGE_TYPE_3D,
                       plane_layouts ? &plane_layout : NULL)) {
         assert(plane_layouts); /* can only fail with explicit layout */
         return vk_error(device, VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
      }

      if (TU_DEBUG(LAYOUT))
         fdl_dump_layout(layout);

      /* fdl6_layout can't take explicit offset without explicit pitch
       * add offset manually for extra layouts for planes
       */
      if (!plane_layouts && i > 0) {
         uint32_t offset = ALIGN_POT(image->total_size, 4096);
         for (int i = 0; i < image->vk.mip_levels; i++) {
            layout->slices[i].offset += offset;
            layout->ubwc_slices[i].offset += offset;
         }
         layout->size += offset;
      }

      image->total_size = MAX2(image->total_size, layout->size);
   }

   const struct util_format_description *desc = util_format_description(image->layout[0].format);
   if (util_format_has_depth(desc) && device->use_lrz) {
      /* Depth plane is the first one */
      struct fdl_layout *layout = &image->layout[0];
      unsigned width = layout->width0;
      unsigned height = layout->height0;

      /* LRZ buffer is super-sampled */
      switch (layout->nr_samples) {
      case 4:
         width *= 2;
         FALLTHROUGH;
      case 2:
         height *= 2;
         break;
      default:
         break;
      }

      unsigned lrz_pitch  = align(DIV_ROUND_UP(width, 8), 32);
      unsigned lrz_height = align(DIV_ROUND_UP(height, 8), 16);

      image->lrz_height = lrz_height;
      image->lrz_pitch = lrz_pitch;
      image->lrz_offset = image->total_size;
      unsigned lrz_size = lrz_pitch * lrz_height * sizeof(uint16_t);

      unsigned nblocksx = DIV_ROUND_UP(DIV_ROUND_UP(width, 8), 16);
      unsigned nblocksy = DIV_ROUND_UP(DIV_ROUND_UP(height, 8), 4);

      /* Fast-clear buffer is 1bit/block */
      unsigned lrz_fc_size = DIV_ROUND_UP(nblocksx * nblocksy, 8);

      /* Fast-clear buffer cannot be larger than 512 bytes on A6XX and 1024 bytes on A7XX (HW limitation) */
      image->has_lrz_fc =
         device->physical_device->info->a6xx.enable_lrz_fast_clear &&
         lrz_fc_size <= fd_lrzfc_layout<CHIP>::FC_SIZE &&
         !TU_DEBUG(NOLRZFC);

      if (image->has_lrz_fc || device->physical_device->info->a6xx.has_lrz_dir_tracking) {
         image->lrz_fc_offset = image->total_size + lrz_size;
         lrz_size += sizeof(fd_lrzfc_layout<CHIP>);
      }

      image->total_size += lrz_size;
   } else {
      image->lrz_height = 0;
   }

   return VK_SUCCESS;
}
TU_GENX(tu_image_update_layout);

static VkResult
tu_image_init(struct tu_device *device, struct tu_image *image,
              const VkImageCreateInfo *pCreateInfo)
{
   image->ubwc_enabled = true;

   /* use linear tiling if requested */
   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR) {
      image->force_linear_tile = true;
   }

   /* Force linear tiling for formats with "fake" optimalTilingFeatures */
   if (!tiling_possible(image->vk.format)) {
      image->force_linear_tile = true;
   }

   /* No sense in tiling a 1D image, you'd just waste space and cache locality. */
   if (pCreateInfo->imageType == VK_IMAGE_TYPE_1D) {
      image->force_linear_tile = true;
   }

   /* Fragment density maps are sampled on the CPU and we don't support
    * sampling tiled images on the CPU or UBWC at the moment.
    */
   if (pCreateInfo->usage & VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT) {
      image->force_linear_tile = true;
   }

   /* Force linear tiling for HIC usage with swapped formats. Because tiled
    * images are stored without the swap, we would have to apply the swap when
    * copying on the CPU, which for some formats is tricky.
    *
    * TODO: should we add a fast path for BGRA8 and allow tiling for it?
    */
   if ((pCreateInfo->usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT) &&
       fd6_color_swap(vk_format_to_pipe_format(image->vk.format),
                                               TILE6_LINEAR) != WZYX)
      image->force_linear_tile = true;

   if (image->force_linear_tile ||
       !ubwc_possible(device, image->vk.format, pCreateInfo->imageType,
                      pCreateInfo->usage, image->vk.stencil_usage,
                      device->physical_device->info, pCreateInfo->samples,
                      device->use_z24uint_s8uint))
      image->ubwc_enabled = false;

   bool fmt_list_has_swaps = false;
   /* Mutable images can be reinterpreted as any other compatible format.
    * This is a problem with UBWC (compression for different formats is different),
    * but also tiling ("swap" affects how tiled formats are stored in memory)
    * Depth and stencil formats cannot be reintepreted as another format, and
    * cannot be linear with sysmem rendering, so don't fall back for those.
    *
    * TODO:
    * - if the fmt_list contains only formats which are swapped, but compatible
    *   with each other (B8G8R8A8_UNORM and B8G8R8A8_UINT for example), then
    *   tiling is still possible
    * - figure out which UBWC compressions are compatible to keep it enabled
    */
   if ((pCreateInfo->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
       !vk_format_is_depth_or_stencil(image->vk.format)) {
      const VkImageFormatListCreateInfo *fmt_list =
         vk_find_struct_const(pCreateInfo->pNext, IMAGE_FORMAT_LIST_CREATE_INFO);
      fmt_list_has_swaps = format_list_has_swaps(fmt_list);
      if (!tu6_mutable_format_list_ubwc_compatible(device->physical_device->info,
                                                   fmt_list)) {
         bool mutable_ubwc_fc = device->physical_device->info->a7xx.ubwc_all_formats_compatible;
         if (image->ubwc_enabled && !mutable_ubwc_fc) {
            if (fmt_list && fmt_list->viewFormatCount == 2) {
               perf_debug(
                  device,
                  "Disabling UBWC on %dx%d %s resource due to mutable formats "
                  "(fmt list %s, %s)",
                  image->vk.extent.width, image->vk.extent.height,
                  util_format_name(vk_format_to_pipe_format(image->vk.format)),
                  util_format_name(vk_format_to_pipe_format(fmt_list->pViewFormats[0])),
                  util_format_name(vk_format_to_pipe_format(fmt_list->pViewFormats[1])));
            } else {
               perf_debug(
                  device,
                  "Disabling UBWC on %dx%d %s resource due to mutable formats "
                  "(fmt list %s)",
                  image->vk.extent.width, image->vk.extent.height,
                  util_format_name(vk_format_to_pipe_format(image->vk.format)),
                  fmt_list ? "present" : "missing");
            }
            image->ubwc_enabled = false;
         }

         bool r8g8_r16 = format_list_reinterprets_r8g8_r16(vk_format_to_pipe_format(image->vk.format), fmt_list);

         /* A750+ TODO: Correctly handle swaps when copying mutable images.
          * We should be able to support UBWC for mutable images with swaps.
          */
         if ((r8g8_r16 && !mutable_ubwc_fc) || fmt_list_has_swaps) {
            image->ubwc_enabled = false;
            image->force_linear_tile = true;
         }

         image->ubwc_fc_mutable = image->ubwc_enabled && mutable_ubwc_fc;
      }
   }

   if (TU_DEBUG(NOUBWC)) {
      image->ubwc_enabled = false;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateImage(VkDevice _device,
               const VkImageCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *alloc,
               VkImage *pImage)
{
   uint64_t modifier = DRM_FORMAT_MOD_INVALID;
   const VkSubresourceLayout *plane_layouts = NULL;
   VkResult result;

   VK_FROM_HANDLE(tu_device, device, _device);

#ifdef TU_USE_WSI_PLATFORM
   /* Ignore swapchain creation info on Android. Since we don't have an
    * implementation in Mesa, we're guaranteed to access an Android object
    * incorrectly.
    */
   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      return wsi_common_create_swapchain_image(device->physical_device->vk.wsi_device,
                                               pCreateInfo,
                                               swapchain_info->swapchain,
                                               pImage);
   }
#endif

   struct tu_image *image = (struct tu_image *)
      vk_image_create(&device->vk, pCreateInfo, alloc, sizeof(*image));

   if (!image)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      const VkImageDrmFormatModifierListCreateInfoEXT *mod_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
      const VkImageDrmFormatModifierExplicitCreateInfoEXT *drm_explicit_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);

      assert(mod_info || drm_explicit_info);

      if (mod_info) {
         modifier = DRM_FORMAT_MOD_LINEAR;
         for (unsigned i = 0; i < mod_info->drmFormatModifierCount; i++) {
            if (mod_info->pDrmFormatModifiers[i] == DRM_FORMAT_MOD_QCOM_COMPRESSED)
               modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
         }
      } else {
         modifier = drm_explicit_info->drmFormatModifier;
         assert(modifier == DRM_FORMAT_MOD_LINEAR ||
                modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED);
         plane_layouts = drm_explicit_info->pPlaneLayouts;
      }
   } else {
      const struct wsi_image_create_info *wsi_info =
         vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
      if (wsi_info && wsi_info->scanout)
         modifier = DRM_FORMAT_MOD_LINEAR;
   }

   /* This section is removed by the optimizer for non-ANDROID builds */
   VkImageDrmFormatModifierExplicitCreateInfoEXT eci;
   VkSubresourceLayout a_plane_layouts[TU_MAX_PLANE_COUNT];
   if (vk_image_is_android_native_buffer(&image->vk)) {
      result = vk_android_get_anb_layout(
         pCreateInfo, &eci, a_plane_layouts, TU_MAX_PLANE_COUNT);
      if (result != VK_SUCCESS)
         goto fail;

      plane_layouts = a_plane_layouts;
      modifier = eci.drmFormatModifier;
   }

   result = tu_image_init(device, image, pCreateInfo);
   if (result != VK_SUCCESS)
      goto fail;

   /* This section is removed by the optimizer for non-ANDROID builds */
   if (vk_image_is_android_hardware_buffer(&image->vk)) {
      /* At this time, an AHB handle is not yet provided.
       * Image layout will be filled up during vkBindImageMemory2
       */
      *pImage = tu_image_to_handle(image);
      return VK_SUCCESS;
   }

   result = TU_CALLX(device, tu_image_update_layout)(device, image, modifier,
                                                    plane_layouts);
   if (result != VK_SUCCESS)
      goto fail;

   /* This section is removed by the optimizer for non-ANDROID builds */
   if (vk_image_is_android_native_buffer(&image->vk)) {
      result = vk_android_import_anb(&device->vk, pCreateInfo, alloc,
                                     &image->vk);
      if (result != VK_SUCCESS)
         goto fail;
   }

   TU_RMV(image_create, device, image);

#ifdef HAVE_PERFETTO
   tu_perfetto_log_create_image(device, image);
#endif

   *pImage = tu_image_to_handle(image);

   return VK_SUCCESS;
fail:
   vk_image_destroy(&device->vk, alloc, &image->vk);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyImage(VkDevice _device,
                VkImage _image,
                const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_image, image, _image);
   struct tu_instance *instance = device->physical_device->instance;

   if (!image)
      return;

   TU_RMV(image_destroy, device, image);

#ifdef HAVE_PERFETTO
   tu_perfetto_log_destroy_image(device, image);
#endif

   if (image->iova)
      vk_address_binding_report(&instance->vk, &image->vk.base,
                                image->iova, image->total_size,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_UNBIND_EXT);

   vk_image_destroy(&device->vk, pAllocator, &image->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_BindImageMemory2(VkDevice _device,
                    uint32_t bindInfoCount,
                    const VkBindImageMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   struct tu_instance *instance = device->physical_device->instance;

   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      VK_FROM_HANDLE(tu_image, image, pBindInfos[i].image);
      VK_FROM_HANDLE(tu_device_memory, mem, pBindInfos[i].memory);

      /* Ignore this struct on Android, we cannot access swapchain structures there. */
#ifdef TU_USE_WSI_PLATFORM
      const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
         vk_find_struct_const(pBindInfos[i].pNext, BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);

      if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
         VkImage _wsi_image = wsi_common_get_image(swapchain_info->swapchain,
                                                   swapchain_info->imageIndex);
         VK_FROM_HANDLE(tu_image, wsi_img, _wsi_image);

         image->bo = wsi_img->bo;
         image->map = NULL;
         image->iova = wsi_img->iova;

         TU_RMV(image_bind, device, image);

         vk_address_binding_report(&instance->vk, &image->vk.base,
                                   image->iova, image->total_size,
                                   VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);

         continue;
      }
#endif

      const VkBindMemoryStatusKHR *status =
         vk_find_struct_const(pBindInfos[i].pNext, BIND_MEMORY_STATUS_KHR);
      if (status)
         *status->pResult = VK_SUCCESS;

      if (mem) {
         VkResult result;
         if (vk_image_is_android_hardware_buffer(&image->vk)) {
            VkImageDrmFormatModifierExplicitCreateInfoEXT eci;
            VkSubresourceLayout a_plane_layouts[TU_MAX_PLANE_COUNT];
            result = vk_android_get_ahb_layout(mem->vk.ahardware_buffer,
                                            &eci, a_plane_layouts,
                                            TU_MAX_PLANE_COUNT);
            if (result != VK_SUCCESS) {
               if (status)
                  *status->pResult = result;
               return result;
            }

            result = TU_CALLX(device, tu_image_update_layout)(device, image,
                                                              eci.drmFormatModifier, a_plane_layouts);
            if (result != VK_SUCCESS) {
               if (status)
                  *status->pResult = result;
               return result;
            }
         }
         image->bo = mem->bo;
         image->bo_offset = pBindInfos[i].memoryOffset;
         image->iova = mem->bo->iova + pBindInfos[i].memoryOffset;

         if (image->vk.usage & (VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT |
                                VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT)) {
            if (!mem->bo->map) {
               result = tu_bo_map(device, mem->bo, NULL);
               if (result != VK_SUCCESS) {
                  if (status)
                     *status->pResult = result;
                  return result;
               }
            }

            image->map = (char *)mem->bo->map + pBindInfos[i].memoryOffset;
         } else {
            image->map = NULL;
         }
#ifdef HAVE_PERFETTO
         tu_perfetto_log_bind_image(device, image);
#endif
      } else {
         image->bo = NULL;
         image->map = NULL;
         image->iova = 0;
      }

      TU_RMV(image_bind, device, image);

      vk_address_binding_report(&instance->vk, &image->vk.base,
                                image->iova, image->total_size,
                                VK_DEVICE_ADDRESS_BINDING_TYPE_BIND_EXT);
   }

   return VK_SUCCESS;
}

static void
tu_get_image_memory_requirements(struct tu_device *dev, struct tu_image *image,
                                 VkMemoryRequirements2 *pMemoryRequirements)
{
   pMemoryRequirements->memoryRequirements = (VkMemoryRequirements) {
      .size = image->total_size,
      .alignment = image->layout[0].base_align,
      .memoryTypeBits = (1 << dev->physical_device->memory.type_count) - 1,
   };

   vk_foreach_struct(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *req =
            (VkMemoryDedicatedRequirements *) ext;
         req->requiresDedicatedAllocation =
            image->vk.external_handle_types != 0;
         req->prefersDedicatedAllocation = req->requiresDedicatedAllocation;
         break;
      }
      default:
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_GetImageMemoryRequirements2(VkDevice _device,
                               const VkImageMemoryRequirementsInfo2 *pInfo,
                               VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_image, image, pInfo->image);

   tu_get_image_memory_requirements(device, image, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetImageSparseMemoryRequirements2(
   VkDevice device,
   const VkImageSparseMemoryRequirementsInfo2 *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   tu_stub();
}

VKAPI_ATTR void VKAPI_CALL
tu_GetDeviceImageMemoryRequirements(
   VkDevice _device,
   const VkDeviceImageMemoryRequirements *pInfo,
   VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   struct tu_image image = {0};

   vk_image_init(&device->vk, &image.vk, pInfo->pCreateInfo);
   tu_image_init(device, &image, pInfo->pCreateInfo);
   TU_CALLX(device, tu_image_update_layout)(device, &image, DRM_FORMAT_MOD_INVALID, NULL);

   tu_get_image_memory_requirements(device, &image, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetDeviceImageSparseMemoryRequirements(
    VkDevice device,
    const VkDeviceImageMemoryRequirements *pInfo,
    uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   tu_stub();
}

static void
tu_get_image_subresource_layout(struct tu_image *image,
                                const VkImageSubresource2KHR *pSubresource,
                                VkSubresourceLayout2KHR *pLayout)
{
   struct fdl_layout *layout =
      &image->layout[tu6_plane_index(image->vk.format,
                                     pSubresource->imageSubresource.aspectMask)];
   const struct fdl_slice *slice = layout->slices +
      pSubresource->imageSubresource.mipLevel;

   pLayout->subresourceLayout.offset =
      fdl_surface_offset(layout, pSubresource->imageSubresource.mipLevel,
                         pSubresource->imageSubresource.arrayLayer);
   pLayout->subresourceLayout.rowPitch =
      fdl_pitch(layout, pSubresource->imageSubresource.mipLevel);
   pLayout->subresourceLayout.arrayPitch =
      fdl_layer_stride(layout, pSubresource->imageSubresource.mipLevel);
   pLayout->subresourceLayout.depthPitch = slice->size0;
   pLayout->subresourceLayout.size = slice->size0 * layout->depth0;

   VkSubresourceHostMemcpySizeEXT *memcpy_size =
      vk_find_struct(pLayout, SUBRESOURCE_HOST_MEMCPY_SIZE_EXT);
   if (memcpy_size) {
      memcpy_size->size = slice->size0;
   }

   if (fdl_ubwc_enabled(layout, pSubresource->imageSubresource.mipLevel)) {
      /* UBWC starts at offset 0 */
      pLayout->subresourceLayout.offset = 0;
      /* UBWC scanout won't match what the kernel wants if we have levels/layers */
      assert(image->vk.mip_levels == 1 && image->vk.array_layers == 1);
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_GetImageSubresourceLayout2KHR(VkDevice _device,
                                 VkImage _image,
                                 const VkImageSubresource2KHR *pSubresource,
                                 VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(tu_image, image, _image);

   tu_get_image_subresource_layout(image, pSubresource, pLayout);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetDeviceImageSubresourceLayoutKHR(VkDevice _device,
                                      const VkDeviceImageSubresourceInfoKHR *pInfo,
                                      VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(tu_device, device, _device);

   struct tu_image image = {0};

   vk_image_init(&device->vk, &image.vk, pInfo->pCreateInfo);
   tu_image_init(device, &image, pInfo->pCreateInfo);
   TU_CALLX(device, tu_image_update_layout)(device, &image, DRM_FORMAT_MOD_INVALID, NULL);

   tu_get_image_subresource_layout(&image, pInfo->pSubresource, pLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateImageView(VkDevice _device,
                   const VkImageViewCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkImageView *pView)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   struct tu_image_view *view;

   view = (struct tu_image_view *) vk_object_alloc(
      &device->vk, pAllocator, sizeof(*view), VK_OBJECT_TYPE_IMAGE_VIEW);
   if (view == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   tu_image_view_init(device, view, pCreateInfo, device->use_z24uint_s8uint);

   *pView = tu_image_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyImageView(VkDevice _device,
                    VkImageView _iview,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(tu_device, device, _device);
   VK_FROM_HANDLE(tu_image_view, iview, _iview);

   if (!iview)
      return;

   vk_object_free(&device->vk, pAllocator, iview);
}

/* Impelements the operations described in "Fragment Density Map Operations."
 */
void
tu_fragment_density_map_sample(const struct tu_image_view *fdm,
                               uint32_t x, uint32_t y,
                               uint32_t width, uint32_t height,
                               uint32_t layers,
                               struct tu_frag_area *areas)
{
   assert(fdm->image->layout[0].tile_mode == TILE6_LINEAR);

   uint32_t fdm_shift_x = util_logbase2_ceil(DIV_ROUND_UP(width, fdm->vk.extent.width));
   uint32_t fdm_shift_y = util_logbase2_ceil(DIV_ROUND_UP(height, fdm->vk.extent.height));

   fdm_shift_x = CLAMP(fdm_shift_x, MIN_FDM_TEXEL_SIZE_LOG2, MAX_FDM_TEXEL_SIZE_LOG2);
   fdm_shift_y = CLAMP(fdm_shift_y, MIN_FDM_TEXEL_SIZE_LOG2, MAX_FDM_TEXEL_SIZE_LOG2);

   uint32_t i = x >> fdm_shift_x;
   uint32_t j = y >> fdm_shift_y;

   unsigned cpp = fdm->image->layout[0].cpp;
   unsigned pitch = fdm->view.pitch;

   void *pixel = (char *)fdm->image->map + fdm->view.offset + cpp * i + pitch * j;
   for (unsigned i = 0; i < layers; i++) {
      float density_src[4], density[4];
      util_format_unpack_rgba(fdm->view.format, density_src, pixel, 1);
      pipe_swizzle_4f(density, density_src, fdm->swizzle);
      areas[i].width = 1.0f / density[0];
      areas[i].height = 1.0f / density[1];

      pixel = (char *)pixel + fdm->view.layer_size;
   }
}
