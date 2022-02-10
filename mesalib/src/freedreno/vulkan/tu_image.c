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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"
#include "fdl/fd6_format_table.h"

#include "util/debug.h"
#include "util/u_atomic.h"
#include "util/format/u_format.h"
#include "vk_format.h"
#include "vk_util.h"
#include "drm-uapi/drm_fourcc.h"

#include "tu_cs.h"

static uint32_t
tu6_plane_count(VkFormat format)
{
   switch (format) {
   default:
      return 1;
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return 2;
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      return 3;
   }
}

enum pipe_format
tu6_plane_format(VkFormat format, uint32_t plane)
{
   switch (format) {
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      return plane ? PIPE_FORMAT_R8G8_UNORM : PIPE_FORMAT_Y8_UNORM;
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      return PIPE_FORMAT_R8_UNORM;
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return plane ? PIPE_FORMAT_S8_UINT : PIPE_FORMAT_Z32_FLOAT;
   default:
      return tu_vk_format_to_pipe_format(format);
   }
}

uint32_t
tu6_plane_index(VkFormat format, VkImageAspectFlags aspect_mask)
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

enum pipe_format
tu_format_for_aspect(enum pipe_format format, VkImageAspectFlags aspect_mask)
{
   switch (format) {
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
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

void
tu_cs_image_ref(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer)
{
   tu_cs_emit(cs, iview->PITCH);
   tu_cs_emit(cs, iview->layer_size >> 6);
   tu_cs_emit_qw(cs, iview->base_addr + iview->layer_size * layer);
}

void
tu_cs_image_stencil_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu_cs_emit(cs, iview->stencil_PITCH);
   tu_cs_emit(cs, iview->stencil_layer_size >> 6);
   tu_cs_emit_qw(cs, iview->stencil_base_addr + iview->stencil_layer_size * layer);
}

void
tu_cs_image_ref_2d(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer, bool src)
{
   tu_cs_emit_qw(cs, iview->base_addr + iview->layer_size * layer);
   /* SP_PS_2D_SRC_PITCH has shifted pitch field */
   tu_cs_emit(cs, iview->PITCH << (src ? 9 : 0));
}

void
tu_cs_image_flag_ref(struct tu_cs *cs, const struct fdl6_view *iview, uint32_t layer)
{
   tu_cs_emit_qw(cs, iview->ubwc_addr + iview->ubwc_layer_size * layer);
   tu_cs_emit(cs, iview->FLAG_BUFFER_PITCH);
}

void
tu_image_view_init(struct tu_image_view *iview,
                   const VkImageViewCreateInfo *pCreateInfo,
                   bool has_z24uint_s8uint)
{
   TU_FROM_HANDLE(tu_image, image, pCreateInfo->image);
   const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;
   VkFormat vk_format = pCreateInfo->format;
   VkImageAspectFlagBits aspect_mask = pCreateInfo->subresourceRange.aspectMask;

   const struct VkSamplerYcbcrConversionInfo *ycbcr_conversion =
      vk_find_struct_const(pCreateInfo->pNext, SAMPLER_YCBCR_CONVERSION_INFO);
   const struct tu_sampler_ycbcr_conversion *conversion = ycbcr_conversion ?
      tu_sampler_ycbcr_conversion_from_handle(ycbcr_conversion->conversion) : NULL;

   iview->image = image;

   const struct fdl_layout *layouts[3];

   layouts[0] = &image->layout[tu6_plane_index(image->vk_format, aspect_mask)];

   enum pipe_format format;
   if (aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT)
      format = tu6_plane_format(vk_format, tu6_plane_index(vk_format, aspect_mask));
   else
      format = tu_vk_format_to_pipe_format(vk_format);

   if (image->vk_format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM &&
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
       (vk_format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM ||
        vk_format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM)) {
      layouts[1] = &image->layout[1];
      layouts[2] = &image->layout[2];
   }

   struct fdl_view_args args = {};
   args.iova = image->iova;
   args.base_array_layer = range->baseArrayLayer;
   args.base_miplevel = range->baseMipLevel;
   args.layer_count = tu_get_layerCount(image, range);
   args.level_count = tu_get_levelCount(image, range);
   args.format = tu_format_for_aspect(format, aspect_mask);
   vk_component_mapping_to_pipe_swizzle(pCreateInfo->components, args.swiz);
   if (conversion) {
      unsigned char conversion_swiz[4], create_swiz[4];
      memcpy(create_swiz, args.swiz, sizeof(create_swiz));
      vk_component_mapping_to_pipe_swizzle(conversion->components,
                                           conversion_swiz);
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
      args.chroma_offsets[0] = (enum fdl_chroma_location) conversion->chroma_offsets[0];
      args.chroma_offsets[1] = (enum fdl_chroma_location) conversion->chroma_offsets[1];
   }

   fdl6_view_init(&iview->view, layouts, &args, has_z24uint_s8uint);

   if (image->vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      struct fdl_layout *layout = &image->layout[1];
      iview->stencil_base_addr = image->iova +
         fdl_surface_offset(layout, range->baseMipLevel, range->baseArrayLayer);
      iview->stencil_layer_size = fdl_layer_stride(layout, range->baseMipLevel);
      iview->stencil_PITCH = A6XX_RB_STENCIL_BUFFER_PITCH(fdl_pitch(layout, range->baseMipLevel)).value;
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

bool
ubwc_possible(VkFormat format, VkImageType type, VkImageUsageFlags usage,
              VkImageUsageFlags stencil_usage, const struct fd_dev_info *info,
              VkSampleCountFlagBits samples)
{
   /* no UBWC with compressed formats, E5B9G9R9, S8_UINT
    * (S8_UINT because separate stencil doesn't have UBWC-enable bit)
    */
   if (vk_format_is_compressed(format) ||
       format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 ||
       format == VK_FORMAT_S8_UINT)
      return false;

   if (!info->a6xx.has_8bpp_ubwc &&
       (format == VK_FORMAT_R8_UNORM ||
        format == VK_FORMAT_R8_SNORM ||
        format == VK_FORMAT_R8_UINT ||
        format == VK_FORMAT_R8_SINT ||
        format == VK_FORMAT_R8_SRGB))
      return false;

   if (type == VK_IMAGE_TYPE_3D) {
      tu_finishme("UBWC with 3D textures");
      return false;
   }

   /* Disable UBWC for storage images.
    *
    * The closed GL driver skips UBWC for storage images (and additionally
    * uses linear for writeonly images).  We seem to have image tiling working
    * in freedreno in general, so turnip matches that.  freedreno also enables
    * UBWC on images, but it's not really tested due to the lack of
    * UBWC-enabled mipmaps in freedreno currently.  Just match the closed GL
    * behavior of no UBWC.
   */
   if ((usage | stencil_usage) & VK_IMAGE_USAGE_STORAGE_BIT)
      return false;

   /* Disable UBWC for D24S8 on A630 in some cases
    *
    * VK_IMAGE_ASPECT_STENCIL_BIT image view requires to be able to sample
    * from the stencil component as UINT, however no format allows this
    * on a630 (the special FMT6_Z24_UINT_S8_UINT format is missing)
    *
    * It must be sampled as FMT6_8_8_8_8_UINT, which is not UBWC-compatible
    *
    * Additionally, the special AS_R8G8B8A8 format is broken without UBWC,
    * so we have to fallback to 8_8_8_8_UNORM when UBWC is disabled
    */
   if (!info->a6xx.has_z24uint_s8uint &&
       format == VK_FORMAT_D24_UNORM_S8_UINT &&
       (stencil_usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)))
      return false;

   if (!info->a6xx.has_z24uint_s8uint && samples > VK_SAMPLE_COUNT_1_BIT)
      return false;

   return true;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateImage(VkDevice _device,
               const VkImageCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *alloc,
               VkImage *pImage)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   uint64_t modifier = DRM_FORMAT_MOD_INVALID;
   const VkSubresourceLayout *plane_layouts = NULL;
   struct tu_image *image;

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

#ifdef ANDROID
   const VkNativeBufferANDROID *gralloc_info =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);
   int dma_buf;
   if (gralloc_info) {
      VkResult result = tu_gralloc_info(device, gralloc_info, &dma_buf, &modifier);
      if (result != VK_SUCCESS)
         return result;
   }
#endif

   image = vk_object_zalloc(&device->vk, alloc, sizeof(*image),
                            VK_OBJECT_TYPE_IMAGE);
   if (!image)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   const VkExternalMemoryImageCreateInfo *external_info =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
   image->shareable = external_info != NULL;

   image->vk_format = pCreateInfo->format;
   image->level_count = pCreateInfo->mipLevels;
   image->layer_count = pCreateInfo->arrayLayers;

   enum a6xx_tile_mode tile_mode = TILE6_3;
   bool ubwc_enabled =
      !(device->physical_device->instance->debug_flags & TU_DEBUG_NOUBWC);

   /* use linear tiling if requested */
   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR || modifier == DRM_FORMAT_MOD_LINEAR) {
      tile_mode = TILE6_LINEAR;
      ubwc_enabled = false;
   }

   /* Force linear tiling for formats with "fake" optimalTilingFeatures */
   if (!tiling_possible(image->vk_format)) {
      tile_mode = TILE6_LINEAR;
      ubwc_enabled = false;
   }

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
       !vk_format_is_depth_or_stencil(image->vk_format)) {
      const VkImageFormatListCreateInfo *fmt_list =
         vk_find_struct_const(pCreateInfo->pNext, IMAGE_FORMAT_LIST_CREATE_INFO);
      bool may_be_swapped = true;
      if (fmt_list) {
         may_be_swapped = false;
         for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
            if (tu6_format_texture(tu_vk_format_to_pipe_format(fmt_list->pViewFormats[i]), TILE6_LINEAR).swap) {
               may_be_swapped = true;
               break;
            }
         }
      }
      if (may_be_swapped)
         tile_mode = TILE6_LINEAR;
      ubwc_enabled = false;
   }

   const VkImageStencilUsageCreateInfo *stencil_usage_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_STENCIL_USAGE_CREATE_INFO);

   if (!ubwc_possible(image->vk_format, pCreateInfo->imageType, pCreateInfo->usage,
                      stencil_usage_info ? stencil_usage_info->stencilUsage : pCreateInfo->usage,
                      device->physical_device->info, pCreateInfo->samples))
      ubwc_enabled = false;

   /* expect UBWC enabled if we asked for it */
   assert(modifier != DRM_FORMAT_MOD_QCOM_COMPRESSED || ubwc_enabled);

   for (uint32_t i = 0; i < tu6_plane_count(image->vk_format); i++) {
      struct fdl_layout *layout = &image->layout[i];
      enum pipe_format format = tu6_plane_format(image->vk_format, i);
      uint32_t width0 = pCreateInfo->extent.width;
      uint32_t height0 = pCreateInfo->extent.height;

      if (i > 0) {
         switch (image->vk_format) {
         case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
         case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
            /* half width/height on chroma planes */
            width0 = (width0 + 1) >> 1;
            height0 = (height0 + 1) >> 1;
            break;
         case VK_FORMAT_D32_SFLOAT_S8_UINT:
            /* no UBWC for separate stencil */
            ubwc_enabled = false;
            break;
         default:
            break;
         }
      }

      struct fdl_explicit_layout plane_layout;

      if (plane_layouts) {
         /* only expect simple 2D images for now */
         if (pCreateInfo->mipLevels != 1 ||
            pCreateInfo->arrayLayers != 1 ||
            pCreateInfo->extent.depth != 1)
            goto invalid_layout;

         plane_layout.offset = plane_layouts[i].offset;
         plane_layout.pitch = plane_layouts[i].rowPitch;
         /* note: use plane_layouts[0].arrayPitch to support array formats */
      }

      layout->tile_mode = tile_mode;
      layout->ubwc = ubwc_enabled;

      if (!fdl6_layout(layout, format,
                       pCreateInfo->samples,
                       width0, height0,
                       pCreateInfo->extent.depth,
                       pCreateInfo->mipLevels,
                       pCreateInfo->arrayLayers,
                       pCreateInfo->imageType == VK_IMAGE_TYPE_3D,
                       plane_layouts ? &plane_layout : NULL)) {
         assert(plane_layouts); /* can only fail with explicit layout */
         goto invalid_layout;
      }

      /* fdl6_layout can't take explicit offset without explicit pitch
       * add offset manually for extra layouts for planes
       */
      if (!plane_layouts && i > 0) {
         uint32_t offset = ALIGN_POT(image->total_size, 4096);
         for (int i = 0; i < pCreateInfo->mipLevels; i++) {
            layout->slices[i].offset += offset;
            layout->ubwc_slices[i].offset += offset;
         }
         layout->size += offset;
      }

      image->total_size = MAX2(image->total_size, layout->size);
   }

   const struct util_format_description *desc = util_format_description(image->layout[0].format);
   if (util_format_has_depth(desc) && !(device->instance->debug_flags & TU_DEBUG_NOLRZ))
   {
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
      unsigned lrz_size = lrz_pitch * lrz_height * 2;
      image->total_size += lrz_size;
   }

   *pImage = tu_image_to_handle(image);

#ifdef ANDROID
   if (gralloc_info)
      return tu_import_memory_from_gralloc_handle(_device, dma_buf, alloc, *pImage);
#endif
   return VK_SUCCESS;

invalid_layout:
   vk_object_free(&device->vk, alloc, image);
   return vk_error(device, VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyImage(VkDevice _device,
                VkImage _image,
                const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_image, image, _image);

   if (!image)
      return;

#ifdef ANDROID
   if (image->owned_memory != VK_NULL_HANDLE)
      tu_FreeMemory(_device, image->owned_memory, pAllocator);
#endif

   vk_object_free(&device->vk, pAllocator, image);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetImageSubresourceLayout(VkDevice _device,
                             VkImage _image,
                             const VkImageSubresource *pSubresource,
                             VkSubresourceLayout *pLayout)
{
   TU_FROM_HANDLE(tu_image, image, _image);

   struct fdl_layout *layout =
      &image->layout[tu6_plane_index(image->vk_format, pSubresource->aspectMask)];
   const struct fdl_slice *slice = layout->slices + pSubresource->mipLevel;

   pLayout->offset =
      fdl_surface_offset(layout, pSubresource->mipLevel, pSubresource->arrayLayer);
   pLayout->rowPitch = fdl_pitch(layout, pSubresource->mipLevel);
   pLayout->arrayPitch = fdl_layer_stride(layout, pSubresource->mipLevel);
   pLayout->depthPitch = slice->size0;
   pLayout->size = pLayout->depthPitch * layout->depth0;

   if (fdl_ubwc_enabled(layout, pSubresource->mipLevel)) {
      /* UBWC starts at offset 0 */
      pLayout->offset = 0;
      /* UBWC scanout won't match what the kernel wants if we have levels/layers */
      assert(image->level_count == 1 && image->layer_count == 1);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetImageDrmFormatModifierPropertiesEXT(
    VkDevice                                    device,
    VkImage                                     _image,
    VkImageDrmFormatModifierPropertiesEXT*      pProperties)
{
   TU_FROM_HANDLE(tu_image, image, _image);

   /* TODO invent a modifier for tiled but not UBWC buffers */

   if (!image->layout[0].tile_mode)
      pProperties->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
   else if (image->layout[0].ubwc_layer_size)
      pProperties->drmFormatModifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
   else
      pProperties->drmFormatModifier = DRM_FORMAT_MOD_INVALID;

   return VK_SUCCESS;
}


VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateImageView(VkDevice _device,
                   const VkImageViewCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkImageView *pView)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_image_view *view;

   view = vk_object_alloc(&device->vk, pAllocator, sizeof(*view),
                          VK_OBJECT_TYPE_IMAGE_VIEW);
   if (view == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   tu_image_view_init(view, pCreateInfo, device->physical_device->info->a6xx.has_z24uint_s8uint);

   *pView = tu_image_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyImageView(VkDevice _device,
                    VkImageView _iview,
                    const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_image_view, iview, _iview);

   if (!iview)
      return;

   vk_object_free(&device->vk, pAllocator, iview);
}

void
tu_buffer_view_init(struct tu_buffer_view *view,
                    struct tu_device *device,
                    const VkBufferViewCreateInfo *pCreateInfo)
{
   TU_FROM_HANDLE(tu_buffer, buffer, pCreateInfo->buffer);

   view->buffer = buffer;

   uint32_t range;
   if (pCreateInfo->range == VK_WHOLE_SIZE)
      range = buffer->size - pCreateInfo->offset;
   else
      range = pCreateInfo->range;

   uint8_t swiz[4] = { PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z,
                       PIPE_SWIZZLE_W };

   fdl6_buffer_view_init(
      view->descriptor, tu_vk_format_to_pipe_format(pCreateInfo->format),
      swiz, buffer->iova + pCreateInfo->offset, range);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateBufferView(VkDevice _device,
                    const VkBufferViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkBufferView *pView)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_buffer_view *view;

   view = vk_object_alloc(&device->vk, pAllocator, sizeof(*view),
                          VK_OBJECT_TYPE_BUFFER_VIEW);
   if (!view)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   tu_buffer_view_init(view, device, pCreateInfo);

   *pView = tu_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyBufferView(VkDevice _device,
                     VkBufferView bufferView,
                     const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_buffer_view, view, bufferView);

   if (!view)
      return;

   vk_object_free(&device->vk, pAllocator, view);
}
