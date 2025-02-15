/*
 * Copyright © 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */
#include "pan_precomp.h"
#include "util/u_memory.h"
#include "bifrost_compile.h"
#include "pan_context.h"
#include "pan_desc.h"
#include "pan_pool.h"
#include "pan_screen.h"
#include "pan_shader.h"

#if PAN_ARCH >= 10
#include "genxml/cs_builder.h"
#include "pan_csf.h"
#endif

struct panfrost_precomp_cache *
GENX(panfrost_precomp_cache_init)(struct panfrost_screen *screen)
{
   struct panfrost_precomp_cache *cache = CALLOC_STRUCT(panfrost_precomp_cache);

   if (cache == NULL)
      return NULL;

   simple_mtx_init(&cache->lock, mtx_plain);
   cache->programs = GENX(libpan_shaders_default);
   cache->bin_pool = &screen->mempools.bin.base;
   cache->desc_pool = &screen->mempools.desc.base;

   return cache;
}

#if PAN_ARCH >= 9
static enum mali_flush_to_zero_mode
panfrost_ftz_mode(struct pan_shader_info *info)
{
   if (info->ftz_fp32) {
      if (info->ftz_fp16)
         return MALI_FLUSH_TO_ZERO_MODE_ALWAYS;
      else
         return MALI_FLUSH_TO_ZERO_MODE_DX11;
   } else {
      /* We don't have a "flush FP16, preserve FP32" mode, but APIs
       * should not be able to generate that.
       */
      assert(!info->ftz_fp16 && !info->ftz_fp32);
      return MALI_FLUSH_TO_ZERO_MODE_PRESERVE_SUBNORMALS;
   }
}
#endif

static struct panfrost_precomp_shader *
panfrost_precomp_shader_create(
   struct panfrost_precomp_cache *cache,
   const struct bifrost_precompiled_kernel_info *info, const void *binary)
{
   struct panfrost_precomp_shader *res = CALLOC_STRUCT(panfrost_precomp_shader);

   if (res == NULL)
      return NULL;

   res->info = info->info;

   struct pan_compute_dim local_dim = {
      .x = info->local_size_x,
      .y = info->local_size_y,
      .z = info->local_size_z,
   };
   res->local_size = local_dim;

   struct panfrost_ptr bin =
      pan_pool_alloc_aligned(cache->bin_pool, info->binary_size, 64);

   if (!bin.gpu)
      goto err;

   memcpy(bin.cpu, binary, info->binary_size);
   res->code_ptr = bin.gpu;

#if PAN_ARCH <= 7
   struct panfrost_ptr rsd =
      pan_pool_alloc_desc(cache->desc_pool, RENDERER_STATE);

   if (!rsd.gpu)
      goto err;

   pan_cast_and_pack(rsd.cpu, RENDERER_STATE, cfg) {
      pan_shader_prepare_rsd(&res->info, bin.gpu, &cfg);
   }

   res->state_ptr = rsd.gpu;
#else
   struct panfrost_ptr spd =
      pan_pool_alloc_desc(cache->desc_pool, SHADER_PROGRAM);

   if (!spd.gpu)
      goto err;

   pan_cast_and_pack(spd.cpu, SHADER_PROGRAM, cfg) {
      cfg.stage = pan_shader_stage(&res->info);
      cfg.register_allocation =
         pan_register_allocation(res->info.work_reg_count);
      cfg.binary = res->code_ptr;
      cfg.preload.r48_r63 = (res->info.preload >> 48);
      cfg.flush_to_zero_mode = panfrost_ftz_mode(&res->info);
   }

   res->state_ptr = spd.gpu;
#endif

   return res;

err:
   FREE(res);
   return NULL;
}

static void
panfrost_precomp_shader_destroy(struct panfrost_precomp_cache *cache,
                                struct panfrost_precomp_shader *shader)
{
   /* XXX: Do we have anything to do here? */
}

void
GENX(panfrost_precomp_cache_cleanup)(struct panfrost_precomp_cache *cache)
{
   for (unsigned i = 0; i < ARRAY_SIZE(cache->precomp); i++) {
      if (cache->precomp[i])
         panfrost_precomp_shader_destroy(cache, cache->precomp[i]);
   }

   simple_mtx_destroy(&cache->lock);
   FREE(cache);
}

static struct panfrost_precomp_shader *
panfrost_precomp_cache_get_locked(struct panfrost_precomp_cache *cache,
                                  unsigned program)
{
   simple_mtx_assert_locked(&cache->lock);

   /* It is possible that, while waiting for the lock, another thread uploaded
    * the shader. Check for that so we don't double-upload.
    */
   if (cache->precomp[program])
      return cache->precomp[program];

   const uint32_t *bin = cache->programs[program];
   const struct bifrost_precompiled_kernel_info *info = (void *)bin;
   const void *binary = (const uint8_t *)bin + sizeof(*info);

   struct panfrost_precomp_shader *shader =
      panfrost_precomp_shader_create(cache, info, binary);

   if (shader == NULL)
      return NULL;

   /* We must only write to the cache once we are done compiling, since other
    * threads may be reading the cache concurrently. Do this last.
    */
   p_atomic_set(&cache->precomp[program], shader);

   return shader;
}

static struct panfrost_precomp_shader *
panfrost_precomp_cache_get(struct panfrost_precomp_cache *cache,
                           unsigned program)
{
   /* Shaders are immutable once written, so if we atomically read a non-NULL
    * shader, then we have a valid cached shader and are done.
    */
   struct panfrost_precomp_shader *ret =
      p_atomic_read(cache->precomp + program);

   if (ret != NULL)
      return ret;

   /* Otherwise, take the lock and upload. */
   simple_mtx_lock(&cache->lock);
   ret = panfrost_precomp_cache_get_locked(cache, program);
   simple_mtx_unlock(&cache->lock);

   return ret;
}

static uint64_t
emit_tls(struct panfrost_batch *batch,
         const struct panfrost_precomp_shader *shader,
         const struct pan_compute_dim *dim)
{
   struct panfrost_context *ctx = batch->ctx;
   struct panfrost_device *dev = pan_device(ctx->base.screen);
   struct panfrost_ptr t =
      pan_pool_alloc_desc(&batch->pool.base, LOCAL_STORAGE);

   struct pan_tls_info info = {
      .tls.size = shader->info.tls_size,
      .wls.size = shader->info.wls_size,
      .wls.instances = pan_wls_instances(dim),
   };

   if (info.tls.size) {
      struct panfrost_bo *bo = panfrost_batch_get_scratchpad(
         batch, info.tls.size, dev->thread_tls_alloc, dev->core_id_range);
      info.tls.ptr = bo->ptr.gpu;
   }

   if (info.wls.size) {
      unsigned size = pan_wls_adjust_size(info.wls.size) * info.wls.instances *
                      dev->core_id_range;

      struct panfrost_bo *bo = panfrost_batch_get_shared_memory(batch, size, 1);

      info.wls.ptr = bo->ptr.gpu;
   }

   GENX(pan_emit_tls)(&info, t.cpu);

   return t.gpu;
}

void
GENX(panfrost_launch_precomp)(struct panfrost_batch *batch,
                              struct panlib_precomp_grid grid,
                              enum panlib_barrier barrier,
                              enum libpan_shaders_program idx, void *data,
                              size_t data_size)
{
   assert(PAN_ARCH >= 6 && "Midgard isn't supported on launch_precomp");

   struct panfrost_context *ctx = batch->ctx;
   struct pipe_context *gallium = (struct pipe_context *)ctx;
   struct panfrost_device *dev = pan_device(gallium->screen);

   struct panfrost_precomp_shader *shader =
      panfrost_precomp_cache_get(dev->precomp_cache, idx);
   assert(shader);

   struct panfrost_ptr push_uniforms = pan_pool_alloc_aligned(
      &batch->pool.base, BIFROST_PRECOMPILED_KERNEL_SYSVALS_SIZE + data_size,
      16);
   assert(push_uniforms.gpu);

   struct pan_compute_dim dim = {.x = grid.count[0],
                                 .y = grid.count[1],
                                 .z = grid.count[2]};
   uint64_t tsd = emit_tls(batch, shader, &dim);
   assert(tsd);

   struct bifrost_precompiled_kernel_sysvals sysvals;
   sysvals.num_workgroups.x = grid.count[0];
   sysvals.num_workgroups.y = grid.count[1];
   sysvals.num_workgroups.z = grid.count[2];
   sysvals.printf_buffer_address = ctx->printf.bo->ptr.gpu;

   bifrost_precompiled_kernel_prepare_push_uniforms(push_uniforms.cpu, data,
                                                    data_size, &sysvals);

#if PAN_ARCH <= 9
   struct panfrost_ptr job =
      pan_pool_alloc_desc(&batch->pool.base, COMPUTE_JOB);
   assert(job.gpu);

#if PAN_ARCH <= 7
   panfrost_pack_work_groups_compute(
      pan_section_ptr(job.cpu, COMPUTE_JOB, INVOCATION), grid.count[0],
      grid.count[1], grid.count[2], shader->local_size.x, shader->local_size.y,
      shader->local_size.z, false, false);

   pan_section_pack(job.cpu, COMPUTE_JOB, PARAMETERS, cfg) {
      cfg.job_task_split = util_logbase2_ceil(shader->local_size.x + 1) +
                           util_logbase2_ceil(shader->local_size.y + 1) +
                           util_logbase2_ceil(shader->local_size.z + 1);
   }

   pan_section_pack(job.cpu, COMPUTE_JOB, DRAW, cfg) {
      cfg.state = shader->state_ptr;
      cfg.push_uniforms = push_uniforms.gpu;
      cfg.thread_storage = tsd;
   }
#else
   pan_section_pack(job.cpu, COMPUTE_JOB, PAYLOAD, cfg) {
      cfg.workgroup_size_x = shader->local_size.x;
      cfg.workgroup_size_y = shader->local_size.y;
      cfg.workgroup_size_z = shader->local_size.z;

      cfg.workgroup_count_x = grid.count[0];
      cfg.workgroup_count_y = grid.count[1];
      cfg.workgroup_count_z = grid.count[2];

      cfg.compute.shader = shader->state_ptr;

      uint64_t fau_count =
         DIV_ROUND_UP(BIFROST_PRECOMPILED_KERNEL_SYSVALS_SIZE + data_size, 8);

      cfg.compute.fau = push_uniforms.gpu;
      cfg.compute.fau_count = fau_count;

      cfg.compute.thread_storage = tsd;

      cfg.compute.resources = 0;
      cfg.allow_merging_workgroups = false;

      cfg.task_increment = 1;
      cfg.task_axis = MALI_TASK_AXIS_Z;
   }
#endif

   bool job_barrier = (barrier & PANLIB_BARRIER_JM_BARRIER) != 0;
   bool suppress_prefetch =
      (barrier & PANLIB_BARRIER_JM_SUPPRESS_PREFETCH) != 0;

   pan_jc_add_job(&batch->jm.jobs.vtc_jc, MALI_JOB_TYPE_COMPUTE, job_barrier,
                  suppress_prefetch, 0, 0, &job, false);
#else
   struct cs_builder *b = batch->csf.cs.builder;

   /* No resource table */
   cs_move64_to(b, cs_reg64(b, 0), 0);

   uint64_t fau_count =
      DIV_ROUND_UP(BIFROST_PRECOMPILED_KERNEL_SYSVALS_SIZE + data_size, 8);
   uint64_t fau_ptr = push_uniforms.gpu | (fau_count << 56);
   cs_move64_to(b, cs_reg64(b, 8), fau_ptr);

   cs_move64_to(b, cs_reg64(b, 16), shader->state_ptr);
   cs_move64_to(b, cs_reg64(b, 24), tsd);

   /* Global attribute offset */
   cs_move32_to(b, cs_reg32(b, 32), 0);

   /* Compute workgroup size */
   struct mali_compute_size_workgroup_packed wg_size;
   pan_pack(&wg_size, COMPUTE_SIZE_WORKGROUP, cfg) {
      cfg.workgroup_size_x = shader->local_size.x;
      cfg.workgroup_size_y = shader->local_size.y;
      cfg.workgroup_size_z = shader->local_size.z;
      cfg.allow_merging_workgroups = false;
   }
   cs_move32_to(b, cs_reg32(b, 33), wg_size.opaque[0]);

   /* Job offset */
   cs_move32_to(b, cs_reg32(b, 34), 0);
   cs_move32_to(b, cs_reg32(b, 35), 0);
   cs_move32_to(b, cs_reg32(b, 36), 0);

   /* Job size */
   cs_move32_to(b, cs_reg32(b, 37), grid.count[0]);
   cs_move32_to(b, cs_reg32(b, 38), grid.count[1]);
   cs_move32_to(b, cs_reg32(b, 39), grid.count[2]);

   unsigned threads_per_wg =
      shader->local_size.x * shader->local_size.y * shader->local_size.z;
   unsigned max_thread_cnt = panfrost_compute_max_thread_count(
      &dev->kmod.props, shader->info.work_reg_count);

   /* Pick the task_axis and task_increment to maximize thread utilization. */
   unsigned task_axis = MALI_TASK_AXIS_X;
   unsigned threads_per_task = threads_per_wg;
   unsigned task_increment = 0;

   for (unsigned i = 0; i < 3; i++) {
      if (threads_per_task * grid.count[i] >= max_thread_cnt) {
         /* We reached out thread limit, stop at the current axis and
          * calculate the increment so it doesn't exceed the per-core
          * thread capacity.
          */
         task_increment = max_thread_cnt / threads_per_task;
         break;
      } else if (task_axis == MALI_TASK_AXIS_Z) {
         /* We reached the Z axis, and there's still room to stuff more
          * threads. Pick the current axis grid size as our increment
          * as there's no point using something bigger.
          */
         task_increment = grid.count[i];
         break;
      }

      threads_per_task *= grid.count[i];
      task_axis++;
   }

   assert(task_axis <= MALI_TASK_AXIS_Z);
   assert(task_increment > 0);
   cs_run_compute(b, task_increment, task_axis, false,
                  cs_shader_res_sel(0, 0, 0, 0));
#endif
}
