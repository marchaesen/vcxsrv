/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "vk_format.h"
#include "vk_log.h"

#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_image_view.h"
#include "panvk_priv_bo.h"

#include "genxml/gen_macros.h"

static enum mali_texture_dimension
panvk_view_type_to_mali_tex_dim(VkImageViewType type)
{
   switch (type) {
   case VK_IMAGE_VIEW_TYPE_1D:
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      return MALI_TEXTURE_DIMENSION_1D;
   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
      return MALI_TEXTURE_DIMENSION_2D;
   case VK_IMAGE_VIEW_TYPE_3D:
      return MALI_TEXTURE_DIMENSION_3D;
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      return MALI_TEXTURE_DIMENSION_CUBE;
   default:
      unreachable("Invalid view type");
   }
}

static void
panvk_convert_swizzle(const VkComponentMapping *in, unsigned char *out)
{
   const VkComponentSwizzle *comp = &in->r;
   for (unsigned i = 0; i < 4; i++) {
      switch (comp[i]) {
      case VK_COMPONENT_SWIZZLE_ZERO:
         out[i] = PIPE_SWIZZLE_0;
         break;
      case VK_COMPONENT_SWIZZLE_ONE:
         out[i] = PIPE_SWIZZLE_1;
         break;
      case VK_COMPONENT_SWIZZLE_R:
         out[i] = PIPE_SWIZZLE_X;
         break;
      case VK_COMPONENT_SWIZZLE_G:
         out[i] = PIPE_SWIZZLE_Y;
         break;
      case VK_COMPONENT_SWIZZLE_B:
         out[i] = PIPE_SWIZZLE_Z;
         break;
      case VK_COMPONENT_SWIZZLE_A:
         out[i] = PIPE_SWIZZLE_W;
         break;
      default:
         unreachable("Invalid swizzle");
      }
   }
}

static VkResult
prepare_tex_descs(struct panvk_image_view *view)
{
   /* Use a temporary pan_image_view so we can tweak it for texture
    * descriptor emission without changing the original definition.
    */
   struct pan_image_view pview = view->pview;
   struct panvk_image *image =
      container_of(view->vk.image, struct panvk_image, vk);
   struct panvk_device *dev = to_panvk_device(view->vk.base.device);
   bool can_preload_other_aspect =
      (view->vk.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) &&
      (image->vk.format == VK_FORMAT_D24_UNORM_S8_UINT ||
       (image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT &&
        view->vk.aspects ==
           (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)));

   if (util_format_is_depth_or_stencil(view->pview.format)) {
      /* Vulkan wants R001, where the depth/stencil is stored in the red
       * component. Tweak the swizzle so we get what Vulkan wants.
       */
      static const unsigned char r001[4] = {
         PIPE_SWIZZLE_X,
         PIPE_SWIZZLE_0,
         PIPE_SWIZZLE_0,
         PIPE_SWIZZLE_1,
      };

      util_format_compose_swizzles(r001, view->pview.swizzle, pview.swizzle);
   }
#if PAN_ARCH == 7
   /* v7 requires AFBC reswizzle. */
   else if (!panfrost_format_is_yuv(view->pview.format) &&
            panfrost_format_supports_afbc(PAN_ARCH, view->pview.format))
      GENX(panfrost_texture_afbc_reswizzle)(&pview);
#endif

   /* If the view contains both stencil and depth, we need to keep only the
    * depth. We'll create another texture with only the stencil.
    */
   if (pview.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)
      pview.format = PIPE_FORMAT_Z32_FLOAT;

   uint32_t plane_count = vk_format_get_plane_count(view->vk.format);
   uint32_t tex_payload_size =
      GENX(panfrost_estimate_texture_payload_size)(&pview);

   struct panvk_pool_alloc_info alloc_info = {
#if PAN_ARCH == 6
      .alignment = pan_alignment(SURFACE_WITH_STRIDE),
#elif PAN_ARCH == 7
      .alignment = (plane_count > 1)
                      ? pan_alignment(MULTIPLANAR_SURFACE)
                      : pan_alignment(SURFACE_WITH_STRIDE),
#else
      .alignment = pan_alignment(PLANE) * (plane_count > 1 ? 2 : 1),
#endif

      .size = tex_payload_size * (can_preload_other_aspect ? 2 : plane_count),
   };

   view->mem = panvk_pool_alloc_mem(&dev->mempools.rw, alloc_info);
   if (!panvk_priv_mem_host_addr(view->mem))
      return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   struct panfrost_ptr ptr = {
      .gpu = panvk_priv_mem_dev_addr(view->mem),
      .cpu = panvk_priv_mem_host_addr(view->mem),
   };

   if (plane_count > 1) {
      memset(pview.planes, 0, sizeof(pview.planes));

      for (uint32_t plane = 0; plane < plane_count; plane++) {
         VkFormat plane_format =
            vk_format_get_plane_format(view->vk.view_format, plane);

         /* We need a per-plane pview. */
         pview.planes[0] = view->pview.planes[plane];
         pview.format = vk_format_to_pipe_format(plane_format);

         GENX(panfrost_new_texture)(&pview, &view->descs.tex[plane], &ptr);

         ptr.cpu += tex_payload_size;
         ptr.gpu += tex_payload_size;
      }
   } else {
      GENX(panfrost_new_texture)(&pview, &view->descs.tex[0], &ptr);
   }

   if (!can_preload_other_aspect)
      return VK_SUCCESS;

   switch (pview.format) {
   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      pview.format = PIPE_FORMAT_X24S8_UINT;
      break;
   case PIPE_FORMAT_X24S8_UINT:
      pview.format = PIPE_FORMAT_Z24X8_UNORM;
      break;
   case PIPE_FORMAT_Z32_FLOAT:
      pview.format = PIPE_FORMAT_S8_UINT;
      break;
   case PIPE_FORMAT_S8_UINT:
      pview.format = PIPE_FORMAT_Z32_FLOAT;
      break;
   default:
      assert(!"Invalid format");
   }

   ptr.cpu += tex_payload_size;
   ptr.gpu += tex_payload_size;

   GENX(panfrost_new_texture)(&pview, &view->descs.zs.other_aspect_tex, &ptr);
   return VK_SUCCESS;
}

#if PAN_ARCH <= 7
static void
prepare_attr_buf_descs(struct panvk_image_view *view)
{
   struct panvk_image *image =
      container_of(view->vk.image, struct panvk_image, vk);
   unsigned plane_idx = 0;

   /* Stencil is on plane 1 in a D32_S8 image. The special color case is for
    * vk_meta copies which create color views of depth/stencil images. In
    * that case, we base the stencil vs depth detection on the format block
    * size.
    */
   if (image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT &&
       (view->vk.aspects == VK_IMAGE_ASPECT_STENCIL_BIT ||
        (view->vk.aspects == VK_IMAGE_ASPECT_COLOR_BIT &&
         vk_format_get_blocksize(view->vk.view_format) == 1)))
      plane_idx = 1;

   bool is_3d =
      image->planes[plane_idx].layout.dim == MALI_TEXTURE_DIMENSION_3D;
   unsigned offset = image->planes[plane_idx].data.offset;
   offset += panfrost_texture_offset(
      &image->planes[plane_idx].layout, view->pview.first_level,
      is_3d ? 0 : view->pview.first_layer, is_3d ? view->pview.first_layer : 0);

   pan_pack(&view->descs.img_attrib_buf[0], ATTRIBUTE_BUFFER, cfg) {
      /* The format is the only thing we lack to emit attribute descriptors
       * when copying from the set to the attribute tables. Instead of
       * making the descriptor size to store an extra format, we pack
       * the 22-bit format with the texel stride, which is expected to be
       * fit in remaining 10 bits.
       */
      uint32_t fmt_blksize = util_format_get_blocksize(view->pview.format);
      uint32_t hw_fmt =
         GENX(panfrost_format_from_pipe_format)(view->pview.format)->hw;

      assert(fmt_blksize < BITFIELD_MASK(10));
      assert(hw_fmt < BITFIELD_MASK(22));

      cfg.type = image->vk.drm_format_mod == DRM_FORMAT_MOD_LINEAR
                    ? MALI_ATTRIBUTE_TYPE_3D_LINEAR
                    : MALI_ATTRIBUTE_TYPE_3D_INTERLEAVED;
      cfg.pointer = image->planes[plane_idx].data.base + offset;
      cfg.stride = fmt_blksize | (hw_fmt << 10);
      cfg.size = pan_kmod_bo_size(image->bo) - offset;
   }

   struct mali_attribute_buffer_packed *buf = &view->descs.img_attrib_buf[1];
   pan_cast_and_pack(buf, ATTRIBUTE_BUFFER_CONTINUATION_3D, cfg) {
      unsigned level = view->pview.first_level;
      VkExtent3D extent = view->vk.extent;

      cfg.s_dimension = extent.width;
      cfg.t_dimension = extent.height;
      cfg.r_dimension =
         view->pview.dim == MALI_TEXTURE_DIMENSION_3D
            ? extent.depth
            : (view->pview.last_layer - view->pview.first_layer + 1);
      cfg.row_stride = image->planes[plane_idx].layout.slices[level].row_stride;
      if (cfg.r_dimension > 1) {
         cfg.slice_stride =
            panfrost_get_layer_stride(&image->planes[plane_idx].layout, level);
      }
   }
}
#endif

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateImageView)(VkDevice _device,
                                const VkImageViewCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkImageView *pView)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_image, image, pCreateInfo->image);
   bool driver_internal =
      (pCreateInfo->flags & VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA) != 0;
   struct panvk_image_view *view;
   VkResult result;

   view = vk_image_view_create(&device->vk, driver_internal, pCreateInfo,
                               pAllocator, sizeof(*view));
   if (view == NULL)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   view->pview = (struct pan_image_view){
      .format = vk_format_to_pipe_format(view->vk.view_format),
      .dim = panvk_view_type_to_mali_tex_dim(view->vk.view_type),
      .nr_samples = image->vk.samples,
      .first_level = view->vk.base_mip_level,
      /* MIPmapping in YUV formats is not supported by the HW. */
      .last_level = vk_format_get_ycbcr_info(view->vk.format)
         ? view->vk.base_mip_level
         : view->vk.base_mip_level + view->vk.level_count - 1,
      .first_layer = view->vk.base_array_layer,
      .last_layer = view->vk.base_array_layer + view->vk.layer_count - 1,
   };
   panvk_convert_swizzle(&view->vk.swizzle, view->pview.swizzle);

   u_foreach_bit(aspect_bit, view->vk.aspects) {
      uint8_t image_plane =
         panvk_plane_index(image->vk.format, 1u << aspect_bit);

      /* Place the view plane at index 0 for single-plane views of multiplane
       * formats. Does not apply to YCbCr views of multiplane images since
       * view->vk.aspects for those will contain the full set of plane aspects.
       */
      uint8_t view_plane = (view->vk.aspects == VK_IMAGE_ASPECT_PLANE_1_BIT ||
                            view->vk.aspects == VK_IMAGE_ASPECT_PLANE_2_BIT) ?
                           0 : image_plane;
      view->pview.planes[view_plane] = &image->planes[image_plane];
   }

   /* Depth/stencil are viewed as color for copies. */
   if (view->vk.aspects == VK_IMAGE_ASPECT_COLOR_BIT &&
       image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT &&
       vk_format_get_blocksize(view->vk.view_format) == 1) {
      view->pview.planes[0] = &image->planes[1];
   }

   /* We need to patch the view format when the image contains both
    * depth and stencil but the view only contains one of these components, so
    * we can ignore the component we don't use.
    */
   if (view->vk.view_format == VK_FORMAT_S8_UINT &&
       image->vk.format == VK_FORMAT_D24_UNORM_S8_UINT)
      view->pview.format = PIPE_FORMAT_X24S8_UINT;

   /* Attachments need a texture for the FB preload logic. */
   VkImageUsageFlags tex_usage_mask =
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

#if PAN_ARCH >= 9
   /* Valhall passes a texture descriptor to LEA_TEX. */
   tex_usage_mask |= VK_IMAGE_USAGE_STORAGE_BIT;
#endif

   if (view->vk.usage & tex_usage_mask) {
      result = prepare_tex_descs(view);
      if (result != VK_SUCCESS)
         goto err_destroy_iview;
   }

#if PAN_ARCH <= 7
   if (view->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT)
      prepare_attr_buf_descs(view);
#endif

   *pView = panvk_image_view_to_handle(view);
   return VK_SUCCESS;

err_destroy_iview:
   vk_image_view_destroy(&device->vk, pAllocator, &view->vk);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroyImageView)(VkDevice _device, VkImageView _view,
                                 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_image_view, view, _view);

   if (!view)
      return;

   panvk_pool_free_mem(&view->mem);
   vk_image_view_destroy(&device->vk, pAllocator, &view->vk);
}
