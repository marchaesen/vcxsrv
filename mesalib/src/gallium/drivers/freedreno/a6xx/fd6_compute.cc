/*
 * Copyright © 2019 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "drm/freedreno_ringbuffer.h"
#define FD_BO_NO_HARDPIN 1

#include "pipe/p_state.h"
#include "util/u_dump.h"
#include "u_tracepoints.h"

#include "freedreno_resource.h"
#include "freedreno_tracepoints.h"

#include "fd6_barrier.h"
#include "fd6_compute.h"
#include "fd6_const.h"
#include "fd6_context.h"
#include "fd6_emit.h"
#include "fd6_pack.h"

template <chip CHIP>
static void
cs_program_emit_local_size(struct fd_context *ctx, struct fd_ringbuffer *ring,
                           struct ir3_shader_variant *v, uint16_t local_size[3])
{
   /*
    * Devices that do not support double threadsize take the threadsize from
    * A6XX_HLSQ_FS_CNTL_0_THREADSIZE instead of A6XX_HLSQ_CS_CNTL_1_THREADSIZE
    * which is always set to THREAD128.
    */
   enum a6xx_threadsize thrsz = v->info.double_threadsize ? THREAD128 : THREAD64;
   enum a6xx_threadsize thrsz_cs = ctx->screen->info->a6xx
      .supports_double_threadsize ? thrsz : THREAD128;

   if (CHIP == A7XX) {
      unsigned tile_height = (local_size[1] % 8 == 0)   ? 3
                             : (local_size[1] % 4 == 0) ? 5
                             : (local_size[1] % 2 == 0) ? 9
                                                           : 17;

      OUT_REG(ring,
         HLSQ_CS_CNTL_1(
            CHIP,
            .linearlocalidregid = INVALID_REG,
            .threadsize = thrsz_cs,
            .workgrouprastorderzfirsten = true,
            .wgtilewidth = 4,
            .wgtileheight = tile_height,
         )
      );

      OUT_REG(ring,
         A7XX_HLSQ_CS_LOCAL_SIZE(
            .localsizex = local_size[0] - 1,
            .localsizey = local_size[1] - 1,
            .localsizez = local_size[2] - 1,
         )
      );
   }
}

template <chip CHIP>
static void
cs_program_emit(struct fd_context *ctx, struct fd_ringbuffer *ring,
                struct ir3_shader_variant *v)
   assert_dt
{
   OUT_REG(ring, HLSQ_INVALIDATE_CMD(CHIP, .vs_state = true, .hs_state = true,
                                          .ds_state = true, .gs_state = true,
                                          .fs_state = true, .cs_state = true,
                                          .cs_ibo = true, .gfx_ibo = true, ));

   OUT_REG(ring, HLSQ_CS_CNTL(
         CHIP,
         .constlen = v->constlen,
         .enabled = true,
   ));

   OUT_PKT4(ring, REG_A6XX_SP_CS_CONFIG, 1);
   OUT_RING(ring, A6XX_SP_CS_CONFIG_ENABLED |
                     COND(v->bindless_tex, A6XX_SP_CS_CONFIG_BINDLESS_TEX) |
                     COND(v->bindless_samp, A6XX_SP_CS_CONFIG_BINDLESS_SAMP) |
                     COND(v->bindless_ibo, A6XX_SP_CS_CONFIG_BINDLESS_IBO) |
                     COND(v->bindless_ubo, A6XX_SP_CS_CONFIG_BINDLESS_UBO) |
                     A6XX_SP_CS_CONFIG_NIBO(ir3_shader_nibo(v)) |
                     A6XX_SP_CS_CONFIG_NTEX(v->num_samp) |
                     A6XX_SP_CS_CONFIG_NSAMP(v->num_samp)); /* SP_CS_CONFIG */

   uint32_t local_invocation_id = v->cs.local_invocation_id;
   uint32_t work_group_id = v->cs.work_group_id;

   /*
    * Devices that do not support double threadsize take the threadsize from
    * A6XX_HLSQ_FS_CNTL_0_THREADSIZE instead of A6XX_HLSQ_CS_CNTL_1_THREADSIZE
    * which is always set to THREAD128.
    */
   enum a6xx_threadsize thrsz = v->info.double_threadsize ? THREAD128 : THREAD64;
   enum a6xx_threadsize thrsz_cs = ctx->screen->info->a6xx
      .supports_double_threadsize ? thrsz : THREAD128;

   if (CHIP == A6XX) {
      OUT_PKT4(ring, REG_A6XX_HLSQ_CS_CNTL_0, 2);
      OUT_RING(ring, A6XX_HLSQ_CS_CNTL_0_WGIDCONSTID(work_group_id) |
                        A6XX_HLSQ_CS_CNTL_0_WGSIZECONSTID(regid(63, 0)) |
                        A6XX_HLSQ_CS_CNTL_0_WGOFFSETCONSTID(regid(63, 0)) |
                        A6XX_HLSQ_CS_CNTL_0_LOCALIDREGID(local_invocation_id));
      OUT_RING(ring, A6XX_HLSQ_CS_CNTL_1_LINEARLOCALIDREGID(regid(63, 0)) |
                        A6XX_HLSQ_CS_CNTL_1_THREADSIZE(thrsz_cs));
      if (!ctx->screen->info->a6xx.supports_double_threadsize) {
         OUT_PKT4(ring, REG_A6XX_HLSQ_FS_CNTL_0, 1);
         OUT_RING(ring, A6XX_HLSQ_FS_CNTL_0_THREADSIZE(thrsz));
      }

      if (ctx->screen->info->a6xx.has_lpac) {
         OUT_PKT4(ring, REG_A6XX_SP_CS_CNTL_0, 2);
         OUT_RING(ring, A6XX_SP_CS_CNTL_0_WGIDCONSTID(work_group_id) |
                           A6XX_SP_CS_CNTL_0_WGSIZECONSTID(regid(63, 0)) |
                           A6XX_SP_CS_CNTL_0_WGOFFSETCONSTID(regid(63, 0)) |
                           A6XX_SP_CS_CNTL_0_LOCALIDREGID(local_invocation_id));
         OUT_RING(ring, A6XX_SP_CS_CNTL_1_LINEARLOCALIDREGID(regid(63, 0)) |
                           A6XX_SP_CS_CNTL_1_THREADSIZE(thrsz));
      }
   } else {
      OUT_REG(ring, HLSQ_FS_CNTL_0(CHIP, .threadsize = THREAD64));
      OUT_REG(ring,
         A6XX_SP_CS_CNTL_0(
            .wgidconstid = work_group_id,
            .wgsizeconstid = INVALID_REG,
            .wgoffsetconstid = INVALID_REG,
            .localidregid = local_invocation_id,
         )
      );
      OUT_REG(ring,
         SP_CS_CNTL_1(
            CHIP,
            .linearlocalidregid = INVALID_REG,
            .threadsize = thrsz_cs,
            .workitemrastorder =
               v->cs.force_linear_dispatch ? WORKITEMRASTORDER_LINEAR
                                           : WORKITEMRASTORDER_TILED,
         )
      );
      OUT_REG(ring, A7XX_SP_CS_UNKNOWN_A9BE(0)); // Sometimes is 0x08000000
   }

   if (!v->local_size_variable)
      cs_program_emit_local_size<CHIP>(ctx, ring, v, v->local_size);

   fd6_emit_shader<CHIP>(ctx, ring, v);
}

template <chip CHIP>
static void
fd6_launch_grid(struct fd_context *ctx, const struct pipe_grid_info *info) in_dt
{
   struct fd6_compute_state *cs = (struct fd6_compute_state *)ctx->compute;
   struct fd_ringbuffer *ring = ctx->batch->draw;

   if (unlikely(!cs->v)) {
      struct ir3_shader_state *hwcso = (struct ir3_shader_state *)cs->hwcso;
      struct ir3_shader_key key = {};

      cs->v = ir3_shader_variant(ir3_get_shader(hwcso), key, false, &ctx->debug);
      if (!cs->v)
         return;

      cs->stateobj = fd_ringbuffer_new_object(ctx->pipe, 0x1000);
      cs_program_emit<CHIP>(ctx, cs->stateobj, cs->v);

      cs->user_consts_cmdstream_size = fd6_user_consts_cmdstream_size<CHIP>(cs->v);
   }

   trace_start_compute(&ctx->batch->trace, ring, !!info->indirect, info->work_dim,
                       info->block[0], info->block[1], info->block[2],
                       info->grid[0],  info->grid[1],  info->grid[2],
                       cs->v->shader_id);

   if (ctx->batch->barrier)
      fd6_barrier_flush<CHIP>(ctx->batch);

   bool emit_instrlen_workaround =
      cs->v->instrlen > ctx->screen->info->a6xx.instr_cache_size;

   /* There appears to be a HW bug where in some rare circumstances it appears
    * to accidentally use the FS instrlen instead of the CS instrlen, which
    * affects all known gens. Based on various experiments it appears that the
    * issue is that when prefetching a branch destination and there is a cache
    * miss, when fetching from memory the HW bounds-checks the fetch against
    * SP_CS_INSTRLEN, except when one of the two register contexts is active
    * it accidentally fetches SP_FS_INSTRLEN from the other (inactive)
    * context. To workaround it we set the FS instrlen here and do a dummy
    * event to roll the context (because it fetches SP_FS_INSTRLEN from the
    * "wrong" context). Because the bug seems to involve cache misses, we
    * don't emit this if the entire CS program fits in cache, which will
    * hopefully be the majority of cases.
    *
    * See https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/19023
    */
   if (emit_instrlen_workaround) {
      OUT_REG(ring, A6XX_SP_FS_INSTRLEN(cs->v->instrlen));
      fd6_event_write<CHIP>(ctx, ring, FD_LABEL);
   }

   if (ctx->gen_dirty)
      fd6_emit_cs_state<CHIP>(ctx, ring, cs);

   if (ctx->gen_dirty & BIT(FD6_GROUP_CONST))
      fd6_emit_cs_user_consts<CHIP>(ctx, ring, cs);

   if (cs->v->need_driver_params || info->input)
      fd6_emit_cs_driver_params<CHIP>(ctx, ring, cs, info);

   OUT_PKT7(ring, CP_SET_MARKER, 1);
   OUT_RING(ring, A6XX_CP_SET_MARKER_0_MODE(RM6_COMPUTE));

   uint32_t shared_size =
      MAX2(((int)(cs->v->cs.req_local_mem + info->variable_shared_mem) - 1) / 1024, 1);
   OUT_PKT4(ring, REG_A6XX_SP_CS_UNKNOWN_A9B1, 1);
   OUT_RING(ring, A6XX_SP_CS_UNKNOWN_A9B1_SHARED_SIZE(shared_size) |
                     A6XX_SP_CS_UNKNOWN_A9B1_UNK6);

   if (CHIP == A6XX && ctx->screen->info->a6xx.has_lpac) {
      OUT_PKT4(ring, REG_A6XX_HLSQ_CS_UNKNOWN_B9D0, 1);
      OUT_RING(ring, A6XX_HLSQ_CS_UNKNOWN_B9D0_SHARED_SIZE(shared_size) |
                        A6XX_HLSQ_CS_UNKNOWN_B9D0_UNK6);
   }

   const unsigned *local_size =
      info->block; // v->shader->nir->info->workgroup_size;
   const unsigned *num_groups = info->grid;
   /* for some reason, mesa/st doesn't set info->work_dim, so just assume 3: */
   const unsigned work_dim = info->work_dim ? info->work_dim : 3;

   if (cs->v->local_size_variable) {
      uint16_t wg[] = {local_size[0], local_size[1], local_size[2]};
      cs_program_emit_local_size<CHIP>(ctx, ring, cs->v, wg);
   }

   OUT_REG(ring,
           HLSQ_CS_NDRANGE_0(
                 CHIP,
                 .kerneldim = work_dim,
                 .localsizex = local_size[0] - 1,
                 .localsizey = local_size[1] - 1,
                 .localsizez = local_size[2] - 1,
           ),
           HLSQ_CS_NDRANGE_1(
                 CHIP,
                 .globalsize_x = local_size[0] * num_groups[0],
           ),
           HLSQ_CS_NDRANGE_2(CHIP, .globaloff_x = 0),
           HLSQ_CS_NDRANGE_3(
                 CHIP,
                 .globalsize_y = local_size[1] * num_groups[1],
           ),
           HLSQ_CS_NDRANGE_4(CHIP, .globaloff_y = 0),
           HLSQ_CS_NDRANGE_5(
                 CHIP,
                 .globalsize_z = local_size[2] * num_groups[2],
           ),
           HLSQ_CS_NDRANGE_6(CHIP, .globaloff_z = 0),
   );

   OUT_REG(ring,
           HLSQ_CS_KERNEL_GROUP_X(CHIP, 1),
           HLSQ_CS_KERNEL_GROUP_Y(CHIP, 1),
           HLSQ_CS_KERNEL_GROUP_Z(CHIP, 1),
   );

   if (info->indirect) {
      struct fd_resource *rsc = fd_resource(info->indirect);

      OUT_PKT7(ring, CP_EXEC_CS_INDIRECT, 4);
      OUT_RING(ring, 0x00000000);
      OUT_RELOC(ring, rsc->bo, info->indirect_offset, 0, 0); /* ADDR_LO/HI */
      OUT_RING(ring,
               A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEX(local_size[0] - 1) |
                  A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEY(local_size[1] - 1) |
                  A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEZ(local_size[2] - 1));
   } else {
      OUT_PKT7(ring, CP_EXEC_CS, 4);
      OUT_RING(ring, 0x00000000);
      OUT_RING(ring, CP_EXEC_CS_1_NGROUPS_X(info->grid[0]));
      OUT_RING(ring, CP_EXEC_CS_2_NGROUPS_Y(info->grid[1]));
      OUT_RING(ring, CP_EXEC_CS_3_NGROUPS_Z(info->grid[2]));
   }

   trace_end_compute(&ctx->batch->trace, ring);

   fd_context_all_clean(ctx);
}

static void *
fd6_compute_state_create(struct pipe_context *pctx,
                         const struct pipe_compute_state *cso)
{
   struct fd6_compute_state *hwcso =
         (struct fd6_compute_state *)calloc(1, sizeof(*hwcso));
   hwcso->hwcso = ir3_shader_compute_state_create(pctx, cso);
   return hwcso;
}

static void
fd6_compute_state_delete(struct pipe_context *pctx, void *_hwcso)
{
   struct fd6_compute_state *hwcso = (struct fd6_compute_state *)_hwcso;
   ir3_shader_state_delete(pctx, hwcso->hwcso);
   if (hwcso->stateobj)
      fd_ringbuffer_del(hwcso->stateobj);
   free(hwcso);
}

static void
fd6_get_compute_state_info(struct pipe_context *pctx, void *cso, struct pipe_compute_state_object_info *info)
{
   static struct ir3_shader_key key; /* static is implicitly zeroed */
   struct fd6_compute_state *cs = (struct fd6_compute_state *)cso;
   struct ir3_shader_state *hwcso = (struct ir3_shader_state *)cs->hwcso;
   struct ir3_shader_variant *v = ir3_shader_variant(ir3_get_shader(hwcso), key, false, &pctx->debug);
   struct fd_context *ctx = fd_context(pctx);
   uint32_t threadsize_base = ctx->screen->info->threadsize_base;

   info->max_threads = threadsize_base * ctx->screen->info->max_waves;
   info->simd_sizes = threadsize_base;
   info->preferred_simd_size = threadsize_base;

   if (ctx->screen->info->a6xx.supports_double_threadsize &&
       v->info.double_threadsize) {

      info->max_threads *= 2;
      info->simd_sizes |= (threadsize_base * 2);
      info->preferred_simd_size *= 2;
   }

   info->private_memory = v->pvtmem_size;
}

template <chip CHIP>
void
fd6_compute_init(struct pipe_context *pctx)
   disable_thread_safety_analysis
{
   struct fd_context *ctx = fd_context(pctx);

   ctx->launch_grid = fd6_launch_grid<CHIP>;
   pctx->create_compute_state = fd6_compute_state_create;
   pctx->delete_compute_state = fd6_compute_state_delete;
   pctx->get_compute_state_info = fd6_get_compute_state_info;
}
FD_GENX(fd6_compute_init);
