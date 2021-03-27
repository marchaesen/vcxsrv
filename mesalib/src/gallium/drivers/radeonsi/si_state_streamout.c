/*
 * Copyright 2013 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "si_build_pm4.h"
#include "util/u_memory.h"
#include "util/u_suballoc.h"

static void si_set_streamout_enable(struct si_context *sctx, bool enable);

static inline void si_so_target_reference(struct si_streamout_target **dst,
                                          struct pipe_stream_output_target *src)
{
   pipe_so_target_reference((struct pipe_stream_output_target **)dst, src);
}

static struct pipe_stream_output_target *si_create_so_target(struct pipe_context *ctx,
                                                             struct pipe_resource *buffer,
                                                             unsigned buffer_offset,
                                                             unsigned buffer_size)
{
   struct si_streamout_target *t;
   struct si_resource *buf = si_resource(buffer);

   t = CALLOC_STRUCT(si_streamout_target);
   if (!t) {
      return NULL;
   }

   t->b.reference.count = 1;
   t->b.context = ctx;
   pipe_resource_reference(&t->b.buffer, buffer);
   t->b.buffer_offset = buffer_offset;
   t->b.buffer_size = buffer_size;

   util_range_add(&buf->b.b, &buf->valid_buffer_range, buffer_offset, buffer_offset + buffer_size);
   return &t->b;
}

static void si_so_target_destroy(struct pipe_context *ctx, struct pipe_stream_output_target *target)
{
   struct si_streamout_target *t = (struct si_streamout_target *)target;
   pipe_resource_reference(&t->b.buffer, NULL);
   si_resource_reference(&t->buf_filled_size, NULL);
   FREE(t);
}

void si_streamout_buffers_dirty(struct si_context *sctx)
{
   if (!sctx->streamout.enabled_mask)
      return;

   si_mark_atom_dirty(sctx, &sctx->atoms.s.streamout_begin);
   si_set_streamout_enable(sctx, true);
}

static void si_set_streamout_targets(struct pipe_context *ctx, unsigned num_targets,
                                     struct pipe_stream_output_target **targets,
                                     const unsigned *offsets)
{
   struct si_context *sctx = (struct si_context *)ctx;
   unsigned old_num_targets = sctx->streamout.num_targets;
   unsigned i;
   bool wait_now = false;

   /* We are going to unbind the buffers. Mark which caches need to be flushed. */
   if (sctx->streamout.num_targets && sctx->streamout.begin_emitted) {
      /* Since streamout uses vector writes which go through TC L2
       * and most other clients can use TC L2 as well, we don't need
       * to flush it.
       *
       * The only cases which requires flushing it is VGT DMA index
       * fetching (on <= GFX7) and indirect draw data, which are rare
       * cases. Thus, flag the TC L2 dirtiness in the resource and
       * handle it at draw call time.
       */
      for (i = 0; i < sctx->streamout.num_targets; i++)
         if (sctx->streamout.targets[i])
            si_resource(sctx->streamout.targets[i]->b.buffer)->TC_L2_dirty = true;

      /* Invalidate the scalar cache in case a streamout buffer is
       * going to be used as a constant buffer.
       *
       * Invalidate vL1, because streamout bypasses it (done by
       * setting GLC=1 in the store instruction), but vL1 in other
       * CUs can contain outdated data of streamout buffers.
       *
       * VS_PARTIAL_FLUSH is required if the buffers are going to be
       * used as an input immediately.
       */
      sctx->flags |= SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE;

      /* The BUFFER_FILLED_SIZE is written using a PS_DONE event. */
      if (sctx->screen->use_ngg_streamout) {
         sctx->flags |= SI_CONTEXT_PS_PARTIAL_FLUSH;

         /* Wait now. This is needed to make sure that GDS is not
          * busy at the end of IBs.
          *
          * Also, the next streamout operation will overwrite GDS,
          * so we need to make sure that it's idle.
          */
         wait_now = true;
      } else {
         sctx->flags |= SI_CONTEXT_VS_PARTIAL_FLUSH;
      }
   }

   /* All readers of the streamout targets need to be finished before we can
    * start writing to the targets.
    */
   if (num_targets) {
      if (sctx->screen->use_ngg_streamout)
         si_allocate_gds(sctx);

      sctx->flags |= SI_CONTEXT_PS_PARTIAL_FLUSH | SI_CONTEXT_CS_PARTIAL_FLUSH;
   }

   /* Streamout buffers must be bound in 2 places:
    * 1) in VGT by setting the VGT_STRMOUT registers
    * 2) as shader resources
    */

   /* Stop streamout. */
   if (sctx->streamout.num_targets && sctx->streamout.begin_emitted)
      si_emit_streamout_end(sctx);

   /* Set the new targets. */
   unsigned enabled_mask = 0, append_bitmask = 0;
   for (i = 0; i < num_targets; i++) {
      si_so_target_reference(&sctx->streamout.targets[i], targets[i]);
      if (!targets[i])
         continue;

      si_context_add_resource_size(sctx, targets[i]->buffer);
      enabled_mask |= 1 << i;

      if (offsets[i] == ((unsigned)-1))
         append_bitmask |= 1 << i;

      /* Allocate space for the filled buffer size. */
      struct si_streamout_target *t = sctx->streamout.targets[i];
      if (!t->buf_filled_size) {
         unsigned buf_filled_size_size = sctx->screen->use_ngg_streamout ? 8 : 4;
         u_suballocator_alloc(&sctx->allocator_zeroed_memory, buf_filled_size_size, 4,
                              &t->buf_filled_size_offset,
                              (struct pipe_resource **)&t->buf_filled_size);
      }
   }

   for (; i < sctx->streamout.num_targets; i++)
      si_so_target_reference(&sctx->streamout.targets[i], NULL);

   sctx->streamout.enabled_mask = enabled_mask;
   sctx->streamout.num_targets = num_targets;
   sctx->streamout.append_bitmask = append_bitmask;

   /* Update dirty state bits. */
   if (num_targets) {
      si_streamout_buffers_dirty(sctx);
   } else {
      si_set_atom_dirty(sctx, &sctx->atoms.s.streamout_begin, false);
      si_set_streamout_enable(sctx, false);
   }

   /* Set the shader resources.*/
   for (i = 0; i < num_targets; i++) {
      if (targets[i]) {
         struct pipe_shader_buffer sbuf;
         sbuf.buffer = targets[i]->buffer;

         if (sctx->screen->use_ngg_streamout) {
            sbuf.buffer_offset = targets[i]->buffer_offset;
            sbuf.buffer_size = targets[i]->buffer_size;
         } else {
            sbuf.buffer_offset = 0;
            sbuf.buffer_size = targets[i]->buffer_offset + targets[i]->buffer_size;
         }

         si_set_internal_shader_buffer(sctx, SI_VS_STREAMOUT_BUF0 + i, &sbuf);
         si_resource(targets[i]->buffer)->bind_history |= PIPE_BIND_STREAM_OUTPUT;
      } else {
         si_set_internal_shader_buffer(sctx, SI_VS_STREAMOUT_BUF0 + i, NULL);
      }
   }
   for (; i < old_num_targets; i++)
      si_set_internal_shader_buffer(sctx, SI_VS_STREAMOUT_BUF0 + i, NULL);

   if (wait_now)
      sctx->emit_cache_flush(sctx, &sctx->gfx_cs);
}

static void gfx10_emit_streamout_begin(struct si_context *sctx)
{
   struct si_streamout_target **t = sctx->streamout.targets;
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned last_target = 0;

   for (unsigned i = 0; i < sctx->streamout.num_targets; i++) {
      if (t[i])
         last_target = i;
   }

   radeon_begin(cs);

   for (unsigned i = 0; i < sctx->streamout.num_targets; i++) {
      if (!t[i])
         continue;

      t[i]->stride_in_dw = sctx->streamout.stride_in_dw[i];

      bool append = sctx->streamout.append_bitmask & (1 << i);
      uint64_t va = 0;

      if (append) {
         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, t[i]->buf_filled_size, RADEON_USAGE_READ,
                                   RADEON_PRIO_SO_FILLED_SIZE);

         va = t[i]->buf_filled_size->gpu_address + t[i]->buf_filled_size_offset;
      }

      radeon_emit(cs, PKT3(PKT3_DMA_DATA, 5, 0));
      radeon_emit(cs, S_411_SRC_SEL(append ? V_411_SRC_ADDR_TC_L2 : V_411_DATA) |
                         S_411_DST_SEL(V_411_GDS) | S_411_CP_SYNC(i == last_target));
      radeon_emit(cs, va);
      radeon_emit(cs, va >> 32);
      radeon_emit(cs, 4 * i); /* destination in GDS */
      radeon_emit(cs, 0);
      radeon_emit(cs, S_414_BYTE_COUNT_GFX9(4) | S_414_DISABLE_WR_CONFIRM_GFX9(i != last_target));
   }
   radeon_end();

   sctx->streamout.begin_emitted = true;
}

static void gfx10_emit_streamout_end(struct si_context *sctx)
{
   struct si_streamout_target **t = sctx->streamout.targets;

   for (unsigned i = 0; i < sctx->streamout.num_targets; i++) {
      if (!t[i])
         continue;

      uint64_t va = t[i]->buf_filled_size->gpu_address + t[i]->buf_filled_size_offset;

      si_cp_release_mem(sctx, &sctx->gfx_cs, V_028A90_PS_DONE, 0, EOP_DST_SEL_TC_L2,
                        EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM, EOP_DATA_SEL_GDS,
                        t[i]->buf_filled_size, va, EOP_DATA_GDS(i, 1), 0);

      t[i]->buf_filled_size_valid = true;
   }

   sctx->streamout.begin_emitted = false;
}

static void si_flush_vgt_streamout(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned reg_strmout_cntl;

   radeon_begin(cs);

   /* The register is at different places on different ASICs. */
   if (sctx->chip_class >= GFX7) {
      reg_strmout_cntl = R_0300FC_CP_STRMOUT_CNTL;
      radeon_set_uconfig_reg(cs, reg_strmout_cntl, 0);
   } else {
      reg_strmout_cntl = R_0084FC_CP_STRMOUT_CNTL;
      radeon_set_config_reg(cs, reg_strmout_cntl, 0);
   }

   radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
   radeon_emit(cs, EVENT_TYPE(EVENT_TYPE_SO_VGTSTREAMOUT_FLUSH) | EVENT_INDEX(0));

   radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
   radeon_emit(cs,
               WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
   radeon_emit(cs, reg_strmout_cntl >> 2); /* register */
   radeon_emit(cs, 0);
   radeon_emit(cs, S_0084FC_OFFSET_UPDATE_DONE(1)); /* reference value */
   radeon_emit(cs, S_0084FC_OFFSET_UPDATE_DONE(1)); /* mask */
   radeon_emit(cs, 4);                              /* poll interval */
   radeon_end();
}

static void si_emit_streamout_begin(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct si_streamout_target **t = sctx->streamout.targets;
   uint16_t *stride_in_dw = sctx->streamout.stride_in_dw;
   unsigned i;

   si_flush_vgt_streamout(sctx);

   radeon_begin(cs);

   for (i = 0; i < sctx->streamout.num_targets; i++) {
      if (!t[i])
         continue;

      t[i]->stride_in_dw = stride_in_dw[i];

      /* AMD GCN binds streamout buffers as shader resources.
       * VGT only counts primitives and tells the shader
       * through SGPRs what to do. */
      radeon_set_context_reg_seq(cs, R_028AD0_VGT_STRMOUT_BUFFER_SIZE_0 + 16 * i, 2);
      radeon_emit(cs, (t[i]->b.buffer_offset + t[i]->b.buffer_size) >> 2); /* BUFFER_SIZE (in DW) */
      radeon_emit(cs, stride_in_dw[i]);                                    /* VTX_STRIDE (in DW) */

      if (sctx->streamout.append_bitmask & (1 << i) && t[i]->buf_filled_size_valid) {
         uint64_t va = t[i]->buf_filled_size->gpu_address + t[i]->buf_filled_size_offset;

         /* Append. */
         radeon_emit(cs, PKT3(PKT3_STRMOUT_BUFFER_UPDATE, 4, 0));
         radeon_emit(cs, STRMOUT_SELECT_BUFFER(i) |
                            STRMOUT_OFFSET_SOURCE(STRMOUT_OFFSET_FROM_MEM)); /* control */
         radeon_emit(cs, 0);                                                 /* unused */
         radeon_emit(cs, 0);                                                 /* unused */
         radeon_emit(cs, va);                                                /* src address lo */
         radeon_emit(cs, va >> 32);                                          /* src address hi */

         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, t[i]->buf_filled_size, RADEON_USAGE_READ,
                                   RADEON_PRIO_SO_FILLED_SIZE);
      } else {
         /* Start from the beginning. */
         radeon_emit(cs, PKT3(PKT3_STRMOUT_BUFFER_UPDATE, 4, 0));
         radeon_emit(cs, STRMOUT_SELECT_BUFFER(i) |
                            STRMOUT_OFFSET_SOURCE(STRMOUT_OFFSET_FROM_PACKET)); /* control */
         radeon_emit(cs, 0);                                                    /* unused */
         radeon_emit(cs, 0);                                                    /* unused */
         radeon_emit(cs, t[i]->b.buffer_offset >> 2); /* buffer offset in DW */
         radeon_emit(cs, 0);                          /* unused */
      }
   }
   radeon_end();

   sctx->streamout.begin_emitted = true;
}

void si_emit_streamout_end(struct si_context *sctx)
{
   if (sctx->screen->use_ngg_streamout) {
      gfx10_emit_streamout_end(sctx);
      return;
   }

   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct si_streamout_target **t = sctx->streamout.targets;
   unsigned i;
   uint64_t va;

   si_flush_vgt_streamout(sctx);

   radeon_begin(cs);

   for (i = 0; i < sctx->streamout.num_targets; i++) {
      if (!t[i])
         continue;

      va = t[i]->buf_filled_size->gpu_address + t[i]->buf_filled_size_offset;
      radeon_emit(cs, PKT3(PKT3_STRMOUT_BUFFER_UPDATE, 4, 0));
      radeon_emit(cs, STRMOUT_SELECT_BUFFER(i) | STRMOUT_OFFSET_SOURCE(STRMOUT_OFFSET_NONE) |
                         STRMOUT_STORE_BUFFER_FILLED_SIZE); /* control */
      radeon_emit(cs, va);                                  /* dst address lo */
      radeon_emit(cs, va >> 32);                            /* dst address hi */
      radeon_emit(cs, 0);                                   /* unused */
      radeon_emit(cs, 0);                                   /* unused */

      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, t[i]->buf_filled_size, RADEON_USAGE_WRITE,
                                RADEON_PRIO_SO_FILLED_SIZE);

      /* Zero the buffer size. The counters (primitives generated,
       * primitives emitted) may be enabled even if there is not
       * buffer bound. This ensures that the primitives-emitted query
       * won't increment. */
      radeon_set_context_reg(cs, R_028AD0_VGT_STRMOUT_BUFFER_SIZE_0 + 16 * i, 0);

      t[i]->buf_filled_size_valid = true;
   }
   radeon_end_update_context_roll(sctx);

   sctx->streamout.begin_emitted = false;
}

/* STREAMOUT CONFIG DERIVED STATE
 *
 * Streamout must be enabled for the PRIMITIVES_GENERATED query to work.
 * The buffer mask is an independent state, so no writes occur if there
 * are no buffers bound.
 */

static void si_emit_streamout_enable(struct si_context *sctx)
{
   assert(!sctx->screen->use_ngg_streamout);

   radeon_begin(&sctx->gfx_cs);
   radeon_set_context_reg_seq(&sctx->gfx_cs, R_028B94_VGT_STRMOUT_CONFIG, 2);
   radeon_emit(&sctx->gfx_cs, S_028B94_STREAMOUT_0_EN(si_get_strmout_en(sctx)) |
                                S_028B94_RAST_STREAM(0) |
                                S_028B94_STREAMOUT_1_EN(si_get_strmout_en(sctx)) |
                                S_028B94_STREAMOUT_2_EN(si_get_strmout_en(sctx)) |
                                S_028B94_STREAMOUT_3_EN(si_get_strmout_en(sctx)));
   radeon_emit(&sctx->gfx_cs,
               sctx->streamout.hw_enabled_mask & sctx->streamout.enabled_stream_buffers_mask);
   radeon_end();
}

static void si_set_streamout_enable(struct si_context *sctx, bool enable)
{
   bool old_strmout_en = si_get_strmout_en(sctx);
   unsigned old_hw_enabled_mask = sctx->streamout.hw_enabled_mask;

   sctx->streamout.streamout_enabled = enable;

   sctx->streamout.hw_enabled_mask =
      sctx->streamout.enabled_mask | (sctx->streamout.enabled_mask << 4) |
      (sctx->streamout.enabled_mask << 8) | (sctx->streamout.enabled_mask << 12);

   if (!sctx->screen->use_ngg_streamout &&
       ((old_strmout_en != si_get_strmout_en(sctx)) ||
        (old_hw_enabled_mask != sctx->streamout.hw_enabled_mask)))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.streamout_enable);
}

void si_update_prims_generated_query_state(struct si_context *sctx, unsigned type, int diff)
{
   if (!sctx->screen->use_ngg_streamout && type == PIPE_QUERY_PRIMITIVES_GENERATED) {
      bool old_strmout_en = si_get_strmout_en(sctx);

      sctx->streamout.num_prims_gen_queries += diff;
      assert(sctx->streamout.num_prims_gen_queries >= 0);

      sctx->streamout.prims_gen_query_enabled = sctx->streamout.num_prims_gen_queries != 0;

      if (old_strmout_en != si_get_strmout_en(sctx))
         si_mark_atom_dirty(sctx, &sctx->atoms.s.streamout_enable);

      if (si_update_ngg(sctx)) {
         si_shader_change_notify(sctx);
         sctx->do_update_shaders = true;
      }
   }
}

void si_init_streamout_functions(struct si_context *sctx)
{
   sctx->b.create_stream_output_target = si_create_so_target;
   sctx->b.stream_output_target_destroy = si_so_target_destroy;
   sctx->b.set_stream_output_targets = si_set_streamout_targets;

   if (sctx->screen->use_ngg_streamout) {
      sctx->atoms.s.streamout_begin.emit = gfx10_emit_streamout_begin;
   } else {
      sctx->atoms.s.streamout_begin.emit = si_emit_streamout_begin;
      sctx->atoms.s.streamout_enable.emit = si_emit_streamout_enable;
   }
}
