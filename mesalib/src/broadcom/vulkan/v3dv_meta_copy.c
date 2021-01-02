/*
 * Copyright © 2019 Raspberry Pi
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

#include "v3dv_private.h"

#include "compiler/nir/nir_builder.h"
#include "broadcom/cle/v3dx_pack.h"
#include "vk_format_info.h"
#include "util/u_pack_color.h"

static uint32_t
meta_blit_key_hash(const void *key)
{
   return _mesa_hash_data(key, V3DV_META_BLIT_CACHE_KEY_SIZE);
}

static bool
meta_blit_key_compare(const void *key1, const void *key2)
{
   return memcmp(key1, key2, V3DV_META_BLIT_CACHE_KEY_SIZE) == 0;
}

static bool
create_blit_pipeline_layout(struct v3dv_device *device,
                            VkDescriptorSetLayout *descriptor_set_layout,
                            VkPipelineLayout *pipeline_layout)
{
   VkResult result;

   if (*descriptor_set_layout == 0) {
      VkDescriptorSetLayoutBinding descriptor_set_layout_binding = {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      };
      VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .bindingCount = 1,
         .pBindings = &descriptor_set_layout_binding,
      };
      result =
         v3dv_CreateDescriptorSetLayout(v3dv_device_to_handle(device),
                                        &descriptor_set_layout_info,
                                        &device->vk.alloc,
                                        descriptor_set_layout);
      if (result != VK_SUCCESS)
         return false;
   }

   assert(*pipeline_layout == 0);
   VkPipelineLayoutCreateInfo pipeline_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = descriptor_set_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges =
         &(VkPushConstantRange) { VK_SHADER_STAGE_VERTEX_BIT, 0, 20 },
   };

   result =
      v3dv_CreatePipelineLayout(v3dv_device_to_handle(device),
                                &pipeline_layout_info,
                                &device->vk.alloc,
                                pipeline_layout);
   return result == VK_SUCCESS;
}

void
v3dv_meta_blit_init(struct v3dv_device *device)
{
   for (uint32_t i = 0; i < 3; i++) {
      device->meta.blit.cache[i] =
         _mesa_hash_table_create(NULL,
                                 meta_blit_key_hash,
                                 meta_blit_key_compare);
   }

   create_blit_pipeline_layout(device,
                               &device->meta.blit.ds_layout,
                               &device->meta.blit.p_layout);
}

void
v3dv_meta_blit_finish(struct v3dv_device *device)
{
   VkDevice _device = v3dv_device_to_handle(device);

   for (uint32_t i = 0; i < 3; i++) {
      hash_table_foreach(device->meta.blit.cache[i], entry) {
         struct v3dv_meta_blit_pipeline *item = entry->data;
         v3dv_DestroyPipeline(_device, item->pipeline, &device->vk.alloc);
         v3dv_DestroyRenderPass(_device, item->pass, &device->vk.alloc);
         v3dv_DestroyRenderPass(_device, item->pass_no_load, &device->vk.alloc);
         vk_free(&device->vk.alloc, item);
      }
      _mesa_hash_table_destroy(device->meta.blit.cache[i], NULL);
   }

   if (device->meta.blit.p_layout) {
      v3dv_DestroyPipelineLayout(_device, device->meta.blit.p_layout,
                                 &device->vk.alloc);
   }

   if (device->meta.blit.ds_layout) {
      v3dv_DestroyDescriptorSetLayout(_device, device->meta.blit.ds_layout,
                                      &device->vk.alloc);
   }
}

static uint32_t
meta_texel_buffer_copy_key_hash(const void *key)
{
   return _mesa_hash_data(key, V3DV_META_TEXEL_BUFFER_COPY_CACHE_KEY_SIZE);
}

static bool
meta_texel_buffer_copy_key_compare(const void *key1, const void *key2)
{
   return memcmp(key1, key2, V3DV_META_TEXEL_BUFFER_COPY_CACHE_KEY_SIZE) == 0;
}

static bool
create_texel_buffer_copy_pipeline_layout(struct v3dv_device *device,
                                         VkDescriptorSetLayout *ds_layout,
                                         VkPipelineLayout *p_layout)
{
   VkResult result;

   if (*ds_layout == 0) {
      VkDescriptorSetLayoutBinding ds_layout_binding = {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      };
      VkDescriptorSetLayoutCreateInfo ds_layout_info = {
         .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .bindingCount = 1,
         .pBindings = &ds_layout_binding,
      };
      result =
         v3dv_CreateDescriptorSetLayout(v3dv_device_to_handle(device),
                                        &ds_layout_info,
                                        &device->vk.alloc,
                                        ds_layout);
      if (result != VK_SUCCESS)
         return false;
   }

   assert(*p_layout == 0);
   VkPipelineLayoutCreateInfo p_layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges =
         &(VkPushConstantRange) { VK_SHADER_STAGE_FRAGMENT_BIT, 0, 20 },
   };

   result =
      v3dv_CreatePipelineLayout(v3dv_device_to_handle(device),
                                &p_layout_info,
                                &device->vk.alloc,
                                p_layout);
   return result == VK_SUCCESS;
}

void
v3dv_meta_texel_buffer_copy_init(struct v3dv_device *device)
{
   for (uint32_t i = 0; i < 3; i++) {
      device->meta.texel_buffer_copy.cache[i] =
         _mesa_hash_table_create(NULL,
                                 meta_texel_buffer_copy_key_hash,
                                 meta_texel_buffer_copy_key_compare);
   }

   create_texel_buffer_copy_pipeline_layout(
      device,
      &device->meta.texel_buffer_copy.ds_layout,
      &device->meta.texel_buffer_copy.p_layout);
}

void
v3dv_meta_texel_buffer_copy_finish(struct v3dv_device *device)
{
   VkDevice _device = v3dv_device_to_handle(device);

   for (uint32_t i = 0; i < 3; i++) {
      hash_table_foreach(device->meta.texel_buffer_copy.cache[i], entry) {
         struct v3dv_meta_texel_buffer_copy_pipeline *item = entry->data;
         v3dv_DestroyPipeline(_device, item->pipeline, &device->vk.alloc);
         v3dv_DestroyRenderPass(_device, item->pass, &device->vk.alloc);
         v3dv_DestroyRenderPass(_device, item->pass_no_load, &device->vk.alloc);
         vk_free(&device->vk.alloc, item);
      }
      _mesa_hash_table_destroy(device->meta.texel_buffer_copy.cache[i], NULL);
   }

   if (device->meta.texel_buffer_copy.p_layout) {
      v3dv_DestroyPipelineLayout(_device, device->meta.texel_buffer_copy.p_layout,
                                 &device->vk.alloc);
   }

   if (device->meta.texel_buffer_copy.ds_layout) {
      v3dv_DestroyDescriptorSetLayout(_device, device->meta.texel_buffer_copy.ds_layout,
                                      &device->vk.alloc);
   }
}

static inline bool
can_use_tlb(struct v3dv_image *image,
            const VkOffset3D *offset,
            VkFormat *compat_format);

/**
 * Copy operations implemented in this file don't operate on a framebuffer
 * object provided by the user, however, since most use the TLB for this,
 * we still need to have some representation of the framebuffer. For the most
 * part, the job's frame tiling information is enough for this, however we
 * still need additional information such us the internal type of our single
 * render target, so we use this auxiliary struct to pass that information
 * around.
 */
struct framebuffer_data {
   /* The internal type of the single render target */
   uint32_t internal_type;

   /* Supertile coverage */
   uint32_t min_x_supertile;
   uint32_t min_y_supertile;
   uint32_t max_x_supertile;
   uint32_t max_y_supertile;

   /* Format info */
   VkFormat vk_format;
   const struct v3dv_format *format;
   uint8_t internal_depth_type;
};

static void
setup_framebuffer_data(struct framebuffer_data *fb,
                       VkFormat vk_format,
                       uint32_t internal_type,
                       const struct v3dv_frame_tiling *tiling)
{
   fb->internal_type = internal_type;

   /* Supertile coverage always starts at 0,0  */
   uint32_t supertile_w_in_pixels =
      tiling->tile_width * tiling->supertile_width;
   uint32_t supertile_h_in_pixels =
      tiling->tile_height * tiling->supertile_height;

   fb->min_x_supertile = 0;
   fb->min_y_supertile = 0;
   fb->max_x_supertile = (tiling->width - 1) / supertile_w_in_pixels;
   fb->max_y_supertile = (tiling->height - 1) / supertile_h_in_pixels;

   fb->vk_format = vk_format;
   fb->format = v3dv_get_format(vk_format);

   fb->internal_depth_type = V3D_INTERNAL_TYPE_DEPTH_32F;
   if (vk_format_is_depth_or_stencil(vk_format))
      fb->internal_depth_type = v3dv_get_internal_depth_type(vk_format);
}

/* This chooses a tile buffer format that is appropriate for the copy operation.
 * Typically, this is the image render target type, however, if we are copying
 * depth/stencil to/from a buffer the hardware can't do raster loads/stores, so
 * we need to load and store to/from a tile color buffer using a compatible
 * color format.
 */
static uint32_t
choose_tlb_format(struct framebuffer_data *framebuffer,
                  VkImageAspectFlags aspect,
                  bool for_store,
                  bool is_copy_to_buffer,
                  bool is_copy_from_buffer)
{
   if (is_copy_to_buffer || is_copy_from_buffer) {
      switch (framebuffer->vk_format) {
      case VK_FORMAT_D16_UNORM:
         return V3D_OUTPUT_IMAGE_FORMAT_R16UI;
      case VK_FORMAT_D32_SFLOAT:
         return V3D_OUTPUT_IMAGE_FORMAT_R32F;
      case VK_FORMAT_X8_D24_UNORM_PACK32:
         return V3D_OUTPUT_IMAGE_FORMAT_RGBA8UI;
      case VK_FORMAT_D24_UNORM_S8_UINT:
         /* When storing the stencil aspect of a combined depth/stencil image
          * to a buffer, the Vulkan spec states that the output buffer must
          * have packed stencil values, so we choose an R8UI format for our
          * store outputs. For the load input we still want RGBA8UI since the
          * source image contains 4 channels (including the 3 channels
          * containing the 24-bit depth value).
          *
          * When loading the stencil aspect of a combined depth/stencil image
          * from a buffer, we read packed 8-bit stencil values from the buffer
          * that we need to put into the LSB of the 32-bit format (the R
          * channel), so we use R8UI. For the store, if we used R8UI then we
          * would write 8-bit stencil values consecutively over depth channels,
          * so we need to use RGBA8UI. This will write each stencil value in
          * its correct position, but will overwrite depth values (channels G
          * B,A) with undefined values. To fix this,  we will have to restore
          * the depth aspect from the Z tile buffer, which we should pre-load
          * from the image before the store).
          */
         if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
            return V3D_OUTPUT_IMAGE_FORMAT_RGBA8UI;
         } else {
            assert(aspect & VK_IMAGE_ASPECT_STENCIL_BIT);
            if (is_copy_to_buffer) {
               return for_store ? V3D_OUTPUT_IMAGE_FORMAT_R8UI :
                                  V3D_OUTPUT_IMAGE_FORMAT_RGBA8UI;
            } else {
               assert(is_copy_from_buffer);
               return for_store ? V3D_OUTPUT_IMAGE_FORMAT_RGBA8UI :
                                  V3D_OUTPUT_IMAGE_FORMAT_R8UI;
            }
         }
      default: /* Color formats */
         return framebuffer->format->rt_type;
         break;
      }
   } else {
      return framebuffer->format->rt_type;
   }
}

static inline bool
format_needs_rb_swap(VkFormat format)
{
   const uint8_t *swizzle = v3dv_get_format_swizzle(format);
   return swizzle[0] == PIPE_SWIZZLE_Z;
}

static void
get_internal_type_bpp_for_image_aspects(VkFormat vk_format,
                                        VkImageAspectFlags aspect_mask,
                                        uint32_t *internal_type,
                                        uint32_t *internal_bpp)
{
   const VkImageAspectFlags ds_aspects = VK_IMAGE_ASPECT_DEPTH_BIT |
                                         VK_IMAGE_ASPECT_STENCIL_BIT;

   /* We can't store depth/stencil pixel formats to a raster format, so
    * so instead we load our depth/stencil aspects to a compatible color
    * format.
    */
   /* FIXME: pre-compute this at image creation time? */
   if (aspect_mask & ds_aspects) {
      switch (vk_format) {
      case VK_FORMAT_D16_UNORM:
         *internal_type = V3D_INTERNAL_TYPE_16UI;
         *internal_bpp = V3D_INTERNAL_BPP_64;
         break;
      case VK_FORMAT_D32_SFLOAT:
         *internal_type = V3D_INTERNAL_TYPE_32F;
         *internal_bpp = V3D_INTERNAL_BPP_128;
         break;
      case VK_FORMAT_X8_D24_UNORM_PACK32:
      case VK_FORMAT_D24_UNORM_S8_UINT:
         /* Use RGBA8 format so we can relocate the X/S bits in the appropriate
          * place to match Vulkan expectations. See the comment on the tile
          * load command for more details.
          */
         *internal_type = V3D_INTERNAL_TYPE_8UI;
         *internal_bpp = V3D_INTERNAL_BPP_32;
         break;
      default:
         assert(!"unsupported format");
         break;
      }
   } else {
      const struct v3dv_format *format = v3dv_get_format(vk_format);
      v3dv_get_internal_type_bpp_for_output_format(format->rt_type,
                                                   internal_type,
                                                   internal_bpp);
   }
}

struct rcl_clear_info {
   const union v3dv_clear_value *clear_value;
   struct v3dv_image *image;
   VkImageAspectFlags aspects;
   uint32_t layer;
   uint32_t level;
};

static struct v3dv_cl *
emit_rcl_prologue(struct v3dv_job *job,
                  struct framebuffer_data *fb,
                  const struct rcl_clear_info *clear_info)
{
   const struct v3dv_frame_tiling *tiling = &job->frame_tiling;

   struct v3dv_cl *rcl = &job->rcl;
   v3dv_cl_ensure_space_with_branch(rcl, 200 +
                                    tiling->layers * 256 *
                                    cl_packet_length(SUPERTILE_COORDINATES));
   if (job->cmd_buffer->state.oom)
      return NULL;

   cl_emit(rcl, TILE_RENDERING_MODE_CFG_COMMON, config) {
      config.early_z_disable = true;
      config.image_width_pixels = tiling->width;
      config.image_height_pixels = tiling->height;
      config.number_of_render_targets = 1;
      config.multisample_mode_4x = tiling->msaa;
      config.maximum_bpp_of_all_render_targets = tiling->internal_bpp;
      config.internal_depth_type = fb->internal_depth_type;
   }

   if (clear_info && (clear_info->aspects & VK_IMAGE_ASPECT_COLOR_BIT)) {
      uint32_t clear_pad = 0;
      if (clear_info->image) {
         const struct v3dv_image *image = clear_info->image;
         const struct v3d_resource_slice *slice =
            &image->slices[clear_info->level];
         if (slice->tiling == VC5_TILING_UIF_NO_XOR ||
             slice->tiling == VC5_TILING_UIF_XOR) {
            int uif_block_height = v3d_utile_height(image->cpp) * 2;

            uint32_t implicit_padded_height =
               align(tiling->height, uif_block_height) / uif_block_height;

            if (slice->padded_height_of_output_image_in_uif_blocks -
                implicit_padded_height >= 15) {
               clear_pad = slice->padded_height_of_output_image_in_uif_blocks;
            }
         }
      }

      const uint32_t *color = &clear_info->clear_value->color[0];
      cl_emit(rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART1, clear) {
         clear.clear_color_low_32_bits = color[0];
         clear.clear_color_next_24_bits = color[1] & 0x00ffffff;
         clear.render_target_number = 0;
      };

      if (tiling->internal_bpp >= V3D_INTERNAL_BPP_64) {
         cl_emit(rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART2, clear) {
            clear.clear_color_mid_low_32_bits =
              ((color[1] >> 24) | (color[2] << 8));
            clear.clear_color_mid_high_24_bits =
              ((color[2] >> 24) | ((color[3] & 0xffff) << 8));
            clear.render_target_number = 0;
         };
      }

      if (tiling->internal_bpp >= V3D_INTERNAL_BPP_128 || clear_pad) {
         cl_emit(rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART3, clear) {
            clear.uif_padded_height_in_uif_blocks = clear_pad;
            clear.clear_color_high_16_bits = color[3] >> 16;
            clear.render_target_number = 0;
         };
      }
   }

   cl_emit(rcl, TILE_RENDERING_MODE_CFG_COLOR, rt) {
      rt.render_target_0_internal_bpp = tiling->internal_bpp;
      rt.render_target_0_internal_type = fb->internal_type;
      rt.render_target_0_clamp = V3D_RENDER_TARGET_CLAMP_NONE;
   }

   cl_emit(rcl, TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES, clear) {
      clear.z_clear_value = clear_info ? clear_info->clear_value->z : 1.0f;
      clear.stencil_clear_value = clear_info ? clear_info->clear_value->s : 0;
   };

   cl_emit(rcl, TILE_LIST_INITIAL_BLOCK_SIZE, init) {
      init.use_auto_chained_tile_lists = true;
      init.size_of_first_block_in_chained_tile_lists =
         TILE_ALLOCATION_BLOCK_SIZE_64B;
   }

   return rcl;
}

static void
emit_frame_setup(struct v3dv_job *job,
                 uint32_t layer,
                 const union v3dv_clear_value *clear_value)
{
   v3dv_return_if_oom(NULL, job);

   const struct v3dv_frame_tiling *tiling = &job->frame_tiling;

   struct v3dv_cl *rcl = &job->rcl;

   const uint32_t tile_alloc_offset =
      64 * layer * tiling->draw_tiles_x * tiling->draw_tiles_y;
   cl_emit(rcl, MULTICORE_RENDERING_TILE_LIST_SET_BASE, list) {
      list.address = v3dv_cl_address(job->tile_alloc, tile_alloc_offset);
   }

   cl_emit(rcl, MULTICORE_RENDERING_SUPERTILE_CFG, config) {
      config.number_of_bin_tile_lists = 1;
      config.total_frame_width_in_tiles = tiling->draw_tiles_x;
      config.total_frame_height_in_tiles = tiling->draw_tiles_y;

      config.supertile_width_in_tiles = tiling->supertile_width;
      config.supertile_height_in_tiles = tiling->supertile_height;

      config.total_frame_width_in_supertiles =
         tiling->frame_width_in_supertiles;
      config.total_frame_height_in_supertiles =
         tiling->frame_height_in_supertiles;
   }

   /* Implement GFXH-1742 workaround. Also, if we are clearing we have to do
    * it here.
    */
   for (int i = 0; i < 2; i++) {
      cl_emit(rcl, TILE_COORDINATES, coords);
      cl_emit(rcl, END_OF_LOADS, end);
      cl_emit(rcl, STORE_TILE_BUFFER_GENERAL, store) {
         store.buffer_to_store = NONE;
      }
      if (clear_value && i == 0) {
         cl_emit(rcl, CLEAR_TILE_BUFFERS, clear) {
            clear.clear_z_stencil_buffer = true;
            clear.clear_all_render_targets = true;
         }
      }
      cl_emit(rcl, END_OF_TILE_MARKER, end);
   }

   cl_emit(rcl, FLUSH_VCD_CACHE, flush);
}

static void
emit_supertile_coordinates(struct v3dv_job *job,
                           struct framebuffer_data *framebuffer)
{
   v3dv_return_if_oom(NULL, job);

   struct v3dv_cl *rcl = &job->rcl;

   const uint32_t min_y = framebuffer->min_y_supertile;
   const uint32_t max_y = framebuffer->max_y_supertile;
   const uint32_t min_x = framebuffer->min_x_supertile;
   const uint32_t max_x = framebuffer->max_x_supertile;

   for (int y = min_y; y <= max_y; y++) {
      for (int x = min_x; x <= max_x; x++) {
         cl_emit(rcl, SUPERTILE_COORDINATES, coords) {
            coords.column_number_in_supertiles = x;
            coords.row_number_in_supertiles = y;
         }
      }
   }
}

static void
emit_linear_load(struct v3dv_cl *cl,
                 uint32_t buffer,
                 struct v3dv_bo *bo,
                 uint32_t offset,
                 uint32_t stride,
                 uint32_t format)
{
   cl_emit(cl, LOAD_TILE_BUFFER_GENERAL, load) {
      load.buffer_to_load = buffer;
      load.address = v3dv_cl_address(bo, offset);
      load.input_image_format = format;
      load.memory_format = VC5_TILING_RASTER;
      load.height_in_ub_or_stride = stride;
      load.decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0;
   }
}

static void
emit_linear_store(struct v3dv_cl *cl,
                  uint32_t buffer,
                  struct v3dv_bo *bo,
                  uint32_t offset,
                  uint32_t stride,
                  bool msaa,
                  uint32_t format)
{
   cl_emit(cl, STORE_TILE_BUFFER_GENERAL, store) {
      store.buffer_to_store = RENDER_TARGET_0;
      store.address = v3dv_cl_address(bo, offset);
      store.clear_buffer_being_stored = false;
      store.output_image_format = format;
      store.memory_format = VC5_TILING_RASTER;
      store.height_in_ub_or_stride = stride;
      store.decimate_mode = msaa ? V3D_DECIMATE_MODE_ALL_SAMPLES :
                                   V3D_DECIMATE_MODE_SAMPLE_0;
   }
}

static void
emit_image_load(struct v3dv_cl *cl,
                struct framebuffer_data *framebuffer,
                struct v3dv_image *image,
                VkImageAspectFlags aspect,
                uint32_t layer,
                uint32_t mip_level,
                bool is_copy_to_buffer,
                bool is_copy_from_buffer)
{
   uint32_t layer_offset = v3dv_layer_offset(image, mip_level, layer);

   /* For image to/from buffer copies we always load to and store from RT0,
    * even for depth/stencil aspects, because the hardware can't do raster
    * stores or loads from/to the depth/stencil tile buffers.
    */
   bool load_to_color_tlb = is_copy_to_buffer || is_copy_from_buffer ||
                            aspect == VK_IMAGE_ASPECT_COLOR_BIT;

   const struct v3d_resource_slice *slice = &image->slices[mip_level];
   cl_emit(cl, LOAD_TILE_BUFFER_GENERAL, load) {
      load.buffer_to_load = load_to_color_tlb ?
         RENDER_TARGET_0 : v3dv_zs_buffer_from_aspect_bits(aspect);

      load.address = v3dv_cl_address(image->mem->bo, layer_offset);

      load.input_image_format = choose_tlb_format(framebuffer, aspect, false,
                                                  is_copy_to_buffer,
                                                  is_copy_from_buffer);
      load.memory_format = slice->tiling;

      /* When copying depth/stencil images to a buffer, for D24 formats Vulkan
       * expects the depth value in the LSB bits of each 32-bit pixel.
       * Unfortunately, the hardware seems to put the S8/X8 bits there and the
       * depth bits on the MSB. To work around that we can reverse the channel
       * order and then swap the R/B channels to get what we want.
       *
       * NOTE: reversing and swapping only gets us the behavior we want if the
       * operations happen in that exact order, which seems to be the case when
       * done on the tile buffer load operations. On the store, it seems the
       * order is not the same. The order on the store is probably reversed so
       * that reversing and swapping on both the load and the store preserves
       * the original order of the channels in memory.
       *
       * Notice that we only need to do this when copying to a buffer, where
       * depth and stencil aspects are copied as separate regions and
       * the spec expects them to be tightly packed.
       */
      bool needs_rb_swap = false;
      bool needs_chan_reverse = false;
      if (is_copy_to_buffer &&
         (framebuffer->vk_format == VK_FORMAT_X8_D24_UNORM_PACK32 ||
          (framebuffer->vk_format == VK_FORMAT_D24_UNORM_S8_UINT &&
           (aspect & VK_IMAGE_ASPECT_DEPTH_BIT)))) {
         needs_rb_swap = true;
         needs_chan_reverse = true;
      } else if (!is_copy_from_buffer && !is_copy_to_buffer &&
                 (aspect & VK_IMAGE_ASPECT_COLOR_BIT)) {
         /* This is not a raw data copy (i.e. we are clearing the image),
          * so we need to make sure we respect the format swizzle.
          */
         needs_rb_swap = format_needs_rb_swap(framebuffer->vk_format);
      }

      load.r_b_swap = needs_rb_swap;
      load.channel_reverse = needs_chan_reverse;

      if (slice->tiling == VC5_TILING_UIF_NO_XOR ||
          slice->tiling == VC5_TILING_UIF_XOR) {
         load.height_in_ub_or_stride =
            slice->padded_height_of_output_image_in_uif_blocks;
      } else if (slice->tiling == VC5_TILING_RASTER) {
         load.height_in_ub_or_stride = slice->stride;
      }

      if (image->samples > VK_SAMPLE_COUNT_1_BIT)
         load.decimate_mode = V3D_DECIMATE_MODE_ALL_SAMPLES;
      else
         load.decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0;
   }
}

static void
emit_image_store(struct v3dv_cl *cl,
                 struct framebuffer_data *framebuffer,
                 struct v3dv_image *image,
                 VkImageAspectFlags aspect,
                 uint32_t layer,
                 uint32_t mip_level,
                 bool is_copy_to_buffer,
                 bool is_copy_from_buffer,
                 bool is_multisample_resolve)
{
   uint32_t layer_offset = v3dv_layer_offset(image, mip_level, layer);

   bool store_from_color_tlb = is_copy_to_buffer || is_copy_from_buffer ||
                               aspect == VK_IMAGE_ASPECT_COLOR_BIT;

   const struct v3d_resource_slice *slice = &image->slices[mip_level];
   cl_emit(cl, STORE_TILE_BUFFER_GENERAL, store) {
      store.buffer_to_store = store_from_color_tlb ?
         RENDER_TARGET_0 : v3dv_zs_buffer_from_aspect_bits(aspect);

      store.address = v3dv_cl_address(image->mem->bo, layer_offset);
      store.clear_buffer_being_stored = false;

      /* See rationale in emit_image_load() */
      bool needs_rb_swap = false;
      bool needs_chan_reverse = false;
      if (is_copy_from_buffer &&
         (framebuffer->vk_format == VK_FORMAT_X8_D24_UNORM_PACK32 ||
          (framebuffer->vk_format == VK_FORMAT_D24_UNORM_S8_UINT &&
           (aspect & VK_IMAGE_ASPECT_DEPTH_BIT)))) {
         needs_rb_swap = true;
         needs_chan_reverse = true;
      } else if (!is_copy_from_buffer && !is_copy_to_buffer &&
                 (aspect & VK_IMAGE_ASPECT_COLOR_BIT)) {
         needs_rb_swap = format_needs_rb_swap(framebuffer->vk_format);
      }

      store.r_b_swap = needs_rb_swap;
      store.channel_reverse = needs_chan_reverse;

      store.output_image_format = choose_tlb_format(framebuffer, aspect, true,
                                                    is_copy_to_buffer,
                                                    is_copy_from_buffer);
      store.memory_format = slice->tiling;
      if (slice->tiling == VC5_TILING_UIF_NO_XOR ||
          slice->tiling == VC5_TILING_UIF_XOR) {
         store.height_in_ub_or_stride =
            slice->padded_height_of_output_image_in_uif_blocks;
      } else if (slice->tiling == VC5_TILING_RASTER) {
         store.height_in_ub_or_stride = slice->stride;
      }

      if (image->samples > VK_SAMPLE_COUNT_1_BIT)
         store.decimate_mode = V3D_DECIMATE_MODE_ALL_SAMPLES;
      else if (is_multisample_resolve)
         store.decimate_mode = V3D_DECIMATE_MODE_4X;
      else
         store.decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0;
   }
}

static void
emit_copy_layer_to_buffer_per_tile_list(struct v3dv_job *job,
                                        struct framebuffer_data *framebuffer,
                                        struct v3dv_buffer *buffer,
                                        struct v3dv_image *image,
                                        uint32_t layer_offset,
                                        const VkBufferImageCopy *region)
{
   struct v3dv_cl *cl = &job->indirect;
   v3dv_cl_ensure_space(cl, 200, 1);
   v3dv_return_if_oom(NULL, job);

   struct v3dv_cl_reloc tile_list_start = v3dv_cl_get_address(cl);

   cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);

   /* Load image to TLB */
   assert((image->type != VK_IMAGE_TYPE_3D &&
           layer_offset < region->imageSubresource.layerCount) ||
          layer_offset < image->extent.depth);

   const uint32_t image_layer = image->type != VK_IMAGE_TYPE_3D ?
      region->imageSubresource.baseArrayLayer + layer_offset :
      region->imageOffset.z + layer_offset;

   emit_image_load(cl, framebuffer, image,
                   region->imageSubresource.aspectMask,
                   image_layer,
                   region->imageSubresource.mipLevel,
                   true, false);

   cl_emit(cl, END_OF_LOADS, end);

   cl_emit(cl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

   /* Store TLB to buffer */
   uint32_t width, height;
   if (region->bufferRowLength == 0)
      width = region->imageExtent.width;
   else
      width = region->bufferRowLength;

   if (region->bufferImageHeight == 0)
      height = region->imageExtent.height;
   else
      height = region->bufferImageHeight;

   /* Handle copy from compressed format */
   width = DIV_ROUND_UP(width, vk_format_get_blockwidth(image->vk_format));
   height = DIV_ROUND_UP(height, vk_format_get_blockheight(image->vk_format));

   /* If we are storing stencil from a combined depth/stencil format the
    * Vulkan spec states that the output buffer must have packed stencil
    * values, where each stencil value is 1 byte.
    */
   uint32_t cpp =
      region->imageSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT ?
         1 : image->cpp;
   uint32_t buffer_stride = width * cpp;
   uint32_t buffer_offset = buffer->mem_offset + region->bufferOffset +
                            height * buffer_stride * layer_offset;

   uint32_t format = choose_tlb_format(framebuffer,
                                       region->imageSubresource.aspectMask,
                                       true, true, false);
   bool msaa = image->samples > VK_SAMPLE_COUNT_1_BIT;

   emit_linear_store(cl, RENDER_TARGET_0, buffer->mem->bo,
                     buffer_offset, buffer_stride, msaa, format);

   cl_emit(cl, END_OF_TILE_MARKER, end);

   cl_emit(cl, RETURN_FROM_SUB_LIST, ret);

   cl_emit(&job->rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
      branch.start = tile_list_start;
      branch.end = v3dv_cl_get_address(cl);
   }
}

static void
emit_copy_layer_to_buffer(struct v3dv_job *job,
                          struct v3dv_buffer *buffer,
                          struct v3dv_image *image,
                          struct framebuffer_data *framebuffer,
                          uint32_t layer,
                          const VkBufferImageCopy *region)
{
   emit_frame_setup(job, layer, NULL);
   emit_copy_layer_to_buffer_per_tile_list(job, framebuffer, buffer,
                                           image, layer, region);
   emit_supertile_coordinates(job, framebuffer);
}

static void
emit_copy_image_to_buffer_rcl(struct v3dv_job *job,
                              struct v3dv_buffer *buffer,
                              struct v3dv_image *image,
                              struct framebuffer_data *framebuffer,
                              const VkBufferImageCopy *region)
{
   struct v3dv_cl *rcl = emit_rcl_prologue(job, framebuffer, NULL);
   v3dv_return_if_oom(NULL, job);

   for (int layer = 0; layer < job->frame_tiling.layers; layer++)
      emit_copy_layer_to_buffer(job, buffer, image, framebuffer, layer, region);
   cl_emit(rcl, END_OF_RENDERING, end);
}

/* Implements a copy using the TLB.
 *
 * This only works if we are copying from offset (0,0), since a TLB store for
 * tile (x,y) will be written at the same tile offset into the destination.
 * When this requirement is not met, we need to use a blit instead.
 *
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 *
 */
static bool
copy_image_to_buffer_tlb(struct v3dv_cmd_buffer *cmd_buffer,
                         struct v3dv_buffer *buffer,
                         struct v3dv_image *image,
                         const VkBufferImageCopy *region)
{
   VkFormat fb_format;
   if (!can_use_tlb(image, &region->imageOffset, &fb_format))
      return false;

   uint32_t internal_type, internal_bpp;
   get_internal_type_bpp_for_image_aspects(fb_format,
                                           region->imageSubresource.aspectMask,
                                           &internal_type, &internal_bpp);

   uint32_t num_layers;
   if (image->type != VK_IMAGE_TYPE_3D)
      num_layers = region->imageSubresource.layerCount;
   else
      num_layers = region->imageExtent.depth;
   assert(num_layers > 0);

   struct v3dv_job *job =
      v3dv_cmd_buffer_start_job(cmd_buffer, -1, V3DV_JOB_TYPE_GPU_CL);
   if (!job)
      return true;

   /* Handle copy from compressed format using a compatible format */
   const uint32_t block_w = vk_format_get_blockwidth(image->vk_format);
   const uint32_t block_h = vk_format_get_blockheight(image->vk_format);
   const uint32_t width = DIV_ROUND_UP(region->imageExtent.width, block_w);
   const uint32_t height = DIV_ROUND_UP(region->imageExtent.height, block_h);

   v3dv_job_start_frame(job, width, height, num_layers, 1, internal_bpp, false);

   struct framebuffer_data framebuffer;
   setup_framebuffer_data(&framebuffer, fb_format, internal_type,
                          &job->frame_tiling);

   v3dv_job_emit_binning_flush(job);
   emit_copy_image_to_buffer_rcl(job, buffer, image, &framebuffer, region);

   v3dv_cmd_buffer_finish_job(cmd_buffer);

   return true;
}

static bool
blit_shader(struct v3dv_cmd_buffer *cmd_buffer,
            struct v3dv_image *dst,
            VkFormat dst_format,
            struct v3dv_image *src,
            VkFormat src_format,
            VkColorComponentFlags cmask,
            VkComponentMapping *cswizzle,
            const VkImageBlit *region,
            VkFilter filter,
            bool dst_is_padded_image);

/**
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 */
static bool
copy_image_to_buffer_blit(struct v3dv_cmd_buffer *cmd_buffer,
                          struct v3dv_buffer *buffer,
                          struct v3dv_image *image,
                          const VkBufferImageCopy *region)
{
   bool handled = false;

   /* Generally, the bpp of the data in the buffer matches that of the
    * source image. The exception is the case where we are copying
    * stencil (8bpp) to a combined d24s8 image (32bpp).
    */
   uint32_t buffer_bpp = image->cpp;

   VkImageAspectFlags copy_aspect = region->imageSubresource.aspectMask;

   /* Because we are going to implement the copy as a blit, we need to create
    * a linear image from the destination buffer and we also want our blit
    * source and destination formats to be the same (to avoid any format
    * conversions), so we choose a canonical format that matches the
    * source image bpp.
    *
    * The exception to the above is copying from combined depth/stencil images
    * because we are copying only one aspect of the image, so we need to setup
    * our formats, color write mask and source swizzle mask to match that.
    */
   VkFormat dst_format;
   VkFormat src_format;
   VkColorComponentFlags cmask = 0; /* All components */
   VkComponentMapping cswizzle = {
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
   };
   switch (buffer_bpp) {
   case 16:
      assert(copy_aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      dst_format = VK_FORMAT_R32G32B32A32_UINT;
      src_format = dst_format;
      break;
   case 8:
      assert(copy_aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      dst_format = VK_FORMAT_R16G16B16A16_UINT;
      src_format = dst_format;
      break;
   case 4:
      switch (copy_aspect) {
      case VK_IMAGE_ASPECT_COLOR_BIT:
         src_format = VK_FORMAT_R8G8B8A8_UINT;
         dst_format = VK_FORMAT_R8G8B8A8_UINT;
         break;
      case VK_IMAGE_ASPECT_DEPTH_BIT:
         assert(image->vk_format == VK_FORMAT_D32_SFLOAT ||
                image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT ||
                image->vk_format == VK_FORMAT_X8_D24_UNORM_PACK32);
         if (image->vk_format == VK_FORMAT_D32_SFLOAT) {
            src_format = VK_FORMAT_R32_UINT;
            dst_format = VK_FORMAT_R32_UINT;
         } else {
            /* We want to write depth in the buffer in the first 24-bits,
             * however, the hardware has depth in bits 8-31, so swizzle the
             * the source components to match what we want. Also, we don't
             * want to write bits 24-31 in the destination.
             */
            src_format = VK_FORMAT_R8G8B8A8_UINT;
            dst_format = VK_FORMAT_R8G8B8A8_UINT;
            cmask = VK_COLOR_COMPONENT_R_BIT |
                    VK_COLOR_COMPONENT_G_BIT |
                    VK_COLOR_COMPONENT_B_BIT;
            cswizzle.r = VK_COMPONENT_SWIZZLE_G;
            cswizzle.g = VK_COMPONENT_SWIZZLE_B;
            cswizzle.b = VK_COMPONENT_SWIZZLE_A;
            cswizzle.a = VK_COMPONENT_SWIZZLE_ZERO;
         }
         break;
      case VK_IMAGE_ASPECT_STENCIL_BIT:
         assert(copy_aspect == VK_IMAGE_ASPECT_STENCIL_BIT);
         assert(image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT);
         /* Copying from S8D24. We want to write 8-bit stencil values only,
          * so adjust the buffer bpp for that. Since the hardware stores stencil
          * in the LSB, we can just do a RGBA8UI to R8UI blit.
          */
         src_format = VK_FORMAT_R8G8B8A8_UINT;
         dst_format = VK_FORMAT_R8_UINT;
         buffer_bpp = 1;
         break;
      default:
         unreachable("unsupported aspect");
         return handled;
      };
      break;
   case 2:
      assert(copy_aspect == VK_IMAGE_ASPECT_COLOR_BIT ||
             copy_aspect == VK_IMAGE_ASPECT_DEPTH_BIT);
      dst_format = VK_FORMAT_R16_UINT;
      src_format = dst_format;
      break;
   case 1:
      assert(copy_aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      dst_format = VK_FORMAT_R8_UINT;
      src_format = dst_format;
      break;
   default:
      unreachable("unsupported bit-size");
      return handled;
   };

   /* The hardware doesn't support linear depth/stencil stores, so we
    * implement copies of depth/stencil aspect as color copies using a
    * compatible color format.
    */
   assert(vk_format_is_color(src_format));
   assert(vk_format_is_color(dst_format));
   copy_aspect = VK_IMAGE_ASPECT_COLOR_BIT;

   /* We should be able to handle the blit if we got this far */
   handled = true;

   /* Obtain the 2D buffer region spec */
   uint32_t buf_width, buf_height;
   if (region->bufferRowLength == 0)
      buf_width = region->imageExtent.width;
   else
      buf_width = region->bufferRowLength;

   if (region->bufferImageHeight == 0)
      buf_height = region->imageExtent.height;
   else
      buf_height = region->bufferImageHeight;

   /* If the image is compressed, the bpp refers to blocks, not pixels */
   uint32_t block_width = vk_format_get_blockwidth(image->vk_format);
   uint32_t block_height = vk_format_get_blockheight(image->vk_format);
   buf_width = buf_width / block_width;
   buf_height = buf_height / block_height;

   /* Compute layers to copy */
   uint32_t num_layers;
   if (image->type != VK_IMAGE_TYPE_3D)
      num_layers = region->imageSubresource.layerCount;
   else
      num_layers = region->imageExtent.depth;
   assert(num_layers > 0);

   /* Our blit interface can see the real format of the images to detect
    * copies between compressed and uncompressed images and adapt the
    * blit region accordingly. Here we are just doing a raw copy of
    * compressed data, but we are passing an uncompressed view of the
    * buffer for the blit destination image (since compressed formats are
    * not renderable), so we also want to provide an uncompressed view of
    * the source image.
    */
   VkResult result;
   struct v3dv_device *device = cmd_buffer->device;
   VkDevice _device = v3dv_device_to_handle(device);
   if (vk_format_is_compressed(image->vk_format)) {
      VkImage uiview;
      VkImageCreateInfo uiview_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .imageType = VK_IMAGE_TYPE_3D,
         .format = dst_format,
         .extent = { buf_width, buf_height, image->extent.depth },
         .mipLevels = image->levels,
         .arrayLayers = image->array_size,
         .samples = image->samples,
         .tiling = image->tiling,
         .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .queueFamilyIndexCount = 0,
         .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
      };
      result = v3dv_CreateImage(_device, &uiview_info, &device->vk.alloc, &uiview);
      if (result != VK_SUCCESS)
         return handled;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t)uiview,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyImage);

      result = v3dv_BindImageMemory(_device, uiview,
                                    v3dv_device_memory_to_handle(image->mem),
                                    image->mem_offset);
      if (result != VK_SUCCESS)
         return handled;

      image = v3dv_image_from_handle(uiview);
   }

   /* Copy requested layers */
   for (uint32_t i = 0; i < num_layers; i++) {
      /* Create the destination blit image from the destination buffer */
      VkImageCreateInfo image_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = dst_format,
         .extent = { buf_width, buf_height, 1 },
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = VK_IMAGE_TILING_LINEAR,
         .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .queueFamilyIndexCount = 0,
         .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
      };

      VkImage buffer_image;
      result =
         v3dv_CreateImage(_device, &image_info, &device->vk.alloc, &buffer_image);
      if (result != VK_SUCCESS)
         return handled;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t)buffer_image,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyImage);

      /* Bind the buffer memory to the image */
      VkDeviceSize buffer_offset = buffer->mem_offset + region->bufferOffset +
         i * buf_width * buf_height * buffer_bpp;
      result = v3dv_BindImageMemory(_device, buffer_image,
                                    v3dv_device_memory_to_handle(buffer->mem),
                                    buffer_offset);
      if (result != VK_SUCCESS)
         return handled;

      /* Blit-copy the requested image extent.
       *
       * Since we are copying, the blit must use the same format on the
       * destination and source images to avoid format conversions. The
       * only exception is copying stencil, which we upload to a R8UI source
       * image, but that we need to blit to a S8D24 destination (the only
       * stencil format we support).
       */
      const VkImageBlit blit_region = {
         .srcSubresource = {
            .aspectMask = copy_aspect,
            .mipLevel = region->imageSubresource.mipLevel,
            .baseArrayLayer = region->imageSubresource.baseArrayLayer + i,
            .layerCount = 1,
         },
         .srcOffsets = {
            {
               DIV_ROUND_UP(region->imageOffset.x, block_width),
               DIV_ROUND_UP(region->imageOffset.y, block_height),
               region->imageOffset.z + i,
            },
            {
               DIV_ROUND_UP(region->imageOffset.x + region->imageExtent.width,
                            block_width),
               DIV_ROUND_UP(region->imageOffset.y + region->imageExtent.height,
                            block_height),
               region->imageOffset.z + i + 1,
            },
         },
         .dstSubresource = {
            .aspectMask = copy_aspect,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .dstOffsets = {
            { 0, 0, 0 },
            {
               DIV_ROUND_UP(region->imageExtent.width, block_width),
               DIV_ROUND_UP(region->imageExtent.height, block_height),
               1
            },
         },
      };

      handled = blit_shader(cmd_buffer,
                            v3dv_image_from_handle(buffer_image), dst_format,
                            image, src_format,
                            cmask, &cswizzle,
                            &blit_region, VK_FILTER_NEAREST, false);
      if (!handled) {
         /* This is unexpected, we should have a supported blit spec */
         unreachable("Unable to blit buffer to destination image");
         return false;
      }
   }

   assert(handled);
   return true;
}

static VkFormat
get_compatible_tlb_format(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_R8G8B8A8_SNORM:
      return VK_FORMAT_R8G8B8A8_UINT;

   case VK_FORMAT_R8G8_SNORM:
      return VK_FORMAT_R8G8_UINT;

   case VK_FORMAT_R8_SNORM:
      return VK_FORMAT_R8_UINT;

   case VK_FORMAT_A8B8G8R8_SNORM_PACK32:
      return VK_FORMAT_A8B8G8R8_UINT_PACK32;

   case VK_FORMAT_R16_UNORM:
   case VK_FORMAT_R16_SNORM:
      return VK_FORMAT_R16_UINT;

   case VK_FORMAT_R16G16_UNORM:
   case VK_FORMAT_R16G16_SNORM:
      return VK_FORMAT_R16G16_UINT;

   case VK_FORMAT_R16G16B16A16_UNORM:
   case VK_FORMAT_R16G16B16A16_SNORM:
      return VK_FORMAT_R16G16B16A16_UINT;

   case VK_FORMAT_E5B9G9R9_UFLOAT_PACK32:
      return VK_FORMAT_R32_SFLOAT;

   /* We can't render to compressed formats using the TLB so instead we use
    * a compatible format with the same bpp as the compressed format. Because
    * the compressed format's bpp is for a full block (i.e. 4x4 pixels in the
    * case of ETC), when we implement copies with the compatible format we
    * will have to divide offsets and dimensions on the compressed image by
    * the compressed block size.
    */
   case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
   case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
   case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
   case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
      return VK_FORMAT_R32G32B32A32_UINT;

   case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
   case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
   case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
   case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
   case VK_FORMAT_EAC_R11_UNORM_BLOCK:
   case VK_FORMAT_EAC_R11_SNORM_BLOCK:
      return VK_FORMAT_R16G16B16A16_UINT;

   default:
      return VK_FORMAT_UNDEFINED;
   }
}

static inline bool
can_use_tlb(struct v3dv_image *image,
            const VkOffset3D *offset,
            VkFormat *compat_format)
{
   if (offset->x != 0 || offset->y != 0)
      return false;

   if (image->format->rt_type != V3D_OUTPUT_IMAGE_FORMAT_NO) {
      if (compat_format)
         *compat_format = image->vk_format;
      return true;
   }

   /* If the image format is not TLB-supported, then check if we can use
    * a compatible format instead.
    */
   if (compat_format) {
      *compat_format = get_compatible_tlb_format(image->vk_format);
      if (*compat_format != VK_FORMAT_UNDEFINED)
         return true;
   }

   return false;
}

void
v3dv_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                          VkImage srcImage,
                          VkImageLayout srcImageLayout,
                          VkBuffer destBuffer,
                          uint32_t regionCount,
                          const VkBufferImageCopy *pRegions)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_image, image, srcImage);
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, destBuffer);

   assert(image->samples == VK_SAMPLE_COUNT_1_BIT);

   for (uint32_t i = 0; i < regionCount; i++) {
      if (copy_image_to_buffer_tlb(cmd_buffer, buffer, image, &pRegions[i]))
         continue;
      if (copy_image_to_buffer_blit(cmd_buffer, buffer, image, &pRegions[i]))
         continue;
      unreachable("Unsupported image to buffer copy.");
   }
}

static void
emit_copy_image_layer_per_tile_list(struct v3dv_job *job,
                                    struct framebuffer_data *framebuffer,
                                    struct v3dv_image *dst,
                                    struct v3dv_image *src,
                                    uint32_t layer_offset,
                                    const VkImageCopy *region)
{
   struct v3dv_cl *cl = &job->indirect;
   v3dv_cl_ensure_space(cl, 200, 1);
   v3dv_return_if_oom(NULL, job);

   struct v3dv_cl_reloc tile_list_start = v3dv_cl_get_address(cl);

   cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);

   assert((src->type != VK_IMAGE_TYPE_3D &&
           layer_offset < region->srcSubresource.layerCount) ||
          layer_offset < src->extent.depth);

   const uint32_t src_layer = src->type != VK_IMAGE_TYPE_3D ?
      region->srcSubresource.baseArrayLayer + layer_offset :
      region->srcOffset.z + layer_offset;

   emit_image_load(cl, framebuffer, src,
                   region->srcSubresource.aspectMask,
                   src_layer,
                   region->srcSubresource.mipLevel,
                   false, false);

   cl_emit(cl, END_OF_LOADS, end);

   cl_emit(cl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

   assert((dst->type != VK_IMAGE_TYPE_3D &&
           layer_offset < region->dstSubresource.layerCount) ||
          layer_offset < dst->extent.depth);

   const uint32_t dst_layer = dst->type != VK_IMAGE_TYPE_3D ?
      region->dstSubresource.baseArrayLayer + layer_offset :
      region->dstOffset.z + layer_offset;

   emit_image_store(cl, framebuffer, dst,
                    region->dstSubresource.aspectMask,
                    dst_layer,
                    region->dstSubresource.mipLevel,
                    false, false, false);

   cl_emit(cl, END_OF_TILE_MARKER, end);

   cl_emit(cl, RETURN_FROM_SUB_LIST, ret);

   cl_emit(&job->rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
      branch.start = tile_list_start;
      branch.end = v3dv_cl_get_address(cl);
   }
}

static void
emit_copy_image_layer(struct v3dv_job *job,
                      struct v3dv_image *dst,
                      struct v3dv_image *src,
                      struct framebuffer_data *framebuffer,
                      uint32_t layer,
                      const VkImageCopy *region)
{
   emit_frame_setup(job, layer, NULL);
   emit_copy_image_layer_per_tile_list(job, framebuffer, dst, src, layer, region);
   emit_supertile_coordinates(job, framebuffer);
}

static void
emit_copy_image_rcl(struct v3dv_job *job,
                    struct v3dv_image *dst,
                    struct v3dv_image *src,
                    struct framebuffer_data *framebuffer,
                    const VkImageCopy *region)
{
   struct v3dv_cl *rcl = emit_rcl_prologue(job, framebuffer, NULL);
   v3dv_return_if_oom(NULL, job);

   for (int layer = 0; layer < job->frame_tiling.layers; layer++)
      emit_copy_image_layer(job, dst, src, framebuffer, layer, region);
   cl_emit(rcl, END_OF_RENDERING, end);
}

/* Disable level 0 write, just write following mipmaps */
#define V3D_TFU_IOA_DIMTW (1 << 0)
#define V3D_TFU_IOA_FORMAT_SHIFT 3
#define V3D_TFU_IOA_FORMAT_LINEARTILE 3
#define V3D_TFU_IOA_FORMAT_UBLINEAR_1_COLUMN 4
#define V3D_TFU_IOA_FORMAT_UBLINEAR_2_COLUMN 5
#define V3D_TFU_IOA_FORMAT_UIF_NO_XOR 6
#define V3D_TFU_IOA_FORMAT_UIF_XOR 7

#define V3D_TFU_ICFG_NUMMM_SHIFT 5
#define V3D_TFU_ICFG_TTYPE_SHIFT 9

#define V3D_TFU_ICFG_OPAD_SHIFT 22

#define V3D_TFU_ICFG_FORMAT_SHIFT 18
#define V3D_TFU_ICFG_FORMAT_RASTER 0
#define V3D_TFU_ICFG_FORMAT_SAND_128 1
#define V3D_TFU_ICFG_FORMAT_SAND_256 2
#define V3D_TFU_ICFG_FORMAT_LINEARTILE 11
#define V3D_TFU_ICFG_FORMAT_UBLINEAR_1_COLUMN 12
#define V3D_TFU_ICFG_FORMAT_UBLINEAR_2_COLUMN 13
#define V3D_TFU_ICFG_FORMAT_UIF_NO_XOR 14
#define V3D_TFU_ICFG_FORMAT_UIF_XOR 15

static void
emit_tfu_job(struct v3dv_cmd_buffer *cmd_buffer,
             struct v3dv_image *dst,
             uint32_t dst_mip_level,
             uint32_t dst_layer,
             struct v3dv_image *src,
             uint32_t src_mip_level,
             uint32_t src_layer,
             uint32_t width,
             uint32_t height,
             const struct v3dv_format *format)
{
   const struct v3d_resource_slice *src_slice = &src->slices[src_mip_level];
   const struct v3d_resource_slice *dst_slice = &dst->slices[dst_mip_level];

   assert(dst->mem && dst->mem->bo);
   const struct v3dv_bo *dst_bo = dst->mem->bo;

   assert(src->mem && src->mem->bo);
   const struct v3dv_bo *src_bo = src->mem->bo;

   struct drm_v3d_submit_tfu tfu = {
      .ios = (height << 16) | width,
      .bo_handles = {
         dst_bo->handle,
         src_bo->handle != dst_bo->handle ? src_bo->handle : 0
      },
   };

   const uint32_t src_offset =
      src_bo->offset + v3dv_layer_offset(src, src_mip_level, src_layer);
   tfu.iia |= src_offset;

   uint32_t icfg;
   if (src_slice->tiling == VC5_TILING_RASTER) {
      icfg = V3D_TFU_ICFG_FORMAT_RASTER;
   } else {
      icfg = V3D_TFU_ICFG_FORMAT_LINEARTILE +
             (src_slice->tiling - VC5_TILING_LINEARTILE);
   }
   tfu.icfg |= icfg << V3D_TFU_ICFG_FORMAT_SHIFT;

   const uint32_t dst_offset =
      dst_bo->offset + v3dv_layer_offset(dst, dst_mip_level, dst_layer);
   tfu.ioa |= dst_offset;

   tfu.ioa |= (V3D_TFU_IOA_FORMAT_LINEARTILE +
               (dst_slice->tiling - VC5_TILING_LINEARTILE)) <<
                V3D_TFU_IOA_FORMAT_SHIFT;
   tfu.icfg |= format->tex_type << V3D_TFU_ICFG_TTYPE_SHIFT;

   switch (src_slice->tiling) {
   case VC5_TILING_UIF_NO_XOR:
   case VC5_TILING_UIF_XOR:
      tfu.iis |= src_slice->padded_height / (2 * v3d_utile_height(src->cpp));
      break;
   case VC5_TILING_RASTER:
      tfu.iis |= src_slice->stride / src->cpp;
      break;
   default:
      break;
   }

   /* If we're writing level 0 (!IOA_DIMTW), then we need to supply the
    * OPAD field for the destination (how many extra UIF blocks beyond
    * those necessary to cover the height).
    */
   if (dst_slice->tiling == VC5_TILING_UIF_NO_XOR ||
       dst_slice->tiling == VC5_TILING_UIF_XOR) {
      uint32_t uif_block_h = 2 * v3d_utile_height(dst->cpp);
      uint32_t implicit_padded_height = align(height, uif_block_h);
      uint32_t icfg =
         (dst_slice->padded_height - implicit_padded_height) / uif_block_h;
      tfu.icfg |= icfg << V3D_TFU_ICFG_OPAD_SHIFT;
   }

   v3dv_cmd_buffer_add_tfu_job(cmd_buffer, &tfu);
}

/**
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 */
static bool
copy_image_tfu(struct v3dv_cmd_buffer *cmd_buffer,
               struct v3dv_image *dst,
               struct v3dv_image *src,
               const VkImageCopy *region)
{
   /* Destination can't be raster format */
   if (dst->tiling == VK_IMAGE_TILING_LINEAR)
      return false;

   /* We can only do full copies, so if the format is D24S8 both aspects need
    * to be copied. We only need to check the dst format because the spec
    * states that depth/stencil formats must match exactly.
    */
   if (dst->vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
       const VkImageAspectFlags ds_aspects = VK_IMAGE_ASPECT_DEPTH_BIT |
                                             VK_IMAGE_ASPECT_STENCIL_BIT;
       if (region->dstSubresource.aspectMask != ds_aspects)
         return false;
   }

   /* Don't handle copies between uncompressed and compressed formats for now.
    *
    * FIXME: we should be able to handle these easily but there is no coverage
    * in CTS at the moment that make such copies with full images (which we
    * require here), only partial copies. Also, in that case the code below that
    * checks for "dst image complete" requires some changes, since it is
    * checking against the region dimensions, which are in units of the source
    * image format.
    */
   if (vk_format_is_compressed(dst->vk_format) !=
       vk_format_is_compressed(src->vk_format)) {
      return false;
   }

   /* Source region must start at (0,0) */
   if (region->srcOffset.x != 0 || region->srcOffset.y != 0)
      return false;

   /* Destination image must be complete */
   if (region->dstOffset.x != 0 || region->dstOffset.y != 0)
      return false;

   const uint32_t dst_mip_level = region->dstSubresource.mipLevel;
   uint32_t dst_width = u_minify(dst->extent.width, dst_mip_level);
   uint32_t dst_height = u_minify(dst->extent.height, dst_mip_level);
   if (region->extent.width != dst_width || region->extent.height != dst_height)
      return false;

   /* From vkCmdCopyImage:
    *
    *   "When copying between compressed and uncompressed formats the extent
    *    members represent the texel dimensions of the source image and not
    *    the destination."
    */
   const uint32_t block_w = vk_format_get_blockwidth(src->vk_format);
   const uint32_t block_h = vk_format_get_blockheight(src->vk_format);
   uint32_t width = DIV_ROUND_UP(region->extent.width, block_w);
   uint32_t height = DIV_ROUND_UP(region->extent.height, block_h);

   /* Account for sample count */
   assert(dst->samples == src->samples);
   if (dst->samples > VK_SAMPLE_COUNT_1_BIT) {
      assert(dst->samples == VK_SAMPLE_COUNT_4_BIT);
      width *= 2;
      height *= 2;
   }

   /* The TFU unit doesn't handle format conversions so we need the formats to
    * match. On the other hand, vkCmdCopyImage allows different color formats
    * on the source and destination images, but only if they are texel
    * compatible. For us, this means that we can effectively ignore different
    * formats and just make the copy using either of them, since we are just
    * moving raw data and not making any conversions.
    *
    * Also, the formats supported by the TFU unit are limited, but again, since
    * we are only doing raw copies here without interpreting or converting
    * the underlying pixel data according to its format, we can always choose
    * to use compatible formats that are supported with the TFU unit.
    */
   assert(dst->cpp == src->cpp);
   const struct v3dv_format *format =
      v3dv_get_compatible_tfu_format(&cmd_buffer->device->devinfo,
                                     dst->cpp, NULL);

   /* Emit a TFU job for each layer to blit */
   const uint32_t layer_count = dst->type != VK_IMAGE_TYPE_3D ?
      region->dstSubresource.layerCount :
      region->extent.depth;
   const uint32_t src_mip_level = region->srcSubresource.mipLevel;

   const uint32_t base_src_layer = src->type != VK_IMAGE_TYPE_3D ?
      region->srcSubresource.baseArrayLayer : region->srcOffset.z;
   const uint32_t base_dst_layer = dst->type != VK_IMAGE_TYPE_3D ?
      region->dstSubresource.baseArrayLayer : region->dstOffset.z;
   for (uint32_t i = 0; i < layer_count; i++) {
      emit_tfu_job(cmd_buffer,
                   dst, dst_mip_level, base_dst_layer + i,
                   src, src_mip_level, base_src_layer + i,
                   width, height, format);
   }

   return true;
}

/**
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 */
static bool
copy_image_tlb(struct v3dv_cmd_buffer *cmd_buffer,
               struct v3dv_image *dst,
               struct v3dv_image *src,
               const VkImageCopy *region)
{
   VkFormat fb_format;
   if (!can_use_tlb(src, &region->srcOffset, &fb_format) ||
       !can_use_tlb(dst, &region->dstOffset, &fb_format)) {
      return false;
   }

   /* From the Vulkan spec, VkImageCopy valid usage:
    *
    *    "If neither the calling command’s srcImage nor the calling command’s
    *     dstImage has a multi-planar image format then the aspectMask member
    *     of srcSubresource and dstSubresource must match."
    */
   assert(region->dstSubresource.aspectMask ==
          region->srcSubresource.aspectMask);
   uint32_t internal_type, internal_bpp;
   get_internal_type_bpp_for_image_aspects(fb_format,
                                           region->dstSubresource.aspectMask,
                                           &internal_type, &internal_bpp);

   /* From the Vulkan spec with VK_KHR_maintenance1, VkImageCopy valid usage:
    *
    * "The number of slices of the extent (for 3D) or layers of the
    *  srcSubresource (for non-3D) must match the number of slices of the
    *  extent (for 3D) or layers of the dstSubresource (for non-3D)."
    */
   assert((src->type != VK_IMAGE_TYPE_3D ?
           region->srcSubresource.layerCount : region->extent.depth) ==
          (dst->type != VK_IMAGE_TYPE_3D ?
           region->dstSubresource.layerCount : region->extent.depth));
   uint32_t num_layers;
   if (dst->type != VK_IMAGE_TYPE_3D)
      num_layers = region->dstSubresource.layerCount;
   else
      num_layers = region->extent.depth;
   assert(num_layers > 0);

   struct v3dv_job *job =
      v3dv_cmd_buffer_start_job(cmd_buffer, -1, V3DV_JOB_TYPE_GPU_CL);
   if (!job)
      return true;

   /* Handle copy to compressed image using compatible format */
   const uint32_t block_w = vk_format_get_blockwidth(dst->vk_format);
   const uint32_t block_h = vk_format_get_blockheight(dst->vk_format);
   const uint32_t width = DIV_ROUND_UP(region->extent.width, block_w);
   const uint32_t height = DIV_ROUND_UP(region->extent.height, block_h);

   v3dv_job_start_frame(job, width, height, num_layers, 1, internal_bpp,
                        src->samples > VK_SAMPLE_COUNT_1_BIT);

   struct framebuffer_data framebuffer;
   setup_framebuffer_data(&framebuffer, fb_format, internal_type,
                          &job->frame_tiling);

   v3dv_job_emit_binning_flush(job);
   emit_copy_image_rcl(job, dst, src, &framebuffer, region);

   v3dv_cmd_buffer_finish_job(cmd_buffer);

   return true;
}

/**
 * Takes the image provided as argument and creates a new image that has
 * the same specification and aliases the same memory storage, except that:
 *
 *   - It has the uncompressed format passed in.
 *   - Its original width/height are scaled by the factors passed in.
 *
 * This is useful to implement copies from compressed images using the blit
 * path. The idea is that we create uncompressed "image views" of both the
 * source and destination images using the uncompressed format and then we
 * define the copy blit in terms of that format.
 */
static struct v3dv_image *
create_image_alias(struct v3dv_cmd_buffer *cmd_buffer,
                   struct v3dv_image *src,
                   float width_scale,
                   float height_scale,
                   VkFormat format)
{
   assert(!vk_format_is_compressed(format));

   VkDevice _device = v3dv_device_to_handle(cmd_buffer->device);

   VkImageCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = src->type,
      .format = format,
      .extent = {
         .width = src->extent.width * width_scale,
         .height = src->extent.height * height_scale,
         .depth = src->extent.depth,
      },
      .mipLevels = src->levels,
      .arrayLayers = src->array_size,
      .samples = src->samples,
      .tiling = src->tiling,
      .usage = src->usage,
   };

    VkImage _image;
    VkResult result =
      v3dv_CreateImage(_device, &info, &cmd_buffer->device->vk.alloc, &_image);
    if (result != VK_SUCCESS) {
       v3dv_flag_oom(cmd_buffer, NULL);
       return NULL;
    }

    struct v3dv_image *image = v3dv_image_from_handle(_image);
    image->mem = src->mem;
    image->mem_offset = src->mem_offset;
    return image;
}

/**
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 */
static bool
copy_image_blit(struct v3dv_cmd_buffer *cmd_buffer,
                struct v3dv_image *dst,
                struct v3dv_image *src,
                const VkImageCopy *region)
{
   const uint32_t src_block_w = vk_format_get_blockwidth(src->vk_format);
   const uint32_t src_block_h = vk_format_get_blockheight(src->vk_format);
   const uint32_t dst_block_w = vk_format_get_blockwidth(dst->vk_format);
   const uint32_t dst_block_h = vk_format_get_blockheight(dst->vk_format);
   const float block_scale_w = (float)src_block_w / (float)dst_block_w;
   const float block_scale_h = (float)src_block_h / (float)dst_block_h;

   /* We need to choose a single format for the blit to ensure that this is
    * really a copy and there are not format conversions going on. Since we
    * going to blit, we need to make sure that the selected format can be
    * both rendered to and textured from.
    */
   VkFormat format;
   float src_scale_w = 1.0f;
   float src_scale_h = 1.0f;
   float dst_scale_w = block_scale_w;
   float dst_scale_h = block_scale_h;
   if (vk_format_is_compressed(src->vk_format)) {
      /* If we are copying from a compressed format we should be aware that we
       * are going to texture from the source image, and the texture setup
       * knows the actual size of the image, so we need to choose a format
       * that has a per-texel (not per-block) bpp that is compatible for that
       * image size. For example, for a source image with size Bw*WxBh*H
       * and format ETC2_RGBA8_UNORM copied to a WxH image of format RGBA32UI,
       * each of the Bw*WxBh*H texels in the compressed source image is 8-bit
       * (which translates to a 128-bit 4x4 RGBA32 block when uncompressed),
       * so we could specify a blit with size Bw*WxBh*H and a format with
       * a bpp of 8-bit per texel (R8_UINT).
       *
       * Unfortunately, when copying from a format like ETC2_RGB8A1_UNORM,
       * which is 64-bit per texel, then we would need a 4-bit format, which
       * we don't have, so instead we still choose an 8-bit format, but we
       * apply a divisor to the row dimensions of the blit, since we are
       * copying two texels per item.
       *
       * Generally, we can choose any format so long as we compute appropriate
       * divisors for the width and height depending on the source image's
       * bpp.
       */
      assert(src->cpp == dst->cpp);

      uint32_t divisor_w, divisor_h;
      format = VK_FORMAT_R32G32_UINT;
      switch (src->cpp) {
      case 16:
         format = VK_FORMAT_R32G32B32A32_UINT;
         divisor_w = 4;
         divisor_h = 4;
         break;
      case 8:
         format = VK_FORMAT_R16G16B16A16_UINT;
         divisor_w = 4;
         divisor_h = 4;
         break;
      default:
         unreachable("Unsupported compressed format");
      }

      /* Create image views of the src/dst images that we can interpret in
       * terms of the canonical format.
       */
      src_scale_w /= divisor_w;
      src_scale_h /= divisor_h;
      dst_scale_w /= divisor_w;
      dst_scale_h /= divisor_h;

      src = create_image_alias(cmd_buffer, src,
                               src_scale_w, src_scale_h, format);

      dst = create_image_alias(cmd_buffer, dst,
                               dst_scale_w, dst_scale_h, format);
   } else {
      format = src->format->rt_type != V3D_OUTPUT_IMAGE_FORMAT_NO ?
         src->vk_format : get_compatible_tlb_format(src->vk_format);
      if (format == VK_FORMAT_UNDEFINED)
         return false;

      const struct v3dv_format *f = v3dv_get_format(format);
      if (!f->supported || f->tex_type == TEXTURE_DATA_FORMAT_NO)
         return false;
   }

   /* Given an uncompressed image with size WxH, if we copy it to a compressed
    * image, it will result in an image with size W*bWxH*bH, where bW and bH
    * are the compressed format's block width and height. This means that
    * copies between compressed and uncompressed images involve different
    * image sizes, and therefore, we need to take that into account when
    * setting up the source and destination blit regions below, so they are
    * consistent from the point of view of the single compatible format
    * selected for the copy.
    *
    * We should take into account that the dimensions of the region provided
    * to the copy command are specified in terms of the source image. With that
    * in mind, below we adjust the blit destination region to be consistent with
    * the source region for the compatible format, so basically, we apply
    * the block scale factor to the destination offset provided by the copy
    * command (because it is specified in terms of the destination image, not
    * the source), and then we just add the region copy dimensions to that
    * (since the region dimensions are already specified in terms of the source
    * image).
    */
   const VkOffset3D src_start = {
      region->srcOffset.x * src_scale_w,
      region->srcOffset.y * src_scale_h,
      region->srcOffset.z,
   };
   const VkOffset3D src_end = {
      src_start.x + region->extent.width * src_scale_w,
      src_start.y + region->extent.height * src_scale_h,
      src_start.z + region->extent.depth,
   };

   const VkOffset3D dst_start = {
      region->dstOffset.x * dst_scale_w,
      region->dstOffset.y * dst_scale_h,
      region->dstOffset.z,
   };
   const VkOffset3D dst_end = {
      dst_start.x + region->extent.width * src_scale_w,
      dst_start.y + region->extent.height * src_scale_h,
      dst_start.z + region->extent.depth,
   };

   const VkImageBlit blit_region = {
      .srcSubresource = region->srcSubresource,
      .srcOffsets = { src_start, src_end },
      .dstSubresource = region->dstSubresource,
      .dstOffsets = { dst_start, dst_end },
   };
   bool handled = blit_shader(cmd_buffer,
                              dst, format,
                              src, format,
                              0, NULL,
                              &blit_region, VK_FILTER_NEAREST, true);

   /* We should have selected formats that we can blit */
   assert(handled);
   return handled;
}

void
v3dv_CmdCopyImage(VkCommandBuffer commandBuffer,
                  VkImage srcImage,
                  VkImageLayout srcImageLayout,
                  VkImage dstImage,
                  VkImageLayout dstImageLayout,
                  uint32_t regionCount,
                  const VkImageCopy *pRegions)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_image, src, srcImage);
   V3DV_FROM_HANDLE(v3dv_image, dst, dstImage);

   assert(src->samples == dst->samples);

   for (uint32_t i = 0; i < regionCount; i++) {
      if (copy_image_tfu(cmd_buffer, dst, src, &pRegions[i]))
         continue;
      if (copy_image_tlb(cmd_buffer, dst, src, &pRegions[i]))
         continue;
      if (copy_image_blit(cmd_buffer, dst, src, &pRegions[i]))
         continue;
      unreachable("Image copy not supported");
   }
}

static void
emit_clear_image_per_tile_list(struct v3dv_job *job,
                               struct framebuffer_data *framebuffer,
                               struct v3dv_image *image,
                               VkImageAspectFlags aspects,
                               uint32_t layer,
                               uint32_t level)
{
   struct v3dv_cl *cl = &job->indirect;
   v3dv_cl_ensure_space(cl, 200, 1);
   v3dv_return_if_oom(NULL, job);

   struct v3dv_cl_reloc tile_list_start = v3dv_cl_get_address(cl);

   cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);

   cl_emit(cl, END_OF_LOADS, end);

   cl_emit(cl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

   emit_image_store(cl, framebuffer, image, aspects, layer, level,
                    false, false, false);

   cl_emit(cl, END_OF_TILE_MARKER, end);

   cl_emit(cl, RETURN_FROM_SUB_LIST, ret);

   cl_emit(&job->rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
      branch.start = tile_list_start;
      branch.end = v3dv_cl_get_address(cl);
   }
}

static void
emit_clear_image(struct v3dv_job *job,
                 struct v3dv_image *image,
                 struct framebuffer_data *framebuffer,
                 VkImageAspectFlags aspects,
                 uint32_t layer,
                 uint32_t level)
{
   emit_clear_image_per_tile_list(job, framebuffer, image, aspects, layer, level);
   emit_supertile_coordinates(job, framebuffer);
}

static void
emit_clear_image_rcl(struct v3dv_job *job,
                     struct v3dv_image *image,
                     struct framebuffer_data *framebuffer,
                     const union v3dv_clear_value *clear_value,
                     VkImageAspectFlags aspects,
                     uint32_t layer,
                     uint32_t level)
{
   const struct rcl_clear_info clear_info = {
      .clear_value = clear_value,
      .image = image,
      .aspects = aspects,
      .layer = layer,
      .level = level,
   };

   struct v3dv_cl *rcl = emit_rcl_prologue(job, framebuffer, &clear_info);
   v3dv_return_if_oom(NULL, job);

   emit_frame_setup(job, 0, clear_value);
   emit_clear_image(job, image, framebuffer, aspects, layer, level);
   cl_emit(rcl, END_OF_RENDERING, end);
}

static void
get_hw_clear_color(const VkClearColorValue *color,
                   VkFormat fb_format,
                   VkFormat image_format,
                   uint32_t internal_type,
                   uint32_t internal_bpp,
                   uint32_t *hw_color)
{
   const uint32_t internal_size = 4 << internal_bpp;

   /* If the image format doesn't match the framebuffer format, then we are
    * trying to clear an unsupported tlb format using a compatible
    * format for the framebuffer. In this case, we want to make sure that
    * we pack the clear value according to the original format semantics,
    * not the compatible format.
    */
   if (fb_format == image_format) {
      v3dv_get_hw_clear_color(color, internal_type, internal_size, hw_color);
   } else {
      union util_color uc;
      enum pipe_format pipe_image_format =
         vk_format_to_pipe_format(image_format);
      util_pack_color(color->float32, pipe_image_format, &uc);
      memcpy(hw_color, uc.ui, internal_size);
   }
}

/* Returns true if the implementation is able to handle the case, false
 * otherwise.
*/
static bool
clear_image_tlb(struct v3dv_cmd_buffer *cmd_buffer,
                struct v3dv_image *image,
                const VkClearValue *clear_value,
                const VkImageSubresourceRange *range)
{
   const VkOffset3D origin = { 0, 0, 0 };
   VkFormat fb_format;
   if (!can_use_tlb(image, &origin, &fb_format))
      return false;

   uint32_t internal_type, internal_bpp;
   get_internal_type_bpp_for_image_aspects(fb_format, range->aspectMask,
                                           &internal_type, &internal_bpp);

   union v3dv_clear_value hw_clear_value = { 0 };
   if (range->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
      get_hw_clear_color(&clear_value->color, fb_format, image->vk_format,
                         internal_type, internal_bpp, &hw_clear_value.color[0]);
   } else {
      assert((range->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) ||
             (range->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT));
      hw_clear_value.z = clear_value->depthStencil.depth;
      hw_clear_value.s = clear_value->depthStencil.stencil;
   }

   uint32_t level_count = range->levelCount == VK_REMAINING_MIP_LEVELS ?
                          image->levels - range->baseMipLevel :
                          range->levelCount;
   uint32_t min_level = range->baseMipLevel;
   uint32_t max_level = range->baseMipLevel + level_count;

   /* For 3D images baseArrayLayer and layerCount must be 0 and 1 respectively.
    * Instead, we need to consider the full depth dimension of the image, which
    * goes from 0 up to the level's depth extent.
    */
   uint32_t min_layer;
   uint32_t max_layer;
   if (image->type != VK_IMAGE_TYPE_3D) {
      uint32_t layer_count = range->layerCount == VK_REMAINING_ARRAY_LAYERS ?
                             image->array_size - range->baseArrayLayer :
                             range->layerCount;
      min_layer = range->baseArrayLayer;
      max_layer = range->baseArrayLayer + layer_count;
   } else {
      min_layer = 0;
      max_layer = 0;
   }

   for (uint32_t level = min_level; level < max_level; level++) {
      if (image->type == VK_IMAGE_TYPE_3D)
         max_layer = u_minify(image->extent.depth, level);
      for (uint32_t layer = min_layer; layer < max_layer; layer++) {
         uint32_t width = u_minify(image->extent.width, level);
         uint32_t height = u_minify(image->extent.height, level);

         struct v3dv_job *job =
            v3dv_cmd_buffer_start_job(cmd_buffer, -1, V3DV_JOB_TYPE_GPU_CL);

         if (!job)
            return true;

         /* We start a a new job for each layer so the frame "depth" is 1 */
         v3dv_job_start_frame(job, width, height, 1, 1, internal_bpp,
                              image->samples > VK_SAMPLE_COUNT_1_BIT);

         struct framebuffer_data framebuffer;
         setup_framebuffer_data(&framebuffer, fb_format, internal_type,
                                &job->frame_tiling);

         v3dv_job_emit_binning_flush(job);

         /* If this triggers it is an application bug: the spec requires
          * that any aspects to clear are present in the image.
          */
         assert(range->aspectMask & image->aspects);

         emit_clear_image_rcl(job, image, &framebuffer, &hw_clear_value,
                             range->aspectMask, layer, level);

         v3dv_cmd_buffer_finish_job(cmd_buffer);
      }
   }

   return true;
}

void
v3dv_CmdClearColorImage(VkCommandBuffer commandBuffer,
                        VkImage _image,
                        VkImageLayout imageLayout,
                        const VkClearColorValue *pColor,
                        uint32_t rangeCount,
                        const VkImageSubresourceRange *pRanges)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_image, image, _image);

   const VkClearValue clear_value = {
      .color = *pColor,
   };

   for (uint32_t i = 0; i < rangeCount; i++) {
      if (clear_image_tlb(cmd_buffer, image, &clear_value, &pRanges[i]))
         continue;
      unreachable("Unsupported color clear.");
   }
}

void
v3dv_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer,
                               VkImage _image,
                               VkImageLayout imageLayout,
                               const VkClearDepthStencilValue *pDepthStencil,
                               uint32_t rangeCount,
                               const VkImageSubresourceRange *pRanges)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_image, image, _image);

   const VkClearValue clear_value = {
      .depthStencil = *pDepthStencil,
   };

   for (uint32_t i = 0; i < rangeCount; i++) {
      if (clear_image_tlb(cmd_buffer, image, &clear_value, &pRanges[i]))
         continue;
      unreachable("Unsupported depth/stencil clear.");
   }
}

static void
emit_copy_buffer_per_tile_list(struct v3dv_job *job,
                               struct v3dv_bo *dst,
                               struct v3dv_bo *src,
                               uint32_t dst_offset,
                               uint32_t src_offset,
                               uint32_t stride,
                               uint32_t format)
{
   struct v3dv_cl *cl = &job->indirect;
   v3dv_cl_ensure_space(cl, 200, 1);
   v3dv_return_if_oom(NULL, job);

   struct v3dv_cl_reloc tile_list_start = v3dv_cl_get_address(cl);

   cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);

   emit_linear_load(cl, RENDER_TARGET_0, src, src_offset, stride, format);

   cl_emit(cl, END_OF_LOADS, end);

   cl_emit(cl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

   emit_linear_store(cl, RENDER_TARGET_0,
                     dst, dst_offset, stride, false, format);

   cl_emit(cl, END_OF_TILE_MARKER, end);

   cl_emit(cl, RETURN_FROM_SUB_LIST, ret);

   cl_emit(&job->rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
      branch.start = tile_list_start;
      branch.end = v3dv_cl_get_address(cl);
   }
}

static void
emit_copy_buffer(struct v3dv_job *job,
                 struct v3dv_bo *dst,
                 struct v3dv_bo *src,
                 uint32_t dst_offset,
                 uint32_t src_offset,
                 struct framebuffer_data *framebuffer,
                 uint32_t format)
{
   const uint32_t stride = job->frame_tiling.width * 4;
   emit_copy_buffer_per_tile_list(job, dst, src,
                                  dst_offset, src_offset,
                                  stride, format);
   emit_supertile_coordinates(job, framebuffer);
}

static void
emit_copy_buffer_rcl(struct v3dv_job *job,
                     struct v3dv_bo *dst,
                     struct v3dv_bo *src,
                     uint32_t dst_offset,
                     uint32_t src_offset,
                     struct framebuffer_data *framebuffer,
                     uint32_t format)
{
   struct v3dv_cl *rcl = emit_rcl_prologue(job, framebuffer, NULL);
   v3dv_return_if_oom(NULL, job);

   emit_frame_setup(job, 0, NULL);
   emit_copy_buffer(job, dst, src, dst_offset, src_offset, framebuffer, format);
   cl_emit(rcl, END_OF_RENDERING, end);
}

/* Figure out a TLB size configuration for a number of pixels to process.
 * Beware that we can't "render" more than 4096x4096 pixels in a single job,
 * if the pixel count is larger than this, the caller might need to split
 * the job and call this function multiple times.
 */
static void
framebuffer_size_for_pixel_count(uint32_t num_pixels,
                                 uint32_t *width,
                                 uint32_t *height)
{
   assert(num_pixels > 0);

   const uint32_t max_dim_pixels = 4096;
   const uint32_t max_pixels = max_dim_pixels * max_dim_pixels;

   uint32_t w, h;
   if (num_pixels > max_pixels) {
      w = max_dim_pixels;
      h = max_dim_pixels;
   } else {
      w = num_pixels;
      h = 1;
      while (w > max_dim_pixels || ((w % 2) == 0 && w > 2 * h)) {
         w >>= 1;
         h <<= 1;
      }
   }
   assert(w <= max_dim_pixels && h <= max_dim_pixels);
   assert(w * h <= num_pixels);
   assert(w > 0 && h > 0);

   *width = w;
   *height = h;
}

static struct v3dv_job *
copy_buffer(struct v3dv_cmd_buffer *cmd_buffer,
            struct v3dv_bo *dst,
            uint32_t dst_offset,
            struct v3dv_bo *src,
            uint32_t src_offset,
            const VkBufferCopy *region)
{
   const uint32_t internal_bpp = V3D_INTERNAL_BPP_32;
   const uint32_t internal_type = V3D_INTERNAL_TYPE_8UI;

   /* Select appropriate pixel format for the copy operation based on the
    * size to copy and the alignment of the source and destination offsets.
    */
   src_offset += region->srcOffset;
   dst_offset += region->dstOffset;
   uint32_t item_size = 4;
   while (item_size > 1 &&
          (src_offset % item_size != 0 || dst_offset % item_size != 0)) {
      item_size /= 2;
   }

   while (item_size > 1 && region->size % item_size != 0)
      item_size /= 2;

   assert(region->size % item_size == 0);
   uint32_t num_items = region->size / item_size;
   assert(num_items > 0);

   uint32_t format;
   VkFormat vk_format;
   switch (item_size) {
   case 4:
      format = V3D_OUTPUT_IMAGE_FORMAT_RGBA8UI;
      vk_format = VK_FORMAT_R8G8B8A8_UINT;
      break;
   case 2:
      format = V3D_OUTPUT_IMAGE_FORMAT_RG8UI;
      vk_format = VK_FORMAT_R8G8_UINT;
      break;
   default:
      format = V3D_OUTPUT_IMAGE_FORMAT_R8UI;
      vk_format = VK_FORMAT_R8_UINT;
      break;
   }

   struct v3dv_job *job = NULL;
   while (num_items > 0) {
      job = v3dv_cmd_buffer_start_job(cmd_buffer, -1, V3DV_JOB_TYPE_GPU_CL);
      if (!job)
         return NULL;

      uint32_t width, height;
      framebuffer_size_for_pixel_count(num_items, &width, &height);

      v3dv_job_start_frame(job, width, height, 1, 1, internal_bpp, false);

      struct framebuffer_data framebuffer;
      setup_framebuffer_data(&framebuffer, vk_format, internal_type,
                             &job->frame_tiling);

      v3dv_job_emit_binning_flush(job);

      emit_copy_buffer_rcl(job, dst, src, dst_offset, src_offset,
                           &framebuffer, format);

      v3dv_cmd_buffer_finish_job(cmd_buffer);

      const uint32_t items_copied = width * height;
      const uint32_t bytes_copied = items_copied * item_size;
      num_items -= items_copied;
      src_offset += bytes_copied;
      dst_offset += bytes_copied;
   }

   return job;
}

void
v3dv_CmdCopyBuffer(VkCommandBuffer commandBuffer,
                   VkBuffer srcBuffer,
                   VkBuffer dstBuffer,
                   uint32_t regionCount,
                   const VkBufferCopy *pRegions)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_buffer, src_buffer, srcBuffer);
   V3DV_FROM_HANDLE(v3dv_buffer, dst_buffer, dstBuffer);

   for (uint32_t i = 0; i < regionCount; i++) {
     copy_buffer(cmd_buffer,
                 dst_buffer->mem->bo, dst_buffer->mem_offset,
                 src_buffer->mem->bo, src_buffer->mem_offset,
                 &pRegions[i]);
   }
}

static void
destroy_update_buffer_cb(VkDevice _device,
                         uint64_t pobj,
                         VkAllocationCallbacks *alloc)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_bo *bo = (struct v3dv_bo *)((uintptr_t) pobj);
   v3dv_bo_free(device, bo);
}

void
v3dv_CmdUpdateBuffer(VkCommandBuffer commandBuffer,
                     VkBuffer dstBuffer,
                     VkDeviceSize dstOffset,
                     VkDeviceSize dataSize,
                     const void *pData)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_buffer, dst_buffer, dstBuffer);

   struct v3dv_bo *src_bo =
      v3dv_bo_alloc(cmd_buffer->device, dataSize, "vkCmdUpdateBuffer", true);
   if (!src_bo) {
      fprintf(stderr, "Failed to allocate BO for vkCmdUpdateBuffer.\n");
      return;
   }

   bool ok = v3dv_bo_map(cmd_buffer->device, src_bo, src_bo->size);
   if (!ok) {
      fprintf(stderr, "Failed to map BO for vkCmdUpdateBuffer.\n");
      return;
   }

   memcpy(src_bo->map, pData, dataSize);

   v3dv_bo_unmap(cmd_buffer->device, src_bo);

   VkBufferCopy region = {
      .srcOffset = 0,
      .dstOffset = dstOffset,
      .size = dataSize,
   };
   struct v3dv_job *copy_job =
      copy_buffer(cmd_buffer,
                  dst_buffer->mem->bo, dst_buffer->mem_offset,
                  src_bo, 0,
                  &region);
   if (!copy_job)
      return;

   v3dv_cmd_buffer_add_private_obj(
      cmd_buffer, (uint64_t)(uintptr_t)src_bo, destroy_update_buffer_cb);
}

static void
emit_fill_buffer_per_tile_list(struct v3dv_job *job,
                               struct v3dv_bo *bo,
                               uint32_t offset,
                               uint32_t stride)
{
   struct v3dv_cl *cl = &job->indirect;
   v3dv_cl_ensure_space(cl, 200, 1);
   v3dv_return_if_oom(NULL, job);

   struct v3dv_cl_reloc tile_list_start = v3dv_cl_get_address(cl);

   cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);

   cl_emit(cl, END_OF_LOADS, end);

   cl_emit(cl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

   emit_linear_store(cl, RENDER_TARGET_0, bo, offset, stride, false,
                     V3D_OUTPUT_IMAGE_FORMAT_RGBA8UI);

   cl_emit(cl, END_OF_TILE_MARKER, end);

   cl_emit(cl, RETURN_FROM_SUB_LIST, ret);

   cl_emit(&job->rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
      branch.start = tile_list_start;
      branch.end = v3dv_cl_get_address(cl);
   }
}

static void
emit_fill_buffer(struct v3dv_job *job,
                 struct v3dv_bo *bo,
                 uint32_t offset,
                 struct framebuffer_data *framebuffer)
{
   const uint32_t stride = job->frame_tiling.width * 4;
   emit_fill_buffer_per_tile_list(job, bo, offset, stride);
   emit_supertile_coordinates(job, framebuffer);
}

static void
emit_fill_buffer_rcl(struct v3dv_job *job,
                     struct v3dv_bo *bo,
                     uint32_t offset,
                     struct framebuffer_data *framebuffer,
                     uint32_t data)
{
   const union v3dv_clear_value clear_value = {
       .color = { data, 0, 0, 0 },
   };

   const struct rcl_clear_info clear_info = {
      .clear_value = &clear_value,
      .image = NULL,
      .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
      .layer = 0,
      .level = 0,
   };

   struct v3dv_cl *rcl = emit_rcl_prologue(job, framebuffer, &clear_info);
   v3dv_return_if_oom(NULL, job);

   emit_frame_setup(job, 0, &clear_value);
   emit_fill_buffer(job, bo, offset, framebuffer);
   cl_emit(rcl, END_OF_RENDERING, end);
}

static void
fill_buffer(struct v3dv_cmd_buffer *cmd_buffer,
            struct v3dv_bo *bo,
            uint32_t offset,
            uint32_t size,
            uint32_t data)
{
   assert(size > 0 && size % 4 == 0);
   assert(offset + size <= bo->size);

   const uint32_t internal_bpp = V3D_INTERNAL_BPP_32;
   const uint32_t internal_type = V3D_INTERNAL_TYPE_8UI;
   uint32_t num_items = size / 4;

   while (num_items > 0) {
      struct v3dv_job *job =
         v3dv_cmd_buffer_start_job(cmd_buffer, -1, V3DV_JOB_TYPE_GPU_CL);
      if (!job)
         return;

      uint32_t width, height;
      framebuffer_size_for_pixel_count(num_items, &width, &height);

      v3dv_job_start_frame(job, width, height, 1, 1, internal_bpp, false);

      struct framebuffer_data framebuffer;
      setup_framebuffer_data(&framebuffer, VK_FORMAT_R8G8B8A8_UINT,
                             internal_type, &job->frame_tiling);

      v3dv_job_emit_binning_flush(job);

      emit_fill_buffer_rcl(job, bo, offset, &framebuffer, data);

      v3dv_cmd_buffer_finish_job(cmd_buffer);

      const uint32_t items_copied = width * height;
      const uint32_t bytes_copied = items_copied * 4;
      num_items -= items_copied;
      offset += bytes_copied;
   }
}

void
v3dv_CmdFillBuffer(VkCommandBuffer commandBuffer,
                   VkBuffer dstBuffer,
                   VkDeviceSize dstOffset,
                   VkDeviceSize size,
                   uint32_t data)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_buffer, dst_buffer, dstBuffer);

   struct v3dv_bo *bo = dst_buffer->mem->bo;

   /* From the Vulkan spec:
    *
    *   "If VK_WHOLE_SIZE is used and the remaining size of the buffer is not
    *    a multiple of 4, then the nearest smaller multiple is used."
    */
   if (size == VK_WHOLE_SIZE) {
      size = dst_buffer->size - dstOffset;
      size -= size % 4;
   }

   fill_buffer(cmd_buffer, bo, dstOffset, size, data);
}

/**
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 */
static bool
copy_buffer_to_image_tfu(struct v3dv_cmd_buffer *cmd_buffer,
                         struct v3dv_image *image,
                         struct v3dv_buffer *buffer,
                         const VkBufferImageCopy *region)
{
   assert(image->samples == VK_SAMPLE_COUNT_1_BIT);

   /* Destination can't be raster format */
   if (image->tiling == VK_IMAGE_TILING_LINEAR)
      return false;

   /* We can't copy D24S8 because buffer to image copies only copy one aspect
    * at a time, and the TFU copies full images. Also, V3D depth bits for
    * both D24S8 and D24X8 stored in the 24-bit MSB of each 32-bit word, but
    * the Vulkan spec has the buffer data specified the other way around, so it
    * is not a straight copy, we would havew to swizzle the channels, which the
    * TFU can't do.
    */
   if (image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT ||
       image->vk_format == VK_FORMAT_X8_D24_UNORM_PACK32) {
         return false;
   }

   /* Region must include full slice */
   const uint32_t offset_x = region->imageOffset.x;
   const uint32_t offset_y = region->imageOffset.y;
   if (offset_x != 0 || offset_y != 0)
      return false;

   uint32_t width, height;
   if (region->bufferRowLength == 0)
      width = region->imageExtent.width;
   else
      width = region->bufferRowLength;

   if (region->bufferImageHeight == 0)
      height = region->imageExtent.height;
   else
      height = region->bufferImageHeight;

   if (width != image->extent.width || height != image->extent.height)
      return false;

   /* Handle region semantics for compressed images */
   const uint32_t block_w = vk_format_get_blockwidth(image->vk_format);
   const uint32_t block_h = vk_format_get_blockheight(image->vk_format);
   width = DIV_ROUND_UP(width, block_w);
   height = DIV_ROUND_UP(height, block_h);

   /* Format must be supported for texturing via the TFU. Since we are just
    * copying raw data and not converting between pixel formats, we can ignore
    * the image's format and choose a compatible TFU format for the image
    * texel size instead, which expands the list of formats we can handle here.
    */
   const struct v3dv_format *format =
      v3dv_get_compatible_tfu_format(&cmd_buffer->device->devinfo,
                                     image->cpp, NULL);

   const uint32_t mip_level = region->imageSubresource.mipLevel;
   const struct v3d_resource_slice *slice = &image->slices[mip_level];

   uint32_t num_layers;
   if (image->type != VK_IMAGE_TYPE_3D)
      num_layers = region->imageSubresource.layerCount;
   else
      num_layers = region->imageExtent.depth;
   assert(num_layers > 0);

   assert(image->mem && image->mem->bo);
   const struct v3dv_bo *dst_bo = image->mem->bo;

   assert(buffer->mem && buffer->mem->bo);
   const struct v3dv_bo *src_bo = buffer->mem->bo;

   /* Emit a TFU job per layer to copy */
   const uint32_t buffer_stride = width * image->cpp;
   for (int i = 0; i < num_layers; i++) {
      uint32_t layer = region->imageSubresource.baseArrayLayer + i;

      struct drm_v3d_submit_tfu tfu = {
         .ios = (height << 16) | width,
         .bo_handles = {
            dst_bo->handle,
            src_bo->handle != dst_bo->handle ? src_bo->handle : 0
         },
      };

      const uint32_t buffer_offset =
         buffer->mem_offset + region->bufferOffset +
         height * buffer_stride * i;

      const uint32_t src_offset = src_bo->offset + buffer_offset;
      tfu.iia |= src_offset;
      tfu.icfg |= V3D_TFU_ICFG_FORMAT_RASTER << V3D_TFU_ICFG_FORMAT_SHIFT;
      tfu.iis |= width;

      const uint32_t dst_offset =
         dst_bo->offset + v3dv_layer_offset(image, mip_level, layer);
      tfu.ioa |= dst_offset;

      tfu.ioa |= (V3D_TFU_IOA_FORMAT_LINEARTILE +
                  (slice->tiling - VC5_TILING_LINEARTILE)) <<
                   V3D_TFU_IOA_FORMAT_SHIFT;
      tfu.icfg |= format->tex_type << V3D_TFU_ICFG_TTYPE_SHIFT;

      /* If we're writing level 0 (!IOA_DIMTW), then we need to supply the
       * OPAD field for the destination (how many extra UIF blocks beyond
       * those necessary to cover the height).
       */
      if (slice->tiling == VC5_TILING_UIF_NO_XOR ||
          slice->tiling == VC5_TILING_UIF_XOR) {
         uint32_t uif_block_h = 2 * v3d_utile_height(image->cpp);
         uint32_t implicit_padded_height = align(height, uif_block_h);
         uint32_t icfg =
            (slice->padded_height - implicit_padded_height) / uif_block_h;
         tfu.icfg |= icfg << V3D_TFU_ICFG_OPAD_SHIFT;
      }

      v3dv_cmd_buffer_add_tfu_job(cmd_buffer, &tfu);
   }

   return true;
}

static void
emit_copy_buffer_to_layer_per_tile_list(struct v3dv_job *job,
                                        struct framebuffer_data *framebuffer,
                                        struct v3dv_image *image,
                                        struct v3dv_buffer *buffer,
                                        uint32_t layer,
                                        const VkBufferImageCopy *region)
{
   struct v3dv_cl *cl = &job->indirect;
   v3dv_cl_ensure_space(cl, 200, 1);
   v3dv_return_if_oom(NULL, job);

   struct v3dv_cl_reloc tile_list_start = v3dv_cl_get_address(cl);

   cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);

   const VkImageSubresourceLayers *imgrsc = &region->imageSubresource;
   assert((image->type != VK_IMAGE_TYPE_3D && layer < imgrsc->layerCount) ||
          layer < image->extent.depth);

   /* Load TLB from buffer */
   uint32_t width, height;
   if (region->bufferRowLength == 0)
      width = region->imageExtent.width;
   else
      width = region->bufferRowLength;

   if (region->bufferImageHeight == 0)
      height = region->imageExtent.height;
   else
      height = region->bufferImageHeight;

   /* Handle copy to compressed format using a compatible format */
   width = DIV_ROUND_UP(width, vk_format_get_blockwidth(image->vk_format));
   height = DIV_ROUND_UP(height, vk_format_get_blockheight(image->vk_format));

   uint32_t cpp = imgrsc->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT ?
                  1 : image->cpp;
   uint32_t buffer_stride = width * cpp;
   uint32_t buffer_offset =
      buffer->mem_offset + region->bufferOffset + height * buffer_stride * layer;

   uint32_t format = choose_tlb_format(framebuffer, imgrsc->aspectMask,
                                       false, false, true);

   emit_linear_load(cl, RENDER_TARGET_0, buffer->mem->bo,
                    buffer_offset, buffer_stride, format);

   /* Because we can't do raster loads/stores of Z/S formats we need to
    * use a color tile buffer with a compatible RGBA color format instead.
    * However, when we are uploading a single aspect to a combined
    * depth/stencil image we have the problem that our tile buffer stores don't
    * allow us to mask out the other aspect, so we always write all four RGBA
    * channels to the image and we end up overwriting that other aspect with
    * undefined values. To work around that, we first load the aspect we are
    * not copying from the image memory into a proper Z/S tile buffer. Then we
    * do our store from the color buffer for the aspect we are copying, and
    * after that, we do another store from the Z/S tile buffer to restore the
    * other aspect to its original value.
    */
   if (framebuffer->vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
      if (imgrsc->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
         emit_image_load(cl, framebuffer, image, VK_IMAGE_ASPECT_STENCIL_BIT,
                         imgrsc->baseArrayLayer + layer, imgrsc->mipLevel,
                         false, false);
      } else {
         assert(imgrsc->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT);
         emit_image_load(cl, framebuffer, image, VK_IMAGE_ASPECT_DEPTH_BIT,
                         imgrsc->baseArrayLayer + layer, imgrsc->mipLevel,
                         false, false);
      }
   }

   cl_emit(cl, END_OF_LOADS, end);

   cl_emit(cl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

   /* Store TLB to image */
   emit_image_store(cl, framebuffer, image, imgrsc->aspectMask,
                    imgrsc->baseArrayLayer + layer, imgrsc->mipLevel,
                    false, true, false);

   if (framebuffer->vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
      if (imgrsc->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
         emit_image_store(cl, framebuffer, image, VK_IMAGE_ASPECT_STENCIL_BIT,
                          imgrsc->baseArrayLayer + layer, imgrsc->mipLevel,
                          false, false, false);
      } else {
         assert(imgrsc->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT);
         emit_image_store(cl, framebuffer, image, VK_IMAGE_ASPECT_DEPTH_BIT,
                          imgrsc->baseArrayLayer + layer, imgrsc->mipLevel,
                          false, false, false);
      }
   }

   cl_emit(cl, END_OF_TILE_MARKER, end);

   cl_emit(cl, RETURN_FROM_SUB_LIST, ret);

   cl_emit(&job->rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
      branch.start = tile_list_start;
      branch.end = v3dv_cl_get_address(cl);
   }
}

static void
emit_copy_buffer_to_layer(struct v3dv_job *job,
                          struct v3dv_image *image,
                          struct v3dv_buffer *buffer,
                          struct framebuffer_data *framebuffer,
                          uint32_t layer,
                          const VkBufferImageCopy *region)
{
   emit_frame_setup(job, layer, NULL);
   emit_copy_buffer_to_layer_per_tile_list(job, framebuffer, image, buffer,
                                           layer, region);
   emit_supertile_coordinates(job, framebuffer);
}

static void
emit_copy_buffer_to_image_rcl(struct v3dv_job *job,
                              struct v3dv_image *image,
                              struct v3dv_buffer *buffer,
                              struct framebuffer_data *framebuffer,
                              const VkBufferImageCopy *region)
{
   struct v3dv_cl *rcl = emit_rcl_prologue(job, framebuffer, NULL);
   v3dv_return_if_oom(NULL, job);

   for (int layer = 0; layer < job->frame_tiling.layers; layer++)
      emit_copy_buffer_to_layer(job, image, buffer, framebuffer, layer, region);
   cl_emit(rcl, END_OF_RENDERING, end);
}

/**
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 */
static bool
copy_buffer_to_image_tlb(struct v3dv_cmd_buffer *cmd_buffer,
                         struct v3dv_image *image,
                         struct v3dv_buffer *buffer,
                         const VkBufferImageCopy *region)
{
   VkFormat fb_format;
   if (!can_use_tlb(image, &region->imageOffset, &fb_format))
      return false;

   uint32_t internal_type, internal_bpp;
   get_internal_type_bpp_for_image_aspects(fb_format,
                                           region->imageSubresource.aspectMask,
                                           &internal_type, &internal_bpp);

   uint32_t num_layers;
   if (image->type != VK_IMAGE_TYPE_3D)
      num_layers = region->imageSubresource.layerCount;
   else
      num_layers = region->imageExtent.depth;
   assert(num_layers > 0);

   struct v3dv_job *job =
      v3dv_cmd_buffer_start_job(cmd_buffer, -1, V3DV_JOB_TYPE_GPU_CL);
   if (!job)
      return true;

   /* Handle copy to compressed format using a compatible format */
   const uint32_t block_w = vk_format_get_blockwidth(image->vk_format);
   const uint32_t block_h = vk_format_get_blockheight(image->vk_format);
   const uint32_t width = DIV_ROUND_UP(region->imageExtent.width, block_w);
   const uint32_t height = DIV_ROUND_UP(region->imageExtent.height, block_h);

   v3dv_job_start_frame(job, width, height, num_layers, 1, internal_bpp, false);

   struct framebuffer_data framebuffer;
   setup_framebuffer_data(&framebuffer, fb_format, internal_type,
                          &job->frame_tiling);

   v3dv_job_emit_binning_flush(job);
   emit_copy_buffer_to_image_rcl(job, image, buffer, &framebuffer, region);

   v3dv_cmd_buffer_finish_job(cmd_buffer);

   return true;
}

static bool
create_tiled_image_from_buffer(struct v3dv_cmd_buffer *cmd_buffer,
                               struct v3dv_image *image,
                               struct v3dv_buffer *buffer,
                               const VkBufferImageCopy *region)
{
   if (copy_buffer_to_image_tfu(cmd_buffer, image, buffer, region))
      return true;
   if (copy_buffer_to_image_tlb(cmd_buffer, image, buffer, region))
      return true;
   return false;
}

static VkResult
create_texel_buffer_copy_descriptor_pool(struct v3dv_cmd_buffer *cmd_buffer)
{
   /* If this is not the first pool we create for this command buffer
    * size it based on the size of the currently exhausted pool.
    */
   uint32_t descriptor_count = 64;
   if (cmd_buffer->meta.texel_buffer_copy.dspool != VK_NULL_HANDLE) {
      struct v3dv_descriptor_pool *exhausted_pool =
         v3dv_descriptor_pool_from_handle(cmd_buffer->meta.texel_buffer_copy.dspool);
      descriptor_count = MIN2(exhausted_pool->max_entry_count * 2, 1024);
   }

   /* Create the descriptor pool */
   cmd_buffer->meta.texel_buffer_copy.dspool = VK_NULL_HANDLE;
   VkDescriptorPoolSize pool_size = {
      .type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
      .descriptorCount = descriptor_count,
   };
   VkDescriptorPoolCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = descriptor_count,
      .poolSizeCount = 1,
      .pPoolSizes = &pool_size,
      .flags = 0,
   };
   VkResult result =
      v3dv_CreateDescriptorPool(v3dv_device_to_handle(cmd_buffer->device),
                                &info,
                                &cmd_buffer->device->vk.alloc,
                                &cmd_buffer->meta.texel_buffer_copy.dspool);

   if (result == VK_SUCCESS) {
      assert(cmd_buffer->meta.texel_buffer_copy.dspool != VK_NULL_HANDLE);
      const VkDescriptorPool _pool = cmd_buffer->meta.texel_buffer_copy.dspool;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t) _pool,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyDescriptorPool);

      struct v3dv_descriptor_pool *pool =
         v3dv_descriptor_pool_from_handle(_pool);
      pool->is_driver_internal = true;
   }

   return result;
}

static VkResult
allocate_texel_buffer_copy_descriptor_set(struct v3dv_cmd_buffer *cmd_buffer,
                                          VkDescriptorSet *set)
{
   /* Make sure we have a descriptor pool */
   VkResult result;
   if (cmd_buffer->meta.texel_buffer_copy.dspool == VK_NULL_HANDLE) {
      result = create_texel_buffer_copy_descriptor_pool(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;
   }
   assert(cmd_buffer->meta.texel_buffer_copy.dspool != VK_NULL_HANDLE);

   /* Allocate descriptor set */
   struct v3dv_device *device = cmd_buffer->device;
   VkDevice _device = v3dv_device_to_handle(device);
   VkDescriptorSetAllocateInfo info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = cmd_buffer->meta.texel_buffer_copy.dspool,
      .descriptorSetCount = 1,
      .pSetLayouts = &device->meta.texel_buffer_copy.ds_layout,
   };
   result = v3dv_AllocateDescriptorSets(_device, &info, set);

   /* If we ran out of pool space, grow the pool and try again */
   if (result == VK_ERROR_OUT_OF_POOL_MEMORY) {
      result = create_texel_buffer_copy_descriptor_pool(cmd_buffer);
      if (result == VK_SUCCESS) {
         info.descriptorPool = cmd_buffer->meta.texel_buffer_copy.dspool;
         result = v3dv_AllocateDescriptorSets(_device, &info, set);
      }
   }

   return result;
}

static void
get_texel_buffer_copy_pipeline_cache_key(VkFormat format,
                                         uint8_t *key)
{
   memset(key, 0, V3DV_META_TEXEL_BUFFER_COPY_CACHE_KEY_SIZE);

   uint32_t *p = (uint32_t *) key;

   *p = format;
   p++;

   assert(((uint8_t*)p - key) == V3DV_META_TEXEL_BUFFER_COPY_CACHE_KEY_SIZE);
}

static bool
create_blit_render_pass(struct v3dv_device *device,
                        VkFormat dst_format,
                        VkFormat src_format,
                        VkRenderPass *pass_load,
                        VkRenderPass *pass_no_load);

static nir_ssa_def *gen_rect_vertices(nir_builder *b);

static bool
create_pipeline(struct v3dv_device *device,
                struct v3dv_render_pass *pass,
                struct nir_shader *vs_nir,
                struct nir_shader *fs_nir,
                const VkPipelineVertexInputStateCreateInfo *vi_state,
                const VkPipelineDepthStencilStateCreateInfo *ds_state,
                const VkPipelineColorBlendStateCreateInfo *cb_state,
                const VkPipelineMultisampleStateCreateInfo *ms_state,
                const VkPipelineLayout layout,
                VkPipeline *pipeline);

static nir_shader *
get_texel_buffer_copy_vs()
{
   const nir_shader_compiler_options *options = v3dv_pipeline_get_nir_options();
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, options,
                                                  "meta texel buffer copy vs");
   nir_variable *vs_out_pos =
      nir_variable_create(b.shader, nir_var_shader_out,
                          glsl_vec4_type(), "gl_Position");
   vs_out_pos->data.location = VARYING_SLOT_POS;

   nir_ssa_def *pos = gen_rect_vertices(&b);
   nir_store_var(&b, vs_out_pos, pos, 0xf);

   return b.shader;
}

static nir_ssa_def *
load_frag_coord(nir_builder *b)
{
   nir_foreach_shader_in_variable(var, b->shader) {
      if (var->data.location == VARYING_SLOT_POS)
         return nir_load_var(b, var);
   }
   nir_variable *pos = nir_variable_create(b->shader, nir_var_shader_in,
                                           glsl_vec4_type(), NULL);
   pos->data.location = VARYING_SLOT_POS;
   return nir_load_var(b, pos);
}

static nir_shader *
get_texel_buffer_copy_fs(struct v3dv_device *device, VkFormat format)
{
   const nir_shader_compiler_options *options = v3dv_pipeline_get_nir_options();
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, options,
                                                  "meta texel buffer copy fs");

   /* We only use the copy from texel buffer shader to implement
    * copy_buffer_to_image_shader, which always selects a compatible integer
    * format for the copy.
    */
   assert(vk_format_is_int(format));

   /* Fragment shader output color */
   nir_variable *fs_out_color =
      nir_variable_create(b.shader, nir_var_shader_out,
                          glsl_uvec4_type(), "out_color");
   fs_out_color->data.location = FRAG_RESULT_DATA0;

   /* Texel buffer input */
   const struct glsl_type *sampler_type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_UINT);
   nir_variable *sampler =
      nir_variable_create(b.shader, nir_var_uniform, sampler_type, "texel_buf");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   /* Load the box describing the pixel region we want to copy from the
    * texel buffer.
    */
   nir_intrinsic_instr *box =
      nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
   box->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
   nir_intrinsic_set_base(box, 0);
   nir_intrinsic_set_range(box, 16);
   box->num_components = 4;
   nir_ssa_dest_init(&box->instr, &box->dest, 4, 32, "box");
   nir_builder_instr_insert(&b, &box->instr);

   /* Load the buffer stride (this comes in texel units) */
   nir_intrinsic_instr *stride =
      nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
   stride->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
   nir_intrinsic_set_base(stride, 16);
   nir_intrinsic_set_range(stride, 4);
   stride->num_components = 1;
   nir_ssa_dest_init(&stride->instr, &stride->dest, 1, 32, "buffer stride");
   nir_builder_instr_insert(&b, &stride->instr);

   /* Load the buffer offset (this comes in texel units) */
   nir_intrinsic_instr *offset =
      nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
   offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
   nir_intrinsic_set_base(offset, 20);
   nir_intrinsic_set_range(offset, 4);
   offset->num_components = 1;
   nir_ssa_dest_init(&offset->instr, &offset->dest, 1, 32, "buffer offset");
   nir_builder_instr_insert(&b, &offset->instr);

   nir_ssa_def *coord = nir_f2i32(&b, load_frag_coord(&b));

   /* Load pixel data from texel buffer based on the x,y offset of the pixel
    * within the box. Texel buffers are 1D arrays of texels.
    *
    * Notice that we already make sure that we only generate fragments that are
    * inside the box through the scissor/viewport state, so our offset into the
    * texel buffer should always be within its bounds and we we don't need
    * to add a check for that here.
    */
   nir_ssa_def *x_offset =
      nir_isub(&b, nir_channel(&b, coord, 0),
                   nir_channel(&b, &box->dest.ssa, 0));
   nir_ssa_def *y_offset =
      nir_isub(&b, nir_channel(&b, coord, 1),
                   nir_channel(&b, &box->dest.ssa, 1));
   nir_ssa_def *texel_offset =
      nir_iadd(&b, nir_iadd(&b, &offset->dest.ssa, x_offset),
                   nir_imul(&b, y_offset, &stride->dest.ssa));

   nir_ssa_def *tex_deref = &nir_build_deref_var(&b, sampler)->dest.ssa;
   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 2);
   tex->sampler_dim = GLSL_SAMPLER_DIM_BUF;
   tex->op = nir_texop_txf;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(texel_offset);
   tex->src[1].src_type = nir_tex_src_texture_deref;
   tex->src[1].src = nir_src_for_ssa(tex_deref);
   tex->dest_type = nir_type_uint;
   tex->is_array = false;
   tex->coord_components = 1;
   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "texel buffer result");
   nir_builder_instr_insert(&b, &tex->instr);

   nir_store_var(&b, fs_out_color, &tex->dest.ssa, 0xf);

   return b.shader;
}

static bool
create_texel_buffer_copy_pipeline(struct v3dv_device *device,
                                  VkFormat format,
                                  VkRenderPass _pass,
                                  VkPipelineLayout pipeline_layout,
                                  VkPipeline *pipeline)
{
   struct v3dv_render_pass *pass = v3dv_render_pass_from_handle(_pass);

   assert(vk_format_is_color(format));

   nir_shader *vs_nir = get_texel_buffer_copy_vs();
   nir_shader *fs_nir = get_texel_buffer_copy_fs(device, format);

   const VkPipelineVertexInputStateCreateInfo vi_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 0,
      .vertexAttributeDescriptionCount = 0,
   };

   VkPipelineDepthStencilStateCreateInfo ds_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
   };

   VkPipelineColorBlendAttachmentState blend_att_state[1] = { 0 };
   blend_att_state[0] = (VkPipelineColorBlendAttachmentState) {
      .blendEnable = false,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                        VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT,
   };

   const VkPipelineColorBlendStateCreateInfo cb_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = false,
      .attachmentCount = 1,
      .pAttachments = blend_att_state
   };

   const VkPipelineMultisampleStateCreateInfo ms_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = false,
      .pSampleMask = NULL,
      .alphaToCoverageEnable = false,
      .alphaToOneEnable = false,
   };

   return create_pipeline(device,
                          pass,
                          vs_nir, fs_nir,
                          &vi_state,
                          &ds_state,
                          &cb_state,
                          &ms_state,
                          pipeline_layout,
                          pipeline);
}

static bool
get_copy_texel_buffer_pipeline(
   struct v3dv_device *device,
   VkFormat format,
   VkImageType image_type,
   struct v3dv_meta_texel_buffer_copy_pipeline **pipeline)
{
   bool ok = true;

   uint8_t key[V3DV_META_TEXEL_BUFFER_COPY_CACHE_KEY_SIZE];
   get_texel_buffer_copy_pipeline_cache_key(format, key);

   mtx_lock(&device->meta.mtx);
   struct hash_entry *entry =
      _mesa_hash_table_search(device->meta.texel_buffer_copy.cache[image_type],
                              &key);
   if (entry) {
      mtx_unlock(&device->meta.mtx);
      *pipeline = entry->data;
      return true;
   }

   *pipeline = vk_zalloc2(&device->vk.alloc, NULL, sizeof(**pipeline), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (*pipeline == NULL)
      goto fail;

   /* The blit render pass is compatible */
   ok = create_blit_render_pass(device, format, format,
                                &(*pipeline)->pass,
                                &(*pipeline)->pass_no_load);
   if (!ok)
      goto fail;

   ok =
      create_texel_buffer_copy_pipeline(device,
                                        format,
                                        (*pipeline)->pass,
                                        device->meta.texel_buffer_copy.p_layout,
                                        &(*pipeline)->pipeline);
   if (!ok)
      goto fail;

   _mesa_hash_table_insert(device->meta.texel_buffer_copy.cache[image_type],
                           &key, *pipeline);

   mtx_unlock(&device->meta.mtx);
   return true;

fail:
   mtx_unlock(&device->meta.mtx);

   VkDevice _device = v3dv_device_to_handle(device);
   if (*pipeline) {
      if ((*pipeline)->pass)
         v3dv_DestroyRenderPass(_device, (*pipeline)->pass, &device->vk.alloc);
      if ((*pipeline)->pipeline)
         v3dv_DestroyPipeline(_device, (*pipeline)->pipeline, &device->vk.alloc);
      vk_free(&device->vk.alloc, *pipeline);
      *pipeline = NULL;
   }

   return false;
}

static bool
texel_buffer_shader_copy(struct v3dv_cmd_buffer *cmd_buffer,
                         VkImageAspectFlags aspect,
                         struct v3dv_image *image,
                         VkFormat dst_format,
                         VkFormat src_format,
                         struct v3dv_buffer *buffer,
                         uint32_t buffer_bpp,
                         VkColorComponentFlags cmask,
                         uint32_t region_count,
                         const VkBufferImageCopy *regions)
{
   VkResult result;
   bool handled = false;

   /* FIXME: we only handle exact copies for now. */
   if (src_format != dst_format)
      return handled;

   VkFormat format = dst_format;

   /* FIXME: we only handle color copies for now. */
   if (aspect != VK_IMAGE_ASPECT_COLOR_BIT)
      return handled;

   /* FIXME: we only handle uncompressed images for now. */
   if (vk_format_is_compressed(image->vk_format))
      return handled;

   /* FIXME: support partial color masks */
   const VkColorComponentFlags full_cmask = VK_COLOR_COMPONENT_R_BIT |
                                            VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT |
                                            VK_COLOR_COMPONENT_A_BIT;
   if (cmask == 0)
      cmask = full_cmask;

   if (cmask != full_cmask)
      return handled;

   /* The buffer needs to have VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
    * so we can bind it as a texel buffer. Otherwise, the buffer view
    * we create below won't setup the texture state that we need for this.
    */
   if (!(buffer->usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)) {
      if (v3dv_buffer_format_supports_features(
            format, VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)) {
         buffer->usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
      } else {
         return handled;
      }
   }

   /* At this point we should be able to handle the copy unless an unexpected
    * error occurs, such as an OOM.
    */
   handled = true;

   /* Get the texel buffer copy pipeline */
   struct v3dv_meta_texel_buffer_copy_pipeline *pipeline = NULL;
   bool ok = get_copy_texel_buffer_pipeline(cmd_buffer->device,
                                            format, image->type, &pipeline);
   if (!ok)
      return handled;
   assert(pipeline && pipeline->pipeline && pipeline->pass);

   /* Setup descriptor set for the source texel buffer. We don't have to
    * register the descriptor as a private command buffer object since
    * all descriptors will be freed automatically with the descriptor
    * pool.
    */
   VkDescriptorSet set;
   result = allocate_texel_buffer_copy_descriptor_set(cmd_buffer, &set);
   if (result != VK_SUCCESS)
      return handled;

   /* FIXME: for some reason passing region->bufferOffset here for the
    * offset field doesn't work, making the following CTS tests fail:
    *
    * dEQP-VK.api.copy_and_blit.core.buffer_to_image.*buffer_offset*
    *
    * So instead we pass 0 here and we pass the offset in texels as a push
    * constant to the shader, which seems to work correctly.
    */
   VkDevice _device = v3dv_device_to_handle(cmd_buffer->device);
   VkBufferViewCreateInfo buffer_view_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
      .buffer = v3dv_buffer_to_handle(buffer),
      .format = format,
      .offset = 0,
      .range = VK_WHOLE_SIZE,
   };

   VkBufferView texel_buffer_view;
   result = v3dv_CreateBufferView(_device, &buffer_view_info,
                                  &cmd_buffer->device->vk.alloc,
                                  &texel_buffer_view);
   if (result != VK_SUCCESS)
      return handled;

   v3dv_cmd_buffer_add_private_obj(
      cmd_buffer, (uintptr_t)texel_buffer_view,
      (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyBufferView);

   VkWriteDescriptorSet write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set,
      .dstBinding = 0,
      .dstArrayElement = 0,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
      .pTexelBufferView = &texel_buffer_view,
   };
   v3dv_UpdateDescriptorSets(_device, 1, &write, 0, NULL);

   /* Push command buffer state before starting meta operation */
   v3dv_cmd_buffer_meta_state_push(cmd_buffer, true);
   uint32_t dirty_dynamic_state = 0;

   /* Bind common state for all layers and regions  */
   VkCommandBuffer _cmd_buffer = v3dv_cmd_buffer_to_handle(cmd_buffer);
   v3dv_CmdBindPipeline(_cmd_buffer,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline->pipeline);

   v3dv_CmdBindDescriptorSets(_cmd_buffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              cmd_buffer->device->meta.texel_buffer_copy.p_layout,
                              0, 1, &set,
                              0, NULL);

   /* Compute the number of layers to copy.
    *
    * If we are batching (region_count > 1) all our regions have the same
    * image subresource so we can take this from the first region.
    */
   const VkImageSubresourceLayers *resource = &regions[0].imageSubresource;
   uint32_t num_layers;
   if (image->type != VK_IMAGE_TYPE_3D) {
      num_layers = resource->layerCount;
   } else {
      assert(region_count == 1);
      num_layers = regions[0].imageExtent.depth;
   }
   assert(num_layers > 0);

   /* Sanity check: we can only batch multiple regions together if they have
    * the same framebuffer (so the same layer).
    */
   assert(num_layers == 1 || region_count == 1);

   /* For each layer */
   for (uint32_t l = 0; l < num_layers; l++) {
      /* Setup framebuffer for this layer.
       *
       * FIXME: once we support geometry shaders, we should be able to have
       *        one layered framebuffer and emit just one draw call for
       *        all layers using layered rendering. At that point, we should
       *        also be able to batch multi-layered regions as well.
       */
      VkImageViewCreateInfo image_view_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = v3dv_image_to_handle(image),
         .viewType = v3dv_image_type_to_view_type(image->type),
         .format = format,
         .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = resource->mipLevel,
            .levelCount = 1,
            .baseArrayLayer = resource->baseArrayLayer + l,
            .layerCount = 1
         },
      };
      VkImageView image_view;
      result = v3dv_CreateImageView(_device, &image_view_info,
                                    &cmd_buffer->device->vk.alloc, &image_view);
      if (result != VK_SUCCESS)
         goto fail;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t)image_view,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyImageView);

      VkFramebufferCreateInfo fb_info = {
         .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
         .renderPass = pipeline->pass,
         .attachmentCount = 1,
         .pAttachments = &image_view,
         .width = u_minify(image->extent.width, resource->mipLevel),
         .height = u_minify(image->extent.height, resource->mipLevel),
         .layers = 1,
      };

      VkFramebuffer fb;
      result = v3dv_CreateFramebuffer(_device, &fb_info,
                                      &cmd_buffer->device->vk.alloc, &fb);
      if (result != VK_SUCCESS)
         goto fail;

       v3dv_cmd_buffer_add_private_obj(
          cmd_buffer, (uintptr_t)fb,
          (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyFramebuffer);

       /* Start render pass for this layer.
        *
        * If the we only have one region to copy, then we might be able to
        * skip the TLB load if it is aligned to tile boundaries. All layers
        * copy the same area, so we only need to check this once.
        */
      bool can_skip_tlb_load = false;
      VkRect2D render_area;
      if (region_count == 1) {
         render_area.offset.x = regions[0].imageOffset.x;
         render_area.offset.y = regions[0].imageOffset.y;
         render_area.extent.width = regions[0].imageExtent.width;
         render_area.extent.height = regions[0].imageExtent.height;

         if (l == 0) {
            struct v3dv_render_pass *pipeline_pass =
               v3dv_render_pass_from_handle(pipeline->pass);
            can_skip_tlb_load =
               v3dv_subpass_area_is_tile_aligned(&render_area,
                                                 v3dv_framebuffer_from_handle(fb),
                                                 pipeline_pass, 0);
         }
      } else {
         render_area.offset.x = 0;
         render_area.offset.y = 0;
         render_area.extent.width = fb_info.width;
         render_area.extent.height = fb_info.height;
      }

      VkRenderPassBeginInfo rp_info = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = can_skip_tlb_load ? pipeline->pass_no_load :
                                           pipeline->pass,
         .framebuffer = fb,
         .renderArea = render_area,
         .clearValueCount = 0,
      };

      v3dv_CmdBeginRenderPass(_cmd_buffer, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
      struct v3dv_job *job = cmd_buffer->state.job;
      if (!job)
         goto fail;

      /* For each region */
      dirty_dynamic_state = V3DV_CMD_DIRTY_VIEWPORT | V3DV_CMD_DIRTY_SCISSOR;
      for (uint32_t r = 0; r < region_count; r++) {
         const VkBufferImageCopy *region = &regions[r];

         /* Obtain the 2D buffer region spec */
         uint32_t buf_width, buf_height;
         if (region->bufferRowLength == 0)
             buf_width = region->imageExtent.width;
         else
             buf_width = region->bufferRowLength;

         if (region->bufferImageHeight == 0)
             buf_height = region->imageExtent.height;
         else
             buf_height = region->bufferImageHeight;

         const VkViewport viewport = {
            .x = region->imageOffset.x,
            .y = region->imageOffset.y,
            .width = region->imageExtent.width,
            .height = region->imageExtent.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f
         };
         v3dv_CmdSetViewport(_cmd_buffer, 0, 1, &viewport);
         const VkRect2D scissor = {
            .offset = { region->imageOffset.x, region->imageOffset.y },
            .extent = { region->imageExtent.width, region->imageExtent.height }
         };
         v3dv_CmdSetScissor(_cmd_buffer, 0, 1, &scissor);

         const VkDeviceSize buf_offset =
            region->bufferOffset / buffer_bpp  + l * buf_height * buf_width;
         uint32_t push_data[6] = {
            region->imageOffset.x,
            region->imageOffset.y,
            region->imageOffset.x + region->imageExtent.width - 1,
            region->imageOffset.y + region->imageExtent.height - 1,
            buf_width,
            buf_offset,
         };

         v3dv_CmdPushConstants(_cmd_buffer,
                               cmd_buffer->device->meta.texel_buffer_copy.p_layout,
                               VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(push_data), &push_data);

         v3dv_CmdDraw(_cmd_buffer, 4, 1, 0, 0);
      } /* For each region */

      v3dv_CmdEndRenderPass(_cmd_buffer);
   } /* For each layer */

fail:
   v3dv_cmd_buffer_meta_state_pop(cmd_buffer, dirty_dynamic_state, true);
   return handled;
}

static bool
copy_buffer_to_image_blit(struct v3dv_cmd_buffer *cmd_buffer,
                          VkImageAspectFlags aspect,
                          struct v3dv_image *image,
                          VkFormat dst_format,
                          VkFormat src_format,
                          struct v3dv_buffer *buffer,
                          uint32_t buffer_bpp,
                          VkColorComponentFlags cmask,
                          const VkBufferImageCopy *region)
{
   perf_debug("Falling back to blit path for buffer to image copy.\n");

   /* Obtain the layer count */
   uint32_t num_layers;
   if (image->type != VK_IMAGE_TYPE_3D)
      num_layers = region->imageSubresource.layerCount;
   else
      num_layers = region->imageExtent.depth;
   assert(num_layers > 0);

   /* Obtain the 2D buffer region spec */
   uint32_t buf_width, buf_height;
   if (region->bufferRowLength == 0)
       buf_width = region->imageExtent.width;
   else
       buf_width = region->bufferRowLength;

   if (region->bufferImageHeight == 0)
       buf_height = region->imageExtent.height;
   else
       buf_height = region->bufferImageHeight;

   /* If the image is compressed, the bpp refers to blocks, not pixels */
   uint32_t block_width = vk_format_get_blockwidth(image->vk_format);
   uint32_t block_height = vk_format_get_blockheight(image->vk_format);
   buf_width = buf_width / block_width;
   buf_height = buf_height / block_height;

   /* We should have configured the blit to use a supported format  */
   bool handled = true;

   struct v3dv_device *device = cmd_buffer->device;
   VkDevice _device = v3dv_device_to_handle(device);
   for (uint32_t i = 0; i < num_layers; i++) {
      /* Otherwise, since we can't sample linear images we need to upload the
       * linear buffer to a tiled image that we can use as a blit source, which
       * is slow.
       */
      VkImageCreateInfo image_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
         .imageType = VK_IMAGE_TYPE_2D,
         .format = src_format,
         .extent = { buf_width, buf_height, 1 },
         .mipLevels = 1,
         .arrayLayers = 1,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .tiling = VK_IMAGE_TILING_OPTIMAL,
         .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT,
         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
         .queueFamilyIndexCount = 0,
         .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
      };

      VkImage buffer_image;
      VkResult result =
         v3dv_CreateImage(_device, &image_info, &device->vk.alloc, &buffer_image);
      if (result != VK_SUCCESS)
         return handled;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t)buffer_image,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyImage);

      /* Allocate and bind memory for the image */
      VkDeviceMemory mem;
      VkMemoryRequirements reqs;
      v3dv_GetImageMemoryRequirements(_device, buffer_image, &reqs);
      VkMemoryAllocateInfo alloc_info = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = reqs.size,
         .memoryTypeIndex = 0,
      };
      result = v3dv_AllocateMemory(_device, &alloc_info, &device->vk.alloc, &mem);
      if (result != VK_SUCCESS)
         return handled;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t)mem,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_FreeMemory);

      result = v3dv_BindImageMemory(_device, buffer_image, mem, 0);
      if (result != VK_SUCCESS)
         return handled;

      /* Upload buffer contents for the selected layer */
      const VkDeviceSize buf_offset_bytes =
         region->bufferOffset + i * buf_height * buf_width * buffer_bpp;
      const VkBufferImageCopy buffer_image_copy = {
         .bufferOffset = buf_offset_bytes,
         .bufferRowLength = region->bufferRowLength / block_width,
         .bufferImageHeight = region->bufferImageHeight / block_height,
         .imageSubresource = {
            .aspectMask = aspect,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .imageOffset = { 0, 0, 0 },
         .imageExtent = { buf_width, buf_height, 1 }
      };
      handled =
         create_tiled_image_from_buffer(cmd_buffer,
                                        v3dv_image_from_handle(buffer_image),
                                        buffer, &buffer_image_copy);
      if (!handled) {
         /* This is unexpected, we should have setup the upload to be
          * conformant to a TFU or TLB copy.
          */
         unreachable("Unable to copy buffer to image through TLB");
         return false;
      }

      /* Blit-copy the requested image extent from the buffer image to the
       * destination image.
       *
       * Since we are copying, the blit must use the same format on the
       * destination and source images to avoid format conversions. The
       * only exception is copying stencil, which we upload to a R8UI source
       * image, but that we need to blit to a S8D24 destination (the only
       * stencil format we support).
       */
      const VkImageBlit blit_region = {
         .srcSubresource = {
            .aspectMask = aspect,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
         },
         .srcOffsets = {
            { 0, 0, 0 },
            { region->imageExtent.width, region->imageExtent.height, 1 },
         },
         .dstSubresource = {
            .aspectMask = aspect,
            .mipLevel = region->imageSubresource.mipLevel,
            .baseArrayLayer = region->imageSubresource.baseArrayLayer + i,
            .layerCount = 1,
         },
         .dstOffsets = {
            {
               DIV_ROUND_UP(region->imageOffset.x, block_width),
               DIV_ROUND_UP(region->imageOffset.y, block_height),
               region->imageOffset.z + i,
            },
            {
               DIV_ROUND_UP(region->imageOffset.x + region->imageExtent.width,
                            block_width),
               DIV_ROUND_UP(region->imageOffset.y + region->imageExtent.height,
                            block_height),
               region->imageOffset.z + i + 1,
            },
         },
      };

      handled = blit_shader(cmd_buffer,
                            image, dst_format,
                            v3dv_image_from_handle(buffer_image), src_format,
                            cmask, NULL,
                            &blit_region, VK_FILTER_NEAREST, true);
      if (!handled) {
         /* This is unexpected, we should have a supported blit spec */
         unreachable("Unable to blit buffer to destination image");
         return false;
      }
   }

   return handled;
}

/**
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 */
static bool
copy_buffer_to_image_shader(struct v3dv_cmd_buffer *cmd_buffer,
                            struct v3dv_image *image,
                            struct v3dv_buffer *buffer,
                            uint32_t region_count,
                            const VkBufferImageCopy *regions,
                            bool use_texel_buffer)
{
   /* FIXME: we only support batching on the texel buffer path for now */
   assert(region_count == 1 || use_texel_buffer);

   /* We can only call this with region_count > 1 if we can batch the regions
    * together, in which case they share the same image subresource, and so
    * the same aspect.
    */
   VkImageAspectFlags aspect = regions[0].imageSubresource.aspectMask;

   /* Generally, the bpp of the data in the buffer matches that of the
    * destination image. The exception is the case where we are uploading
    * stencil (8bpp) to a combined d24s8 image (32bpp).
    */
   uint32_t buf_bpp = image->cpp;

   /* We are about to upload the buffer data to an image so we can then
    * blit that to our destination region. Because we are going to implement
    * the copy as a blit, we want our blit source and destination formats to be
    * the same (to avoid any format conversions), so we choose a canonical
    * format that matches the destination image bpp.
    */
   VkColorComponentFlags cmask = 0; /* Write all components */
   VkFormat src_format;
   VkFormat dst_format;
   switch (buf_bpp) {
   case 16:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      src_format = VK_FORMAT_R32G32B32A32_UINT;
      dst_format = src_format;
      break;
   case 8:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      src_format = VK_FORMAT_R16G16B16A16_UINT;
      dst_format = src_format;
      break;
   case 4:
      switch (aspect) {
      case VK_IMAGE_ASPECT_COLOR_BIT:
         src_format = VK_FORMAT_R8G8B8A8_UINT;
         dst_format = src_format;
         break;
      case VK_IMAGE_ASPECT_DEPTH_BIT:
         assert(image->vk_format == VK_FORMAT_D32_SFLOAT ||
                image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT ||
                image->vk_format == VK_FORMAT_X8_D24_UNORM_PACK32);
         if (image->tiling != VK_IMAGE_TILING_LINEAR) {
            src_format = image->vk_format;
         } else {
            src_format = VK_FORMAT_R8G8B8A8_UINT;
            aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            if (image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
               cmask = VK_COLOR_COMPONENT_R_BIT |
                       VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT;
            }
         }
         dst_format = src_format;
         break;
      case VK_IMAGE_ASPECT_STENCIL_BIT:
         /* Since we don't support separate stencil this is always a stencil
          * copy to a combined depth/stencil image. Becasue we don't support
          * separate stencil images, we upload the buffer data to a compatible
          * color R8UI image, and implement the blit as a compatible color
          * blit to an RGBA8UI destination masking out writes to components
          * GBA (which map to the D24 component of a S8D24 image).
          */
         assert(image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT);
         buf_bpp = 1;
         src_format = VK_FORMAT_R8_UINT;
         dst_format = VK_FORMAT_R8G8B8A8_UINT;
         cmask = VK_COLOR_COMPONENT_R_BIT;
         aspect = VK_IMAGE_ASPECT_COLOR_BIT;
         break;
      default:
         unreachable("unsupported aspect");
         return false;
      };
      break;
   case 2:
      aspect = VK_IMAGE_ASPECT_COLOR_BIT;
      src_format = VK_FORMAT_R16_UINT;
      dst_format = src_format;
      break;
   case 1:
      assert(aspect == VK_IMAGE_ASPECT_COLOR_BIT);
      src_format = VK_FORMAT_R8_UINT;
      dst_format = src_format;
      break;
   default:
      unreachable("unsupported bit-size");
      return false;
   }

   if (use_texel_buffer) {
      return texel_buffer_shader_copy(cmd_buffer, aspect, image,
                                      dst_format, src_format,
                                      buffer, buf_bpp,
                                      cmask, region_count, regions);
   } else {
      bool handled = true;
      for (uint32_t i = 0; i < region_count; i++) {
         handled = copy_buffer_to_image_blit(cmd_buffer, aspect, image,
                                             dst_format, src_format,
                                             buffer, buf_bpp,
                                             cmask, &regions[i]);
         if (!handled)
            break;
      }
      return handled;
   }
}

/**
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 */
static bool
copy_buffer_to_image_cpu(struct v3dv_cmd_buffer *cmd_buffer,
                         struct v3dv_image *image,
                         struct v3dv_buffer *buffer,
                         const VkBufferImageCopy *region)
{
   /* FIXME */
   if (vk_format_is_depth_or_stencil(image->vk_format))
      return false;

   if (vk_format_is_compressed(image->vk_format))
      return false;

   if (image->tiling == VK_IMAGE_TILING_LINEAR)
      return false;

   uint32_t buffer_width, buffer_height;
   if (region->bufferRowLength == 0)
      buffer_width = region->imageExtent.width;
   else
      buffer_width = region->bufferRowLength;

   if (region->bufferImageHeight == 0)
      buffer_height = region->imageExtent.height;
   else
      buffer_height = region->bufferImageHeight;

   uint32_t buffer_stride = buffer_width * image->cpp;
   uint32_t buffer_layer_stride = buffer_stride * buffer_height;

   uint32_t num_layers;
   if (image->type != VK_IMAGE_TYPE_3D)
      num_layers = region->imageSubresource.layerCount;
   else
      num_layers = region->imageExtent.depth;
   assert(num_layers > 0);

   struct v3dv_job *job =
      v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                     V3DV_JOB_TYPE_CPU_COPY_BUFFER_TO_IMAGE,
                                     cmd_buffer, -1);
   if (!job)
      return true;

   job->cpu.copy_buffer_to_image.image = image;
   job->cpu.copy_buffer_to_image.buffer = buffer;
   job->cpu.copy_buffer_to_image.buffer_stride = buffer_stride;
   job->cpu.copy_buffer_to_image.buffer_layer_stride = buffer_layer_stride;
   job->cpu.copy_buffer_to_image.buffer_offset = region->bufferOffset;
   job->cpu.copy_buffer_to_image.image_extent = region->imageExtent;
   job->cpu.copy_buffer_to_image.image_offset = region->imageOffset;
   job->cpu.copy_buffer_to_image.mip_level =
      region->imageSubresource.mipLevel;
   job->cpu.copy_buffer_to_image.base_layer =
      region->imageSubresource.baseArrayLayer;
   job->cpu.copy_buffer_to_image.layer_count = num_layers;

   list_addtail(&job->list_link, &cmd_buffer->jobs);

   return true;
}

void
v3dv_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                          VkBuffer srcBuffer,
                          VkImage dstImage,
                          VkImageLayout dstImageLayout,
                          uint32_t regionCount,
                          const VkBufferImageCopy *pRegions)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_buffer, buffer, srcBuffer);
   V3DV_FROM_HANDLE(v3dv_image, image, dstImage);

   assert(image->samples == VK_SAMPLE_COUNT_1_BIT);

   uint32_t r = 0;

   while (r < regionCount) {
      /* The TFU and TLB paths can only copy one region at a time and the region
       * needs to start at the origin. We try these first for the common case
       * where we are copying full images, since they should be the fastest.
       */
      uint32_t batch_size = 1;
      if (copy_buffer_to_image_tfu(cmd_buffer, image, buffer, &pRegions[r]))
         goto handled;

      if (copy_buffer_to_image_tlb(cmd_buffer, image, buffer, &pRegions[r]))
         goto handled;

      /* Otherwise, we are copying subrects, so we fallback to copying
       * via shader and texel buffers and we try to batch the regions
       * if possible. We can only batch copies if they target the same
       * image subresource (so they have the same framebuffer spec).
       */
      const VkImageSubresourceLayers *rsc = &pRegions[r].imageSubresource;
      if (image->type != VK_IMAGE_TYPE_3D) {
         for (uint32_t s = r + 1; s < regionCount; s++) {
            const VkImageSubresourceLayers *rsc_s = &pRegions[s].imageSubresource;
            if (memcmp(rsc, rsc_s, sizeof(VkImageSubresourceLayers)) != 0)
               break;
            batch_size++;
         }
      }

      if (copy_buffer_to_image_shader(cmd_buffer, image, buffer,
                                      batch_size, &pRegions[r], true)) {
         goto handled;
      }

      /* If we still could not copy, fallback to slower paths.
       *
       * FIXME: we could try to batch these too, but since they are bound to be
       * slow it might not be worth it and we should instead put more effort
       * in handling more cases with the other paths.
       */
      batch_size = 1;

      if (copy_buffer_to_image_cpu(cmd_buffer, image, buffer, &pRegions[r]))
         goto handled;

      if (copy_buffer_to_image_shader(cmd_buffer, image, buffer,
                                      1, &pRegions[r], false)) {
         goto handled;
      }

      unreachable("Unsupported buffer to image copy.");

handled:
      r += batch_size;
   }
}

static void
compute_blit_3d_layers(const VkOffset3D *offsets,
                       uint32_t *min_layer, uint32_t *max_layer,
                       bool *mirror_z);

/**
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 *
 * The TFU blit path doesn't handle scaling so the blit filter parameter can
 * be ignored.
 */
static bool
blit_tfu(struct v3dv_cmd_buffer *cmd_buffer,
         struct v3dv_image *dst,
         struct v3dv_image *src,
         const VkImageBlit *region)
{
   assert(dst->samples == VK_SAMPLE_COUNT_1_BIT);
   assert(src->samples == VK_SAMPLE_COUNT_1_BIT);

   /* Format must match */
   if (src->vk_format != dst->vk_format)
      return false;

   /* Destination can't be raster format */
   if (dst->tiling == VK_IMAGE_TILING_LINEAR)
      return false;

   /* Source region must start at (0,0) */
   if (region->srcOffsets[0].x != 0 || region->srcOffsets[0].y != 0)
      return false;

   /* Destination image must be complete */
   if (region->dstOffsets[0].x != 0 || region->dstOffsets[0].y != 0)
      return false;

   const uint32_t dst_mip_level = region->dstSubresource.mipLevel;
   const uint32_t dst_width = u_minify(dst->extent.width, dst_mip_level);
   const uint32_t dst_height = u_minify(dst->extent.height, dst_mip_level);
   if (region->dstOffsets[1].x < dst_width - 1||
       region->dstOffsets[1].y < dst_height - 1) {
      return false;
   }

   /* No XY scaling */
   if (region->srcOffsets[1].x != region->dstOffsets[1].x ||
       region->srcOffsets[1].y != region->dstOffsets[1].y) {
      return false;
   }

   /* If the format is D24S8 both aspects need to be copied, since the TFU
    * can't be programmed to copy only one aspect of the image.
    */
   if (dst->vk_format == VK_FORMAT_D24_UNORM_S8_UINT) {
       const VkImageAspectFlags ds_aspects = VK_IMAGE_ASPECT_DEPTH_BIT |
                                             VK_IMAGE_ASPECT_STENCIL_BIT;
       if (region->dstSubresource.aspectMask != ds_aspects)
          return false;
   }

   /* Our TFU blits only handle exact copies (it requires same formats
    * on input and output, no scaling, etc), so there is no pixel format
    * conversions and we can rewrite the format to use one that is TFU
    * compatible based on its texel size.
    */
   const struct v3dv_format *format =
      v3dv_get_compatible_tfu_format(&cmd_buffer->device->devinfo,
                                     dst->cpp, NULL);

   /* Emit a TFU job for each layer to blit */
   assert(region->dstSubresource.layerCount ==
          region->srcSubresource.layerCount);

   uint32_t min_dst_layer;
   uint32_t max_dst_layer;
   bool dst_mirror_z = false;
   if (dst->type == VK_IMAGE_TYPE_3D) {
      compute_blit_3d_layers(region->dstOffsets,
                             &min_dst_layer, &max_dst_layer,
                             &dst_mirror_z);
   } else {
      min_dst_layer = region->dstSubresource.baseArrayLayer;
      max_dst_layer = min_dst_layer + region->dstSubresource.layerCount;
   }

   uint32_t min_src_layer;
   uint32_t max_src_layer;
   bool src_mirror_z = false;
   if (src->type == VK_IMAGE_TYPE_3D) {
      compute_blit_3d_layers(region->srcOffsets,
                             &min_src_layer, &max_src_layer,
                             &src_mirror_z);
   } else {
      min_src_layer = region->srcSubresource.baseArrayLayer;
      max_src_layer = min_src_layer + region->srcSubresource.layerCount;
   }

   /* No Z scaling for 3D images (for non-3D images both src and dst must
    * have the same layerCount).
    */
   if (max_dst_layer - min_dst_layer != max_src_layer - min_src_layer)
      return false;

   const uint32_t layer_count = max_dst_layer - min_dst_layer;
   const uint32_t src_mip_level = region->srcSubresource.mipLevel;
   for (uint32_t i = 0; i < layer_count; i++) {
      /* Since the TFU path doesn't handle scaling, Z mirroring for 3D images
       * only involves reversing the order of the slices.
       */
      const uint32_t dst_layer =
         dst_mirror_z ? max_dst_layer - i - 1: min_dst_layer + i;
      const uint32_t src_layer =
         src_mirror_z ? max_src_layer - i - 1: min_src_layer + i;
      emit_tfu_job(cmd_buffer,
                   dst, dst_mip_level, dst_layer,
                   src, src_mip_level, src_layer,
                   dst_width, dst_height, format);
   }

   return true;
}

static bool
format_needs_software_int_clamp(VkFormat format)
{
   switch (format) {
      case VK_FORMAT_A2R10G10B10_UINT_PACK32:
      case VK_FORMAT_A2R10G10B10_SINT_PACK32:
      case VK_FORMAT_A2B10G10R10_UINT_PACK32:
      case VK_FORMAT_A2B10G10R10_SINT_PACK32:
         return true;
      default:
         return false;
   };
}

static void
get_blit_pipeline_cache_key(VkFormat dst_format,
                            VkFormat src_format,
                            VkColorComponentFlags cmask,
                            VkSampleCountFlagBits dst_samples,
                            VkSampleCountFlagBits src_samples,
                            uint8_t *key)
{
   memset(key, 0, V3DV_META_BLIT_CACHE_KEY_SIZE);

   uint32_t *p = (uint32_t *) key;

   *p = dst_format;
   p++;

   /* Generally, when blitting from a larger format to a smaller format
    * the hardware takes care of clamping the source to the RT range.
    * Specifically, for integer formats, this is done by using
    * V3D_RENDER_TARGET_CLAMP_INT in the render target setup, however, this
    * clamps to the bit-size of the render type, and some formats, such as
    * rgb10a2_uint have a 16-bit type, so it won't do what we need and we
    * require to clamp in software. In these cases, we need to amend the blit
    * shader with clamp code that depends on both the src and dst formats, so
    * we need the src format to be part of the key.
    */
   *p = format_needs_software_int_clamp(dst_format) ? src_format : 0;
   p++;

   *p = cmask;
   p++;

   *p = (dst_samples << 8) | src_samples;
   p++;

   assert(((uint8_t*)p - key) == V3DV_META_BLIT_CACHE_KEY_SIZE);
}

static bool
create_blit_render_pass(struct v3dv_device *device,
                        VkFormat dst_format,
                        VkFormat src_format,
                        VkRenderPass *pass_load,
                        VkRenderPass *pass_no_load)
{
   const bool is_color_blit = vk_format_is_color(dst_format);

   /* Attachment load operation is specified below */
   VkAttachmentDescription att = {
      .format = dst_format,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
      .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
   };

   VkAttachmentReference att_ref = {
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_GENERAL,
   };

   VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .inputAttachmentCount = 0,
      .colorAttachmentCount = is_color_blit ? 1 : 0,
      .pColorAttachments = is_color_blit ? &att_ref : NULL,
      .pResolveAttachments = NULL,
      .pDepthStencilAttachment = is_color_blit ? NULL : &att_ref,
      .preserveAttachmentCount = 0,
      .pPreserveAttachments = NULL,
   };

   VkRenderPassCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &att,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 0,
      .pDependencies = NULL,
   };

   VkResult result;
   att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
   result = v3dv_CreateRenderPass(v3dv_device_to_handle(device),
                                  &info, &device->vk.alloc, pass_load);
   if (result != VK_SUCCESS)
      return false;

   att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   result = v3dv_CreateRenderPass(v3dv_device_to_handle(device),
                                  &info, &device->vk.alloc, pass_no_load);
   return result == VK_SUCCESS;
}

static nir_ssa_def *
gen_rect_vertices(nir_builder *b)
{
   nir_intrinsic_instr *vertex_id =
      nir_intrinsic_instr_create(b->shader,
                                 nir_intrinsic_load_vertex_id);
   nir_ssa_dest_init(&vertex_id->instr, &vertex_id->dest, 1, 32, "vertexid");
   nir_builder_instr_insert(b, &vertex_id->instr);


   /* vertex 0: -1.0, -1.0
    * vertex 1: -1.0,  1.0
    * vertex 2:  1.0, -1.0
    * vertex 3:  1.0,  1.0
    *
    * so:
    *
    * channel 0 is vertex_id < 2 ? -1.0 :  1.0
    * channel 1 is vertex id & 1 ?  1.0 : -1.0
    */

   nir_ssa_def *one = nir_imm_int(b, 1);
   nir_ssa_def *c0cmp = nir_ilt(b, &vertex_id->dest.ssa, nir_imm_int(b, 2));
   nir_ssa_def *c1cmp = nir_ieq(b, nir_iand(b, &vertex_id->dest.ssa, one), one);

   nir_ssa_def *comp[4];
   comp[0] = nir_bcsel(b, c0cmp,
                       nir_imm_float(b, -1.0f),
                       nir_imm_float(b, 1.0f));

   comp[1] = nir_bcsel(b, c1cmp,
                       nir_imm_float(b, 1.0f),
                       nir_imm_float(b, -1.0f));
   comp[2] = nir_imm_float(b, 0.0f);
   comp[3] = nir_imm_float(b, 1.0f);
   return nir_vec(b, comp, 4);
}

static nir_ssa_def *
gen_tex_coords(nir_builder *b)
{
   nir_intrinsic_instr *tex_box =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_push_constant);
   tex_box->src[0] = nir_src_for_ssa(nir_imm_int(b, 0));
   nir_intrinsic_set_base(tex_box, 0);
   nir_intrinsic_set_range(tex_box, 16);
   tex_box->num_components = 4;
   nir_ssa_dest_init(&tex_box->instr, &tex_box->dest, 4, 32, "tex_box");
   nir_builder_instr_insert(b, &tex_box->instr);

   nir_intrinsic_instr *tex_z =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_push_constant);
   tex_z->src[0] = nir_src_for_ssa(nir_imm_int(b, 0));
   nir_intrinsic_set_base(tex_z, 16);
   nir_intrinsic_set_range(tex_z, 4);
   tex_z->num_components = 1;
   nir_ssa_dest_init(&tex_z->instr, &tex_z->dest, 1, 32, "tex_z");
   nir_builder_instr_insert(b, &tex_z->instr);

   nir_intrinsic_instr *vertex_id =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_vertex_id);
   nir_ssa_dest_init(&vertex_id->instr, &vertex_id->dest, 1, 32, "vertexid");
   nir_builder_instr_insert(b, &vertex_id->instr);

   /* vertex 0: src0_x, src0_y
    * vertex 1: src0_x, src1_y
    * vertex 2: src1_x, src0_y
    * vertex 3: src1_x, src1_y
    *
    * So:
    *
    * channel 0 is vertex_id < 2 ? src0_x : src1_x
    * channel 1 is vertex id & 1 ? src1_y : src0_y
    */

   nir_ssa_def *one = nir_imm_int(b, 1);
   nir_ssa_def *c0cmp = nir_ilt(b, &vertex_id->dest.ssa, nir_imm_int(b, 2));
   nir_ssa_def *c1cmp = nir_ieq(b, nir_iand(b, &vertex_id->dest.ssa, one), one);

   nir_ssa_def *comp[4];
   comp[0] = nir_bcsel(b, c0cmp,
                       nir_channel(b, &tex_box->dest.ssa, 0),
                       nir_channel(b, &tex_box->dest.ssa, 2));

   comp[1] = nir_bcsel(b, c1cmp,
                       nir_channel(b, &tex_box->dest.ssa, 3),
                       nir_channel(b, &tex_box->dest.ssa, 1));
   comp[2] = &tex_z->dest.ssa;
   comp[3] = nir_imm_float(b, 1.0f);
   return nir_vec(b, comp, 4);
}

static nir_ssa_def *
build_nir_tex_op_read(struct nir_builder *b,
                      nir_ssa_def *tex_pos,
                      enum glsl_base_type tex_type,
                      enum glsl_sampler_dim dim)
{
   assert(dim != GLSL_SAMPLER_DIM_MS);

   const struct glsl_type *sampler_type =
      glsl_sampler_type(dim, false, false, tex_type);
   nir_variable *sampler =
      nir_variable_create(b->shader, nir_var_uniform, sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   nir_ssa_def *tex_deref = &nir_build_deref_var(b, sampler)->dest.ssa;
   nir_tex_instr *tex = nir_tex_instr_create(b->shader, 3);
   tex->sampler_dim = dim;
   tex->op = nir_texop_tex;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(tex_pos);
   tex->src[1].src_type = nir_tex_src_texture_deref;
   tex->src[1].src = nir_src_for_ssa(tex_deref);
   tex->src[2].src_type = nir_tex_src_sampler_deref;
   tex->src[2].src = nir_src_for_ssa(tex_deref);
   tex->dest_type =
      nir_alu_type_get_base_type(nir_get_nir_type_for_glsl_base_type(tex_type));
   tex->is_array = glsl_sampler_type_is_array(sampler_type);
   tex->coord_components = tex_pos->num_components;

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
   nir_builder_instr_insert(b, &tex->instr);
   return &tex->dest.ssa;
}

static nir_ssa_def *
build_nir_tex_op_ms_fetch_sample(struct nir_builder *b,
                                 nir_variable *sampler,
                                 nir_ssa_def *tex_deref,
                                 enum glsl_base_type tex_type,
                                 nir_ssa_def *tex_pos,
                                 nir_ssa_def *sample_idx)
{
   nir_tex_instr *tex = nir_tex_instr_create(b->shader, 4);
   tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
   tex->op = nir_texop_txf_ms;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(tex_pos);
   tex->src[1].src_type = nir_tex_src_texture_deref;
   tex->src[1].src = nir_src_for_ssa(tex_deref);
   tex->src[2].src_type = nir_tex_src_sampler_deref;
   tex->src[2].src = nir_src_for_ssa(tex_deref);
   tex->src[3].src_type = nir_tex_src_ms_index;
   tex->src[3].src = nir_src_for_ssa(sample_idx);
   tex->dest_type =
      nir_alu_type_get_base_type(nir_get_nir_type_for_glsl_base_type(tex_type));
   tex->is_array = false;
   tex->coord_components = tex_pos->num_components;

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
   nir_builder_instr_insert(b, &tex->instr);
   return &tex->dest.ssa;
}

/* Fetches all samples at the given position and averages them */
static nir_ssa_def *
build_nir_tex_op_ms_resolve(struct nir_builder *b,
                            nir_ssa_def *tex_pos,
                            enum glsl_base_type tex_type,
                            VkSampleCountFlagBits src_samples)
{
   assert(src_samples > VK_SAMPLE_COUNT_1_BIT);
   const struct glsl_type *sampler_type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_MS, false, false, tex_type);
   nir_variable *sampler =
      nir_variable_create(b->shader, nir_var_uniform, sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   const bool is_int = glsl_base_type_is_integer(tex_type);

   nir_ssa_def *tmp;
   nir_ssa_def *tex_deref = &nir_build_deref_var(b, sampler)->dest.ssa;
   for (uint32_t i = 0; i < src_samples; i++) {
      nir_ssa_def *s =
         build_nir_tex_op_ms_fetch_sample(b, sampler, tex_deref,
                                          tex_type, tex_pos,
                                          nir_imm_int(b, i));

      /* For integer formats, the multisample resolve operation is expected to
       * return one of the samples, we just return the first one.
       */
      if (is_int)
         return s;

      tmp = i == 0 ? s : nir_fadd(b, tmp, s);
   }

   assert(!is_int);
   return nir_fmul(b, tmp, nir_imm_float(b, 1.0f / src_samples));
}

/* Fetches the current sample (gl_SampleID) at the given position */
static nir_ssa_def *
build_nir_tex_op_ms_read(struct nir_builder *b,
                         nir_ssa_def *tex_pos,
                         enum glsl_base_type tex_type)
{
   const struct glsl_type *sampler_type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_MS, false, false, tex_type);
   nir_variable *sampler =
      nir_variable_create(b->shader, nir_var_uniform, sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   nir_ssa_def *tex_deref = &nir_build_deref_var(b, sampler)->dest.ssa;

   return build_nir_tex_op_ms_fetch_sample(b, sampler, tex_deref,
                                           tex_type, tex_pos,
                                           nir_load_sample_id(b));
}

static nir_ssa_def *
build_nir_tex_op(struct nir_builder *b,
                 struct v3dv_device *device,
                 nir_ssa_def *tex_pos,
                 enum glsl_base_type tex_type,
                 VkSampleCountFlagBits dst_samples,
                 VkSampleCountFlagBits src_samples,
                 enum glsl_sampler_dim dim)
{
   switch (dim) {
   case GLSL_SAMPLER_DIM_MS:
      assert(src_samples == VK_SAMPLE_COUNT_4_BIT);
      /* For multisampled texture sources we need to use fetching instead of
       * normalized texture coordinates. We already configured our blit
       * coordinates to be in texel units, but here we still need to convert
       * them from floating point to integer.
       */
      tex_pos = nir_f2i32(b, tex_pos);

      if (dst_samples == VK_SAMPLE_COUNT_1_BIT)
         return build_nir_tex_op_ms_resolve(b, tex_pos, tex_type, src_samples);
      else
         return build_nir_tex_op_ms_read(b, tex_pos, tex_type);
   default:
      assert(src_samples == VK_SAMPLE_COUNT_1_BIT);
      return build_nir_tex_op_read(b, tex_pos, tex_type, dim);
   }
}

static nir_shader *
get_blit_vs()
{
   const nir_shader_compiler_options *options = v3dv_pipeline_get_nir_options();
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, options,
                                                  "meta blit vs");

   const struct glsl_type *vec4 = glsl_vec4_type();

   nir_variable *vs_out_pos =
      nir_variable_create(b.shader, nir_var_shader_out, vec4, "gl_Position");
   vs_out_pos->data.location = VARYING_SLOT_POS;

   nir_variable *vs_out_tex_coord =
      nir_variable_create(b.shader, nir_var_shader_out, vec4, "out_tex_coord");
   vs_out_tex_coord->data.location = VARYING_SLOT_VAR0;
   vs_out_tex_coord->data.interpolation = INTERP_MODE_SMOOTH;

   nir_ssa_def *pos = gen_rect_vertices(&b);
   nir_store_var(&b, vs_out_pos, pos, 0xf);

   nir_ssa_def *tex_coord = gen_tex_coords(&b);
   nir_store_var(&b, vs_out_tex_coord, tex_coord, 0xf);

   return b.shader;
}

static uint32_t
get_channel_mask_for_sampler_dim(enum glsl_sampler_dim sampler_dim)
{
   switch (sampler_dim) {
   case GLSL_SAMPLER_DIM_1D: return 0x1;
   case GLSL_SAMPLER_DIM_2D: return 0x3;
   case GLSL_SAMPLER_DIM_MS: return 0x3;
   case GLSL_SAMPLER_DIM_3D: return 0x7;
   default:
      unreachable("invalid sampler dim");
   };
}

static nir_shader *
get_color_blit_fs(struct v3dv_device *device,
                  VkFormat dst_format,
                  VkFormat src_format,
                  VkSampleCountFlagBits dst_samples,
                  VkSampleCountFlagBits src_samples,
                  enum glsl_sampler_dim sampler_dim)
{
   const nir_shader_compiler_options *options = v3dv_pipeline_get_nir_options();
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, options,
                                                  "meta blit fs");

   const struct glsl_type *vec4 = glsl_vec4_type();

   nir_variable *fs_in_tex_coord =
      nir_variable_create(b.shader, nir_var_shader_in, vec4, "in_tex_coord");
   fs_in_tex_coord->data.location = VARYING_SLOT_VAR0;

   const struct glsl_type *fs_out_type =
      vk_format_is_sint(dst_format) ? glsl_ivec4_type() :
      vk_format_is_uint(dst_format) ? glsl_uvec4_type() :
                                      glsl_vec4_type();

   enum glsl_base_type src_base_type =
      vk_format_is_sint(src_format) ? GLSL_TYPE_INT :
      vk_format_is_uint(src_format) ? GLSL_TYPE_UINT :
                                      GLSL_TYPE_FLOAT;

   nir_variable *fs_out_color =
      nir_variable_create(b.shader, nir_var_shader_out, fs_out_type, "out_color");
   fs_out_color->data.location = FRAG_RESULT_DATA0;

   nir_ssa_def *tex_coord = nir_load_var(&b, fs_in_tex_coord);
   const uint32_t channel_mask = get_channel_mask_for_sampler_dim(sampler_dim);
   tex_coord = nir_channels(&b, tex_coord, channel_mask);

   nir_ssa_def *color = build_nir_tex_op(&b, device, tex_coord, src_base_type,
                                         dst_samples, src_samples, sampler_dim);

   /* For integer textures, if the bit-size of the destination is too small to
    * hold source value, Vulkan (CTS) expects the implementation to clamp to the
    * maximum value the destination can hold. The hardware can clamp to the
    * render target type, which usually matches the component bit-size, but
    * there are some cases that won't match, such as rgb10a2, which has a 16-bit
    * render target type, so in these cases we need to clamp manually.
    */
   if (format_needs_software_int_clamp(dst_format)) {
      assert(vk_format_is_int(dst_format));
      enum pipe_format src_pformat = vk_format_to_pipe_format(src_format);
      enum pipe_format dst_pformat = vk_format_to_pipe_format(dst_format);

      nir_ssa_def *c[4];
      for (uint32_t i = 0; i < 4; i++) {
         c[i] = nir_channel(&b, color, i);

         const uint32_t src_bit_size =
            util_format_get_component_bits(src_pformat,
                                           UTIL_FORMAT_COLORSPACE_RGB,
                                           i);
         const uint32_t dst_bit_size =
            util_format_get_component_bits(dst_pformat,
                                           UTIL_FORMAT_COLORSPACE_RGB,
                                           i);

         if (dst_bit_size >= src_bit_size)
            continue;

         if (util_format_is_pure_uint(dst_pformat)) {
            nir_ssa_def *max = nir_imm_int(&b, (1 << dst_bit_size) - 1);
            c[i] = nir_umin(&b, c[i], max);
         } else {
            nir_ssa_def *max = nir_imm_int(&b, (1 << (dst_bit_size - 1)) - 1);
            nir_ssa_def *min = nir_imm_int(&b, -(1 << (dst_bit_size - 1)));
            c[i] = nir_imax(&b, nir_imin(&b, c[i], max), min);
         }
      }

      color = nir_vec4(&b, c[0], c[1], c[2], c[3]);
   }

   nir_store_var(&b, fs_out_color, color, 0xf);

   return b.shader;
}

static bool
create_pipeline(struct v3dv_device *device,
                struct v3dv_render_pass *pass,
                struct nir_shader *vs_nir,
                struct nir_shader *fs_nir,
                const VkPipelineVertexInputStateCreateInfo *vi_state,
                const VkPipelineDepthStencilStateCreateInfo *ds_state,
                const VkPipelineColorBlendStateCreateInfo *cb_state,
                const VkPipelineMultisampleStateCreateInfo *ms_state,
                const VkPipelineLayout layout,
                VkPipeline *pipeline)
{
   struct v3dv_shader_module vs_m;
   struct v3dv_shader_module fs_m;

   v3dv_shader_module_internal_init(&vs_m, vs_nir);
   v3dv_shader_module_internal_init(&fs_m, fs_nir);

   VkPipelineShaderStageCreateInfo stages[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = v3dv_shader_module_to_handle(&vs_m),
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = v3dv_shader_module_to_handle(&fs_m),
         .pName = "main",
      },
   };

   VkGraphicsPipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,

      .stageCount = 2,
      .pStages = stages,

      .pVertexInputState = vi_state,

      .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
         .primitiveRestartEnable = false,
      },

      .pViewportState = &(VkPipelineViewportStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
         .viewportCount = 1,
         .scissorCount = 1,
      },

      .pRasterizationState = &(VkPipelineRasterizationStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
         .rasterizerDiscardEnable = false,
         .polygonMode = VK_POLYGON_MODE_FILL,
         .cullMode = VK_CULL_MODE_NONE,
         .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
         .depthBiasEnable = false,
      },

      .pMultisampleState = ms_state,

      .pDepthStencilState = ds_state,

      .pColorBlendState = cb_state,

      /* The meta clear pipeline declares all state as dynamic.
       * As a consequence, vkCmdBindPipeline writes no dynamic state
       * to the cmd buffer. Therefore, at the end of the meta clear,
       * we need only restore dynamic state that was vkCmdSet.
       */
      .pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
         .dynamicStateCount = 6,
         .pDynamicStates = (VkDynamicState[]) {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_LINE_WIDTH,
         },
      },

      .flags = 0,
      .layout = layout,
      .renderPass = v3dv_render_pass_to_handle(pass),
      .subpass = 0,
   };

   VkResult result =
      v3dv_CreateGraphicsPipelines(v3dv_device_to_handle(device),
                                   VK_NULL_HANDLE,
                                   1, &info,
                                   &device->vk.alloc,
                                   pipeline);

   ralloc_free(vs_nir);
   ralloc_free(fs_nir);

   return result == VK_SUCCESS;
}

static enum glsl_sampler_dim
get_sampler_dim(VkImageType type, VkSampleCountFlagBits src_samples)
{
   /* From the Vulkan 1.0 spec, VkImageCreateInfo Validu Usage:
    *
    *   "If samples is not VK_SAMPLE_COUNT_1_BIT, then imageType must be
    *    VK_IMAGE_TYPE_2D, ..."
    */
   assert(src_samples == VK_SAMPLE_COUNT_1_BIT || type == VK_IMAGE_TYPE_2D);

   switch (type) {
   case VK_IMAGE_TYPE_1D: return GLSL_SAMPLER_DIM_1D;
   case VK_IMAGE_TYPE_2D:
      return src_samples == VK_SAMPLE_COUNT_1_BIT ? GLSL_SAMPLER_DIM_2D :
                                                    GLSL_SAMPLER_DIM_MS;
   case VK_IMAGE_TYPE_3D: return GLSL_SAMPLER_DIM_3D;
   default:
      unreachable("Invalid image type");
   }
}

static bool
create_blit_pipeline(struct v3dv_device *device,
                     VkFormat dst_format,
                     VkFormat src_format,
                     VkColorComponentFlags cmask,
                     VkImageType src_type,
                     VkSampleCountFlagBits dst_samples,
                     VkSampleCountFlagBits src_samples,
                     VkRenderPass _pass,
                     VkPipelineLayout pipeline_layout,
                     VkPipeline *pipeline)
{
   struct v3dv_render_pass *pass = v3dv_render_pass_from_handle(_pass);

   /* We always rewrite depth/stencil blits to compatible color blits */
   assert(vk_format_is_color(dst_format));
   assert(vk_format_is_color(src_format));

   const enum glsl_sampler_dim sampler_dim =
      get_sampler_dim(src_type, src_samples);

   nir_shader *vs_nir = get_blit_vs();
   nir_shader *fs_nir =
      get_color_blit_fs(device, dst_format, src_format,
                        dst_samples, src_samples, sampler_dim);

   const VkPipelineVertexInputStateCreateInfo vi_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 0,
      .vertexAttributeDescriptionCount = 0,
   };

   VkPipelineDepthStencilStateCreateInfo ds_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
   };

   VkPipelineColorBlendAttachmentState blend_att_state[1] = { 0 };
   blend_att_state[0] = (VkPipelineColorBlendAttachmentState) {
      .blendEnable = false,
      .colorWriteMask = cmask,
   };

   const VkPipelineColorBlendStateCreateInfo cb_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = false,
      .attachmentCount = 1,
      .pAttachments = blend_att_state
   };

   const VkPipelineMultisampleStateCreateInfo ms_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = dst_samples,
      .sampleShadingEnable = dst_samples > VK_SAMPLE_COUNT_1_BIT,
      .pSampleMask = NULL,
      .alphaToCoverageEnable = false,
      .alphaToOneEnable = false,
   };

   return create_pipeline(device,
                          pass,
                          vs_nir, fs_nir,
                          &vi_state,
                          &ds_state,
                          &cb_state,
                          &ms_state,
                          pipeline_layout,
                          pipeline);
}

/**
 * Return a pipeline suitable for blitting the requested aspect given the
 * destination and source formats.
 */
static bool
get_blit_pipeline(struct v3dv_device *device,
                  VkFormat dst_format,
                  VkFormat src_format,
                  VkColorComponentFlags cmask,
                  VkImageType src_type,
                  VkSampleCountFlagBits dst_samples,
                  VkSampleCountFlagBits src_samples,
                  struct v3dv_meta_blit_pipeline **pipeline)
{
   bool ok = true;

   uint8_t key[V3DV_META_BLIT_CACHE_KEY_SIZE];
   get_blit_pipeline_cache_key(dst_format, src_format, cmask,
                               dst_samples, src_samples, key);
   mtx_lock(&device->meta.mtx);
   struct hash_entry *entry =
      _mesa_hash_table_search(device->meta.blit.cache[src_type], &key);
   if (entry) {
      mtx_unlock(&device->meta.mtx);
      *pipeline = entry->data;
      return true;
   }

   *pipeline = vk_zalloc2(&device->vk.alloc, NULL, sizeof(**pipeline), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (*pipeline == NULL)
      goto fail;

   ok = create_blit_render_pass(device, dst_format, src_format,
                                &(*pipeline)->pass,
                                &(*pipeline)->pass_no_load);
   if (!ok)
      goto fail;

   /* Create the pipeline using one of the render passes, they are both
    * compatible, so we don't care which one we use here.
    */
   ok = create_blit_pipeline(device,
                             dst_format,
                             src_format,
                             cmask,
                             src_type,
                             dst_samples,
                             src_samples,
                             (*pipeline)->pass,
                             device->meta.blit.p_layout,
                             &(*pipeline)->pipeline);
   if (!ok)
      goto fail;

   memcpy((*pipeline)->key, key, sizeof((*pipeline)->key));
   _mesa_hash_table_insert(device->meta.blit.cache[src_type],
                           &(*pipeline)->key, *pipeline);

   mtx_unlock(&device->meta.mtx);
   return true;

fail:
   mtx_unlock(&device->meta.mtx);

   VkDevice _device = v3dv_device_to_handle(device);
   if (*pipeline) {
      if ((*pipeline)->pass)
         v3dv_DestroyRenderPass(_device, (*pipeline)->pass, &device->vk.alloc);
      if ((*pipeline)->pass_no_load)
         v3dv_DestroyRenderPass(_device, (*pipeline)->pass_no_load, &device->vk.alloc);
      if ((*pipeline)->pipeline)
         v3dv_DestroyPipeline(_device, (*pipeline)->pipeline, &device->vk.alloc);
      vk_free(&device->vk.alloc, *pipeline);
      *pipeline = NULL;
   }

   return false;
}

static void
compute_blit_box(const VkOffset3D *offsets,
                 uint32_t image_w, uint32_t image_h,
                 uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h,
                 bool *mirror_x, bool *mirror_y)
{
   if (offsets[1].x >= offsets[0].x) {
      *mirror_x = false;
      *x = MIN2(offsets[0].x, image_w - 1);
      *w = MIN2(offsets[1].x - offsets[0].x, image_w - offsets[0].x);
   } else {
      *mirror_x = true;
      *x = MIN2(offsets[1].x, image_w - 1);
      *w = MIN2(offsets[0].x - offsets[1].x, image_w - offsets[1].x);
   }
   if (offsets[1].y >= offsets[0].y) {
      *mirror_y = false;
      *y = MIN2(offsets[0].y, image_h - 1);
      *h = MIN2(offsets[1].y - offsets[0].y, image_h - offsets[0].y);
   } else {
      *mirror_y = true;
      *y = MIN2(offsets[1].y, image_h - 1);
      *h = MIN2(offsets[0].y - offsets[1].y, image_h - offsets[1].y);
   }
}

static void
compute_blit_3d_layers(const VkOffset3D *offsets,
                       uint32_t *min_layer, uint32_t *max_layer,
                       bool *mirror_z)
{
   if (offsets[1].z >= offsets[0].z) {
      *mirror_z = false;
      *min_layer = offsets[0].z;
      *max_layer = offsets[1].z;
   } else {
      *mirror_z = true;
      *min_layer = offsets[1].z;
      *max_layer = offsets[0].z;
   }
}

static VkResult
create_blit_descriptor_pool(struct v3dv_cmd_buffer *cmd_buffer)
{
   /* If this is not the first pool we create for this command buffer
    * size it based on the size of the currently exhausted pool.
    */
   uint32_t descriptor_count = 64;
   if (cmd_buffer->meta.blit.dspool != VK_NULL_HANDLE) {
      struct v3dv_descriptor_pool *exhausted_pool =
         v3dv_descriptor_pool_from_handle(cmd_buffer->meta.blit.dspool);
      descriptor_count = MIN2(exhausted_pool->max_entry_count * 2, 1024);
   }

   /* Create the descriptor pool */
   cmd_buffer->meta.blit.dspool = VK_NULL_HANDLE;
   VkDescriptorPoolSize pool_size = {
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = descriptor_count,
   };
   VkDescriptorPoolCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = descriptor_count,
      .poolSizeCount = 1,
      .pPoolSizes = &pool_size,
      .flags = 0,
   };
   VkResult result =
      v3dv_CreateDescriptorPool(v3dv_device_to_handle(cmd_buffer->device),
                                &info,
                                &cmd_buffer->device->vk.alloc,
                                &cmd_buffer->meta.blit.dspool);

   if (result == VK_SUCCESS) {
      assert(cmd_buffer->meta.blit.dspool != VK_NULL_HANDLE);
      const VkDescriptorPool _pool = cmd_buffer->meta.blit.dspool;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t) _pool,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyDescriptorPool);

      struct v3dv_descriptor_pool *pool =
         v3dv_descriptor_pool_from_handle(_pool);
      pool->is_driver_internal = true;
   }

   return result;
}

static VkResult
allocate_blit_source_descriptor_set(struct v3dv_cmd_buffer *cmd_buffer,
                                    VkDescriptorSet *set)
{
   /* Make sure we have a descriptor pool */
   VkResult result;
   if (cmd_buffer->meta.blit.dspool == VK_NULL_HANDLE) {
      result = create_blit_descriptor_pool(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;
   }
   assert(cmd_buffer->meta.blit.dspool != VK_NULL_HANDLE);

   /* Allocate descriptor set */
   struct v3dv_device *device = cmd_buffer->device;
   VkDevice _device = v3dv_device_to_handle(device);
   VkDescriptorSetAllocateInfo info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = cmd_buffer->meta.blit.dspool,
      .descriptorSetCount = 1,
      .pSetLayouts = &device->meta.blit.ds_layout,
   };
   result = v3dv_AllocateDescriptorSets(_device, &info, set);

   /* If we ran out of pool space, grow the pool and try again */
   if (result == VK_ERROR_OUT_OF_POOL_MEMORY) {
      result = create_blit_descriptor_pool(cmd_buffer);
      if (result == VK_SUCCESS) {
         info.descriptorPool = cmd_buffer->meta.blit.dspool;
         result = v3dv_AllocateDescriptorSets(_device, &info, set);
      }
   }

   return result;
}

/**
 * Returns true if the implementation supports the requested operation (even if
 * it failed to process it, for example, due to an out-of-memory error).
 *
 * The caller can specify the channels on the destination to be written via the
 * cmask parameter (which can be 0 to default to all channels), as well as a
 * swizzle to apply to the source via the cswizzle parameter  (which can be NULL
 * to use the default identity swizzle).
 */
static bool
blit_shader(struct v3dv_cmd_buffer *cmd_buffer,
            struct v3dv_image *dst,
            VkFormat dst_format,
            struct v3dv_image *src,
            VkFormat src_format,
            VkColorComponentFlags cmask,
            VkComponentMapping *cswizzle,
            const VkImageBlit *_region,
            VkFilter filter,
            bool dst_is_padded_image)
{
   bool handled = true;
   VkResult result;

   /* We don't support rendering to linear depth/stencil, this should have
    * been rewritten to a compatible color blit by the caller.
    */
   assert(dst->tiling != VK_IMAGE_TILING_LINEAR ||
          !vk_format_is_depth_or_stencil(dst_format));

   /* Can't sample from linear images */
   if (src->tiling == VK_IMAGE_TILING_LINEAR && src->type != VK_IMAGE_TYPE_1D)
      return false;

   VkImageBlit region = *_region;
   /* Rewrite combined D/S blits to compatible color blits */
   if (vk_format_is_depth_or_stencil(dst_format)) {
      assert(src_format == dst_format);
      assert(cmask == 0);
      switch(dst_format) {
      case VK_FORMAT_D16_UNORM:
         dst_format = VK_FORMAT_R16_UINT;
         break;
      case VK_FORMAT_D32_SFLOAT:
         dst_format = VK_FORMAT_R32_UINT;
         break;
      case VK_FORMAT_X8_D24_UNORM_PACK32:
      case VK_FORMAT_D24_UNORM_S8_UINT:
         if (region.srcSubresource.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
            cmask |= VK_COLOR_COMPONENT_G_BIT |
                     VK_COLOR_COMPONENT_B_BIT |
                     VK_COLOR_COMPONENT_A_BIT;
         }
         if (region.srcSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) {
            assert(dst_format == VK_FORMAT_D24_UNORM_S8_UINT);
            cmask |= VK_COLOR_COMPONENT_R_BIT;
         }
         dst_format = VK_FORMAT_R8G8B8A8_UINT;
         break;
      default:
         unreachable("Unsupported depth/stencil format");
      };
      src_format = dst_format;
      region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
   }

   const VkColorComponentFlags full_cmask = VK_COLOR_COMPONENT_R_BIT |
                                            VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT |
                                            VK_COLOR_COMPONENT_A_BIT;
   if (cmask == 0)
      cmask = full_cmask;

   VkComponentMapping ident_swizzle = {
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
   };
   if (!cswizzle)
      cswizzle = &ident_swizzle;

   /* When we get here from a copy between compressed / uncompressed images
    * we choose to specify the destination blit region based on the size
    * semantics of the source image of the copy (see copy_image_blit), so we
    * need to apply those same semantics here when we compute the size of the
    * destination image level.
    */
   const uint32_t dst_block_w = vk_format_get_blockwidth(dst->vk_format);
   const uint32_t dst_block_h = vk_format_get_blockheight(dst->vk_format);
   const uint32_t src_block_w = vk_format_get_blockwidth(src->vk_format);
   const uint32_t src_block_h = vk_format_get_blockheight(src->vk_format);
   const uint32_t dst_level_w =
      u_minify(DIV_ROUND_UP(dst->extent.width * src_block_w, dst_block_w),
               region.dstSubresource.mipLevel);
   const uint32_t dst_level_h =
      u_minify(DIV_ROUND_UP(dst->extent.height * src_block_h, dst_block_h),
               region.dstSubresource.mipLevel);

   const uint32_t src_level_w =
      u_minify(src->extent.width, region.srcSubresource.mipLevel);
   const uint32_t src_level_h =
      u_minify(src->extent.height, region.srcSubresource.mipLevel);
   const uint32_t src_level_d =
      u_minify(src->extent.depth, region.srcSubresource.mipLevel);

   uint32_t dst_x, dst_y, dst_w, dst_h;
   bool dst_mirror_x, dst_mirror_y;
   compute_blit_box(region.dstOffsets,
                    dst_level_w, dst_level_h,
                    &dst_x, &dst_y, &dst_w, &dst_h,
                    &dst_mirror_x, &dst_mirror_y);

   uint32_t src_x, src_y, src_w, src_h;
   bool src_mirror_x, src_mirror_y;
   compute_blit_box(region.srcOffsets,
                    src_level_w, src_level_h,
                    &src_x, &src_y, &src_w, &src_h,
                    &src_mirror_x, &src_mirror_y);

   uint32_t min_dst_layer;
   uint32_t max_dst_layer;
   bool dst_mirror_z;
   if (dst->type != VK_IMAGE_TYPE_3D) {
      min_dst_layer = region.dstSubresource.baseArrayLayer;
      max_dst_layer = min_dst_layer + region.dstSubresource.layerCount;
   } else {
      compute_blit_3d_layers(region.dstOffsets,
                             &min_dst_layer, &max_dst_layer,
                             &dst_mirror_z);
   }

   uint32_t min_src_layer;
   uint32_t max_src_layer;
   bool src_mirror_z;
   if (src->type != VK_IMAGE_TYPE_3D) {
      min_src_layer = region.srcSubresource.baseArrayLayer;
      max_src_layer = min_src_layer + region.srcSubresource.layerCount;
   } else {
      compute_blit_3d_layers(region.srcOffsets,
                             &min_src_layer, &max_src_layer,
                             &src_mirror_z);
   }

   uint32_t layer_count = max_dst_layer - min_dst_layer;

   /* Translate source blit coordinates to normalized texture coordinates for
    * single sampled textures. For multisampled textures we require
    * unnormalized coordinates, since we can only do texelFetch on them.
    */
   float coords[4] =  {
      (float)src_x,
      (float)src_y,
      (float)(src_x + src_w),
      (float)(src_y + src_h),
   };

   if (src->samples == VK_SAMPLE_COUNT_1_BIT) {
      coords[0] /= (float)src_level_w;
      coords[1] /= (float)src_level_h;
      coords[2] /= (float)src_level_w;
      coords[3] /= (float)src_level_h;
   }

   /* Handle mirroring */
   const bool mirror_x = dst_mirror_x != src_mirror_x;
   const bool mirror_y = dst_mirror_y != src_mirror_y;
   const bool mirror_z = dst_mirror_z != src_mirror_z;
   float tex_coords[5] = {
      !mirror_x ? coords[0] : coords[2],
      !mirror_y ? coords[1] : coords[3],
      !mirror_x ? coords[2] : coords[0],
      !mirror_y ? coords[3] : coords[1],
      /* Z coordinate for 3D blit sources, to be filled for each
       * destination layer
       */
      0.0f
   };

   /* For blits from 3D images we also need to compute the slice coordinate to
    * sample from, which will change for each layer in the destination.
    * Compute the step we should increase for each iteration.
    */
   const float src_z_step =
      (float)(max_src_layer - min_src_layer) / (float)layer_count;

   /* Get the blit pipeline */
   struct v3dv_meta_blit_pipeline *pipeline = NULL;
   bool ok = get_blit_pipeline(cmd_buffer->device,
                               dst_format, src_format, cmask, src->type,
                               dst->samples, src->samples,
                               &pipeline);
   if (!ok)
      return handled;
   assert(pipeline && pipeline->pipeline &&
          pipeline->pass && pipeline->pass_no_load);

   struct v3dv_device *device = cmd_buffer->device;
   assert(device->meta.blit.ds_layout);

   VkDevice _device = v3dv_device_to_handle(device);
   VkCommandBuffer _cmd_buffer = v3dv_cmd_buffer_to_handle(cmd_buffer);

   /* Create sampler for blit source image */
   VkSamplerCreateInfo sampler_info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = filter,
      .minFilter = filter,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
   };
   VkSampler sampler;
   result = v3dv_CreateSampler(_device, &sampler_info, &device->vk.alloc,
                               &sampler);
   if (result != VK_SUCCESS)
      goto fail;

   v3dv_cmd_buffer_add_private_obj(
      cmd_buffer, (uintptr_t)sampler,
      (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroySampler);

   /* Push command buffer state before starting meta operation */
   v3dv_cmd_buffer_meta_state_push(cmd_buffer, true);

   /* Push state that is common for all layers */
   v3dv_CmdBindPipeline(_cmd_buffer,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline->pipeline);

   const VkViewport viewport = {
      .x = dst_x,
      .y = dst_y,
      .width = dst_w,
      .height = dst_h,
      .minDepth = 0.0f,
      .maxDepth = 1.0f
   };
   v3dv_CmdSetViewport(_cmd_buffer, 0, 1, &viewport);

   const VkRect2D scissor = {
      .offset = { dst_x, dst_y },
      .extent = { dst_w, dst_h }
   };
   v3dv_CmdSetScissor(_cmd_buffer, 0, 1, &scissor);

   bool can_skip_tlb_load;
   const VkRect2D render_area = {
      .offset = { dst_x, dst_y },
      .extent = { dst_w, dst_h },
   };

   /* Record per-layer commands */
   uint32_t dirty_dynamic_state = 0;
   VkImageAspectFlags aspects = region.dstSubresource.aspectMask;
   for (uint32_t i = 0; i < layer_count; i++) {
      /* Setup framebuffer */
      VkImageViewCreateInfo dst_image_view_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = v3dv_image_to_handle(dst),
         .viewType = v3dv_image_type_to_view_type(dst->type),
         .format = dst_format,
         .subresourceRange = {
            .aspectMask = aspects,
            .baseMipLevel = region.dstSubresource.mipLevel,
            .levelCount = 1,
            .baseArrayLayer = min_dst_layer + i,
            .layerCount = 1
         },
      };
      VkImageView dst_image_view;
      result = v3dv_CreateImageView(_device, &dst_image_view_info,
                                    &device->vk.alloc, &dst_image_view);
      if (result != VK_SUCCESS)
         goto fail;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t)dst_image_view,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyImageView);

      VkFramebufferCreateInfo fb_info = {
         .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
         .renderPass = pipeline->pass,
         .attachmentCount = 1,
         .pAttachments = &dst_image_view,
         .width = dst_x + dst_w,
         .height = dst_y + dst_h,
         .layers = 1,
      };

      VkFramebuffer fb;
      result = v3dv_CreateFramebuffer(_device, &fb_info,
                                      &cmd_buffer->device->vk.alloc, &fb);
      if (result != VK_SUCCESS)
         goto fail;

      struct v3dv_framebuffer *framebuffer = v3dv_framebuffer_from_handle(fb);
      framebuffer->has_edge_padding = fb_info.width == dst_level_w &&
                                      fb_info.height == dst_level_h &&
                                      dst_is_padded_image;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t)fb,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyFramebuffer);

      /* Setup descriptor set for blit source texture. We don't have to
       * register the descriptor as a private command buffer object since
       * all descriptors will be freed automatically with the descriptor
       * pool.
       */
      VkDescriptorSet set;
      result = allocate_blit_source_descriptor_set(cmd_buffer, &set);
      if (result != VK_SUCCESS)
         goto fail;

      VkImageViewCreateInfo src_image_view_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = v3dv_image_to_handle(src),
         .viewType = v3dv_image_type_to_view_type(src->type),
         .format = src_format,
         .components = *cswizzle,
         .subresourceRange = {
            .aspectMask = aspects,
            .baseMipLevel = region.srcSubresource.mipLevel,
            .levelCount = 1,
            .baseArrayLayer =
               src->type == VK_IMAGE_TYPE_3D ? 0 : min_src_layer + i,
            .layerCount = 1
         },
      };
      VkImageView src_image_view;
      result = v3dv_CreateImageView(_device, &src_image_view_info,
                                    &device->vk.alloc, &src_image_view);
      if (result != VK_SUCCESS)
         goto fail;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t)src_image_view,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyImageView);

      VkDescriptorImageInfo image_info = {
         .sampler = sampler,
         .imageView = src_image_view,
         .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      };
      VkWriteDescriptorSet write = {
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet = set,
         .dstBinding = 0,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .pImageInfo = &image_info,
      };
      v3dv_UpdateDescriptorSets(_device, 1, &write, 0, NULL);

      v3dv_CmdBindDescriptorSets(_cmd_buffer,
                                 VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 device->meta.blit.p_layout,
                                 0, 1, &set,
                                 0, NULL);

      /* If the region we are about to blit is tile-aligned, then we can
       * use the render pass version that won't pre-load the tile buffer
       * with the dst image contents before the blit. The exception is when we
       * don't have a full color mask, since in that case we need to preserve
       * the original value of some of the color components.
       *
       * Since all layers have the same area, we only need to compute this for
       * the first.
       */
      if (i == 0) {
         struct v3dv_render_pass *pipeline_pass =
            v3dv_render_pass_from_handle(pipeline->pass);
         can_skip_tlb_load =
            cmask == full_cmask &&
            v3dv_subpass_area_is_tile_aligned(&render_area, framebuffer,
                                              pipeline_pass, 0);
      }

      /* Record blit */
      VkRenderPassBeginInfo rp_info = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = can_skip_tlb_load ? pipeline->pass_no_load :
                                           pipeline->pass,
         .framebuffer = fb,
         .renderArea = render_area,
         .clearValueCount = 0,
      };

      v3dv_CmdBeginRenderPass(_cmd_buffer, &rp_info, VK_SUBPASS_CONTENTS_INLINE);
      struct v3dv_job *job = cmd_buffer->state.job;
      if (!job)
         goto fail;

      /* For 3D blits we need to compute the source slice to blit from (the Z
       * coordinate of the source sample operation). We want to choose this
       * based on the ratio of the depth of the source and the destination
       * images, picking the coordinate in the middle of each step.
       */
      if (src->type == VK_IMAGE_TYPE_3D) {
         tex_coords[4] =
            !mirror_z ?
            (min_src_layer + (i + 0.5f) * src_z_step) / (float)src_level_d :
            (max_src_layer - (i + 0.5f) * src_z_step) / (float)src_level_d;
      }

      v3dv_CmdPushConstants(_cmd_buffer,
                            device->meta.blit.p_layout,
                            VK_SHADER_STAGE_VERTEX_BIT, 0, 20,
                            &tex_coords);

      v3dv_CmdDraw(_cmd_buffer, 4, 1, 0, 0);

      v3dv_CmdEndRenderPass(_cmd_buffer);
      dirty_dynamic_state = V3DV_CMD_DIRTY_VIEWPORT | V3DV_CMD_DIRTY_SCISSOR;
   }

fail:
   v3dv_cmd_buffer_meta_state_pop(cmd_buffer, dirty_dynamic_state, true);

   return handled;
}

void
v3dv_CmdBlitImage(VkCommandBuffer commandBuffer,
                  VkImage srcImage,
                  VkImageLayout srcImageLayout,
                  VkImage dstImage,
                  VkImageLayout dstImageLayout,
                  uint32_t regionCount,
                  const VkImageBlit* pRegions,
                  VkFilter filter)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_image, src, srcImage);
   V3DV_FROM_HANDLE(v3dv_image, dst, dstImage);

    /* This command can only happen outside a render pass */
   assert(cmd_buffer->state.pass == NULL);
   assert(cmd_buffer->state.job == NULL);

   /* From the Vulkan 1.0 spec, vkCmdBlitImage valid usage */
   assert(dst->samples == VK_SAMPLE_COUNT_1_BIT &&
          src->samples == VK_SAMPLE_COUNT_1_BIT);

   /* We don't export VK_FORMAT_FEATURE_BLIT_DST_BIT on compressed formats */
   assert(!vk_format_is_compressed(dst->vk_format));

   for (uint32_t i = 0; i < regionCount; i++) {
      if (blit_tfu(cmd_buffer, dst, src, &pRegions[i]))
         continue;
      if (blit_shader(cmd_buffer,
                      dst, dst->vk_format,
                      src, src->vk_format,
                      0, NULL,
                      &pRegions[i], filter, true)) {
         continue;
      }
      unreachable("Unsupported blit operation");
   }
}

static void
emit_resolve_image_layer_per_tile_list(struct v3dv_job *job,
                                       struct framebuffer_data *framebuffer,
                                       struct v3dv_image *dst,
                                       struct v3dv_image *src,
                                       uint32_t layer_offset,
                                       const VkImageResolve *region)
{
   struct v3dv_cl *cl = &job->indirect;
   v3dv_cl_ensure_space(cl, 200, 1);
   v3dv_return_if_oom(NULL, job);

   struct v3dv_cl_reloc tile_list_start = v3dv_cl_get_address(cl);

   cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);

   assert((src->type != VK_IMAGE_TYPE_3D &&
           layer_offset < region->srcSubresource.layerCount) ||
          layer_offset < src->extent.depth);

   const uint32_t src_layer = src->type != VK_IMAGE_TYPE_3D ?
      region->srcSubresource.baseArrayLayer + layer_offset :
      region->srcOffset.z + layer_offset;

   emit_image_load(cl, framebuffer, src,
                   region->srcSubresource.aspectMask,
                   src_layer,
                   region->srcSubresource.mipLevel,
                   false, false);

   cl_emit(cl, END_OF_LOADS, end);

   cl_emit(cl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

   assert((dst->type != VK_IMAGE_TYPE_3D &&
           layer_offset < region->dstSubresource.layerCount) ||
          layer_offset < dst->extent.depth);

   const uint32_t dst_layer = dst->type != VK_IMAGE_TYPE_3D ?
      region->dstSubresource.baseArrayLayer + layer_offset :
      region->dstOffset.z + layer_offset;

   emit_image_store(cl, framebuffer, dst,
                    region->dstSubresource.aspectMask,
                    dst_layer,
                    region->dstSubresource.mipLevel,
                    false, false, true);

   cl_emit(cl, END_OF_TILE_MARKER, end);

   cl_emit(cl, RETURN_FROM_SUB_LIST, ret);

   cl_emit(&job->rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
      branch.start = tile_list_start;
      branch.end = v3dv_cl_get_address(cl);
   }
}

static void
emit_resolve_image_layer(struct v3dv_job *job,
                         struct v3dv_image *dst,
                         struct v3dv_image *src,
                         struct framebuffer_data *framebuffer,
                         uint32_t layer,
                         const VkImageResolve *region)
{
   emit_frame_setup(job, layer, NULL);
   emit_resolve_image_layer_per_tile_list(job, framebuffer,
                                          dst, src, layer, region);
   emit_supertile_coordinates(job, framebuffer);
}

static void
emit_resolve_image_rcl(struct v3dv_job *job,
                       struct v3dv_image *dst,
                       struct v3dv_image *src,
                       struct framebuffer_data *framebuffer,
                       const VkImageResolve *region)
{
   struct v3dv_cl *rcl = emit_rcl_prologue(job, framebuffer, NULL);
   v3dv_return_if_oom(NULL, job);

   for (int layer = 0; layer < job->frame_tiling.layers; layer++)
      emit_resolve_image_layer(job, dst, src, framebuffer, layer, region);
   cl_emit(rcl, END_OF_RENDERING, end);
}

static bool
resolve_image_tlb(struct v3dv_cmd_buffer *cmd_buffer,
                  struct v3dv_image *dst,
                  struct v3dv_image *src,
                  const VkImageResolve *region)
{
   if (!can_use_tlb(src, &region->srcOffset, NULL) ||
       !can_use_tlb(dst, &region->dstOffset, NULL)) {
      return false;
   }

   if (!v3dv_format_supports_tlb_resolve(src->format))
      return false;

   const VkFormat fb_format = src->vk_format;

   uint32_t num_layers;
   if (dst->type != VK_IMAGE_TYPE_3D)
      num_layers = region->dstSubresource.layerCount;
   else
      num_layers = region->extent.depth;
   assert(num_layers > 0);

   struct v3dv_job *job =
      v3dv_cmd_buffer_start_job(cmd_buffer, -1, V3DV_JOB_TYPE_GPU_CL);
   if (!job)
      return true;

   const uint32_t block_w = vk_format_get_blockwidth(dst->vk_format);
   const uint32_t block_h = vk_format_get_blockheight(dst->vk_format);
   const uint32_t width = DIV_ROUND_UP(region->extent.width, block_w);
   const uint32_t height = DIV_ROUND_UP(region->extent.height, block_h);

   uint32_t internal_type, internal_bpp;
   get_internal_type_bpp_for_image_aspects(fb_format,
                                           region->srcSubresource.aspectMask,
                                           &internal_type, &internal_bpp);

   v3dv_job_start_frame(job, width, height, num_layers, 1, internal_bpp, true);

   struct framebuffer_data framebuffer;
   setup_framebuffer_data(&framebuffer, fb_format, internal_type,
                          &job->frame_tiling);

   v3dv_job_emit_binning_flush(job);
   emit_resolve_image_rcl(job, dst, src, &framebuffer, region);

   v3dv_cmd_buffer_finish_job(cmd_buffer);
   return true;
}

static bool
resolve_image_blit(struct v3dv_cmd_buffer *cmd_buffer,
                   struct v3dv_image *dst,
                   struct v3dv_image *src,
                   const VkImageResolve *region)
{
   const VkImageBlit blit_region = {
      .srcSubresource = region->srcSubresource,
      .srcOffsets = {
         region->srcOffset,
         {
            region->srcOffset.x + region->extent.width,
            region->srcOffset.y + region->extent.height,
         }
      },
      .dstSubresource = region->dstSubresource,
      .dstOffsets = {
         region->dstOffset,
         {
            region->dstOffset.x + region->extent.width,
            region->dstOffset.y + region->extent.height,
         }
      },
   };
   return blit_shader(cmd_buffer,
                      dst, dst->vk_format,
                      src, src->vk_format,
                      0, NULL,
                      &blit_region, VK_FILTER_NEAREST, true);
}

void
v3dv_CmdResolveImage(VkCommandBuffer commandBuffer,
                     VkImage srcImage,
                     VkImageLayout srcImageLayout,
                     VkImage dstImage,
                     VkImageLayout dstImageLayout,
                     uint32_t regionCount,
                     const VkImageResolve *pRegions)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_image, src, srcImage);
   V3DV_FROM_HANDLE(v3dv_image, dst, dstImage);

    /* This command can only happen outside a render pass */
   assert(cmd_buffer->state.pass == NULL);
   assert(cmd_buffer->state.job == NULL);

   assert(src->samples == VK_SAMPLE_COUNT_4_BIT);
   assert(dst->samples == VK_SAMPLE_COUNT_1_BIT);

   for (uint32_t i = 0; i < regionCount; i++) {
      if (resolve_image_tlb(cmd_buffer, dst, src, &pRegions[i]))
         continue;
      if (resolve_image_blit(cmd_buffer, dst, src, &pRegions[i]))
         continue;
      unreachable("Unsupported multismaple resolve operation");
   }
}
