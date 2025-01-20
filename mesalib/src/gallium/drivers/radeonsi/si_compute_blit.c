/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "util/format/u_format.h"
#include "util/format_srgb.h"
#include "util/helpers.h"
#include "util/hash_table.h"
#include "util/u_pack_color.h"
#include "ac_nir_meta.h"

static void si_compute_begin_internal(struct si_context *sctx, bool render_condition_enabled)
{
   sctx->barrier_flags &= ~SI_BARRIER_EVENT_PIPELINESTAT_START;
   if (sctx->num_hw_pipestat_streamout_queries) {
      sctx->barrier_flags |= SI_BARRIER_EVENT_PIPELINESTAT_STOP;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
   }

   if (!render_condition_enabled)
      sctx->render_cond_enabled = false;

   /* Force-disable fbfetch because there are unsolvable recursion problems. */
   si_force_disable_ps_colorbuf0_slot(sctx);

   /* Skip decompression to prevent infinite recursion. */
   sctx->blitter_running = true;
}

static void si_compute_end_internal(struct si_context *sctx)
{
   sctx->barrier_flags &= ~SI_BARRIER_EVENT_PIPELINESTAT_STOP;
   if (sctx->num_hw_pipestat_streamout_queries) {
      sctx->barrier_flags |= SI_BARRIER_EVENT_PIPELINESTAT_START;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
   }

   sctx->render_cond_enabled = sctx->render_cond;
   sctx->blitter_running = false;

   /* We force-disabled fbfetch, so recompute the state. */
   si_update_ps_colorbuf0_slot(sctx);
}

static void si_launch_grid_internal(struct si_context *sctx, const struct pipe_grid_info *info,
                                    void *shader)
{
   void *saved_cs = sctx->cs_shader_state.program;
   sctx->b.bind_compute_state(&sctx->b, shader);
   sctx->b.launch_grid(&sctx->b, info);
   sctx->b.bind_compute_state(&sctx->b, saved_cs);
}

void si_launch_grid_internal_ssbos(struct si_context *sctx, struct pipe_grid_info *info,
                                   void *shader, unsigned num_buffers,
                                   const struct pipe_shader_buffer *buffers,
                                   unsigned writeable_bitmask, bool render_condition_enable)
{
   /* Save states. */
   struct pipe_shader_buffer saved_sb[3] = {};
   assert(num_buffers <= ARRAY_SIZE(saved_sb));
   si_get_shader_buffers(sctx, PIPE_SHADER_COMPUTE, 0, num_buffers, saved_sb);

   unsigned saved_writable_mask = 0;
   for (unsigned i = 0; i < num_buffers; i++) {
      if (sctx->const_and_shader_buffers[PIPE_SHADER_COMPUTE].writable_mask &
          (1u << si_get_shaderbuf_slot(i)))
         saved_writable_mask |= 1 << i;
   }

   /* Bind buffers and launch compute. */
   si_set_shader_buffers(&sctx->b, PIPE_SHADER_COMPUTE, 0, num_buffers, buffers,
                         writeable_bitmask,
                         true /* don't update bind_history to prevent unnecessary syncs later */);

   si_compute_begin_internal(sctx, render_condition_enable);
   si_launch_grid_internal(sctx, info, shader);
   si_compute_end_internal(sctx);

   /* Restore states. */
   sctx->b.set_shader_buffers(&sctx->b, PIPE_SHADER_COMPUTE, 0, num_buffers, saved_sb,
                              saved_writable_mask);
   for (int i = 0; i < num_buffers; i++)
      pipe_resource_reference(&saved_sb[i].buffer, NULL);
}

static unsigned
set_work_size(struct pipe_grid_info *info, unsigned block_x, unsigned block_y, unsigned block_z,
              unsigned work_x, unsigned work_y, unsigned work_z)
{
   info->block[0] = block_x;
   info->block[1] = block_y;
   info->block[2] = block_z;

   unsigned work[3] = {work_x, work_y, work_z};
   for (int i = 0; i < 3; ++i) {
      info->last_block[i] = work[i] % info->block[i];
      info->grid[i] = DIV_ROUND_UP(work[i], info->block[i]);
   }

   return work_z > 1 ? 3 : (work_y > 1 ? 2 : 1);
}

/**
 * Clear a buffer using read-modify-write with a 32-bit write bitmask.
 * The clear value has 32 bits.
 */
void si_compute_clear_buffer_rmw(struct si_context *sctx, struct pipe_resource *dst,
                                 unsigned dst_offset, unsigned size, uint32_t clear_value,
                                 uint32_t writebitmask, bool render_condition_enable)
{
   assert(dst_offset % 4 == 0);
   assert(size % 4 == 0);

   assert(dst->target != PIPE_BUFFER || dst_offset + size <= dst->width0);

   /* Use buffer_load_dwordx4 and buffer_store_dwordx4 per thread. */
   unsigned dwords_per_thread = 4;
   unsigned num_threads = DIV_ROUND_UP(size, dwords_per_thread * 4);

   struct pipe_grid_info info = {};
   set_work_size(&info, 64, 1, 1, num_threads, 1, 1);

   struct pipe_shader_buffer sb = {};
   sb.buffer = dst;
   sb.buffer_offset = dst_offset;
   sb.buffer_size = size;

   sctx->cs_user_data[0] = clear_value & writebitmask;
   sctx->cs_user_data[1] = ~writebitmask;

   if (!sctx->cs_clear_buffer_rmw)
      sctx->cs_clear_buffer_rmw = si_create_clear_buffer_rmw_cs(sctx);

   si_launch_grid_internal_ssbos(sctx, &info, sctx->cs_clear_buffer_rmw, 1, &sb, 0x1,
                                 render_condition_enable);
}

/**
 * This implements a clear/copy_buffer compute shader allowing an arbitrary src_offset, dst_offset,
 * and size alignment, so that it can be used as a complete replacement for the typically slower
 * CP DMA.
 *
 * It stores 16B blocks per thread aligned to a 16B offset just like a 16B-aligned clear/copy,
 * and it byte-shifts src data by the amount of both src and dst misalignment to get the behavior
 * of a totally unaligned clear/copy.
 *
 * The first and last thread can store less than 16B (up to 1B store granularity) depending on how
 * much dst is unaligned.
 */
bool si_compute_clear_copy_buffer(struct si_context *sctx, struct pipe_resource *dst,
                                  unsigned dst_offset, struct pipe_resource *src,
                                  unsigned src_offset, unsigned size,
                                  const uint32_t *clear_value, unsigned clear_value_size,
                                  unsigned dwords_per_thread, bool render_condition_enable,
                                  bool fail_if_slow)
{
   assert(dst->target != PIPE_BUFFER || dst_offset + size <= dst->width0);
   assert(!src || src_offset + size <= src->width0);
   bool is_copy = src != NULL;

   struct ac_cs_clear_copy_buffer_options options = {
      .nir_options = sctx->screen->nir_options,
      .info = &sctx->screen->info,
      .print_key = si_can_dump_shader(sctx->screen, MESA_SHADER_COMPUTE, SI_DUMP_SHADER_KEY),
      .fail_if_slow = fail_if_slow,
   };

   struct ac_cs_clear_copy_buffer_info info = {
      .dst_offset = dst_offset,
      .src_offset = src_offset,
      .size = size,
      .clear_value_size = is_copy ? 0 : clear_value_size,
      .dwords_per_thread = dwords_per_thread,
      .render_condition_enabled = render_condition_enable,
      .dst_is_vram = si_resource(dst)->domains & RADEON_DOMAIN_VRAM,
      .src_is_vram = src && si_resource(src)->domains & RADEON_DOMAIN_VRAM,
      .src_is_sparse = src && src->flags & PIPE_RESOURCE_FLAG_SPARSE,
   };
   memcpy(info.clear_value, clear_value, clear_value_size);

   struct ac_cs_clear_copy_buffer_dispatch dispatch;

   if (!ac_prepare_cs_clear_copy_buffer(&options, &info, &dispatch))
      return false;

   struct pipe_shader_buffer sb[2] = {};
   for (unsigned i = 0; i < 2; i++) {
      sb[i].buffer_offset = dispatch.ssbo[i].offset;
      sb[i].buffer_size = dispatch.ssbo[i].size;
   }

   if (is_copy)
      sb[0].buffer = src;
   sb[is_copy].buffer = dst;

   void *shader = _mesa_hash_table_u64_search(sctx->cs_dma_shaders, dispatch.shader_key.key);
   if (!shader) {
      shader = si_create_shader_state(sctx, ac_create_clear_copy_buffer_cs(&options,
                                                                           &dispatch.shader_key));
      _mesa_hash_table_u64_insert(sctx->cs_dma_shaders, dispatch.shader_key.key, shader);
   }

   memcpy(sctx->cs_user_data, dispatch.user_data, sizeof(dispatch.user_data));

   struct pipe_grid_info grid = {};
   set_work_size(&grid, dispatch.workgroup_size, 1, 1, dispatch.num_threads, 1, 1);

   si_launch_grid_internal_ssbos(sctx, &grid, shader, dispatch.num_ssbos, sb,
                                 is_copy ? 0x2 : 0x1, render_condition_enable);
   return true;
}

void si_clear_buffer(struct si_context *sctx, struct pipe_resource *dst,
                     uint64_t offset, uint64_t size, uint32_t *clear_value,
                     uint32_t clear_value_size, enum si_clear_method method,
                     bool render_condition_enable)
{
   if (!size)
      return;

   ASSERTED unsigned clear_alignment = MIN2(clear_value_size, 4);

   assert(clear_value_size != 3 && clear_value_size != 6); /* 12 is allowed. */
   assert(offset % clear_alignment == 0);
   assert(size % clear_alignment == 0);
   assert(offset < (UINT32_MAX & ~0x3)); /* the limit of pipe_shader_buffer::buffer_size */
   assert(align(size, 16) < UINT32_MAX); /* we round up the size to 16 for compute */

   uint32_t clamped;
   if (util_lower_clearsize_to_dword(clear_value, (int*)&clear_value_size, &clamped))
      clear_value = &clamped;

   if (si_compute_clear_copy_buffer(sctx, dst, offset, NULL, 0, size, clear_value,
                                    clear_value_size, 0, render_condition_enable,
                                    method == SI_AUTO_SELECT_CLEAR_METHOD))
      return;

   /* Compute handles all unaligned sizes, so this is always aligned. */
   assert(offset % 4 == 0 && size % 4 == 0 && clear_value_size == 4);
   assert(!render_condition_enable);

   si_cp_dma_clear_buffer(sctx, &sctx->gfx_cs, dst, offset, size, *clear_value);
}

static void si_pipe_clear_buffer(struct pipe_context *ctx, struct pipe_resource *dst,
                                 unsigned offset, unsigned size, const void *clear_value,
                                 int clear_value_size)
{
   struct si_context *sctx = (struct si_context *)ctx;

   si_barrier_before_simple_buffer_op(sctx, 0, dst, NULL);
   si_clear_buffer(sctx, dst, offset, size, (uint32_t *)clear_value, clear_value_size,
                   SI_AUTO_SELECT_CLEAR_METHOD, false);
   si_barrier_after_simple_buffer_op(sctx, 0, dst, NULL);
}

void si_copy_buffer(struct si_context *sctx, struct pipe_resource *dst, struct pipe_resource *src,
                    uint64_t dst_offset, uint64_t src_offset, unsigned size)
{
   if (!size)
      return;

   if (si_compute_clear_copy_buffer(sctx, dst, dst_offset, src, src_offset, size, NULL, 0, 0,
                                    false, true))
      return;

   si_cp_dma_copy_buffer(sctx, dst, src, dst_offset, src_offset, size);
}

void si_compute_shorten_ubyte_buffer(struct si_context *sctx, struct pipe_resource *dst, struct pipe_resource *src,
                                     uint64_t dst_offset, uint64_t src_offset, unsigned count,
                                     bool render_condition_enable)
{
   if (!count)
      return;

   if (!sctx->cs_ubyte_to_ushort)
      sctx->cs_ubyte_to_ushort = si_create_ubyte_to_ushort_compute_shader(sctx);

   struct pipe_grid_info info = {};
   set_work_size(&info, 64, 1, 1, count, 1, 1);

   struct pipe_shader_buffer sb[2] = {};
   sb[0].buffer = dst;
   sb[0].buffer_offset = dst_offset;
   sb[0].buffer_size = count * 2;

   sb[1].buffer = src;
   sb[1].buffer_offset = src_offset;
   sb[1].buffer_size = count;

   si_launch_grid_internal_ssbos(sctx, &info, sctx->cs_ubyte_to_ushort, 2, sb, 0x1,
                                 render_condition_enable);
}

static void si_compute_save_and_bind_images(struct si_context *sctx, unsigned num_images,
                                            struct pipe_image_view *images,
                                            struct pipe_image_view *saved_images)
{
   for (unsigned i = 0; i < num_images; i++) {
      assert(sctx->b.screen->is_format_supported(sctx->b.screen, images[i].format,
                                                 images[i].resource->target,
                                                 images[i].resource->nr_samples,
                                                 images[i].resource->nr_storage_samples,
                                                 PIPE_BIND_SHADER_IMAGE));

      /* Always allow DCC stores on gfx10+. */
      if (sctx->gfx_level >= GFX10 &&
          images[i].access & PIPE_IMAGE_ACCESS_WRITE &&
          !(images[i].access & SI_IMAGE_ACCESS_DCC_OFF))
         images[i].access |= SI_IMAGE_ACCESS_ALLOW_DCC_STORE;

      /* Simplify the format according to what image stores support. */
      if (images[i].access & PIPE_IMAGE_ACCESS_WRITE) {
         images[i].format = util_format_linear(images[i].format); /* SRGB not supported */
         /* Keep L8A8 formats as-is because GFX7 is unable to store into R8A8 for some reason. */
         images[i].format = util_format_intensity_to_red(images[i].format);
         images[i].format = util_format_rgbx_to_rgba(images[i].format); /* prevent partial writes */
      }

      /* Save the image. */
      util_copy_image_view(&saved_images[i], &sctx->images[PIPE_SHADER_COMPUTE].views[i]);
   }

   /* This must be before the barrier and si_compute_begin_internal because it might invoke DCC
    * decompression.
    */
   sctx->b.set_shader_images(&sctx->b, PIPE_SHADER_COMPUTE, 0, num_images, 0, images);
}

static void si_compute_restore_images(struct si_context *sctx, unsigned num_images,
                                      struct pipe_image_view *saved_images)
{
   sctx->b.set_shader_images(&sctx->b, PIPE_SHADER_COMPUTE, 0, num_images, 0, saved_images);
   for (unsigned i = 0; i < num_images; i++)
      pipe_resource_reference(&saved_images[i].resource, NULL);
}

void si_retile_dcc(struct si_context *sctx, struct si_texture *tex)
{
   assert(sctx->gfx_level < GFX12);

   /* Flush and wait for CB before retiling DCC. */
   sctx->barrier_flags |= SI_BARRIER_SYNC_AND_INV_CB;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);

   /* Set the DCC buffer. */
   assert(tex->surface.meta_offset && tex->surface.meta_offset <= UINT_MAX);
   assert(tex->surface.display_dcc_offset && tex->surface.display_dcc_offset <= UINT_MAX);
   assert(tex->surface.display_dcc_offset < tex->surface.meta_offset);
   assert(tex->buffer.bo_size <= UINT_MAX);

   struct pipe_shader_buffer sb = {};
   sb.buffer = &tex->buffer.b.b;
   sb.buffer_offset = tex->surface.display_dcc_offset;
   sb.buffer_size = tex->buffer.bo_size - sb.buffer_offset;

   sctx->cs_user_data[0] = tex->surface.meta_offset - tex->surface.display_dcc_offset;
   sctx->cs_user_data[1] = (tex->surface.u.gfx9.color.dcc_pitch_max + 1) |
                           (tex->surface.u.gfx9.color.dcc_height << 16);
   sctx->cs_user_data[2] = (tex->surface.u.gfx9.color.display_dcc_pitch_max + 1) |
                           (tex->surface.u.gfx9.color.display_dcc_height << 16);

   /* We have only 1 variant per bpp for now, so expect 32 bpp. */
   assert(tex->surface.bpe == 4);

   void **shader = &sctx->cs_dcc_retile[tex->surface.u.gfx9.swizzle_mode];
   if (!*shader)
      *shader = si_create_dcc_retile_cs(sctx, &tex->surface);

   /* Dispatch compute. */
   unsigned width = DIV_ROUND_UP(tex->buffer.b.b.width0, tex->surface.u.gfx9.color.dcc_block_width);
   unsigned height = DIV_ROUND_UP(tex->buffer.b.b.height0, tex->surface.u.gfx9.color.dcc_block_height);

   struct pipe_grid_info info = {};
   set_work_size(&info, 8, 8, 1, width, height, 1);

   si_barrier_before_simple_buffer_op(sctx, 0, sb.buffer, NULL);
   si_launch_grid_internal_ssbos(sctx, &info, *shader, 1, &sb, 0x1, false);
   si_barrier_after_simple_buffer_op(sctx, 0, sb.buffer, NULL);

   /* Don't flush caches. L2 will be flushed by the kernel fence. */
}

void gfx9_clear_dcc_msaa(struct si_context *sctx, struct pipe_resource *res, uint32_t clear_value,
                         bool render_condition_enable)
{
   struct si_texture *tex = (struct si_texture*)res;

   assert(sctx->gfx_level < GFX11);

   /* Set the DCC buffer. */
   assert(tex->surface.meta_offset && tex->surface.meta_offset <= UINT_MAX);
   assert(tex->buffer.bo_size <= UINT_MAX);

   struct pipe_shader_buffer sb = {};
   sb.buffer = &tex->buffer.b.b;
   sb.buffer_offset = tex->surface.meta_offset;
   sb.buffer_size = tex->buffer.bo_size - sb.buffer_offset;

   sctx->cs_user_data[0] = (tex->surface.u.gfx9.color.dcc_pitch_max + 1) |
                           (tex->surface.u.gfx9.color.dcc_height << 16);
   sctx->cs_user_data[1] = (clear_value & 0xffff) |
                           ((uint32_t)tex->surface.tile_swizzle << 16);

   /* These variables identify the shader variant. */
   unsigned swizzle_mode = tex->surface.u.gfx9.swizzle_mode;
   unsigned bpe_log2 = util_logbase2(tex->surface.bpe);
   unsigned log2_samples = util_logbase2(tex->buffer.b.b.nr_samples);
   bool fragments8 = tex->buffer.b.b.nr_storage_samples == 8;
   bool is_array = tex->buffer.b.b.array_size > 1;
   void **shader = &sctx->cs_clear_dcc_msaa[swizzle_mode][bpe_log2][fragments8][log2_samples - 2][is_array];

   if (!*shader)
      *shader = gfx9_create_clear_dcc_msaa_cs(sctx, tex);

   /* Dispatch compute. */
   unsigned width = DIV_ROUND_UP(tex->buffer.b.b.width0, tex->surface.u.gfx9.color.dcc_block_width);
   unsigned height = DIV_ROUND_UP(tex->buffer.b.b.height0, tex->surface.u.gfx9.color.dcc_block_height);
   unsigned depth = DIV_ROUND_UP(tex->buffer.b.b.array_size, tex->surface.u.gfx9.color.dcc_block_depth);

   struct pipe_grid_info info = {};
   set_work_size(&info, 8, 8, 1, width, height, depth);

   si_launch_grid_internal_ssbos(sctx, &info, *shader, 1, &sb, 0x1, render_condition_enable);
}

/* Expand FMASK to make it identity, so that image stores can ignore it. */
void si_compute_expand_fmask(struct pipe_context *ctx, struct pipe_resource *tex)
{
   struct si_context *sctx = (struct si_context *)ctx;
   bool is_array = tex->target == PIPE_TEXTURE_2D_ARRAY;
   unsigned log_fragments = util_logbase2(tex->nr_storage_samples);
   unsigned log_samples = util_logbase2(tex->nr_samples);
   assert(tex->nr_samples >= 2);

   assert(sctx->gfx_level < GFX11);

   /* EQAA FMASK expansion is unimplemented. */
   if (tex->nr_samples != tex->nr_storage_samples)
      return;

   si_make_CB_shader_coherent(sctx, tex->nr_samples, true,
                              ((struct si_texture*)tex)->surface.u.gfx9.color.dcc.pipe_aligned);

   /* Save states. */
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
      *shader = si_create_fmask_expand_cs(sctx, tex->nr_samples, is_array);

   /* Dispatch compute. */
   struct pipe_grid_info info = {0};
   set_work_size(&info, 8, 8, 1, tex->width0, tex->height0, is_array ? tex->array_size : 1);

   si_barrier_before_internal_op(sctx, 0, 0, NULL, 0, 1, &image);
   si_compute_begin_internal(sctx, false);
   si_launch_grid_internal(sctx, &info, *shader);
   si_compute_end_internal(sctx);
   si_barrier_after_internal_op(sctx, 0, 0, NULL, 0, 1, &image);

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
                   SI_AUTO_SELECT_CLEAR_METHOD, false);
   si_barrier_after_simple_buffer_op(sctx, 0, tex, NULL);
}

void si_compute_clear_image_dcc_single(struct si_context *sctx, struct si_texture *tex,
                                       unsigned level, enum pipe_format format,
                                       const union pipe_color_union *color,
                                       bool render_condition_enable)
{
   assert(sctx->gfx_level >= GFX11); /* not believed to be useful on gfx10 */
   unsigned dcc_block_width = tex->surface.u.gfx9.color.dcc_block_width;
   unsigned dcc_block_height = tex->surface.u.gfx9.color.dcc_block_height;
   unsigned width = DIV_ROUND_UP(u_minify(tex->buffer.b.b.width0, level), dcc_block_width);
   unsigned height = DIV_ROUND_UP(u_minify(tex->buffer.b.b.height0, level), dcc_block_height);
   unsigned depth = util_num_layers(&tex->buffer.b.b, level);
   bool is_msaa = tex->buffer.b.b.nr_samples >= 2;

   struct pipe_image_view image = {0};
   image.resource = &tex->buffer.b.b;
   image.shader_access = image.access = PIPE_IMAGE_ACCESS_WRITE | SI_IMAGE_ACCESS_DCC_OFF;
   image.format = format;
   image.u.tex.level = level;
   image.u.tex.last_layer = depth - 1;

   if (util_format_is_srgb(format)) {
      union pipe_color_union color_srgb;
      for (int i = 0; i < 3; i++)
         color_srgb.f[i] = util_format_linear_to_srgb_float(color->f[i]);
      color_srgb.f[3] = color->f[3];
      memcpy(sctx->cs_user_data, color_srgb.ui, sizeof(color->ui));
   } else {
      memcpy(sctx->cs_user_data, color->ui, sizeof(color->ui));
   }

   sctx->cs_user_data[4] = dcc_block_width | (dcc_block_height << 16);

   struct pipe_grid_info info = {0};
   unsigned wg_dim = set_work_size(&info, 8, 8, 1, width, height, depth);

   void **shader = &sctx->cs_clear_image_dcc_single[is_msaa][wg_dim];
   if (!*shader)
      *shader = si_clear_image_dcc_single_shader(sctx, is_msaa, wg_dim);

   struct pipe_image_view saved_image = {};

   si_compute_save_and_bind_images(sctx, 1, &image, &saved_image);
   si_compute_begin_internal(sctx, render_condition_enable);
   si_launch_grid_internal(sctx, &info, *shader);
   si_compute_end_internal(sctx);
   si_compute_restore_images(sctx, 1, &saved_image);
}

void si_init_compute_blit_functions(struct si_context *sctx)
{
   sctx->b.clear_buffer = si_pipe_clear_buffer;
}

bool si_should_blit_clamp_to_edge(const struct pipe_blit_info *info, unsigned coord_mask)
{
   return util_is_box_out_of_bounds(&info->src.box, coord_mask, info->src.resource->width0,
                                    info->src.resource->height0, info->src.level);
}

bool si_compute_clear_image(struct si_context *sctx, struct pipe_resource *tex,
                            enum pipe_format format, unsigned level, const struct pipe_box *box,
                            const union pipe_color_union *color, bool render_condition_enable,
                            bool fail_if_slow)
{
   unsigned access = 0;

   struct pipe_blit_info info;
   memset(&info, 0, sizeof(info));
   info.dst.resource = tex;
   info.dst.level = level;
   info.dst.box = *box;
   info.dst.format = format;
   info.mask = util_format_is_depth_or_stencil(format) ? PIPE_MASK_ZS : PIPE_MASK_RGBA;
   info.render_condition_enable = render_condition_enable;

   if (util_format_is_subsampled_422(tex->format)) {
      access |= SI_IMAGE_ACCESS_BLOCK_FORMAT_AS_UINT;
      info.dst.format = PIPE_FORMAT_R32_UINT;
      info.dst.box.x = util_format_get_nblocksx(tex->format, info.dst.box.x);
   }

   return si_compute_blit(sctx, &info, color, access, 0, fail_if_slow);
}

bool si_compute_copy_image(struct si_context *sctx, struct pipe_resource *dst, unsigned dst_level,
                           struct pipe_resource *src, unsigned src_level, unsigned dstx,
                           unsigned dsty, unsigned dstz, const struct pipe_box *src_box,
                           bool fail_if_slow)
{
   struct si_texture *ssrc = (struct si_texture*)src;
   struct si_texture *sdst = (struct si_texture*)dst;
   enum pipe_format src_format = util_format_linear(src->format);
   enum pipe_format dst_format = util_format_linear(dst->format);

   assert(util_format_is_subsampled_422(src_format) == util_format_is_subsampled_422(dst_format));

   /* Interpret as integer values to avoid NaN issues */
   if (!vi_dcc_enabled(ssrc, src_level) &&
       !vi_dcc_enabled(sdst, dst_level) &&
       src_format == dst_format &&
       util_format_is_float(src_format) &&
       !util_format_is_compressed(src_format)) {
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

   /* Interpret compressed formats as UINT. */
   struct pipe_box new_box;
   unsigned src_access = 0, dst_access = 0;

   /* Note that staging copies do compressed<->UINT, so one of the formats is already UINT. */
   if (util_format_is_compressed(src_format) || util_format_is_compressed(dst_format)) {
      if (util_format_is_compressed(src_format))
         src_access |= SI_IMAGE_ACCESS_BLOCK_FORMAT_AS_UINT;
      if (util_format_is_compressed(dst_format))
         dst_access |= SI_IMAGE_ACCESS_BLOCK_FORMAT_AS_UINT;

      dstx = util_format_get_nblocksx(dst_format, dstx);
      dsty = util_format_get_nblocksy(dst_format, dsty);

      new_box.x = util_format_get_nblocksx(src_format, src_box->x);
      new_box.y = util_format_get_nblocksy(src_format, src_box->y);
      new_box.z = src_box->z;
      new_box.width = util_format_get_nblocksx(src_format, src_box->width);
      new_box.height = util_format_get_nblocksy(src_format, src_box->height);
      new_box.depth = src_box->depth;
      src_box = &new_box;

      if (ssrc->surface.bpe == 8)
         src_format = dst_format = PIPE_FORMAT_R16G16B16A16_UINT; /* 64-bit block */
      else
         src_format = dst_format = PIPE_FORMAT_R32G32B32A32_UINT; /* 128-bit block */
   }

   if (util_format_is_subsampled_422(src_format)) {
      assert(src_format == dst_format);

      src_access |= SI_IMAGE_ACCESS_BLOCK_FORMAT_AS_UINT;
      dst_access |= SI_IMAGE_ACCESS_BLOCK_FORMAT_AS_UINT;

      dstx = util_format_get_nblocksx(src_format, dstx);

      src_format = dst_format = PIPE_FORMAT_R32_UINT;

      /* Interpreting 422 subsampled format (16 bpp) as 32 bpp
       * should force us to divide src_box->x, dstx and width by 2.
       * But given that ac_surface allocates this format as 32 bpp
       * and that surf_size is then modified to pack the values
       * we must keep the original values to get the correct results.
       */
   }

   /* SNORM blitting has precision issues. Use the SINT equivalent instead, which doesn't
    * force DCC decompression.
    */
   if (util_format_is_snorm(dst_format))
      src_format = dst_format = util_format_snorm_to_sint(dst_format);

   struct pipe_blit_info info;
   memset(&info, 0, sizeof(info));
   info.dst.resource = dst;
   info.dst.level = dst_level;
   info.dst.box.x = dstx;
   info.dst.box.y = dsty;
   info.dst.box.z = dstz;
   info.dst.box.width = src_box->width;
   info.dst.box.height = src_box->height;
   info.dst.box.depth = src_box->depth;
   info.dst.format = dst_format;
   info.src.resource = src;
   info.src.level = src_level;
   info.src.box = *src_box;
   info.src.format = src_format;
   info.mask = util_format_is_depth_or_stencil(dst_format) ? PIPE_MASK_ZS : PIPE_MASK_RGBA;

   /* Only the compute blit can copy compressed and subsampled images. */
   fail_if_slow &= !dst_access && !src_access;

   bool success = si_compute_blit(sctx, &info, NULL, dst_access, src_access, fail_if_slow);
   assert((!dst_access && !src_access) || success);
   return success;
}

static unsigned get_tex_dim(struct si_texture *tex)
{
   switch (tex->buffer.b.b.target) {
   case PIPE_TEXTURE_3D:
      return 3;
   case PIPE_BUFFER:
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      return 1;
   default:
      return 2;
   }
}

static bool get_tex_is_array(struct si_texture *tex)
{
   switch (tex->buffer.b.b.target) {
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_1D_ARRAY:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return true;
   default:
      return false;;
   }
}

bool si_compute_blit(struct si_context *sctx, const struct pipe_blit_info *info,
                     const union pipe_color_union *clear_color, unsigned dst_access,
                     unsigned src_access, bool fail_if_slow)
{
   struct si_texture *sdst = (struct si_texture *)info->dst.resource;
   struct si_texture *ssrc = (struct si_texture *)info->src.resource;
   bool is_clear = !ssrc;
   unsigned dst_samples = MAX2(1, sdst->buffer.b.b.nr_samples);

   /* MSAA image stores don't work on <= Gfx10.3. It's an issue with FMASK because
    * AMD_DEBUG=nofmask fixes them. EQAA image stores are also unimplemented.
    * MSAA image stores work fine on Gfx11 (it has neither FMASK nor EQAA).
    */
   if (sctx->gfx_level < GFX11 && !(sctx->screen->debug_flags & DBG(NO_FMASK)) && dst_samples > 1)
      return false;

   if (info->dst_sample != 0 ||
       info->alpha_blend ||
       info->num_window_rectangles ||
       info->swizzle_enable ||
       info->scissor_enable)
      return false;

   struct ac_cs_blit_options options = {
      .nir_options = sctx->screen->nir_options,
      .info = &sctx->screen->info,
      .use_aco = sctx->screen->use_aco,
      .no_fmask = sctx->screen->debug_flags & DBG(NO_FMASK),
      /* Compute queues can't fail because there is no alternative. */
      .fail_if_slow = sctx->has_graphics && fail_if_slow,
   };

   struct ac_cs_blit_description blit = {
      .dst = {
         .surf = &sdst->surface,
         .dim = get_tex_dim(sdst),
         .is_array = get_tex_is_array(sdst),
         .width0 = info->dst.resource->width0,
         .height0 = info->dst.resource->height0,
         .num_samples = info->dst.resource->nr_samples,
         .level = info->dst.level,
         .box = info->dst.box,
         .format = info->dst.format,
      },
      .src = {
         .surf = ssrc ? &ssrc->surface : NULL,
         .dim = ssrc ? get_tex_dim(ssrc) : 0,
         .is_array = ssrc ? get_tex_is_array(ssrc) : false,
         .width0 = ssrc ? info->src.resource->width0 : 0,
         .height0 = ssrc ? info->src.resource->height0 : 0,
         .num_samples = ssrc ? info->src.resource->nr_samples : 0,
         .level = info->src.level,
         .box = info->src.box,
         .format = info->src.format,
      },
      .is_gfx_queue = sctx->has_graphics,
      /* if (src_access || dst_access), one of the images is block-compressed, which can't fall
       * back to a pixel shader on radeonsi */
      .dst_has_dcc = vi_dcc_enabled(sdst, info->dst.level) && !src_access && !dst_access,
      .sample0_only = info->sample0_only,
   };

   if (clear_color)
      blit.clear_color = *clear_color;

   struct ac_cs_blit_dispatches out;
   if (!ac_prepare_compute_blit(&options, &blit, &out))
      return false;

   if (!out.num_dispatches)
      return true;

   /* This is needed for compute queues if DCC stores are unsupported. */
   if (sctx->gfx_level < GFX10 && !sctx->has_graphics && vi_dcc_enabled(sdst, info->dst.level))
      si_texture_disable_dcc(sctx, sdst);

   /* Shader images. */
   struct pipe_image_view image[2];
   unsigned dst_index = is_clear ? 0 : 1;

   if (!is_clear) {
      image[0].resource = info->src.resource;
      image[0].shader_access = image[0].access = PIPE_IMAGE_ACCESS_READ | src_access;
      image[0].format = info->src.format;
      image[0].u.tex.level = info->src.level;
      image[0].u.tex.first_layer = 0;
      image[0].u.tex.last_layer = util_max_layer(info->src.resource, info->src.level);
   }

   image[dst_index].resource = info->dst.resource;
   image[dst_index].shader_access = image[dst_index].access = PIPE_IMAGE_ACCESS_WRITE | dst_access;
   image[dst_index].format = info->dst.format;
   image[dst_index].u.tex.level = info->dst.level;
   image[dst_index].u.tex.first_layer = 0;
   image[dst_index].u.tex.last_layer = util_max_layer(info->dst.resource, info->dst.level);

   /* Bind images and execute the barrier. */
   unsigned num_images = is_clear ? 1 : 2;
   struct pipe_image_view saved_images[2] = {};
   assert(num_images <= ARRAY_SIZE(saved_images));

   /* This must be before the barrier and si_compute_begin_internal because it might invoke DCC
    * decompression.
    */
   si_compute_save_and_bind_images(sctx, num_images, image, saved_images);
   si_barrier_before_internal_op(sctx, 0, 0, NULL, 0, num_images, image);
   si_compute_begin_internal(sctx, info->render_condition_enable);

   /* Execute compute blits. */
   for (unsigned i = 0; i < out.num_dispatches; i++) {
      struct ac_cs_blit_dispatch *dispatch = &out.dispatches[i];

      void *shader = _mesa_hash_table_u64_search(sctx->cs_blit_shaders, dispatch->shader_key.key);
      if (!shader) {
         shader = si_create_shader_state(sctx, ac_create_blit_cs(&options, &dispatch->shader_key));
         _mesa_hash_table_u64_insert(sctx->cs_blit_shaders, dispatch->shader_key.key, shader);
      }

      memcpy(sctx->cs_user_data, dispatch->user_data, sizeof(sctx->cs_user_data));

      struct pipe_grid_info grid = {
         .block = {
            dispatch->wg_size[0],
            dispatch->wg_size[1],
            dispatch->wg_size[2],
         },
         .last_block = {
            dispatch->last_wg_size[0],
            dispatch->last_wg_size[1],
            dispatch->last_wg_size[2],
         },
         .grid = {
            dispatch->num_workgroups[0],
            dispatch->num_workgroups[1],
            dispatch->num_workgroups[2],
         },
      };

      si_launch_grid_internal(sctx, &grid, shader);
   }

   si_compute_end_internal(sctx);
   si_barrier_after_internal_op(sctx, 0, 0, NULL, 0, num_images, image);
   si_compute_restore_images(sctx, num_images, saved_images);
   return true;
}
