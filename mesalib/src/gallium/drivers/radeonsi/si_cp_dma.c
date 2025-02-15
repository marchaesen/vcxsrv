/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_pipe.h"
#include "sid.h"
#include "si_build_pm4.h"

/* Set this if you want the ME to wait until CP DMA is done.
 * It should be set on the last CP DMA packet. */
#define CP_DMA_SYNC        (1 << 0)

/* Set this if the source data was used as a destination in a previous CP DMA
 * packet. It's for preventing a read-after-write (RAW) hazard between two
 * CP DMA packets. */
#define CP_DMA_RAW_WAIT    (1 << 1)
#define CP_DMA_CLEAR       (1 << 2)

/* The max number of bytes that can be copied per packet. */
static inline unsigned cp_dma_max_byte_count(struct si_context *sctx)
{
   unsigned max =
      sctx->gfx_level >= GFX11 ? 32767 :
      sctx->gfx_level >= GFX9 ? S_415_BYTE_COUNT_GFX9(~0u) : S_415_BYTE_COUNT_GFX6(~0u);

   /* make it aligned for optimal performance */
   return max & ~(SI_CPDMA_ALIGNMENT - 1);
}

/* should cp dma skip the hole in sparse bo */
static inline bool cp_dma_sparse_wa(struct si_context *sctx, struct si_resource *sdst)
{
   return sctx->gfx_level == GFX9 && sdst->flags & RADEON_FLAG_SPARSE;
}

/* Emit a CP DMA packet to do a copy from one buffer to another, or to clear
 * a buffer. The size must fit in bits [20:0]. If CP_DMA_CLEAR is set, src_va is a 32-bit
 * clear value.
 */
static void si_emit_cp_dma(struct si_context *sctx, struct radeon_cmdbuf *cs, uint64_t dst_va,
                           uint64_t src_va, unsigned size, unsigned flags)
{
   uint32_t header = 0, command = 0;

   assert(sctx->screen->info.has_cp_dma);
   assert(size <= cp_dma_max_byte_count(sctx));

   if (sctx->gfx_level >= GFX9)
      command |= S_415_BYTE_COUNT_GFX9(size);
   else
      command |= S_415_BYTE_COUNT_GFX6(size);

   /* Sync flags. */
   if (flags & CP_DMA_SYNC)
      header |= S_411_CP_SYNC(1);

   if (flags & CP_DMA_RAW_WAIT)
      command |= S_415_RAW_WAIT(1);

   /* Src and dst flags. */
   if (sctx->screen->info.cp_dma_use_L2)
      header |= S_501_DST_SEL(V_501_DST_ADDR_TC_L2);

   if (flags & CP_DMA_CLEAR) {
      header |= S_411_SRC_SEL(V_411_DATA);
   } else if (sctx->screen->info.cp_dma_use_L2) {
      header |= S_501_SRC_SEL(V_501_SRC_ADDR_TC_L2);
   }

   radeon_begin(cs);

   if (sctx->gfx_level >= GFX7) {
      radeon_emit(PKT3(PKT3_DMA_DATA, 5, 0));
      radeon_emit(header);
      radeon_emit(src_va);       /* SRC_ADDR_LO [31:0] */
      radeon_emit(src_va >> 32); /* SRC_ADDR_HI [31:0] */
      radeon_emit(dst_va);       /* DST_ADDR_LO [31:0] */
      radeon_emit(dst_va >> 32); /* DST_ADDR_HI [31:0] */
      radeon_emit(command);
   } else {
      header |= S_411_SRC_ADDR_HI(src_va >> 32);

      radeon_emit(PKT3(PKT3_CP_DMA, 4, 0));
      radeon_emit(src_va);                  /* SRC_ADDR_LO [31:0] */
      radeon_emit(header);                  /* SRC_ADDR_HI [15:0] + flags. */
      radeon_emit(dst_va);                  /* DST_ADDR_LO [31:0] */
      radeon_emit((dst_va >> 32) & 0xffff); /* DST_ADDR_HI [15:0] */
      radeon_emit(command);
   }
   radeon_end();
}

void si_cp_dma_wait_for_idle(struct si_context *sctx, struct radeon_cmdbuf *cs)
{
   /* Issue a dummy DMA that copies zero bytes.
    *
    * The DMA engine will see that there's no work to do and skip this
    * DMA request, however, the CP will see the sync flag and still wait
    * for all DMAs to complete.
    */
   si_emit_cp_dma(sctx, cs, 0, 0, 0, CP_DMA_SYNC);
}

static void si_cp_dma_prepare(struct si_context *sctx, struct pipe_resource *dst,
                              struct pipe_resource *src, unsigned byte_count,
                              uint64_t remaining_size, bool *is_first, unsigned *packet_flags)
{
   si_need_gfx_cs_space(sctx, 0, 0);

   /* This must be done after need_cs_space. */
   radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, si_resource(dst),
                             RADEON_USAGE_WRITE | RADEON_PRIO_CP_DMA);
   if (src)
      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, si_resource(src),
                                RADEON_USAGE_READ | RADEON_PRIO_CP_DMA);

   /* Flush the caches for the first copy only.
    * Also wait for the previous CP DMA operations.
    */
   if (*is_first)
      si_emit_barrier_direct(sctx);

   if (*is_first && !(*packet_flags & CP_DMA_CLEAR))
      *packet_flags |= CP_DMA_RAW_WAIT;

   *is_first = false;

   /* Do the synchronization after the last dma, so that all data
    * is written to memory.
    */
   if (byte_count == remaining_size)
      *packet_flags |= CP_DMA_SYNC;
}

void si_cp_dma_clear_buffer(struct si_context *sctx, struct radeon_cmdbuf *cs,
                            struct pipe_resource *dst, uint64_t offset, uint64_t size,
                            unsigned value)
{
   struct si_resource *sdst = si_resource(dst);
   uint64_t va = sdst->gpu_address + offset;
   bool is_first = true;

   assert(!sctx->screen->info.cp_sdma_ge_use_system_memory_scope);
   assert(size && size % 4 == 0);

   if (!sctx->screen->info.cp_dma_use_L2) {
      sctx->barrier_flags |= SI_BARRIER_INV_L2;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
   }

   /* Mark the buffer range of destination as valid (initialized),
    * so that transfer_map knows it should wait for the GPU when mapping
    * that range. */
   util_range_add(dst, &sdst->valid_buffer_range, offset, offset + size);

   while (size) {
      unsigned byte_count = MIN2(size, cp_dma_max_byte_count(sctx));
      unsigned dma_flags = CP_DMA_CLEAR;

      if (cp_dma_sparse_wa(sctx, sdst)) {
         unsigned skip_count =
            sctx->ws->buffer_find_next_committed_memory(sdst->buf,
                  va - sdst->gpu_address, &byte_count);
         va += skip_count;
         size -= skip_count;
      }

      if (!byte_count)
         continue;

      si_cp_dma_prepare(sctx, dst, NULL, byte_count, size, &is_first, &dma_flags);

      /* Emit the clear packet. */
      si_emit_cp_dma(sctx, cs, va, value, byte_count, dma_flags);

      size -= byte_count;
      va += byte_count;
   }

   sctx->num_cp_dma_calls++;
}

/**
 * Realign the CP DMA engine. This must be done after a copy with an unaligned
 * size.
 *
 * \param size  Remaining size to the CP DMA alignment.
 */
static void si_cp_dma_realign_engine(struct si_context *sctx, unsigned size, bool *is_first)
{
   uint64_t va;
   unsigned dma_flags = 0;
   unsigned scratch_size = SI_CPDMA_ALIGNMENT * 2;

   assert(size < SI_CPDMA_ALIGNMENT);

   /* Use the scratch buffer as the dummy buffer. The 3D engine should be
    * idle at this point.
    */
   if (!sctx->scratch_buffer || sctx->scratch_buffer->b.b.width0 < scratch_size) {
      si_resource_reference(&sctx->scratch_buffer, NULL);
      sctx->scratch_buffer = si_aligned_buffer_create(&sctx->screen->b,
                                                      PIPE_RESOURCE_FLAG_UNMAPPABLE | SI_RESOURCE_FLAG_DRIVER_INTERNAL |
                                                      SI_RESOURCE_FLAG_DISCARDABLE,
                                                      PIPE_USAGE_DEFAULT, scratch_size, 256);
      if (!sctx->scratch_buffer)
         return;

      si_mark_atom_dirty(sctx, &sctx->atoms.s.scratch_state);
   }

   si_cp_dma_prepare(sctx, &sctx->scratch_buffer->b.b, &sctx->scratch_buffer->b.b, size, size,
                     is_first, &dma_flags);

   va = sctx->scratch_buffer->gpu_address;
   si_emit_cp_dma(sctx, &sctx->gfx_cs, va, va + SI_CPDMA_ALIGNMENT, size, dma_flags);
}

/**
 * Do memcpy between buffers using CP DMA.
 */
void si_cp_dma_copy_buffer(struct si_context *sctx, struct pipe_resource *dst,
                           struct pipe_resource *src, uint64_t dst_offset, uint64_t src_offset,
                           unsigned size)
{
   assert(size);
   assert(dst && src);

   if (!sctx->screen->info.cp_dma_use_L2) {
      sctx->barrier_flags |= SI_BARRIER_INV_L2;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
   }

   /* Mark the buffer range of destination as valid (initialized),
    * so that transfer_map knows it should wait for the GPU when mapping
    * that range.
    */
   util_range_add(dst, &si_resource(dst)->valid_buffer_range, dst_offset, dst_offset + size);

   dst_offset += si_resource(dst)->gpu_address;
   src_offset += si_resource(src)->gpu_address;

   unsigned skipped_size = 0;
   unsigned realign_size = 0;

   /* The workarounds aren't needed on Fiji and beyond. */
   if (sctx->family <= CHIP_CARRIZO || sctx->family == CHIP_STONEY) {
      /* If the size is not aligned, we must add a dummy copy at the end
       * just to align the internal counter. Otherwise, the DMA engine
       * would slow down by an order of magnitude for following copies.
       */
      if (size % SI_CPDMA_ALIGNMENT)
         realign_size = SI_CPDMA_ALIGNMENT - (size % SI_CPDMA_ALIGNMENT);

      /* If the copy begins unaligned, we must start copying from the next
       * aligned block and the skipped part should be copied after everything
       * else has been copied. Only the src alignment matters, not dst.
       */
      if (src_offset % SI_CPDMA_ALIGNMENT) {
         skipped_size = SI_CPDMA_ALIGNMENT - (src_offset % SI_CPDMA_ALIGNMENT);
         /* The main part will be skipped if the size is too small. */
         skipped_size = MIN2(skipped_size, size);
         size -= skipped_size;
      }
   }

   /* TMZ handling */
   if (unlikely(radeon_uses_secure_bos(sctx->ws))) {
      bool secure = si_resource(src)->flags & RADEON_FLAG_ENCRYPTED;
      assert(!secure || si_resource(dst)->flags & RADEON_FLAG_ENCRYPTED);
      if (secure != sctx->ws->cs_is_secure(&sctx->gfx_cs)) {
         si_flush_gfx_cs(sctx, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW |
                               RADEON_FLUSH_TOGGLE_SECURE_SUBMISSION, NULL);
      }
   }

   /* This is the main part doing the copying. Src is always aligned. */
   uint64_t main_dst_offset = dst_offset + skipped_size;
   uint64_t main_src_offset = src_offset + skipped_size;
   bool is_first = true;

   while (size) {
      unsigned byte_count = MIN2(size, cp_dma_max_byte_count(sctx));
      unsigned dma_flags = 0;

      if (cp_dma_sparse_wa(sctx, si_resource(dst))) {
         unsigned skip_count =
            sctx->ws->buffer_find_next_committed_memory(si_resource(dst)->buf,
                  main_dst_offset - si_resource(dst)->gpu_address, &byte_count);
         main_dst_offset += skip_count;
         main_src_offset += skip_count;
         size -= skip_count;
      }

      if (cp_dma_sparse_wa(sctx, si_resource(src))) {
         unsigned skip_count =
            sctx->ws->buffer_find_next_committed_memory(si_resource(src)->buf,
                  main_src_offset - si_resource(src)->gpu_address, &byte_count);
         main_dst_offset += skip_count;
         main_src_offset += skip_count;
         size -= skip_count;
      }

      if (!byte_count)
         continue;

      si_cp_dma_prepare(sctx, dst, src, byte_count, size + skipped_size + realign_size,
                        &is_first, &dma_flags);

      si_emit_cp_dma(sctx, &sctx->gfx_cs, main_dst_offset, main_src_offset, byte_count, dma_flags);

      size -= byte_count;
      main_src_offset += byte_count;
      main_dst_offset += byte_count;
   }

   /* Copy the part we skipped because src wasn't aligned. */
   if (skipped_size) {
      unsigned dma_flags = 0;

      si_cp_dma_prepare(sctx, dst, src, skipped_size, skipped_size + realign_size,
                        &is_first, &dma_flags);

      si_emit_cp_dma(sctx, &sctx->gfx_cs, dst_offset, src_offset, skipped_size, dma_flags);
   }

   /* Finally, realign the engine if the size wasn't aligned. */
   if (realign_size)
      si_cp_dma_realign_engine(sctx, realign_size, &is_first);

   sctx->num_cp_dma_calls++;
}

void si_cp_write_data(struct si_context *sctx, struct si_resource *buf, unsigned offset,
                      unsigned size, unsigned dst_sel, unsigned engine, const void *data)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   assert(offset % 4 == 0);
   assert(size % 4 == 0);

   if (sctx->gfx_level == GFX6 && dst_sel == V_370_MEM)
      dst_sel = V_370_MEM_GRBM;

   radeon_add_to_buffer_list(sctx, cs, buf, RADEON_USAGE_WRITE | RADEON_PRIO_CP_DMA);
   uint64_t va = buf->gpu_address + offset;

   radeon_begin(cs);
   radeon_emit(PKT3(PKT3_WRITE_DATA, 2 + size / 4, 0));
   radeon_emit(S_370_DST_SEL(dst_sel) | S_370_WR_CONFIRM(1) | S_370_ENGINE_SEL(engine));
   radeon_emit(va);
   radeon_emit(va >> 32);
   radeon_emit_array((const uint32_t *)data, size / 4);
   radeon_end();
}

void si_cp_copy_data(struct si_context *sctx, struct radeon_cmdbuf *cs, unsigned dst_sel,
                     struct si_resource *dst, unsigned dst_offset, unsigned src_sel,
                     struct si_resource *src, unsigned src_offset)
{
   /* cs can point to the compute IB, which has the buffer list in gfx_cs. */
   if (dst) {
      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, dst, RADEON_USAGE_WRITE | RADEON_PRIO_CP_DMA);
   }
   if (src) {
      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, src, RADEON_USAGE_READ | RADEON_PRIO_CP_DMA);
   }

   uint64_t dst_va = (dst ? dst->gpu_address : 0ull) + dst_offset;
   uint64_t src_va = (src ? src->gpu_address : 0ull) + src_offset;

   radeon_begin(cs);
   radeon_emit(PKT3(PKT3_COPY_DATA, 4, 0));
   radeon_emit(COPY_DATA_SRC_SEL(src_sel) | COPY_DATA_DST_SEL(dst_sel) | COPY_DATA_WR_CONFIRM);
   radeon_emit(src_va);
   radeon_emit(src_va >> 32);
   radeon_emit(dst_va);
   radeon_emit(dst_va >> 32);
   radeon_end();
}
