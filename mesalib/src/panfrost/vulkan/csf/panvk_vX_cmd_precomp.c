/*
 * Copyright © 2024 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bifrost_compile.h"
#include "pan_desc.h"
#include "pan_encoder.h"
#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_precomp.h"
#include "panvk_macros.h"
#include "panvk_mempool.h"
#include "panvk_precomp_cache.h"
#include "panvk_queue.h"

void
panvk_per_arch(dispatch_precomp)(struct panvk_precomp_ctx *ctx,
                                 struct panlib_precomp_grid grid,
                                 enum panlib_barrier barrier,
                                 enum libpan_shaders_program idx, void *data,
                                 size_t data_size)
{
   assert(barrier == PANLIB_BARRIER_NONE && "Unsupported barrier flags");

   struct panvk_cmd_buffer *cmdbuf = ctx->cmdbuf;
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   const struct panvk_shader *shader =
      panvk_per_arch(precomp_cache_get)(dev->precomp_cache, idx);
   assert(shader);

   struct panfrost_ptr push_uniforms = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, BIFROST_PRECOMPILED_KERNEL_SYSVALS_SIZE + data_size, 16);

   assert(push_uniforms.gpu);

   struct bifrost_precompiled_kernel_sysvals sysvals;
   sysvals.num_workgroups.x = grid.count[0];
   sysvals.num_workgroups.y = grid.count[1];
   sysvals.num_workgroups.z = grid.count[2];
   sysvals.printf_buffer_address = dev->printf.bo->addr.dev;

   bifrost_precompiled_kernel_prepare_push_uniforms(push_uniforms.cpu, data,
                                                    data_size, &sysvals);

   struct pan_tls_info tlsinfo = {.tls = {.size = shader->info.tls_size},
                                  .wls = {.size = shader->info.wls_size}};

   if (tlsinfo.tls.size) {
      unsigned thread_tls_alloc =
         panfrost_query_thread_tls_alloc(&phys_dev->kmod.props);
      unsigned core_id_range;
      panfrost_query_core_count(&phys_dev->kmod.props, &core_id_range);

      unsigned size = panfrost_get_total_stack_size(
         tlsinfo.tls.size, thread_tls_alloc, core_id_range);
      tlsinfo.tls.ptr = panvk_cmd_alloc_dev_mem(cmdbuf, tls, size, 4096).gpu;
      assert(tlsinfo.tls.ptr);
   }

   if (tlsinfo.wls.size) {
      unsigned core_id_range;
      panfrost_query_core_count(&phys_dev->kmod.props, &core_id_range);

      struct pan_compute_dim wg_count = {.x = grid.count[0],
                                         .y = grid.count[1],
                                         .z = grid.count[2]};
      tlsinfo.wls.instances = pan_wls_instances(&wg_count);

      unsigned wls_total_size = pan_wls_adjust_size(tlsinfo.wls.size) *
                                tlsinfo.wls.instances * core_id_range;

      tlsinfo.wls.ptr =
         panvk_cmd_alloc_dev_mem(cmdbuf, tls, wls_total_size, 4096).gpu;

      assert(tlsinfo.wls.ptr);
   }

   struct pan_compute_dim dim = {.x = grid.count[0],
                                 .y = grid.count[1],
                                 .z = grid.count[2]};

   uint64_t tsd =
      panvk_per_arch(cmd_dispatch_prepare_tls)(cmdbuf, shader, &dim, false);
   assert(tsd);

   struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_COMPUTE);
   const struct cs_tracing_ctx *tracing_ctx =
      &cmdbuf->state.cs[PANVK_SUBQUEUE_COMPUTE].tracing;

   /* Copy the global TLS pointer to the per-job TSD. */
   if (shader->info.tls_size) {
      cs_move64_to(b, cs_scratch_reg64(b, 0), cmdbuf->state.tls.desc.gpu);
      cs_load64_to(b, cs_scratch_reg64(b, 2), cs_scratch_reg64(b, 0), 8);
      cs_wait_slot(b, SB_ID(LS), false);
      cs_move64_to(b, cs_scratch_reg64(b, 0), tsd);
      cs_store64(b, cs_scratch_reg64(b, 2), cs_scratch_reg64(b, 0), 8);
      cs_wait_slot(b, SB_ID(LS), false);
   }

   cs_update_compute_ctx(b) {
      /* No resource table */
      cs_move64_to(b, cs_sr_reg64(b, 0), 0);

      uint64_t fau_count =
         DIV_ROUND_UP(BIFROST_PRECOMPILED_KERNEL_SYSVALS_SIZE + data_size, 8);
      uint64_t fau_ptr = push_uniforms.gpu | (fau_count << 56);
      cs_move64_to(b, cs_sr_reg64(b, 8), fau_ptr);

      cs_move64_to(b, cs_sr_reg64(b, 16), panvk_priv_mem_dev_addr(shader->spd));

      cs_move64_to(b, cs_sr_reg64(b, 24), tsd);

      /* Global attribute offset */
      cs_move32_to(b, cs_sr_reg32(b, 32), 0);

      struct mali_compute_size_workgroup_packed wg_size;
      pan_pack(&wg_size, COMPUTE_SIZE_WORKGROUP, cfg) {
         cfg.workgroup_size_x = shader->local_size.x;
         cfg.workgroup_size_y = shader->local_size.y;
         cfg.workgroup_size_z = shader->local_size.z;
         cfg.allow_merging_workgroups = false;
      }
      cs_move32_to(b, cs_sr_reg32(b, 33), wg_size.opaque[0]);

      /* Job offset */
      cs_move32_to(b, cs_sr_reg32(b, 34), 0);
      cs_move32_to(b, cs_sr_reg32(b, 35), 0);
      cs_move32_to(b, cs_sr_reg32(b, 36), 0);

      /* Job size */
      cs_move32_to(b, cs_sr_reg32(b, 37), grid.count[0]);
      cs_move32_to(b, cs_sr_reg32(b, 38), grid.count[1]);
      cs_move32_to(b, cs_sr_reg32(b, 39), grid.count[2]);
   }

   panvk_per_arch(cs_pick_iter_sb)(cmdbuf, PANVK_SUBQUEUE_COMPUTE);

   cs_req_res(b, CS_COMPUTE_RES);
   unsigned task_axis = MALI_TASK_AXIS_X;
   unsigned task_increment = 0;
   panvk_per_arch(calculate_task_axis_and_increment)(
      shader, phys_dev, &task_axis, &task_increment);
   cs_trace_run_compute(b, tracing_ctx, cs_scratch_reg_tuple(b, 0, 4),
                        task_increment, task_axis, false,
                        cs_shader_res_sel(0, 0, 0, 0));
   cs_req_res(b, 0);

   struct cs_index sync_addr = cs_scratch_reg64(b, 0);
   struct cs_index iter_sb = cs_scratch_reg32(b, 2);
   struct cs_index cmp_scratch = cs_scratch_reg32(b, 3);
   struct cs_index add_val = cs_scratch_reg64(b, 4);

   cs_load_to(b, cs_scratch_reg_tuple(b, 0, 3), cs_subqueue_ctx_reg(b),
              BITFIELD_MASK(3),
              offsetof(struct panvk_cs_subqueue_context, syncobjs));
   cs_wait_slot(b, SB_ID(LS), false);

   cs_add64(b, sync_addr, sync_addr,
            PANVK_SUBQUEUE_COMPUTE * sizeof(struct panvk_cs_sync64));
   cs_move64_to(b, add_val, 1);

   cs_match(b, iter_sb, cmp_scratch) {
#define CASE(x)                                                                \
   cs_case(b, x) {                                                             \
      cs_sync64_add(b, true, MALI_CS_SYNC_SCOPE_CSG, add_val, sync_addr,       \
                    cs_defer(SB_WAIT_ITER(x), SB_ID(DEFERRED_SYNC)));          \
      cs_move32_to(b, iter_sb, next_iter_sb(x));                               \
   }

      CASE(0)
      CASE(1)
      CASE(2)
      CASE(3)
      CASE(4)
#undef CASE
   }

   cs_store32(b, iter_sb, cs_subqueue_ctx_reg(b),
              offsetof(struct panvk_cs_subqueue_context, iter_sb));
   cs_wait_slot(b, SB_ID(LS), false);

   ++cmdbuf->state.cs[PANVK_SUBQUEUE_COMPUTE].relative_sync_point;

   /* XXX: clobbers the registers instead to avoid recreating them when calling
    * a dispatch after? */
   compute_state_set_dirty(cmdbuf, CS);
   compute_state_set_dirty(cmdbuf, DESC_STATE);
   compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);
}
