/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 *
 */

#include "si_pipe.h"
#include "util/format/u_format.h"
#include "util/format_srgb.h"

/* Note: Compute shaders always use SI_COMPUTE_DST_CACHE_POLICY for dst
 * and L2_STREAM for src.
 */
static enum si_cache_policy get_cache_policy(struct si_context *sctx, enum si_coherency coher,
                                             uint64_t size)
{
   if ((sctx->chip_class >= GFX9 && (coher == SI_COHERENCY_CB_META ||
                                     coher == SI_COHERENCY_DB_META ||
                                     coher == SI_COHERENCY_CP)) ||
       (sctx->chip_class >= GFX7 && coher == SI_COHERENCY_SHADER))
      return size <= 256 * 1024 ? L2_LRU : L2_STREAM;

   return L2_BYPASS;
}

unsigned si_get_flush_flags(struct si_context *sctx, enum si_coherency coher,
                            enum si_cache_policy cache_policy)
{
   switch (coher) {
   default:
   case SI_COHERENCY_NONE:
   case SI_COHERENCY_CP:
      return 0;
   case SI_COHERENCY_SHADER:
      return SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE |
             (cache_policy == L2_BYPASS ? SI_CONTEXT_INV_L2 : 0);
   case SI_COHERENCY_CB_META:
      return SI_CONTEXT_FLUSH_AND_INV_CB;
   case SI_COHERENCY_DB_META:
      return SI_CONTEXT_FLUSH_AND_INV_DB;
   }
}

void si_launch_grid_internal(struct si_context *sctx, struct pipe_grid_info *info,
                                    void *restore_cs, unsigned flags)
{
   /* Wait for previous shaders to finish. */
   sctx->flags |= SI_CONTEXT_PS_PARTIAL_FLUSH;
   if (!(flags & SI_CS_PARTIAL_FLUSH_DISABLE))
      sctx->flags |= SI_CONTEXT_CS_PARTIAL_FLUSH;
   /* Invalidate L0-L1 caches. */
   /* sL0 is never invalidated, because src resources don't use it. */
   sctx->flags |= SI_CONTEXT_INV_VCACHE;

   /* Set settings for driver-internal compute dispatches. */
   sctx->flags &= ~SI_CONTEXT_START_PIPELINE_STATS;
   sctx->flags |= SI_CONTEXT_STOP_PIPELINE_STATS;

   if (!(flags & SI_CS_RENDER_COND_ENABLE))
      sctx->render_cond_enabled = false;

   /* Skip decompression to prevent infinite recursion. */
   sctx->blitter_running = true;

   /* Dispatch compute. */
   sctx->b.launch_grid(&sctx->b, info);

   /* Restore default settings. */
   sctx->flags &= ~SI_CONTEXT_STOP_PIPELINE_STATS;
   sctx->flags |= SI_CONTEXT_START_PIPELINE_STATS;
   sctx->render_cond_enabled = sctx->render_cond;
   sctx->blitter_running = false;

   /* Restore the original compute shader. */
   sctx->b.bind_compute_state(&sctx->b, restore_cs);

   if (flags & SI_CS_WAIT_FOR_IDLE) {
      sctx->flags |= SI_CONTEXT_CS_PARTIAL_FLUSH;

      if (flags & SI_CS_IMAGE_OP) {
         /* Make sure image stores are visible to CB, which doesn't use L2 on GFX6-8. */
         sctx->flags |= sctx->chip_class <= GFX8 ? SI_CONTEXT_WB_L2 : 0;
         /* Make sure image stores are visible to all CUs. */
         sctx->flags |= SI_CONTEXT_INV_VCACHE;
      } else {
         /* Make sure buffer stores are visible to all CUs. */
         sctx->flags |= SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE;
      }
   } else {
      assert(!(flags & SI_CS_IMAGE_OP));
   }
}

static void si_compute_clear_12bytes_buffer(struct si_context *sctx, struct pipe_resource *dst,
                                            unsigned dst_offset, unsigned size,
                                            const uint32_t *clear_value, enum si_coherency coher)
{
   struct pipe_context *ctx = &sctx->b;

   assert(dst_offset % 4 == 0);
   assert(size % 4 == 0);
   unsigned size_12 = DIV_ROUND_UP(size, 12);

   unsigned data[4] = {0};
   memcpy(data, clear_value, 12);

   sctx->flags |= si_get_flush_flags(sctx, coher, SI_COMPUTE_DST_CACHE_POLICY);

   struct pipe_shader_buffer saved_sb = {0};
   si_get_shader_buffers(sctx, PIPE_SHADER_COMPUTE, 0, 1, &saved_sb);

   unsigned saved_writable_mask = 0;
   if (sctx->const_and_shader_buffers[PIPE_SHADER_COMPUTE].writable_mask &
       (1u << si_get_shaderbuf_slot(0)))
      saved_writable_mask = 1;

   struct pipe_constant_buffer saved_cb = {};
   si_get_pipe_constant_buffer(sctx, PIPE_SHADER_COMPUTE, 0, &saved_cb);

   void *saved_cs = sctx->cs_shader_state.program;

   struct pipe_constant_buffer cb = {};
   cb.buffer_size = sizeof(data);
   cb.user_buffer = data;
   ctx->set_constant_buffer(ctx, PIPE_SHADER_COMPUTE, 0, false, &cb);

   struct pipe_shader_buffer sb = {0};
   sb.buffer = dst;
   sb.buffer_offset = dst_offset;
   sb.buffer_size = size;

   ctx->set_shader_buffers(ctx, PIPE_SHADER_COMPUTE, 0, 1, &sb, 0x1);

   struct pipe_grid_info info = {0};

   if (!sctx->cs_clear_12bytes_buffer)
      sctx->cs_clear_12bytes_buffer = si_clear_12bytes_buffer_shader(ctx);
   ctx->bind_compute_state(ctx, sctx->cs_clear_12bytes_buffer);
   info.block[0] = 64;
   info.last_block[0] = size_12 % 64;
   info.block[1] = 1;
   info.block[2] = 1;
   info.grid[0] = DIV_ROUND_UP(size_12, 64);
   info.grid[1] = 1;
   info.grid[2] = 1;

   si_launch_grid_internal(sctx, &info, saved_cs, SI_CS_WAIT_FOR_IDLE);

   ctx->set_shader_buffers(ctx, PIPE_SHADER_COMPUTE, 0, 1, &saved_sb, saved_writable_mask);
   ctx->set_constant_buffer(ctx, PIPE_SHADER_COMPUTE, 0, true, &saved_cb);

   pipe_resource_reference(&saved_sb.buffer, NULL);
}

static void si_compute_do_clear_or_copy(struct si_context *sctx, struct pipe_resource *dst,
                                        unsigned dst_offset, struct pipe_resource *src,
                                        unsigned src_offset, unsigned size,
                                        const uint32_t *clear_value, unsigned clear_value_size,
                                        enum si_coherency coher)
{
   struct pipe_context *ctx = &sctx->b;

   assert(src_offset % 4 == 0);
   assert(dst_offset % 4 == 0);
   assert(size % 4 == 0);

   assert(dst->target != PIPE_BUFFER || dst_offset + size <= dst->width0);
   assert(!src || src_offset + size <= src->width0);

   sctx->flags |= si_get_flush_flags(sctx, coher, SI_COMPUTE_DST_CACHE_POLICY);

   /* Save states. */
   void *saved_cs = sctx->cs_shader_state.program;
   struct pipe_shader_buffer saved_sb[2] = {};
   si_get_shader_buffers(sctx, PIPE_SHADER_COMPUTE, 0, src ? 2 : 1, saved_sb);

   unsigned saved_writable_mask = 0;
   for (unsigned i = 0; i < (src ? 2 : 1); i++) {
      if (sctx->const_and_shader_buffers[PIPE_SHADER_COMPUTE].writable_mask &
          (1u << si_get_shaderbuf_slot(i)))
         saved_writable_mask |= 1 << i;
   }

   /* The memory accesses are coalesced, meaning that the 1st instruction writes
    * the 1st contiguous block of data for the whole wave, the 2nd instruction
    * writes the 2nd contiguous block of data, etc.
    */
   unsigned dwords_per_thread =
      src ? SI_COMPUTE_COPY_DW_PER_THREAD : SI_COMPUTE_CLEAR_DW_PER_THREAD;
   unsigned instructions_per_thread = MAX2(1, dwords_per_thread / 4);
   unsigned dwords_per_instruction = dwords_per_thread / instructions_per_thread;
   unsigned wave_size = sctx->screen->compute_wave_size;
   unsigned dwords_per_wave = dwords_per_thread * wave_size;

   unsigned num_dwords = size / 4;
   unsigned num_instructions = DIV_ROUND_UP(num_dwords, dwords_per_instruction);

   struct pipe_grid_info info = {};
   info.block[0] = MIN2(wave_size, num_instructions);
   info.block[1] = 1;
   info.block[2] = 1;
   info.grid[0] = DIV_ROUND_UP(num_dwords, dwords_per_wave);
   info.grid[1] = 1;
   info.grid[2] = 1;

   struct pipe_shader_buffer sb[2] = {};
   sb[0].buffer = dst;
   sb[0].buffer_offset = dst_offset;
   sb[0].buffer_size = size;

   bool shader_dst_stream_policy = SI_COMPUTE_DST_CACHE_POLICY != L2_LRU;

   if (src) {
      sb[1].buffer = src;
      sb[1].buffer_offset = src_offset;
      sb[1].buffer_size = size;

      ctx->set_shader_buffers(ctx, PIPE_SHADER_COMPUTE, 0, 2, sb, 0x1);

      if (!sctx->cs_copy_buffer) {
         sctx->cs_copy_buffer = si_create_dma_compute_shader(
            &sctx->b, SI_COMPUTE_COPY_DW_PER_THREAD, shader_dst_stream_policy, true);
      }
      ctx->bind_compute_state(ctx, sctx->cs_copy_buffer);
   } else {
      assert(clear_value_size >= 4 && clear_value_size <= 16 &&
             util_is_power_of_two_or_zero(clear_value_size));

      for (unsigned i = 0; i < 4; i++)
         sctx->cs_user_data[i] = clear_value[i % (clear_value_size / 4)];

      ctx->set_shader_buffers(ctx, PIPE_SHADER_COMPUTE, 0, 1, sb, 0x1);

      if (!sctx->cs_clear_buffer) {
         sctx->cs_clear_buffer = si_create_dma_compute_shader(
            &sctx->b, SI_COMPUTE_CLEAR_DW_PER_THREAD, shader_dst_stream_policy, false);
      }
      ctx->bind_compute_state(ctx, sctx->cs_clear_buffer);
   }

   si_launch_grid_internal(sctx, &info, saved_cs, SI_CS_WAIT_FOR_IDLE);

   enum si_cache_policy cache_policy = get_cache_policy(sctx, coher, size);
   sctx->flags |= cache_policy == L2_BYPASS ? SI_CONTEXT_WB_L2 : 0;

   if (cache_policy != L2_BYPASS)
      si_resource(dst)->TC_L2_dirty = true;

   /* Restore states. */
   ctx->set_shader_buffers(ctx, PIPE_SHADER_COMPUTE, 0, src ? 2 : 1, saved_sb, saved_writable_mask);
   for (int i = 0; i < 2; i++)
      pipe_resource_reference(&saved_sb[i].buffer, NULL);
}

void si_clear_buffer(struct si_context *sctx, struct pipe_resource *dst, uint64_t offset,
                     uint64_t size, uint32_t *clear_value, uint32_t clear_value_size,
                     enum si_coherency coher, enum si_clear_method method)
{
   if (!size)
      return;

   ASSERTED unsigned clear_alignment = MIN2(clear_value_size, 4);

   assert(clear_value_size != 3 && clear_value_size != 6); /* 12 is allowed. */
   assert(offset % clear_alignment == 0);
   assert(size % clear_alignment == 0);
   assert(size < (UINT_MAX & ~0xf)); /* TODO: test 64-bit sizes in all codepaths */

   /* Reduce a large clear value size if possible. */
   if (clear_value_size > 4) {
      bool clear_dword_duplicated = true;

      /* See if we can lower large fills to dword fills. */
      for (unsigned i = 1; i < clear_value_size / 4; i++) {
         if (clear_value[0] != clear_value[i]) {
            clear_dword_duplicated = false;
            break;
         }
      }
      if (clear_dword_duplicated)
         clear_value_size = 4;
   }

   /* Expand a small clear value size. */
   uint32_t tmp_clear_value;
   if (clear_value_size <= 2) {
      if (clear_value_size == 1) {
         tmp_clear_value = *(uint8_t *)clear_value;
         tmp_clear_value |=
            (tmp_clear_value << 8) | (tmp_clear_value << 16) | (tmp_clear_value << 24);
      } else {
         tmp_clear_value = *(uint16_t *)clear_value;
         tmp_clear_value |= tmp_clear_value << 16;
      }
      clear_value = &tmp_clear_value;
      clear_value_size = 4;
   }

   if (clear_value_size == 12) {
      si_compute_clear_12bytes_buffer(sctx, dst, offset, size, clear_value, coher);
      return;
   }

   uint64_t aligned_size = size & ~3ull;
   if (aligned_size >= 4) {
      uint64_t compute_min_size;

      /* CP DMA clears are terribly slow with GTT on GFX6-8, which can always
       * happen due to BO evictions.
       */
      if (sctx->chip_class <= GFX8) {
         compute_min_size = 0;
      } else if (sctx->chip_class >= GFX10 &&
                 sctx->screen->info.has_dedicated_vram &&
                 si_resource(dst)->domains & RADEON_DOMAIN_VRAM) {
         /* VRAM clears on gfx10 dGPUs */
         if (sctx->screen->info.vram_bit_width >= 192)
            compute_min_size = 128 * 1024;
         else
            compute_min_size = 1024 * 1024;
      } else if (sctx->screen->info.has_dedicated_vram &&
                 si_resource(dst)->domains & RADEON_DOMAIN_GTT) {
         /* GTT clears on gfx9 and gfx10 dGPUs */
         compute_min_size = UINT64_MAX; /* CP DMA is the best due to slow PCIe */
      } else {
         compute_min_size = 32 * 1024;
      }

      if (method == SI_AUTO_SELECT_CLEAR_METHOD && (
           clear_value_size > 4 ||
           (clear_value_size == 4 && offset % 4 == 0 && size > compute_min_size))) {
         method = SI_COMPUTE_CLEAR_METHOD;
      }
      if (method == SI_COMPUTE_CLEAR_METHOD) {
         si_compute_do_clear_or_copy(sctx, dst, offset, NULL, 0, aligned_size, clear_value,
                                     clear_value_size, coher);
      } else {
         assert(clear_value_size == 4);
         si_cp_dma_clear_buffer(sctx, &sctx->gfx_cs, dst, offset, aligned_size, *clear_value, 0,
                                coher, get_cache_policy(sctx, coher, size));
      }

      offset += aligned_size;
      size -= aligned_size;
   }

   /* Handle non-dword alignment. */
   if (size) {
      assert(dst);
      assert(dst->target == PIPE_BUFFER);
      assert(size < 4);

      pipe_buffer_write(&sctx->b, dst, offset, size, clear_value);
   }
}

void si_screen_clear_buffer(struct si_screen *sscreen, struct pipe_resource *dst, uint64_t offset,
                            uint64_t size, unsigned value)
{
   struct si_context *ctx = (struct si_context *)sscreen->aux_context;

   simple_mtx_lock(&sscreen->aux_context_lock);
   ctx->b.clear_buffer(&ctx->b, dst, offset, size, &value, 4);
   sscreen->aux_context->flush(sscreen->aux_context, NULL, 0);
   simple_mtx_unlock(&sscreen->aux_context_lock);
}

static void si_pipe_clear_buffer(struct pipe_context *ctx, struct pipe_resource *dst,
                                 unsigned offset, unsigned size, const void *clear_value,
                                 int clear_value_size)
{
   si_clear_buffer((struct si_context *)ctx, dst, offset, size, (uint32_t *)clear_value,
                   clear_value_size, SI_COHERENCY_SHADER, SI_AUTO_SELECT_CLEAR_METHOD);
}

void si_copy_buffer(struct si_context *sctx, struct pipe_resource *dst, struct pipe_resource *src,
                    uint64_t dst_offset, uint64_t src_offset, unsigned size)
{
   if (!size)
      return;

   enum si_coherency coher = SI_COHERENCY_SHADER;
   enum si_cache_policy cache_policy = get_cache_policy(sctx, coher, size);
   uint64_t compute_min_size;

   if (sctx->chip_class >= GFX10 &&
       sctx->screen->info.has_dedicated_vram &&
       si_resource(dst)->domains & RADEON_DOMAIN_VRAM &&
       si_resource(src)->domains & RADEON_DOMAIN_VRAM) {
      /* VRAM copies on gfx10 dGPUs */
      if (sctx->screen->info.vram_bit_width >= 192)
         compute_min_size = 128 * 1024;
      else
         compute_min_size = 1024 * 1024;
   } else if (sctx->chip_class >= GFX10 &&
              sctx->screen->info.has_dedicated_vram &&
              (si_resource(dst)->domains | si_resource(src)->domains) & RADEON_DOMAIN_GTT) {
      compute_min_size = UINT64_MAX; /* CP DMA is the best due to slow PCIe */
   } else {
      compute_min_size = 32 * 1024;
   }

   /* Only use compute for VRAM copies on dGPUs. */
   if (sctx->screen->info.has_dedicated_vram && si_resource(dst)->domains & RADEON_DOMAIN_VRAM &&
       si_resource(src)->domains & RADEON_DOMAIN_VRAM && size > compute_min_size &&
       dst_offset % 4 == 0 && src_offset % 4 == 0 && size % 4 == 0) {
      si_compute_do_clear_or_copy(sctx, dst, dst_offset, src, src_offset, size, NULL, 0, coher);
   } else {
      si_cp_dma_copy_buffer(sctx, dst, src, dst_offset, src_offset, size, 0, coher, cache_policy);
   }
}

void si_compute_copy_image(struct si_context *sctx, struct pipe_resource *dst, unsigned dst_level,
                           struct pipe_resource *src, unsigned src_level, unsigned dstx,
                           unsigned dsty, unsigned dstz, const struct pipe_box *src_box,
                           bool is_dcc_decompress)
{
   struct pipe_context *ctx = &sctx->b;
   unsigned width = src_box->width;
   unsigned height = src_box->height;
   unsigned depth = src_box->depth;
   enum pipe_format src_format = util_format_linear(src->format);
   enum pipe_format dst_format = util_format_linear(dst->format);
   bool is_linear = ((struct si_texture*)src)->surface.is_linear ||
                    ((struct si_texture*)dst)->surface.is_linear;

   assert(util_format_is_subsampled_422(src_format) == util_format_is_subsampled_422(dst_format));

   if (!vi_dcc_enabled((struct si_texture*)src, src_level) &&
       src_format == dst_format &&
       util_format_is_float(src_format) &&
       !util_format_is_compressed(src_format)) {
      /* Interpret as integer values to avoid NaN issues */
      switch(util_format_get_blocksizebits(src_format)) {
        case 16:
          src_format = dst_format = PIPE_FORMAT_R16_UINT;
          break;
        case 32:
          src_format = dst_format = PIPE_FORMAT_R32_UINT;
          break;
        case 64:
          src_format = dst_format = PIPE_FORMAT_R32G32_UINT;
          break;
        case 128:
          src_format = dst_format = PIPE_FORMAT_R32G32B32A32_UINT;
          break;
        default:
          assert(false);
      }
   }

   if (util_format_is_subsampled_422(src_format)) {
      src_format = dst_format = PIPE_FORMAT_R32_UINT;
      /* Interpreting 422 subsampled format (16 bpp) as 32 bpp
       * should force us to divide src_box->x, dstx and width by 2.
       * But given that ac_surface allocates this format as 32 bpp
       * and that surf_size is then modified to pack the values
       * we must keep the original values to get the correct results.
       */
   }

   if (width == 0 || height == 0)
      return;

   /* The driver doesn't decompress resources automatically here. */
   si_decompress_subresource(ctx, dst, PIPE_MASK_RGBAZS, dst_level, dstz,
                             dstz + src_box->depth - 1);
   si_decompress_subresource(ctx, src, PIPE_MASK_RGBAZS, src_level, src_box->z,
                             src_box->z + src_box->depth - 1);

   /* src and dst have the same number of samples. */
   si_make_CB_shader_coherent(sctx, src->nr_samples, true,
                              /* Only src can have DCC.*/
                              ((struct si_texture *)src)->surface.u.gfx9.dcc.pipe_aligned);

   struct pipe_constant_buffer saved_cb = {};

   struct si_images *images = &sctx->images[PIPE_SHADER_COMPUTE];
   struct pipe_image_view saved_image[2] = {0};
   util_copy_image_view(&saved_image[0], &images->views[0]);
   util_copy_image_view(&saved_image[1], &images->views[1]);

   void *saved_cs = sctx->cs_shader_state.program;

   if (!is_dcc_decompress) {
      unsigned data[] = {src_box->x, src_box->y, src_box->z, 0, dstx, dsty, dstz, 0};

      si_get_pipe_constant_buffer(sctx, PIPE_SHADER_COMPUTE, 0, &saved_cb);

      struct pipe_constant_buffer cb = {};
      cb.buffer_size = sizeof(data);
      cb.user_buffer = data;
      ctx->set_constant_buffer(ctx, PIPE_SHADER_COMPUTE, 0, false, &cb);
   }

   struct pipe_image_view image[2] = {0};
   image[0].resource = src;
   image[0].shader_access = image[0].access = PIPE_IMAGE_ACCESS_READ;
   image[0].format = src_format;
   image[0].u.tex.level = src_level;
   image[0].u.tex.first_layer = 0;
   image[0].u.tex.last_layer = src->target == PIPE_TEXTURE_3D ? u_minify(src->depth0, src_level) - 1
                                                              : (unsigned)(src->array_size - 1);
   image[1].resource = dst;
   image[1].shader_access = image[1].access = PIPE_IMAGE_ACCESS_WRITE;
   image[1].format = dst_format;
   image[1].u.tex.level = dst_level;
   image[1].u.tex.first_layer = 0;
   image[1].u.tex.last_layer = dst->target == PIPE_TEXTURE_3D ? u_minify(dst->depth0, dst_level) - 1
                                                              : (unsigned)(dst->array_size - 1);

   /* SNORM8 blitting has precision issues on some chips. Use the SINT
    * equivalent instead, which doesn't force DCC decompression.
    */
   if (util_format_is_snorm8(dst->format)) {
      image[0].format = image[1].format = util_format_snorm8_to_sint8(dst->format);
   }

   if (is_dcc_decompress)
      image[1].access |= SI_IMAGE_ACCESS_DCC_OFF;
   else if (sctx->chip_class >= GFX10)
      image[1].access |= SI_IMAGE_ACCESS_DCC_WRITE;

   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 2, 0, image);

   struct pipe_grid_info info = {0};

   if (is_dcc_decompress) {
      /* The DCC decompression is a normal blit where the load is compressed
       * and the store is uncompressed. The workgroup size is either equal to
       * the DCC block size or a multiple thereof. The shader uses a barrier
       * between loads and stores to safely overwrite each DCC block of pixels.
       */
      struct si_texture *tex = (struct si_texture*)src;
      unsigned dim[3] = {src_box->width, src_box->height, src_box->depth};

      assert(src == dst);
      assert(dst->target != PIPE_TEXTURE_1D && dst->target != PIPE_TEXTURE_1D_ARRAY);

      if (!sctx->cs_dcc_decompress)
         sctx->cs_dcc_decompress = si_create_dcc_decompress_cs(ctx);
      ctx->bind_compute_state(ctx, sctx->cs_dcc_decompress);

      info.block[0] = tex->surface.u.gfx9.dcc_block_width;
      info.block[1] = tex->surface.u.gfx9.dcc_block_height;
      info.block[2] = tex->surface.u.gfx9.dcc_block_depth;

      /* Make sure the block size is at least the same as wave size. */
      while (info.block[0] * info.block[1] * info.block[2] <
             sctx->screen->compute_wave_size) {
         info.block[0] *= 2;
      }

      for (unsigned i = 0; i < 3; i++) {
         info.last_block[i] = dim[i] % info.block[i];
         info.grid[i] = DIV_ROUND_UP(dim[i], info.block[i]);
      }
   } else if (dst->target == PIPE_TEXTURE_1D_ARRAY && src->target == PIPE_TEXTURE_1D_ARRAY) {
      if (!sctx->cs_copy_image_1d_array)
         sctx->cs_copy_image_1d_array = si_create_copy_image_compute_shader_1d_array(ctx);
      ctx->bind_compute_state(ctx, sctx->cs_copy_image_1d_array);
      info.block[0] = 64;
      info.last_block[0] = width % 64;
      info.block[1] = 1;
      info.block[2] = 1;
      info.grid[0] = DIV_ROUND_UP(width, 64);
      info.grid[1] = depth;
      info.grid[2] = 1;
   } else {
      if (!sctx->cs_copy_image)
         sctx->cs_copy_image = si_create_copy_image_compute_shader(ctx);
      ctx->bind_compute_state(ctx, sctx->cs_copy_image);

      /* This is better for access over PCIe. */
      if (is_linear) {
         info.block[0] = 64;
         info.block[1] = 1;
      } else {
         info.block[0] = 8;
         info.block[1] = 8;
      }
      info.last_block[0] = width % info.block[0];
      info.last_block[1] = height % info.block[1];
      info.block[2] = 1;
      info.grid[0] = DIV_ROUND_UP(width, info.block[0]);
      info.grid[1] = DIV_ROUND_UP(height, info.block[1]);
      info.grid[2] = depth;
   }

   si_launch_grid_internal(sctx, &info, saved_cs,
                           SI_CS_WAIT_FOR_IDLE | SI_CS_IMAGE_OP);

   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 2, 0, saved_image);
   for (int i = 0; i < 2; i++)
      pipe_resource_reference(&saved_image[i].resource, NULL);
   if (!is_dcc_decompress) {
      ctx->set_constant_buffer(ctx, PIPE_SHADER_COMPUTE, 0, true, &saved_cb);
   }
}

void si_retile_dcc(struct si_context *sctx, struct si_texture *tex)
{
   struct pipe_context *ctx = &sctx->b;

   sctx->flags |= si_get_flush_flags(sctx, SI_COHERENCY_CB_META, L2_LRU);

   /* Save states. */
   void *saved_cs = sctx->cs_shader_state.program;
   struct pipe_image_view saved_img[3] = {};

   for (unsigned i = 0; i < 3; i++) {
      util_copy_image_view(&saved_img[i], &sctx->images[PIPE_SHADER_COMPUTE].views[i]);
   }

   /* Set images. */
   bool use_uint16 = tex->surface.u.gfx9.dcc_retile_use_uint16;
   unsigned num_elements = tex->surface.u.gfx9.dcc_retile_num_elements;
   struct pipe_image_view img[3];

   assert(tex->dcc_retile_buffer);
   assert(tex->surface.dcc_offset && tex->surface.dcc_offset <= UINT_MAX);
   assert(tex->surface.display_dcc_offset && tex->surface.display_dcc_offset <= UINT_MAX);

   for (unsigned i = 0; i < 3; i++) {
      img[i].resource = i == 0 ? &tex->dcc_retile_buffer->b.b : &tex->buffer.b.b;
      img[i].access = i == 2 ? PIPE_IMAGE_ACCESS_WRITE : PIPE_IMAGE_ACCESS_READ;
      img[i].shader_access = SI_IMAGE_ACCESS_AS_BUFFER;
   }

   img[0].format = use_uint16 ? PIPE_FORMAT_R16G16B16A16_UINT : PIPE_FORMAT_R32G32B32A32_UINT;
   img[0].u.buf.offset = 0;
   img[0].u.buf.size = ac_surface_get_retile_map_size(&tex->surface);

   img[1].format = PIPE_FORMAT_R8_UINT;
   img[1].u.buf.offset = tex->surface.dcc_offset;
   img[1].u.buf.size = tex->surface.dcc_size;

   img[2].format = PIPE_FORMAT_R8_UINT;
   img[2].u.buf.offset = tex->surface.display_dcc_offset;
   img[2].u.buf.size = tex->surface.u.gfx9.display_dcc_size;

   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 3, 0, img);

   /* Bind the compute shader. */
   if (!sctx->cs_dcc_retile)
      sctx->cs_dcc_retile = si_create_dcc_retile_cs(ctx);
   ctx->bind_compute_state(ctx, sctx->cs_dcc_retile);

   /* Dispatch compute. */
   /* img[0] has 4 channels per element containing 2 pairs of DCC offsets. */
   unsigned num_threads = num_elements / 4;

   struct pipe_grid_info info = {};
   info.block[0] = 64;
   info.block[1] = 1;
   info.block[2] = 1;
   info.grid[0] = DIV_ROUND_UP(num_threads, 64); /* includes the partial block */
   info.grid[1] = 1;
   info.grid[2] = 1;
   info.last_block[0] = num_threads % 64;

   si_launch_grid_internal(sctx, &info, saved_cs, 0);

   /* Don't flush caches or wait. The driver will wait at the end of this IB,
    * and L2 will be flushed by the kernel fence.
    */

   /* Restore states. */
   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 3, 0, saved_img);

   for (unsigned i = 0; i < 3; i++) {
      pipe_resource_reference(&saved_img[i].resource, NULL);
   }
}

/* Expand FMASK to make it identity, so that image stores can ignore it. */
void si_compute_expand_fmask(struct pipe_context *ctx, struct pipe_resource *tex)
{
   struct si_context *sctx = (struct si_context *)ctx;
   bool is_array = tex->target == PIPE_TEXTURE_2D_ARRAY;
   unsigned log_fragments = util_logbase2(tex->nr_storage_samples);
   unsigned log_samples = util_logbase2(tex->nr_samples);
   assert(tex->nr_samples >= 2);

   /* EQAA FMASK expansion is unimplemented. */
   if (tex->nr_samples != tex->nr_storage_samples)
      return;

   si_make_CB_shader_coherent(sctx, tex->nr_samples, true,
                              true /* DCC is not possible with image stores */);

   /* Save states. */
   void *saved_cs = sctx->cs_shader_state.program;
   struct pipe_image_view saved_image = {0};
   util_copy_image_view(&saved_image, &sctx->images[PIPE_SHADER_COMPUTE].views[0]);

   /* Bind the image. */
   struct pipe_image_view image = {0};
   image.resource = tex;
   /* Don't set WRITE so as not to trigger FMASK expansion, causing
    * an infinite loop. */
   image.shader_access = image.access = PIPE_IMAGE_ACCESS_READ;
   image.format = util_format_linear(tex->format);
   if (is_array)
      image.u.tex.last_layer = tex->array_size - 1;

   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 1, 0, &image);

   /* Bind the shader. */
   void **shader = &sctx->cs_fmask_expand[log_samples - 1][is_array];
   if (!*shader)
      *shader = si_create_fmask_expand_cs(ctx, tex->nr_samples, is_array);
   ctx->bind_compute_state(ctx, *shader);

   /* Dispatch compute. */
   struct pipe_grid_info info = {0};
   info.block[0] = 8;
   info.last_block[0] = tex->width0 % 8;
   info.block[1] = 8;
   info.last_block[1] = tex->height0 % 8;
   info.block[2] = 1;
   info.grid[0] = DIV_ROUND_UP(tex->width0, 8);
   info.grid[1] = DIV_ROUND_UP(tex->height0, 8);
   info.grid[2] = is_array ? tex->array_size : 1;

   si_launch_grid_internal(sctx, &info, saved_cs,
                           SI_CS_WAIT_FOR_IDLE | SI_CS_IMAGE_OP);

   /* Restore previous states. */
   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 1, 0, &saved_image);
   pipe_resource_reference(&saved_image.resource, NULL);

   /* Array of fully expanded FMASK values, arranged by [log2(fragments)][log2(samples)-1]. */
#define INVALID 0 /* never used */
   static const uint64_t fmask_expand_values[][4] = {
      /* samples */
      /* 2 (8 bpp) 4 (8 bpp)   8 (8-32bpp) 16 (16-64bpp)      fragments */
      {0x02020202, 0x0E0E0E0E, 0xFEFEFEFE, 0xFFFEFFFE},      /* 1 */
      {0x02020202, 0xA4A4A4A4, 0xAAA4AAA4, 0xAAAAAAA4},      /* 2 */
      {INVALID, 0xE4E4E4E4, 0x44443210, 0x4444444444443210}, /* 4 */
      {INVALID, INVALID, 0x76543210, 0x8888888876543210},    /* 8 */
   };

   /* Clear FMASK to identity. */
   struct si_texture *stex = (struct si_texture *)tex;
   si_clear_buffer(sctx, tex, stex->surface.fmask_offset, stex->surface.fmask_size,
                   (uint32_t *)&fmask_expand_values[log_fragments][log_samples - 1],
                   log_fragments >= 2 && log_samples == 4 ? 8 : 4,
                   SI_COHERENCY_SHADER, SI_AUTO_SELECT_CLEAR_METHOD);
}

void si_init_compute_blit_functions(struct si_context *sctx)
{
   sctx->b.clear_buffer = si_pipe_clear_buffer;
}

/* Clear a region of a color surface to a constant value. */
void si_compute_clear_render_target(struct pipe_context *ctx, struct pipe_surface *dstsurf,
                                    const union pipe_color_union *color, unsigned dstx,
                                    unsigned dsty, unsigned width, unsigned height,
                                    bool render_condition_enabled)
{
   struct si_context *sctx = (struct si_context *)ctx;
   unsigned num_layers = dstsurf->u.tex.last_layer - dstsurf->u.tex.first_layer + 1;
   unsigned data[4 + sizeof(color->ui)] = {dstx, dsty, dstsurf->u.tex.first_layer, 0};

   if (width == 0 || height == 0)
      return;

   /* The driver doesn't decompress resources automatically here. */
   si_decompress_subresource(ctx, dstsurf->texture, PIPE_MASK_RGBA, dstsurf->u.tex.level,
                             dstsurf->u.tex.first_layer, dstsurf->u.tex.last_layer);

   if (util_format_is_srgb(dstsurf->format)) {
      union pipe_color_union color_srgb;
      for (int i = 0; i < 3; i++)
         color_srgb.f[i] = util_format_linear_to_srgb_float(color->f[i]);
      color_srgb.f[3] = color->f[3];
      memcpy(data + 4, color_srgb.ui, sizeof(color->ui));
   } else {
      memcpy(data + 4, color->ui, sizeof(color->ui));
   }

   si_make_CB_shader_coherent(sctx, dstsurf->texture->nr_samples, true,
                              true /* DCC is not possible with image stores */);

   struct pipe_constant_buffer saved_cb = {};
   si_get_pipe_constant_buffer(sctx, PIPE_SHADER_COMPUTE, 0, &saved_cb);

   struct si_images *images = &sctx->images[PIPE_SHADER_COMPUTE];
   struct pipe_image_view saved_image = {0};
   util_copy_image_view(&saved_image, &images->views[0]);

   void *saved_cs = sctx->cs_shader_state.program;

   struct pipe_constant_buffer cb = {};
   cb.buffer_size = sizeof(data);
   cb.user_buffer = data;
   ctx->set_constant_buffer(ctx, PIPE_SHADER_COMPUTE, 0, false, &cb);

   struct pipe_image_view image = {0};
   image.resource = dstsurf->texture;
   image.shader_access = image.access = PIPE_IMAGE_ACCESS_WRITE;
   image.format = util_format_linear(dstsurf->format);
   image.u.tex.level = dstsurf->u.tex.level;
   image.u.tex.first_layer = 0; /* 3D images ignore first_layer (BASE_ARRAY) */
   image.u.tex.last_layer = dstsurf->u.tex.last_layer;

   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 1, 0, &image);

   struct pipe_grid_info info = {0};

   if (dstsurf->texture->target != PIPE_TEXTURE_1D_ARRAY) {
      if (!sctx->cs_clear_render_target)
         sctx->cs_clear_render_target = si_clear_render_target_shader(ctx);
      ctx->bind_compute_state(ctx, sctx->cs_clear_render_target);
      info.block[0] = 8;
      info.last_block[0] = width % 8;
      info.block[1] = 8;
      info.last_block[1] = height % 8;
      info.block[2] = 1;
      info.grid[0] = DIV_ROUND_UP(width, 8);
      info.grid[1] = DIV_ROUND_UP(height, 8);
      info.grid[2] = num_layers;
   } else {
      if (!sctx->cs_clear_render_target_1d_array)
         sctx->cs_clear_render_target_1d_array = si_clear_render_target_shader_1d_array(ctx);
      ctx->bind_compute_state(ctx, sctx->cs_clear_render_target_1d_array);
      info.block[0] = 64;
      info.last_block[0] = width % 64;
      info.block[1] = 1;
      info.block[2] = 1;
      info.grid[0] = DIV_ROUND_UP(width, 64);
      info.grid[1] = num_layers;
      info.grid[2] = 1;
   }

   si_launch_grid_internal(sctx, &info, saved_cs,
                           SI_CS_WAIT_FOR_IDLE | SI_CS_IMAGE_OP |
                           (render_condition_enabled ? SI_CS_RENDER_COND_ENABLE : 0));

   ctx->set_shader_images(ctx, PIPE_SHADER_COMPUTE, 0, 1, 0, &saved_image);
   ctx->set_constant_buffer(ctx, PIPE_SHADER_COMPUTE, 0, true, &saved_cb);
   pipe_resource_reference(&saved_image.resource, NULL);
}
