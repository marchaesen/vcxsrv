/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_META_H
#define PANVK_META_H

#include "panvk_image.h"
#include "panvk_mempool.h"

#include "vk_format.h"
#include "vk_meta.h"

enum panvk_meta_object_key_type {
   PANVK_META_OBJECT_KEY_BLEND_SHADER = VK_META_OBJECT_KEY_DRIVER_OFFSET,
   PANVK_META_OBJECT_KEY_COPY_DESC_SHADER,
   PANVK_META_OBJECT_KEY_FB_PRELOAD_SHADER,
};

static inline VkFormat
panvk_meta_get_uint_format_for_blk_size(unsigned blk_sz)
{
   switch (blk_sz) {
   case 1:
      return VK_FORMAT_R8_UINT;
   case 2:
      return VK_FORMAT_R16_UINT;
   case 3:
      return VK_FORMAT_R8G8B8_UINT;
   case 4:
      return VK_FORMAT_R32_UINT;
   case 6:
      return VK_FORMAT_R16G16B16_UINT;
   case 8:
      return VK_FORMAT_R32G32_UINT;
   case 12:
      return VK_FORMAT_R32G32B32_UINT;
   case 16:
      return VK_FORMAT_R32G32B32A32_UINT;
   default:
      return VK_FORMAT_UNDEFINED;
   }
}

static inline struct vk_meta_copy_image_properties
panvk_meta_copy_get_image_properties(struct panvk_image *img)
{
   uint64_t mod = img->pimage.layout.modifier;
   enum pipe_format pfmt = vk_format_to_pipe_format(img->vk.format);
   unsigned blk_sz = util_format_get_blocksize(pfmt);
   struct vk_meta_copy_image_properties props = {0};

   if (drm_is_afbc(mod)) {
      if (!vk_format_is_depth_or_stencil(img->vk.format)) {
         props.color.view_format = img->vk.format;
      } else {
         switch (img->vk.format) {
         case VK_FORMAT_D24_UNORM_S8_UINT:
            props.depth.view_format = VK_FORMAT_R8G8B8A8_UNORM;
            props.depth.component_mask = BITFIELD_MASK(3);
            props.stencil.view_format = VK_FORMAT_R8G8B8A8_UNORM;
            props.stencil.component_mask = BITFIELD_BIT(3);
            break;
         case VK_FORMAT_X8_D24_UNORM_PACK32:
            props.depth.view_format = VK_FORMAT_R8G8B8A8_UNORM;
            props.depth.component_mask = BITFIELD_MASK(3);
            break;
         case VK_FORMAT_D16_UNORM:
            props.depth.view_format = VK_FORMAT_R8G8_UNORM;
            props.depth.component_mask = BITFIELD_MASK(2);
            break;
         default:
            assert(!"Invalid ZS format");
            break;
         }
      }
   } else if (vk_format_is_depth_or_stencil(img->vk.format)) {
      switch (img->vk.format) {
      case VK_FORMAT_S8_UINT:
         props.stencil.view_format = VK_FORMAT_R8_UINT;
         props.stencil.component_mask = BITFIELD_MASK(1);
         break;
      case VK_FORMAT_D24_UNORM_S8_UINT:
         props.depth.view_format = VK_FORMAT_R8G8B8A8_UINT;
         props.depth.component_mask = BITFIELD_MASK(3);
         props.stencil.view_format = VK_FORMAT_R8G8B8A8_UINT;
         props.stencil.component_mask = BITFIELD_BIT(3);
         break;
      case VK_FORMAT_X8_D24_UNORM_PACK32:
         props.depth.view_format = VK_FORMAT_R8G8B8A8_UINT;
         props.depth.component_mask = BITFIELD_MASK(3);
         break;
      case VK_FORMAT_D32_SFLOAT_S8_UINT:
         props.depth.view_format = VK_FORMAT_R32G32_UINT;
         props.depth.component_mask = BITFIELD_BIT(0);
         props.stencil.view_format = VK_FORMAT_R32G32_UINT;
         props.stencil.component_mask = BITFIELD_BIT(1);
         break;
      case VK_FORMAT_D16_UNORM:
         props.depth.view_format = VK_FORMAT_R16_UINT;
         props.depth.component_mask = BITFIELD_BIT(0);
         break;
      case VK_FORMAT_D32_SFLOAT:
         props.depth.view_format = VK_FORMAT_R32_UINT;
         props.depth.component_mask = BITFIELD_BIT(0);
         break;
      default:
         assert(!"Invalid ZS format");
         break;
      }
   } else {
      props.color.view_format = panvk_meta_get_uint_format_for_blk_size(blk_sz);
   }

   if (mod == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED ||
       drm_is_afbc(mod)) {
      props.tile_size.width = 16;
      props.tile_size.height = 16;
      props.tile_size.depth = 1;
   } else {
      /* When linear, pretend we have a 1D-tile so we end up with a <64,1,1>
       * workgroup. */
      props.tile_size.width = 64;
      props.tile_size.height = 1;
      props.tile_size.depth = 1;
   }

   return props;
}

#if defined(PAN_ARCH) && PAN_ARCH <= 7
struct panvk_cmd_buffer;
struct panvk_descriptor_state;
struct panvk_device;
struct panvk_shader;
struct panvk_shader_desc_state;

VkResult panvk_per_arch(meta_get_copy_desc_job)(
   struct panvk_cmd_buffer *cmdbuf, const struct panvk_shader *shader,
   const struct panvk_descriptor_state *desc_state,
   const struct panvk_shader_desc_state *shader_desc_state,
   uint32_t attrib_buf_idx_offset, struct panfrost_ptr *job_desc);
#endif

#endif
