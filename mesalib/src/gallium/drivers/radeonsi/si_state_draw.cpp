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

#include "ac_exp_param.h"
#include "ac_sqtt.h"
#include "si_build_pm4.h"
#include "util/u_index_modify.h"
#include "util/u_prim.h"
#include "util/u_upload_mgr.h"

#if (GFX_VER == 6)
#define GFX(name) name##GFX6
#elif (GFX_VER == 7)
#define GFX(name) name##GFX7
#elif (GFX_VER == 8)
#define GFX(name) name##GFX8
#elif (GFX_VER == 9)
#define GFX(name) name##GFX9
#elif (GFX_VER == 10)
#define GFX(name) name##GFX10
#elif (GFX_VER == 103)
#define GFX(name) name##GFX10_3
#else
#error "Unknown gfx version"
#endif

/* special primitive types */
#define SI_PRIM_RECTANGLE_LIST PIPE_PRIM_MAX

template<int NUM_INTERP>
static void si_emit_spi_map(struct si_context *sctx)
{
   struct si_shader *ps = sctx->shader.ps.current;
   struct si_shader_info *psinfo = ps ? &ps->selector->info : NULL;
   unsigned spi_ps_input_cntl[NUM_INTERP];

   STATIC_ASSERT(NUM_INTERP >= 0 && NUM_INTERP <= 32);

   if (!NUM_INTERP)
      return;

   struct si_shader *vs = si_get_vs(sctx)->current;
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;

   for (unsigned i = 0; i < NUM_INTERP; i++) {
      union si_input_info input = psinfo->input[i];
      unsigned ps_input_cntl = vs->info.vs_output_ps_input_cntl[input.semantic];
      bool non_default_val = G_028644_OFFSET(ps_input_cntl) != 0x20;

      if (non_default_val) {
         if (input.interpolate == INTERP_MODE_FLAT ||
             (input.interpolate == INTERP_MODE_COLOR && rs->flatshade))
            ps_input_cntl |= S_028644_FLAT_SHADE(1);

         if (input.fp16_lo_hi_valid) {
            ps_input_cntl |= S_028644_FP16_INTERP_MODE(1) |
                             S_028644_ATTR0_VALID(1) | /* this must be set if FP16_INTERP_MODE is set */
                             S_028644_ATTR1_VALID(!!(input.fp16_lo_hi_valid & 0x2));
         }
      }

      if (input.semantic == VARYING_SLOT_PNTC ||
          (input.semantic >= VARYING_SLOT_TEX0 && input.semantic <= VARYING_SLOT_TEX7 &&
           rs->sprite_coord_enable & (1 << (input.semantic - VARYING_SLOT_TEX0)))) {
         /* Overwrite the whole value for sprite coordinates. */
         ps_input_cntl = S_028644_OFFSET(0) |
                         S_028644_PT_SPRITE_TEX(1);
         if (input.fp16_lo_hi_valid & 0x1) {
            ps_input_cntl |= S_028644_FP16_INTERP_MODE(1) |
                             S_028644_ATTR0_VALID(1);
         }
      }

      spi_ps_input_cntl[i] = ps_input_cntl;
   }

   /* R_028644_SPI_PS_INPUT_CNTL_0 */
   /* Dota 2: Only ~16% of SPI map updates set different values. */
   /* Talos: Only ~9% of SPI map updates set different values. */
   radeon_begin(&sctx->gfx_cs);
   radeon_opt_set_context_regn(sctx, R_028644_SPI_PS_INPUT_CNTL_0, spi_ps_input_cntl,
                               sctx->tracked_regs.spi_ps_input_cntl, NUM_INTERP);
   radeon_end_update_context_roll(sctx);
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG>
static bool si_update_shaders(struct si_context *sctx)
{
   struct pipe_context *ctx = (struct pipe_context *)sctx;
   struct si_shader *old_vs = si_get_vs_inline(sctx, HAS_TESS, HAS_GS)->current;
   unsigned old_kill_clip_distances = old_vs ? old_vs->key.opt.kill_clip_distances : 0;
   struct si_shader *old_ps = sctx->shader.ps.current;
   unsigned old_spi_shader_col_format =
      old_ps ? old_ps->key.part.ps.epilog.spi_shader_col_format : 0;
   int r;

   /* Update TCS and TES. */
   if (HAS_TESS) {
      if (!sctx->tess_rings) {
         si_init_tess_factor_ring(sctx);
         if (!sctx->tess_rings)
            return false;
      }

      if (sctx->shader.tcs.cso) {
         r = si_shader_select(ctx, &sctx->shader.tcs);
         if (r)
            return false;
         si_pm4_bind_state(sctx, hs, sctx->shader.tcs.current);
      } else {
         if (!sctx->fixed_func_tcs_shader.cso) {
            sctx->fixed_func_tcs_shader.cso =
               (struct si_shader_selector*)si_create_fixed_func_tcs(sctx);
            if (!sctx->fixed_func_tcs_shader.cso)
               return false;

            sctx->fixed_func_tcs_shader.key.part.tcs.epilog.invoc0_tess_factors_are_def =
               sctx->fixed_func_tcs_shader.cso->info.tessfactors_are_def_in_all_invocs;
         }

         r = si_shader_select(ctx, &sctx->fixed_func_tcs_shader);
         if (r)
            return false;
         si_pm4_bind_state(sctx, hs, sctx->fixed_func_tcs_shader.current);
      }

      if (!HAS_GS || GFX_VERSION <= GFX8) {
         r = si_shader_select(ctx, &sctx->shader.tes);
         if (r)
            return false;

         if (HAS_GS) {
            /* TES as ES */
            assert(GFX_VERSION <= GFX8);
            si_pm4_bind_state(sctx, es, sctx->shader.tes.current);
         } else if (NGG) {
            si_pm4_bind_state(sctx, gs, sctx->shader.tes.current);
         } else {
            si_pm4_bind_state(sctx, vs, sctx->shader.tes.current);
         }
      }
   } else {
      if (GFX_VERSION <= GFX8) {
         si_pm4_bind_state(sctx, ls, NULL);
         sctx->prefetch_L2_mask &= ~SI_PREFETCH_LS;
      }
      si_pm4_bind_state(sctx, hs, NULL);
      sctx->prefetch_L2_mask &= ~SI_PREFETCH_HS;
   }

   /* Update GS. */
   if (HAS_GS) {
      r = si_shader_select(ctx, &sctx->shader.gs);
      if (r)
         return false;
      si_pm4_bind_state(sctx, gs, sctx->shader.gs.current);
      if (!NGG) {
         si_pm4_bind_state(sctx, vs, sctx->shader.gs.cso->gs_copy_shader);

         if (!si_update_gs_ring_buffers(sctx))
            return false;
      } else {
         si_pm4_bind_state(sctx, vs, NULL);
         sctx->prefetch_L2_mask &= ~SI_PREFETCH_VS;
      }
   } else {
      if (!NGG) {
         si_pm4_bind_state(sctx, gs, NULL);
         sctx->prefetch_L2_mask &= ~SI_PREFETCH_GS;
         if (GFX_VERSION <= GFX8) {
            si_pm4_bind_state(sctx, es, NULL);
            sctx->prefetch_L2_mask &= ~SI_PREFETCH_ES;
         }
      }
   }

   /* Update VS. */
   if ((!HAS_TESS && !HAS_GS) || GFX_VERSION <= GFX8) {
      r = si_shader_select(ctx, &sctx->shader.vs);
      if (r)
         return false;

      if (!HAS_TESS && !HAS_GS) {
         if (NGG) {
            si_pm4_bind_state(sctx, gs, sctx->shader.vs.current);
            si_pm4_bind_state(sctx, vs, NULL);
            sctx->prefetch_L2_mask &= ~SI_PREFETCH_VS;
         } else {
            si_pm4_bind_state(sctx, vs, sctx->shader.vs.current);
         }
      } else if (HAS_TESS) {
         si_pm4_bind_state(sctx, ls, sctx->shader.vs.current);
      } else {
         assert(HAS_GS);
         si_pm4_bind_state(sctx, es, sctx->shader.vs.current);
      }
   }

   sctx->vs_uses_base_instance =
      sctx->shader.vs.current ? sctx->shader.vs.current->uses_base_instance :
      sctx->queued.named.hs ? sctx->queued.named.hs->uses_base_instance :
      sctx->shader.gs.current->uses_base_instance;

   union si_vgt_stages_key key;
   key.index = 0;

   /* Update VGT_SHADER_STAGES_EN. */
   if (HAS_TESS)
      key.u.tess = 1;
   if (HAS_GS)
      key.u.gs = 1;
   if (NGG)
      key.index |= si_get_vs_inline(sctx, HAS_TESS, HAS_GS)->current->ctx_reg.ngg.vgt_stages.index;

   struct si_pm4_state **pm4 = &sctx->vgt_shader_config[key.index];
   if (unlikely(!*pm4))
      *pm4 = si_build_vgt_shader_config(sctx->screen, key);
   si_pm4_bind_state(sctx, vgt_shader_config, *pm4);

   if (old_kill_clip_distances != si_get_vs_inline(sctx, HAS_TESS, HAS_GS)->current->key.opt.kill_clip_distances)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.clip_regs);

   r = si_shader_select(ctx, &sctx->shader.ps);
   if (r)
      return false;
   si_pm4_bind_state(sctx, ps, sctx->shader.ps.current);

   if (si_pm4_state_changed(sctx, ps) ||
       (!NGG && si_pm4_state_changed(sctx, vs)) ||
       (NGG && si_pm4_state_changed(sctx, gs))) {
      sctx->atoms.s.spi_map.emit = sctx->emit_spi_map[sctx->shader.ps.current->ctx_reg.ps.num_interp];
      si_mark_atom_dirty(sctx, &sctx->atoms.s.spi_map);
   }

   if ((GFX_VERSION >= GFX10_3 || (GFX_VERSION >= GFX9 && sctx->screen->info.rbplus_allowed)) &&
       si_pm4_state_changed(sctx, ps) &&
       (!old_ps || old_spi_shader_col_format !=
                      sctx->shader.ps.current->key.part.ps.epilog.spi_shader_col_format))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.cb_render_state);

   if (sctx->smoothing_enabled !=
       sctx->shader.ps.current->key.part.ps.epilog.poly_line_smoothing) {
      sctx->smoothing_enabled = sctx->shader.ps.current->key.part.ps.epilog.poly_line_smoothing;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);

      /* NGG cull state uses smoothing_enabled. */
      if (GFX_VERSION >= GFX10 && sctx->screen->use_ngg_culling)
         si_mark_atom_dirty(sctx, &sctx->atoms.s.ngg_cull_state);

      if (GFX_VERSION == GFX6)
         si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);

      if (sctx->framebuffer.nr_samples <= 1)
         si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_sample_locs);
   }

   if (unlikely(sctx->screen->debug_flags & DBG(SQTT) && sctx->thread_trace)) {
      /* Pretend the bound shaders form a vk pipeline */
      uint32_t pipeline_code_hash = 0;
      uint64_t base_address = ~0;

      for (int i = 0; i < SI_NUM_GRAPHICS_SHADERS; i++) {
         struct si_shader *shader = sctx->shaders[i].current;
         if (sctx->shaders[i].cso && shader) {
            pipeline_code_hash = _mesa_hash_data_with_seed(
               shader->binary.elf_buffer,
               shader->binary.elf_size,
               pipeline_code_hash);
            base_address = MIN2(base_address,
                                shader->bo->gpu_address);
         }
      }

      struct ac_thread_trace_data *thread_trace_data = sctx->thread_trace;
      if (!si_sqtt_pipeline_is_registered(thread_trace_data, pipeline_code_hash)) {
         si_sqtt_register_pipeline(sctx, pipeline_code_hash, base_address, false);
      }

      si_sqtt_describe_pipeline_bind(sctx, pipeline_code_hash, 0);
   }

   if ((GFX_VERSION <= GFX8 &&
        (si_pm4_state_enabled_and_changed(sctx, ls) || si_pm4_state_enabled_and_changed(sctx, es))) ||
       si_pm4_state_enabled_and_changed(sctx, hs) || si_pm4_state_enabled_and_changed(sctx, gs) ||
       si_pm4_state_enabled_and_changed(sctx, vs) || si_pm4_state_enabled_and_changed(sctx, ps)) {
      unsigned scratch_size = 0;

      if (HAS_TESS) {
         if (GFX_VERSION <= GFX8) /* LS */
            scratch_size = MAX2(scratch_size, sctx->shader.vs.current->config.scratch_bytes_per_wave);

         scratch_size = MAX2(scratch_size, sctx->queued.named.hs->config.scratch_bytes_per_wave);

         if (HAS_GS) {
            if (GFX_VERSION <= GFX8) /* ES */
               scratch_size = MAX2(scratch_size, sctx->shader.tes.current->config.scratch_bytes_per_wave);

            scratch_size = MAX2(scratch_size, sctx->shader.gs.current->config.scratch_bytes_per_wave);
         } else {
            scratch_size = MAX2(scratch_size, sctx->shader.tes.current->config.scratch_bytes_per_wave);
         }
      } else if (HAS_GS) {
         if (GFX_VERSION <= GFX8) /* ES */
            scratch_size = MAX2(scratch_size, sctx->shader.vs.current->config.scratch_bytes_per_wave);

         scratch_size = MAX2(scratch_size, sctx->shader.gs.current->config.scratch_bytes_per_wave);
      } else {
         scratch_size = MAX2(scratch_size, sctx->shader.vs.current->config.scratch_bytes_per_wave);
      }

      scratch_size = MAX2(scratch_size, sctx->shader.ps.current->config.scratch_bytes_per_wave);

      if (scratch_size && !si_update_spi_tmpring_size(sctx, scratch_size))
         return false;

      if (GFX_VERSION >= GFX7) {
         if (GFX_VERSION <= GFX8 && HAS_TESS && si_pm4_state_enabled_and_changed(sctx, ls))
            sctx->prefetch_L2_mask |= SI_PREFETCH_LS;

         if (HAS_TESS && si_pm4_state_enabled_and_changed(sctx, hs))
            sctx->prefetch_L2_mask |= SI_PREFETCH_HS;

         if (GFX_VERSION <= GFX8 && HAS_GS && si_pm4_state_enabled_and_changed(sctx, es))
            sctx->prefetch_L2_mask |= SI_PREFETCH_ES;

         if ((HAS_GS || NGG) && si_pm4_state_enabled_and_changed(sctx, gs))
            sctx->prefetch_L2_mask |= SI_PREFETCH_GS;

         if (!NGG && si_pm4_state_enabled_and_changed(sctx, vs))
            sctx->prefetch_L2_mask |= SI_PREFETCH_VS;

         if (si_pm4_state_enabled_and_changed(sctx, ps))
            sctx->prefetch_L2_mask |= SI_PREFETCH_PS;
      }
   }

   sctx->do_update_shaders = false;
   return true;
}

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

static void si_prefetch_shader_async(struct si_context *sctx, struct si_shader *shader)
{
   struct pipe_resource *bo = &shader->bo->b.b;

   si_cp_dma_prefetch(sctx, bo, 0, bo->width0);
}

enum si_L2_prefetch_mode {
   PREFETCH_BEFORE_DRAW = 1,
   PREFETCH_AFTER_DRAW,
   PREFETCH_ALL,
};

/**
 * Prefetch shaders.
 */
template<chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG,
         si_L2_prefetch_mode mode>
static void si_prefetch_shaders(struct si_context *sctx)
{
   unsigned mask = sctx->prefetch_L2_mask;

   /* GFX6 doesn't support the L2 prefetch. */
   if (GFX_VERSION < GFX7 || !mask)
      return;

   /* Prefetch shaders and VBO descriptors to TC L2. */
   if (GFX_VERSION >= GFX9) {
      /* Choose the right spot for the VBO prefetch. */
      if (HAS_TESS) {
         if (mode != PREFETCH_AFTER_DRAW) {
            if (mask & SI_PREFETCH_HS)
               si_prefetch_shader_async(sctx, sctx->queued.named.hs);

            if (mode == PREFETCH_BEFORE_DRAW)
               return;
         }

         if ((HAS_GS || NGG) && mask & SI_PREFETCH_GS)
            si_prefetch_shader_async(sctx, sctx->queued.named.gs);
         if (!NGG && mask & SI_PREFETCH_VS)
            si_prefetch_shader_async(sctx, sctx->queued.named.vs);
      } else if (HAS_GS || NGG) {
         if (mode != PREFETCH_AFTER_DRAW) {
            if (mask & SI_PREFETCH_GS)
               si_prefetch_shader_async(sctx, sctx->queued.named.gs);

            if (mode == PREFETCH_BEFORE_DRAW)
               return;
         }

         if (!NGG && mask & SI_PREFETCH_VS)
            si_prefetch_shader_async(sctx, sctx->queued.named.vs);
      } else {
         if (mode != PREFETCH_AFTER_DRAW) {
            if (mask & SI_PREFETCH_VS)
               si_prefetch_shader_async(sctx, sctx->queued.named.vs);

            if (mode == PREFETCH_BEFORE_DRAW)
               return;
         }
      }
   } else {
      /* GFX6-GFX8 */
      /* Choose the right spot for the VBO prefetch. */
      if (HAS_TESS) {
         if (mode != PREFETCH_AFTER_DRAW) {
            if (mask & SI_PREFETCH_LS)
               si_prefetch_shader_async(sctx, sctx->queued.named.ls);

            if (mode == PREFETCH_BEFORE_DRAW)
               return;
         }

         if (mask & SI_PREFETCH_HS)
            si_prefetch_shader_async(sctx, sctx->queued.named.hs);
         if (mask & SI_PREFETCH_ES)
            si_prefetch_shader_async(sctx, sctx->queued.named.es);
         if (mask & SI_PREFETCH_GS)
            si_prefetch_shader_async(sctx, sctx->queued.named.gs);
         if (mask & SI_PREFETCH_VS)
            si_prefetch_shader_async(sctx, sctx->queued.named.vs);
      } else if (HAS_GS) {
         if (mode != PREFETCH_AFTER_DRAW) {
            if (mask & SI_PREFETCH_ES)
               si_prefetch_shader_async(sctx, sctx->queued.named.es);

            if (mode == PREFETCH_BEFORE_DRAW)
               return;
         }

         if (mask & SI_PREFETCH_GS)
            si_prefetch_shader_async(sctx, sctx->queued.named.gs);
         if (mask & SI_PREFETCH_VS)
            si_prefetch_shader_async(sctx, sctx->queued.named.vs);
      } else {
         if (mode != PREFETCH_AFTER_DRAW) {
            if (mask & SI_PREFETCH_VS)
               si_prefetch_shader_async(sctx, sctx->queued.named.vs);

            if (mode == PREFETCH_BEFORE_DRAW)
               return;
         }
      }
   }

   if (mask & SI_PREFETCH_PS)
      si_prefetch_shader_async(sctx, sctx->queued.named.ps);

   /* This must be cleared only when AFTER_DRAW is true. */
   sctx->prefetch_L2_mask = 0;
}

/**
 * This calculates the LDS size for tessellation shaders (VS, TCS, TES).
 * LS.LDS_SIZE is shared by all 3 shader stages.
 *
 * The information about LDS and other non-compile-time parameters is then
 * written to userdata SGPRs.
 */
static void si_emit_derived_tess_state(struct si_context *sctx, unsigned *num_patches)
{
   struct si_shader *ls_current;
   struct si_shader_selector *ls;
   /* The TES pointer will only be used for sctx->last_tcs.
    * It would be wrong to think that TCS = TES. */
   struct si_shader_selector *tcs =
      sctx->shader.tcs.cso ? sctx->shader.tcs.cso : sctx->shader.tes.cso;
   unsigned tess_uses_primid = sctx->ia_multi_vgt_param_key.u.tess_uses_prim_id;
   bool has_primid_instancing_bug = sctx->chip_class == GFX6 && sctx->screen->info.max_se == 1;
   unsigned tes_sh_base = sctx->shader_pointers.sh_base[PIPE_SHADER_TESS_EVAL];
   uint8_t num_tcs_input_cp = sctx->patch_vertices;

   /* Since GFX9 has merged LS-HS in the TCS state, set LS = TCS. */
   if (sctx->chip_class >= GFX9) {
      if (sctx->shader.tcs.cso)
         ls_current = sctx->shader.tcs.current;
      else
         ls_current = sctx->fixed_func_tcs_shader.current;

      ls = ls_current->key.part.tcs.ls;
   } else {
      ls_current = sctx->shader.vs.current;
      ls = sctx->shader.vs.cso;
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
   unsigned num_tcs_inputs = util_last_bit64(ls->outputs_written);
   unsigned num_tcs_output_cp, num_tcs_outputs, num_tcs_patch_outputs;

   if (sctx->shader.tcs.cso) {
      num_tcs_outputs = util_last_bit64(tcs->outputs_written);
      num_tcs_output_cp = tcs->info.base.tess.tcs_vertices_out;
      num_tcs_patch_outputs = util_last_bit64(tcs->patch_outputs_written);
   } else {
      /* No TCS. Route varyings from LS to TES. */
      num_tcs_outputs = num_tcs_inputs;
      num_tcs_output_cp = num_tcs_input_cp;
      num_tcs_patch_outputs = 2; /* TESSINNER + TESSOUTER */
   }

   unsigned input_vertex_size = ls->lshs_vertex_stride;
   unsigned output_vertex_size = num_tcs_outputs * 16;
   unsigned input_patch_size;

   /* Allocate LDS for TCS inputs only if it's used. */
   if (!ls_current->key.opt.same_patch_vertices ||
       tcs->info.base.inputs_read & ~tcs->tcs_vgpr_only_inputs)
      input_patch_size = num_tcs_input_cp * input_vertex_size;
   else
      input_patch_size = 0;

   unsigned pervertex_output_patch_size = num_tcs_output_cp * output_vertex_size;
   unsigned output_patch_size = pervertex_output_patch_size + num_tcs_patch_outputs * 16;
   unsigned lds_per_patch;

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

   /* Ensure that we only need 4 waves per CU, so that we don't need to check
    * resource usage (such as whether we have enough VGPRs to fit the whole
    * threadgroup into the CU). It also ensures that the number of tcs in and out
    * vertices per threadgroup are at most 256, which is the hw limit.
    */
   unsigned max_verts_per_patch = MAX2(num_tcs_input_cp, num_tcs_output_cp);
   *num_patches = 256 / max_verts_per_patch;

   /* Not necessary for correctness, but higher numbers are slower.
    * The hardware can do more, but the radeonsi shader constant is
    * limited to 6 bits.
    */
   *num_patches = MIN2(*num_patches, 64); /* e.g. 64 triangles in exactly 3 waves */

   /* When distributed tessellation is unsupported, switch between SEs
    * at a higher frequency to manually balance the workload between SEs.
    */
   if (!sctx->screen->info.has_distributed_tess && sctx->screen->info.max_se > 1)
      *num_patches = MIN2(*num_patches, 16); /* recommended */

   /* Make sure the output data fits in the offchip buffer */
   *num_patches =
      MIN2(*num_patches, (sctx->screen->tess_offchip_block_dw_size * 4) / output_patch_size);

   /* Make sure that the data fits in LDS. This assumes the shaders only
    * use LDS for the inputs and outputs.
    *
    * The maximum allowed LDS size is 32K. Higher numbers can hang.
    * Use 16K as the maximum, so that we can fit 2 workgroups on the same CU.
    */
   ASSERTED unsigned max_lds_size = 32 * 1024; /* hw limit */
   unsigned target_lds_size = 16 * 1024; /* target at least 2 workgroups per CU, 16K each */
   *num_patches = MIN2(*num_patches, target_lds_size / lds_per_patch);
   *num_patches = MAX2(*num_patches, 1);
   assert(*num_patches * lds_per_patch <= max_lds_size);

   /* Make sure that vector lanes are fully occupied by cutting off the last wave
    * if it's only partially filled.
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

   unsigned output_patch0_offset = input_patch_size * *num_patches;
   unsigned perpatch_output_offset = output_patch0_offset + pervertex_output_patch_size;

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

   unsigned tcs_in_layout = S_VS_STATE_LS_OUT_PATCH_SIZE(input_patch_size / 4) |
                            S_VS_STATE_LS_OUT_VERTEX_SIZE(input_vertex_size / 4);
   unsigned tcs_out_layout = (output_patch_size / 4) | (num_tcs_input_cp << 13) | ring_va;
   unsigned tcs_out_offsets = (output_patch0_offset / 16) | ((perpatch_output_offset / 16) << 16);
   unsigned offchip_layout =
      (*num_patches - 1) | ((num_tcs_output_cp - 1) << 6) |
      ((pervertex_output_patch_size * *num_patches) << 11);

   /* Compute the LDS size. */
   unsigned lds_size = lds_per_patch * *num_patches;

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

   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   radeon_begin(cs);

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
   radeon_end();

   unsigned ls_hs_config =
         S_028B58_NUM_PATCHES(*num_patches) |
         S_028B58_HS_NUM_INPUT_CP(num_tcs_input_cp) |
         S_028B58_HS_NUM_OUTPUT_CP(num_tcs_output_cp);

   if (sctx->last_ls_hs_config != ls_hs_config) {
      radeon_begin(cs);
      if (sctx->chip_class >= GFX7) {
         radeon_set_context_reg_idx(cs, R_028B58_VGT_LS_HS_CONFIG, 2, ls_hs_config);
      } else {
         radeon_set_context_reg(cs, R_028B58_VGT_LS_HS_CONFIG, ls_hs_config);
      }
      radeon_end_update_context_roll(sctx);
      sctx->last_ls_hs_config = ls_hs_config;
   }
}

static unsigned si_num_prims_for_vertices(enum pipe_prim_type prim,
                                          unsigned count, unsigned vertices_per_patch)
{
   switch (prim) {
   case PIPE_PRIM_PATCHES:
      return count / vertices_per_patch;
   case PIPE_PRIM_POLYGON:
      /* It's a triangle fan with different edge flags. */
      return count >= 3 ? count - 2 : 0;
   case SI_PRIM_RECTANGLE_LIST:
      return count / 3;
   default:
      return u_decomposed_prims_for_vertices(prim, count);
   }
}

static unsigned si_get_init_multi_vgt_param(struct si_screen *sscreen, union si_vgt_param_key *key)
{
   STATIC_ASSERT(sizeof(union si_vgt_param_key) == 2);
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

static bool num_instanced_prims_less_than(const struct pipe_draw_indirect_info *indirect,
                                          enum pipe_prim_type prim,
                                          unsigned min_vertex_count,
                                          unsigned instance_count,
                                          unsigned num_prims,
                                          ubyte vertices_per_patch)
{
   if (indirect) {
      return indirect->buffer ||
             (instance_count > 1 && indirect->count_from_stream_output);
   } else {
      return instance_count > 1 &&
             si_num_prims_for_vertices(prim, min_vertex_count, vertices_per_patch) < num_prims;
   }
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS> ALWAYS_INLINE
static unsigned si_get_ia_multi_vgt_param(struct si_context *sctx,
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
      num_instanced_prims_less_than(indirect, prim, min_vertex_count, instance_count,
                                    primgroup_size, sctx->patch_vertices);
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
          num_instanced_prims_less_than(indirect, prim, min_vertex_count, instance_count, 2,
                                        sctx->patch_vertices))
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
template<chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG> ALWAYS_INLINE
static void si_emit_rasterizer_prim_state(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   enum pipe_prim_type rast_prim = sctx->current_rast_prim;
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;

   radeon_begin(cs);

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

   if (GFX_VERSION == GFX9)
      radeon_end_update_context_roll(sctx);
   else
      radeon_end();

   if (NGG) {
      struct si_shader *hw_vs = si_get_vs_inline(sctx, HAS_TESS, HAS_GS)->current;

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

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG>
ALWAYS_INLINE
static void si_emit_vs_state(struct si_context *sctx, unsigned index_size)
{
   if (sctx->num_vs_blit_sgprs) {
      /* Re-emit the state after we leave u_blitter. */
      sctx->last_vs_state = ~0;
      return;
   }

   if (sctx->shader.vs.cso->info.uses_base_vertex) {
      sctx->current_vs_state &= C_VS_STATE_INDEXED;
      sctx->current_vs_state |= S_VS_STATE_INDEXED(!!index_size);
   }

   if (sctx->current_vs_state != sctx->last_vs_state) {
      struct radeon_cmdbuf *cs = &sctx->gfx_cs;

      /* For the API vertex shader (VS_STATE_INDEXED, LS_OUT_*). */
      unsigned vs_base = si_get_user_data_base(GFX_VERSION, HAS_TESS, HAS_GS, NGG,
                                               PIPE_SHADER_VERTEX);
      radeon_begin(cs);
      radeon_set_sh_reg(cs, vs_base + SI_SGPR_VS_STATE_BITS * 4,
                        sctx->current_vs_state);

      /* Set CLAMP_VERTEX_COLOR and OUTPRIM in the last stage
       * before the rasterizer.
       *
       * For TES or the GS copy shader without NGG:
       */
      if (vs_base != R_00B130_SPI_SHADER_USER_DATA_VS_0) {
         radeon_set_sh_reg(cs, R_00B130_SPI_SHADER_USER_DATA_VS_0 + SI_SGPR_VS_STATE_BITS * 4,
                           sctx->current_vs_state);
      }

      /* For NGG: */
      if (GFX_VERSION >= GFX10 && vs_base != R_00B230_SPI_SHADER_USER_DATA_GS_0) {
         radeon_set_sh_reg(cs, R_00B230_SPI_SHADER_USER_DATA_GS_0 + SI_SGPR_VS_STATE_BITS * 4,
                           sctx->current_vs_state);
      }
      radeon_end();

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
static void si_emit_ia_multi_vgt_param(struct si_context *sctx,
                                       const struct pipe_draw_indirect_info *indirect,
                                       enum pipe_prim_type prim, unsigned num_patches,
                                       unsigned instance_count, bool primitive_restart,
                                       unsigned min_vertex_count)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned ia_multi_vgt_param;

   ia_multi_vgt_param =
      si_get_ia_multi_vgt_param<GFX_VERSION, HAS_TESS, HAS_GS>
         (sctx, indirect, prim, num_patches, instance_count, primitive_restart,
          min_vertex_count);

   /* Draw state. */
   if (ia_multi_vgt_param != sctx->last_multi_vgt_param) {
      radeon_begin(cs);

      if (GFX_VERSION == GFX9)
         radeon_set_uconfig_reg_idx(cs, sctx->screen, GFX_VERSION,
                                    R_030960_IA_MULTI_VGT_PARAM, 4, ia_multi_vgt_param);
      else if (GFX_VERSION >= GFX7)
         radeon_set_context_reg_idx(cs, R_028AA8_IA_MULTI_VGT_PARAM, 1, ia_multi_vgt_param);
      else
         radeon_set_context_reg(cs, R_028AA8_IA_MULTI_VGT_PARAM, ia_multi_vgt_param);

      radeon_end();

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
         ge_cntl = si_get_vs_inline(sctx, HAS_TESS, HAS_GS)->current->ge_cntl;
      }
   } else {
      unsigned primgroup_size;
      unsigned vertgroup_size;

      if (HAS_TESS) {
         primgroup_size = num_patches; /* must be a multiple of NUM_PATCHES */
         vertgroup_size = 0;
      } else if (HAS_GS) {
         unsigned vgt_gs_onchip_cntl = sctx->shader.gs.current->ctx_reg.gs.vgt_gs_onchip_cntl;
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
      struct radeon_cmdbuf *cs = &sctx->gfx_cs;

      radeon_begin(cs);
      radeon_set_uconfig_reg(cs, R_03096C_GE_CNTL, ge_cntl);
      radeon_end();
      sctx->last_multi_vgt_param = ge_cntl;
   }
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG> ALWAYS_INLINE
static void si_emit_draw_registers(struct si_context *sctx,
                                   const struct pipe_draw_indirect_info *indirect,
                                   enum pipe_prim_type prim, unsigned num_patches,
                                   unsigned instance_count, bool primitive_restart,
                                   unsigned restart_index, unsigned min_vertex_count)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   if (GFX_VERSION >= GFX10)
      gfx10_emit_ge_cntl<GFX_VERSION, HAS_TESS, HAS_GS, NGG>(sctx, num_patches);
   else
      si_emit_ia_multi_vgt_param<GFX_VERSION, HAS_TESS, HAS_GS>
         (sctx, indirect, prim, num_patches, instance_count, primitive_restart,
          min_vertex_count);

   radeon_begin(cs);

   if (prim != sctx->last_prim) {
      unsigned vgt_prim = si_conv_pipe_prim(prim);

      if (GFX_VERSION >= GFX10)
         radeon_set_uconfig_reg(cs, R_030908_VGT_PRIMITIVE_TYPE, vgt_prim);
      else if (GFX_VERSION >= GFX7)
         radeon_set_uconfig_reg_idx(cs, sctx->screen, GFX_VERSION, R_030908_VGT_PRIMITIVE_TYPE, 1, vgt_prim);
      else
         radeon_set_config_reg(cs, R_008958_VGT_PRIMITIVE_TYPE, vgt_prim);

      sctx->last_prim = prim;
   }

   /* Primitive restart. */
   if (primitive_restart != sctx->last_primitive_restart_en) {
      if (GFX_VERSION >= GFX9)
         radeon_set_uconfig_reg(cs, R_03092C_VGT_MULTI_PRIM_IB_RESET_EN, primitive_restart);
      else
         radeon_set_context_reg(cs, R_028A94_VGT_MULTI_PRIM_IB_RESET_EN, primitive_restart);

      sctx->last_primitive_restart_en = primitive_restart;
   }
   if (si_prim_restart_index_changed(sctx, primitive_restart, restart_index)) {
      radeon_set_context_reg(cs, R_02840C_VGT_MULTI_PRIM_IB_RESET_INDX, restart_index);
      sctx->last_restart_index = restart_index;
      if (GFX_VERSION == GFX9)
         sctx->context_roll = true;
   }
   radeon_end();
}

#define EMIT_SQTT_END_DRAW do {                                          \
      if (GFX_VERSION >= GFX9 && unlikely(sctx->thread_trace_enabled)) { \
         radeon_begin(&sctx->gfx_cs);                                    \
         radeon_emit(&sctx->gfx_cs, PKT3(PKT3_EVENT_WRITE, 0, 0));       \
         radeon_emit(&sctx->gfx_cs,                                      \
                     EVENT_TYPE(V_028A90_THREAD_TRACE_MARKER) |          \
                     EVENT_INDEX(0));                                    \
         radeon_end();                                      \
      }                                                                  \
   } while (0)

template <chip_class GFX_VERSION, si_has_ngg NGG>
ALWAYS_INLINE
static void si_emit_draw_packets(struct si_context *sctx, const struct pipe_draw_info *info,
                                 unsigned drawid_base,
                                 const struct pipe_draw_indirect_info *indirect,
                                 const struct pipe_draw_start_count_bias *draws,
                                 unsigned num_draws, unsigned total_count,
                                 struct pipe_resource *indexbuf, unsigned index_size,
                                 unsigned index_offset, unsigned instance_count,
                                 unsigned original_index_size)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   if (unlikely(sctx->thread_trace_enabled)) {
      si_sqtt_write_event_marker(sctx, &sctx->gfx_cs, sctx->sqtt_next_event,
                                 UINT_MAX, UINT_MAX, UINT_MAX);
   }

   uint32_t use_opaque = 0;

   if (indirect && indirect->count_from_stream_output) {
      struct si_streamout_target *t = (struct si_streamout_target *)indirect->count_from_stream_output;

      radeon_begin(cs);
      radeon_set_context_reg(cs, R_028B30_VGT_STRMOUT_DRAW_OPAQUE_VERTEX_STRIDE, t->stride_in_dw);
      radeon_end();

      si_cp_copy_data(sctx, &sctx->gfx_cs, COPY_DATA_REG, NULL,
                      R_028B2C_VGT_STRMOUT_DRAW_OPAQUE_BUFFER_FILLED_SIZE >> 2, COPY_DATA_SRC_MEM,
                      t->buf_filled_size, t->buf_filled_size_offset);
      use_opaque = S_0287F0_USE_OPAQUE(1);
      indirect = NULL;
   }

   uint32_t index_max_size = 0;
   uint64_t index_va = 0;

   radeon_begin(cs);

   /* draw packet */
   if (index_size) {
      /* Register shadowing doesn't shadow INDEX_TYPE. */
      if (index_size != sctx->last_index_size || sctx->shadowed_regs) {
         unsigned index_type;

         /* Index type computation. When we look at how we need to translate index_size,
          * we can see that we just need 2 shifts to get the hw value.
          *
          * 1 = 001b --> 10b = 2
          * 2 = 010b --> 00b = 0
          * 4 = 100b --> 01b = 1
          */
         index_type = ((index_size >> 2) | (index_size << 1)) & 0x3;

         if (GFX_VERSION <= GFX7 && SI_BIG_ENDIAN) {
            /* GFX7 doesn't support ubyte indices. */
            index_type |= index_size == 2 ? V_028A7C_VGT_DMA_SWAP_16_BIT
                                          : V_028A7C_VGT_DMA_SWAP_32_BIT;
         }

         if (GFX_VERSION >= GFX9) {
            radeon_set_uconfig_reg_idx(cs, sctx->screen, GFX_VERSION,
                                       R_03090C_VGT_INDEX_TYPE, 2, index_type);
         } else {
            radeon_emit(cs, PKT3(PKT3_INDEX_TYPE, 0, 0));
            radeon_emit(cs, index_type);
         }

         sctx->last_index_size = index_size;
      }

      index_max_size = (indexbuf->width0 - index_offset) >> util_logbase2(index_size);
      /* Skip draw calls with 0-sized index buffers.
       * They cause a hang on some chips, like Navi10-14.
       */
      if (!index_max_size) {
         radeon_end();
         return;
      }

      index_va = si_resource(indexbuf)->gpu_address + index_offset;

      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, si_resource(indexbuf), RADEON_USAGE_READ,
                                RADEON_PRIO_INDEX_BUFFER);
   } else {
      /* On GFX7 and later, non-indexed draws overwrite VGT_INDEX_TYPE,
       * so the state must be re-emitted before the next indexed draw.
       */
      if (GFX_VERSION >= GFX7)
         sctx->last_index_size = -1;
   }

   unsigned sh_base_reg = sctx->shader_pointers.sh_base[PIPE_SHADER_VERTEX];
   bool render_cond_bit = sctx->render_cond_enabled;

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
                            S_2C3_DRAW_INDEX_ENABLE(sctx->shader.vs.cso->info.uses_drawid) |
                            S_2C3_COUNT_INDIRECT_ENABLE(!!indirect->indirect_draw_count));
         radeon_emit(cs, indirect->draw_count);
         radeon_emit(cs, count_va);
         radeon_emit(cs, count_va >> 32);
         radeon_emit(cs, indirect->stride);
         radeon_emit(cs, di_src_sel);
      }
   } else {
      /* Register shadowing requires that we always emit PKT3_NUM_INSTANCES. */
      if (sctx->shadowed_regs ||
          sctx->last_instance_count == SI_INSTANCE_COUNT_UNKNOWN ||
          sctx->last_instance_count != instance_count) {
         radeon_emit(cs, PKT3(PKT3_NUM_INSTANCES, 0, 0));
         radeon_emit(cs, instance_count);
         sctx->last_instance_count = instance_count;
      }

      /* Base vertex and start instance. */
      int base_vertex = original_index_size ? draws[0].index_bias : draws[0].start;

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
                  (drawid_base != sctx->last_drawid ||
                   sctx->last_drawid == SI_DRAW_ID_UNKNOWN)) ||
                 sh_base_reg != sctx->last_sh_base_reg) {
         if (set_base_instance) {
            radeon_set_sh_reg_seq(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, 3);
            radeon_emit(cs, base_vertex);
            radeon_emit(cs, drawid_base);
            radeon_emit(cs, info->start_instance);

            sctx->last_start_instance = info->start_instance;
            sctx->last_drawid = drawid_base;
         } else if (set_draw_id) {
            radeon_set_sh_reg_seq(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, 2);
            radeon_emit(cs, base_vertex);
            radeon_emit(cs, drawid_base);

            sctx->last_drawid = drawid_base;
         } else {
            radeon_set_sh_reg(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, base_vertex);
         }

         sctx->last_base_vertex = base_vertex;
         sctx->last_sh_base_reg = sh_base_reg;
      }

      /* Don't update draw_id in the following code if it doesn't increment. */
      bool increment_draw_id = num_draws > 1 && set_draw_id && info->increment_draw_id;

      if (index_size) {
         /* NOT_EOP allows merging multiple draws into 1 wave, but only user VGPRs
          * can be changed between draws, and GS fast launch must be disabled.
          * NOT_EOP doesn't work on gfx9 and older.
          *
          * Instead of doing this, which evaluates the case conditions repeatedly:
          *  for (all draws) {
          *    if (case1);
          *    else;
          *  }
          *
          * Use this structuring to evaluate the case conditions once:
          *  if (case1) for (all draws);
          *  else for (all draws);
          *
          */
         bool index_bias_varies = num_draws > 1 && info->index_bias_varies;

         if (increment_draw_id) {
            if (index_bias_varies) {
               for (unsigned i = 0; i < num_draws; i++) {
                  uint64_t va = index_va + draws[i].start * index_size;

                  if (i > 0) {
                     radeon_set_sh_reg_seq(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, 2);
                     radeon_emit(cs, draws[i].index_bias);
                     radeon_emit(cs, drawid_base + i);
                  }

                  radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_2, 4, render_cond_bit));
                  radeon_emit(cs, index_max_size);
                  radeon_emit(cs, va);
                  radeon_emit(cs, va >> 32);
                  radeon_emit(cs, draws[i].count);
                  radeon_emit(cs, V_0287F0_DI_SRC_SEL_DMA); /* NOT_EOP disabled */
               }
               if (num_draws > 1) {
                  sctx->last_base_vertex = draws[num_draws - 1].index_bias;
                  sctx->last_drawid = drawid_base + num_draws - 1;
               }
            } else {
               /* Only DrawID varies. */
               for (unsigned i = 0; i < num_draws; i++) {
                  uint64_t va = index_va + draws[i].start * index_size;

                  if (i > 0)
                     radeon_set_sh_reg(cs, sh_base_reg + SI_SGPR_DRAWID * 4, drawid_base + i);

                  radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_2, 4, render_cond_bit));
                  radeon_emit(cs, index_max_size);
                  radeon_emit(cs, va);
                  radeon_emit(cs, va >> 32);
                  radeon_emit(cs, draws[i].count);
                  radeon_emit(cs, V_0287F0_DI_SRC_SEL_DMA); /* NOT_EOP disabled */
               }
               if (num_draws > 1)
                  sctx->last_drawid = drawid_base + num_draws - 1;
            }
         } else {
            if (index_bias_varies) {
               /* Only BaseVertex varies. */
               for (unsigned i = 0; i < num_draws; i++) {
                  uint64_t va = index_va + draws[i].start * index_size;

                  if (i > 0)
                     radeon_set_sh_reg(cs, sh_base_reg + SI_SGPR_BASE_VERTEX * 4, draws[i].index_bias);

                  radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_2, 4, render_cond_bit));
                  radeon_emit(cs, index_max_size);
                  radeon_emit(cs, va);
                  radeon_emit(cs, va >> 32);
                  radeon_emit(cs, draws[i].count);
                  radeon_emit(cs, V_0287F0_DI_SRC_SEL_DMA); /* NOT_EOP disabled */
               }
               if (num_draws > 1)
                  sctx->last_base_vertex = draws[num_draws - 1].index_bias;
            } else {
               /* DrawID and BaseVertex are constant. */
               if (GFX_VERSION == GFX10) {
                  /* GFX10 has a bug that consecutive draw packets with NOT_EOP must not have
                   * count == 0 in the last draw (which doesn't set NOT_EOP).
                   *
                   * So remove all trailing draws with count == 0.
                   */
                  while (num_draws > 1 && !draws[num_draws - 1].count)
                     num_draws--;
               }

               for (unsigned i = 0; i < num_draws; i++) {
                  uint64_t va = index_va + draws[i].start * index_size;

                  radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_2, 4, render_cond_bit));
                  radeon_emit(cs, index_max_size);
                  radeon_emit(cs, va);
                  radeon_emit(cs, va >> 32);
                  radeon_emit(cs, draws[i].count);
                  radeon_emit(cs, V_0287F0_DI_SRC_SEL_DMA |
                              S_0287F0_NOT_EOP(GFX_VERSION >= GFX10 && i < num_draws - 1));
               }
            }
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
                  if (increment_draw_id) {
                     unsigned draw_id = drawid_base + i;

                     radeon_set_sh_reg(cs, sh_base_reg + SI_SGPR_DRAWID * 4, draw_id);
                     sctx->last_drawid = draw_id;
                  }
               }

               /* TODO: Do index buffer bounds checking? We don't do it in this case. */
               radeon_emit(cs, PKT3(PKT3_DRAW_INDEX_AUTO, 1, render_cond_bit));
               radeon_emit(cs, draws[i].count);
               radeon_emit(cs, V_0287F0_DI_SRC_SEL_AUTO_INDEX);
            }
            radeon_end();

            EMIT_SQTT_END_DRAW;
            return;
         }

         for (unsigned i = 0; i < num_draws; i++) {
            if (i > 0) {
               if (increment_draw_id) {
                  unsigned draw_id = drawid_base + i;

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
   radeon_end();

   EMIT_SQTT_END_DRAW;
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG> ALWAYS_INLINE
static bool si_upload_and_prefetch_VB_descriptors(struct si_context *sctx)
{
   unsigned count = sctx->num_vertex_elements;
   unsigned num_vbos_in_user_sgprs = si_num_vbos_in_user_sgprs_inline(GFX_VERSION);
   bool pointer_dirty, user_sgprs_dirty;

   assert(count <= SI_MAX_ATTRIBS);

   if (sctx->vertex_buffers_dirty) {
      assert(count);

      struct si_vertex_elements *velems = sctx->vertex_elements;
      unsigned alloc_size = velems->vb_desc_list_alloc_size;
      uint32_t *ptr;

      if (alloc_size) {
         /* Vertex buffer descriptors are the only ones which are uploaded directly
          * and don't go through si_upload_graphics_shader_descriptors.
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
         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, sctx->vb_descriptors_buffer,
                                   RADEON_USAGE_READ, RADEON_PRIO_DESCRIPTORS);
         /* GFX6 doesn't support the L2 prefetch. */
         if (GFX_VERSION >= GFX7)
            si_cp_dma_prefetch(sctx, &sctx->vb_descriptors_buffer->b.b, sctx->vb_descriptors_offset,
                               alloc_size);
      } else {
         si_resource_reference(&sctx->vb_descriptors_buffer, NULL);
      }

      unsigned first_vb_use_mask = velems->first_vb_use_mask;

      for (unsigned i = 0; i < count; i++) {
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

      sctx->vertex_buffers_dirty = false;

      pointer_dirty = alloc_size != 0;
      user_sgprs_dirty = num_vbos_in_user_sgprs > 0;
   } else {
      pointer_dirty = sctx->vertex_buffer_pointer_dirty;
      user_sgprs_dirty = sctx->vertex_buffer_user_sgprs_dirty;
   }

   if (pointer_dirty || user_sgprs_dirty) {
      struct radeon_cmdbuf *cs = &sctx->gfx_cs;
      unsigned sh_base = si_get_user_data_base(GFX_VERSION, HAS_TESS, HAS_GS, NGG,
                                               PIPE_SHADER_VERTEX);
      assert(count);

      radeon_begin(cs);

      /* Set the pointer to vertex buffer descriptors. */
      if (pointer_dirty && count > num_vbos_in_user_sgprs) {
         /* Find the location of the VB descriptor pointer. */
         unsigned sh_dw_offset = SI_VS_NUM_USER_SGPR;
         if (GFX_VERSION >= GFX9) {
            if (HAS_TESS)
               sh_dw_offset = GFX9_TCS_NUM_USER_SGPR;
            else if (HAS_GS)
               sh_dw_offset = GFX9_VSGS_NUM_USER_SGPR;
         }

         radeon_set_sh_reg(cs, sh_base + sh_dw_offset * 4,
                           sctx->vb_descriptors_buffer->gpu_address +
                           sctx->vb_descriptors_offset);
         sctx->vertex_buffer_pointer_dirty = false;
      }

      /* Set VB descriptors in user SGPRs. */
      if (user_sgprs_dirty) {
         assert(num_vbos_in_user_sgprs);

         unsigned num_sgprs = MIN2(count, num_vbos_in_user_sgprs) * 4;

         radeon_set_sh_reg_seq(cs, sh_base + SI_SGPR_VS_VB_DESCRIPTOR_FIRST * 4, num_sgprs);
         radeon_emit_array(cs, sctx->vb_descriptor_user_sgprs, num_sgprs);
         sctx->vertex_buffer_user_sgprs_dirty = false;
      }
      radeon_end();
   }

   return true;
}

static void si_get_draw_start_count(struct si_context *sctx, const struct pipe_draw_info *info,
                                    const struct pipe_draw_indirect_info *indirect,
                                    const struct pipe_draw_start_count_bias *draws,
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
ALWAYS_INLINE
static void si_emit_all_states(struct si_context *sctx, const struct pipe_draw_info *info,
                               const struct pipe_draw_indirect_info *indirect,
                               enum pipe_prim_type prim, unsigned instance_count,
                               unsigned min_vertex_count, bool primitive_restart,
                               unsigned skip_atom_mask)
{
   unsigned num_patches = 0;

   si_emit_rasterizer_prim_state<GFX_VERSION, HAS_TESS, HAS_GS, NGG>(sctx);
   if (HAS_TESS)
      si_emit_derived_tess_state(sctx, &num_patches);

   /* Emit state atoms. */
   unsigned mask = sctx->dirty_atoms & ~skip_atom_mask;
   if (mask) {
      do {
         sctx->atoms.array[u_bit_scan(&mask)].emit(sctx);
      } while (mask);

      sctx->dirty_atoms &= skip_atom_mask;
   }

   /* Emit states. */
   mask = sctx->dirty_states;
   if (mask) {
      do {
         unsigned i = u_bit_scan(&mask);
         struct si_pm4_state *state = sctx->queued.array[i];

         /* All places should unset dirty_states if this doesn't pass. */
         assert(state && state != sctx->emitted.array[i]);

         si_pm4_emit(sctx, state);
         sctx->emitted.array[i] = state;
      } while (mask);

      sctx->dirty_states = 0;
   }

   /* Emit draw states. */
   si_emit_vs_state<GFX_VERSION, HAS_TESS, HAS_GS, NGG>(sctx, info->index_size);
   si_emit_draw_registers<GFX_VERSION, HAS_TESS, HAS_GS, NGG>
         (sctx, indirect, prim, num_patches, instance_count, primitive_restart,
          info->restart_index, min_vertex_count);
}

#define DRAW_CLEANUP do {                                 \
      if (index_size && indexbuf != info->index.resource) \
         pipe_resource_reference(&indexbuf, NULL);        \
   } while (0)

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG>
static void si_draw_vbo(struct pipe_context *ctx,
                        const struct pipe_draw_info *info,
                        unsigned drawid_offset,
                        const struct pipe_draw_indirect_info *indirect,
                        const struct pipe_draw_start_count_bias *draws,
                        unsigned num_draws)
{
   /* Keep code that uses the least number of local variables as close to the beginning
    * of this function as possible to minimize register pressure.
    *
    * It doesn't matter where we return due to invalid parameters because such cases
    * shouldn't occur in practice.
    */
   struct si_context *sctx = (struct si_context *)ctx;

   /* Recompute and re-emit the texture resource states if needed. */
   unsigned dirty_tex_counter = p_atomic_read(&sctx->screen->dirty_tex_counter);
   if (unlikely(dirty_tex_counter != sctx->last_dirty_tex_counter)) {
      sctx->last_dirty_tex_counter = dirty_tex_counter;
      sctx->framebuffer.dirty_cbufs |= ((1 << sctx->framebuffer.state.nr_cbufs) - 1);
      sctx->framebuffer.dirty_zsbuf = true;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.framebuffer);
      si_update_all_texture_descriptors(sctx);
   }

   unsigned dirty_buf_counter = p_atomic_read(&sctx->screen->dirty_buf_counter);
   if (unlikely(dirty_buf_counter != sctx->last_dirty_buf_counter)) {
      sctx->last_dirty_buf_counter = dirty_buf_counter;
      /* Rebind all buffers unconditionally. */
      si_rebind_buffer(sctx, NULL);
   }

   si_decompress_textures(sctx, u_bit_consecutive(0, SI_NUM_GRAPHICS_SHADERS));
   si_need_gfx_cs_space(sctx, num_draws);

   if (HAS_TESS) {
      struct si_shader_selector *tcs = sctx->shader.tcs.cso;

      /* The rarely occuring tcs == NULL case is not optimized. */
      bool same_patch_vertices =
         GFX_VERSION >= GFX9 &&
         tcs && sctx->patch_vertices == tcs->info.base.tess.tcs_vertices_out;

      if (sctx->shader.tcs.key.opt.same_patch_vertices != same_patch_vertices) {
         sctx->shader.tcs.key.opt.same_patch_vertices = same_patch_vertices;
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
            tcs && sctx->patch_vertices > tcs->info.base.tess.tcs_vertices_out;

         if (ls_vgpr_fix != sctx->shader.tcs.key.part.tcs.ls_prolog.ls_vgpr_fix) {
            sctx->shader.tcs.key.part.tcs.ls_prolog.ls_vgpr_fix = ls_vgpr_fix;
            sctx->fixed_func_tcs_shader.key.part.tcs.ls_prolog.ls_vgpr_fix = ls_vgpr_fix;
            sctx->do_update_shaders = true;
         }
      }
   }

   enum pipe_prim_type prim = (enum pipe_prim_type)info->mode;
   unsigned instance_count = info->instance_count;

   /* GFX6-GFX7 treat instance_count==0 as instance_count==1. There is
    * no workaround for indirect draws, but we can at least skip
    * direct draws.
    * 'instance_count == 0' seems to be problematic on Renoir chips (#4866),
    * so simplify the condition and drop these draws for all <= GFX9 chips.
    */
   if (GFX_VERSION <= GFX9 && unlikely(!indirect && !instance_count))
      return;

   struct si_shader_selector *vs = sctx->shader.vs.cso;
   if (unlikely(!vs || sctx->num_vertex_elements < vs->num_vs_inputs ||
                !sctx->shader.ps.cso || (HAS_TESS != (prim == PIPE_PRIM_PATCHES)))) {
      assert(0);
      return;
   }

   if (GFX_VERSION <= GFX9 && HAS_GS) {
      /* Determine whether the GS triangle strip adjacency fix should
       * be applied. Rotate every other triangle if triangle strips with
       * adjacency are fed to the GS. This doesn't work if primitive
       * restart occurs after an odd number of triangles.
       */
      bool gs_tri_strip_adj_fix =
         !HAS_TESS && prim == PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY;

      if (gs_tri_strip_adj_fix != sctx->shader.gs.key.part.gs.prolog.tri_strip_adj_fix) {
         sctx->shader.gs.key.part.gs.prolog.tri_strip_adj_fix = gs_tri_strip_adj_fix;
         sctx->do_update_shaders = true;
      }
   }

   struct pipe_resource *indexbuf = info->index.resource;
   unsigned index_size = info->index_size;
   unsigned index_offset = indirect && indirect->buffer ? draws[0].start * index_size : 0;

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
      total_direct_count = min_direct_count = draws[0].count;

      for (unsigned i = 1; i < num_draws; i++) {
         unsigned count = draws[i].count;

         total_direct_count += count;
         min_direct_count = MIN2(min_direct_count, count);
      }
   }

   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
   bool primitive_restart =
      info->primitive_restart &&
      (!sctx->screen->options.prim_restart_tri_strips_only ||
       (prim != PIPE_PRIM_TRIANGLE_STRIP && prim != PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY));
   unsigned original_index_size = index_size;

   /* Set the rasterization primitive type.
    *
    * This must be done after si_decompress_textures, which can call
    * draw_vbo recursively, and before si_update_shaders, which uses
    * current_rast_prim for this draw_vbo call.
    */
   if (!HAS_GS && !HAS_TESS) {
      enum pipe_prim_type rast_prim;

      if (util_rast_prim_is_triangles(prim)) {
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
   }

   /* Update NGG culling settings. */
   uint8_t old_ngg_culling = sctx->ngg_culling;
   if (GFX_VERSION >= GFX10) {
      struct si_shader_selector *hw_vs = si_get_vs_inline(sctx, HAS_TESS, HAS_GS)->cso;

      if (NGG && !HAS_GS &&
          /* Tessellation sets ngg_cull_vert_threshold to UINT_MAX if the prim type
           * is not triangles, so this check is only needed without tessellation. */
          (HAS_TESS || sctx->current_rast_prim == PIPE_PRIM_TRIANGLES) &&
          total_direct_count > hw_vs->ngg_cull_vert_threshold) {
         uint8_t ngg_culling = sctx->viewport0_y_inverted ? rs->ngg_cull_flags_y_inverted :
                                                            rs->ngg_cull_flags;

         /* Use NGG fast launch for certain primitive types.
          * A draw must have at least 1 full primitive.
          * The fast launch doesn't work with tessellation.
          *
          * Since NGG fast launch is enabled by VGT_SHADER_STAGES_EN, which causes a context roll,
          * which decreases performance, decrease the frequency of switching it on/off using
          * a high vertex count threshold.
          */
         if (!HAS_TESS && ngg_culling && total_direct_count >= 8000 &&
             !(sctx->screen->debug_flags & DBG(NO_FAST_LAUNCH))) {
            if (prim == PIPE_PRIM_TRIANGLES && !index_size) {
               ngg_culling |= SI_NGG_CULL_GS_FAST_LAUNCH_TRI_LIST;
            } else if (prim == PIPE_PRIM_TRIANGLE_STRIP) {
               if (!index_size) {
                  ngg_culling |= SI_NGG_CULL_GS_FAST_LAUNCH_TRI_STRIP;
               } else if (!primitive_restart) {
                  ngg_culling |= SI_NGG_CULL_GS_FAST_LAUNCH_TRI_STRIP |
                                 SI_NGG_CULL_GS_FAST_LAUNCH_INDEX_SIZE_PACKED(MIN2(index_size, 3));
                  /* The index buffer will be emulated. */
                  index_size = 0;
               }
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

   if (unlikely(sctx->do_update_shaders)) {
      if (unlikely(!(si_update_shaders<GFX_VERSION, HAS_TESS, HAS_GS, NGG>(sctx)))) {
         DRAW_CLEANUP;
         return;
      }

      /* Insert a VGT_FLUSH when enabling fast launch changes to prevent hangs.
       * See issues #2418, #2426, #2434
       *
       * This is the setting that is used by the draw.
       */
      if (GFX_VERSION >= GFX10) {
         uint8_t ngg_culling = si_get_vs_inline(sctx, HAS_TESS, HAS_GS)->current->key.opt.ngg_culling;
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

   /* Since we've called si_context_add_resource_size for vertex buffers,
    * this must be called after si_need_cs_space, because we must let
    * need_cs_space flush before we add buffers to the buffer list.
    *
    * This must be done after si_update_shaders because si_update_shaders can
    * flush the CS when enabling tess and GS rings.
    */
   if (sctx->bo_list_add_all_gfx_resources)
      si_gfx_resources_add_all_to_bo_list(sctx);

   /* Graphics shader descriptors must be uploaded after si_update_shaders because
    * it binds tess and GS ring buffers.
    */
   if (unlikely(!si_upload_graphics_shader_descriptors(sctx))) {
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
                               SI_CONTEXT_PS_PARTIAL_FLUSH | SI_CONTEXT_CS_PARTIAL_FLUSH |
                               SI_CONTEXT_VS_PARTIAL_FLUSH))) {
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
      sctx->emit_cache_flush(sctx, &sctx->gfx_cs);
      /* <-- CUs are idle here. */

      /* This uploads VBO descriptors, sets user SGPRs, and executes the L2 prefetch.
       * It should done after cache flushing.
       */
      if (unlikely((!si_upload_and_prefetch_VB_descriptors<GFX_VERSION, HAS_TESS, HAS_GS, NGG>(sctx)))) {
         DRAW_CLEANUP;
         return;
      }

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

      si_emit_draw_packets<GFX_VERSION, NGG>
            (sctx, info, drawid_offset, indirect, draws, num_draws, total_direct_count, indexbuf,
             index_size, index_offset, instance_count, original_index_size);
      /* <-- CUs are busy here. */

      /* Start prefetches after the draw has been started. Both will run
       * in parallel, but starting the draw first is more important.
       */
      si_prefetch_shaders<GFX_VERSION, HAS_TESS, HAS_GS, NGG, PREFETCH_ALL>(sctx);
   } else {
      /* If we don't wait for idle, start prefetches first, then set
       * states, and draw at the end.
       */
      if (sctx->flags)
         sctx->emit_cache_flush(sctx, &sctx->gfx_cs);

      /* Only prefetch the API VS and VBO descriptors. */
      si_prefetch_shaders<GFX_VERSION, HAS_TESS, HAS_GS, NGG, PREFETCH_BEFORE_DRAW>(sctx);

      /* This uploads VBO descriptors, sets user SGPRs, and executes the L2 prefetch.
       * It should done after cache flushing and after the VS prefetch.
       */
      if (unlikely((!si_upload_and_prefetch_VB_descriptors<GFX_VERSION, HAS_TESS, HAS_GS, NGG>(sctx)))) {
         DRAW_CLEANUP;
         return;
      }

      si_emit_all_states<GFX_VERSION, HAS_TESS, HAS_GS, NGG>
            (sctx, info, indirect, prim, instance_count, min_direct_count,
             primitive_restart, masked_atoms);

      if (GFX_VERSION == GFX9 && gfx9_scissor_bug &&
          (sctx->context_roll || si_is_atom_dirty(sctx, &sctx->atoms.s.scissors))) {
         sctx->atoms.s.scissors.emit(sctx);
         sctx->dirty_atoms &= ~si_get_atom_bit(sctx, &sctx->atoms.s.scissors);
      }
      assert(sctx->dirty_atoms == 0);

      si_emit_draw_packets<GFX_VERSION, NGG>
            (sctx, info, drawid_offset, indirect, draws, num_draws, total_direct_count, indexbuf,
             index_size, index_offset, instance_count, original_index_size);

      /* Prefetch the remaining shaders after the draw has been
       * started. */
      si_prefetch_shaders<GFX_VERSION, HAS_TESS, HAS_GS, NGG, PREFETCH_AFTER_DRAW>(sctx);
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
   if (((GFX_VERSION == GFX7 && sctx->family == CHIP_HAWAII) ||
        (GFX_VERSION == GFX8 && (sctx->family == CHIP_TONGA || sctx->family == CHIP_FIJI))) &&
       si_get_strmout_en(sctx)) {
      sctx->flags |= SI_CONTEXT_VGT_STREAMOUT_SYNC;
   }

   if (unlikely(sctx->decompression_enabled)) {
      sctx->num_decompress_calls++;
   } else {
      sctx->num_draw_calls++;
      if (primitive_restart)
         sctx->num_prim_restart_calls++;
   }

   if (!sctx->blitter_running && sctx->framebuffer.state.zsbuf) {
      struct si_texture *zstex = (struct si_texture *)sctx->framebuffer.state.zsbuf->texture;
      zstex->depth_cleared_level_mask &= ~BITFIELD_BIT(sctx->framebuffer.state.zsbuf->u.tex.level);
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
   struct pipe_draw_start_count_bias draw;

   info.mode = SI_PRIM_RECTANGLE_LIST;
   info.instance_count = num_instances;

   draw.start = 0;
   draw.count = 3;

   /* Don't set per-stage shader pointers for VS. */
   sctx->shader_pointers_dirty &= ~SI_DESCS_SHADER_MASK(VERTEX);
   sctx->vertex_buffer_pointer_dirty = false;
   sctx->vertex_buffer_user_sgprs_dirty = false;

   pipe->draw_vbo(pipe, &info, 0, NULL, &draw, 1);
}

template <chip_class GFX_VERSION, si_has_tess HAS_TESS, si_has_gs HAS_GS, si_has_ngg NGG>
static void si_init_draw_vbo(struct si_context *sctx)
{
   if (NGG && GFX_VERSION < GFX10)
      return;

   sctx->draw_vbo[HAS_TESS][HAS_GS][NGG] =
      si_draw_vbo<GFX_VERSION, HAS_TESS, HAS_GS, NGG>;
}

template <chip_class GFX_VERSION>
static void si_init_draw_vbo_all_pipeline_options(struct si_context *sctx)
{
   si_init_draw_vbo<GFX_VERSION, TESS_OFF, GS_OFF, NGG_OFF>(sctx);
   si_init_draw_vbo<GFX_VERSION, TESS_OFF, GS_ON,  NGG_OFF>(sctx);
   si_init_draw_vbo<GFX_VERSION, TESS_ON,  GS_OFF, NGG_OFF>(sctx);
   si_init_draw_vbo<GFX_VERSION, TESS_ON,  GS_ON,  NGG_OFF>(sctx);
   si_init_draw_vbo<GFX_VERSION, TESS_OFF, GS_OFF, NGG_ON>(sctx);
   si_init_draw_vbo<GFX_VERSION, TESS_OFF, GS_ON,  NGG_ON>(sctx);
   si_init_draw_vbo<GFX_VERSION, TESS_ON,  GS_OFF, NGG_ON>(sctx);
   si_init_draw_vbo<GFX_VERSION, TESS_ON,  GS_ON,  NGG_ON>(sctx);
}

static void si_invalid_draw_vbo(struct pipe_context *pipe,
                                const struct pipe_draw_info *info,
                                unsigned drawid_offset,
                                const struct pipe_draw_indirect_info *indirect,
                                const struct pipe_draw_start_count_bias *draws,
                                unsigned num_draws)
{
   unreachable("vertex shader not bound");
}

extern "C"
void GFX(si_init_draw_functions_)(struct si_context *sctx)
{
   assert(sctx->chip_class == GFX());

   si_init_draw_vbo_all_pipeline_options<GFX()>(sctx);

   /* Bind a fake draw_vbo, so that draw_vbo isn't NULL, which would skip
    * initialization of callbacks in upper layers (such as u_threaded_context).
    */
   sctx->b.draw_vbo = si_invalid_draw_vbo;
   sctx->blitter->draw_rectangle = si_draw_rectangle;

   si_init_ia_multi_vgt_param_table(sctx);
}

#if GFX_VER == 6 /* declare this function only once because it supports all chips. */

extern "C"
void si_init_spi_map_functions(struct si_context *sctx)
{
   /* This unrolls the loops in si_emit_spi_map and inlines memcmp and memcpys.
    * It improves performance for viewperf/snx.
    */
   sctx->emit_spi_map[0] = si_emit_spi_map<0>;
   sctx->emit_spi_map[1] = si_emit_spi_map<1>;
   sctx->emit_spi_map[2] = si_emit_spi_map<2>;
   sctx->emit_spi_map[3] = si_emit_spi_map<3>;
   sctx->emit_spi_map[4] = si_emit_spi_map<4>;
   sctx->emit_spi_map[5] = si_emit_spi_map<5>;
   sctx->emit_spi_map[6] = si_emit_spi_map<6>;
   sctx->emit_spi_map[7] = si_emit_spi_map<7>;
   sctx->emit_spi_map[8] = si_emit_spi_map<8>;
   sctx->emit_spi_map[9] = si_emit_spi_map<9>;
   sctx->emit_spi_map[10] = si_emit_spi_map<10>;
   sctx->emit_spi_map[11] = si_emit_spi_map<11>;
   sctx->emit_spi_map[12] = si_emit_spi_map<12>;
   sctx->emit_spi_map[13] = si_emit_spi_map<13>;
   sctx->emit_spi_map[14] = si_emit_spi_map<14>;
   sctx->emit_spi_map[15] = si_emit_spi_map<15>;
   sctx->emit_spi_map[16] = si_emit_spi_map<16>;
   sctx->emit_spi_map[17] = si_emit_spi_map<17>;
   sctx->emit_spi_map[18] = si_emit_spi_map<18>;
   sctx->emit_spi_map[19] = si_emit_spi_map<19>;
   sctx->emit_spi_map[20] = si_emit_spi_map<20>;
   sctx->emit_spi_map[21] = si_emit_spi_map<21>;
   sctx->emit_spi_map[22] = si_emit_spi_map<22>;
   sctx->emit_spi_map[23] = si_emit_spi_map<23>;
   sctx->emit_spi_map[24] = si_emit_spi_map<24>;
   sctx->emit_spi_map[25] = si_emit_spi_map<25>;
   sctx->emit_spi_map[26] = si_emit_spi_map<26>;
   sctx->emit_spi_map[27] = si_emit_spi_map<27>;
   sctx->emit_spi_map[28] = si_emit_spi_map<28>;
   sctx->emit_spi_map[29] = si_emit_spi_map<29>;
   sctx->emit_spi_map[30] = si_emit_spi_map<30>;
   sctx->emit_spi_map[31] = si_emit_spi_map<31>;
   sctx->emit_spi_map[32] = si_emit_spi_map<32>;
}

#endif
