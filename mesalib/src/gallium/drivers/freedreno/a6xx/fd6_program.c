/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "pipe/p_state.h"
#include "util/bitset.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include "freedreno_program.h"

#include "fd6_const.h"
#include "fd6_emit.h"
#include "fd6_format.h"
#include "fd6_pack.h"
#include "fd6_program.h"
#include "fd6_texture.h"

void
fd6_emit_shader(struct fd_context *ctx, struct fd_ringbuffer *ring,
                const struct ir3_shader_variant *so)
{
   enum a6xx_state_block sb = fd6_stage2shadersb(so->type);

   uint32_t first_exec_offset = 0;
   uint32_t instrlen = 0;
   uint32_t hw_stack_offset = 0;

   switch (so->type) {
   case MESA_SHADER_VERTEX:
      first_exec_offset = REG_A6XX_SP_VS_OBJ_FIRST_EXEC_OFFSET;
      instrlen = REG_A6XX_SP_VS_INSTRLEN;
      hw_stack_offset = REG_A6XX_SP_VS_PVT_MEM_HW_STACK_OFFSET;
      break;
   case MESA_SHADER_TESS_CTRL:
      first_exec_offset = REG_A6XX_SP_HS_OBJ_FIRST_EXEC_OFFSET;
      instrlen = REG_A6XX_SP_HS_INSTRLEN;
      hw_stack_offset = REG_A6XX_SP_HS_PVT_MEM_HW_STACK_OFFSET;
      break;
   case MESA_SHADER_TESS_EVAL:
      first_exec_offset = REG_A6XX_SP_DS_OBJ_FIRST_EXEC_OFFSET;
      instrlen = REG_A6XX_SP_DS_INSTRLEN;
      hw_stack_offset = REG_A6XX_SP_DS_PVT_MEM_HW_STACK_OFFSET;
      break;
   case MESA_SHADER_GEOMETRY:
      first_exec_offset = REG_A6XX_SP_GS_OBJ_FIRST_EXEC_OFFSET;
      instrlen = REG_A6XX_SP_GS_INSTRLEN;
      hw_stack_offset = REG_A6XX_SP_GS_PVT_MEM_HW_STACK_OFFSET;
      break;
   case MESA_SHADER_FRAGMENT:
      first_exec_offset = REG_A6XX_SP_FS_OBJ_FIRST_EXEC_OFFSET;
      instrlen = REG_A6XX_SP_FS_INSTRLEN;
      hw_stack_offset = REG_A6XX_SP_FS_PVT_MEM_HW_STACK_OFFSET;
      break;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      first_exec_offset = REG_A6XX_SP_CS_OBJ_FIRST_EXEC_OFFSET;
      instrlen = REG_A6XX_SP_CS_INSTRLEN;
      hw_stack_offset = REG_A6XX_SP_CS_PVT_MEM_HW_STACK_OFFSET;
      break;
   case MESA_SHADER_TASK:
   case MESA_SHADER_MESH:
   case MESA_SHADER_RAYGEN:
   case MESA_SHADER_ANY_HIT:
   case MESA_SHADER_CLOSEST_HIT:
   case MESA_SHADER_MISS:
   case MESA_SHADER_INTERSECTION:
   case MESA_SHADER_CALLABLE:
      unreachable("Unsupported shader stage");
   case MESA_SHADER_NONE:
      unreachable("");
   }

#ifdef DEBUG
   /* Name should generally match what you get with MESA_SHADER_CAPTURE_PATH: */
   const char *name = so->shader->nir->info.name;
   if (name)
      fd_emit_string5(ring, name, strlen(name));
#endif

   uint32_t fibers_per_sp = ctx->screen->info->a6xx.fibers_per_sp;
   uint32_t num_sp_cores = ctx->screen->info->num_sp_cores;

   uint32_t per_fiber_size = ALIGN(so->pvtmem_size, 512);
   if (per_fiber_size > ctx->pvtmem[so->pvtmem_per_wave].per_fiber_size) {
      if (ctx->pvtmem[so->pvtmem_per_wave].bo)
         fd_bo_del(ctx->pvtmem[so->pvtmem_per_wave].bo);
      ctx->pvtmem[so->pvtmem_per_wave].per_fiber_size = per_fiber_size;
      uint32_t total_size =
         ALIGN(per_fiber_size * fibers_per_sp, 1 << 12) * num_sp_cores;
      ctx->pvtmem[so->pvtmem_per_wave].bo = fd_bo_new(
         ctx->screen->dev, total_size, 0,
         "pvtmem_%s_%d", so->pvtmem_per_wave ? "per_wave" : "per_fiber",
         per_fiber_size);
   } else {
      per_fiber_size = ctx->pvtmem[so->pvtmem_per_wave].per_fiber_size;
   }

   uint32_t per_sp_size = ALIGN(per_fiber_size * fibers_per_sp, 1 << 12);

   OUT_PKT4(ring, instrlen, 1);
   OUT_RING(ring, so->instrlen);

   OUT_PKT4(ring, first_exec_offset, 7);
   OUT_RING(ring, 0);                /* SP_xS_OBJ_FIRST_EXEC_OFFSET */
   OUT_RELOC(ring, so->bo, 0, 0, 0); /* SP_xS_OBJ_START_LO */
   OUT_RING(ring, A6XX_SP_VS_PVT_MEM_PARAM_MEMSIZEPERITEM(per_fiber_size));
   if (so->pvtmem_size > 0) { /* SP_xS_PVT_MEM_ADDR */
      OUT_RELOC(ring, ctx->pvtmem[so->pvtmem_per_wave].bo, 0, 0, 0);
   } else {
      OUT_RING(ring, 0);
      OUT_RING(ring, 0);
   }
   OUT_RING(ring, A6XX_SP_VS_PVT_MEM_SIZE_TOTALPVTMEMSIZE(per_sp_size) |
                     COND(so->pvtmem_per_wave,
                          A6XX_SP_VS_PVT_MEM_SIZE_PERWAVEMEMLAYOUT));

   OUT_PKT4(ring, hw_stack_offset, 1);
   OUT_RING(ring, A6XX_SP_VS_PVT_MEM_HW_STACK_OFFSET_OFFSET(per_sp_size));

   uint32_t shader_preload_size =
      MIN2(so->instrlen, ctx->screen->info->a6xx.instr_cache_size);

   OUT_PKT7(ring, fd6_stage2opcode(so->type), 3);
   OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
                     CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
                     CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                     CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
                     CP_LOAD_STATE6_0_NUM_UNIT(shader_preload_size));
   OUT_RELOC(ring, so->bo, 0, 0, 0);
}

/**
 * Build a pre-baked state-obj to disable SO, so that we aren't dynamically
 * building this at draw time whenever we transition from SO enabled->disabled
 */
static void
setup_stream_out_disable(struct fd_context *ctx)
{
   unsigned sizedw = 4;

   if (ctx->screen->info->a6xx.tess_use_shared)
      sizedw += 2;

   struct fd_ringbuffer *ring =
      fd_ringbuffer_new_object(ctx->pipe, (1 + sizedw) * 4);

   OUT_PKT7(ring, CP_CONTEXT_REG_BUNCH, sizedw);
   OUT_RING(ring, REG_A6XX_VPC_SO_CNTL);
   OUT_RING(ring, 0);
   OUT_RING(ring, REG_A6XX_VPC_SO_STREAM_CNTL);
   OUT_RING(ring, 0);

   if (ctx->screen->info->a6xx.tess_use_shared) {
      OUT_RING(ring, REG_A6XX_PC_SO_STREAM_CNTL);
      OUT_RING(ring, 0);
   }

   fd6_context(ctx)->streamout_disable_stateobj = ring;
}

static void
setup_stream_out(struct fd_context *ctx, struct fd6_program_state *state,
                 const struct ir3_shader_variant *v,
                 struct ir3_shader_linkage *l)
{
   const struct ir3_stream_output_info *strmout = &v->shader->stream_output;

   /* Note: 64 here comes from the HW layout of the program RAM. The program
    * for stream N is at DWORD 64 * N.
    */
#define A6XX_SO_PROG_DWORDS 64
   uint32_t prog[A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS] = {};
   BITSET_DECLARE(valid_dwords, A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) = {0};
   uint32_t ncomp[PIPE_MAX_SO_BUFFERS];

   memset(ncomp, 0, sizeof(ncomp));
   memset(prog, 0, sizeof(prog));

   for (unsigned i = 0; i < strmout->num_outputs; i++) {
      const struct ir3_stream_output *out = &strmout->output[i];
      unsigned k = out->register_index;
      unsigned idx;

      ncomp[out->output_buffer] += out->num_components;

      /* linkage map sorted by order frag shader wants things, so
       * a bit less ideal here..
       */
      for (idx = 0; idx < l->cnt; idx++)
         if (l->var[idx].slot == v->outputs[k].slot)
            break;

      debug_assert(idx < l->cnt);

      for (unsigned j = 0; j < out->num_components; j++) {
         unsigned c = j + out->start_component;
         unsigned loc = l->var[idx].loc + c;
         unsigned off = j + out->dst_offset; /* in dwords */

         unsigned dword = out->stream * A6XX_SO_PROG_DWORDS + loc/2;
         if (loc & 1) {
            prog[dword] |= A6XX_VPC_SO_PROG_B_EN |
                           A6XX_VPC_SO_PROG_B_BUF(out->output_buffer) |
                           A6XX_VPC_SO_PROG_B_OFF(off * 4);
         } else {
            prog[dword] |= A6XX_VPC_SO_PROG_A_EN |
                           A6XX_VPC_SO_PROG_A_BUF(out->output_buffer) |
                           A6XX_VPC_SO_PROG_A_OFF(off * 4);
         }
         BITSET_SET(valid_dwords, dword);
      }
   }

   unsigned prog_count = 0;
   unsigned start, end;
   BITSET_FOREACH_RANGE (start, end, valid_dwords,
                         A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) {
      prog_count += end - start + 1;
   }

   unsigned sizedw = 10 + (2 * prog_count);
   if (ctx->screen->info->a6xx.tess_use_shared)
      sizedw += 2;

   struct fd_ringbuffer *ring =
      fd_ringbuffer_new_object(ctx->pipe, (1 + sizedw) * 4);

   OUT_PKT7(ring, CP_CONTEXT_REG_BUNCH, sizedw);
   OUT_RING(ring, REG_A6XX_VPC_SO_STREAM_CNTL);
   OUT_RING(ring,
            A6XX_VPC_SO_STREAM_CNTL_STREAM_ENABLE(0x1) |
               COND(ncomp[0] > 0, A6XX_VPC_SO_STREAM_CNTL_BUF0_STREAM(1)) |
               COND(ncomp[1] > 0, A6XX_VPC_SO_STREAM_CNTL_BUF1_STREAM(1)) |
               COND(ncomp[2] > 0, A6XX_VPC_SO_STREAM_CNTL_BUF2_STREAM(1)) |
               COND(ncomp[3] > 0, A6XX_VPC_SO_STREAM_CNTL_BUF3_STREAM(1)));
   OUT_RING(ring, REG_A6XX_VPC_SO_NCOMP(0));
   OUT_RING(ring, ncomp[0]);
   OUT_RING(ring, REG_A6XX_VPC_SO_NCOMP(1));
   OUT_RING(ring, ncomp[1]);
   OUT_RING(ring, REG_A6XX_VPC_SO_NCOMP(2));
   OUT_RING(ring, ncomp[2]);
   OUT_RING(ring, REG_A6XX_VPC_SO_NCOMP(3));
   OUT_RING(ring, ncomp[3]);

   bool first = true;
   BITSET_FOREACH_RANGE (start, end, valid_dwords,
                         A6XX_SO_PROG_DWORDS * IR3_MAX_SO_STREAMS) {
      OUT_RING(ring, REG_A6XX_VPC_SO_CNTL);
      OUT_RING(ring, COND(first, A6XX_VPC_SO_CNTL_RESET) |
                     A6XX_VPC_SO_CNTL_ADDR(start));
      for (unsigned i = start; i < end; i++) {
         OUT_RING(ring, REG_A6XX_VPC_SO_PROG);
         OUT_RING(ring, prog[i]);
      }
      first = false;
   }

   if (ctx->screen->info->a6xx.tess_use_shared) {
      /* Possibly not tess_use_shared related, but the combination of
       * tess + xfb fails some tests if we don't emit this.
       */
      OUT_RING(ring, REG_A6XX_PC_SO_STREAM_CNTL);
      OUT_RING(ring, A6XX_PC_SO_STREAM_CNTL_STREAM_ENABLE);
   }

   state->streamout_stateobj = ring;
}

static void
setup_config_stateobj(struct fd_context *ctx, struct fd6_program_state *state)
{
   struct fd_ringbuffer *ring = fd_ringbuffer_new_object(ctx->pipe, 100 * 4);

   OUT_REG(ring, A6XX_HLSQ_INVALIDATE_CMD(.vs_state = true, .hs_state = true,
                                          .ds_state = true, .gs_state = true,
                                          .fs_state = true, .cs_state = true,
                                          .gfx_ibo = true, .cs_ibo = true, ));

   debug_assert(state->vs->constlen >= state->bs->constlen);

   OUT_PKT4(ring, REG_A6XX_HLSQ_VS_CNTL, 4);
   OUT_RING(ring, A6XX_HLSQ_VS_CNTL_CONSTLEN(state->vs->constlen) |
                     A6XX_HLSQ_VS_CNTL_ENABLED);
   OUT_RING(ring, COND(state->hs,
                       A6XX_HLSQ_HS_CNTL_ENABLED |
                          A6XX_HLSQ_HS_CNTL_CONSTLEN(state->hs->constlen)));
   OUT_RING(ring, COND(state->ds,
                       A6XX_HLSQ_DS_CNTL_ENABLED |
                          A6XX_HLSQ_DS_CNTL_CONSTLEN(state->ds->constlen)));
   OUT_RING(ring, COND(state->gs,
                       A6XX_HLSQ_GS_CNTL_ENABLED |
                          A6XX_HLSQ_GS_CNTL_CONSTLEN(state->gs->constlen)));
   OUT_PKT4(ring, REG_A6XX_HLSQ_FS_CNTL, 1);
   OUT_RING(ring, A6XX_HLSQ_FS_CNTL_CONSTLEN(state->fs->constlen) |
                     A6XX_HLSQ_FS_CNTL_ENABLED);

   OUT_PKT4(ring, REG_A6XX_SP_VS_CONFIG, 1);
   OUT_RING(ring, COND(state->vs, A6XX_SP_VS_CONFIG_ENABLED) |
                     A6XX_SP_VS_CONFIG_NIBO(ir3_shader_nibo(state->vs)) |
                     A6XX_SP_VS_CONFIG_NTEX(state->vs->num_samp) |
                     A6XX_SP_VS_CONFIG_NSAMP(state->vs->num_samp));

   OUT_PKT4(ring, REG_A6XX_SP_HS_CONFIG, 1);
   OUT_RING(ring, COND(state->hs,
                       A6XX_SP_HS_CONFIG_ENABLED |
                          A6XX_SP_HS_CONFIG_NIBO(ir3_shader_nibo(state->hs)) |
                          A6XX_SP_HS_CONFIG_NTEX(state->hs->num_samp) |
                          A6XX_SP_HS_CONFIG_NSAMP(state->hs->num_samp)));

   OUT_PKT4(ring, REG_A6XX_SP_DS_CONFIG, 1);
   OUT_RING(ring, COND(state->ds,
                       A6XX_SP_DS_CONFIG_ENABLED |
                          A6XX_SP_DS_CONFIG_NIBO(ir3_shader_nibo(state->ds)) |
                          A6XX_SP_DS_CONFIG_NTEX(state->ds->num_samp) |
                          A6XX_SP_DS_CONFIG_NSAMP(state->ds->num_samp)));

   OUT_PKT4(ring, REG_A6XX_SP_GS_CONFIG, 1);
   OUT_RING(ring, COND(state->gs,
                       A6XX_SP_GS_CONFIG_ENABLED |
                          A6XX_SP_GS_CONFIG_NIBO(ir3_shader_nibo(state->gs)) |
                          A6XX_SP_GS_CONFIG_NTEX(state->gs->num_samp) |
                          A6XX_SP_GS_CONFIG_NSAMP(state->gs->num_samp)));

   OUT_PKT4(ring, REG_A6XX_SP_FS_CONFIG, 1);
   OUT_RING(ring, COND(state->fs, A6XX_SP_FS_CONFIG_ENABLED) |
                     A6XX_SP_FS_CONFIG_NIBO(ir3_shader_nibo(state->fs)) |
                     A6XX_SP_FS_CONFIG_NTEX(state->fs->num_samp) |
                     A6XX_SP_FS_CONFIG_NSAMP(state->fs->num_samp));

   OUT_PKT4(ring, REG_A6XX_SP_IBO_COUNT, 1);
   OUT_RING(ring, ir3_shader_nibo(state->fs));

   state->config_stateobj = ring;
}

static inline uint32_t
next_regid(uint32_t reg, uint32_t increment)
{
   if (VALIDREG(reg))
      return reg + increment;
   else
      return regid(63, 0);
}

static void
fd6_emit_tess_bos(struct fd_screen *screen, struct fd_ringbuffer *ring,
                  const struct ir3_shader_variant *s) assert_dt
{
   const struct ir3_const_state *const_state = ir3_const_state(s);
   const unsigned regid = const_state->offsets.primitive_param + 1;
   uint32_t dwords = 8;

   if (regid >= s->constlen)
      return;

   OUT_PKT7(ring, fd6_stage2opcode(s->type), 7);
   OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(regid) |
                     CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                     CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                     CP_LOAD_STATE6_0_STATE_BLOCK(fd6_stage2shadersb(s->type)) |
                     CP_LOAD_STATE6_0_NUM_UNIT(dwords / 4));
   OUT_RING(ring, 0);
   OUT_RING(ring, 0);
   OUT_RELOC(ring, screen->tess_bo, FD6_TESS_FACTOR_SIZE, 0, 0);
   OUT_RELOC(ring, screen->tess_bo, 0, 0, 0);
}

static void
setup_stateobj(struct fd_ringbuffer *ring, struct fd_context *ctx,
               struct fd6_program_state *state,
               const struct ir3_cache_key *cache_key,
               bool binning_pass) assert_dt
{
   const struct ir3_shader_key *key = &cache_key->key;
   uint32_t pos_regid, psize_regid, color_regid[8], posz_regid;
   uint32_t clip0_regid, clip1_regid;
   uint32_t face_regid, coord_regid, zwcoord_regid, samp_id_regid;
   uint32_t smask_in_regid, smask_regid;
   uint32_t stencilref_regid;
   uint32_t vertex_regid, instance_regid, layer_regid, vs_primitive_regid;
   uint32_t hs_invocation_regid;
   uint32_t tess_coord_x_regid, tess_coord_y_regid, hs_rel_patch_regid,
      ds_rel_patch_regid, ds_primitive_regid;
   uint32_t ij_regid[IJ_COUNT];
   uint32_t gs_header_regid;
   enum a6xx_threadsize fssz;
   uint8_t psize_loc = ~0, pos_loc = ~0, layer_loc = ~0;
   uint8_t clip0_loc, clip1_loc;
   int i, j;

   static const struct ir3_shader_variant dummy_fs = {0};
   const struct ir3_shader_variant *vs = binning_pass ? state->bs : state->vs;
   const struct ir3_shader_variant *hs = state->hs;
   const struct ir3_shader_variant *ds = state->ds;
   const struct ir3_shader_variant *gs = state->gs;
   const struct ir3_shader_variant *fs = binning_pass ? &dummy_fs : state->fs;

   /* binning VS is wrong when GS is present, so use nonbinning VS
    * TODO: compile both binning VS/GS variants correctly
    */
   if (binning_pass && state->gs)
      vs = state->vs;

   bool sample_shading = fs->per_samp | key->sample_shading;

   fssz = fs->info.double_threadsize ? THREAD128 : THREAD64;

   pos_regid = ir3_find_output_regid(vs, VARYING_SLOT_POS);
   psize_regid = ir3_find_output_regid(vs, VARYING_SLOT_PSIZ);
   clip0_regid = ir3_find_output_regid(vs, VARYING_SLOT_CLIP_DIST0);
   clip1_regid = ir3_find_output_regid(vs, VARYING_SLOT_CLIP_DIST1);
   layer_regid = ir3_find_output_regid(vs, VARYING_SLOT_LAYER);
   vertex_regid = ir3_find_sysval_regid(vs, SYSTEM_VALUE_VERTEX_ID);
   instance_regid = ir3_find_sysval_regid(vs, SYSTEM_VALUE_INSTANCE_ID);
   if (hs)
      vs_primitive_regid = ir3_find_sysval_regid(hs, SYSTEM_VALUE_PRIMITIVE_ID);
   else if (gs)
      vs_primitive_regid = ir3_find_sysval_regid(gs, SYSTEM_VALUE_PRIMITIVE_ID);
   else
      vs_primitive_regid = regid(63, 0);

   bool hs_reads_primid = false, ds_reads_primid = false;
   if (hs) {
      tess_coord_x_regid = ir3_find_sysval_regid(ds, SYSTEM_VALUE_TESS_COORD);
      tess_coord_y_regid = next_regid(tess_coord_x_regid, 1);
      hs_reads_primid = VALIDREG(ir3_find_sysval_regid(hs, SYSTEM_VALUE_PRIMITIVE_ID));
      ds_reads_primid = VALIDREG(ir3_find_sysval_regid(ds, SYSTEM_VALUE_PRIMITIVE_ID));
      hs_rel_patch_regid = ir3_find_sysval_regid(hs, SYSTEM_VALUE_REL_PATCH_ID_IR3);
      ds_rel_patch_regid = ir3_find_sysval_regid(ds, SYSTEM_VALUE_REL_PATCH_ID_IR3);
      ds_primitive_regid = ir3_find_sysval_regid(ds, SYSTEM_VALUE_PRIMITIVE_ID);
      hs_invocation_regid =
         ir3_find_sysval_regid(hs, SYSTEM_VALUE_TCS_HEADER_IR3);

      pos_regid = ir3_find_output_regid(ds, VARYING_SLOT_POS);
      psize_regid = ir3_find_output_regid(ds, VARYING_SLOT_PSIZ);
      clip0_regid = ir3_find_output_regid(ds, VARYING_SLOT_CLIP_DIST0);
      clip1_regid = ir3_find_output_regid(ds, VARYING_SLOT_CLIP_DIST1);
   } else {
      tess_coord_x_regid = regid(63, 0);
      tess_coord_y_regid = regid(63, 0);
      hs_rel_patch_regid = regid(63, 0);
      ds_rel_patch_regid = regid(63, 0);
      ds_primitive_regid = regid(63, 0);
      hs_invocation_regid = regid(63, 0);
   }

   bool gs_reads_primid = false;
   if (gs) {
      gs_header_regid = ir3_find_sysval_regid(gs, SYSTEM_VALUE_GS_HEADER_IR3);
      gs_reads_primid = VALIDREG(ir3_find_sysval_regid(gs, SYSTEM_VALUE_PRIMITIVE_ID));
      pos_regid = ir3_find_output_regid(gs, VARYING_SLOT_POS);
      psize_regid = ir3_find_output_regid(gs, VARYING_SLOT_PSIZ);
      clip0_regid = ir3_find_output_regid(gs, VARYING_SLOT_CLIP_DIST0);
      clip1_regid = ir3_find_output_regid(gs, VARYING_SLOT_CLIP_DIST1);
      layer_regid = ir3_find_output_regid(gs, VARYING_SLOT_LAYER);
   } else {
      gs_header_regid = regid(63, 0);
   }

   if (fs->color0_mrt) {
      color_regid[0] = color_regid[1] = color_regid[2] = color_regid[3] =
         color_regid[4] = color_regid[5] = color_regid[6] = color_regid[7] =
            ir3_find_output_regid(fs, FRAG_RESULT_COLOR);
   } else {
      color_regid[0] = ir3_find_output_regid(fs, FRAG_RESULT_DATA0);
      color_regid[1] = ir3_find_output_regid(fs, FRAG_RESULT_DATA1);
      color_regid[2] = ir3_find_output_regid(fs, FRAG_RESULT_DATA2);
      color_regid[3] = ir3_find_output_regid(fs, FRAG_RESULT_DATA3);
      color_regid[4] = ir3_find_output_regid(fs, FRAG_RESULT_DATA4);
      color_regid[5] = ir3_find_output_regid(fs, FRAG_RESULT_DATA5);
      color_regid[6] = ir3_find_output_regid(fs, FRAG_RESULT_DATA6);
      color_regid[7] = ir3_find_output_regid(fs, FRAG_RESULT_DATA7);
   }

   samp_id_regid = ir3_find_sysval_regid(fs, SYSTEM_VALUE_SAMPLE_ID);
   smask_in_regid = ir3_find_sysval_regid(fs, SYSTEM_VALUE_SAMPLE_MASK_IN);
   face_regid = ir3_find_sysval_regid(fs, SYSTEM_VALUE_FRONT_FACE);
   coord_regid = ir3_find_sysval_regid(fs, SYSTEM_VALUE_FRAG_COORD);
   zwcoord_regid = next_regid(coord_regid, 2);
   posz_regid = ir3_find_output_regid(fs, FRAG_RESULT_DEPTH);
   smask_regid = ir3_find_output_regid(fs, FRAG_RESULT_SAMPLE_MASK);
   stencilref_regid = ir3_find_output_regid(fs, FRAG_RESULT_STENCIL);
   for (unsigned i = 0; i < ARRAY_SIZE(ij_regid); i++)
      ij_regid[i] =
         ir3_find_sysval_regid(fs, SYSTEM_VALUE_BARYCENTRIC_PERSP_PIXEL + i);

   /* If we have pre-dispatch texture fetches, then ij_pix should not
    * be DCE'd, even if not actually used in the shader itself:
    */
   if (fs->num_sampler_prefetch > 0) {
      assert(VALIDREG(ij_regid[IJ_PERSP_PIXEL]));
      /* also, it seems like ij_pix is *required* to be r0.x */
      assert(ij_regid[IJ_PERSP_PIXEL] == regid(0, 0));
   }

   /* we can't write gl_SampleMask for !msaa..  if b0 is zero then we
    * end up masking the single sample!!
    */
   if (!key->msaa)
      smask_regid = regid(63, 0);

   /* we could probably divide this up into things that need to be
    * emitted if frag-prog is dirty vs if vert-prog is dirty..
    */

   OUT_PKT4(ring, REG_A6XX_SP_FS_PREFETCH_CNTL, 1 + fs->num_sampler_prefetch);
   OUT_RING(ring, A6XX_SP_FS_PREFETCH_CNTL_COUNT(fs->num_sampler_prefetch) |
                     A6XX_SP_FS_PREFETCH_CNTL_UNK4(regid(63, 0)) |
                     0x7000); // XXX
   for (int i = 0; i < fs->num_sampler_prefetch; i++) {
      const struct ir3_sampler_prefetch *prefetch = &fs->sampler_prefetch[i];
      OUT_RING(ring,
               A6XX_SP_FS_PREFETCH_CMD_SRC(prefetch->src) |
                  A6XX_SP_FS_PREFETCH_CMD_SAMP_ID(prefetch->samp_id) |
                  A6XX_SP_FS_PREFETCH_CMD_TEX_ID(prefetch->tex_id) |
                  A6XX_SP_FS_PREFETCH_CMD_DST(prefetch->dst) |
                  A6XX_SP_FS_PREFETCH_CMD_WRMASK(prefetch->wrmask) |
                  COND(prefetch->half_precision, A6XX_SP_FS_PREFETCH_CMD_HALF) |
                  A6XX_SP_FS_PREFETCH_CMD_CMD(prefetch->cmd));
   }

   OUT_PKT4(ring, REG_A6XX_SP_UNKNOWN_A9A8, 1);
   OUT_RING(ring, 0);

   OUT_PKT4(ring, REG_A6XX_SP_MODE_CONTROL, 1);
   OUT_RING(ring, A6XX_SP_MODE_CONTROL_CONSTANT_DEMOTION_ENABLE | 4);

   bool fs_has_dual_src_color =
      !binning_pass && fs->shader->nir->info.fs.color_is_dual_source;

   OUT_PKT4(ring, REG_A6XX_SP_FS_OUTPUT_CNTL0, 1);
   OUT_RING(ring,
            A6XX_SP_FS_OUTPUT_CNTL0_DEPTH_REGID(posz_regid) |
               A6XX_SP_FS_OUTPUT_CNTL0_SAMPMASK_REGID(smask_regid) |
               A6XX_SP_FS_OUTPUT_CNTL0_STENCILREF_REGID(stencilref_regid) |
               COND(fs_has_dual_src_color,
                    A6XX_SP_FS_OUTPUT_CNTL0_DUAL_COLOR_IN_ENABLE));

   OUT_PKT4(ring, REG_A6XX_SP_VS_CTRL_REG0, 1);
   OUT_RING(
      ring,
      A6XX_SP_VS_CTRL_REG0_FULLREGFOOTPRINT(vs->info.max_reg + 1) |
         A6XX_SP_VS_CTRL_REG0_HALFREGFOOTPRINT(vs->info.max_half_reg + 1) |
         COND(vs->mergedregs, A6XX_SP_VS_CTRL_REG0_MERGEDREGS) |
         A6XX_SP_VS_CTRL_REG0_BRANCHSTACK(ir3_shader_branchstack_hw(vs)));

   fd6_emit_shader(ctx, ring, vs);
   fd6_emit_immediates(ctx->screen, vs, ring);
   if (hs) {
      fd6_emit_tess_bos(ctx->screen, ring, hs);
      fd6_emit_tess_bos(ctx->screen, ring, ds);
   }

   struct ir3_shader_linkage l = {0};
   const struct ir3_shader_variant *last_shader = fd6_last_shader(state);

   bool do_streamout = (last_shader->shader->stream_output.num_outputs > 0);
   uint8_t clip_mask = last_shader->clip_mask,
           cull_mask = last_shader->cull_mask;
   uint8_t clip_cull_mask = clip_mask | cull_mask;

   clip_mask &= cache_key->clip_plane_enable;

   /* If we have streamout, link against the real FS, rather than the
    * dummy FS used for binning pass state, to ensure the OUTLOC's
    * match.  Depending on whether we end up doing sysmem or gmem,
    * the actual streamout could happen with either the binning pass
    * or draw pass program, but the same streamout stateobj is used
    * in either case:
    */
   ir3_link_shaders(&l, last_shader, do_streamout ? state->fs : fs, true);

   bool primid_passthru = l.primid_loc != 0xff;
   clip0_loc = l.clip0_loc;
   clip1_loc = l.clip1_loc;

   OUT_PKT4(ring, REG_A6XX_VPC_VAR_DISABLE(0), 4);
   OUT_RING(ring, ~l.varmask[0]); /* VPC_VAR[0].DISABLE */
   OUT_RING(ring, ~l.varmask[1]); /* VPC_VAR[1].DISABLE */
   OUT_RING(ring, ~l.varmask[2]); /* VPC_VAR[2].DISABLE */
   OUT_RING(ring, ~l.varmask[3]); /* VPC_VAR[3].DISABLE */

   /* Add stream out outputs after computing the VPC_VAR_DISABLE bitmask. */
   ir3_link_stream_out(&l, last_shader);

   if (VALIDREG(layer_regid)) {
      layer_loc = l.max_loc;
      ir3_link_add(&l, VARYING_SLOT_LAYER, layer_regid, 0x1, l.max_loc);
   }

   if (VALIDREG(pos_regid)) {
      pos_loc = l.max_loc;
      ir3_link_add(&l, VARYING_SLOT_POS, pos_regid, 0xf, l.max_loc);
   }

   if (VALIDREG(psize_regid)) {
      psize_loc = l.max_loc;
      ir3_link_add(&l, VARYING_SLOT_PSIZ, psize_regid, 0x1, l.max_loc);
   }

   /* Handle the case where clip/cull distances aren't read by the FS. Make
    * sure to avoid adding an output with an empty writemask if the user
    * disables all the clip distances in the API so that the slot is unused.
    */
   if (clip0_loc == 0xff && VALIDREG(clip0_regid) &&
       (clip_cull_mask & 0xf) != 0) {
      clip0_loc = l.max_loc;
      ir3_link_add(&l, VARYING_SLOT_CLIP_DIST0, clip0_regid,
                   clip_cull_mask & 0xf, l.max_loc);
   }

   if (clip1_loc == 0xff && VALIDREG(clip1_regid) &&
       (clip_cull_mask >> 4) != 0) {
      clip1_loc = l.max_loc;
      ir3_link_add(&l, VARYING_SLOT_CLIP_DIST1, clip1_regid,
                   clip_cull_mask >> 4, l.max_loc);
   }

   /* If we have stream-out, we use the full shader for binning
    * pass, rather than the optimized binning pass one, so that we
    * have all the varying outputs available for xfb.  So streamout
    * state should always be derived from the non-binning pass
    * program:
    */
   if (do_streamout && !binning_pass) {
      setup_stream_out(ctx, state, last_shader, &l);

      if (!fd6_context(ctx)->streamout_disable_stateobj)
         setup_stream_out_disable(ctx);
   }

   debug_assert(l.cnt <= 32);
   if (gs)
      OUT_PKT4(ring, REG_A6XX_SP_GS_OUT_REG(0), DIV_ROUND_UP(l.cnt, 2));
   else if (ds)
      OUT_PKT4(ring, REG_A6XX_SP_DS_OUT_REG(0), DIV_ROUND_UP(l.cnt, 2));
   else
      OUT_PKT4(ring, REG_A6XX_SP_VS_OUT_REG(0), DIV_ROUND_UP(l.cnt, 2));

   for (j = 0; j < l.cnt;) {
      uint32_t reg = 0;

      reg |= A6XX_SP_VS_OUT_REG_A_REGID(l.var[j].regid);
      reg |= A6XX_SP_VS_OUT_REG_A_COMPMASK(l.var[j].compmask);
      j++;

      reg |= A6XX_SP_VS_OUT_REG_B_REGID(l.var[j].regid);
      reg |= A6XX_SP_VS_OUT_REG_B_COMPMASK(l.var[j].compmask);
      j++;

      OUT_RING(ring, reg);
   }

   if (gs)
      OUT_PKT4(ring, REG_A6XX_SP_GS_VPC_DST_REG(0), DIV_ROUND_UP(l.cnt, 4));
   else if (ds)
      OUT_PKT4(ring, REG_A6XX_SP_DS_VPC_DST_REG(0), DIV_ROUND_UP(l.cnt, 4));
   else
      OUT_PKT4(ring, REG_A6XX_SP_VS_VPC_DST_REG(0), DIV_ROUND_UP(l.cnt, 4));

   for (j = 0; j < l.cnt;) {
      uint32_t reg = 0;

      reg |= A6XX_SP_VS_VPC_DST_REG_OUTLOC0(l.var[j++].loc);
      reg |= A6XX_SP_VS_VPC_DST_REG_OUTLOC1(l.var[j++].loc);
      reg |= A6XX_SP_VS_VPC_DST_REG_OUTLOC2(l.var[j++].loc);
      reg |= A6XX_SP_VS_VPC_DST_REG_OUTLOC3(l.var[j++].loc);

      OUT_RING(ring, reg);
   }

   if (hs) {
      assert(vs->mergedregs == hs->mergedregs);
      OUT_PKT4(ring, REG_A6XX_SP_HS_CTRL_REG0, 1);
      OUT_RING(
         ring,
         A6XX_SP_HS_CTRL_REG0_FULLREGFOOTPRINT(hs->info.max_reg + 1) |
            A6XX_SP_HS_CTRL_REG0_HALFREGFOOTPRINT(hs->info.max_half_reg + 1) |
            A6XX_SP_HS_CTRL_REG0_BRANCHSTACK(ir3_shader_branchstack_hw(hs)));

      fd6_emit_shader(ctx, ring, hs);
      fd6_emit_immediates(ctx->screen, hs, ring);
      fd6_emit_link_map(ctx->screen, vs, hs, ring);

      OUT_PKT4(ring, REG_A6XX_SP_DS_CTRL_REG0, 1);
      OUT_RING(
         ring,
         A6XX_SP_DS_CTRL_REG0_FULLREGFOOTPRINT(ds->info.max_reg + 1) |
            A6XX_SP_DS_CTRL_REG0_HALFREGFOOTPRINT(ds->info.max_half_reg + 1) |
            COND(ds->mergedregs, A6XX_SP_DS_CTRL_REG0_MERGEDREGS) |
            A6XX_SP_DS_CTRL_REG0_BRANCHSTACK(ir3_shader_branchstack_hw(ds)));

      fd6_emit_shader(ctx, ring, ds);
      fd6_emit_immediates(ctx->screen, ds, ring);
      fd6_emit_link_map(ctx->screen, hs, ds, ring);

      shader_info *hs_info = &hs->shader->nir->info;
      OUT_PKT4(ring, REG_A6XX_PC_TESS_NUM_VERTEX, 1);
      OUT_RING(ring, hs_info->tess.tcs_vertices_out);

      if (ctx->screen->info->a6xx.tess_use_shared) {
         unsigned hs_input_size = 6 + (3 * (vs->output_size - 1));
         unsigned wave_input_size =
               MIN2(64, DIV_ROUND_UP(hs_input_size * 4,
                                     hs_info->tess.tcs_vertices_out));

         OUT_PKT4(ring, REG_A6XX_PC_HS_INPUT_SIZE, 1);
         OUT_RING(ring, hs_input_size);

         OUT_PKT4(ring, REG_A6XX_SP_HS_WAVE_INPUT_SIZE, 1);
         OUT_RING(ring, wave_input_size);
      } else {
         uint32_t hs_input_size =
               hs_info->tess.tcs_vertices_out * vs->output_size / 4;

         /* Total attribute slots in HS incoming patch. */
         OUT_PKT4(ring, REG_A6XX_PC_HS_INPUT_SIZE, 1);
         OUT_RING(ring, hs_input_size);

         const uint32_t wavesize = 64;
         const uint32_t max_wave_input_size = 64;
         const uint32_t patch_control_points = hs_info->tess.tcs_vertices_out;

         /* note: if HS is really just the VS extended, then this
          * should be by MAX2(patch_control_points, hs_info->tess.tcs_vertices_out)
          * however that doesn't match the blob, and fails some dEQP tests.
          */
         uint32_t prims_per_wave = wavesize / hs_info->tess.tcs_vertices_out;
         uint32_t max_prims_per_wave = max_wave_input_size * wavesize /
               (vs->output_size * patch_control_points);
         prims_per_wave = MIN2(prims_per_wave, max_prims_per_wave);

         uint32_t total_size =
               vs->output_size * patch_control_points * prims_per_wave;
         uint32_t wave_input_size = DIV_ROUND_UP(total_size, wavesize);

         OUT_PKT4(ring, REG_A6XX_SP_HS_WAVE_INPUT_SIZE, 1);
         OUT_RING(ring, wave_input_size);
      }

      shader_info *ds_info = &ds->shader->nir->info;
      OUT_PKT4(ring, REG_A6XX_PC_TESS_CNTL, 1);
      uint32_t output;
      if (ds_info->tess.point_mode)
         output = TESS_POINTS;
      else if (ds_info->tess._primitive_mode == TESS_PRIMITIVE_ISOLINES)
         output = TESS_LINES;
      else if (ds_info->tess.ccw)
         output = TESS_CCW_TRIS;
      else
         output = TESS_CW_TRIS;

      OUT_RING(ring, A6XX_PC_TESS_CNTL_SPACING(
                        fd6_gl2spacing(ds_info->tess.spacing)) |
                        A6XX_PC_TESS_CNTL_OUTPUT(output));

      OUT_PKT4(ring, REG_A6XX_VPC_DS_CLIP_CNTL, 1);
      OUT_RING(ring, A6XX_VPC_DS_CLIP_CNTL_CLIP_MASK(clip_cull_mask) |
                        A6XX_VPC_DS_CLIP_CNTL_CLIP_DIST_03_LOC(clip0_loc) |
                        A6XX_VPC_DS_CLIP_CNTL_CLIP_DIST_47_LOC(clip1_loc));

      OUT_PKT4(ring, REG_A6XX_VPC_DS_LAYER_CNTL, 1);
      OUT_RING(ring, 0x0000ffff);

      OUT_PKT4(ring, REG_A6XX_GRAS_DS_LAYER_CNTL, 1);
      OUT_RING(ring, 0x0);

      OUT_PKT4(ring, REG_A6XX_GRAS_DS_CL_CNTL, 1);
      OUT_RING(ring, A6XX_GRAS_DS_CL_CNTL_CLIP_MASK(clip_mask) |
                        A6XX_GRAS_DS_CL_CNTL_CULL_MASK(cull_mask));

      OUT_PKT4(ring, REG_A6XX_VPC_VS_PACK, 1);
      OUT_RING(ring, A6XX_VPC_VS_PACK_POSITIONLOC(pos_loc) |
                        A6XX_VPC_VS_PACK_PSIZELOC(255) |
                        A6XX_VPC_VS_PACK_STRIDE_IN_VPC(l.max_loc));

      OUT_PKT4(ring, REG_A6XX_VPC_DS_PACK, 1);
      OUT_RING(ring, A6XX_VPC_DS_PACK_POSITIONLOC(pos_loc) |
                        A6XX_VPC_DS_PACK_PSIZELOC(psize_loc) |
                        A6XX_VPC_DS_PACK_STRIDE_IN_VPC(l.max_loc));

      OUT_PKT4(ring, REG_A6XX_SP_DS_PRIMITIVE_CNTL, 1);
      OUT_RING(ring, A6XX_SP_DS_PRIMITIVE_CNTL_OUT(l.cnt));

      OUT_PKT4(ring, REG_A6XX_PC_DS_OUT_CNTL, 1);
      OUT_RING(ring, A6XX_PC_DS_OUT_CNTL_STRIDE_IN_VPC(l.max_loc) |
                        CONDREG(psize_regid, A6XX_PC_DS_OUT_CNTL_PSIZE) |
                        COND(ds_reads_primid, A6XX_PC_DS_OUT_CNTL_PRIMITIVE_ID) |
                        A6XX_PC_DS_OUT_CNTL_CLIP_MASK(clip_cull_mask));

      OUT_PKT4(ring, REG_A6XX_PC_HS_OUT_CNTL, 1);
      OUT_RING(ring, COND(hs_reads_primid, A6XX_PC_HS_OUT_CNTL_PRIMITIVE_ID));
   } else {
      OUT_PKT4(ring, REG_A6XX_SP_HS_WAVE_INPUT_SIZE, 1);
      OUT_RING(ring, 0);
   }

   OUT_PKT4(ring, REG_A6XX_SP_VS_PRIMITIVE_CNTL, 1);
   OUT_RING(ring, A6XX_SP_VS_PRIMITIVE_CNTL_OUT(l.cnt));

   bool enable_varyings = fs->total_in > 0;

   OUT_PKT4(ring, REG_A6XX_VPC_CNTL_0, 1);
   OUT_RING(ring, A6XX_VPC_CNTL_0_NUMNONPOSVAR(fs->total_in) |
                     COND(enable_varyings, A6XX_VPC_CNTL_0_VARYING) |
                     A6XX_VPC_CNTL_0_PRIMIDLOC(l.primid_loc) |
                     A6XX_VPC_CNTL_0_VIEWIDLOC(0xff));

   OUT_PKT4(ring, REG_A6XX_PC_VS_OUT_CNTL, 1);
   OUT_RING(ring, A6XX_PC_VS_OUT_CNTL_STRIDE_IN_VPC(l.max_loc) |
                     CONDREG(psize_regid, A6XX_PC_VS_OUT_CNTL_PSIZE) |
                     CONDREG(layer_regid, A6XX_PC_VS_OUT_CNTL_LAYER) |
                     A6XX_PC_VS_OUT_CNTL_CLIP_MASK(clip_cull_mask));

   OUT_PKT4(ring, REG_A6XX_HLSQ_CONTROL_1_REG, 5);
   OUT_RING(ring, 0x7); /* XXX */
   OUT_RING(ring, A6XX_HLSQ_CONTROL_2_REG_FACEREGID(face_regid) |
                     A6XX_HLSQ_CONTROL_2_REG_SAMPLEID(samp_id_regid) |
                     A6XX_HLSQ_CONTROL_2_REG_SAMPLEMASK(smask_in_regid) |
                     A6XX_HLSQ_CONTROL_2_REG_SIZE(ij_regid[IJ_PERSP_SIZE]));
   OUT_RING(
      ring,
      A6XX_HLSQ_CONTROL_3_REG_IJ_PERSP_PIXEL(ij_regid[IJ_PERSP_PIXEL]) |
         A6XX_HLSQ_CONTROL_3_REG_IJ_LINEAR_PIXEL(ij_regid[IJ_LINEAR_PIXEL]) |
         A6XX_HLSQ_CONTROL_3_REG_IJ_PERSP_CENTROID(
            ij_regid[IJ_PERSP_CENTROID]) |
         A6XX_HLSQ_CONTROL_3_REG_IJ_LINEAR_CENTROID(
            ij_regid[IJ_LINEAR_CENTROID]));
   OUT_RING(
      ring,
      A6XX_HLSQ_CONTROL_4_REG_XYCOORDREGID(coord_regid) |
         A6XX_HLSQ_CONTROL_4_REG_ZWCOORDREGID(zwcoord_regid) |
         A6XX_HLSQ_CONTROL_4_REG_IJ_PERSP_SAMPLE(ij_regid[IJ_PERSP_SAMPLE]) |
         A6XX_HLSQ_CONTROL_4_REG_IJ_LINEAR_SAMPLE(ij_regid[IJ_LINEAR_SAMPLE]));
   OUT_RING(ring, 0xfcfc); /* line length (?), foveation quality */

   OUT_PKT4(ring, REG_A6XX_HLSQ_FS_CNTL_0, 1);
   OUT_RING(ring, A6XX_HLSQ_FS_CNTL_0_THREADSIZE(fssz) |
                     COND(enable_varyings, A6XX_HLSQ_FS_CNTL_0_VARYINGS));

   OUT_PKT4(ring, REG_A6XX_SP_FS_CTRL_REG0, 1);
   OUT_RING(
      ring,
      A6XX_SP_FS_CTRL_REG0_THREADSIZE(fssz) |
         COND(enable_varyings, A6XX_SP_FS_CTRL_REG0_VARYING) | 0x1000000 |
         A6XX_SP_FS_CTRL_REG0_FULLREGFOOTPRINT(fs->info.max_reg + 1) |
         A6XX_SP_FS_CTRL_REG0_HALFREGFOOTPRINT(fs->info.max_half_reg + 1) |
         COND(fs->mergedregs, A6XX_SP_FS_CTRL_REG0_MERGEDREGS) |
         A6XX_SP_FS_CTRL_REG0_BRANCHSTACK(ir3_shader_branchstack_hw(fs)) |
         COND(fs->need_pixlod, A6XX_SP_FS_CTRL_REG0_PIXLODENABLE));

   OUT_PKT4(ring, REG_A6XX_VPC_VS_LAYER_CNTL, 1);
   OUT_RING(ring, A6XX_VPC_VS_LAYER_CNTL_LAYERLOC(layer_loc) |
                     A6XX_VPC_VS_LAYER_CNTL_VIEWLOC(0xff));

   bool need_size = fs->frag_face || fs->fragcoord_compmask != 0;
   bool need_size_persamp = false;
   if (VALIDREG(ij_regid[IJ_PERSP_SIZE])) {
      if (sample_shading)
         need_size_persamp = true;
      else
         need_size = true;
   }

   OUT_PKT4(ring, REG_A6XX_GRAS_CNTL, 1);
   OUT_RING(
      ring,
      CONDREG(ij_regid[IJ_PERSP_PIXEL], A6XX_GRAS_CNTL_IJ_PERSP_PIXEL) |
         CONDREG(ij_regid[IJ_PERSP_CENTROID],
                 A6XX_GRAS_CNTL_IJ_PERSP_CENTROID) |
         CONDREG(ij_regid[IJ_PERSP_SAMPLE], A6XX_GRAS_CNTL_IJ_PERSP_SAMPLE) |
         CONDREG(ij_regid[IJ_LINEAR_PIXEL], A6XX_GRAS_CNTL_IJ_LINEAR_PIXEL) |
         CONDREG(ij_regid[IJ_LINEAR_CENTROID],
                 A6XX_GRAS_CNTL_IJ_LINEAR_CENTROID) |
         CONDREG(ij_regid[IJ_LINEAR_SAMPLE], A6XX_GRAS_CNTL_IJ_LINEAR_SAMPLE) |
         COND(need_size, A6XX_GRAS_CNTL_IJ_LINEAR_PIXEL) |
         COND(need_size_persamp, A6XX_GRAS_CNTL_IJ_LINEAR_SAMPLE) |
         COND(fs->fragcoord_compmask != 0,
              A6XX_GRAS_CNTL_COORD_MASK(fs->fragcoord_compmask)));

   OUT_PKT4(ring, REG_A6XX_RB_RENDER_CONTROL0, 2);
   OUT_RING(
      ring,
      CONDREG(ij_regid[IJ_PERSP_PIXEL],
              A6XX_RB_RENDER_CONTROL0_IJ_PERSP_PIXEL) |
         CONDREG(ij_regid[IJ_PERSP_CENTROID],
                 A6XX_RB_RENDER_CONTROL0_IJ_PERSP_CENTROID) |
         CONDREG(ij_regid[IJ_PERSP_SAMPLE],
                 A6XX_RB_RENDER_CONTROL0_IJ_PERSP_SAMPLE) |
         CONDREG(ij_regid[IJ_LINEAR_PIXEL],
              A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_PIXEL) |
         CONDREG(ij_regid[IJ_LINEAR_CENTROID],
                 A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_CENTROID) |
         CONDREG(ij_regid[IJ_LINEAR_SAMPLE],
                 A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_SAMPLE) |
         COND(need_size, A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_PIXEL) |
         COND(enable_varyings, A6XX_RB_RENDER_CONTROL0_UNK10) |
         COND(need_size_persamp, A6XX_RB_RENDER_CONTROL0_IJ_LINEAR_SAMPLE) |
         COND(fs->fragcoord_compmask != 0,
              A6XX_RB_RENDER_CONTROL0_COORD_MASK(fs->fragcoord_compmask)));

   OUT_RING(ring,
            CONDREG(smask_in_regid, A6XX_RB_RENDER_CONTROL1_SAMPLEMASK) |
               CONDREG(samp_id_regid, A6XX_RB_RENDER_CONTROL1_SAMPLEID) |
               CONDREG(ij_regid[IJ_PERSP_SIZE], A6XX_RB_RENDER_CONTROL1_SIZE) |
               COND(fs->frag_face, A6XX_RB_RENDER_CONTROL1_FACENESS));

   OUT_PKT4(ring, REG_A6XX_RB_SAMPLE_CNTL, 1);
   OUT_RING(ring, COND(sample_shading, A6XX_RB_SAMPLE_CNTL_PER_SAMP_MODE));

   OUT_PKT4(ring, REG_A6XX_GRAS_LRZ_PS_INPUT_CNTL, 1);
   OUT_RING(ring,
         CONDREG(samp_id_regid, A6XX_GRAS_LRZ_PS_INPUT_CNTL_SAMPLEID) |
         A6XX_GRAS_LRZ_PS_INPUT_CNTL_FRAGCOORDSAMPLEMODE(
            sample_shading ? FRAGCOORD_SAMPLE : FRAGCOORD_CENTER));

   OUT_PKT4(ring, REG_A6XX_GRAS_SAMPLE_CNTL, 1);
   OUT_RING(ring, COND(sample_shading, A6XX_GRAS_SAMPLE_CNTL_PER_SAMP_MODE));

   OUT_PKT4(ring, REG_A6XX_SP_FS_OUTPUT_REG(0), 8);
   for (i = 0; i < 8; i++) {
      OUT_RING(ring, A6XX_SP_FS_OUTPUT_REG_REGID(color_regid[i]) |
                        COND(color_regid[i] & HALF_REG_ID,
                             A6XX_SP_FS_OUTPUT_REG_HALF_PRECISION));
      if (VALIDREG(color_regid[i])) {
         state->mrt_components |= 0xf << (i * 4);
      }
   }

   /* dual source blending has an extra fs output in the 2nd slot */
   if (fs_has_dual_src_color) {
      state->mrt_components |= 0xf << 4;
   }

   OUT_PKT4(ring, REG_A6XX_VPC_VS_PACK, 1);
   OUT_RING(ring, A6XX_VPC_VS_PACK_POSITIONLOC(pos_loc) |
                     A6XX_VPC_VS_PACK_PSIZELOC(psize_loc) |
                     A6XX_VPC_VS_PACK_STRIDE_IN_VPC(l.max_loc));

   if (gs) {
      assert(gs->mergedregs == (ds ? ds->mergedregs : vs->mergedregs));
      OUT_PKT4(ring, REG_A6XX_SP_GS_CTRL_REG0, 1);
      OUT_RING(
         ring,
         A6XX_SP_GS_CTRL_REG0_FULLREGFOOTPRINT(gs->info.max_reg + 1) |
            A6XX_SP_GS_CTRL_REG0_HALFREGFOOTPRINT(gs->info.max_half_reg + 1) |
            A6XX_SP_GS_CTRL_REG0_BRANCHSTACK(ir3_shader_branchstack_hw(gs)));

      fd6_emit_shader(ctx, ring, gs);
      fd6_emit_immediates(ctx->screen, gs, ring);
      if (ds)
         fd6_emit_link_map(ctx->screen, ds, gs, ring);
      else
         fd6_emit_link_map(ctx->screen, vs, gs, ring);

      OUT_PKT4(ring, REG_A6XX_VPC_GS_PACK, 1);
      OUT_RING(ring, A6XX_VPC_GS_PACK_POSITIONLOC(pos_loc) |
                        A6XX_VPC_GS_PACK_PSIZELOC(psize_loc) |
                        A6XX_VPC_GS_PACK_STRIDE_IN_VPC(l.max_loc));

      OUT_PKT4(ring, REG_A6XX_VPC_GS_LAYER_CNTL, 1);
      OUT_RING(ring, A6XX_VPC_GS_LAYER_CNTL_LAYERLOC(layer_loc) | 0xff00);

      OUT_PKT4(ring, REG_A6XX_GRAS_GS_LAYER_CNTL, 1);
      OUT_RING(ring,
               CONDREG(layer_regid, A6XX_GRAS_GS_LAYER_CNTL_WRITES_LAYER));

      uint32_t flags_regid =
         ir3_find_output_regid(gs, VARYING_SLOT_GS_VERTEX_FLAGS_IR3);

      OUT_PKT4(ring, REG_A6XX_SP_GS_PRIMITIVE_CNTL, 1);
      OUT_RING(ring, A6XX_SP_GS_PRIMITIVE_CNTL_OUT(l.cnt) |
                        A6XX_SP_GS_PRIMITIVE_CNTL_FLAGS_REGID(flags_regid));

      OUT_PKT4(ring, REG_A6XX_PC_GS_OUT_CNTL, 1);
      OUT_RING(ring,
               A6XX_PC_GS_OUT_CNTL_STRIDE_IN_VPC(l.max_loc) |
                  CONDREG(psize_regid, A6XX_PC_GS_OUT_CNTL_PSIZE) |
                  CONDREG(layer_regid, A6XX_PC_GS_OUT_CNTL_LAYER) |
                  COND(gs_reads_primid, A6XX_PC_GS_OUT_CNTL_PRIMITIVE_ID) |
                  A6XX_PC_GS_OUT_CNTL_CLIP_MASK(clip_cull_mask));

      uint32_t output;
      switch (gs->shader->nir->info.gs.output_primitive) {
      case SHADER_PRIM_POINTS:
         output = TESS_POINTS;
         break;
      case SHADER_PRIM_LINE_STRIP:
         output = TESS_LINES;
         break;
      case SHADER_PRIM_TRIANGLE_STRIP:
         output = TESS_CW_TRIS;
         break;
      default:
         unreachable("");
      }
      OUT_PKT4(ring, REG_A6XX_PC_PRIMITIVE_CNTL_5, 1);
      OUT_RING(ring, A6XX_PC_PRIMITIVE_CNTL_5_GS_VERTICES_OUT(
                        gs->shader->nir->info.gs.vertices_out - 1) |
                        A6XX_PC_PRIMITIVE_CNTL_5_GS_OUTPUT(output) |
                        A6XX_PC_PRIMITIVE_CNTL_5_GS_INVOCATIONS(
                           gs->shader->nir->info.gs.invocations - 1));

      OUT_PKT4(ring, REG_A6XX_GRAS_GS_CL_CNTL, 1);
      OUT_RING(ring, A6XX_GRAS_GS_CL_CNTL_CLIP_MASK(clip_mask) |
                        A6XX_GRAS_GS_CL_CNTL_CULL_MASK(cull_mask));

      OUT_PKT4(ring, REG_A6XX_VPC_GS_PARAM, 1);
      OUT_RING(ring, 0xff);

      OUT_PKT4(ring, REG_A6XX_VPC_GS_CLIP_CNTL, 1);
      OUT_RING(ring, A6XX_VPC_GS_CLIP_CNTL_CLIP_MASK(clip_cull_mask) |
                        A6XX_VPC_GS_CLIP_CNTL_CLIP_DIST_03_LOC(clip0_loc) |
                        A6XX_VPC_GS_CLIP_CNTL_CLIP_DIST_47_LOC(clip1_loc));

      const struct ir3_shader_variant *prev = state->ds ? state->ds : state->vs;

      /* Size of per-primitive alloction in ldlw memory in vec4s. */
      uint32_t vec4_size = gs->shader->nir->info.gs.vertices_in *
                           DIV_ROUND_UP(prev->output_size, 4);
      OUT_PKT4(ring, REG_A6XX_PC_PRIMITIVE_CNTL_6, 1);
      OUT_RING(ring, A6XX_PC_PRIMITIVE_CNTL_6_STRIDE_IN_VPC(vec4_size));

      OUT_PKT4(ring, REG_A6XX_PC_MULTIVIEW_CNTL, 1);
      OUT_RING(ring, 0);

      uint32_t prim_size = prev->output_size;
      if (prim_size > 64)
         prim_size = 64;
      else if (prim_size == 64)
         prim_size = 63;
      OUT_PKT4(ring, REG_A6XX_SP_GS_PRIM_SIZE, 1);
      OUT_RING(ring, prim_size);
   } else {
      OUT_PKT4(ring, REG_A6XX_PC_PRIMITIVE_CNTL_6, 1);
      OUT_RING(ring, 0);
      OUT_PKT4(ring, REG_A6XX_SP_GS_PRIM_SIZE, 1);
      OUT_RING(ring, 0);

      OUT_PKT4(ring, REG_A6XX_GRAS_VS_LAYER_CNTL, 1);
      OUT_RING(ring,
               CONDREG(layer_regid, A6XX_GRAS_VS_LAYER_CNTL_WRITES_LAYER));
   }

   OUT_PKT4(ring, REG_A6XX_VPC_VS_CLIP_CNTL, 1);
   OUT_RING(ring, A6XX_VPC_VS_CLIP_CNTL_CLIP_MASK(clip_cull_mask) |
                     A6XX_VPC_VS_CLIP_CNTL_CLIP_DIST_03_LOC(clip0_loc) |
                     A6XX_VPC_VS_CLIP_CNTL_CLIP_DIST_47_LOC(clip1_loc));

   OUT_PKT4(ring, REG_A6XX_GRAS_VS_CL_CNTL, 1);
   OUT_RING(ring, A6XX_GRAS_VS_CL_CNTL_CLIP_MASK(clip_mask) |
                     A6XX_GRAS_VS_CL_CNTL_CULL_MASK(cull_mask));

   OUT_PKT4(ring, REG_A6XX_VPC_UNKNOWN_9107, 1);
   OUT_RING(ring, 0);

   if (fs->instrlen)
      fd6_emit_shader(ctx, ring, fs);

   OUT_REG(ring, A6XX_PC_PRIMID_PASSTHRU(primid_passthru));

   uint32_t non_sysval_input_count = 0;
   for (uint32_t i = 0; i < vs->inputs_count; i++)
      if (!vs->inputs[i].sysval)
         non_sysval_input_count++;

   OUT_PKT4(ring, REG_A6XX_VFD_CONTROL_0, 1);
   OUT_RING(ring, A6XX_VFD_CONTROL_0_FETCH_CNT(non_sysval_input_count) |
                     A6XX_VFD_CONTROL_0_DECODE_CNT(non_sysval_input_count));

   OUT_PKT4(ring, REG_A6XX_VFD_DEST_CNTL(0), non_sysval_input_count);
   for (uint32_t i = 0; i < non_sysval_input_count; i++) {
      assert(vs->inputs[i].compmask);
      OUT_RING(ring,
               A6XX_VFD_DEST_CNTL_INSTR_WRITEMASK(vs->inputs[i].compmask) |
                  A6XX_VFD_DEST_CNTL_INSTR_REGID(vs->inputs[i].regid));
   }

   OUT_PKT4(ring, REG_A6XX_VFD_CONTROL_1, 6);
   OUT_RING(ring, A6XX_VFD_CONTROL_1_REGID4VTX(vertex_regid) |
                     A6XX_VFD_CONTROL_1_REGID4INST(instance_regid) |
                     A6XX_VFD_CONTROL_1_REGID4PRIMID(vs_primitive_regid) |
                     0xfc000000);
   OUT_RING(ring,
            A6XX_VFD_CONTROL_2_REGID_HSRELPATCHID(hs_rel_patch_regid) |
               A6XX_VFD_CONTROL_2_REGID_INVOCATIONID(hs_invocation_regid));
   OUT_RING(ring, A6XX_VFD_CONTROL_3_REGID_DSRELPATCHID(ds_rel_patch_regid) |
                     A6XX_VFD_CONTROL_3_REGID_TESSX(tess_coord_x_regid) |
                     A6XX_VFD_CONTROL_3_REGID_TESSY(tess_coord_y_regid) |
                     A6XX_VFD_CONTROL_3_REGID_DSPRIMID(ds_primitive_regid));
   OUT_RING(ring, 0x000000fc); /* VFD_CONTROL_4 */
   OUT_RING(ring, A6XX_VFD_CONTROL_5_REGID_GSHEADER(gs_header_regid) |
                     0xfc00); /* VFD_CONTROL_5 */
   OUT_RING(ring, COND(primid_passthru,
                       A6XX_VFD_CONTROL_6_PRIMID_PASSTHRU)); /* VFD_CONTROL_6 */

   if (!binning_pass)
      fd6_emit_immediates(ctx->screen, fs, ring);
}

static void emit_interp_state(struct fd_ringbuffer *ring,
                              struct ir3_shader_variant *fs, bool rasterflat,
                              bool sprite_coord_mode,
                              uint32_t sprite_coord_enable);

static struct fd_ringbuffer *
create_interp_stateobj(struct fd_context *ctx, struct fd6_program_state *state)
{
   struct fd_ringbuffer *ring = fd_ringbuffer_new_object(ctx->pipe, 18 * 4);

   emit_interp_state(ring, state->fs, false, false, 0);

   return ring;
}

/* build the program streaming state which is not part of the pre-
 * baked stateobj because of dependency on other gl state (rasterflat
 * or sprite-coord-replacement)
 */
struct fd_ringbuffer *
fd6_program_interp_state(struct fd6_emit *emit)
{
   const struct fd6_program_state *state = fd6_emit_get_prog(emit);

   if (!unlikely(emit->rasterflat || emit->sprite_coord_enable)) {
      /* fastpath: */
      return fd_ringbuffer_ref(state->interp_stateobj);
   } else {
      struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(
         emit->ctx->batch->submit, 18 * 4, FD_RINGBUFFER_STREAMING);

      emit_interp_state(ring, state->fs, emit->rasterflat,
                        emit->sprite_coord_mode, emit->sprite_coord_enable);

      return ring;
   }
}

static void
emit_interp_state(struct fd_ringbuffer *ring, struct ir3_shader_variant *fs,
                  bool rasterflat, bool sprite_coord_mode,
                  uint32_t sprite_coord_enable)
{
   uint32_t vinterp[8], vpsrepl[8];

   memset(vinterp, 0, sizeof(vinterp));
   memset(vpsrepl, 0, sizeof(vpsrepl));

   for (int j = -1; (j = ir3_next_varying(fs, j)) < (int)fs->inputs_count;) {

      /* NOTE: varyings are packed, so if compmask is 0xb
       * then first, third, and fourth component occupy
       * three consecutive varying slots:
       */
      unsigned compmask = fs->inputs[j].compmask;

      uint32_t inloc = fs->inputs[j].inloc;

      if (fs->inputs[j].flat || (fs->inputs[j].rasterflat && rasterflat)) {
         uint32_t loc = inloc;

         for (int i = 0; i < 4; i++) {
            if (compmask & (1 << i)) {
               vinterp[loc / 16] |= 1 << ((loc % 16) * 2);
               loc++;
            }
         }
      }

      bool coord_mode = sprite_coord_mode;
      if (ir3_point_sprite(fs, j, sprite_coord_enable, &coord_mode)) {
         /* mask is two 2-bit fields, where:
          *   '01' -> S
          *   '10' -> T
          *   '11' -> 1 - T  (flip mode)
          */
         unsigned mask = coord_mode ? 0b1101 : 0b1001;
         uint32_t loc = inloc;
         if (compmask & 0x1) {
            vpsrepl[loc / 16] |= ((mask >> 0) & 0x3) << ((loc % 16) * 2);
            loc++;
         }
         if (compmask & 0x2) {
            vpsrepl[loc / 16] |= ((mask >> 2) & 0x3) << ((loc % 16) * 2);
            loc++;
         }
         if (compmask & 0x4) {
            /* .z <- 0.0f */
            vinterp[loc / 16] |= 0b10 << ((loc % 16) * 2);
            loc++;
         }
         if (compmask & 0x8) {
            /* .w <- 1.0f */
            vinterp[loc / 16] |= 0b11 << ((loc % 16) * 2);
            loc++;
         }
      }
   }

   OUT_PKT4(ring, REG_A6XX_VPC_VARYING_INTERP_MODE(0), 8);
   for (int i = 0; i < 8; i++)
      OUT_RING(ring, vinterp[i]); /* VPC_VARYING_INTERP[i].MODE */

   OUT_PKT4(ring, REG_A6XX_VPC_VARYING_PS_REPL_MODE(0), 8);
   for (int i = 0; i < 8; i++)
      OUT_RING(ring, vpsrepl[i]); /* VPC_VARYING_PS_REPL[i] */
}

static struct ir3_program_state *
fd6_program_create(void *data, struct ir3_shader_variant *bs,
                   struct ir3_shader_variant *vs, struct ir3_shader_variant *hs,
                   struct ir3_shader_variant *ds, struct ir3_shader_variant *gs,
                   struct ir3_shader_variant *fs,
                   const struct ir3_cache_key *key) in_dt
{
   struct fd_context *ctx = fd_context(data);
   struct fd_screen *screen = ctx->screen;
   struct fd6_program_state *state = CALLOC_STRUCT(fd6_program_state);

   tc_assert_driver_thread(ctx->tc);

   /* if we have streamout, use full VS in binning pass, as the
    * binning pass VS will have outputs on other than position/psize
    * stripped out:
    */
   state->bs = vs->shader->stream_output.num_outputs ? vs : bs;
   state->vs = vs;
   state->hs = hs;
   state->ds = ds;
   state->gs = gs;
   state->fs = fs;
   state->binning_stateobj = fd_ringbuffer_new_object(ctx->pipe, 0x1000);
   state->stateobj = fd_ringbuffer_new_object(ctx->pipe, 0x1000);

#ifdef DEBUG
   if (!ds) {
      for (unsigned i = 0; i < bs->inputs_count; i++) {
         if (vs->inputs[i].sysval)
            continue;
         debug_assert(bs->inputs[i].regid == vs->inputs[i].regid);
      }
   }
#endif

   if (hs) {
      /* Allocate the fixed-size tess factor BO globally on the screen.  This
       * lets the program (which ideally we would have shared across contexts,
       * though the current ir3_cache impl doesn't do that) bake in the
       * addresses.
       */
      fd_screen_lock(screen);
      if (!screen->tess_bo)
         screen->tess_bo =
            fd_bo_new(screen->dev, FD6_TESS_BO_SIZE, 0, "tessfactor");
      fd_screen_unlock(screen);
   }

   setup_config_stateobj(ctx, state);
   setup_stateobj(state->binning_stateobj, ctx, state, key, true);
   setup_stateobj(state->stateobj, ctx, state, key, false);
   state->interp_stateobj = create_interp_stateobj(ctx, state);

   struct ir3_stream_output_info *stream_output =
      &fd6_last_shader(state)->shader->stream_output;
   if (stream_output->num_outputs > 0)
      state->stream_output = stream_output;

   return &state->base;
}

static void
fd6_program_destroy(void *data, struct ir3_program_state *state)
{
   struct fd6_program_state *so = fd6_program_state(state);
   fd_ringbuffer_del(so->stateobj);
   fd_ringbuffer_del(so->binning_stateobj);
   fd_ringbuffer_del(so->config_stateobj);
   fd_ringbuffer_del(so->interp_stateobj);
   if (so->streamout_stateobj)
      fd_ringbuffer_del(so->streamout_stateobj);
   free(so);
}

static const struct ir3_cache_funcs cache_funcs = {
   .create_state = fd6_program_create,
   .destroy_state = fd6_program_destroy,
};

void
fd6_prog_init(struct pipe_context *pctx)
{
   struct fd_context *ctx = fd_context(pctx);

   ctx->shader_cache = ir3_cache_create(&cache_funcs, ctx);

   ir3_prog_init(pctx);

   fd_prog_init(pctx);
}
