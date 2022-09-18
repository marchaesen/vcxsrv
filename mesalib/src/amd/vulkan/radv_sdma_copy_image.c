/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2015-2021 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "util/u_memory.h"
#include "radv_cs.h"
#include "radv_private.h"
#include "sid.h"

static bool
radv_translate_format_to_hw(struct radeon_info *info, VkFormat format, unsigned *hw_fmt,
                            unsigned *hw_type)
{
   const struct util_format_description *desc = vk_format_description(format);
   *hw_fmt = radv_translate_colorformat(format);

   int firstchan;
   for (firstchan = 0; firstchan < 4; firstchan++) {
      if (desc->channel[firstchan].type != UTIL_FORMAT_TYPE_VOID) {
         break;
      }
   }
   if (firstchan == 4 || desc->channel[firstchan].type == UTIL_FORMAT_TYPE_FLOAT) {
      *hw_type = V_028C70_NUMBER_FLOAT;
   } else {
      *hw_type = V_028C70_NUMBER_UNORM;
      if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
         *hw_type = V_028C70_NUMBER_SRGB;
      else if (desc->channel[firstchan].type == UTIL_FORMAT_TYPE_SIGNED) {
         if (desc->channel[firstchan].pure_integer) {
            *hw_type = V_028C70_NUMBER_SINT;
         } else {
            assert(desc->channel[firstchan].normalized);
            *hw_type = V_028C70_NUMBER_SNORM;
         }
      } else if (desc->channel[firstchan].type == UTIL_FORMAT_TYPE_UNSIGNED) {
         if (desc->channel[firstchan].pure_integer) {
            *hw_type = V_028C70_NUMBER_UINT;
         } else {
            assert(desc->channel[firstchan].normalized);
            *hw_type = V_028C70_NUMBER_UNORM;
         }
      } else {
         return false;
      }
   }
   return true;
}

static bool
radv_sdma_v4_v5_copy_image_to_buffer(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                                     struct radv_buffer *buffer,
                                     const VkBufferImageCopy2 *region)
{
   assert(image->plane_count == 1);
   struct radv_device *device = cmd_buffer->device;
   unsigned bpp = image->planes[0].surface.bpe;
   uint64_t dst_address = buffer->bo->va;
   uint64_t src_address = image->bindings[0].bo->va + image->planes[0].surface.u.gfx9.surf_offset;
   unsigned src_pitch = image->planes[0].surface.u.gfx9.surf_pitch;
   unsigned copy_width = DIV_ROUND_UP(image->info.width, image->planes[0].surface.blk_w);
   unsigned copy_height = DIV_ROUND_UP(image->info.height, image->planes[0].surface.blk_h);
   bool tmz = false;

   uint32_t ib_pad_dw_mask = cmd_buffer->device->physical_device->rad_info.ib_pad_dw_mask[AMD_IP_SDMA];

   /* Linear -> linear sub-window copy. */
   if (image->planes[0].surface.is_linear) {
      ASSERTED unsigned cdw_max =
         radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, align(8, ib_pad_dw_mask + 1));
      unsigned bytes = src_pitch * copy_height * bpp;

      if (!(bytes < (1u << 22)))
         return false;

      radeon_emit(cmd_buffer->cs, 0x00000000);

      src_address += image->planes[0].surface.u.gfx9.offset[0];

      radeon_emit(cmd_buffer->cs, CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY,
                                                  CIK_SDMA_COPY_SUB_OPCODE_LINEAR, (tmz ? 4 : 0)));
      radeon_emit(cmd_buffer->cs, bytes);
      radeon_emit(cmd_buffer->cs, 0);
      radeon_emit(cmd_buffer->cs, src_address);
      radeon_emit(cmd_buffer->cs, src_address >> 32);
      radeon_emit(cmd_buffer->cs, dst_address);
      radeon_emit(cmd_buffer->cs, dst_address >> 32);

      while (cmd_buffer->cs->cdw & ib_pad_dw_mask)
         radeon_emit(cmd_buffer->cs, SDMA_NOP_PAD);

      assert(cmd_buffer->cs->cdw <= cdw_max);

      return true;
   }
   /* Tiled sub-window copy -> Linear */
   else {
      unsigned tiled_width = copy_width;
      unsigned tiled_height = copy_height;
      unsigned linear_pitch = region->bufferRowLength;
      unsigned linear_slice_pitch = region->bufferRowLength * copy_height;
      uint64_t tiled_address = src_address;
      uint64_t linear_address = dst_address;
      bool is_v5 = device->physical_device->rad_info.gfx_level >= GFX10;
      /* Only SDMA 5 supports DCC with SDMA */
      bool dcc = radv_dcc_enabled(image, 0) && is_v5;

      /* Check if everything fits into the bitfields */
      if (!(tiled_width < (1 << 14) && tiled_height < (1 << 14) && linear_pitch < (1 << 14) &&
            linear_slice_pitch < (1 << 28) && copy_width < (1 << 14) && copy_height < (1 << 14)))
         return false;

      ASSERTED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs,
                                                     align(15 + dcc * 3, ib_pad_dw_mask + 1));

      radeon_emit(cmd_buffer->cs, 0x00000000);
      radeon_emit(cmd_buffer->cs,
                  CIK_SDMA_PACKET(CIK_SDMA_OPCODE_COPY, CIK_SDMA_COPY_SUB_OPCODE_TILED_SUB_WINDOW,
                                  (tmz ? 4 : 0)) |
                     dcc << 19 | (is_v5 ? 0 : 0 /* tiled->buffer.b.b.last_level */) << 20 |
                     1u << 31);
      radeon_emit(cmd_buffer->cs,
                  (uint32_t)tiled_address | (image->planes[0].surface.tile_swizzle << 8));
      radeon_emit(cmd_buffer->cs, (uint32_t)(tiled_address >> 32));
      radeon_emit(cmd_buffer->cs, 0);
      radeon_emit(cmd_buffer->cs, ((tiled_width - 1) << 16));
      radeon_emit(cmd_buffer->cs, (tiled_height - 1));
      radeon_emit(
         cmd_buffer->cs,
         util_logbase2(bpp) | image->planes[0].surface.u.gfx9.swizzle_mode << 3 |
            image->planes[0].surface.u.gfx9.resource_type << 9 |
            (is_v5 ? 0 /* tiled->buffer.b.b.last_level */ : image->planes[0].surface.u.gfx9.epitch)
               << 16);
      radeon_emit(cmd_buffer->cs, (uint32_t)linear_address);
      radeon_emit(cmd_buffer->cs, (uint32_t)(linear_address >> 32));
      radeon_emit(cmd_buffer->cs, 0);
      radeon_emit(cmd_buffer->cs, ((linear_pitch - 1) << 16));
      radeon_emit(cmd_buffer->cs, linear_slice_pitch - 1);
      radeon_emit(cmd_buffer->cs, (copy_width - 1) | ((copy_height - 1) << 16));
      radeon_emit(cmd_buffer->cs, 0);

      if (dcc) {
         unsigned hw_fmt, hw_type;
         uint64_t md_address = tiled_address + image->planes[0].surface.meta_offset;

         radv_translate_format_to_hw(&device->physical_device->rad_info, image->vk.format, &hw_fmt,
                                     &hw_type);

         /* Add metadata */
         radeon_emit(cmd_buffer->cs, (uint32_t)md_address);
         radeon_emit(cmd_buffer->cs, (uint32_t)(md_address >> 32));
         radeon_emit(cmd_buffer->cs,
                     hw_fmt | vi_alpha_is_on_msb(device, image->vk.format) << 8 | hw_type << 9 |
                        image->planes[0].surface.u.gfx9.color.dcc.max_compressed_block_size << 24 |
                        V_028C78_MAX_BLOCK_SIZE_256B << 26 | tmz << 29 |
                        image->planes[0].surface.u.gfx9.color.dcc.pipe_aligned << 31);
      }

      while (cmd_buffer->cs->cdw & ib_pad_dw_mask)
         radeon_emit(cmd_buffer->cs, SDMA_NOP_PAD);

      assert(cmd_buffer->cs->cdw <= cdw_max);

      return true;
   }

   return false;
}

bool
radv_sdma_copy_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                     struct radv_buffer *buffer, const VkBufferImageCopy2 *region)
{
   assert(cmd_buffer->device->physical_device->rad_info.gfx_level >= GFX9);
   return radv_sdma_v4_v5_copy_image_to_buffer(cmd_buffer, image, buffer, region);
}
