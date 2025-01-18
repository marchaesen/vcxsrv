/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright © 2024 Arm Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_buffer.h"
#include "panvk_device.h"

static enum cs_reg_perm
tiler_oom_reg_perm_cb(struct cs_builder *b, unsigned reg)
{
   switch (reg) {
   /* The bbox is set up by the fragment subqueue, we should not modify it. */
   case 42:
   case 43:
   /* We should only load from the subqueue context. */
   case PANVK_CS_REG_SUBQUEUE_CTX_START:
   case PANVK_CS_REG_SUBQUEUE_CTX_END:
      return CS_REG_RD;
   }
   return CS_REG_RW;
}

static size_t
generate_tiler_oom_handler(struct cs_buffer handler_mem, bool has_zs_ext,
                           uint32_t rt_count, bool tracing_enabled,
                           uint32_t *dump_region_size)
{
   assert(rt_count >= 1 && rt_count <= MAX_RTS);
   uint32_t fbd_size = get_fbd_size(has_zs_ext, rt_count);

   struct cs_builder b;
   struct cs_builder_conf conf = {
      .nr_registers = 96,
      .nr_kernel_registers = 4,
      .reg_perm = tiler_oom_reg_perm_cb,
   };
   cs_builder_init(&b, &conf, handler_mem);

   struct cs_exception_handler handler;
   struct cs_exception_handler_ctx handler_ctx = {
      .ctx_reg = cs_subqueue_ctx_reg(&b),
      .dump_addr_offset = TILER_OOM_CTX_FIELD_OFFSET(reg_dump_addr),
      .ls_sb_slot = SB_ID(LS),
   };
   struct cs_tracing_ctx tracing_ctx = {
      .enabled = tracing_enabled,
      .ctx_reg = cs_subqueue_ctx_reg(&b),
      .tracebuf_addr_offset =
         offsetof(struct panvk_cs_subqueue_context, debug.tracebuf.cs),
      .ls_sb_slot = SB_ID(LS),
   };

   cs_exception_handler_def(&b, &handler, handler_ctx) {
      struct cs_index subqueue_ctx = cs_subqueue_ctx_reg(&b);
      struct cs_index zero = cs_scratch_reg64(&b, 0);
      /* Have flush_id read part of the double zero register */
      struct cs_index flush_id = cs_scratch_reg32(&b, 0);
      struct cs_index completed_chunks = cs_scratch_reg_tuple(&b, 2, 4);
      struct cs_index completed_top = cs_scratch_reg64(&b, 2);
      struct cs_index completed_bottom = cs_scratch_reg64(&b, 4);
      struct cs_index counter = cs_scratch_reg32(&b, 6);
      struct cs_index layer_count = cs_scratch_reg32(&b, 7);

      /* The tiler pointer is pre-filled. */
      struct cs_index tiler_ptr = cs_sr_reg64(&b, 38);
      struct cs_index fbd_ptr = cs_sr_reg64(&b, 40);

      /* Use different framebuffer descriptor depending on whether incremental
       * rendering has already been triggered */
      cs_load32_to(&b, counter, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(counter));
      cs_wait_slot(&b, SB_ID(LS), false);

      cs_if(&b, MALI_CS_CONDITION_GREATER, counter)
         cs_load64_to(&b, fbd_ptr, subqueue_ctx,
                      TILER_OOM_CTX_FBDPTR_OFFSET(MIDDLE));
      cs_else(&b)
         cs_load64_to(&b, fbd_ptr, subqueue_ctx,
                      TILER_OOM_CTX_FBDPTR_OFFSET(FIRST));

      cs_load32_to(&b, layer_count, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(layer_count));
      cs_wait_slot(&b, SB_ID(LS), false);

      cs_req_res(&b, CS_FRAG_RES);
      cs_while(&b, MALI_CS_CONDITION_GREATER, layer_count) {
         cs_trace_run_fragment(&b, &tracing_ctx,
                               cs_scratch_reg_tuple(&b, 8, 4), false,
                               MALI_TILE_RENDER_ORDER_Z_ORDER, false);
         cs_add32(&b, layer_count, layer_count, -1);
         cs_add64(&b, fbd_ptr, fbd_ptr, fbd_size);
      }
      cs_req_res(&b, 0);
      /* Wait for all iter scoreboards for simplicity. */
      cs_wait_slots(&b, SB_ALL_ITERS_MASK, false);

      /* Increment counter */
      cs_add32(&b, counter, counter, 1);
      cs_store32(&b, counter, subqueue_ctx,
                 TILER_OOM_CTX_FIELD_OFFSET(counter));

      /* Reuse layer_count reg for td_count */
      struct cs_index td_count = layer_count;
      cs_load32_to(&b, td_count, subqueue_ctx,
                   TILER_OOM_CTX_FIELD_OFFSET(td_count));
      cs_move64_to(&b, zero, 0);
      cs_wait_slot(&b, SB_ID(LS), false);

      cs_while(&b, MALI_CS_CONDITION_GREATER, td_count) {
         /* Load completed chunks */
         cs_load_to(&b, completed_chunks, tiler_ptr, BITFIELD_MASK(4), 10 * 4);
         cs_wait_slot(&b, SB_ID(LS), false);

         cs_finish_fragment(&b, false, completed_top, completed_bottom,
                            cs_now());

         /* Zero out polygon list, completed_top and completed_bottom */
         cs_store64(&b, zero, tiler_ptr, 0);
         cs_store64(&b, zero, tiler_ptr, 10 * 4);
         cs_store64(&b, zero, tiler_ptr, 12 * 4);

         cs_add64(&b, tiler_ptr, tiler_ptr, pan_size(TILER_CONTEXT));
         cs_add32(&b, td_count, td_count, -1);
      }

      /* We need to flush the texture caches so future preloads see the new
       * content. */
      cs_flush_caches(&b, MALI_CS_FLUSH_MODE_NONE, MALI_CS_FLUSH_MODE_NONE,
                      true, flush_id, cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));

      cs_wait_slot(&b, SB_ID(IMM_FLUSH), false);
   }

   assert(cs_is_valid(&b));
   cs_finish(&b);
   *dump_region_size = handler.dump_size;

   return handler.length * sizeof(uint64_t);
}

#define TILER_OOM_HANDLER_MAX_SIZE 512
VkResult
panvk_per_arch(init_tiler_oom)(struct panvk_device *device)
{
   struct panvk_instance *instance =
      to_panvk_instance(device->vk.physical->instance);
   bool tracing_enabled = instance->debug_flags & PANVK_DEBUG_TRACE;
   VkResult result = panvk_priv_bo_create(
      device, TILER_OOM_HANDLER_MAX_SIZE * 2 * MAX_RTS, 0,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &device->tiler_oom.handlers_bo);
   if (result != VK_SUCCESS)
      return result;

   for (uint32_t zs_ext = 0; zs_ext <= 1; zs_ext++) {
      for (uint32_t rt_count = 1; rt_count <= MAX_RTS; rt_count++) {
         uint32_t idx = get_tiler_oom_handler_idx(zs_ext, rt_count);
         /* Check that we have calculated a handler_stride if we need it to
          * offset addresses. */
         assert(idx == 0 || device->tiler_oom.handler_stride != 0);
         size_t offset = idx * device->tiler_oom.handler_stride;

         struct cs_buffer handler_mem = {
            .cpu = device->tiler_oom.handlers_bo->addr.host + offset,
            .gpu = device->tiler_oom.handlers_bo->addr.dev + offset,
            .capacity = TILER_OOM_HANDLER_MAX_SIZE / sizeof(uint64_t),
         };

         uint32_t dump_region_size;
         size_t handler_length = generate_tiler_oom_handler(
            handler_mem, zs_ext, rt_count, tracing_enabled, &dump_region_size);

         /* All handlers must have the same length */
         assert(idx == 0 || handler_length == device->tiler_oom.handler_stride);
         assert(idx == 0 ||
                dump_region_size == device->tiler_oom.dump_region_size);
         device->tiler_oom.handler_stride = handler_length;
         device->tiler_oom.dump_region_size = dump_region_size;
      }
   }

   return result;
}
