/*
 * Copyright © 2023 Collabora Ltd.
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

#include "nir/nir_builder.h"
#include "nir/nir_format_convert.h"

#include "vk_buffer.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"
#include "vk_device.h"
#include "vk_format.h"
#include "vk_meta.h"
#include "vk_meta_private.h"
#include "vk_physical_device.h"
#include "vk_pipeline.h"

#include "util/format/u_format.h"

struct vk_meta_fill_buffer_key {
   enum vk_meta_object_key_type key_type;
};

struct vk_meta_copy_buffer_key {
   enum vk_meta_object_key_type key_type;

   uint32_t chunk_size;
};

struct vk_meta_copy_image_view {
   VkImageViewType type;

   union {
      struct {
         VkFormat format;
      } color;
      struct {
         struct {
            VkFormat format;
            nir_component_mask_t component_mask;
         } depth, stencil;
      };
   };
};

struct vk_meta_copy_buffer_image_key {
   enum vk_meta_object_key_type key_type;

   VkPipelineBindPoint bind_point;

   struct {
      struct vk_meta_copy_image_view view;

      VkImageAspectFlagBits aspect;
   } img;

   uint32_t wg_size[3];
};

struct vk_meta_copy_image_key {
   enum vk_meta_object_key_type key_type;

   VkPipelineBindPoint bind_point;

   /* One source per-aspect being copied. */
   struct {
      struct vk_meta_copy_image_view view;
   } src, dst;

   VkImageAspectFlagBits aspects;
   VkSampleCountFlagBits samples;

   uint32_t wg_size[3];
};

#define load_info(__b, __type, __field_name)                                   \
   nir_load_push_constant((__b), 1,                                            \
                          sizeof(((__type *)NULL)->__field_name) * 8,          \
                          nir_imm_int(b, offsetof(__type, __field_name)))

struct vk_meta_fill_buffer_info {
   uint64_t buf_addr;
   uint32_t data;
   uint32_t size;
};

struct vk_meta_copy_buffer_info {
   uint64_t src_addr;
   uint64_t dst_addr;
   uint32_t size;
};

struct vk_meta_copy_buffer_image_info {
   struct {
      uint64_t addr;
      uint32_t row_stride;
      uint32_t image_stride;
   } buf;

   struct {
      struct {
         uint32_t x, y, z;
      } offset;
   } img;

   /* Workgroup size should be selected based on the image tile size. This
    * means we can issue threads outside the image area we want to copy
    * from/to. This field encodes the copy IDs that should be skipped, and
    * also serve as an adjustment for the buffer/image coordinates. */
   struct {
      struct {
         uint32_t x, y, z;
      } start, end;
   } copy_id_range;
};

struct vk_meta_copy_image_fs_info {
   struct {
      int32_t x, y, z;
   } dst_to_src_offs;
};

struct vk_meta_copy_image_cs_info {
   struct {
      struct {
         uint32_t x, y, z;
      } offset;
   } src_img, dst_img;

   /* Workgroup size should be selected based on the image tile size. This
    * means we can issue threads outside the image area we want to copy
    * from/to. This field encodes the copy IDs that should be skipped, and
    * also serve as an adjustment for the buffer/image coordinates. */
   struct {
      struct {
         uint32_t x, y, z;
      } start, end;
   } copy_id_range;
};

static VkOffset3D
base_layer_as_offset(VkImageViewType view_type, VkOffset3D offset,
                     uint32_t base_layer)
{
   switch (view_type) {
   case VK_IMAGE_VIEW_TYPE_1D:
      return (VkOffset3D){
         .x = offset.x,
      };

   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      return (VkOffset3D){
         .x = offset.x,
         .y = base_layer,
      };

   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      return (VkOffset3D){
         .x = offset.x,
         .y = offset.y,
         .z = base_layer,
      };

   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_3D:
      return offset;

   default:
      assert(!"Invalid view type");
      return (VkOffset3D){0};
   }
}

static VkExtent3D
layer_count_as_extent(VkImageViewType view_type, VkExtent3D extent,
                      uint32_t layer_count)
{
   switch (view_type) {
   case VK_IMAGE_VIEW_TYPE_1D:
      return (VkExtent3D){
         .width = extent.width,
         .height = 1,
         .depth = 1,
      };

   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      return (VkExtent3D){
         .width = extent.width,
         .height = layer_count,
         .depth = 1,
      };

   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      return (VkExtent3D){
         .width = extent.width,
         .height = extent.height,
         .depth = layer_count,
      };

   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_3D:
      return extent;

   default:
      assert(!"Invalid view type");
      return (VkExtent3D){0};
   }
}

#define COPY_SHADER_BINDING(__binding, __type, __stage)                        \
   {                                                                           \
      .binding = __binding,                                                    \
      .descriptorCount = 1,                                                    \
      .descriptorType = VK_DESCRIPTOR_TYPE_##__type,                           \
      .stageFlags = VK_SHADER_STAGE_##__stage##_BIT,                           \
   }

static VkResult
get_copy_pipeline_layout(struct vk_device *device, struct vk_meta_device *meta,
                         const char *key, VkShaderStageFlagBits shader_stage,
                         size_t push_const_size,
                         const struct VkDescriptorSetLayoutBinding *bindings,
                         uint32_t binding_count, VkPipelineLayout *layout_out)
{
   const VkDescriptorSetLayoutCreateInfo set_layout = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = binding_count,
      .pBindings = bindings,
   };

   const VkPushConstantRange push_range = {
      .stageFlags = shader_stage,
      .offset = 0,
      .size = push_const_size,
   };

   return vk_meta_get_pipeline_layout(device, meta, &set_layout, &push_range,
                                      key, strlen(key) + 1, layout_out);
}

#define COPY_PUSH_SET_IMG_DESC(__binding, __type, __iview, __layout)           \
   {                                                                           \
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                         \
      .dstBinding = __binding,                                                 \
      .descriptorType = VK_DESCRIPTOR_TYPE_##__type##_IMAGE,                   \
      .descriptorCount = 1,                                                    \
      .pImageInfo =  &(VkDescriptorImageInfo){                                 \
         .imageView = __iview,                                                 \
         .imageLayout = __layout,                                              \
      },                                                                       \
   }

static VkFormat
copy_img_view_format_for_aspect(const struct vk_meta_copy_image_view *info,
                                VkImageAspectFlagBits aspect)
{
   switch (aspect) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
      return info->color.format;

   case VK_IMAGE_ASPECT_DEPTH_BIT:
      return info->depth.format;

   case VK_IMAGE_ASPECT_STENCIL_BIT:
      return info->stencil.format;

   default:
      assert(!"Unsupported aspect");
      return VK_FORMAT_UNDEFINED;
   }
}

static bool
depth_stencil_interleaved(const struct vk_meta_copy_image_view *view)
{
   return view->stencil.format != VK_FORMAT_UNDEFINED &&
          view->depth.format != VK_FORMAT_UNDEFINED &&
          view->stencil.format == view->depth.format &&
          view->stencil.component_mask != 0 &&
          view->depth.component_mask != 0 &&
          (view->stencil.component_mask & view->depth.component_mask) == 0;
}

static VkResult
get_gfx_copy_pipeline(
   struct vk_device *device, struct vk_meta_device *meta,
   VkPipelineLayout layout, VkSampleCountFlagBits samples,
   nir_shader *(*build_nir)(const struct vk_meta_device *, const void *),
   VkImageAspectFlagBits aspects, const struct vk_meta_copy_image_view *view,
   const void *key_data, size_t key_size, VkPipeline *pipeline_out)
{
   VkPipeline from_cache = vk_meta_lookup_pipeline(meta, key_data, key_size);
   if (from_cache != VK_NULL_HANDLE) {
      *pipeline_out = from_cache;
      return VK_SUCCESS;
   }

   const VkPipelineShaderStageNirCreateInfoMESA fs_nir_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NIR_CREATE_INFO_MESA,
      .nir = build_nir(meta, key_data),
   };
   const VkPipelineShaderStageCreateInfo fs_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .pNext = &fs_nir_info,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pName = "main",
   };

   VkPipelineDepthStencilStateCreateInfo ds_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
   };
   VkPipelineDynamicStateCreateInfo dyn_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
   };
   struct vk_meta_rendering_info render = {
      .samples = samples,
   };

   const VkGraphicsPipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 1,
      .pStages = &fs_info,
      .pDepthStencilState = &ds_info,
      .pDynamicState = &dyn_info,
      .layout = layout,
   };

   if (aspects & VK_IMAGE_ASPECT_COLOR_BIT) {
      VkFormat fmt =
         copy_img_view_format_for_aspect(view, aspects);

      render.color_attachment_formats[render.color_attachment_count] = fmt;
      render.color_attachment_write_masks[render.color_attachment_count] =
         VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
      render.color_attachment_count++;
   }

   if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
      VkFormat fmt =
         copy_img_view_format_for_aspect(view, VK_IMAGE_ASPECT_DEPTH_BIT);

      render.color_attachment_formats[render.color_attachment_count] = fmt;
      render.color_attachment_write_masks[render.color_attachment_count] =
         (VkColorComponentFlags)view->depth.component_mask;
      render.color_attachment_count++;
   }

   if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      VkFormat fmt =
         copy_img_view_format_for_aspect(view, VK_IMAGE_ASPECT_STENCIL_BIT);

      if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT &&
          depth_stencil_interleaved(view)) {
         render.color_attachment_write_masks[0] |= view->stencil.component_mask;
      } else {
         render.color_attachment_formats[render.color_attachment_count] = fmt;
         render.color_attachment_write_masks[render.color_attachment_count] =
            (VkColorComponentFlags)view->stencil.component_mask;
         render.color_attachment_count++;
      }
   }

   VkResult result = vk_meta_create_graphics_pipeline(
      device, meta, &info, &render, key_data, key_size, pipeline_out);

   ralloc_free(fs_nir_info.nir);

   return result;
}

static VkResult
get_compute_copy_pipeline(
   struct vk_device *device, struct vk_meta_device *meta,
   VkPipelineLayout layout,
   nir_shader *(*build_nir)(const struct vk_meta_device *, const void *),
   const void *key_data, size_t key_size, VkPipeline *pipeline_out)
{
   VkPipeline from_cache = vk_meta_lookup_pipeline(meta, key_data, key_size);
   if (from_cache != VK_NULL_HANDLE) {
      *pipeline_out = from_cache;
      return VK_SUCCESS;
   }

   const VkPipelineShaderStageNirCreateInfoMESA cs_nir_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NIR_CREATE_INFO_MESA,
      .nir = build_nir(meta, key_data),
   };

   const VkComputePipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = &cs_nir_info,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .pName = "main",
      },
      .layout = layout,
   };

   VkResult result = vk_meta_create_compute_pipeline(
      device, meta, &info, key_data, key_size, pipeline_out);

   ralloc_free(cs_nir_info.nir);

   return result;
}

static VkResult
copy_create_src_image_view(struct vk_command_buffer *cmd,
                           struct vk_meta_device *meta, struct vk_image *img,
                           const struct vk_meta_copy_image_view *view_info,
                           VkImageAspectFlags aspect,
                           const VkImageSubresourceLayers *subres,
                           VkImageView *view_out)
{
   const VkImageViewUsageCreateInfo usage = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
   };

   VkFormat format = copy_img_view_format_for_aspect(view_info, aspect);

   VkImageViewCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = &usage,
      .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
      .image = vk_image_to_handle(img),
      .viewType = view_info->type,
      .format = format,
      .subresourceRange = {
         .aspectMask = vk_format_aspects(format),
         .baseMipLevel = subres->mipLevel,
         .levelCount = 1,
         .baseArrayLayer = 0,
         .layerCount = img->array_layers,
      },
   };

   if (aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      nir_component_mask_t comp_mask = aspect == VK_IMAGE_ASPECT_STENCIL_BIT
                                          ? view_info->stencil.component_mask
                                          : view_info->depth.component_mask;
      assert(comp_mask != 0);

      VkComponentSwizzle *swizzle = &info.components.r;
      unsigned num_comps = util_bitcount(comp_mask);
      unsigned first_comp = ffs(comp_mask) - 1;

      assert(first_comp + num_comps <= 4);

      for (unsigned i = 0; i < num_comps; i++)
         swizzle[i] = first_comp + i + VK_COMPONENT_SWIZZLE_R;
   }

   return vk_meta_create_image_view(cmd, meta, &info, view_out);
}

static VkResult
copy_create_dst_image_view(struct vk_command_buffer *cmd,
                           struct vk_meta_device *meta, struct vk_image *img,
                           const struct vk_meta_copy_image_view *view_info,
                           VkImageAspectFlags aspect, const VkOffset3D *offset,
                           const VkExtent3D *extent,
                           const VkImageSubresourceLayers *subres,
                           VkPipelineBindPoint bind_point,
                           VkImageView *view_out)
{
   uint32_t layer_count, base_layer;
   VkFormat format = copy_img_view_format_for_aspect(view_info, aspect);
   VkImageAspectFlags fmt_aspects = vk_format_aspects(format);
   const VkImageViewUsageCreateInfo usage = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO,
      .usage = bind_point == VK_PIPELINE_BIND_POINT_COMPUTE
                  ? VK_IMAGE_USAGE_STORAGE_BIT
                  : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
   };

   if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      layer_count =
         MAX2(extent->depth, vk_image_subresource_layer_count(img, subres));
      base_layer = img->image_type == VK_IMAGE_TYPE_3D ? offset->z
                                                       : subres->baseArrayLayer;
   } else {
      /* Always create a view covering the whole image in case of compute. */
      layer_count = img->image_type == VK_IMAGE_TYPE_3D ? 1 : img->array_layers;
      base_layer = 0;
   }

   const VkImageViewCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = &usage,
      .flags = VK_IMAGE_VIEW_CREATE_DRIVER_INTERNAL_BIT_MESA,
      .image = vk_image_to_handle(img),
      .viewType = bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS
                     ? vk_image_render_view_type(img, layer_count)
                     : vk_image_storage_view_type(img),
      .format = format,
      .subresourceRange = {
         .aspectMask = fmt_aspects,
         .baseMipLevel = subres->mipLevel,
         .levelCount = 1,
         .baseArrayLayer = base_layer,
         .layerCount = layer_count,
      },
   };

   return vk_meta_create_image_view(cmd, meta, &info, view_out);
}

static nir_def *
trim_img_coords(nir_builder *b, VkImageViewType view_type, nir_def *coords)
{
   switch (view_type) {
   case VK_IMAGE_VIEW_TYPE_1D:
      return nir_channel(b, coords, 0);

   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
   case VK_IMAGE_VIEW_TYPE_2D:
      return nir_trim_vector(b, coords, 2);

   default:
      return nir_trim_vector(b, coords, 3);
   }
}

static nir_def *
copy_img_buf_addr(nir_builder *b, enum pipe_format pfmt, nir_def *coords)
{
   nir_def *buf_row_stride =
      load_info(b, struct vk_meta_copy_buffer_image_info, buf.row_stride);
   nir_def *buf_img_stride =
      load_info(b, struct vk_meta_copy_buffer_image_info, buf.image_stride);
   nir_def *buf_addr =
      load_info(b, struct vk_meta_copy_buffer_image_info, buf.addr);
   nir_def *offset = nir_imul(b, nir_channel(b, coords, 2), buf_img_stride);
   unsigned blk_sz = util_format_get_blocksize(pfmt);

   offset = nir_iadd(b, offset,
                     nir_imul(b, nir_channel(b, coords, 1), buf_row_stride));
   offset = nir_iadd(b, offset,
                     nir_imul_imm(b, nir_channel(b, coords, 0), blk_sz));

   return nir_iadd(b, buf_addr, nir_u2u64(b, offset));
}

static VkFormat
copy_img_buf_format_for_aspect(const struct vk_meta_copy_image_view *info,
                               VkImageAspectFlagBits aspect)
{
   if (aspect == VK_IMAGE_ASPECT_DEPTH_BIT) {
      enum pipe_format pfmt = vk_format_to_pipe_format(info->depth.format);
      unsigned num_comps = util_format_get_nr_components(pfmt);
      unsigned depth_comp_bits = 0;

      for (unsigned i = 0; i < num_comps; i++) {
         if (info->depth.component_mask & BITFIELD_BIT(i))
            depth_comp_bits += util_format_get_component_bits(
               pfmt, UTIL_FORMAT_COLORSPACE_RGB, i);
      }

      switch (depth_comp_bits) {
      case 16:
         return VK_FORMAT_R16_UINT;
      case 24:
      case 32:
         return VK_FORMAT_R32_UINT;
      default:
         assert(!"Unsupported format");
         return VK_FORMAT_UNDEFINED;
      }
   } else if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT) {
      return VK_FORMAT_R8_UINT;
   }

   enum pipe_format pfmt = vk_format_to_pipe_format(info->color.format);

   switch (util_format_get_blocksize(pfmt)) {
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
      assert(!"Unsupported format");
      return VK_FORMAT_UNDEFINED;
   }
}

static nir_def *
convert_texel(nir_builder *b, VkFormat src_fmt, VkFormat dst_fmt,
              nir_def *texel)
{
   enum pipe_format src_pfmt = vk_format_to_pipe_format(src_fmt);
   enum pipe_format dst_pfmt = vk_format_to_pipe_format(dst_fmt);

   if (src_pfmt == dst_pfmt)
      return texel;

   unsigned src_blksz = util_format_get_blocksize(src_pfmt);
   unsigned dst_blksz = util_format_get_blocksize(dst_pfmt);

   nir_def *packed = nir_format_pack_rgba(b, src_pfmt, texel);

   /* Needed for depth/stencil copies where the source/dest formats might
    * have a different size. */
   if (src_blksz < dst_blksz)
      packed = nir_pad_vector_imm_int(b, packed, 0, 4);

   nir_def *unpacked = nir_format_unpack_rgba(b, packed, dst_pfmt);

   return unpacked;
}

static nir_def *
place_ds_texel(nir_builder *b, VkFormat fmt, nir_component_mask_t comp_mask,
               nir_def *texel)
{
   assert(comp_mask != 0);

   enum pipe_format pfmt = vk_format_to_pipe_format(fmt);
   unsigned num_comps = util_format_get_nr_components(pfmt);

   if (comp_mask == nir_component_mask(num_comps))
      return texel;

   assert(num_comps <= 4);

   nir_def *comps[4];
   unsigned c = 0;

   for (unsigned i = 0; i < num_comps; i++) {
      if (comp_mask & BITFIELD_BIT(i))
         comps[i] = nir_channel(b, texel, c++);
      else
         comps[i] = nir_imm_intN_t(b, 0, texel->bit_size);
   }

   return nir_vec(b, comps, num_comps);
}

static nir_deref_instr *
tex_deref(nir_builder *b, const struct vk_meta_copy_image_view *view,
          VkImageAspectFlags aspect, VkSampleCountFlagBits samples,
          unsigned binding)
{
   VkFormat fmt = copy_img_view_format_for_aspect(view, aspect);
   bool is_array = vk_image_view_type_is_array(view->type);
   enum glsl_sampler_dim sampler_dim =
      samples != VK_SAMPLE_COUNT_1_BIT
         ? GLSL_SAMPLER_DIM_MS
         : vk_image_view_type_to_sampler_dim(view->type);
   enum pipe_format pfmt = vk_format_to_pipe_format(fmt);
   enum glsl_base_type base_type =
      util_format_is_pure_sint(pfmt)   ? GLSL_TYPE_INT
      : util_format_is_pure_uint(pfmt) ? GLSL_TYPE_UINT
                                       : GLSL_TYPE_FLOAT;
   const char *tex_name;
   switch (aspect) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
      tex_name = "color_tex";
      break;
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      tex_name = "depth_tex";
      break;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      tex_name = "stencil_tex";
      break;
   default:
      assert(!"Unsupported aspect");
      return NULL;
   }

   const struct glsl_type *texture_type =
      glsl_sampler_type(sampler_dim, false, is_array, base_type);
   nir_variable *texture =
      nir_variable_create(b->shader, nir_var_uniform, texture_type, tex_name);
   texture->data.descriptor_set = 0;
   texture->data.binding = binding;

   return nir_build_deref_var(b, texture);
}

static nir_deref_instr *
img_deref(nir_builder *b, const struct vk_meta_copy_image_view *view,
          VkImageAspectFlags aspect, VkSampleCountFlagBits samples,
          unsigned binding)
{
   VkFormat fmt = copy_img_view_format_for_aspect(view, aspect);
   bool is_array = vk_image_view_type_is_array(view->type);
   enum glsl_sampler_dim sampler_dim =
      samples != VK_SAMPLE_COUNT_1_BIT
         ? GLSL_SAMPLER_DIM_MS
         : vk_image_view_type_to_sampler_dim(view->type);
   enum pipe_format pfmt = vk_format_to_pipe_format(fmt);
   enum glsl_base_type base_type =
      util_format_is_pure_sint(pfmt)   ? GLSL_TYPE_INT
      : util_format_is_pure_uint(pfmt) ? GLSL_TYPE_UINT
                                       : GLSL_TYPE_FLOAT;
   const char *img_name;
   switch (aspect) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
      img_name = "color_img";
      break;
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      img_name = "depth_img";
      break;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      img_name = "stencil_img";
      break;
   default:
      assert(!"Unsupported aspect");
      return NULL;
   }
   const struct glsl_type *image_type =
      glsl_image_type(sampler_dim, is_array, base_type);
   nir_variable *image_var =
      nir_variable_create(b->shader, nir_var_uniform, image_type, img_name);
   image_var->data.descriptor_set = 0;
   image_var->data.binding = binding;

   return nir_build_deref_var(b, image_var);
}

static nir_def *
read_texel(nir_builder *b, nir_deref_instr *tex_deref, nir_def *coords,
           nir_def *sample_id)
{
   return sample_id ? nir_txf_ms_deref(b, tex_deref, coords, sample_id)
                    : nir_txf_deref(b, tex_deref, coords, NULL);
}

static nir_variable *
frag_var(nir_builder *b, const struct vk_meta_copy_image_view *view,
         VkImageAspectFlags aspect, uint32_t rt)
{
   VkFormat fmt = copy_img_view_format_for_aspect(view, aspect);
   enum pipe_format pfmt = vk_format_to_pipe_format(fmt);
   enum glsl_base_type base_type =
      util_format_is_pure_sint(pfmt)   ? GLSL_TYPE_INT
      : util_format_is_pure_uint(pfmt) ? GLSL_TYPE_UINT
                                       : GLSL_TYPE_FLOAT;
   const struct glsl_type *var_type = glsl_vector_type(base_type, 4);
   static const char *var_names[] = {
      "gl_FragData[0]",
      "gl_FragData[1]",
   };

   assert(rt < ARRAY_SIZE(var_names));

   nir_variable *var = nir_variable_create(b->shader, nir_var_shader_out,
                                           var_type, var_names[rt]);
   var->data.location = FRAG_RESULT_DATA0 + rt;

   return var;
}

static void
write_frag(nir_builder *b, const struct vk_meta_copy_image_view *view,
           VkImageAspectFlags aspect, nir_variable *frag_var, nir_def *frag_val)
{
   nir_component_mask_t comp_mask;

   if (aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      VkFormat fmt = copy_img_view_format_for_aspect(view, aspect);

      comp_mask = aspect == VK_IMAGE_ASPECT_DEPTH_BIT
                     ? view->depth.component_mask
                     : view->stencil.component_mask;
      frag_val = place_ds_texel(b, fmt, comp_mask, frag_val);
   } else {
      comp_mask = nir_component_mask(4);
   }

   if (frag_val->bit_size != 32) {
      switch (glsl_get_base_type(frag_var->type)) {
      case GLSL_TYPE_INT:
         frag_val = nir_i2i32(b, frag_val);
         break;
      case GLSL_TYPE_UINT:
         frag_val = nir_u2u32(b, frag_val);
         break;
      case GLSL_TYPE_FLOAT:
         frag_val = nir_f2f32(b, frag_val);
         break;
      default:
         assert(!"Invalid type");
         frag_val = NULL;
         break;
      }
   }

   frag_val = nir_pad_vector_imm_int(b, frag_val, 0, 4);

   nir_store_var(b, frag_var, frag_val, comp_mask);
}

static void
write_img(nir_builder *b, const struct vk_meta_copy_image_view *view,
          VkImageAspectFlags aspect, VkSampleCountFlagBits samples,
          nir_deref_instr *img_deref, nir_def *coords, nir_def *sample_id,
          nir_def *val)
{
   VkFormat fmt = copy_img_view_format_for_aspect(view, aspect);
   enum pipe_format pfmt = vk_format_to_pipe_format(fmt);
   enum glsl_base_type base_type =
      util_format_is_pure_sint(pfmt)   ? GLSL_TYPE_INT
      : util_format_is_pure_uint(pfmt) ? GLSL_TYPE_UINT
                                       : GLSL_TYPE_FLOAT;
   enum glsl_sampler_dim sampler_dim =
      samples != VK_SAMPLE_COUNT_1_BIT
         ? GLSL_SAMPLER_DIM_MS
         : vk_image_view_type_to_sampler_dim(view->type);
   bool is_array = vk_image_view_type_is_array(view->type);

   if (!sample_id) {
      assert(samples == VK_SAMPLE_COUNT_1_BIT);
      sample_id = nir_imm_int(b, 0);
   }

   unsigned access_flags = ACCESS_NON_READABLE;
   nir_def *zero_lod = nir_imm_int(b, 0);

   if (aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      nir_component_mask_t comp_mask = aspect == VK_IMAGE_ASPECT_DEPTH_BIT
                                          ? view->depth.component_mask
                                          : view->stencil.component_mask;
      unsigned num_comps = util_format_get_nr_components(pfmt);

      val = place_ds_texel(b, fmt, comp_mask, val);

      if (comp_mask != nir_component_mask(num_comps)) {
         nir_def *comps[4];
         access_flags = 0;

         nir_def *old_val = nir_image_deref_load(b,
            val->num_components, val->bit_size, &img_deref->def, coords,
            sample_id, zero_lod, .image_dim = sampler_dim,
            .image_array = is_array, .format = pfmt, .access = access_flags,
            .dest_type = nir_get_nir_type_for_glsl_base_type(base_type));

         for (unsigned i = 0; i < val->num_components; i++) {
            if (comp_mask & BITFIELD_BIT(i))
               comps[i] = nir_channel(b, val, i);
            else
               comps[i] = nir_channel(b, old_val, i);
         }

         val = nir_vec(b, comps, val->num_components);
      }
   }

   nir_image_deref_store(b,
       &img_deref->def, coords, sample_id, val, zero_lod,
      .image_dim = sampler_dim, .image_array = is_array, .format = pfmt,
      .access = access_flags,
      .src_type = nir_get_nir_type_for_glsl_base_type(base_type));
}

static nir_shader *
build_image_to_buffer_shader(const struct vk_meta_device *meta,
                             const void *key_data)
{
   const struct vk_meta_copy_buffer_image_key *key = key_data;

   assert(key->bind_point == VK_PIPELINE_BIND_POINT_COMPUTE);

   nir_builder builder = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL, "vk-meta-copy-image-to-buffer");
   nir_builder *b = &builder;

   b->shader->info.workgroup_size[0] = key->wg_size[0];
   b->shader->info.workgroup_size[1] = key->wg_size[1];
   b->shader->info.workgroup_size[2] = key->wg_size[2];

   VkFormat buf_fmt =
      copy_img_buf_format_for_aspect(&key->img.view, key->img.aspect);
   enum pipe_format buf_pfmt = vk_format_to_pipe_format(buf_fmt);

   nir_def *copy_id = nir_load_global_invocation_id(b, 32);
   nir_def *copy_id_start =
      nir_vec3(b,
               load_info(b, struct vk_meta_copy_buffer_image_info,
                         copy_id_range.start.x),
               load_info(b, struct vk_meta_copy_buffer_image_info,
                         copy_id_range.start.y),
               load_info(b, struct vk_meta_copy_buffer_image_info,
                         copy_id_range.start.z));
   nir_def *copy_id_end = nir_vec3(b,
      load_info(b, struct vk_meta_copy_buffer_image_info, copy_id_range.end.x),
      load_info(b, struct vk_meta_copy_buffer_image_info, copy_id_range.end.y),
      load_info(b, struct vk_meta_copy_buffer_image_info,
                copy_id_range.end.z));

   nir_def *in_bounds =
      nir_iand(b, nir_ball(b, nir_uge(b, copy_id, copy_id_start)),
               nir_ball(b, nir_ult(b, copy_id, copy_id_end)));

   nir_push_if(b, in_bounds);

   copy_id = nir_isub(b, copy_id, copy_id_start);

   nir_def *img_offs = nir_vec3(b,
      load_info(b, struct vk_meta_copy_buffer_image_info, img.offset.x),
      load_info(b, struct vk_meta_copy_buffer_image_info, img.offset.y),
      load_info(b, struct vk_meta_copy_buffer_image_info, img.offset.z));

   nir_def *img_coords =
      trim_img_coords(b, key->img.view.type, nir_iadd(b, copy_id, img_offs));

   VkFormat iview_fmt =
      copy_img_view_format_for_aspect(&key->img.view, key->img.aspect);
   nir_deref_instr *tex =
      tex_deref(b, &key->img.view, key->img.aspect, VK_SAMPLE_COUNT_1_BIT, 0);
   nir_def *texel = read_texel(b, tex, img_coords, NULL);

   texel = convert_texel(b, iview_fmt, buf_fmt, texel);

   unsigned blk_sz = util_format_get_blocksize(buf_pfmt);
   unsigned comp_count = util_format_get_nr_components(buf_pfmt);
   assert(blk_sz % comp_count == 0);
   unsigned comp_sz = (blk_sz / comp_count) * 8;

   /* nir_format_unpack() (which is called in convert_texel()) always
    * returns a 32-bit result, which we might have to downsize to match
    * the component size we want, hence the u2uN().
    */
   texel = nir_u2uN(b, texel, comp_sz);

   /* nir_format_unpack_rgba() (which is called from convert_texel()) returns
    * a vec4, which means we might have more components than we need, but
    * that's fine because we pass a write_mask to store_global.
    */
   assert(texel->num_components >= comp_count);
   nir_store_global(b, copy_img_buf_addr(b, buf_pfmt, copy_id),
                    comp_sz / 8, texel, nir_component_mask(comp_count));

   nir_pop_if(b, NULL);

   return b->shader;
}

static VkResult
get_copy_image_to_buffer_pipeline(
   struct vk_device *device, struct vk_meta_device *meta,
   const struct vk_meta_copy_buffer_image_key *key,
   VkPipelineLayout *layout_out, VkPipeline *pipeline_out)
{
   const VkDescriptorSetLayoutBinding bindings[] = {
      COPY_SHADER_BINDING(0, SAMPLED_IMAGE, COMPUTE),
   };

   VkResult result = get_copy_pipeline_layout(
      device, meta, "vk-meta-copy-image-to-buffer-pipeline-layout",
      VK_SHADER_STAGE_COMPUTE_BIT,
      sizeof(struct vk_meta_copy_buffer_image_info), bindings,
      ARRAY_SIZE(bindings), layout_out);

   if (unlikely(result != VK_SUCCESS))
      return result;

   return get_compute_copy_pipeline(device, meta, *layout_out,
                                    build_image_to_buffer_shader, key,
                                    sizeof(*key), pipeline_out);
}

static nir_shader *
build_buffer_to_image_fs(const struct vk_meta_device *meta,
                         const void *key_data)
{
   const struct vk_meta_copy_buffer_image_key *key = key_data;

   assert(key->bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS);

   nir_builder builder = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, NULL, "vk-meta-copy-buffer-to-image-frag");
   nir_builder *b = &builder;

   VkFormat buf_fmt =
      copy_img_buf_format_for_aspect(&key->img.view, key->img.aspect);

   enum pipe_format buf_pfmt = vk_format_to_pipe_format(buf_fmt);
   nir_def *out_coord_xy = nir_f2u32(b, nir_load_frag_coord(b));
   nir_def *out_layer = nir_load_layer_id(b);

   nir_def *img_offs = nir_vec3(b,
      load_info(b, struct vk_meta_copy_buffer_image_info, img.offset.x),
      load_info(b, struct vk_meta_copy_buffer_image_info, img.offset.y),
      load_info(b, struct vk_meta_copy_buffer_image_info, img.offset.z));

   /* Move the layer ID to the second coordinate if we're dealing with a 1D
    * array, as this is where the texture instruction expects it. */
   nir_def *coords = key->img.view.type == VK_IMAGE_VIEW_TYPE_1D_ARRAY
                        ? nir_vec3(b, nir_channel(b, out_coord_xy, 0),
                                   out_layer, nir_imm_int(b, 0))
                        : nir_vec3(b, nir_channel(b, out_coord_xy, 0),
                                   nir_channel(b, out_coord_xy, 1), out_layer);

   unsigned blk_sz = util_format_get_blocksize(buf_pfmt);
   unsigned comp_count = util_format_get_nr_components(buf_pfmt);
   assert(blk_sz % comp_count == 0);
   unsigned comp_sz = (blk_sz / comp_count) * 8;

   coords = nir_isub(b, coords, img_offs);

   nir_def *texel = nir_build_load_global(b,
      comp_count, comp_sz, copy_img_buf_addr(b, buf_pfmt, coords),
      .align_mul = 1 << (ffs(blk_sz) - 1));

   /* We don't do compressed formats. The driver should select a non-compressed
    * format with the same block size. */
   assert(!util_format_is_compressed(buf_pfmt));

   VkFormat iview_fmt =
      copy_img_view_format_for_aspect(&key->img.view, key->img.aspect);
   nir_variable *out_var = frag_var(b, &key->img.view, key->img.aspect, 0);

   texel = convert_texel(b, buf_fmt, iview_fmt, texel);
   write_frag(b, &key->img.view, key->img.aspect, out_var, texel);
   return b->shader;
}

static VkResult
get_copy_buffer_to_image_gfx_pipeline(
   struct vk_device *device, struct vk_meta_device *meta,
   const struct vk_meta_copy_buffer_image_key *key,
   VkPipelineLayout *layout_out, VkPipeline *pipeline_out)
{
   VkResult result = get_copy_pipeline_layout(
      device, meta, "vk-meta-copy-buffer-to-image-gfx-pipeline-layout",
      VK_SHADER_STAGE_FRAGMENT_BIT,
      sizeof(struct vk_meta_copy_buffer_image_info), NULL, 0, layout_out);

   if (unlikely(result != VK_SUCCESS))
      return result;

   return get_gfx_copy_pipeline(device, meta, *layout_out,
                                VK_SAMPLE_COUNT_1_BIT, build_buffer_to_image_fs,
                                key->img.aspect, &key->img.view, key,
                                sizeof(*key), pipeline_out);
}

static nir_shader *
build_buffer_to_image_cs(const struct vk_meta_device *meta,
                         const void *key_data)
{
   const struct vk_meta_copy_buffer_image_key *key = key_data;

   assert(key->bind_point == VK_PIPELINE_BIND_POINT_COMPUTE);

   nir_builder builder = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL, "vk-meta-copy-buffer-to-image-compute");
   nir_builder *b = &builder;

   b->shader->info.workgroup_size[0] = key->wg_size[0];
   b->shader->info.workgroup_size[1] = key->wg_size[1];
   b->shader->info.workgroup_size[2] = key->wg_size[2];

   VkFormat buf_fmt =
      copy_img_buf_format_for_aspect(&key->img.view, key->img.aspect);
   VkFormat img_fmt =
      copy_img_view_format_for_aspect(&key->img.view, key->img.aspect);
   enum pipe_format buf_pfmt = vk_format_to_pipe_format(buf_fmt);
   nir_deref_instr *image_deref =
      img_deref(b, &key->img.view, key->img.aspect, VK_SAMPLE_COUNT_1_BIT, 0);

   nir_def *copy_id = nir_load_global_invocation_id(b, 32);
   nir_def *copy_id_start =
      nir_vec3(b,
               load_info(b, struct vk_meta_copy_buffer_image_info,
                         copy_id_range.start.x),
               load_info(b, struct vk_meta_copy_buffer_image_info,
                         copy_id_range.start.y),
               load_info(b, struct vk_meta_copy_buffer_image_info,
                         copy_id_range.start.z));
   nir_def *copy_id_end = nir_vec3(b,
      load_info(b, struct vk_meta_copy_buffer_image_info, copy_id_range.end.x),
      load_info(b, struct vk_meta_copy_buffer_image_info, copy_id_range.end.y),
      load_info(b, struct vk_meta_copy_buffer_image_info,
                copy_id_range.end.z));

   nir_def *in_bounds =
      nir_iand(b, nir_ball(b, nir_uge(b, copy_id, copy_id_start)),
               nir_ball(b, nir_ult(b, copy_id, copy_id_end)));

   nir_push_if(b, in_bounds);

   /* Adjust the copy ID such that we can directly deduce the image coords and
    * buffer offset from it. */
   copy_id = nir_isub(b, copy_id, copy_id_start);

   nir_def *img_offs = nir_vec3(b,
      load_info(b, struct vk_meta_copy_buffer_image_info, img.offset.x),
      load_info(b, struct vk_meta_copy_buffer_image_info, img.offset.y),
      load_info(b, struct vk_meta_copy_buffer_image_info, img.offset.z));

   nir_def *img_coords =
      trim_img_coords(b, key->img.view.type, nir_iadd(b, copy_id, img_offs));

   img_coords = nir_pad_vector_imm_int(b, img_coords, 0, 4);

   unsigned blk_sz = util_format_get_blocksize(buf_pfmt);
   unsigned bit_sz = blk_sz & 1 ? 8 : blk_sz & 2 ? 16 : 32;
   unsigned comp_count = blk_sz * 8 / bit_sz;

   nir_def *texel = nir_build_load_global(b,
         comp_count, bit_sz, copy_img_buf_addr(b, buf_pfmt, copy_id),
         .align_mul = 1 << (ffs(blk_sz) - 1));

   texel = convert_texel(b, buf_fmt, img_fmt, texel);

   /* If the image view format matches buf_fmt, convert_texel() does nothing,
    * but we still need to promote the texel to a 32-bit unsigned integer,
    * because write_img() wants a 32-bit value.
    */
   if (texel->bit_size < 32)
      texel = nir_u2u32(b, texel);

   write_img(b, &key->img.view, key->img.aspect, VK_SAMPLE_COUNT_1_BIT,
             image_deref, img_coords, NULL, texel);

   nir_pop_if(b, NULL);

   return b->shader;
}

static VkResult
get_copy_buffer_to_image_compute_pipeline(
   struct vk_device *device, struct vk_meta_device *meta,
   const struct vk_meta_copy_buffer_image_key *key,
   VkPipelineLayout *layout_out, VkPipeline *pipeline_out)
{
   const VkDescriptorSetLayoutBinding bindings[] = {
      COPY_SHADER_BINDING(0, STORAGE_IMAGE, COMPUTE),
   };

   VkResult result = get_copy_pipeline_layout(
      device, meta, "vk-meta-copy-buffer-to-image-compute-pipeline-layout",
      VK_SHADER_STAGE_COMPUTE_BIT,
      sizeof(struct vk_meta_copy_buffer_image_info), bindings,
      ARRAY_SIZE(bindings), layout_out);

   if (unlikely(result != VK_SUCCESS))
      return result;

   return get_compute_copy_pipeline(device, meta, *layout_out,
                                    build_buffer_to_image_cs, key, sizeof(*key),
                                    pipeline_out);
}

static VkResult
copy_buffer_image_prepare_gfx_push_const(
   struct vk_command_buffer *cmd, struct vk_meta_device *meta,
   const struct vk_meta_copy_buffer_image_key *key,
   VkPipelineLayout pipeline_layout, VkBuffer buffer,
   const struct vk_image_buffer_layout *buf_layout, struct vk_image *img,
   const VkBufferImageCopy2 *region)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;

   /* vk_meta_copy_buffer_image_info::image_stride is 32-bit for now.
    * We might want to make it a 64-bit integer (and patch the shader code
    * accordingly) if that becomes a limiting factor for vk_meta_copy users.
    */
   assert(buf_layout->image_stride_B <= UINT32_MAX);

   struct vk_meta_copy_buffer_image_info info = {
      .buf = {
         .row_stride = buf_layout->row_stride_B,
         .image_stride = buf_layout->image_stride_B,
         .addr = vk_meta_buffer_address(dev, buffer, region->bufferOffset,
                                        VK_WHOLE_SIZE),
      },
      .img.offset = {
         .x = region->imageOffset.x,
         .y = region->imageOffset.y,
         .z = region->imageOffset.z,
      },
   };

   disp->CmdPushConstants(vk_command_buffer_to_handle(cmd), pipeline_layout,
                          VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(info), &info);
   return VK_SUCCESS;
}

static VkResult
copy_buffer_image_prepare_compute_push_const(
   struct vk_command_buffer *cmd, struct vk_meta_device *meta,
   const struct vk_meta_copy_buffer_image_key *key,
   VkPipelineLayout pipeline_layout, VkBuffer buffer,
   const struct vk_image_buffer_layout *buf_layout, struct vk_image *img,
   const VkBufferImageCopy2 *region, uint32_t *wg_count)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   VkImageViewType img_view_type = key->img.view.type;
   VkOffset3D img_offs =
      base_layer_as_offset(img_view_type, region->imageOffset,
                           region->imageSubresource.baseArrayLayer);
   uint32_t layer_count =
      vk_image_subresource_layer_count(img, &region->imageSubresource);
   VkExtent3D img_extent =
      layer_count_as_extent(img_view_type, region->imageExtent, layer_count);

   struct vk_meta_copy_buffer_image_info info = {
      .buf = {
         .row_stride = buf_layout->row_stride_B,
         .image_stride = buf_layout->image_stride_B,
         .addr = vk_meta_buffer_address(dev, buffer, region->bufferOffset,
                                        VK_WHOLE_SIZE),
      },
      .img.offset = {
         .x = img_offs.x,
         .y = img_offs.y,
         .z = img_offs.z,
      },
   };

   info.copy_id_range.start.x = img_offs.x % key->wg_size[0];
   info.copy_id_range.start.y = img_offs.y % key->wg_size[1];
   info.copy_id_range.start.z = img_offs.z % key->wg_size[2];
   info.copy_id_range.end.x = info.copy_id_range.start.x + img_extent.width;
   info.copy_id_range.end.y = info.copy_id_range.start.y + img_extent.height;
   info.copy_id_range.end.z = info.copy_id_range.start.z + img_extent.depth;
   wg_count[0] = DIV_ROUND_UP(info.copy_id_range.end.x, key->wg_size[0]);
   wg_count[1] = DIV_ROUND_UP(info.copy_id_range.end.y, key->wg_size[1]);
   wg_count[2] = DIV_ROUND_UP(info.copy_id_range.end.z, key->wg_size[2]);

   disp->CmdPushConstants(vk_command_buffer_to_handle(cmd), pipeline_layout,
                          VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(info), &info);
   return VK_SUCCESS;
}

static bool
format_is_supported(VkFormat fmt)
{
   enum pipe_format pfmt = vk_format_to_pipe_format(fmt);
   const struct util_format_description *fdesc = util_format_description(pfmt);

   /* We only support RGB formats in the copy path to keep things simple. */
   return fdesc->colorspace == UTIL_FORMAT_COLORSPACE_RGB ||
          fdesc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB;
}

static struct vk_meta_copy_image_view
img_copy_view_info(VkImageViewType view_type, VkImageAspectFlags aspects,
                   const struct vk_image *img,
                   const struct vk_meta_copy_image_properties *img_props)
{
   struct vk_meta_copy_image_view view = {
      .type = view_type,
   };

   /* We only support color/depth/stencil aspects. */
   assert(aspects & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT |
                     VK_IMAGE_ASPECT_STENCIL_BIT));

   if (aspects & VK_IMAGE_ASPECT_COLOR_BIT) {
      /* Color aspect can't be combined with other aspects. */
      assert(!(aspects & ~VK_IMAGE_ASPECT_COLOR_BIT));
      view.color.format = img_props->color.view_format;
      assert(format_is_supported(view.color.format));
      return view;
   }


   view.depth.format = img_props->depth.view_format;
   view.depth.component_mask = img_props->depth.component_mask;
   view.stencil.format = img_props->stencil.view_format;
   view.stencil.component_mask = img_props->stencil.component_mask;

   assert(view.depth.format == VK_FORMAT_UNDEFINED ||
          format_is_supported(view.depth.format));
   assert(view.stencil.format == VK_FORMAT_UNDEFINED ||
          format_is_supported(view.stencil.format));
   return view;
}

static void
copy_image_to_buffer_region(
   struct vk_command_buffer *cmd, struct vk_meta_device *meta,
   struct vk_image *img, VkImageLayout img_layout,
   const struct vk_meta_copy_image_properties *img_props, VkBuffer buffer,
   const struct vk_image_buffer_layout *buf_layout,
   const VkBufferImageCopy2 *region)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   struct vk_meta_copy_buffer_image_key key = {
      .key_type = VK_META_OBJECT_KEY_COPY_IMAGE_TO_BUFFER_PIPELINE,
      .bind_point = VK_PIPELINE_BIND_POINT_COMPUTE,
      .img = {
         .view = img_copy_view_info(vk_image_sampled_view_type(img),
                                    region->imageSubresource.aspectMask, img,
                                    img_props),
         .aspect = region->imageSubresource.aspectMask,
      },
      .wg_size = {
         img_props->tile_size.width,
         img_props->tile_size.height,
         img_props->tile_size.depth,
      },
   };

   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkResult result = get_copy_image_to_buffer_pipeline(
      dev, meta, &key, &pipeline_layout, &pipeline);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   disp->CmdBindPipeline(vk_command_buffer_to_handle(cmd),
                         VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   VkImageView iview;
   result = copy_create_src_image_view(cmd, meta, img, &key.img.view,
                                       region->imageSubresource.aspectMask,
                                       &region->imageSubresource, &iview);

   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   const VkWriteDescriptorSet descs[] = {
      COPY_PUSH_SET_IMG_DESC(0, SAMPLED, iview, img_layout),
   };

   disp->CmdPushDescriptorSetKHR(vk_command_buffer_to_handle(cmd),
                                 VK_PIPELINE_BIND_POINT_COMPUTE,
                                 pipeline_layout, 0, ARRAY_SIZE(descs), descs);

   uint32_t wg_count[3] = {0};

   result = copy_buffer_image_prepare_compute_push_const(
      cmd, meta, &key, pipeline_layout, buffer, buf_layout, img, region,
      wg_count);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   disp->CmdDispatch(vk_command_buffer_to_handle(cmd), wg_count[0], wg_count[1],
                     wg_count[2]);
}

void
vk_meta_copy_image_to_buffer(
   struct vk_command_buffer *cmd, struct vk_meta_device *meta,
   const VkCopyImageToBufferInfo2 *info,
   const struct vk_meta_copy_image_properties *img_props)
{
   VK_FROM_HANDLE(vk_image, img, info->srcImage);

   for (uint32_t i = 0; i < info->regionCount; i++) {
      VkBufferImageCopy2 region = info->pRegions[i];
      struct vk_image_buffer_layout buf_layout =
         vk_image_buffer_copy_layout(img, &region);

      region.imageExtent = vk_image_extent_to_elements(img, region.imageExtent);
      region.imageOffset = vk_image_offset_to_elements(img, region.imageOffset);

      copy_image_to_buffer_region(cmd, meta, img, info->srcImageLayout,
                                  img_props, info->dstBuffer, &buf_layout,
                                  &region);
   }
}

static void
copy_draw(struct vk_command_buffer *cmd, struct vk_meta_device *meta,
          struct vk_image *dst_img, VkImageLayout dst_img_layout,
          const VkImageSubresourceLayers *dst_img_subres,
          const VkOffset3D *dst_img_offset, const VkExtent3D *copy_extent,
          const struct vk_meta_copy_image_view *view_info)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   uint32_t depth_or_layer_count =
      MAX2(copy_extent->depth,
           vk_image_subresource_layer_count(dst_img, dst_img_subres));
   struct vk_meta_rect rect = {
      .x0 = dst_img_offset->x,
      .x1 = dst_img_offset->x + copy_extent->width,
      .y0 = dst_img_offset->y,
      .y1 = dst_img_offset->y + copy_extent->height,
   };
   VkRenderingAttachmentInfo vk_atts[2];
   VkRenderingInfo vk_render = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {
         .offset = {
            dst_img_offset->x,
            dst_img_offset->y,
         },
         .extent = {
            copy_extent->width,
            copy_extent->height,
         },
      },
      .layerCount = depth_or_layer_count,
      .pColorAttachments = vk_atts,
   };
   VkImageView iview = VK_NULL_HANDLE;

   u_foreach_bit(a, dst_img_subres->aspectMask) {
      VkImageAspectFlagBits aspect = 1 << a;

      if (aspect == VK_IMAGE_ASPECT_STENCIL_BIT && iview != VK_NULL_HANDLE &&
          depth_stencil_interleaved(view_info))
         continue;

      VkResult result = copy_create_dst_image_view(
         cmd, meta, dst_img, view_info, aspect, dst_img_offset, copy_extent,
         dst_img_subres, VK_PIPELINE_BIND_POINT_GRAPHICS, &iview);
      if (unlikely(result != VK_SUCCESS)) {
         vk_command_buffer_set_error(cmd, result);
         return;
      }

      vk_atts[vk_render.colorAttachmentCount] = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = iview,
         .imageLayout = dst_img_layout,
         .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      };

      /* If we have interleaved depth/stencil and only one aspect is copied, we
       * need to load the attachment to preserve the other component. */
      if (vk_format_has_depth(dst_img->format) &&
          vk_format_has_stencil(dst_img->format) &&
          depth_stencil_interleaved(view_info) &&
          (dst_img_subres->aspectMask !=
           (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))) {
         vk_atts[vk_render.colorAttachmentCount].loadOp =
            VK_ATTACHMENT_LOAD_OP_LOAD;
      }

      vk_render.colorAttachmentCount++;
   }

   disp->CmdBeginRendering(vk_command_buffer_to_handle(cmd), &vk_render);
   meta->cmd_draw_volume(cmd, meta, &rect, vk_render.layerCount);
   disp->CmdEndRendering(vk_command_buffer_to_handle(cmd));
}

static void
copy_buffer_to_image_region_gfx(
   struct vk_command_buffer *cmd, struct vk_meta_device *meta,
   struct vk_image *img, VkImageLayout img_layout,
   const struct vk_meta_copy_image_properties *img_props, VkBuffer buffer,
   const struct vk_image_buffer_layout *buf_layout,
   const VkBufferImageCopy2 *region)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;

   /* We only special-case 1D_ARRAY to move the layer ID to the second
    * component instead of the third. For all other view types, let's pick an
    * invalid VkImageViewType value so we don't end up creating the same
    * pipeline multiple times. */
   VkImageViewType view_type =
      img->image_type == VK_IMAGE_TYPE_1D && img->array_layers > 1
         ? VK_IMAGE_VIEW_TYPE_1D_ARRAY
         : (VkImageViewType)-1;

   struct vk_meta_copy_buffer_image_key key = {
      .key_type = VK_META_OBJECT_KEY_COPY_BUFFER_TO_IMAGE_PIPELINE,
      .bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .img = {
         .view = img_copy_view_info(view_type,
                                    region->imageSubresource.aspectMask, img,
                                    img_props),
         .aspect = region->imageSubresource.aspectMask,
      },
   };

   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkResult result = get_copy_buffer_to_image_gfx_pipeline(
      dev, meta, &key, &pipeline_layout, &pipeline);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   disp->CmdBindPipeline(vk_command_buffer_to_handle(cmd),
                         VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

   result = copy_buffer_image_prepare_gfx_push_const(
      cmd, meta, &key, pipeline_layout, buffer, buf_layout, img, region);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   copy_draw(cmd, meta, img, img_layout, &region->imageSubresource,
             &region->imageOffset, &region->imageExtent, &key.img.view);
}

static void
copy_buffer_to_image_region_compute(
   struct vk_command_buffer *cmd, struct vk_meta_device *meta,
   struct vk_image *img, VkImageLayout img_layout,
   const struct vk_meta_copy_image_properties *img_props, VkBuffer buffer,
   const struct vk_image_buffer_layout *buf_layout,
   const VkBufferImageCopy2 *region)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   VkImageViewType view_type = vk_image_storage_view_type(img);
   struct vk_meta_copy_buffer_image_key key = {
      .key_type = VK_META_OBJECT_KEY_COPY_BUFFER_TO_IMAGE_PIPELINE,
      .bind_point = VK_PIPELINE_BIND_POINT_COMPUTE,
      .img = {
         .view = img_copy_view_info(view_type,
                                    region->imageSubresource.aspectMask, img,
                                    img_props),
         .aspect = region->imageSubresource.aspectMask,
      },
      .wg_size = {
         img_props->tile_size.width,
         img_props->tile_size.height,
         img_props->tile_size.depth,
      },
   };

   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkResult result = get_copy_buffer_to_image_compute_pipeline(
      dev, meta, &key, &pipeline_layout, &pipeline);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   disp->CmdBindPipeline(vk_command_buffer_to_handle(cmd),
                         VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   VkImageView iview;
   result = copy_create_dst_image_view(
      cmd, meta, img, &key.img.view, region->imageSubresource.aspectMask,
      &region->imageOffset, &region->imageExtent, &region->imageSubresource,
      VK_PIPELINE_BIND_POINT_COMPUTE, &iview);

   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   const VkWriteDescriptorSet descs[] = {
      COPY_PUSH_SET_IMG_DESC(0, STORAGE, iview, img_layout),
   };

   disp->CmdPushDescriptorSetKHR(vk_command_buffer_to_handle(cmd),
                                 VK_PIPELINE_BIND_POINT_COMPUTE,
                                 pipeline_layout, 0, ARRAY_SIZE(descs), descs);

   uint32_t wg_count[3] = {0};

   result = copy_buffer_image_prepare_compute_push_const(
      cmd, meta, &key, pipeline_layout, buffer, buf_layout, img, region,
      wg_count);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   disp->CmdDispatch(vk_command_buffer_to_handle(cmd),
                     wg_count[0], wg_count[1], wg_count[2]);
}

void
vk_meta_copy_buffer_to_image(
   struct vk_command_buffer *cmd, struct vk_meta_device *meta,
   const VkCopyBufferToImageInfo2 *info,
   const struct vk_meta_copy_image_properties *img_props,
   VkPipelineBindPoint bind_point)
{
   VK_FROM_HANDLE(vk_image, img, info->dstImage);

   for (uint32_t i = 0; i < info->regionCount; i++) {
      VkBufferImageCopy2 region = info->pRegions[i];
      struct vk_image_buffer_layout buf_layout =
         vk_image_buffer_copy_layout(img, &region);

      region.imageExtent = vk_image_extent_to_elements(img, region.imageExtent);
      region.imageOffset = vk_image_offset_to_elements(img, region.imageOffset);

      if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
         copy_buffer_to_image_region_gfx(cmd, meta, img, info->dstImageLayout,
                                         img_props, info->srcBuffer,
                                         &buf_layout, &region);
      } else {
         copy_buffer_to_image_region_compute(cmd, meta, img,
                                             info->dstImageLayout, img_props,
                                             info->srcBuffer, &buf_layout,
                                             &region);
      }
   }
}

static nir_shader *
build_copy_image_fs(const struct vk_meta_device *meta, const void *key_data)
{
   const struct vk_meta_copy_image_key *key = key_data;

   assert(key->bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS);

   nir_builder builder = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, NULL, "vk-meta-copy-image-frag");
   nir_builder *b = &builder;

   b->shader->info.fs.uses_sample_shading =
      key->samples != VK_SAMPLE_COUNT_1_BIT;

   nir_def *out_coord_xy = nir_f2u32(b, nir_load_frag_coord(b));
   nir_def *out_layer = nir_load_layer_id(b);

   nir_def *src_offset = nir_vec3(b,
      load_info(b, struct vk_meta_copy_image_fs_info, dst_to_src_offs.x),
      load_info(b, struct vk_meta_copy_image_fs_info, dst_to_src_offs.y),
      load_info(b, struct vk_meta_copy_image_fs_info, dst_to_src_offs.z));

   /* Move the layer ID to the second coordinate if we're dealing with a 1D
    * array, as this is where the texture instruction expects it. */
   nir_def *src_coords =
      key->dst.view.type == VK_IMAGE_VIEW_TYPE_1D_ARRAY
         ? nir_vec3(b, nir_channel(b, out_coord_xy, 0), out_layer,
                    nir_imm_int(b, 0))
         : nir_vec3(b, nir_channel(b, out_coord_xy, 0),
                    nir_channel(b, out_coord_xy, 1), out_layer);

   src_coords = trim_img_coords(b, key->src.view.type,
                                nir_iadd(b, src_coords, src_offset));

   nir_def *sample_id =
      key->samples != VK_SAMPLE_COUNT_1_BIT ? nir_load_sample_id(b) : NULL;
   nir_variable *color_var = NULL;
   uint32_t tex_binding = 0;

   u_foreach_bit(a, key->aspects) {
      VkImageAspectFlagBits aspect = 1 << a;
      VkFormat src_fmt =
         copy_img_view_format_for_aspect(&key->src.view, aspect);
      VkFormat dst_fmt =
         copy_img_view_format_for_aspect(&key->dst.view, aspect);
      nir_deref_instr *tex =
         tex_deref(b, &key->src.view, aspect, key->samples, tex_binding++);
      nir_def *texel = read_texel(b, tex, src_coords, sample_id);

      if (!color_var || !depth_stencil_interleaved(&key->dst.view)) {
         color_var =
            frag_var(b, &key->dst.view, aspect, color_var != NULL ? 1 : 0);
      }

      texel = convert_texel(b, src_fmt, dst_fmt, texel);
      write_frag(b, &key->dst.view, aspect, color_var, texel);
   }

   return b->shader;
}

static VkResult
get_copy_image_gfx_pipeline(struct vk_device *device,
                            struct vk_meta_device *meta,
                            const struct vk_meta_copy_image_key *key,
                            VkPipelineLayout *layout_out,
                            VkPipeline *pipeline_out)
{
   const struct VkDescriptorSetLayoutBinding bindings[] = {
      COPY_SHADER_BINDING(0, SAMPLED_IMAGE, FRAGMENT),
      COPY_SHADER_BINDING(1, SAMPLED_IMAGE, FRAGMENT),
   };

   VkResult result = get_copy_pipeline_layout(
      device, meta, "vk-meta-copy-image-gfx-pipeline-layout",
      VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(struct vk_meta_copy_image_fs_info),
      bindings, ARRAY_SIZE(bindings), layout_out);
   if (unlikely(result != VK_SUCCESS))
      return result;

   return get_gfx_copy_pipeline(
      device, meta, *layout_out, key->samples, build_copy_image_fs,
      key->aspects, &key->dst.view, key, sizeof(*key), pipeline_out);
}

static nir_shader *
build_copy_image_cs(const struct vk_meta_device *meta, const void *key_data)
{
   const struct vk_meta_copy_image_key *key = key_data;

   assert(key->bind_point == VK_PIPELINE_BIND_POINT_COMPUTE);

   nir_builder builder = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL, "vk-meta-copy-image-compute");
   nir_builder *b = &builder;

   b->shader->info.workgroup_size[0] = key->wg_size[0];
   b->shader->info.workgroup_size[1] = key->wg_size[1];
   b->shader->info.workgroup_size[2] = key->wg_size[2];

   nir_def *copy_id = nir_load_global_invocation_id(b, 32);
   nir_def *copy_id_start = nir_vec3(b,
      load_info(b, struct vk_meta_copy_image_cs_info, copy_id_range.start.x),
      load_info(b, struct vk_meta_copy_image_cs_info, copy_id_range.start.y),
      load_info(b, struct vk_meta_copy_image_cs_info, copy_id_range.start.z));
   nir_def *copy_id_end = nir_vec3(b,
      load_info(b, struct vk_meta_copy_image_cs_info, copy_id_range.end.x),
      load_info(b, struct vk_meta_copy_image_cs_info, copy_id_range.end.y),
      load_info(b, struct vk_meta_copy_image_cs_info, copy_id_range.end.z));

   nir_def *in_bounds =
      nir_iand(b, nir_ball(b, nir_uge(b, copy_id, copy_id_start)),
               nir_ball(b, nir_ult(b, copy_id, copy_id_end)));

   nir_push_if(b, in_bounds);

   nir_def *src_offset = nir_vec3(b,
      load_info(b, struct vk_meta_copy_image_cs_info, src_img.offset.x),
      load_info(b, struct vk_meta_copy_image_cs_info, src_img.offset.y),
      load_info(b, struct vk_meta_copy_image_cs_info, src_img.offset.z));
   nir_def *dst_offset = nir_vec3(b,
      load_info(b, struct vk_meta_copy_image_cs_info, dst_img.offset.x),
      load_info(b, struct vk_meta_copy_image_cs_info, dst_img.offset.y),
      load_info(b, struct vk_meta_copy_image_cs_info, dst_img.offset.z));

   nir_def *src_coords = trim_img_coords(b, key->src.view.type,
                                         nir_iadd(b, copy_id, src_offset));
   nir_def *dst_coords = trim_img_coords(b, key->dst.view.type,
                                         nir_iadd(b, copy_id, dst_offset));

   dst_coords = nir_pad_vector_imm_int(b, dst_coords, 0, 4);

   uint32_t binding = 0;
   u_foreach_bit(a, key->aspects) {
      VkImageAspectFlagBits aspect = 1 << a;
      VkFormat src_fmt =
         copy_img_view_format_for_aspect(&key->src.view, aspect);
      VkFormat dst_fmt =
         copy_img_view_format_for_aspect(&key->dst.view, aspect);
      nir_deref_instr *tex =
         tex_deref(b, &key->src.view, aspect, key->samples, binding);
      nir_deref_instr *img =
         img_deref(b, &key->dst.view, aspect, key->samples, binding + 1);

      for (uint32_t s = 0; s < key->samples; s++) {
         nir_def *sample_id =
            key->samples == VK_SAMPLE_COUNT_1_BIT ? NULL : nir_imm_int(b, s);
         nir_def *texel = read_texel(b, tex, src_coords, sample_id);

         texel = convert_texel(b, src_fmt, dst_fmt, texel);
         write_img(b, &key->dst.view, aspect, key->samples, img, dst_coords,
                   sample_id, texel);
      }

      binding += 2;
   }

   nir_pop_if(b, NULL);

   return b->shader;
}

static VkResult
get_copy_image_compute_pipeline(struct vk_device *device,
                                struct vk_meta_device *meta,
                                const struct vk_meta_copy_image_key *key,
                                VkPipelineLayout *layout_out,
                                VkPipeline *pipeline_out)
{
   const VkDescriptorSetLayoutBinding bindings[] = {
      COPY_SHADER_BINDING(0, SAMPLED_IMAGE, COMPUTE),
      COPY_SHADER_BINDING(1, STORAGE_IMAGE, COMPUTE),
      COPY_SHADER_BINDING(2, SAMPLED_IMAGE, COMPUTE),
      COPY_SHADER_BINDING(3, STORAGE_IMAGE, COMPUTE),
   };

   VkResult result = get_copy_pipeline_layout(
      device, meta, "vk-meta-copy-image-compute-pipeline-layout",
      VK_SHADER_STAGE_COMPUTE_BIT, sizeof(struct vk_meta_copy_image_cs_info),
      bindings, ARRAY_SIZE(bindings), layout_out);

   if (unlikely(result != VK_SUCCESS))
      return result;

   return get_compute_copy_pipeline(device, meta, *layout_out,
                                    build_copy_image_cs, key, sizeof(*key),
                                    pipeline_out);
}

static VkResult
copy_image_prepare_gfx_desc_set(
   struct vk_command_buffer *cmd, struct vk_meta_device *meta,
   const struct vk_meta_copy_image_key *key, VkPipelineLayout pipeline_layout,
   struct vk_image *src_img, VkImageLayout src_img_layout,
   struct vk_image *dst_img, VkImageLayout dst_img_layout,
   const VkImageCopy2 *region)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   VkImageAspectFlags aspects = key->aspects;
   VkImageView iviews[] = {
      VK_NULL_HANDLE,
      VK_NULL_HANDLE,
   };
   uint32_t desc_count = 0;

   u_foreach_bit(a, aspects) {
      assert(desc_count < ARRAY_SIZE(iviews));

      VkResult result = copy_create_src_image_view(
         cmd, meta, src_img, &key->src.view, 1 << a, &region->srcSubresource,
         &iviews[desc_count++]);
      if (unlikely(result != VK_SUCCESS))
         return result;
   }

   VkWriteDescriptorSet descs[2] = {
      COPY_PUSH_SET_IMG_DESC(0, SAMPLED, iviews[0], src_img_layout),
      COPY_PUSH_SET_IMG_DESC(1, SAMPLED, iviews[1], src_img_layout),
   };

   disp->CmdPushDescriptorSetKHR(vk_command_buffer_to_handle(cmd),
                                 VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pipeline_layout, 0, desc_count, descs);
   return VK_SUCCESS;
}

static VkResult
copy_image_prepare_compute_desc_set(
   struct vk_command_buffer *cmd, struct vk_meta_device *meta,
   const struct vk_meta_copy_image_key *key, VkPipelineLayout pipeline_layout,
   struct vk_image *src_img, VkImageLayout src_img_layout,
   struct vk_image *dst_img, VkImageLayout dst_img_layout,
   const VkImageCopy2 *region)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   VkImageAspectFlags aspects = key->aspects;
   VkImageView iviews[] = {
      VK_NULL_HANDLE,
      VK_NULL_HANDLE,
      VK_NULL_HANDLE,
      VK_NULL_HANDLE,
   };
   unsigned desc_count = 0;

   u_foreach_bit(a, aspects) {
      VkImageAspectFlagBits aspect = 1 << a;

      assert(desc_count + 2 <= ARRAY_SIZE(iviews));

      VkResult result = copy_create_src_image_view(
         cmd, meta, src_img, &key->src.view, aspect, &region->srcSubresource,
         &iviews[desc_count++]);
      if (unlikely(result != VK_SUCCESS))
         return result;

      result = copy_create_dst_image_view(
         cmd, meta, dst_img, &key->dst.view, aspect, &region->dstOffset,
         &region->extent, &region->dstSubresource,
         VK_PIPELINE_BIND_POINT_COMPUTE, &iviews[desc_count++]);
      if (unlikely(result != VK_SUCCESS))
         return result;
   }

   VkWriteDescriptorSet descs[] = {
      COPY_PUSH_SET_IMG_DESC(0, SAMPLED, iviews[0], src_img_layout),
      COPY_PUSH_SET_IMG_DESC(1, STORAGE, iviews[1], dst_img_layout),
      COPY_PUSH_SET_IMG_DESC(2, SAMPLED, iviews[2], src_img_layout),
      COPY_PUSH_SET_IMG_DESC(3, STORAGE, iviews[3], dst_img_layout),
   };

   disp->CmdPushDescriptorSetKHR(vk_command_buffer_to_handle(cmd),
                                 VK_PIPELINE_BIND_POINT_COMPUTE,
                                 pipeline_layout, 0, desc_count, descs);
   return VK_SUCCESS;
}

enum vk_meta_copy_image_align_policy {
   VK_META_COPY_IMAGE_ALIGN_ON_SRC_TILE,
   VK_META_COPY_IMAGE_ALIGN_ON_DST_TILE,
};

static VkResult
copy_image_prepare_compute_push_const(
   struct vk_command_buffer *cmd, struct vk_meta_device *meta,
   const struct vk_meta_copy_image_key *key, VkPipelineLayout pipeline_layout,
   const struct vk_image *src, const struct vk_image *dst,
   enum vk_meta_copy_image_align_policy align_policy,
   const VkImageCopy2 *region, uint32_t *wg_count)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   VkOffset3D src_offs =
      base_layer_as_offset(key->src.view.type, region->srcOffset,
                           region->srcSubresource.baseArrayLayer);
   uint32_t layer_count =
      vk_image_subresource_layer_count(src, &region->srcSubresource);
   VkExtent3D src_extent =
      layer_count_as_extent(key->src.view.type, region->extent, layer_count);
   VkOffset3D dst_offs =
      base_layer_as_offset(key->dst.view.type, region->dstOffset,
                           region->dstSubresource.baseArrayLayer);

   struct vk_meta_copy_image_cs_info info = {0};

   /* We can't necessarily optimize the read+write path, so align things
    * on the biggest tile size. */
   if (align_policy == VK_META_COPY_IMAGE_ALIGN_ON_SRC_TILE) {
      info.copy_id_range.start.x = src_offs.x % key->wg_size[0];
      info.copy_id_range.start.y = src_offs.y % key->wg_size[1];
      info.copy_id_range.start.z = src_offs.z % key->wg_size[2];
   } else {
      info.copy_id_range.start.x = dst_offs.x % key->wg_size[0];
      info.copy_id_range.start.y = dst_offs.y % key->wg_size[1];
      info.copy_id_range.start.z = dst_offs.z % key->wg_size[2];
   }

   info.copy_id_range.end.x = info.copy_id_range.start.x + src_extent.width;
   info.copy_id_range.end.y = info.copy_id_range.start.y + src_extent.height;
   info.copy_id_range.end.z = info.copy_id_range.start.z + src_extent.depth;

   info.src_img.offset.x = src_offs.x - info.copy_id_range.start.x;
   info.src_img.offset.y = src_offs.y - info.copy_id_range.start.y;
   info.src_img.offset.z = src_offs.z - info.copy_id_range.start.z;
   info.dst_img.offset.x = dst_offs.x - info.copy_id_range.start.x;
   info.dst_img.offset.y = dst_offs.y - info.copy_id_range.start.y;
   info.dst_img.offset.z = dst_offs.z - info.copy_id_range.start.z;
   wg_count[0] = DIV_ROUND_UP(info.copy_id_range.end.x, key->wg_size[0]);
   wg_count[1] = DIV_ROUND_UP(info.copy_id_range.end.y, key->wg_size[1]);
   wg_count[2] = DIV_ROUND_UP(info.copy_id_range.end.z, key->wg_size[2]);

   disp->CmdPushConstants(vk_command_buffer_to_handle(cmd), pipeline_layout,
                          VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(info), &info);

   return VK_SUCCESS;
}

static VkResult
copy_image_prepare_gfx_push_const(struct vk_command_buffer *cmd,
                                  struct vk_meta_device *meta,
                                  const struct vk_meta_copy_image_key *key,
                                  VkPipelineLayout pipeline_layout,
                                  struct vk_image *src_img,
                                  struct vk_image *dst_img,
                                  const VkImageCopy2 *region)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   VkOffset3D src_img_offs =
      base_layer_as_offset(key->src.view.type, region->srcOffset,
                           region->srcSubresource.baseArrayLayer);

   struct vk_meta_copy_image_fs_info info = {
      .dst_to_src_offs = {
         /* The subtraction may lead to negative values, but that's fine
	  * because the shader does the mirror operation thus guaranteeing
	  * a src_coords >= 0. */
         .x = src_img_offs.x - region->dstOffset.x,
         .y = src_img_offs.y - region->dstOffset.y,
         /* Render image view only contains the layers needed for rendering,
          * so we consider the coordinate containing the layer to always be
          * zero.
	  */
         .z = src_img_offs.z,
      },
   };

   disp->CmdPushConstants(vk_command_buffer_to_handle(cmd), pipeline_layout,
                          VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(info), &info);

   return VK_SUCCESS;
}

static void
copy_image_region_gfx(struct vk_command_buffer *cmd,
                      struct vk_meta_device *meta, struct vk_image *src_img,
                      VkImageLayout src_image_layout,
                      const struct vk_meta_copy_image_properties *src_props,
                      struct vk_image *dst_img, VkImageLayout dst_image_layout,
                      const struct vk_meta_copy_image_properties *dst_props,
                      const VkImageCopy2 *region)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;

   /* We only special-case 1D_ARRAY to move the layer ID to the second
    * component instead of the third. For all other view types, let's pick an
    * invalid VkImageViewType value so we don't end up creating the same
    * pipeline multiple times. */
   VkImageViewType dst_view_type =
      dst_img->image_type == VK_IMAGE_TYPE_1D && dst_img->array_layers > 1
         ? VK_IMAGE_VIEW_TYPE_1D_ARRAY
         : (VkImageViewType)-1;

   assert(region->srcSubresource.aspectMask ==
          region->dstSubresource.aspectMask);

   struct vk_meta_copy_image_key key = {
      .key_type = VK_META_OBJECT_KEY_COPY_IMAGE_PIPELINE,
      .bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .samples = src_img->samples,
      .aspects = region->srcSubresource.aspectMask,
      .src.view = img_copy_view_info(vk_image_sampled_view_type(src_img),
                                     region->srcSubresource.aspectMask, src_img,
                                     src_props),
      .dst.view = img_copy_view_info(dst_view_type,
                                     region->dstSubresource.aspectMask, dst_img,
                                     dst_props),
   };

   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkResult result =
      get_copy_image_gfx_pipeline(dev, meta, &key, &pipeline_layout, &pipeline);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   disp->CmdBindPipeline(vk_command_buffer_to_handle(cmd),
                         VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

   result = copy_image_prepare_gfx_desc_set(cmd, meta, &key, pipeline_layout,
                                            src_img, src_image_layout, dst_img,
                                            dst_image_layout, region);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   result = copy_image_prepare_gfx_push_const(cmd, meta, &key, pipeline_layout,
                                              src_img, dst_img, region);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   copy_draw(cmd, meta, dst_img, dst_image_layout, &region->dstSubresource,
             &region->dstOffset, &region->extent, &key.dst.view);
}

static void
copy_image_region_compute(struct vk_command_buffer *cmd,
                          struct vk_meta_device *meta, struct vk_image *src_img,
                          VkImageLayout src_image_layout,
                          const struct vk_meta_copy_image_properties *src_props,
                          struct vk_image *dst_img,
                          VkImageLayout dst_image_layout,
                          const struct vk_meta_copy_image_properties *dst_props,
                          const VkImageCopy2 *region)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   VkImageViewType dst_view_type = vk_image_storage_view_type(dst_img);

   assert(region->srcSubresource.aspectMask ==
          region->dstSubresource.aspectMask);

   struct vk_meta_copy_image_key key = {
      .key_type = VK_META_OBJECT_KEY_COPY_IMAGE_PIPELINE,
      .bind_point = VK_PIPELINE_BIND_POINT_COMPUTE,
      .samples = src_img->samples,
      .aspects = region->srcSubresource.aspectMask,
      .src.view = img_copy_view_info(vk_image_sampled_view_type(src_img),
                                     region->srcSubresource.aspectMask, src_img,
                                     src_props),
      .dst.view = img_copy_view_info(
         dst_view_type, region->dstSubresource.aspectMask, dst_img, dst_props),
   };

   uint32_t src_pix_per_tile = src_props->tile_size.width *
                               src_props->tile_size.height *
                               src_props->tile_size.depth;
   uint32_t dst_pix_per_tile = dst_props->tile_size.width *
                               dst_props->tile_size.height *
                               dst_props->tile_size.depth;
   enum vk_meta_copy_image_align_policy align_policy;

   if (src_pix_per_tile >= dst_pix_per_tile) {
      key.wg_size[0] = src_props->tile_size.width;
      key.wg_size[1] = src_props->tile_size.height;
      key.wg_size[2] = src_props->tile_size.depth;
      align_policy = VK_META_COPY_IMAGE_ALIGN_ON_SRC_TILE;
   } else {
      key.wg_size[0] = dst_props->tile_size.width;
      key.wg_size[1] = dst_props->tile_size.height;
      key.wg_size[2] = dst_props->tile_size.depth;
      align_policy = VK_META_COPY_IMAGE_ALIGN_ON_DST_TILE;
   }

   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   VkResult result = get_copy_image_compute_pipeline(
      dev, meta, &key, &pipeline_layout, &pipeline);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   disp->CmdBindPipeline(vk_command_buffer_to_handle(cmd),
                         VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   result = copy_image_prepare_compute_desc_set(
      cmd, meta, &key, pipeline_layout, src_img, src_image_layout, dst_img,
      dst_image_layout, region);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   assert(key.wg_size[0] && key.wg_size[1] && key.wg_size[2]);

   uint32_t wg_count[3] = {0};

   result = copy_image_prepare_compute_push_const(
      cmd, meta, &key, pipeline_layout, src_img, dst_img, align_policy, region,
      wg_count);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   disp->CmdDispatch(vk_command_buffer_to_handle(cmd), wg_count[0], wg_count[1],
                     wg_count[2]);
}

void
vk_meta_copy_image(struct vk_command_buffer *cmd, struct vk_meta_device *meta,
                   const VkCopyImageInfo2 *info,
                   const struct vk_meta_copy_image_properties *src_props,
                   const struct vk_meta_copy_image_properties *dst_props,
                   VkPipelineBindPoint bind_point)
{
   VK_FROM_HANDLE(vk_image, src_img, info->srcImage);
   VK_FROM_HANDLE(vk_image, dst_img, info->dstImage);

   for (uint32_t i = 0; i < info->regionCount; i++) {
      VkImageCopy2 region = info->pRegions[i];

      region.extent = vk_image_extent_to_elements(src_img, region.extent);
      region.srcOffset = vk_image_offset_to_elements(src_img, region.srcOffset);
      region.dstOffset = vk_image_offset_to_elements(dst_img, region.dstOffset);

      if (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
         copy_image_region_gfx(cmd, meta, src_img, info->srcImageLayout,
                               src_props, dst_img, info->dstImageLayout,
                               dst_props, &region);
      } else {
         copy_image_region_compute(cmd, meta, src_img, info->srcImageLayout,
                                   src_props, dst_img, info->dstImageLayout,
                                   dst_props, &region);
      }
   }
}

static nir_shader *
build_copy_buffer_shader(const struct vk_meta_device *meta,
                         const void *key_data)
{
   const struct vk_meta_copy_buffer_key *key = key_data;
   nir_builder builder = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL, "vk-meta-copy-buffer");
   nir_builder *b = &builder;

   b->shader->info.workgroup_size[0] =
      vk_meta_buffer_access_wg_size(meta, key->chunk_size);
   b->shader->info.workgroup_size[1] = 1;
   b->shader->info.workgroup_size[2] = 1;

   uint32_t chunk_bit_size, chunk_comp_count;

   assert(util_is_power_of_two_nonzero(key->chunk_size));
   if (key->chunk_size <= 4) {
      chunk_bit_size = key->chunk_size * 8;
      chunk_comp_count = 1;
   } else {
      chunk_bit_size = 32;
      chunk_comp_count = key->chunk_size / 4;
   }

   assert(chunk_comp_count < NIR_MAX_VEC_COMPONENTS);

   nir_def *global_id = nir_load_global_invocation_id(b, 32);
   nir_def *copy_id = nir_channel(b, global_id, 0);
   nir_def *offset = nir_imul_imm(b, copy_id, key->chunk_size);
   nir_def *size = load_info(b, struct vk_meta_copy_buffer_info, size);

   nir_push_if(b, nir_ult(b, offset, size));

   offset = nir_u2u64(b, offset);

   nir_def *src_addr = load_info(b, struct vk_meta_copy_buffer_info, src_addr);
   nir_def *dst_addr = nir_load_push_constant(b, 1, 64, nir_imm_int(b, 8));
   nir_def *data = nir_build_load_global(b, chunk_comp_count, chunk_bit_size,
                                         nir_iadd(b, src_addr, offset),
                                         .align_mul = chunk_bit_size / 8);

   nir_build_store_global(b, data, nir_iadd(b, dst_addr, offset),
                          .align_mul = key->chunk_size);

   nir_pop_if(b, NULL);

   return b->shader;
}

static VkResult
get_copy_buffer_pipeline(struct vk_device *device, struct vk_meta_device *meta,
                         const struct vk_meta_copy_buffer_key *key,
                         VkPipelineLayout *layout_out, VkPipeline *pipeline_out)
{
   VkResult result = get_copy_pipeline_layout(
      device, meta, "vk-meta-copy-buffer-pipeline-layout",
      VK_SHADER_STAGE_COMPUTE_BIT, sizeof(struct vk_meta_copy_buffer_info),
      NULL, 0, layout_out);

   if (unlikely(result != VK_SUCCESS))
      return result;

   return get_compute_copy_pipeline(device, meta, *layout_out,
                                    build_copy_buffer_shader, key, sizeof(*key),
                                    pipeline_out);
}

static void
copy_buffer_region(struct vk_command_buffer *cmd, struct vk_meta_device *meta,
                   VkBuffer src, VkBuffer dst, const VkBufferCopy2 *region)
{
   struct vk_device *dev = cmd->base.device;
   const struct vk_physical_device *pdev = dev->physical;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   VkResult result;

   struct vk_meta_copy_buffer_key key = {
      .key_type = VK_META_OBJECT_KEY_COPY_BUFFER_PIPELINE,
   };

   VkDeviceSize size = region->size;
   VkDeviceAddress src_addr =
      vk_meta_buffer_address(dev, src, region->srcOffset, size);
   VkDeviceAddress dst_addr =
      vk_meta_buffer_address(dev, dst, region->dstOffset, size);

   /* Combine the size and src/dst address to extract the alignment. */
   uint64_t align = src_addr | dst_addr | size;

   assert(align != 0);

   /* Pick the first power-of-two of the combined src/dst address and size as
    * our alignment. We limit the chunk size to 16 bytes (a uvec4) for now.
    */
   key.chunk_size = MIN2(16, 1 << (ffs(align) - 1));

   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   result =
      get_copy_buffer_pipeline(dev, meta, &key, &pipeline_layout, &pipeline);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   disp->CmdBindPipeline(vk_command_buffer_to_handle(cmd),
                         VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   const uint32_t optimal_wg_size =
      vk_meta_buffer_access_wg_size(meta, key.chunk_size);
   const uint32_t per_wg_copy_size = optimal_wg_size * key.chunk_size;
   uint32_t max_per_dispatch_size =
      pdev->properties.maxComputeWorkGroupCount[0] * per_wg_copy_size;

   assert(optimal_wg_size <= pdev->properties.maxComputeWorkGroupSize[0]);

   while (size) {
      struct vk_meta_copy_buffer_info args = {
         .size = MIN2(size, max_per_dispatch_size),
         .src_addr = src_addr,
         .dst_addr = dst_addr,
      };
      uint32_t wg_count = DIV_ROUND_UP(args.size, per_wg_copy_size);

      disp->CmdPushConstants(vk_command_buffer_to_handle(cmd), pipeline_layout,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args),
                             &args);

      disp->CmdDispatch(vk_command_buffer_to_handle(cmd), wg_count, 1, 1);

      src_addr += args.size;
      dst_addr += args.size;
      size -= args.size;
   }
}

void
vk_meta_copy_buffer(struct vk_command_buffer *cmd, struct vk_meta_device *meta,
                    const VkCopyBufferInfo2 *info)
{
   for (unsigned i = 0; i < info->regionCount; i++) {
      const VkBufferCopy2 *region = &info->pRegions[i];

      copy_buffer_region(cmd, meta, info->srcBuffer, info->dstBuffer, region);
   }
}

void
vk_meta_update_buffer(struct vk_command_buffer *cmd,
                      struct vk_meta_device *meta, VkBuffer buffer,
                      VkDeviceSize offset, VkDeviceSize size, const void *data)
{
   VkResult result;

   const VkBufferCreateInfo tmp_buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &cmd->pool->queue_family_index,
   };

   VkBuffer tmp_buffer;
   result = vk_meta_create_buffer(cmd, meta, &tmp_buffer_info, &tmp_buffer);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   void *tmp_buffer_map;
   result = meta->cmd_bind_map_buffer(cmd, meta, tmp_buffer, &tmp_buffer_map);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   memcpy(tmp_buffer_map, data, size);

   const VkBufferCopy2 copy_region = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
      .srcOffset = 0,
      .dstOffset = offset,
      .size = size,
   };
   const VkCopyBufferInfo2 copy_info = {
      .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
      .srcBuffer = tmp_buffer,
      .dstBuffer = buffer,
      .regionCount = 1,
      .pRegions = &copy_region,
   };

   vk_meta_copy_buffer(cmd, meta, &copy_info);
}

static nir_shader *
build_fill_buffer_shader(const struct vk_meta_device *meta,
                         UNUSED const void *key_data)
{
   nir_builder builder = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL, "vk-meta-fill-buffer");
   nir_builder *b = &builder;

   b->shader->info.workgroup_size[0] = vk_meta_buffer_access_wg_size(meta, 4);
   b->shader->info.workgroup_size[1] = 1;
   b->shader->info.workgroup_size[2] = 1;

   nir_def *global_id = nir_load_global_invocation_id(b, 32);
   nir_def *copy_id = nir_channel(b, global_id, 0);
   nir_def *offset = nir_imul_imm(b, copy_id, 4);
   nir_def *size = load_info(b, struct vk_meta_fill_buffer_info, size);
   nir_def *data = load_info(b, struct vk_meta_fill_buffer_info, data);

   nir_push_if(b, nir_ult(b, offset, size));

   offset = nir_u2u64(b, offset);

   nir_def *buf_addr =
      load_info(b, struct vk_meta_fill_buffer_info, buf_addr);

   nir_build_store_global(b, data, nir_iadd(b, buf_addr, offset),
                          .align_mul = 4);

   nir_pop_if(b, NULL);

   return b->shader;
}

static VkResult
get_fill_buffer_pipeline(struct vk_device *device, struct vk_meta_device *meta,
                         const struct vk_meta_fill_buffer_key *key,
                         VkPipelineLayout *layout_out, VkPipeline *pipeline_out)
{
   VkResult result = get_copy_pipeline_layout(
      device, meta, "vk-meta-fill-buffer-pipeline-layout",
      VK_SHADER_STAGE_COMPUTE_BIT, sizeof(struct vk_meta_fill_buffer_info), NULL, 0,
      layout_out);
   if (unlikely(result != VK_SUCCESS))
      return result;

   return get_compute_copy_pipeline(device, meta, *layout_out,
                                    build_fill_buffer_shader, key, sizeof(*key),
                                    pipeline_out);
}

void
vk_meta_fill_buffer(struct vk_command_buffer *cmd, struct vk_meta_device *meta,
                    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
                    uint32_t data)
{
   VK_FROM_HANDLE(vk_buffer, buf, buffer);
   struct vk_device *dev = cmd->base.device;
   const struct vk_physical_device *pdev = dev->physical;
   const struct vk_device_dispatch_table *disp = &dev->dispatch_table;
   VkResult result;

   struct vk_meta_fill_buffer_key key = {
      .key_type = VK_META_OBJECT_KEY_FILL_BUFFER_PIPELINE,
   };

   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
   result =
      get_fill_buffer_pipeline(dev, meta, &key, &pipeline_layout, &pipeline);
   if (unlikely(result != VK_SUCCESS)) {
      vk_command_buffer_set_error(cmd, result);
      return;
   }

   disp->CmdBindPipeline(vk_command_buffer_to_handle(cmd),
                         VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   /* From the Vulkan 1.3.290 spec:
    *
    *   "If VK_WHOLE_SIZE is used and the remaining size of the buffer is not a
    *    multiple of 4, then the nearest smaller multiple is used."
    *
    * hence the mask to align the size on 4 bytes here.
    */
   size = vk_buffer_range(buf, offset, size) & ~3u;

   const uint32_t optimal_wg_size = vk_meta_buffer_access_wg_size(meta, 4);
   const uint32_t per_wg_copy_size = optimal_wg_size * 4;
   uint32_t max_per_dispatch_size =
      pdev->properties.maxComputeWorkGroupCount[0] * per_wg_copy_size;

   while (size > 0) {
      struct vk_meta_fill_buffer_info args = {
         .size = MIN2(size, max_per_dispatch_size),
         .buf_addr = vk_meta_buffer_address(dev, buffer, offset, size),
         .data = data,
      };
      uint32_t wg_count = DIV_ROUND_UP(args.size, per_wg_copy_size);

      disp->CmdPushConstants(vk_command_buffer_to_handle(cmd), pipeline_layout,
                             VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(args),
                             &args);

      disp->CmdDispatch(vk_command_buffer_to_handle(cmd), wg_count, 1, 1);

      offset += args.size;
      size -= args.size;
   }
}
