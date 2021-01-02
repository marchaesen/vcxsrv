/*
 * Copyright 2012 Advanced Micro Devices, Inc.
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

#include "ac_debug.h"
#include "si_build_pm4.h"
#include "sid.h"
#include "util/u_index_modify.h"
#include "util/u_log.h"
#include "util/u_prim.h"
#include "util/u_suballoc.h"
#include "util/u_upload_mgr.h"

/* special primitive types */
#define SI_PRIM_RECTANGLE_LIST PIPE_PRIM_MAX

ALWAYS_INLINE
static unsigned si_conv_pipe_prim(unsigned mode)
{
   static const unsigned prim_conv[] = {
      [PIPE_PRIM_POINTS] = V_008958_DI_PT_POINTLIST,
      [PIPE_PRIM_LINES] = V_008958_DI_PT_LINELIST,
      [PIPE_PRIM_LINE_LOOP] = V_008958_DI_PT_LINELOOP,
      [PIPE_PRIM_LINE_STRIP] = V_008958_DI_PT_LINESTRIP,
      [PIPE_PRIM_TRIANGLES] = V_008958_DI_PT_TRILIST,
      [PIPE_PRIM_TRIANGLE_STRIP] = V_008958_DI_PT_TRISTRIP,
      [PIPE_PRIM_TRIANGLE_FAN] = V_008958_DI_PT_TRIFAN,
      [PIPE_PRIM_QUADS] = V_008958_DI_PT_QUADLIST,
      [PIPE_PRIM_QUAD_STRIP] = V_008958_DI_PT_QUADSTRIP,
      [PIPE_PRIM_POLYGON] = V_008958_DI_PT_POLYGON,
      [PIPE_PRIM_LINES_ADJACENCY] = V_008958_DI_PT_LINELIST_ADJ,
      [PIPE_PRIM_LINE_STRIP_ADJACENCY] = V_008958_DI_PT_LINESTRIP_ADJ,
      [PIPE_PRIM_TRIANGLES_ADJACENCY] = V_008958_DI_PT_TRILIST_ADJ,
      [PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY] = V_008958_DI_PT_TRISTRIP_ADJ,
      [PIPE_PRIM_PATCHES] = V_008958_DI_PT_PATCH,
      [SI_PRIM_RECTANGLE_LIST] = V_008958_DI_PT_RECTLIST};
   assert(mode < ARRAY_SIZE(prim_conv));
   return prim_conv[mode];
}

/**
 * This calculates the LDS size for tessellation shaders (VS, TCS, TES).
 * LS.LDS_SIZE is shared by all 3 shader stages.
 *
 * The information about LDS and other non-compile-time parameters is then
 * written to userdata SGPRs.
 */
static void si_emit_derived_tess_state(struct si_context *sctx, const struct pipe_draw_info *info,
                                       unsigned *num_patches)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct si_shader *ls_current;
   struct si_shader_selector *ls;
   /* The TES pointer will only be used for sctx->last_tcs.
    * It would be wrong to think that TCS = TES. */
   struct si_shader_selector *tcs =
      sctx->tcs_shader.cso ? sctx->tcs_shader.cso : sctx->tes_shader.cso;
   unsigned tess_uses_primid = sctx->ia_multi_vgt_param_key.u.tess_uses_prim_id;
   bool has_primid_instancing_bug = sctx->chip_class == GFX6 && sctx->screen->info.max_se == 1;
   unsigned tes_sh_base = sctx->shader_pointers.sh_base[PIPE_SHADER_TESS_EVAL];
   unsigned num_tcs_input_cp = info->vertices_per_patch;
   unsigned num_tcs_output_cp, num_tcs_inputs, num_tcs_outputs;
   unsigned num_tcs_patch_outputs;
   unsigned input_vertex_size, output_vertex_size, pervertex_output_patch_size;
   unsigned input_patch_size, output_patch_size, output_patch0_offset;
   unsigned perpatch_output_offset, lds_per_patch, lds_size;
   unsigned tcs_in_layout, tcs_out_layout, tcs_out_offsets;
   unsigned offchip_layout, target_lds_size, ls_hs_config;

   /* Since GFX9 has merged LS-HS in the TCS state, set LS = TCS. */
   if (sctx->chip_class >= GFX9) {
      if (sctx->tcs_shader.cso)
         ls_current = sctx->tcs_shader.current;
      else
         ls_current = sctx->fixed_func_tcs_shader.current;

      ls = ls_current->key.part.tcs.ls;
   } else {
      ls_current = sctx->vs_shader.current;
      ls = sctx->vs_shader.cso;
   }

   if (sctx->last_ls == ls_current && sctx->last_tcs == tcs &&
       sctx->last_tes_sh_base == tes_sh_base && sctx->last_num_tcs_input_cp == num_tcs_input_cp &&
       (!has_primid_instancing_bug || (sctx->last_tess_uses_primid == tess_uses_primid))) {
      *num_patches = sctx->last_num_patches;
      return;
   }

   sctx->last_ls = ls_current;
   sctx->last_tcs = tcs;
   sctx->last_tes_sh_base = tes_sh_base;
   sctx->last_num_tcs_input_cp = num_tcs_input_cp;
   sctx->last_tess_uses_primid = tess_uses_primid;

   /* This calculates how shader inputs and outputs among VS, TCS, and TES
    * are laid out in LDS. */
   num_tcs_inputs = util_last_bit64(ls->outputs_written);

   if (sctx->tcs_shader.cso) {
      num_tcs_outputs = util_last_bit64(tcs->outputs_written);
      num_tcs_output_cp = tcs->info.base.tess.tcs_vertices_out;
      num_tcs_patch_outputs = util_last_bit64(tcs->patch_outputs_written);
   } else {
      /* No TCS. Route varyings from LS to TES. */
      num_tcs_outputs = num_tcs_inputs;
      num_tcs_output_cp = num_tcs_input_cp;
      num_tcs_patch_outputs = 2; /* TESSINNER + TESSOUTER */
   }

   input_vertex_size = ls->lshs_vertex_stride;
   output_vertex_size = num_tcs_outputs * 16;

   /* Allocate LDS for TCS inputs only if it's used. */
   if (!ls_current->key.opt.same_patch_vertices ||
       tcs->info.base.inputs_read & ~tcs->tcs_vgpr_only_inputs)
      input_patch_size = num_tcs_input_cp * input_vertex_size;
   else
      input_patch_size = 0;

   pervertex_output_patch_size = num_tcs_output_cp * output_vertex_size;
   output_patch_size = pervertex_output_patch_size + num_tcs_patch_outputs * 16;

   /* Compute the LDS size per patch.
    *
    * LDS is used to store TCS outputs if they are read, and to store tess
    * factors if they are not defined in all invocations.
    */
   if (tcs->info.base.outputs_read ||
       tcs->info.base.patch_outputs_read ||
       !tcs->info.tessfactors_are_def_in_all_invocs) {
      lds_per_patch = input_patch_size + output_patch_size;
   } else {
      /* LDS will only store TCS inputs. The offchip buffer will only store TCS outputs. */
      lds_per_patch = MAX2(input_patch_size, output_patch_size);
   }

   /* Ensure that we only need one wave per SIMD so we don't need to check
    * resource usage. Also ensures that the number of tcs in and out
    * vertices per threadgroup are at most 256.
    */
   unsigned max_verts_per_patch = MAX2(num_tcs_input_cp, num_tcs_output_cp);
   *num_patches = 256 / max_verts_per_patch;

   /* Make sure that the data fits in LDS. This assumes the shaders only
    * use LDS for the inputs and outputs.
    *
    * While GFX7 can use 64K per threadgroup, there is a hang on Stoney
    * with 2 CUs if we use more than 32K. The closed Vulkan driver also
    * uses 32K at most on all GCN chips.
    *
    * Use 16K so that we can fit 2 workgroups on the same CU.
    */
   ASSERTED unsigned max_lds_size = 32 * 1024; /* hw limit */
   target_lds_size = 16 * 1024; /* target at least 2 workgroups per CU, 16K each */
   *num_patches = MIN2(*num_patches, target_lds_size / lds_per_patch);
   *num_patches = MAX2(*num_patches, 1);
   assert(*num_patches * lds_per_patch <= max_lds_size);

   /* Make sure the output data fits in the offchip buffer */
   *num_patches =
      MIN2(*num_patches, (sctx->screen->tess_offchip_block_dw_size * 4) / output_patch_size);

   /* Not necessary for correctness, but improves performance.
    * The hardware can do more, but the radeonsi shader constant is
    * limited to 6 bits.
    */
   *num_patches = MIN2(*num_patches, 64); /* triangles: 3 full waves */

   /* When distributed tessellation is unsupported, switch between SEs
    * at a higher frequency to compensate for it.
    */
   if (!sctx->screen->info.has_distributed_tess && sctx->screen->info.max_se > 1)
      *num_patches = MIN2(*num_patches, 16); /* recommended */

   /* Make sure that vector lanes are reasonably occupied. It probably
    * doesn't matter much because this is LS-HS, and TES is likely to
    * occupy significantly more CUs.
    */
   unsigned temp_verts_per_tg = *num_patches * max_verts_per_patch;
   unsigned wave_size = sctx->screen->ge_wave_size;

   if (temp_verts_per_tg > wave_size &&
       (wave_size - temp_verts_per_tg % wave_size >= MAX2(max_verts_per_patch, 8)))
      *num_patches = (temp_verts_per_tg & ~(wave_size - 1)) / max_verts_per_patch;

   if (sctx->chip_class == GFX6) {
      /* GFX6 bug workaround, related to power management. Limit LS-HS
       * threadgroups to only one wave.
       */
      unsigned one_wave = wave_size / max_verts_per_patch;
      *num_patches = MIN2(*num_patches, one_wave);
   }

   /* The VGT HS block increments the patch ID unconditionally
    * within a single threadgroup. This results in incorrect
    * patch IDs when instanced draws are used.
    *
    * The intended solution is to restrict threadgroups to
    * a single instance by setting SWITCH_ON_EOI, which
    * should cause IA to split instances up. However, this
    * doesn't work correctly on GFX6 when there is no other
    * SE to switch to.
    */
   if (has_primid_instancing_bug && tess_uses_primid)
      *num_patches = 1;

   sctx->last_num_patches = *num_patches;

   output_patch0_offset = input_patch_size * *num_patches;
   perpatch_output_offset = output_patch0_offset + pervertex_output_patch_size;

   /* Compute userdata SGPRs. */
   assert(((input_vertex_size / 4) & ~0xff) == 0);
   assert(((output_vertex_size / 4) & ~0xff) == 0);
   assert(((input_patch_size / 4) & ~0x1fff) == 0);
   assert(((output_patch_size / 4) & ~0x1fff) == 0);
   assert(((output_patch0_offset / 16) & ~0xffff) == 0);
   assert(((perpatch_output_offset / 16) & ~0xffff) == 0);
   assert(num_tcs_input_cp <= 32);
   assert(num_tcs_output_cp <= 32);
   assert(*num_patches <= 64);
   assert(((pervertex_output_patch_size * *num_patches) & ~0x1fffff) == 0);

   uint64_t ring_va = (unlikely(sctx->ws->cs_is_secure(&sctx->gfx_cs)) ?
      si_resource(sctx->tess_rings_tmz) : si_resource(sctx->tess_rings))->gpu_address;
   assert((ring_va & u_bit_consecutive(0, 19)) == 0);

   tcs_in_layout = S_VS_STATE_LS_OUT_PATCH_SIZE(input_patch_size / 4) |
                   S_VS_STATE_LS_OUT_VERTEX_SIZE(input_vertex_size / 4);
   tcs_out_layout = (output_patch_size / 4) | (num_tcs_input_cp << 13) | ring_va;
   tcs_out_offsets = (output_patch0_offset / 16) | ((perpatch_output_offset / 16) << 16);
   offchip_layout =
      (*num_patches - 1) | ((num_tcs_output_cp - 1) << 6) |
      ((pervertex_output_patch_size * *num_patches) << 11);

   /* Compute the LDS size. */
   lds_size = lds_per_patch * *num_patches;

   if (sctx->chip_class >= GFX7) {
      assert(lds_size <= 65536);
      lds_size = align(lds_size, 512) / 512;
   } else {
      assert(lds_size <= 32768);
      lds_size = align(lds_size, 256) / 256;
   }

   /* Set SI_SGPR_VS_STATE_BITS. */
   sctx->current_vs_state &= C_VS_STATE_LS_OUT_PATCH_SIZE & C_VS_STATE_LS_OUT_VERTEX_SIZE;
   sctx->current_vs_state |= tcs_in_layout;

   /* We should be able to support in-shader LDS use with LLVM >= 9
    * by just adding the lds_sizes together, but it has never
    * been tested. */
   assert(ls_current->config.lds_size == 0);

   if (sctx->chip_class >= GFX9) {
      unsigned hs_rsrc2 = ls_current->config.rsrc2;

      if (sctx->chip_class >= GFX10)
         hs_rsrc2 |= S_00B42C_LDS_SIZE_GFX10(lds_size);
      else
         hs_rsrc2 |= S_00B42C_LDS_SIZE_GFX9(lds_size);

      radeon_set_sh_reg(cs, R_00B42C_SPI_SHADER_PGM_RSRC2_HS, hs_rsrc2);

      /* Set userdata SGPRs for merged LS-HS. */
      radeon_set_sh_reg_seq(
         cs, R_00B430_SPI_SHADER_USER_DATA_LS_0 + GFX9_SGPR_TCS_OFFCHIP_LAYOUT * 4, 3);
      radeon_emit(cs, offchip_layout);
      radeon_emit(cs, tcs_out_offsets);
      radeon_emit(cs, tcs_out_layout);
   } else {
      unsigned ls_rsrc2 = ls_current->config.rsrc2;

      si_multiwave_lds_size_workaround(sctx->screen, &lds_size);
      ls_rsrc2 |= S_00B52C_LDS_SIZE(lds_size);

      /* Due to a hw bug, RSRC2_LS must be written twice with another
       * LS register written in between. */
      if (sctx->chip_class == GFX7 && sctx->family != CHIP_HAWAII)
         radeon_set_sh_reg(cs, R_00B52C_SPI_SHADER_PGM_RSRC2_LS, ls_rsrc2);
      radeon_set_sh_reg_seq(cs, R_00B528_SPI_SHADER_PGM_RSRC1_LS, 2);
      radeon_emit(cs, ls_current->config.rsrc1);
      radeon_emit(cs, ls_rsrc2);

      /* Set userdata SGPRs for TCS. */
      radeon_set_sh_reg_seq(
         cs, R_00B430_SPI_SHADER_USER_DATA_HS_0 + GFX6_SGPR_TCS_OFFCHIP_LAYOUT * 4, 4);
      radeon_emit(cs, offchip_layout);
      radeon_emit(cs, tcs_out_offsets);
      radeon_emit(cs, tcs_out_layout);
      radeon_emit(cs, tcs_in_layout);
   }

   /* Set userdata SGPRs for TES. */
   radeon_set_sh_reg_seq(cs, tes_sh_base + SI_SGPR_TES_OFFCHIP_LAYOUT * 4, 2);
   radeon_emit(cs, offchip_layout);
   radeon_emit(cs, ring_va);

   ls_hs_config = S_028B58_NUM_PATCHES(*num_patches) | S_028B58_HS_NUM_INPUT_CP(num_tcs_input_cp) |
                  S_028B58_HS_NUM_OUTPUT_CP(num_tcs_output_cp);

   if (sctx->last_ls_hs_config != ls_hs_config) {
      if (sctx->chip_class >= GFX7) {
         radeon_set_context_reg_idx(cs, R_028B58_VGT_LS_HS_CONFIG, 2, ls_hs_config);
      } else {
         radeon_set_context_reg(cs, R_028B58_VGT_LS_HS_CONFIG, ls_hs_config);
      }
      sctx->last_ls_hs_config = ls_hs_config;
      sctx->context_roll = true;
   }
}

static unsigned si_num_prims_for_vertices(enum pipe_prim_type prim,
                                          unsigned count, unsigned vertices_per_patch)
{
   switch (prim) {
   case PIPE_PRIM_PATCHES:
      return count / vertices_per_patch;
   case PIPE_PRIM_POLYGON:
      return count >= 3;
   case SI_PRIM_RECTANGLE_LIST:
      return count / 3;
   default:
      return u_decomposed_prims_for_vertices(prim, count);
   }
}

static unsigned si_get_init_multi_vgt_param(struct si_screen *sscreen, union si_vgt_param_key *key)
{
   STATIC_ASSERT(sizeof(union si_vgt_param_key) == 4);
   unsigned max_primgroup_in_wave = 2;

   /* SWITCH_ON_EOP(0) is always preferable. */
   bool wd_switch_on_eop = false;
   bool ia_switch_on_eop = false;
   bool ia_switch_on_eoi = false;
   bool partial_vs_wave = false;
   bool partial_es_wave = false;

   if (key->u.uses_tess) {
      /* SWITCH_ON_EOI must be set if PrimID is used. */
      if (key->u.tess_uses_prim_id)
         ia_switch_on_eoi = true;

      /* Bug with tessellation and GS on Bonaire and older 2 SE chips. */
      if ((sscreen->info.family == CHIP_TAHITI || sscreen->info.family == CHIP_PITCAIRN ||
           sscreen->info.family == CHIP_BONAIRE) &&
          key->u.uses_gs)
         partial_vs_wave = true;

      /* Needed for 028B6C_DISTRIBUTION_MODE != 0. (implies >= GFX8) */
      if (sscreen->info.has_distributed_tess) {
         if (key->u.uses_gs) {
            if (sscreen->info.chip_class == GFX8)
               partial_es_wave = true;
         } else {
            partial_vs_wave = true;
         }
      }
   }

   /* This is a hardware requirement. */
   if (key->u.line_stipple_enabled || (sscreen->debug_flags & DBG(SWITCH_ON_EOP))) {
      ia_switch_on_eop = true;
      wd_switch_on_eop = true;
   }

   if (sscreen->info.chip_class >= GFX7) {
      /* WD_SWITCH_ON_EOP has no effect on GPUs with less than
       * 4 shader engines. Set 1 to pass the assertion below.
       * The other cases are hardware requirements.
       *
       * Polaris supports primitive restart with WD_SWITCH_ON_EOP=0
       * for points, line strips, and tri strips.
       */
      if (sscreen->info.max_se <= 2 || key->u.prim == PIPE_PRIM_POLYGON ||
          key->u.prim == PIPE_PRIM_LINE_LOOP || key->u.prim == PIPE_PRIM_TRIANGLE_FAN ||
          key->u.prim == PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY ||
          (key->u.primitive_restart &&
           (sscreen->info.family < CHIP_POLARIS10 ||
            (key->u.prim != PIPE_PRIM_POINTS && key->u.prim != PIPE_PRIM_LINE_STRIP &&
             key->u.prim != PIPE_PRIM_TRIANGLE_STRIP))) ||
          key->u.count_from_stream_output)
         wd_switch_on_eop = true;

      /* Hawaii hangs if instancing is enabled and WD_SWITCH_ON_EOP is 0.
       * We don't know that for indirect drawing, so treat it as
       * always problematic. */
      if (sscreen->info.family == CHIP_HAWAII && key->u.uses_instancing)
         wd_switch_on_eop = true;

      /* Performance recommendation for 4 SE Gfx7-8 parts if
       * instances are smaller than a primgroup.
       * Assume indirect draws always use small instances.
       * This is needed for good VS wave utilization.
       */
      if (sscreen->info.chip_class <= GFX8 && sscreen->info.max_se == 4 &&
          key->u.multi_instances_smaller_than_primgroup)
         wd_switch_on_eop = true;

      /* Required on GFX7 and later. */
      if (sscreen->info.max_se == 4 && !wd_switch_on_eop)
         ia_switch_on_eoi = true;

      /* HW engineers suggested that PARTIAL_VS_WAVE_ON should be set
       * to work around a GS hang.
       */
      if (key->u.uses_gs &&
          (sscreen->info.family == CHIP_TONGA || sscreen->info.family == CHIP_FIJI ||
           sscreen->info.family == CHIP_POLARIS10 || sscreen->info.family == CHIP_POLARIS11 ||
           sscreen->info.family == CHIP_POLARIS12 || sscreen->info.family == CHIP_VEGAM))
         partial_vs_wave = true;

      /* Required by Hawaii and, for some special cases, by GFX8. */
      if (ia_switch_on_eoi &&
          (sscreen->info.family == CHIP_HAWAII ||
           (sscreen->info.chip_class == GFX8 && (key->u.uses_gs || max_primgroup_in_wave != 2))))
         partial_vs_wave = true;

      /* Instancing bug on Bonaire. */
      if (sscreen->info.family == CHIP_BONAIRE && ia_switch_on_eoi && key->u.uses_instancing)
         partial_vs_wave = true;

      /* This only applies to Polaris10 and later 4 SE chips.
       * wd_switch_on_eop is already true on all other chips.
       */
      if (!wd_switch_on_eop && key->u.primitive_restart)
         partial_vs_wave = true;

      /* If the WD switch is false, the IA switch must be false too. */
      assert(wd_switch_on_eop || !ia_switch_on_eop);
   }

   /* If SWITCH_ON_EOI is set, PARTIAL_ES_WAVE must be set too. */
   if (sscreen->info.chip_class <= GFX8 && ia_switch_on_eoi)
      partial_es_wave = true;

   return S_028AA8_SWITCH_ON_EOP(ia_switch_on_eop) | S_028AA8_SWITCH_ON_EOI(ia_switch_on_eoi) |
          S_028AA8_PARTIAL_VS_WAVE_ON(partial_vs_wave) |
          S_028AA8_PARTIAL_ES_WAVE_ON(partial_es_wave) |
          S_028AA8_WD_SWITCH_ON_EOP(sscreen->info.chip_class >= GFX7 ? wd_switch_on_eop : 0) |
          /* The following field was moved to VGT_SHADER_STAGES_EN in GFX9. */
          S_028AA8_MAX_PRIMGRP_IN_WAVE(sscreen->info.chip_class == GFX8 ? max_primgroup_in_wave
                                                                        : 0) |
          S_030960_EN_INST_OPT_BASIC(sscreen->info.chip_class >= GFX9) |
          S_030960_EN_INST_OPT_ADV(sscreen->info.chip_class >= GFX9);
}

static void si_init_ia_multi_vgt_param_table(struct si_context *sctx)
{
   for (int prim = 0; prim <= SI_PRIM_RECTANGLE_LIST; prim++)
      for (int uses_instancing = 0; uses_instancing < 2; uses_instancing++)
         for (int multi_instances = 0; multi_instances < 2; multi_instances++)
            for (int primitive_restart = 0; primitive_restart < 2; primitive_restart++)
               for (int count_from_so = 0; count_from_so < 2; count_from_so++)
                  for (int line_stipple = 0; line_stipple < 2; line_stipple++)
                     for (int uses_tess = 0; uses_tess < 2; uses_tess++)
                        for (int tess_uses_primid = 0; tess_uses_primid < 2; tess_uses_primid++)
                           for (int uses_gs = 0; uses_gs < 2; uses_gs++) {
                              union si_vgt_param_key key;

                              key.index = 0;
                              key.u.prim = prim;
                              key.u.uses_instancing = uses_instancing;
                              key.u.multi_instances_smaller_than_primgroup = multi_instances;
                              key.u.primitive_restart = primitive_restart;
                              key.u.count_from_stream_output = count_from_so;
                              key.u.line_stipple_enabled = line_stipple;
                              key.u.uses_tess = uses_tess;
                              key.u.tess_uses_prim_id = tess_uses_primid;
                              key.u.uses_gs = uses_gs;

                              sctx->ia_multi_vgt_param[key.index] =
                                 si_get_init_multi_vgt_param(sctx->screen, &key);
                           }
}

static bool si_is_line_stipple_enabled(struct si_context *sctx)
{
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;

   return rs->line_stipple_enable && sctx->current_rast_prim != PIPE_PRIM_POINTS &&
          (rs->polygon_mode_is_lines || util_prim_is_lines(sctx->current_rast_prim));
}

static bool num_instanced_prims_less_than(const struct pipe_draw_info *info,
                                          const struct pipe_draw_indirect_info *indirect,
                                          enum pipe_prim_type prim,
                                          unsigned min_vertex_count,
                                          unsigned instance_count,
                                          unsigned num_prims)
{
   if (indirect) {
      return indirect->buffer ||
             (instance_count > 1 && indirect->count_from_stream_output);
   } else {
      return instance_count > 1 &&
             si_num_prims_for_vertices(prim, min_vertex_count, info->vertices_per_patch) < num_prims;
   }
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS> ALWAYS_INLINE
static unsigned si_get_ia_multi_vgt_param(struct si_context *sctx,
                                          const struct pipe_draw_info *info,
                                          const struct pipe_draw_indirect_info *indirect,
                                          enum pipe_prim_type prim, unsigned num_patches,
                                          unsigned instance_count, bool primitive_restart,
                                          unsigned min_vertex_count)
{
   union si_vgt_param_key key = sctx->ia_multi_vgt_param_key;
   unsigned primgroup_size;
   unsigned ia_multi_vgt_param;

   if (HAS_TESS) {
      primgroup_size = num_patches; /* must be a multiple of NUM_PATCHES */
   } else if (HAS_GS) {
      primgroup_size = 64; /* recommended with a GS */
   } else {
      primgroup_size = 128; /* recommended without a GS and tess */
   }

   key.u.prim = prim;
   key.u.uses_instancing = (indirect && indirect->buffer) || instance_count > 1;
   key.u.multi_instances_smaller_than_primgroup =
      num_instanced_prims_less_than(info, indirect, prim, min_vertex_count, instance_count, primgroup_size);
   key.u.primitive_restart = primitive_restart;
   key.u.count_from_stream_output = indirect && indirect->count_from_stream_output;
   key.u.line_stipple_enabled = si_is_line_stipple_enabled(sctx);

   ia_multi_vgt_param =
      sctx->ia_multi_vgt_param[key.index] | S_028AA8_PRIMGROUP_SIZE(primgroup_size - 1);

   if (HAS_GS) {
      /* GS requirement. */
      if (GFX_VERSION <= GFX8 &&
          SI_GS_PER_ES / primgroup_size >= sctx->screen->gs_table_depth - 3)
         ia_multi_vgt_param |= S_028AA8_PARTIAL_ES_WAVE_ON(1);

      /* GS hw bug with single-primitive instances and SWITCH_ON_EOI.
       * The hw doc says all multi-SE chips are affected, but Vulkan
       * only applies it to Hawaii. Do what Vulkan does.
       */
      if (GFX_VERSION == GFX7 &&
          sctx->family == CHIP_HAWAII && G_028AA8_SWITCH_ON_EOI(ia_multi_vgt_param) &&
          num_instanced_prims_less_than(info, indirect, prim, min_vertex_count, instance_count, 2))
         sctx->flags |= SI_CONTEXT_VGT_FLUSH;
   }

   return ia_multi_vgt_param;
}

ALWAYS_INLINE
static unsigned si_conv_prim_to_gs_out(unsigned mode)
{
   static const int prim_conv[] = {
      [PIPE_PRIM_POINTS] = V_028A6C_POINTLIST,
      [PIPE_PRIM_LINES] = V_028A6C_LINESTRIP,
      [PIPE_PRIM_LINE_LOOP] = V_028A6C_LINESTRIP,
      [PIPE_PRIM_LINE_STRIP] = V_028A6C_LINESTRIP,
      [PIPE_PRIM_TRIANGLES] = V_028A6C_TRISTRIP,
      [PIPE_PRIM_TRIANGLE_STRIP] = V_028A6C_TRISTRIP,
      [PIPE_PRIM_TRIANGLE_FAN] = V_028A6C_TRISTRIP,
      [PIPE_PRIM_QUADS] = V_028A6C_TRISTRIP,
      [PIPE_PRIM_QUAD_STRIP] = V_028A6C_TRISTRIP,
      [PIPE_PRIM_POLYGON] = V_028A6C_TRISTRIP,
      [PIPE_PRIM_LINES_ADJACENCY] = V_028A6C_LINESTRIP,
      [PIPE_PRIM_LINE_STRIP_ADJACENCY] = V_028A6C_LINESTRIP,
      [PIPE_PRIM_TRIANGLES_ADJACENCY] = V_028A6C_TRISTRIP,
      [PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY] = V_028A6C_TRISTRIP,
      [PIPE_PRIM_PATCHES] = V_028A6C_POINTLIST,
      [SI_PRIM_RECTANGLE_LIST] = V_028A6C_RECTLIST,
   };
   assert(mode < ARRAY_SIZE(prim_conv));

   return prim_conv[mode];
}

/* rast_prim is the primitive type after GS. */
template<si_has_gs HAS_GS, si_has_ngg NGG> ALWAYS_INLINE
static void si_emit_rasterizer_prim_state(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   enum pipe_prim_type rast_prim = sctx->current_rast_prim;
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
   unsigned initial_cdw = cs->current.cdw;

   if (unlikely(si_is_line_stipple_enabled(sctx))) {
      /* For lines, reset the stipple pattern at each primitive. Otherwise,
       * reset the stipple pattern at each packet (line strips, line loops).
       */
      bool reset_per_prim = rast_prim == PIPE_PRIM_LINES ||
                            rast_prim == PIPE_PRIM_LINES_ADJACENCY;
      /* 0 = no reset, 1 = reset per prim, 2 = reset per packet */
      unsigned value =
         rs->pa_sc_line_stipple | S_028A0C_AUTO_RESET_CNTL(reset_per_prim ? 1 : 2);

      radeon_opt_set_context_reg(sctx, R_028A0C_PA_SC_LINE_STIPPLE, SI_TRACKED_PA_SC_LINE_STIPPLE,
                                 value);
   }

   unsigned gs_out_prim = si_conv_prim_to_gs_out(rast_prim);
   if (unlikely(gs_out_prim != sctx->last_gs_out_prim && (NGG || HAS_GS))) {
      radeon_set_context_reg(cs, R_028A6C_VGT_GS_OUT_PRIM_TYPE, gs_out_prim);
      sctx->last_gs_out_prim = gs_out_prim;
   }

   if (initial_cdw != cs->current.cdw)
      sctx->context_roll = true;

   if (NGG) {
      struct si_shader *hw_vs = si_get_vs_state(sctx);

      if (hw_vs->uses_vs_state_provoking_vertex) {
         unsigned vtx_index = rs->flatshade_first ? 0 : gs_out_prim;

         sctx->current_vs_state &= C_VS_STATE_PROVOKING_VTX_INDEX;
         sctx->current_vs_state |= S_VS_STATE_PROVOKING_VTX_INDEX(vtx_index);
      }

      if (hw_vs->uses_vs_state_outprim) {
         sctx->current_vs_state &= C_VS_STATE_OUTPRIM;
         sctx->current_vs_state |= S_VS_STATE_OUTPRIM(gs_out_prim);
      }
   }
}

ALWAYS_INLINE
static void si_emit_vs_state(struct si_context *sctx, const struct pipe_draw_info *info)
{
   if (sctx->vs_shader.cso->info.uses_base_vertex) {
      sctx->current_vs_state &= C_VS_STATE_INDEXED;
      sctx->current_vs_state |= S_VS_STATE_INDEXED(!!info->index_size);
   }

   if (sctx->num_vs_blit_sgprs) {
      /* Re-emit the state after we leave u_blitter. */
      sctx->last_vs_state = ~0;
      return;
   }

   if (sctx->current_vs_state != sctx->last_vs_state) {
      struct radeon_cmdbuf *cs = &sctx->gfx_cs;

      /* For the API vertex shader (VS_STATE_INDEXED, LS_OUT_*). */
      radeon_set_sh_reg(
         cs, sctx->shader_pointers.sh_base[PIPE_SHADER_VERTEX] + SI_SGPR_VS_STATE_BITS * 4,
         sctx->current_vs_state);

      /* Set CLAMP_VERTEX_COLOR and OUTPRIM in the last stage
       * before the rasterizer.
       *
       * For TES or the GS copy shader without NGG:
       */
      if (sctx->shader_pointers.sh_base[PIPE_SHADER_VERTEX] != R_00B130_SPI_SHADER_USER_DATA_VS_0) {
         radeon_set_sh_reg(cs, R_00B130_SPI_SHADER_USER_DATA_VS_0 + SI_SGPR_VS_STATE_BITS * 4,
                           sctx->current_vs_state);
      }

      /* For NGG: */
      if (sctx->screen->use_ngg &&
          sctx->shader_pointers.sh_base[PIPE_SHADER_VERTEX] != R_00B230_SPI_SHADER_USER_DATA_GS_0) {
         radeon_set_sh_reg(cs, R_00B230_SPI_SHADER_USER_DATA_GS_0 + SI_SGPR_VS_STATE_BITS * 4,
                           sctx->current_vs_state);
      }

      sctx->last_vs_state = sctx->current_vs_state;
   }
}

ALWAYS_INLINE
static bool si_prim_restart_index_changed(struct si_context *sctx, bool primitive_restart,
                                          unsigned restart_index)
{
   return primitive_restart && (restart_index != sctx->last_restart_index ||
                                sctx->last_restart_index == SI_RESTART_INDEX_UNKNOWN);
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS> ALWAYS_INLINE
static void si_emit_ia_multi_vgt_param(struct si_context *sctx, const struct pipe_draw_info *info,
                                       const struct pipe_draw_indirect_info *indirect,
                                       enum pipe_prim_type prim, unsigned num_patches,
                                       unsigned instance_count, bool primitive_restart,
                                       unsigned min_vertex_count)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned ia_multi_vgt_param;

   ia_multi_vgt_param =
      si_get_ia_multi_vgt_param<GFX_VERSION, HAS_TESS, HAS_GS>
         (sctx, info, indirect, prim, num_patches, instance_count, primitive_restart,
          min_vertex_count);

   /* Draw state. */
   if (ia_multi_vgt_param != sctx->last_multi_vgt_param) {
      if (GFX_VERSION == GFX9)
         radeon_set_uconfig_reg_idx(cs, sctx->screen, R_030960_IA_MULTI_VGT_PARAM, 4,
                                    ia_multi_vgt_param);
      else if (GFX_VERSION >= GFX7)
         radeon_set_context_reg_idx(cs, R_028AA8_IA_MULTI_VGT_PARAM, 1, ia_multi_vgt_param);
      else
         radeon_set_context_reg(cs, R_028AA8_IA_MULTI_VGT_PARAM, ia_multi_vgt_param);

      sctx->last_multi_vgt_param = ia_multi_vgt_param;
   }
}

/* GFX10 removed IA_MULTI_VGT_PARAM in exchange for GE_CNTL.
 * We overload last_multi_vgt_param.
 */
template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG> ALWAYS_INLINE
static void gfx10_emit_ge_cntl(struct si_context *sctx, unsigned num_patches)
{
   union si_vgt_param_key key = sctx->ia_multi_vgt_param_key;
   unsigned ge_cntl;

   if (NGG) {
      if (HAS_TESS) {
         ge_cntl = S_03096C_PRIM_GRP_SIZE(num_patches) |
                   S_03096C_VERT_GRP_SIZE(0) |
                   S_03096C_BREAK_WAVE_AT_EOI(key.u.tess_uses_prim_id);
      } else {
         ge_cntl = si_get_vs_state(sctx)->ge_cntl;
      }
   } else {
      unsigned primgroup_size;
      unsigned vertgroup_size;

      if (HAS_TESS) {
         primgroup_size = num_patches; /* must be a multiple of NUM_PATCHES */
         vertgroup_size = 0;
      } else if (HAS_GS) {
         unsigned vgt_gs_onchip_cntl = sctx->gs_shader.current->ctx_reg.gs.vgt_gs_onchip_cntl;
         primgroup_size = G_028A44_GS_PRIMS_PER_SUBGRP(vgt_gs_onchip_cntl);
         vertgroup_size = G_028A44_ES_VERTS_PER_SUBGRP(vgt_gs_onchip_cntl);
      } else {
         primgroup_size = 128; /* recommended without a GS and tess */
         vertgroup_size = 0;
      }

      ge_cntl = S_03096C_PRIM_GRP_SIZE(primgroup_size) | S_03096C_VERT_GRP_SIZE(vertgroup_size) |
                S_03096C_BREAK_WAVE_AT_EOI(key.u.uses_tess && key.u.tess_uses_prim_id);
   }

   ge_cntl |= S_03096C_PACKET_TO_ONE_PA(si_is_line_stipple_enabled(sctx));

   if (ge_cntl != sctx->last_multi_vgt_param) {
      radeon_set_uconfig_reg(&sctx->gfx_cs, R_03096C_GE_CNTL, ge_cntl);
      sctx->last_multi_vgt_param = ge_cntl;
   }
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG> ALWAYS_INLINE
static void si_emit_draw_registers(struct si_context *sctx, const struct pipe_draw_info *info,
                                   const struct pipe_draw_indirect_info *indirect,
                                   enum pipe_prim_type prim, unsigned num_patches,
                                   unsigned instance_count, bool primitive_restart,
                                   unsigned min_vertex_count)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned vgt_prim = si_conv_pipe_prim(prim);

   if (GFX_VERSION >= GFX10)
      gfx10_emit_ge_cntl<GFX_VERSION, HAS_TESS, HAS_GS, NGG>(sctx, num_patches);
   else
      si_emit_ia_multi_vgt_param<GFX_VERSION, HAS_TESS, HAS_GS>
         (sctx, info, indirect, prim, num_patches, instance_count, primitive_restart,
          min_vertex_count);

   if (vgt_prim != sctx->last_prim) {
      if (GFX_VERSION >= GFX10)
         radeon_set_uconfig_reg(cs, R_030908_VGT_PRIMITIVE_TYPE, vgt_prim);
      else if (GFX_VERSION >= GFX7)
         radeon_set_uconfig_reg_idx(cs, sctx->screen, R_030908_VGT_PRIMITIVE_TYPE, 1, vgt_prim);
      else
         radeon_set_config_reg(cs, R_008958_VGT_PRIMITIVE_TYPE, vgt_prim);

      sctx->last_prim = vgt_prim;
   }

   /* Primitive restart. */
   if (primitive_restart != sctx->last_primitive_restart_en) {
      if (GFX_VERSION >= GFX9)
         radeon_set_uconfig_reg(cs, R_03092C_VGT_MULTI_PRIM_IB_RESET_EN, primitive_restart);
      else
         radeon_set_context_reg(cs, R_028A94_VGT_MULTI_PRIM_IB_RESET_EN, primitive_restart);

      sctx->last_primitive_restart_en = primitive_restart;
   }
   if (si_prim_restart_index_changed(sctx, primitive_restart, info->restart_index)) {
      radeon_set_context_reg(cs, R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX, info->restart_index);
      sctx->last_restart_index = info->restart_index;
      sctx->context_roll = true;
   }
}

template <chip_class GFX_VERSION, si_has_ngg NGG, si_has_prim_discard_cs ALLOW_PRIM_DISCARD_CS>
static void si_emit_draw_packets(struct si_context *sctx, const struct pipe_draw_info *info,
                                 const struct pipe_draw_indirect_info *indirect,
                                 const struct pipe_draw_start_count *draws,
                                 unsigned num_draws,
                                 struct pipe_resource *indexbuf, unsigned index_size,
                                 unsigned index_offset, unsigned instance_count,
                                 bool dispatch_prim_discard_cs, unsigned original_index_size)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned sh_base_reg = sctx->shader_pointers.sh_base[PIPE_SHADER_VERTEX];
   bool render_cond_bit = sctx->render_cond && !sctx->render_cond_force_off;
   uint32_t index_max_size = 0;
   uint32_t use_opaque = 0;
   uint64_t index_va = 0;

   if (indirect && indirect->count_from_stream_output) {
      struct si_streamout_target *t = (struct si_streamout_target *)indirect->count_from_stream_output;

      radeon_set_context_reg(cs, R_028B30_VGT_STRMOUT_DRAW_OPAQUE_VERTEX_STRIDE, t->stride_in_dw);
      si_cp_copy_data(sctx, &sctx->gfx_cs, COPY_DATA_REG, NULL,
                      R_028B2C_VGT_STRMOUT_DRAW_OPAQUE_BUFFER_FILLED_SIZE >> 2, COPY_DATA_SRC_MEM,
                      t->buf_filled_size, t->buf_filled_size_offset);
      use_opaque = S_0287F0_USE_OPAQUE(1);
      indirect = NULL;
   }

   /* draw packet */
   if (index_size) {
      /* Register shadowing doesn't shadow INDEX_TYPE. */
      if (index_size != sctx->last_index_size || sctx->shadowed_regs) {
         unsigned index_type;

         /* index type */
         switch (index_size) {
         case 1:
            index_type = V_028A7C_VGT_INDEX_8;
            break;
         case 2:
            index_type =
               V_028A7C_VGT_INDEX_16 |
               (SI_BIG_ENDIAN && GFX_VERSION <= GFX7 ? V_028A7C_VGT_DMA_SWAP_16_BIT : 0);
            break;
         case 4:
            index_type =
               V_028A7C_VGT_INDEX_32 |
               (SI_BIG_ENDIAN && GFX_VERSION <= GFX7 ? V_028A7C_VGT_DMA_SWAP_32_BIT : 0);
            break;
         default:
            assert(!"unreachable");
            return;
         }

         if (GFX_VERSION >= GFX9) {
            radeon_set_uconfig_reg_idx(cs, sctx->screen, R_03090C_VGT_INDEX_TYPE, 2, index_type);
         } else {
            radeon_emit(cs, PKT3(PKT3_INDEX_TYPE, 0, 0));
            radeon_emit(cs, index_type);
         }

         sctx->last_index_size = index_size;
      }

      /* If !ALLOW_PRIM_DISCARD_CS, index_size == original_index_size. */
      if (!ALLOW_PRIM_DISCARD_CS || original_index_size) {
         index_max_size = (indexbuf->width0 - index_offset) >> util_logbase2(original_index_size);
         /* Skip draw calls with 0-sized index buffers.
          * They cause a hang on some chips, like Navi10-14.
          */
         if (!index_max_size)
            return;

         index_va = si_resource(indexbuf)->gpu_address + index_offset;

         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, si_resource(indexbuf), RADEON_USAGE_READ,
                                   RADEON_PRIO_INDEX_BUFFER);
      }
   } else {
      /* On GFX7 and later, non-indexed draws overwrite VGT_INDEX_TYPE,
       * so the state must be re-emitted before the next indexed draw.
       */
      if (GFX_VERSION >= GFX7)
         sctx->last_index_size = -1;
   }

   if (indirect) {
      assert(num_draws == 1);
      uint64_t indirect_va = si_resource(indirect->buffer)->gpu_address;

      assert(indirect_va % 8 == 0);

      si_invalidate_draw_constants(sctx);

      radeon_emit(cs, PKT3(PKT3_SET_BASE, 2, 0));
      radeon_emit(cs, 1);
      radeon_emit(cs, indirect_va);
      radeon_emit(cs, indirect_va >> 32);

      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, si_resource(indirect->buffer),
                                RADEON_USAGE_READ, RADEON_PRIO_DRAW_INDIRECT);

      unsigned di_src_sel = index_size ? V_0287F0_DI_SRC_SEL_DMA : V_0287F0_DI_SRC_SEL_AUTO_INDEX;

      assert(indirect->offset % 4 == 0);

      if (index_size) {
         radeon_emit(cs, PKT3(PKT3_INDEX_BASE, 1, 0));
         radeon_emit(cs, index_va);
         radeon_emit(cs, index_va >> 32);

         radeon_emit(cs, PKT3(PKT3_INDEX_BUFFER_SIZE, 0, 0));
         radeon_emit(cs, index_max_size);
      }

      if (!sctx->screen->has_draw_indirect_multi) {
         radeon_emit(cs, PKT3(index_size ? PKT3_DRAW_INDEX_INDIRECT : PKT3_DRAW_INDIRECT, 3,
                              render_cond_bit));
         radeon_emit(cs, indirect->offset);
         radeon_emit(cs, (sh_base_reg + SI_SGPR_BASE_VERTEX * 4 - SI_SH_REG_OFFSET) >> 2);
         radeon_emit(cs, (sh_base_reg + SI_SGPR_START_INSTANCE * 4 - SI_SH_REG_OFFSET) >> 2);
         radeon_emit(cs, di_src_sel);
      } else {
         uint64_t count_va = 0;

         if (indirect->indirect_draw_count) {
            struct si_resource *params_buf = si_resource(indirect->indirect_draw_count);

            radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, params_buf, RADEON_USAGE_READ,
                                      RADEON_PRIO_DRAW_INDIRECT);

            count_va = params_buf->gpu_address + indirect->indirect_draw_count_offset;
         }

         radeon_emit(cs,
                     PKT3(index_size ? PKT3_DRAW_INDEX_INDIRECT_MULTI : PKT3_DRAW_INDIRECT_MULTI, 8,
                          render_cond_bit));
         radeon_emit(cs, indirect->offset);
         radeon_emit(cs, (sh_base_reg + SI_SGPR_BASE_VERTEX * 4 - SI_SH_REG_OFFSET) >> 2);
         radeon_emit(cs, (sh_base_reg + SI_SGPR_START_INSTANCE * 4 - SI_SH_REG_OFFSET) >> 2);
         radeon_emit(cs, ((sh_base_reg + SI_SGPR_DRAWID * 4 - SI_SH_REG_OFFSET) >> 2) |
                            S_2C3_DRAW_INDEX_ENABLE(sctx->vs_shader.cso->info.uses_drawid) |
                            S_2C3_COUNT_INDIRECT_ENABLE(!!indirect->indirect_draw_count));
         radeon_emit(cs, indirect->draw_count);
         radeon_emit(cs, count_va);
         radeon_emit(cs, count_va >> 32);
         radeon_emit(cs, indirect->stride);
         radeon_emit(cs, di_src_sel);
      }
   } else {
      int base_vertex;

      /* Register shadowing requires that we always emit PKT3_NUM_INSTANCES. */
      if (sctx->shadowed_regs ||
          sctx->last_instance_count == SI_INSTANCE_COUNT_UNKNOWN ||
          sctx->last_instance_count != instance_count) {
         radeon_emit(cs, PKT3(PKT3_NUM_INSTANCES, 0, 0));
         radeon_emit(cs, instance_count);
         sctx->last_instance_count = instance_count;
      }

      /* Base vertex and start instance. */
      base_vertex = original_index_size ? info->index_bias : draws[0].start;

      bool set_draw_id = sctx->vs_uses_draw_id;
      bool set_base_instance = sctx->vs_uses_base_instance;

      if (sctx->num_vs_blit_sgprs) {
         /* Re-emit draw constants after we leave u_blitter. */
         si_invalidate_draw_sh_constants(sctx);

         /* Blit VS doesn't use BASE_VERTEX, START_INSTANCE, and DRAWID. */
         radeon_set_sh_reg_seq(cs, sh_base_reg + SI_SGPR_VS_BLIT_DATA * 4, sctx->num_vs_blit_sgprs);
         radeon_emit_array(cs, sctx->vs_blit_sh_data, sctx->num_vs_blit_sgprs);
      } else if (base_vertex != sctx->last_base_vertex ||
                 sctx->last_base_vertex == SI_BASE_VERTEX_UNKNOWN ||
                 (set_base_instance &&
                  (info->start_instance != sctx->last_start_instance ||
                   sctx->last_start_instance == SI_START_INSTANCE_UNKNOWN)) ||
                 (set_draw_id &&
                  (info->drawid != sctx->last_drawid ||
                   sctx->last_drawid == SI_DRAW_ID_UNKNOWN)) ||
                 sh_base_reg != sctx->last_sh_base_reg) {
         if (set_base_instance) {
            radeon_set_sh_reg_seq(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, 3);
            radeon_emit(cs, base_vertex);
            radeon_emit(cs, info->drawid);
            radeon_emit(cs, info->start_instance);

            sctx->last_start_instance = info->start_instance;
            sctx->last_drawid = info->drawid;
         } else if (set_draw_id) {
            radeon_set_sh_reg_seq(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, 2);
            radeon_emit(cs, base_vertex);
            radeon_emit(cs, info->drawid);

            sctx->last_drawid = info->drawid;
         } else {
            radeon_set_sh_reg(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, base_vertex);
         }

         sctx->last_base_vertex = base_vertex;
         sctx->last_sh_base_reg = sh_base_reg;
      }

      /* Don't update draw_id in the following code if it doesn't increment. */
      set_draw_id &= info->increment_draw_id;

      if (index_size) {
         if (ALLOW_PRIM_DISCARD_CS && dispatch_prim_discard_cs) {
            for (unsigned i = 0; i < num_draws; i++) {
               uint64_t va = index_va + draws[0].start * original_index_size;

               si_dispatch_prim_discard_cs_and_draw(sctx, info, draws[i].count,
                                                    original_index_size, base_vertex,
                                                    va, MIN2(index_max_size, draws[i].count));
            }
            return;
         }

         for (unsigned i = 0; i < num_draws; i++) {
            uint64_t va = index_va + draws[i].start * index_size;

            if (i > 0 && set_draw_id) {
               unsigned draw_id = info->drawid + i;

               radeon_set_sh_reg(cs, sh_base_reg + SI_SGPR_DRAWID * 4, draw_id);
               sctx->last_drawid = draw_id;
            }

            radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_2, 4, render_cond_bit));
            radeon_emit(cs, index_max_size);
            radeon_emit(cs, va);
            radeon_emit(cs, va >> 32);
            radeon_emit(cs, draws[i].count);
            radeon_emit(cs, V_0287F0_DI_SRC_SEL_DMA |
                        /* NOT_EOP allows merging multiple draws into 1 wave, but only user VGPRs
                         * can be changed between draws and GS fast launch must be disabled.
                         * NOT_EOP doesn't work on gfx9 and older.
                         */
                        S_0287F0_NOT_EOP(GFX_VERSION >= GFX10 &&
                                         !set_draw_id &&
                                         i < num_draws - 1));
         }
      } else {
         /* Set the index buffer for fast launch. The VS prolog will load the indices. */
         if (NGG && sctx->ngg_culling & SI_NGG_CULL_GS_FAST_LAUNCH_INDEX_SIZE_PACKED(~0)) {
            index_max_size = (indexbuf->width0 - index_offset) >> util_logbase2(original_index_size);

            radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, si_resource(indexbuf),
                                      RADEON_USAGE_READ, RADEON_PRIO_INDEX_BUFFER);
            uint64_t base_index_va = si_resource(indexbuf)->gpu_address + index_offset;

            for (unsigned i = 0; i < num_draws; i++) {
               uint64_t index_va = base_index_va + draws[i].start * original_index_size;

               radeon_set_sh_reg_seq(cs, R_00B208_SPI_SHADER_USER_DATA_ADDR_LO_GS, 2);
               radeon_emit(cs, index_va);
               radeon_emit(cs, index_va >> 32);

               if (i > 0) {
                  if (set_draw_id) {
                     unsigned draw_id = info->drawid + i;

                     radeon_set_sh_reg(cs, sh_base_reg + SI_SGPR_DRAWID * 4, draw_id);
                     sctx->last_drawid = draw_id;
                  }
               }

               /* TODO: Do index buffer bounds checking? We don't do it in this case. */
               radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_AUTO, 1, render_cond_bit));
               radeon_emit(cs, draws[i].count);
               radeon_emit(cs, V_0287F0_DI_SRC_SEL_AUTO_INDEX);
            }
            return;
         }

         for (unsigned i = 0; i < num_draws; i++) {
            if (i > 0) {
               if (set_draw_id) {
                  unsigned draw_id = info->drawid + i;

                  radeon_set_sh_reg_seq(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, 2);
                  radeon_emit(cs, draws[i].start);
                  radeon_emit(cs, draw_id);

                  sctx->last_drawid = draw_id;
               } else {
                  radeon_set_sh_reg(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, draws[i].start);
               }
            }

            radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_AUTO, 1, render_cond_bit));
            radeon_emit(cs, draws[i].count);
            radeon_emit(cs, V_0287F0_DI_SRC_SEL_AUTO_INDEX | use_opaque);
         }
         if (num_draws > 1 && !sctx->num_vs_blit_sgprs)
            sctx->last_base_vertex = draws[num_draws - 1].start;
      }
   }
}

extern "C"
void si_emit_surface_sync(struct si_context *sctx, struct radeon_cmdbuf *cs, unsigned cp_coher_cntl)
{
   bool compute_ib = !sctx->has_graphics || cs == &sctx->prim_discard_compute_cs;

   assert(sctx->chip_class <= GFX9);

   if (sctx->chip_class == GFX9 || compute_ib) {
      /* Flush caches and wait for the caches to assert idle. */
      radeon_emit(cs, PKT3(PKT3_ACQUIRE_MEM, 5, 0));
      radeon_emit(cs, cp_coher_cntl); /* CP_COHER_CNTL */
      radeon_emit(cs, 0xffffffff);    /* CP_COHER_SIZE */
      radeon_emit(cs, 0xffffff);      /* CP_COHER_SIZE_HI */
      radeon_emit(cs, 0);             /* CP_COHER_BASE */
      radeon_emit(cs, 0);             /* CP_COHER_BASE_HI */
      radeon_emit(cs, 0x0000000A);    /* POLL_INTERVAL */
   } else {
      /* ACQUIRE_MEM is only required on a compute ring. */
      radeon_emit(cs, PKT3(PKT3_SURFACE_SYNC, 3, 0));
      radeon_emit(cs, cp_coher_cntl); /* CP_COHER_CNTL */
      radeon_emit(cs, 0xffffffff);    /* CP_COHER_SIZE */
      radeon_emit(cs, 0);             /* CP_COHER_BASE */
      radeon_emit(cs, 0x0000000A);    /* POLL_INTERVAL */
   }

   /* ACQUIRE_MEM has an implicit context roll if the current context
    * is busy. */
   if (!compute_ib)
      sctx->context_roll = true;
}

extern "C"
void si_prim_discard_signal_next_compute_ib_start(struct si_context *sctx)
{
   if (!si_compute_prim_discard_enabled(sctx))
      return;

   if (!sctx->barrier_buf) {
      u_suballocator_alloc(&sctx->allocator_zeroed_memory, 4, 4, &sctx->barrier_buf_offset,
                           (struct pipe_resource **)&sctx->barrier_buf);
   }

   /* Emit a placeholder to signal the next compute IB to start.
    * See si_compute_prim_discard.c for explanation.
    */
   uint32_t signal = 1;
   si_cp_write_data(sctx, sctx->barrier_buf, sctx->barrier_buf_offset, 4, V_370_MEM, V_370_ME,
                    &signal);

   sctx->last_pkt3_write_data = &sctx->gfx_cs.current.buf[sctx->gfx_cs.current.cdw - 5];

   /* Only the last occurence of WRITE_DATA will be executed.
    * The packet will be enabled in si_flush_gfx_cs.
    */
   *sctx->last_pkt3_write_data = PKT3(PKT3_NOP, 3, 0);
}

extern "C"
void gfx10_emit_cache_flush(struct si_context *ctx)
{
   struct radeon_cmdbuf *cs = &ctx->gfx_cs;
   uint32_t gcr_cntl = 0;
   unsigned cb_db_event = 0;
   unsigned flags = ctx->flags;

   if (!ctx->has_graphics) {
      /* Only process compute flags. */
      flags &= SI_CONTEXT_INV_ICACHE | SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE |
               SI_CONTEXT_INV_L2 | SI_CONTEXT_WB_L2 | SI_CONTEXT_INV_L2_METADATA |
               SI_CONTEXT_CS_PARTIAL_FLUSH;
   }

   /* We don't need these. */
   assert(!(flags & (SI_CONTEXT_VGT_STREAMOUT_SYNC | SI_CONTEXT_FLUSH_AND_INV_DB_META)));

   if (flags & SI_CONTEXT_VGT_FLUSH) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));
   }

   if (flags & SI_CONTEXT_FLUSH_AND_INV_CB)
      ctx->num_cb_cache_flushes++;
   if (flags & SI_CONTEXT_FLUSH_AND_INV_DB)
      ctx->num_db_cache_flushes++;

   if (flags & SI_CONTEXT_INV_ICACHE)
      gcr_cntl |= S_586_GLI_INV(V_586_GLI_ALL);
   if (flags & SI_CONTEXT_INV_SCACHE) {
      /* TODO: When writing to the SMEM L1 cache, we need to set SEQ
       * to FORWARD when both L1 and L2 are written out (WB or INV).
       */
      gcr_cntl |= S_586_GL1_INV(1) | S_586_GLK_INV(1);
   }
   if (flags & SI_CONTEXT_INV_VCACHE)
      gcr_cntl |= S_586_GL1_INV(1) | S_586_GLV_INV(1);

   /* The L2 cache ops are:
    * - INV: - invalidate lines that reflect memory (were loaded from memory)
    *        - don't touch lines that were overwritten (were stored by gfx clients)
    * - WB: - don't touch lines that reflect memory
    *       - write back lines that were overwritten
    * - WB | INV: - invalidate lines that reflect memory
    *             - write back lines that were overwritten
    *
    * GLM doesn't support WB alone. If WB is set, INV must be set too.
    */
   if (flags & SI_CONTEXT_INV_L2) {
      /* Writeback and invalidate everything in L2. */
      gcr_cntl |= S_586_GL2_INV(1) | S_586_GL2_WB(1) | S_586_GLM_INV(1) | S_586_GLM_WB(1);
      ctx->num_L2_invalidates++;
   } else if (flags & SI_CONTEXT_WB_L2) {
      gcr_cntl |= S_586_GL2_WB(1) | S_586_GLM_WB(1) | S_586_GLM_INV(1);
   } else if (flags & SI_CONTEXT_INV_L2_METADATA) {
      gcr_cntl |= S_586_GLM_INV(1) | S_586_GLM_WB(1);
   }

   if (flags & (SI_CONTEXT_FLUSH_AND_INV_CB | SI_CONTEXT_FLUSH_AND_INV_DB)) {
      if (flags & SI_CONTEXT_FLUSH_AND_INV_CB) {
         /* Flush CMASK/FMASK/DCC. Will wait for idle later. */
         radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
         radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_CB_META) | EVENT_INDEX(0));
      }
      if (flags & SI_CONTEXT_FLUSH_AND_INV_DB) {
         /* Flush HTILE. Will wait for idle later. */
         radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
         radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_DB_META) | EVENT_INDEX(0));
      }

      /* First flush CB/DB, then L1/L2. */
      gcr_cntl |= S_586_SEQ(V_586_SEQ_FORWARD);

      if ((flags & (SI_CONTEXT_FLUSH_AND_INV_CB | SI_CONTEXT_FLUSH_AND_INV_DB)) ==
          (SI_CONTEXT_FLUSH_AND_INV_CB | SI_CONTEXT_FLUSH_AND_INV_DB)) {
         cb_db_event = V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT;
      } else if (flags & SI_CONTEXT_FLUSH_AND_INV_CB) {
         cb_db_event = V_028A90_FLUSH_AND_INV_CB_DATA_TS;
      } else if (flags & SI_CONTEXT_FLUSH_AND_INV_DB) {
         cb_db_event = V_028A90_FLUSH_AND_INV_DB_DATA_TS;
      } else {
         assert(0);
      }
   } else {
      /* Wait for graphics shaders to go idle if requested. */
      if (flags & SI_CONTEXT_PS_PARTIAL_FLUSH) {
         radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
         radeon_emit(cs, EVENT_TYPE(V_028A90_PS_PARTIAL_FLUSH) | EVENT_INDEX(4));
         /* Only count explicit shader flushes, not implicit ones. */
         ctx->num_vs_flushes++;
         ctx->num_ps_flushes++;
      } else if (flags & SI_CONTEXT_VS_PARTIAL_FLUSH) {
         radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
         radeon_emit(cs, EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));
         ctx->num_vs_flushes++;
      }
   }

   if (flags & SI_CONTEXT_CS_PARTIAL_FLUSH && ctx->compute_is_busy) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_CS_PARTIAL_FLUSH | EVENT_INDEX(4)));
      ctx->num_cs_flushes++;
      ctx->compute_is_busy = false;
   }

   if (cb_db_event) {
      struct si_resource* wait_mem_scratch = unlikely(ctx->ws->cs_is_secure(cs)) ?
        ctx->wait_mem_scratch_tmz : ctx->wait_mem_scratch;
      /* CB/DB flush and invalidate (or possibly just a wait for a
       * meta flush) via RELEASE_MEM.
       *
       * Combine this with other cache flushes when possible; this
       * requires affected shaders to be idle, so do it after the
       * CS_PARTIAL_FLUSH before (VS/PS partial flushes are always
       * implied).
       */
      uint64_t va;

      /* Do the flush (enqueue the event and wait for it). */
      va = wait_mem_scratch->gpu_address;
      ctx->wait_mem_number++;

      /* Get GCR_CNTL fields, because the encoding is different in RELEASE_MEM. */
      unsigned glm_wb = G_586_GLM_WB(gcr_cntl);
      unsigned glm_inv = G_586_GLM_INV(gcr_cntl);
      unsigned glv_inv = G_586_GLV_INV(gcr_cntl);
      unsigned gl1_inv = G_586_GL1_INV(gcr_cntl);
      assert(G_586_GL2_US(gcr_cntl) == 0);
      assert(G_586_GL2_RANGE(gcr_cntl) == 0);
      assert(G_586_GL2_DISCARD(gcr_cntl) == 0);
      unsigned gl2_inv = G_586_GL2_INV(gcr_cntl);
      unsigned gl2_wb = G_586_GL2_WB(gcr_cntl);
      unsigned gcr_seq = G_586_SEQ(gcr_cntl);

      gcr_cntl &= C_586_GLM_WB & C_586_GLM_INV & C_586_GLV_INV & C_586_GL1_INV & C_586_GL2_INV &
                  C_586_GL2_WB; /* keep SEQ */

      si_cp_release_mem(ctx, cs, cb_db_event,
                        S_490_GLM_WB(glm_wb) | S_490_GLM_INV(glm_inv) | S_490_GLV_INV(glv_inv) |
                           S_490_GL1_INV(gl1_inv) | S_490_GL2_INV(gl2_inv) | S_490_GL2_WB(gl2_wb) |
                           S_490_SEQ(gcr_seq),
                        EOP_DST_SEL_MEM, EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM,
                        EOP_DATA_SEL_VALUE_32BIT, wait_mem_scratch, va, ctx->wait_mem_number,
                        SI_NOT_QUERY);
      si_cp_wait_mem(ctx, &ctx->gfx_cs, va, ctx->wait_mem_number, 0xffffffff, WAIT_REG_MEM_EQUAL);
   }

   /* Ignore fields that only modify the behavior of other fields. */
   if (gcr_cntl & C_586_GL1_RANGE & C_586_GL2_RANGE & C_586_SEQ) {
      /* Flush caches and wait for the caches to assert idle.
       * The cache flush is executed in the ME, but the PFP waits
       * for completion.
       */
      radeon_emit(cs, PKT3(PKT3_ACQUIRE_MEM, 6, 0));
      radeon_emit(cs, 0);          /* CP_COHER_CNTL */
      radeon_emit(cs, 0xffffffff); /* CP_COHER_SIZE */
      radeon_emit(cs, 0xffffff);   /* CP_COHER_SIZE_HI */
      radeon_emit(cs, 0);          /* CP_COHER_BASE */
      radeon_emit(cs, 0);          /* CP_COHER_BASE_HI */
      radeon_emit(cs, 0x0000000A); /* POLL_INTERVAL */
      radeon_emit(cs, gcr_cntl);   /* GCR_CNTL */
   } else if (cb_db_event || (flags & (SI_CONTEXT_VS_PARTIAL_FLUSH | SI_CONTEXT_PS_PARTIAL_FLUSH |
                                       SI_CONTEXT_CS_PARTIAL_FLUSH))) {
      /* We need to ensure that PFP waits as well. */
      radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
      radeon_emit(cs, 0);
   }

   if (flags & SI_CONTEXT_START_PIPELINE_STATS) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_PIPELINESTAT_START) | EVENT_INDEX(0));
   } else if (flags & SI_CONTEXT_STOP_PIPELINE_STATS) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_PIPELINESTAT_STOP) | EVENT_INDEX(0));
   }

   ctx->flags = 0;
}

extern "C"
void si_emit_cache_flush(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   uint32_t flags = sctx->flags;

   if (!sctx->has_graphics) {
      /* Only process compute flags. */
      flags &= SI_CONTEXT_INV_ICACHE | SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE |
               SI_CONTEXT_INV_L2 | SI_CONTEXT_WB_L2 | SI_CONTEXT_INV_L2_METADATA |
               SI_CONTEXT_CS_PARTIAL_FLUSH;
   }

   uint32_t cp_coher_cntl = 0;
   const uint32_t flush_cb_db = flags & (SI_CONTEXT_FLUSH_AND_INV_CB | SI_CONTEXT_FLUSH_AND_INV_DB);
   const bool is_barrier =
      flush_cb_db ||
      /* INV_ICACHE == beginning of gfx IB. Checking
       * INV_ICACHE fixes corruption for DeusExMD with
       * compute-based culling, but I don't know why.
       */
      flags & (SI_CONTEXT_INV_ICACHE | SI_CONTEXT_PS_PARTIAL_FLUSH | SI_CONTEXT_VS_PARTIAL_FLUSH) ||
      (flags & SI_CONTEXT_CS_PARTIAL_FLUSH && sctx->compute_is_busy);

   assert(sctx->chip_class <= GFX9);

   if (flags & SI_CONTEXT_FLUSH_AND_INV_CB)
      sctx->num_cb_cache_flushes++;
   if (flags & SI_CONTEXT_FLUSH_AND_INV_DB)
      sctx->num_db_cache_flushes++;

   /* GFX6 has a bug that it always flushes ICACHE and KCACHE if either
    * bit is set. An alternative way is to write SQC_CACHES, but that
    * doesn't seem to work reliably. Since the bug doesn't affect
    * correctness (it only does more work than necessary) and
    * the performance impact is likely negligible, there is no plan
    * to add a workaround for it.
    */

   if (flags & SI_CONTEXT_INV_ICACHE)
      cp_coher_cntl |= S_0085F0_SH_ICACHE_ACTION_ENA(1);
   if (flags & SI_CONTEXT_INV_SCACHE)
      cp_coher_cntl |= S_0085F0_SH_KCACHE_ACTION_ENA(1);

   if (sctx->chip_class <= GFX8) {
      if (flags & SI_CONTEXT_FLUSH_AND_INV_CB) {
         cp_coher_cntl |= S_0085F0_CB_ACTION_ENA(1) | S_0085F0_CB0_DEST_BASE_ENA(1) |
                          S_0085F0_CB1_DEST_BASE_ENA(1) | S_0085F0_CB2_DEST_BASE_ENA(1) |
                          S_0085F0_CB3_DEST_BASE_ENA(1) | S_0085F0_CB4_DEST_BASE_ENA(1) |
                          S_0085F0_CB5_DEST_BASE_ENA(1) | S_0085F0_CB6_DEST_BASE_ENA(1) |
                          S_0085F0_CB7_DEST_BASE_ENA(1);

         /* Necessary for DCC */
         if (sctx->chip_class == GFX8)
            si_cp_release_mem(sctx, cs, V_028A90_FLUSH_AND_INV_CB_DATA_TS, 0, EOP_DST_SEL_MEM,
                              EOP_INT_SEL_NONE, EOP_DATA_SEL_DISCARD, NULL, 0, 0, SI_NOT_QUERY);
      }
      if (flags & SI_CONTEXT_FLUSH_AND_INV_DB)
         cp_coher_cntl |= S_0085F0_DB_ACTION_ENA(1) | S_0085F0_DB_DEST_BASE_ENA(1);
   }

   if (flags & SI_CONTEXT_FLUSH_AND_INV_CB) {
      /* Flush CMASK/FMASK/DCC. SURFACE_SYNC will wait for idle. */
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_CB_META) | EVENT_INDEX(0));
   }
   if (flags & (SI_CONTEXT_FLUSH_AND_INV_DB | SI_CONTEXT_FLUSH_AND_INV_DB_META)) {
      /* Flush HTILE. SURFACE_SYNC will wait for idle. */
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_FLUSH_AND_INV_DB_META) | EVENT_INDEX(0));
   }

   /* Wait for shader engines to go idle.
    * VS and PS waits are unnecessary if SURFACE_SYNC is going to wait
    * for everything including CB/DB cache flushes.
    */
   if (!flush_cb_db) {
      if (flags & SI_CONTEXT_PS_PARTIAL_FLUSH) {
         radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
         radeon_emit(cs, EVENT_TYPE(V_028A90_PS_PARTIAL_FLUSH) | EVENT_INDEX(4));
         /* Only count explicit shader flushes, not implicit ones
          * done by SURFACE_SYNC.
          */
         sctx->num_vs_flushes++;
         sctx->num_ps_flushes++;
      } else if (flags & SI_CONTEXT_VS_PARTIAL_FLUSH) {
         radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
         radeon_emit(cs, EVENT_TYPE(V_028A90_VS_PARTIAL_FLUSH) | EVENT_INDEX(4));
         sctx->num_vs_flushes++;
      }
   }

   if (flags & SI_CONTEXT_CS_PARTIAL_FLUSH && sctx->compute_is_busy) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_CS_PARTIAL_FLUSH) | EVENT_INDEX(4));
      sctx->num_cs_flushes++;
      sctx->compute_is_busy = false;
   }

   /* VGT state synchronization. */
   if (flags & SI_CONTEXT_VGT_FLUSH) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_VGT_FLUSH) | EVENT_INDEX(0));
   }
   if (flags & SI_CONTEXT_VGT_STREAMOUT_SYNC) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_VGT_STREAMOUT_SYNC) | EVENT_INDEX(0));
   }

   /* GFX9: Wait for idle if we're flushing CB or DB. ACQUIRE_MEM doesn't
    * wait for idle on GFX9. We have to use a TS event.
    */
   if (sctx->chip_class == GFX9 && flush_cb_db) {
      uint64_t va;
      unsigned tc_flags, cb_db_event;

      /* Set the CB/DB flush event. */
      switch (flush_cb_db) {
      case SI_CONTEXT_FLUSH_AND_INV_CB:
         cb_db_event = V_028A90_FLUSH_AND_INV_CB_DATA_TS;
         break;
      case SI_CONTEXT_FLUSH_AND_INV_DB:
         cb_db_event = V_028A90_FLUSH_AND_INV_DB_DATA_TS;
         break;
      default:
         /* both CB & DB */
         cb_db_event = V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT;
      }

      /* These are the only allowed combinations. If you need to
       * do multiple operations at once, do them separately.
       * All operations that invalidate L2 also seem to invalidate
       * metadata. Volatile (VOL) and WC flushes are not listed here.
       *
       * TC    | TC_WB         = writeback & invalidate L2 & L1
       * TC    | TC_WB | TC_NC = writeback & invalidate L2 for MTYPE == NC
       *         TC_WB | TC_NC = writeback L2 for MTYPE == NC
       * TC            | TC_NC = invalidate L2 for MTYPE == NC
       * TC    | TC_MD         = writeback & invalidate L2 metadata (DCC, etc.)
       * TCL1                  = invalidate L1
       */
      tc_flags = 0;

      if (flags & SI_CONTEXT_INV_L2_METADATA) {
         tc_flags = EVENT_TC_ACTION_ENA | EVENT_TC_MD_ACTION_ENA;
      }

      /* Ideally flush TC together with CB/DB. */
      if (flags & SI_CONTEXT_INV_L2) {
         /* Writeback and invalidate everything in L2 & L1. */
         tc_flags = EVENT_TC_ACTION_ENA | EVENT_TC_WB_ACTION_ENA;

         /* Clear the flags. */
         flags &= ~(SI_CONTEXT_INV_L2 | SI_CONTEXT_WB_L2 | SI_CONTEXT_INV_VCACHE);
         sctx->num_L2_invalidates++;
      }

      /* Do the flush (enqueue the event and wait for it). */
      struct si_resource* wait_mem_scratch = unlikely(sctx->ws->cs_is_secure(cs)) ?
        sctx->wait_mem_scratch_tmz : sctx->wait_mem_scratch;
      va = wait_mem_scratch->gpu_address;
      sctx->wait_mem_number++;

      si_cp_release_mem(sctx, cs, cb_db_event, tc_flags, EOP_DST_SEL_MEM,
                        EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM, EOP_DATA_SEL_VALUE_32BIT,
                        wait_mem_scratch, va, sctx->wait_mem_number, SI_NOT_QUERY);
      si_cp_wait_mem(sctx, cs, va, sctx->wait_mem_number, 0xffffffff, WAIT_REG_MEM_EQUAL);
   }

   /* Make sure ME is idle (it executes most packets) before continuing.
    * This prevents read-after-write hazards between PFP and ME.
    */
   if (sctx->has_graphics &&
       (cp_coher_cntl || (flags & (SI_CONTEXT_CS_PARTIAL_FLUSH | SI_CONTEXT_INV_VCACHE |
                                   SI_CONTEXT_INV_L2 | SI_CONTEXT_WB_L2)))) {
      radeon_emit(cs, PKT3(PKT3_PFP_SYNC_ME, 0, 0));
      radeon_emit(cs, 0);
   }

   /* GFX6-GFX8 only:
    *   When one of the CP_COHER_CNTL.DEST_BASE flags is set, SURFACE_SYNC
    *   waits for idle, so it should be last. SURFACE_SYNC is done in PFP.
    *
    * cp_coher_cntl should contain all necessary flags except TC flags
    * at this point.
    *
    * GFX6-GFX7 don't support L2 write-back.
    */
   if (flags & SI_CONTEXT_INV_L2 || (sctx->chip_class <= GFX7 && (flags & SI_CONTEXT_WB_L2))) {
      /* Invalidate L1 & L2. (L1 is always invalidated on GFX6)
       * WB must be set on GFX8+ when TC_ACTION is set.
       */
      si_emit_surface_sync(sctx, &sctx->gfx_cs,
                           cp_coher_cntl | S_0085F0_TC_ACTION_ENA(1) | S_0085F0_TCL1_ACTION_ENA(1) |
                              S_0301F0_TC_WB_ACTION_ENA(sctx->chip_class >= GFX8));
      cp_coher_cntl = 0;
      sctx->num_L2_invalidates++;
   } else {
      /* L1 invalidation and L2 writeback must be done separately,
       * because both operations can't be done together.
       */
      if (flags & SI_CONTEXT_WB_L2) {
         /* WB = write-back
          * NC = apply to non-coherent MTYPEs
          *      (i.e. MTYPE <= 1, which is what we use everywhere)
          *
          * WB doesn't work without NC.
          */
         si_emit_surface_sync(
            sctx, &sctx->gfx_cs,
            cp_coher_cntl | S_0301F0_TC_WB_ACTION_ENA(1) | S_0301F0_TC_NC_ACTION_ENA(1));
         cp_coher_cntl = 0;
         sctx->num_L2_writebacks++;
      }
      if (flags & SI_CONTEXT_INV_VCACHE) {
         /* Invalidate per-CU VMEM L1. */
         si_emit_surface_sync(sctx, &sctx->gfx_cs, cp_coher_cntl | S_0085F0_TCL1_ACTION_ENA(1));
         cp_coher_cntl = 0;
      }
   }

   /* If TC flushes haven't cleared this... */
   if (cp_coher_cntl)
      si_emit_surface_sync(sctx, &sctx->gfx_cs, cp_coher_cntl);

   if (is_barrier)
      si_prim_discard_signal_next_compute_ib_start(sctx);

   if (flags & SI_CONTEXT_START_PIPELINE_STATS) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_PIPELINESTAT_START) | EVENT_INDEX(0));
   } else if (flags & SI_CONTEXT_STOP_PIPELINE_STATS) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_PIPELINESTAT_STOP) | EVENT_INDEX(0));
   }

   sctx->flags = 0;
}

template <chip_class GFX_VERSION> ALWAYS_INLINE
static bool si_upload_vertex_buffer_descriptors(struct si_context *sctx)
{
   unsigned i, count = sctx->num_vertex_elements;
   uint32_t *ptr;

   struct si_vertex_elements *velems = sctx->vertex_elements;
   unsigned alloc_size = velems->vb_desc_list_alloc_size;

   if (alloc_size) {
      /* Vertex buffer descriptors are the only ones which are uploaded
       * directly through a staging buffer and don't go through
       * the fine-grained upload path.
       */
      u_upload_alloc(sctx->b.const_uploader, 0, alloc_size,
                     si_optimal_tcc_alignment(sctx, alloc_size), &sctx->vb_descriptors_offset,
                     (struct pipe_resource **)&sctx->vb_descriptors_buffer, (void **)&ptr);
      if (!sctx->vb_descriptors_buffer) {
         sctx->vb_descriptors_offset = 0;
         sctx->vb_descriptors_gpu_list = NULL;
         return false;
      }

      sctx->vb_descriptors_gpu_list = ptr;
      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->vb_descriptors_buffer, RADEON_USAGE_READ,
                                RADEON_PRIO_DESCRIPTORS);
      sctx->vertex_buffer_pointer_dirty = true;
      sctx->prefetch_L2_mask |= SI_PREFETCH_VBO_DESCRIPTORS;
   } else {
      si_resource_reference(&sctx->vb_descriptors_buffer, NULL);
      sctx->vertex_buffer_pointer_dirty = false;
      sctx->prefetch_L2_mask &= ~SI_PREFETCH_VBO_DESCRIPTORS;
   }

   assert(count <= SI_MAX_ATTRIBS);

   unsigned first_vb_use_mask = velems->first_vb_use_mask;
   unsigned num_vbos_in_user_sgprs = sctx->screen->num_vbos_in_user_sgprs;

   for (i = 0; i < count; i++) {
      struct pipe_vertex_buffer *vb;
      struct si_resource *buf;
      unsigned vbo_index = velems->vertex_buffer_index[i];
      uint32_t *desc = i < num_vbos_in_user_sgprs ? &sctx->vb_descriptor_user_sgprs[i * 4]
                                                  : &ptr[(i - num_vbos_in_user_sgprs) * 4];

      vb = &sctx->vertex_buffer[vbo_index];
      buf = si_resource(vb->buffer.resource);
      if (!buf) {
         memset(desc, 0, 16);
         continue;
      }

      int64_t offset = (int64_t)((int)vb->buffer_offset) + velems->src_offset[i];

      if (offset >= buf->b.b.width0) {
         assert(offset < buf->b.b.width0);
         memset(desc, 0, 16);
         continue;
      }

      uint64_t va = buf->gpu_address + offset;

      int64_t num_records = (int64_t)buf->b.b.width0 - offset;
      if (GFX_VERSION != GFX8 && vb->stride) {
         /* Round up by rounding down and adding 1 */
         num_records = (num_records - velems->format_size[i]) / vb->stride + 1;
      }
      assert(num_records >= 0 && num_records <= UINT_MAX);

      uint32_t rsrc_word3 = velems->rsrc_word3[i];

      /* OOB_SELECT chooses the out-of-bounds check:
       *  - 1: index >= NUM_RECORDS (Structured)
       *  - 3: offset >= NUM_RECORDS (Raw)
       */
      if (GFX_VERSION >= GFX10)
         rsrc_word3 |= S_008F0C_OOB_SELECT(vb->stride ? V_008F0C_OOB_SELECT_STRUCTURED
                                                      : V_008F0C_OOB_SELECT_RAW);

      desc[0] = va;
      desc[1] = S_008F04_BASE_ADDRESS_HI(va >> 32) | S_008F04_STRIDE(vb->stride);
      desc[2] = num_records;
      desc[3] = rsrc_word3;

      if (first_vb_use_mask & (1 << i)) {
         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, si_resource(vb->buffer.resource),
                                   RADEON_USAGE_READ, RADEON_PRIO_VERTEX_BUFFER);
      }
   }

   /* Don't flush the const cache. It would have a very negative effect
    * on performance (confirmed by testing). New descriptors are always
    * uploaded to a fresh new buffer, so I don't think flushing the const
    * cache is needed. */
   si_mark_atom_dirty(sctx, &sctx->atoms.s.shader_pointers);
   sctx->vertex_buffer_user_sgprs_dirty = num_vbos_in_user_sgprs > 0;
   sctx->vertex_buffers_dirty = false;
   return true;
}

static void si_get_draw_start_count(struct si_context *sctx, const struct pipe_draw_info *info,
                                    const struct pipe_draw_indirect_info *indirect,
                                    const struct pipe_draw_start_count *draws,
                                    unsigned num_draws, unsigned *start, unsigned *count)
{
   if (indirect && !indirect->count_from_stream_output) {
      unsigned indirect_count;
      struct pipe_transfer *transfer;
      unsigned begin, end;
      unsigned map_size;
      unsigned *data;

      if (indirect->indirect_draw_count) {
         data = (unsigned*)
                pipe_buffer_map_range(&sctx->b, indirect->indirect_draw_count,
                                      indirect->indirect_draw_count_offset, sizeof(unsigned),
                                      PIPE_MAP_READ, &transfer);

         indirect_count = *data;

         pipe_buffer_unmap(&sctx->b, transfer);
      } else {
         indirect_count = indirect->draw_count;
      }

      if (!indirect_count) {
         *start = *count = 0;
         return;
      }

      map_size = (indirect_count - 1) * indirect->stride + 3 * sizeof(unsigned);
      data = (unsigned*)
             pipe_buffer_map_range(&sctx->b, indirect->buffer, indirect->offset, map_size,
                                   PIPE_MAP_READ, &transfer);

      begin = UINT_MAX;
      end = 0;

      for (unsigned i = 0; i < indirect_count; ++i) {
         unsigned count = data[0];
         unsigned start = data[2];

         if (count > 0) {
            begin = MIN2(begin, start);
            end = MAX2(end, start + count);
         }

         data += indirect->stride / sizeof(unsigned);
      }

      pipe_buffer_unmap(&sctx->b, transfer);

      if (begin < end) {
         *start = begin;
         *count = end - begin;
      } else {
         *start = *count = 0;
      }
   } else {
      unsigned min_element = UINT_MAX;
      unsigned max_element = 0;

      for (unsigned i = 0; i < num_draws; i++) {
         min_element = MIN2(min_element, draws[i].start);
         max_element = MAX2(max_element, draws[i].start + draws[i].count);
      }

      *start = min_element;
      *count = max_element - min_element;
   }
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG>
static void si_emit_all_states(struct si_context *sctx, const struct pipe_draw_info *info,
                               const struct pipe_draw_indirect_info *indirect,
                               enum pipe_prim_type prim, unsigned instance_count,
                               unsigned min_vertex_count, bool primitive_restart,
                               unsigned skip_atom_mask)
{
   unsigned num_patches = 0;

   si_emit_rasterizer_prim_state<HAS_GS, NGG>(sctx);
   if (HAS_TESS)
      si_emit_derived_tess_state(sctx, info, &num_patches);

   /* Emit state atoms. */
   unsigned mask = sctx->dirty_atoms & ~skip_atom_mask;
   while (mask)
      sctx->atoms.array[u_bit_scan(&mask)].emit(sctx);

   sctx->dirty_atoms &= skip_atom_mask;

   /* Emit states. */
   mask = sctx->dirty_states;
   while (mask) {
      unsigned i = u_bit_scan(&mask);
      struct si_pm4_state *state = sctx->queued.array[i];

      if (!state || sctx->emitted.array[i] == state)
         continue;

      si_pm4_emit(sctx, state);
      sctx->emitted.array[i] = state;
   }
   sctx->dirty_states = 0;

   /* Emit draw states. */
   si_emit_vs_state(sctx, info);
   si_emit_draw_registers<GFX_VERSION, HAS_TESS, HAS_GS, NGG>
         (sctx, info, indirect, prim, num_patches, instance_count, primitive_restart,
          min_vertex_count);
}

static bool si_all_vs_resources_read_only(struct si_context *sctx, struct pipe_resource *indexbuf)
{
   struct radeon_winsys *ws = sctx->ws;
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct si_descriptors *buffers =
      &sctx->descriptors[si_const_and_shader_buffer_descriptors_idx(PIPE_SHADER_VERTEX)];
   struct si_shader_selector *vs = sctx->vs_shader.cso;
   struct si_vertex_elements *velems = sctx->vertex_elements;
   unsigned num_velems = velems->count;
   unsigned num_images = vs->info.base.num_images;

   /* Index buffer. */
   if (indexbuf && ws->cs_is_buffer_referenced(cs, si_resource(indexbuf)->buf, RADEON_USAGE_WRITE))
      goto has_write_reference;

   /* Vertex buffers. */
   for (unsigned i = 0; i < num_velems; i++) {
      if (!((1 << i) & velems->first_vb_use_mask))
         continue;

      unsigned vb_index = velems->vertex_buffer_index[i];
      struct pipe_resource *res = sctx->vertex_buffer[vb_index].buffer.resource;
      if (!res)
         continue;

      if (ws->cs_is_buffer_referenced(cs, si_resource(res)->buf, RADEON_USAGE_WRITE))
         goto has_write_reference;
   }

   /* Constant and shader buffers. */
   for (unsigned i = 0; i < buffers->num_active_slots; i++) {
      unsigned index = buffers->first_active_slot + i;
      struct pipe_resource *res = sctx->const_and_shader_buffers[PIPE_SHADER_VERTEX].buffers[index];
      if (!res)
         continue;

      if (ws->cs_is_buffer_referenced(cs, si_resource(res)->buf, RADEON_USAGE_WRITE))
         goto has_write_reference;
   }

   /* Samplers. */
   if (vs->info.base.textures_used) {
      unsigned num_samplers = util_last_bit(vs->info.base.textures_used);

      for (unsigned i = 0; i < num_samplers; i++) {
         struct pipe_sampler_view *view = sctx->samplers[PIPE_SHADER_VERTEX].views[i];
         if (!view)
            continue;

         if (ws->cs_is_buffer_referenced(cs, si_resource(view->texture)->buf, RADEON_USAGE_WRITE))
            goto has_write_reference;
      }
   }

   /* Images. */
   if (num_images) {
      for (unsigned i = 0; i < num_images; i++) {
         struct pipe_resource *res = sctx->images[PIPE_SHADER_VERTEX].views[i].resource;
         if (!res)
            continue;

         if (ws->cs_is_buffer_referenced(cs, si_resource(res)->buf, RADEON_USAGE_WRITE))
            goto has_write_reference;
      }
   }

   return true;

has_write_reference:
   /* If the current gfx IB has enough packets, flush it to remove write
    * references to buffers.
    */
   if (cs->prev_dw + cs->current.cdw > 2048) {
      si_flush_gfx_cs(sctx, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW, NULL);
      assert(si_all_vs_resources_read_only(sctx, indexbuf));
      return true;
   }
   return false;
}

static ALWAYS_INLINE bool pd_msg(const char *s)
{
   if (SI_PRIM_DISCARD_DEBUG)
      printf("PD failed: %s\n", s);
   return false;
}

#define DRAW_CLEANUP do {                                 \
      if (index_size && indexbuf != info->index.resource) \
         pipe_resource_reference(&indexbuf, NULL);        \
   } while (0)

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG,
          si_has_prim_discard_cs ALLOW_PRIM_DISCARD_CS>
static void si_draw_vbo(struct pipe_context *ctx,
                        const struct pipe_draw_info *info,
                        const struct pipe_draw_indirect_info *indirect,
                        const struct pipe_draw_start_count *draws,
                        unsigned num_draws)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
   struct pipe_resource *indexbuf = info->index.resource;
   unsigned dirty_tex_counter, dirty_buf_counter;
   enum pipe_prim_type rast_prim, prim = info->mode;
   unsigned index_size = info->index_size;
   unsigned index_offset = indirect && indirect->buffer ? draws[0].start * index_size : 0;
   unsigned instance_count = info->instance_count;
   bool primitive_restart =
      info->primitive_restart &&
      (!sctx->screen->options.prim_restart_tri_strips_only ||
       (prim != PIPE_PRIM_TRIANGLE_STRIP && prim != PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY));

   /* GFX6-GFX7 treat instance_count==0 as instance_count==1. There is
    * no workaround for indirect draws, but we can at least skip
    * direct draws.
    */
   if (unlikely(!indirect && !instance_count))
      return;

   struct si_shader_selector *vs = sctx->vs_shader.cso;
   if (unlikely(!vs || sctx->num_vertex_elements < vs->num_vs_inputs ||
                (!sctx->ps_shader.cso && !rs->rasterizer_discard) ||
                (HAS_TESS != (prim == PIPE_PRIM_PATCHES)))) {
      assert(0);
      return;
   }

   /* Recompute and re-emit the texture resource states if needed. */
   dirty_tex_counter = p_atomic_read(&sctx->screen->dirty_tex_counter);
   if (unlikely(dirty_tex_counter != sctx->last_dirty_tex_counter)) {
      sctx->last_dirty_tex_counter = dirty_tex_counter;
      sctx->framebuffer.dirty_cbufs |= ((1 << sctx->framebuffer.state.nr_cbufs) - 1);
      sctx->framebuffer.dirty_zsbuf = true;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.framebuffer);
      si_update_all_texture_descriptors(sctx);
   }

   dirty_buf_counter = p_atomic_read(&sctx->screen->dirty_buf_counter);
   if (unlikely(dirty_buf_counter != sctx->last_dirty_buf_counter)) {
      sctx->last_dirty_buf_counter = dirty_buf_counter;
      /* Rebind all buffers unconditionally. */
      si_rebind_buffer(sctx, NULL);
   }

   si_decompress_textures(sctx, u_bit_consecutive(0, SI_NUM_GRAPHICS_SHADERS));

   /* Set the rasterization primitive type.
    *
    * This must be done after si_decompress_textures, which can call
    * draw_vbo recursively, and before si_update_shaders, which uses
    * current_rast_prim for this draw_vbo call. */
   if (HAS_GS) {
      /* Only possibilities: POINTS, LINE_STRIP, TRIANGLES */
      rast_prim = sctx->gs_shader.cso->rast_prim;
   } else if (HAS_TESS) {
      /* Only possibilities: POINTS, LINE_STRIP, TRIANGLES */
      rast_prim = sctx->tes_shader.cso->rast_prim;
   } else if (util_rast_prim_is_triangles(prim)) {
      rast_prim = PIPE_PRIM_TRIANGLES;
   } else {
      /* Only possibilities, POINTS, LINE*, RECTANGLES */
      rast_prim = prim;
   }

   if (rast_prim != sctx->current_rast_prim) {
      if (util_prim_is_points_or_lines(sctx->current_rast_prim) !=
          util_prim_is_points_or_lines(rast_prim))
         si_mark_atom_dirty(sctx, &sctx->atoms.s.guardband);

      sctx->current_rast_prim = rast_prim;
      sctx->do_update_shaders = true;
   }

   if (HAS_TESS) {
      struct si_shader_selector *tcs = sctx->tcs_shader.cso;

      /* The rarely occuring tcs == NULL case is not optimized. */
      bool same_patch_vertices =
         GFX_VERSION >= GFX9 &&
         tcs && info->vertices_per_patch == tcs->info.base.tess.tcs_vertices_out;

      if (sctx->same_patch_vertices != same_patch_vertices) {
         sctx->same_patch_vertices = same_patch_vertices;
         sctx->do_update_shaders = true;
      }

      if (GFX_VERSION == GFX9 && sctx->screen->info.has_ls_vgpr_init_bug) {
         /* Determine whether the LS VGPR fix should be applied.
          *
          * It is only required when num input CPs > num output CPs,
          * which cannot happen with the fixed function TCS. We should
          * also update this bit when switching from TCS to fixed
          * function TCS.
          */
         bool ls_vgpr_fix =
            tcs && info->vertices_per_patch > tcs->info.base.tess.tcs_vertices_out;

         if (ls_vgpr_fix != sctx->ls_vgpr_fix) {
            sctx->ls_vgpr_fix = ls_vgpr_fix;
            sctx->do_update_shaders = true;
         }
      }
   }

   if (GFX_VERSION <= GFX9 && HAS_GS) {
      /* Determine whether the GS triangle strip adjacency fix should
       * be applied. Rotate every other triangle if
       * - triangle strips with adjacency are fed to the GS and
       * - primitive restart is disabled (the rotation doesn't help
       *   when the restart occurs after an odd number of triangles).
       */
      bool gs_tri_strip_adj_fix =
         !HAS_TESS && prim == PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY && !primitive_restart;

      if (gs_tri_strip_adj_fix != sctx->gs_tri_strip_adj_fix) {
         sctx->gs_tri_strip_adj_fix = gs_tri_strip_adj_fix;
         sctx->do_update_shaders = true;
      }
   }

   if (index_size) {
      /* Translate or upload, if needed. */
      /* 8-bit indices are supported on GFX8. */
      if (GFX_VERSION <= GFX7 && index_size == 1) {
         unsigned start, count, start_offset, size, offset;
         void *ptr;

         si_get_draw_start_count(sctx, info, indirect, draws, num_draws, &start, &count);
         start_offset = start * 2;
         size = count * 2;

         indexbuf = NULL;
         u_upload_alloc(ctx->stream_uploader, start_offset, size,
                        si_optimal_tcc_alignment(sctx, size), &offset, &indexbuf, &ptr);
         if (unlikely(!indexbuf))
            return;

         util_shorten_ubyte_elts_to_userptr(&sctx->b, info, 0, 0, index_offset + start, count, ptr);

         /* info->start will be added by the drawing code */
         index_offset = offset - start_offset;
         index_size = 2;
      } else if (info->has_user_indices) {
         unsigned start_offset;

         assert(!indirect);
         assert(num_draws == 1);
         start_offset = draws[0].start * index_size;

         indexbuf = NULL;
         u_upload_data(ctx->stream_uploader, start_offset, draws[0].count * index_size,
                       sctx->screen->info.tcc_cache_line_size,
                       (char *)info->index.user + start_offset, &index_offset, &indexbuf);
         if (unlikely(!indexbuf))
            return;

         /* info->start will be added by the drawing code */
         index_offset -= start_offset;
      } else if (GFX_VERSION <= GFX7 && si_resource(indexbuf)->TC_L2_dirty) {
         /* GFX8 reads index buffers through TC L2, so it doesn't
          * need this. */
         sctx->flags |= SI_CONTEXT_WB_L2;
         si_resource(indexbuf)->TC_L2_dirty = false;
      }
   }

   bool dispatch_prim_discard_cs = false;
   bool prim_discard_cs_instancing = false;
   unsigned original_index_size = index_size;
   unsigned avg_direct_count = 0;
   unsigned min_direct_count = 0;
   unsigned total_direct_count = 0;

   if (indirect) {
      /* Add the buffer size for memory checking in need_cs_space. */
      if (indirect->buffer)
         si_context_add_resource_size(sctx, indirect->buffer);

      /* Indirect buffers use TC L2 on GFX9, but not older hw. */
      if (GFX_VERSION <= GFX8) {
         if (indirect->buffer && si_resource(indirect->buffer)->TC_L2_dirty) {
            sctx->flags |= SI_CONTEXT_WB_L2;
            si_resource(indirect->buffer)->TC_L2_dirty = false;
         }

         if (indirect->indirect_draw_count &&
             si_resource(indirect->indirect_draw_count)->TC_L2_dirty) {
            sctx->flags |= SI_CONTEXT_WB_L2;
            si_resource(indirect->indirect_draw_count)->TC_L2_dirty = false;
         }
      }
   } else {
      min_direct_count = num_draws ? UINT_MAX : 0;
      for (unsigned i = 0; i < num_draws; i++) {
         unsigned count = draws[i].count;

         total_direct_count += count;
         min_direct_count = MIN2(min_direct_count, count);
      }
      avg_direct_count = (total_direct_count / num_draws) * instance_count;
   }

   /* Determine if we can use the primitive discard compute shader. */
   if (ALLOW_PRIM_DISCARD_CS &&
       (avg_direct_count > sctx->prim_discard_vertex_count_threshold
           ? (sctx->compute_num_verts_rejected += total_direct_count, true)
           : /* Add, then return true. */
           (sctx->compute_num_verts_ineligible += total_direct_count,
            false)) && /* Add, then return false. */
       (primitive_restart ?
                          /* Supported prim types with primitive restart: */
           (prim == PIPE_PRIM_TRIANGLE_STRIP || pd_msg("bad prim type with primitive restart")) &&
              /* Disallow instancing with primitive restart: */
              (instance_count == 1 || pd_msg("instance_count > 1 with primitive restart"))
                          :
                          /* Supported prim types without primitive restart + allow instancing: */
           (1 << prim) & ((1 << PIPE_PRIM_TRIANGLES) | (1 << PIPE_PRIM_TRIANGLE_STRIP) |
                          (1 << PIPE_PRIM_TRIANGLE_FAN)) &&
              /* Instancing is limited to 16-bit indices, because InstanceID is packed into
                 VertexID. */
              /* TODO: DrawArraysInstanced doesn't sometimes work, so it's disabled. */
              (instance_count == 1 ||
               (instance_count <= USHRT_MAX && index_size && index_size <= 2) ||
               pd_msg("instance_count too large or index_size == 4 or DrawArraysInstanced"))) &&
       ((info->drawid == 0 && (num_draws == 1 || !info->increment_draw_id)) ||
        !sctx->vs_shader.cso->info.uses_drawid || pd_msg("draw_id > 0")) &&
       (!sctx->render_cond || pd_msg("render condition")) &&
       /* Forced enablement ignores pipeline statistics queries. */
       (sctx->screen->debug_flags & (DBG(PD) | DBG(ALWAYS_PD)) ||
        (!sctx->num_pipeline_stat_queries && !sctx->streamout.prims_gen_query_enabled) ||
        pd_msg("pipestat or primgen query")) &&
       (!sctx->vertex_elements->instance_divisor_is_fetched || pd_msg("loads instance divisors")) &&
       (!HAS_TESS || pd_msg("uses tess")) &&
       (!HAS_GS || pd_msg("uses GS")) &&
       (!sctx->ps_shader.cso->info.uses_primid || pd_msg("PS uses PrimID")) &&
       !rs->polygon_mode_enabled &&
#if SI_PRIM_DISCARD_DEBUG /* same as cso->prim_discard_cs_allowed */
       (!sctx->vs_shader.cso->info.uses_bindless_images || pd_msg("uses bindless images")) &&
       (!sctx->vs_shader.cso->info.uses_bindless_samplers || pd_msg("uses bindless samplers")) &&
       (!sctx->vs_shader.cso->info.writes_memory || pd_msg("writes memory")) &&
       (!sctx->vs_shader.cso->info.writes_viewport_index || pd_msg("writes viewport index")) &&
       !sctx->vs_shader.cso->info.base.vs.window_space_position &&
       !sctx->vs_shader.cso->so.num_outputs &&
#else
       (sctx->vs_shader.cso->prim_discard_cs_allowed ||
        pd_msg("VS shader uses unsupported features")) &&
#endif
       /* Check that all buffers are used for read only, because compute
        * dispatches can run ahead. */
       (si_all_vs_resources_read_only(sctx, index_size ? indexbuf : NULL) ||
        pd_msg("write reference"))) {
      switch (si_prepare_prim_discard_or_split_draw(sctx, info, draws, num_draws,
                                                    primitive_restart, total_direct_count)) {
      case SI_PRIM_DISCARD_ENABLED:
         original_index_size = index_size;
         prim_discard_cs_instancing = instance_count > 1;
         dispatch_prim_discard_cs = true;

         /* The compute shader changes/lowers the following: */
         prim = PIPE_PRIM_TRIANGLES;
         index_size = 4;
         instance_count = 1;
         primitive_restart = false;
         sctx->compute_num_verts_rejected -= total_direct_count;
         sctx->compute_num_verts_accepted += total_direct_count;
         break;
      case SI_PRIM_DISCARD_DISABLED:
         break;
      case SI_PRIM_DISCARD_DRAW_SPLIT:
         sctx->compute_num_verts_rejected -= total_direct_count;
         FALLTHROUGH;
      case SI_PRIM_DISCARD_MULTI_DRAW_SPLIT:
         /* The multi draw was split into multiple ones and executed. Return. */
         DRAW_CLEANUP;
         return;
      }
   }

   if (ALLOW_PRIM_DISCARD_CS &&
       prim_discard_cs_instancing != sctx->prim_discard_cs_instancing) {
      sctx->prim_discard_cs_instancing = prim_discard_cs_instancing;
      sctx->do_update_shaders = true;
   }

   /* Update NGG culling settings. */
   uint8_t old_ngg_culling = sctx->ngg_culling;
   if (GFX_VERSION >= GFX10) {
      struct si_shader_selector *hw_vs;
      if (NGG && !dispatch_prim_discard_cs && rast_prim == PIPE_PRIM_TRIANGLES &&
          (hw_vs = si_get_vs(sctx)->cso) &&
          (avg_direct_count > hw_vs->ngg_cull_vert_threshold ||
           (!index_size &&
            avg_direct_count > hw_vs->ngg_cull_nonindexed_fast_launch_vert_threshold &&
            prim & ((1 << PIPE_PRIM_TRIANGLES) |
                    (1 << PIPE_PRIM_TRIANGLE_STRIP))))) {
         uint8_t ngg_culling = 0;

         if (rs->rasterizer_discard) {
            ngg_culling |= SI_NGG_CULL_FRONT_FACE | SI_NGG_CULL_BACK_FACE;
         } else {
            /* Polygon mode can't use view and small primitive culling,
             * because it draws points or lines where the culling depends
             * on the point or line width.
             */
            if (!rs->polygon_mode_enabled)
               ngg_culling |= SI_NGG_CULL_VIEW_SMALLPRIMS;

            if (sctx->viewports.y_inverted ? rs->cull_back : rs->cull_front)
               ngg_culling |= SI_NGG_CULL_FRONT_FACE;
            if (sctx->viewports.y_inverted ? rs->cull_front : rs->cull_back)
               ngg_culling |= SI_NGG_CULL_BACK_FACE;
         }

         /* Use NGG fast launch for certain primitive types.
          * A draw must have at least 1 full primitive.
          */
         if (ngg_culling &&
             hw_vs->ngg_cull_nonindexed_fast_launch_vert_threshold < UINT32_MAX &&
             min_direct_count >= 3 && !HAS_TESS && !HAS_GS) {
            if (prim == PIPE_PRIM_TRIANGLES && !index_size) {
               ngg_culling |= SI_NGG_CULL_GS_FAST_LAUNCH_TRI_LIST;
#if 0 /* It's disabled because this hangs: AMD_DEBUG=nggc torcs */
            } else if (prim == PIPE_PRIM_TRIANGLE_STRIP && !primitive_restart) {
               ngg_culling |= SI_NGG_CULL_GS_FAST_LAUNCH_TRI_STRIP |
                              SI_NGG_CULL_GS_FAST_LAUNCH_INDEX_SIZE_PACKED(MIN2(index_size, 3));
               /* The index buffer will be emulated. */
               index_size = 0;
#endif
            }
         }

         if (ngg_culling != old_ngg_culling) {
            /* If shader compilation is not ready, this setting will be rejected. */
            sctx->ngg_culling = ngg_culling;
            sctx->do_update_shaders = true;
         }
      } else if (old_ngg_culling) {
         sctx->ngg_culling = 0;
         sctx->do_update_shaders = true;
      }
   }

   if (sctx->shader_has_inlinable_uniforms_mask &
       sctx->inlinable_uniforms_valid_mask &
       sctx->inlinable_uniforms_dirty_mask) {
      sctx->do_update_shaders = true;
      /* If inlinable uniforms are not valid, they are also not dirty, so clear all bits. */
      sctx->inlinable_uniforms_dirty_mask = 0;
   }

   if (unlikely(sctx->do_update_shaders)) {
      if (unlikely(!si_update_shaders(sctx))) {
         DRAW_CLEANUP;
         return;
      }

      /* Insert a VGT_FLUSH when enabling fast launch changes to prevent hangs.
       * See issues #2418, #2426, #2434
       *
       * This is the setting that is used by the draw.
       */
      if (GFX_VERSION >= GFX10) {
         uint8_t ngg_culling = si_get_vs(sctx)->current->key.opt.ngg_culling;
         if (GFX_VERSION == GFX10 &&
             !(old_ngg_culling & SI_NGG_CULL_GS_FAST_LAUNCH_ALL) &&
             ngg_culling & SI_NGG_CULL_GS_FAST_LAUNCH_ALL)
            sctx->flags |= SI_CONTEXT_VGT_FLUSH;

         if (old_ngg_culling & SI_NGG_CULL_GS_FAST_LAUNCH_INDEX_SIZE_PACKED(~0) &&
             !(ngg_culling & SI_NGG_CULL_GS_FAST_LAUNCH_INDEX_SIZE_PACKED(~0))) {
            /* Need to re-set these, because we have bound an index buffer there. */
            sctx->shader_pointers_dirty |=
               (1u << si_const_and_shader_buffer_descriptors_idx(PIPE_SHADER_GEOMETRY)) |
               (1u << si_sampler_and_image_descriptors_idx(PIPE_SHADER_GEOMETRY));
            si_mark_atom_dirty(sctx, &sctx->atoms.s.shader_pointers);
         }

         /* Set this to the correct value determined by si_update_shaders. */
         sctx->ngg_culling = ngg_culling;
      }
   }

   si_need_gfx_cs_space(sctx, num_draws);

   /* If we're using a secure context, determine if cs must be secure or not */
   if (GFX_VERSION >= GFX9 && unlikely(radeon_uses_secure_bos(sctx->ws))) {
      bool secure = si_gfx_resources_check_encrypted(sctx);
      if (secure != sctx->ws->cs_is_secure(&sctx->gfx_cs)) {
         si_flush_gfx_cs(sctx, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW |
                               RADEON_FLUSH_TOGGLE_SECURE_SUBMISSION, NULL);
      }
   }

   /* Since we've called si_context_add_resource_size for vertex buffers,
    * this must be called after si_need_cs_space, because we must let
    * need_cs_space flush before we add buffers to the buffer list.
    */
   if (sctx->bo_list_add_all_gfx_resources)
      si_gfx_resources_add_all_to_bo_list(sctx);

   if (unlikely(!si_upload_graphics_shader_descriptors(sctx) ||
                (sctx->vertex_buffers_dirty &&
                 sctx->num_vertex_elements &&
                 !si_upload_vertex_buffer_descriptors<GFX_VERSION>(sctx)))) {
      DRAW_CLEANUP;
      return;
   }

   /* Vega10/Raven scissor bug workaround. When any context register is
    * written (i.e. the GPU rolls the context), PA_SC_VPORT_SCISSOR
    * registers must be written too.
    */
   unsigned masked_atoms = 0;
   bool gfx9_scissor_bug = false;

   if (GFX_VERSION == GFX9 && sctx->screen->info.has_gfx9_scissor_bug) {
      masked_atoms |= si_get_atom_bit(sctx, &sctx->atoms.s.scissors);
      gfx9_scissor_bug = true;

      if ((indirect && indirect->count_from_stream_output) ||
          sctx->dirty_atoms & si_atoms_that_always_roll_context() ||
          sctx->dirty_states & si_states_that_always_roll_context())
         sctx->context_roll = true;
   }

   /* Use optimal packet order based on whether we need to sync the pipeline. */
   if (unlikely(sctx->flags & (SI_CONTEXT_FLUSH_AND_INV_CB | SI_CONTEXT_FLUSH_AND_INV_DB |
                               SI_CONTEXT_PS_PARTIAL_FLUSH | SI_CONTEXT_CS_PARTIAL_FLUSH))) {
      /* If we have to wait for idle, set all states first, so that all
       * SET packets are processed in parallel with previous draw calls.
       * Then draw and prefetch at the end. This ensures that the time
       * the CUs are idle is very short.
       */
      if (unlikely(sctx->flags & SI_CONTEXT_FLUSH_FOR_RENDER_COND))
         masked_atoms |= si_get_atom_bit(sctx, &sctx->atoms.s.render_cond);

      /* Emit all states except possibly render condition. */
      si_emit_all_states<GFX_VERSION, HAS_TESS, HAS_GS, NGG>
            (sctx, info, indirect, prim, instance_count, min_direct_count,
             primitive_restart, masked_atoms);
      sctx->emit_cache_flush(sctx);
      /* <-- CUs are idle here. */

      if (si_is_atom_dirty(sctx, &sctx->atoms.s.render_cond)) {
         sctx->atoms.s.render_cond.emit(sctx);
         sctx->dirty_atoms &= ~si_get_atom_bit(sctx, &sctx->atoms.s.render_cond);
      }

      if (GFX_VERSION == GFX9 && gfx9_scissor_bug &&
          (sctx->context_roll || si_is_atom_dirty(sctx, &sctx->atoms.s.scissors))) {
         sctx->atoms.s.scissors.emit(sctx);
         sctx->dirty_atoms &= ~si_get_atom_bit(sctx, &sctx->atoms.s.scissors);
      }
      assert(sctx->dirty_atoms == 0);

      si_emit_draw_packets<GFX_VERSION, NGG, ALLOW_PRIM_DISCARD_CS>
            (sctx, info, indirect, draws, num_draws, indexbuf, index_size,
             index_offset, instance_count, dispatch_prim_discard_cs,
             original_index_size);
      /* <-- CUs are busy here. */

      /* Start prefetches after the draw has been started. Both will run
       * in parallel, but starting the draw first is more important.
       */
      if (GFX_VERSION >= GFX7 && sctx->prefetch_L2_mask)
         cik_emit_prefetch_L2(sctx, false);
   } else {
      /* If we don't wait for idle, start prefetches first, then set
       * states, and draw at the end.
       */
      if (sctx->flags)
         sctx->emit_cache_flush(sctx);

      /* Only prefetch the API VS and VBO descriptors. */
      if (GFX_VERSION >= GFX7 && sctx->prefetch_L2_mask)
         cik_emit_prefetch_L2(sctx, true);

      si_emit_all_states<GFX_VERSION, HAS_TESS, HAS_GS, NGG>
            (sctx, info, indirect, prim, instance_count, min_direct_count,
             primitive_restart, masked_atoms);

      if (GFX_VERSION == GFX9 && gfx9_scissor_bug &&
          (sctx->context_roll || si_is_atom_dirty(sctx, &sctx->atoms.s.scissors))) {
         sctx->atoms.s.scissors.emit(sctx);
         sctx->dirty_atoms &= ~si_get_atom_bit(sctx, &sctx->atoms.s.scissors);
      }
      assert(sctx->dirty_atoms == 0);

      si_emit_draw_packets<GFX_VERSION, NGG, ALLOW_PRIM_DISCARD_CS>
            (sctx, info, indirect, draws, num_draws, indexbuf, index_size,
             index_offset, instance_count,
             dispatch_prim_discard_cs, original_index_size);

      /* Prefetch the remaining shaders after the draw has been
       * started. */
      if (GFX_VERSION >= GFX7 && sctx->prefetch_L2_mask)
         cik_emit_prefetch_L2(sctx, false);
   }

   /* Clear the context roll flag after the draw call.
    * Only used by the gfx9 scissor bug.
    */
   if (GFX_VERSION == GFX9)
      sctx->context_roll = false;

   if (unlikely(sctx->current_saved_cs)) {
      si_trace_emit(sctx);
      si_log_draw_state(sctx, sctx->log);
   }

   /* Workaround for a VGT hang when streamout is enabled.
    * It must be done after drawing. */
   if ((GFX_VERSION == GFX7 || GFX_VERSION == GFX8) &&
       (sctx->family == CHIP_HAWAII || sctx->family == CHIP_TONGA || sctx->family == CHIP_FIJI) &&
       si_get_strmout_en(sctx)) {
      sctx->flags |= SI_CONTEXT_VGT_STREAMOUT_SYNC;
   }

   if (unlikely(sctx->decompression_enabled)) {
      sctx->num_decompress_calls++;
   } else {
      sctx->num_draw_calls++;
      if (sctx->framebuffer.state.nr_cbufs > 1)
         sctx->num_mrt_draw_calls++;
      if (primitive_restart)
         sctx->num_prim_restart_calls++;
      if (G_0286E8_WAVESIZE(sctx->spi_tmpring_size))
         sctx->num_spill_draw_calls++;
   }

   DRAW_CLEANUP;
}

static void si_draw_rectangle(struct blitter_context *blitter, void *vertex_elements_cso,
                              blitter_get_vs_func get_vs, int x1, int y1, int x2, int y2,
                              float depth, unsigned num_instances, enum blitter_attrib_type type,
                              const union blitter_attrib *attrib)
{
   struct pipe_context *pipe = util_blitter_get_pipe(blitter);
   struct si_context *sctx = (struct si_context *)pipe;

   /* Pack position coordinates as signed int16. */
   sctx->vs_blit_sh_data[0] = (uint32_t)(x1 & 0xffff) | ((uint32_t)(y1 & 0xffff) << 16);
   sctx->vs_blit_sh_data[1] = (uint32_t)(x2 & 0xffff) | ((uint32_t)(y2 & 0xffff) << 16);
   sctx->vs_blit_sh_data[2] = fui(depth);

   switch (type) {
   case UTIL_BLITTER_ATTRIB_COLOR:
      memcpy(&sctx->vs_blit_sh_data[3], attrib->color, sizeof(float) * 4);
      break;
   case UTIL_BLITTER_ATTRIB_TEXCOORD_XY:
   case UTIL_BLITTER_ATTRIB_TEXCOORD_XYZW:
      memcpy(&sctx->vs_blit_sh_data[3], &attrib->texcoord, sizeof(attrib->texcoord));
      break;
   case UTIL_BLITTER_ATTRIB_NONE:;
   }

   pipe->bind_vs_state(pipe, si_get_blitter_vs(sctx, type, num_instances));

   struct pipe_draw_info info = {};
   struct pipe_draw_start_count draw;

   info.mode = SI_PRIM_RECTANGLE_LIST;
   info.instance_count = num_instances;

   draw.start = 0;
   draw.count = 3;

   /* Don't set per-stage shader pointers for VS. */
   sctx->shader_pointers_dirty &= ~SI_DESCS_SHADER_MASK(VERTEX);
   sctx->vertex_buffer_pointer_dirty = false;
   sctx->vertex_buffer_user_sgprs_dirty = false;

   pipe->draw_vbo(pipe, &info, NULL, &draw, 1);
}

extern "C"
void si_trace_emit(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   uint32_t trace_id = ++sctx->current_saved_cs->trace_id;

   si_cp_write_data(sctx, sctx->current_saved_cs->trace_buf, 0, 4, V_370_MEM, V_370_ME, &trace_id);

   radeon_emit(cs, PKT3(PKT3_NOP, 0, 0));
   radeon_emit(cs, AC_ENCODE_TRACE_POINT(trace_id));

   if (sctx->log)
      u_log_flush(sctx->log);
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS,
          si_has_ngg NGG, si_has_prim_discard_cs ALLOW_PRIM_DISCARD_CS>
static void si_init_draw_vbo(struct si_context *sctx)
{
   /* Prim discard CS is only useful on gfx7+ because gfx6 doesn't have async compute. */
   if (ALLOW_PRIM_DISCARD_CS && GFX_VERSION < GFX7)
      return;

   if (NGG && GFX_VERSION < GFX10)
      return;

   sctx->draw_vbo[GFX_VERSION - GFX6][HAS_TESS][HAS_GS][NGG][ALLOW_PRIM_DISCARD_CS] =
      si_draw_vbo<GFX_VERSION, HAS_TESS, HAS_GS, NGG, ALLOW_PRIM_DISCARD_CS>;
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS>
static void si_init_draw_vbo_all_internal_options(struct si_context *sctx)
{
   si_init_draw_vbo<GFX_VERSION, HAS_TESS, HAS_GS, NGG_OFF, PRIM_DISCARD_CS_OFF>(sctx);
   si_init_draw_vbo<GFX_VERSION, HAS_TESS, HAS_GS, NGG_OFF, PRIM_DISCARD_CS_ON>(sctx);
   si_init_draw_vbo<GFX_VERSION, HAS_TESS, HAS_GS, NGG_ON, PRIM_DISCARD_CS_OFF>(sctx);
   si_init_draw_vbo<GFX_VERSION, HAS_TESS, HAS_GS, NGG_ON, PRIM_DISCARD_CS_ON>(sctx);
}

template <chip_class GFX_VERSION>
static void si_init_draw_vbo_all_pipeline_options(struct si_context *sctx)
{
   si_init_draw_vbo_all_internal_options<GFX_VERSION, TESS_OFF, GS_OFF>(sctx);
   si_init_draw_vbo_all_internal_options<GFX_VERSION, TESS_OFF, GS_ON>(sctx);
   si_init_draw_vbo_all_internal_options<GFX_VERSION, TESS_ON, GS_OFF>(sctx);
   si_init_draw_vbo_all_internal_options<GFX_VERSION, TESS_ON, GS_ON>(sctx);
}

static void si_init_draw_vbo_all_families(struct si_context *sctx)
{
   si_init_draw_vbo_all_pipeline_options<GFX6>(sctx);
   si_init_draw_vbo_all_pipeline_options<GFX7>(sctx);
   si_init_draw_vbo_all_pipeline_options<GFX8>(sctx);
   si_init_draw_vbo_all_pipeline_options<GFX9>(sctx);
   si_init_draw_vbo_all_pipeline_options<GFX10>(sctx);
   si_init_draw_vbo_all_pipeline_options<GFX10_3>(sctx);
}

static void si_invalid_draw_vbo(struct pipe_context *pipe,
                                const struct pipe_draw_info *info,
                                const struct pipe_draw_indirect_info *indirect,
                                const struct pipe_draw_start_count *draws,
                                unsigned num_draws)
{
   unreachable("vertex shader not bound");
}

extern "C"
void si_init_draw_functions(struct si_context *sctx)
{
   si_init_draw_vbo_all_families(sctx);

   /* Bind a fake draw_vbo, so that draw_vbo isn't NULL, which would skip
    * initialization of callbacks in upper layers (such as u_threaded_context).
    */
   sctx->b.draw_vbo = si_invalid_draw_vbo;
   sctx->blitter->draw_rectangle = si_draw_rectangle;

   si_init_ia_multi_vgt_param_table(sctx);
}
