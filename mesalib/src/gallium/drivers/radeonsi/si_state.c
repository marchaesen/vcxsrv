/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_build_pm4.h"
#include "si_query.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "util/fast_idiv_by_const.h"
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/hash_table.h"
#include "util/u_dual_blend.h"
#include "util/u_helpers.h"
#include "util/u_memory.h"
#include "util/u_resource.h"
#include "util/u_upload_mgr.h"
#include "util/u_blend.h"

#include "ac_cmdbuf.h"
#include "ac_descriptors.h"
#include "ac_formats.h"
#include "gfx10_format_table.h"

/* 12.4 fixed-point */
static unsigned si_pack_float_12p4(float x)
{
   return x <= 0 ? 0 : x >= 4096 ? 0xffff : x * 16;
}

/*
 * Inferred framebuffer and blender state.
 *
 * CB_TARGET_MASK is emitted here to avoid a hang with dual source blending
 * if there is not enough PS outputs.
 */
static void si_emit_cb_render_state(struct si_context *sctx, unsigned index)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct si_state_blend *blend = sctx->queued.named.blend;
   /* CB_COLORn_INFO.FORMAT=INVALID should disable unbound colorbuffers,
    * but you never know. */
   uint32_t cb_target_mask = sctx->framebuffer.colorbuf_enabled_4bit & blend->cb_target_mask;
   unsigned i;

   /* Avoid a hang that happens when dual source blending is enabled
    * but there is not enough color outputs. This is undefined behavior,
    * so disable color writes completely.
    *
    * Reproducible with Unigine Heaven 4.0 and drirc missing.
    */
   if (blend->dual_src_blend && sctx->shader.ps.cso &&
       (sctx->shader.ps.cso->info.colors_written & 0x3) != 0x3)
      cb_target_mask = 0;

   /* GFX9: Flush DFSM when CB_TARGET_MASK changes.
    * I think we don't have to do anything between IBs.
    */
   if (sctx->screen->dpbb_allowed && sctx->last_cb_target_mask != cb_target_mask &&
       sctx->screen->pbb_context_states_per_bin > 1) {
      sctx->last_cb_target_mask = cb_target_mask;

      radeon_begin(cs);
      radeon_event_write(V_028A90_BREAK_BATCH);
      radeon_end();
   }

   uint32_t cb_dcc_control = 0;

   if (sctx->gfx_level >= GFX8 && sctx->gfx_level < GFX12) {
      /* DCC MSAA workaround.
       * Alternatively, we can set CB_COLORi_DCC_CONTROL.OVERWRITE_-
       * COMBINER_DISABLE, but that would be more complicated.
       */
      bool oc_disable =
         blend->dcc_msaa_corruption_4bit & cb_target_mask && sctx->framebuffer.nr_samples >= 2;

      if (sctx->gfx_level >= GFX11) {
         cb_dcc_control =
            S_028424_SAMPLE_MASK_TRACKER_DISABLE(oc_disable) |
            S_028424_SAMPLE_MASK_TRACKER_WATERMARK(sctx->screen->info.has_dedicated_vram ? 0 : 15);
      } else {
         cb_dcc_control =
            S_028424_OVERWRITE_COMBINER_MRT_SHARING_DISABLE(sctx->gfx_level <= GFX9) |
            S_028424_OVERWRITE_COMBINER_WATERMARK(sctx->gfx_level >= GFX10 ? 6 : 4) |
            S_028424_OVERWRITE_COMBINER_DISABLE(oc_disable) |
            S_028424_DISABLE_CONSTANT_ENCODE_REG(sctx->gfx_level < GFX11 &&
                                                 sctx->screen->info.has_dcc_constant_encode);
      }
   }

   uint32_t sx_ps_downconvert = 0;
   uint32_t sx_blend_opt_epsilon = 0;
   uint32_t sx_blend_opt_control = 0;

   /* RB+ register settings. */
   if (sctx->screen->info.rbplus_allowed) {
      unsigned spi_shader_col_format =
         sctx->shader.ps.cso ? sctx->shader.ps.current->key.ps.part.epilog.spi_shader_col_format
                             : 0;
      unsigned num_cbufs = util_last_bit(sctx->framebuffer.colorbuf_enabled_4bit &
                                         blend->cb_target_enabled_4bit) / 4;

      for (i = 0; i < num_cbufs; i++) {
         struct si_surface *surf = (struct si_surface *)sctx->framebuffer.state.cbufs[i];
         unsigned format, swap, spi_format, colormask;
         bool has_alpha, has_rgb;

         if (!surf) {
            /* If the color buffer is not set, the driver sets 32_R
             * as the SPI color format, because the hw doesn't allow
             * holes between color outputs, so also set this to
             * enable RB+.
             */
            sx_ps_downconvert |= V_028754_SX_RT_EXPORT_32_R << (i * 4);
            continue;
         }

         format = sctx->gfx_level >= GFX11 ? G_028C70_FORMAT_GFX11(surf->cb.cb_color_info):
                                             G_028C70_FORMAT_GFX6(surf->cb.cb_color_info);
         swap = G_028C70_COMP_SWAP(surf->cb.cb_color_info);
         spi_format = (spi_shader_col_format >> (i * 4)) & 0xf;
         colormask = (cb_target_mask >> (i * 4)) & 0xf;

         /* Set if RGB and A are present. */
         has_alpha = !(sctx->gfx_level >= GFX11 ? G_028C74_FORCE_DST_ALPHA_1_GFX11(surf->cb.cb_color_attrib):
                                                  G_028C74_FORCE_DST_ALPHA_1_GFX6(surf->cb.cb_color_attrib));

         if (format == V_028C70_COLOR_8 || format == V_028C70_COLOR_16 ||
             format == V_028C70_COLOR_32)
            has_rgb = !has_alpha;
         else
            has_rgb = true;

         /* Check the colormask and export format. */
         if (!(colormask & (PIPE_MASK_RGBA & ~PIPE_MASK_A)))
            has_rgb = false;
         if (!(colormask & PIPE_MASK_A))
            has_alpha = false;

         if (spi_format == V_028714_SPI_SHADER_ZERO) {
            has_rgb = false;
            has_alpha = false;
         }

         /* Disable value checking for disabled channels. */
         if (!has_rgb)
            sx_blend_opt_control |= S_02875C_MRT0_COLOR_OPT_DISABLE(1) << (i * 4);
         if (!has_alpha)
            sx_blend_opt_control |= S_02875C_MRT0_ALPHA_OPT_DISABLE(1) << (i * 4);

         /* Enable down-conversion for 32bpp and smaller formats. */
         switch (format) {
         case V_028C70_COLOR_8:
         case V_028C70_COLOR_8_8:
         case V_028C70_COLOR_8_8_8_8:
            /* For 1 and 2-channel formats, use the superset thereof. */
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR ||
                spi_format == V_028714_SPI_SHADER_UINT16_ABGR ||
                spi_format == V_028714_SPI_SHADER_SINT16_ABGR) {
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_8_8_8_8 << (i * 4);
               if (G_028C70_NUMBER_TYPE(surf->cb.cb_color_info) != V_028C70_NUMBER_SRGB)
                  sx_blend_opt_epsilon |= V_028758_8BIT_FORMAT_0_5 << (i * 4);
            }
            break;

         case V_028C70_COLOR_5_6_5:
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_5_6_5 << (i * 4);
               sx_blend_opt_epsilon |= V_028758_6BIT_FORMAT_0_5 << (i * 4);
            }
            break;

         case V_028C70_COLOR_1_5_5_5:
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_1_5_5_5 << (i * 4);
               sx_blend_opt_epsilon |= V_028758_5BIT_FORMAT_0_5 << (i * 4);
            }
            break;

         case V_028C70_COLOR_4_4_4_4:
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_4_4_4_4 << (i * 4);
               sx_blend_opt_epsilon |= V_028758_4BIT_FORMAT_0_5 << (i * 4);
            }
            break;

         case V_028C70_COLOR_32:
            if (swap == V_028C70_SWAP_STD && spi_format == V_028714_SPI_SHADER_32_R)
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_32_R << (i * 4);
            else if (swap == V_028C70_SWAP_ALT_REV && spi_format == V_028714_SPI_SHADER_32_AR)
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_32_A << (i * 4);
            break;

         case V_028C70_COLOR_16:
         case V_028C70_COLOR_16_16:
            /* For 1-channel formats, use the superset thereof. */
            if (spi_format == V_028714_SPI_SHADER_UNORM16_ABGR ||
                spi_format == V_028714_SPI_SHADER_SNORM16_ABGR ||
                spi_format == V_028714_SPI_SHADER_UINT16_ABGR ||
                spi_format == V_028714_SPI_SHADER_SINT16_ABGR) {
               if (swap == V_028C70_SWAP_STD || swap == V_028C70_SWAP_STD_REV)
                  sx_ps_downconvert |= V_028754_SX_RT_EXPORT_16_16_GR << (i * 4);
               else
                  sx_ps_downconvert |= V_028754_SX_RT_EXPORT_16_16_AR << (i * 4);
            }
            break;

         case V_028C70_COLOR_10_11_11:
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR)
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_10_11_11 << (i * 4);
            break;

         case V_028C70_COLOR_2_10_10_10:
         case V_028C70_COLOR_10_10_10_2:
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_2_10_10_10 << (i * 4);
               sx_blend_opt_epsilon |= V_028758_10BIT_FORMAT_0_5 << (i * 4);
            }
            break;

         case V_028C70_COLOR_5_9_9_9:
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR)
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_9_9_9_E5 << (i * 4);
            break;
         }
      }

      /* If there are no color outputs, the first color export is
       * always enabled as 32_R, so also set this to enable RB+.
       */
      if (!sx_ps_downconvert)
         sx_ps_downconvert = V_028754_SX_RT_EXPORT_32_R;
   }

   if (sctx->gfx_level >= GFX12) {
      /* GFX12 doesn't have CB_FDCC_CONTROL. */
      assert(cb_dcc_control == 0);

      radeon_begin(cs);
      gfx12_begin_context_regs();
      gfx12_opt_set_context_reg(R_028850_CB_TARGET_MASK, SI_TRACKED_CB_TARGET_MASK,
                                cb_target_mask);
      gfx12_opt_set_context_reg(R_028754_SX_PS_DOWNCONVERT, SI_TRACKED_SX_PS_DOWNCONVERT,
                                sx_ps_downconvert);
      gfx12_opt_set_context_reg(R_028758_SX_BLEND_OPT_EPSILON, SI_TRACKED_SX_BLEND_OPT_EPSILON,
                                sx_blend_opt_epsilon);
      gfx12_opt_set_context_reg(R_02875C_SX_BLEND_OPT_CONTROL, SI_TRACKED_SX_BLEND_OPT_CONTROL,
                                sx_blend_opt_control);
      gfx12_end_context_regs();
      radeon_end(); /* don't track context rolls on GFX12 */
   } else if (sctx->screen->info.has_set_context_pairs_packed) {
      radeon_begin(cs);
      gfx11_begin_packed_context_regs();
      gfx11_opt_set_context_reg(R_028238_CB_TARGET_MASK, SI_TRACKED_CB_TARGET_MASK,
                                cb_target_mask);
      gfx11_opt_set_context_reg(R_028424_CB_DCC_CONTROL, SI_TRACKED_CB_DCC_CONTROL,
                                cb_dcc_control);
      gfx11_opt_set_context_reg(R_028754_SX_PS_DOWNCONVERT, SI_TRACKED_SX_PS_DOWNCONVERT,
                                sx_ps_downconvert);
      gfx11_opt_set_context_reg(R_028758_SX_BLEND_OPT_EPSILON, SI_TRACKED_SX_BLEND_OPT_EPSILON,
                                sx_blend_opt_epsilon);
      gfx11_opt_set_context_reg(R_02875C_SX_BLEND_OPT_CONTROL, SI_TRACKED_SX_BLEND_OPT_CONTROL,
                                sx_blend_opt_control);
      gfx11_end_packed_context_regs();
      radeon_end(); /* don't track context rolls on GFX11 */
   } else {
      radeon_begin(cs);
      radeon_opt_set_context_reg(R_028238_CB_TARGET_MASK, SI_TRACKED_CB_TARGET_MASK,
                                 cb_target_mask);
      if (sctx->gfx_level >= GFX8) {
         radeon_opt_set_context_reg(R_028424_CB_DCC_CONTROL, SI_TRACKED_CB_DCC_CONTROL,
                                    cb_dcc_control);
      }
      if (sctx->screen->info.rbplus_allowed) {
         radeon_opt_set_context_reg3(R_028754_SX_PS_DOWNCONVERT, SI_TRACKED_SX_PS_DOWNCONVERT,
                                     sx_ps_downconvert, sx_blend_opt_epsilon, sx_blend_opt_control);
      }
      radeon_end_update_context_roll();
   }
}

/*
 * Blender functions
 */

static uint32_t si_translate_blend_function(int blend_func)
{
   switch (blend_func) {
   case PIPE_BLEND_ADD:
      return V_028780_COMB_DST_PLUS_SRC;
   case PIPE_BLEND_SUBTRACT:
      return V_028780_COMB_SRC_MINUS_DST;
   case PIPE_BLEND_REVERSE_SUBTRACT:
      return V_028780_COMB_DST_MINUS_SRC;
   case PIPE_BLEND_MIN:
      return V_028780_COMB_MIN_DST_SRC;
   case PIPE_BLEND_MAX:
      return V_028780_COMB_MAX_DST_SRC;
   default:
      PRINT_ERR("Unknown blend function %d\n", blend_func);
      assert(0);
      break;
   }
   return 0;
}

static uint32_t si_translate_blend_factor(enum amd_gfx_level gfx_level, int blend_fact)
{
   switch (blend_fact) {
   case PIPE_BLENDFACTOR_ONE:
      return V_028780_BLEND_ONE;
   case PIPE_BLENDFACTOR_SRC_COLOR:
      return V_028780_BLEND_SRC_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA:
      return V_028780_BLEND_SRC_ALPHA;
   case PIPE_BLENDFACTOR_DST_ALPHA:
      return V_028780_BLEND_DST_ALPHA;
   case PIPE_BLENDFACTOR_DST_COLOR:
      return V_028780_BLEND_DST_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
      return V_028780_BLEND_SRC_ALPHA_SATURATE;
   case PIPE_BLENDFACTOR_CONST_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_CONSTANT_COLOR_GFX11:
                                   V_028780_BLEND_CONSTANT_COLOR_GFX6;
   case PIPE_BLENDFACTOR_CONST_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_CONSTANT_ALPHA_GFX11 :
                                   V_028780_BLEND_CONSTANT_ALPHA_GFX6;
   case PIPE_BLENDFACTOR_ZERO:
      return V_028780_BLEND_ZERO;
   case PIPE_BLENDFACTOR_INV_SRC_COLOR:
      return V_028780_BLEND_ONE_MINUS_SRC_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
      return V_028780_BLEND_ONE_MINUS_SRC_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA:
      return V_028780_BLEND_ONE_MINUS_DST_ALPHA;
   case PIPE_BLENDFACTOR_INV_DST_COLOR:
      return V_028780_BLEND_ONE_MINUS_DST_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR_GFX11:
                                   V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR_GFX6;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA_GFX11:
                                   V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA_GFX6;
   case PIPE_BLENDFACTOR_SRC1_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_SRC1_COLOR_GFX11:
                                   V_028780_BLEND_SRC1_COLOR_GFX6;
   case PIPE_BLENDFACTOR_SRC1_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_SRC1_ALPHA_GFX11:
                                   V_028780_BLEND_SRC1_ALPHA_GFX6;
   case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
      return gfx_level >= GFX11 ? V_028780_BLEND_INV_SRC1_COLOR_GFX11:
                                   V_028780_BLEND_INV_SRC1_COLOR_GFX6;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
      return gfx_level >= GFX11 ? V_028780_BLEND_INV_SRC1_ALPHA_GFX11:
                                   V_028780_BLEND_INV_SRC1_ALPHA_GFX6;
   default:
      PRINT_ERR("Bad blend factor %d not supported!\n", blend_fact);
      assert(0);
      break;
   }
   return 0;
}

static uint32_t si_translate_blend_opt_function(int blend_func)
{
   switch (blend_func) {
   case PIPE_BLEND_ADD:
      return V_028760_OPT_COMB_ADD;
   case PIPE_BLEND_SUBTRACT:
      return V_028760_OPT_COMB_SUBTRACT;
   case PIPE_BLEND_REVERSE_SUBTRACT:
      return V_028760_OPT_COMB_REVSUBTRACT;
   case PIPE_BLEND_MIN:
      return V_028760_OPT_COMB_MIN;
   case PIPE_BLEND_MAX:
      return V_028760_OPT_COMB_MAX;
   default:
      return V_028760_OPT_COMB_BLEND_DISABLED;
   }
}

static uint32_t si_translate_blend_opt_factor(int blend_fact, bool is_alpha)
{
   switch (blend_fact) {
   case PIPE_BLENDFACTOR_ZERO:
      return V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_ALL;
   case PIPE_BLENDFACTOR_ONE:
      return V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE;
   case PIPE_BLENDFACTOR_SRC_COLOR:
      return is_alpha ? V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0
                      : V_028760_BLEND_OPT_PRESERVE_C1_IGNORE_C0;
   case PIPE_BLENDFACTOR_INV_SRC_COLOR:
      return is_alpha ? V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1
                      : V_028760_BLEND_OPT_PRESERVE_C0_IGNORE_C1;
   case PIPE_BLENDFACTOR_SRC_ALPHA:
      return V_028760_BLEND_OPT_PRESERVE_A1_IGNORE_A0;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA:
      return V_028760_BLEND_OPT_PRESERVE_A0_IGNORE_A1;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE:
      return is_alpha ? V_028760_BLEND_OPT_PRESERVE_ALL_IGNORE_NONE
                      : V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0;
   default:
      return V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;
   }
}

static void si_blend_check_commutativity(struct si_screen *sscreen, struct si_state_blend *blend,
                                         enum pipe_blend_func func, enum pipe_blendfactor src,
                                         enum pipe_blendfactor dst, unsigned chanmask)
{
   /* Src factor is allowed when it does not depend on Dst */
   static const uint32_t src_allowed =
      (1u << PIPE_BLENDFACTOR_ONE) | (1u << PIPE_BLENDFACTOR_SRC_COLOR) |
      (1u << PIPE_BLENDFACTOR_SRC_ALPHA) | (1u << PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE) |
      (1u << PIPE_BLENDFACTOR_CONST_COLOR) | (1u << PIPE_BLENDFACTOR_CONST_ALPHA) |
      (1u << PIPE_BLENDFACTOR_SRC1_COLOR) | (1u << PIPE_BLENDFACTOR_SRC1_ALPHA) |
      (1u << PIPE_BLENDFACTOR_ZERO) | (1u << PIPE_BLENDFACTOR_INV_SRC_COLOR) |
      (1u << PIPE_BLENDFACTOR_INV_SRC_ALPHA) | (1u << PIPE_BLENDFACTOR_INV_CONST_COLOR) |
      (1u << PIPE_BLENDFACTOR_INV_CONST_ALPHA) | (1u << PIPE_BLENDFACTOR_INV_SRC1_COLOR) |
      (1u << PIPE_BLENDFACTOR_INV_SRC1_ALPHA);

   if (dst == PIPE_BLENDFACTOR_ONE && (src_allowed & (1u << src)) &&
       (func == PIPE_BLEND_MAX || func == PIPE_BLEND_MIN))
      blend->commutative_4bit |= chanmask;
}

/**
 * Get rid of DST in the blend factors by commuting the operands:
 *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
 */
static void si_blend_remove_dst(unsigned *func, unsigned *src_factor, unsigned *dst_factor,
                                unsigned expected_dst, unsigned replacement_src)
{
   if (*src_factor == expected_dst && *dst_factor == PIPE_BLENDFACTOR_ZERO) {
      *src_factor = PIPE_BLENDFACTOR_ZERO;
      *dst_factor = replacement_src;

      /* Commuting the operands requires reversing subtractions. */
      if (*func == PIPE_BLEND_SUBTRACT)
         *func = PIPE_BLEND_REVERSE_SUBTRACT;
      else if (*func == PIPE_BLEND_REVERSE_SUBTRACT)
         *func = PIPE_BLEND_SUBTRACT;
   }
}

static void *si_create_blend_state_mode(struct pipe_context *ctx,
                                        const struct pipe_blend_state *state, unsigned mode)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_state_blend *blend = CALLOC_STRUCT(si_state_blend);
   struct si_pm4_state *pm4 = &blend->pm4;
   uint32_t sx_mrt_blend_opt[8] = {0};
   uint32_t color_control = 0;
   bool logicop_enable = state->logicop_enable && state->logicop_func != PIPE_LOGICOP_COPY;

   if (!blend)
      return NULL;

   si_pm4_clear_state(pm4, sctx->screen, false);

   blend->alpha_to_coverage = state->alpha_to_coverage;
   blend->alpha_to_one = state->alpha_to_one;
   blend->dual_src_blend = util_blend_state_is_dual(state, 0);
   blend->logicop_enable = logicop_enable;
   blend->allows_noop_optimization =
      state->rt[0].rgb_func == PIPE_BLEND_ADD &&
      state->rt[0].alpha_func == PIPE_BLEND_ADD &&
      state->rt[0].rgb_src_factor == PIPE_BLENDFACTOR_DST_COLOR &&
      state->rt[0].alpha_src_factor == PIPE_BLENDFACTOR_DST_COLOR &&
      state->rt[0].rgb_dst_factor == PIPE_BLENDFACTOR_ZERO &&
      state->rt[0].alpha_dst_factor == PIPE_BLENDFACTOR_ZERO &&
      mode == V_028808_CB_NORMAL;

   unsigned num_shader_outputs = state->max_rt + 1; /* estimate */
   if (blend->dual_src_blend)
      num_shader_outputs = MAX2(num_shader_outputs, 2);

   if (logicop_enable) {
      color_control |= S_028808_ROP3(state->logicop_func | (state->logicop_func << 4));
   } else {
      color_control |= S_028808_ROP3(0xcc);
   }

   unsigned db_alpha_to_mask;
   if (state->alpha_to_coverage && state->alpha_to_coverage_dither) {
      db_alpha_to_mask = S_028B70_ALPHA_TO_MASK_ENABLE(state->alpha_to_coverage) |
                         S_028B70_ALPHA_TO_MASK_OFFSET0(3) | S_028B70_ALPHA_TO_MASK_OFFSET1(1) |
                         S_028B70_ALPHA_TO_MASK_OFFSET2(0) | S_028B70_ALPHA_TO_MASK_OFFSET3(2) |
                         S_028B70_OFFSET_ROUND(1);
   } else {
      db_alpha_to_mask = S_028B70_ALPHA_TO_MASK_ENABLE(state->alpha_to_coverage) |
                         S_028B70_ALPHA_TO_MASK_OFFSET0(2) | S_028B70_ALPHA_TO_MASK_OFFSET1(2) |
                         S_028B70_ALPHA_TO_MASK_OFFSET2(2) | S_028B70_ALPHA_TO_MASK_OFFSET3(2) |
                         S_028B70_OFFSET_ROUND(0);
   }

   if (sctx->gfx_level >= GFX12)
      ac_pm4_set_reg(&pm4->base, R_02807C_DB_ALPHA_TO_MASK, db_alpha_to_mask);
   else
      ac_pm4_set_reg(&pm4->base, R_028B70_DB_ALPHA_TO_MASK, db_alpha_to_mask);

   blend->cb_target_mask = 0;
   blend->cb_target_enabled_4bit = 0;

   unsigned last_blend_cntl;

   for (int i = 0; i < num_shader_outputs; i++) {
      /* state->rt entries > 0 only written if independent blending */
      const int j = state->independent_blend_enable ? i : 0;

      unsigned eqRGB = state->rt[j].rgb_func;
      unsigned srcRGB = state->rt[j].rgb_src_factor;
      unsigned dstRGB = state->rt[j].rgb_dst_factor;
      unsigned eqA = state->rt[j].alpha_func;
      unsigned srcA = state->rt[j].alpha_src_factor;
      unsigned dstA = state->rt[j].alpha_dst_factor;

      unsigned srcRGB_opt, dstRGB_opt, srcA_opt, dstA_opt;
      unsigned blend_cntl = 0;

      sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED) |
                            S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_BLEND_DISABLED);

      /* Only set dual source blending for MRT0 to avoid a hang. */
      if (i >= 1 && blend->dual_src_blend) {
         if (i == 1) {
            if (sctx->gfx_level >= GFX11)
               blend_cntl = last_blend_cntl;
            else
               blend_cntl = S_028780_ENABLE(1);
         }

         ac_pm4_set_reg(&pm4->base, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);
         continue;
      }

      /* Only addition and subtraction equations are supported with
       * dual source blending.
       */
      if (blend->dual_src_blend && (eqRGB == PIPE_BLEND_MIN || eqRGB == PIPE_BLEND_MAX ||
                                    eqA == PIPE_BLEND_MIN || eqA == PIPE_BLEND_MAX)) {
         assert(!"Unsupported equation for dual source blending");
         ac_pm4_set_reg(&pm4->base, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);
         continue;
      }

      /* cb_render_state will disable unused ones */
      blend->cb_target_mask |= (unsigned)state->rt[j].colormask << (4 * i);
      if (state->rt[j].colormask)
         blend->cb_target_enabled_4bit |= 0xf << (4 * i);

      if (!state->rt[j].colormask || !state->rt[j].blend_enable) {
         ac_pm4_set_reg(&pm4->base, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);
         continue;
      }

      si_blend_check_commutativity(sctx->screen, blend, eqRGB, srcRGB, dstRGB, 0x7 << (4 * i));
      si_blend_check_commutativity(sctx->screen, blend, eqA, srcA, dstA, 0x8 << (4 * i));

      /* Blending optimizations for RB+.
       * These transformations don't change the behavior.
       *
       * First, get rid of DST in the blend factors:
       *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
       */
      si_blend_remove_dst(&eqRGB, &srcRGB, &dstRGB, PIPE_BLENDFACTOR_DST_COLOR,
                          PIPE_BLENDFACTOR_SRC_COLOR);
      si_blend_remove_dst(&eqA, &srcA, &dstA, PIPE_BLENDFACTOR_DST_COLOR,
                          PIPE_BLENDFACTOR_SRC_COLOR);
      si_blend_remove_dst(&eqA, &srcA, &dstA, PIPE_BLENDFACTOR_DST_ALPHA,
                          PIPE_BLENDFACTOR_SRC_ALPHA);

      /* Look up the ideal settings from tables. */
      srcRGB_opt = si_translate_blend_opt_factor(srcRGB, false);
      dstRGB_opt = si_translate_blend_opt_factor(dstRGB, false);
      srcA_opt = si_translate_blend_opt_factor(srcA, true);
      dstA_opt = si_translate_blend_opt_factor(dstA, true);

      /* Handle interdependencies. */
      if (util_blend_factor_uses_dest(srcRGB, false))
         dstRGB_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;
      if (util_blend_factor_uses_dest(srcA, false))
         dstA_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_NONE;

      if (srcRGB == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE &&
          (dstRGB == PIPE_BLENDFACTOR_ZERO || dstRGB == PIPE_BLENDFACTOR_SRC_ALPHA ||
           dstRGB == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE))
         dstRGB_opt = V_028760_BLEND_OPT_PRESERVE_NONE_IGNORE_A0;

      /* Set the final value. */
      sx_mrt_blend_opt[i] = S_028760_COLOR_SRC_OPT(srcRGB_opt) |
                            S_028760_COLOR_DST_OPT(dstRGB_opt) |
                            S_028760_COLOR_COMB_FCN(si_translate_blend_opt_function(eqRGB)) |
                            S_028760_ALPHA_SRC_OPT(srcA_opt) | S_028760_ALPHA_DST_OPT(dstA_opt) |
                            S_028760_ALPHA_COMB_FCN(si_translate_blend_opt_function(eqA));

      /* Alpha-to-coverage with blending enabled, depth writes enabled, and having no MRTZ export
       * should disable SX blend optimizations.
       *
       * TODO: Add a piglit test for this. It should fail on gfx11 without this.
       */
      if (sctx->gfx_level >= GFX11 && state->alpha_to_coverage && i == 0) {
         sx_mrt_blend_opt[0] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_NONE) |
                               S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_NONE);
      }

      /* Set blend state. */
      blend_cntl |= S_028780_ENABLE(1);
      blend_cntl |= S_028780_COLOR_COMB_FCN(si_translate_blend_function(eqRGB));
      blend_cntl |= S_028780_COLOR_SRCBLEND(si_translate_blend_factor(sctx->gfx_level, srcRGB));
      blend_cntl |= S_028780_COLOR_DESTBLEND(si_translate_blend_factor(sctx->gfx_level, dstRGB));

      if (srcA != srcRGB || dstA != dstRGB || eqA != eqRGB) {
         blend_cntl |= S_028780_SEPARATE_ALPHA_BLEND(1);
         blend_cntl |= S_028780_ALPHA_COMB_FCN(si_translate_blend_function(eqA));
         blend_cntl |= S_028780_ALPHA_SRCBLEND(si_translate_blend_factor(sctx->gfx_level, srcA));
         blend_cntl |= S_028780_ALPHA_DESTBLEND(si_translate_blend_factor(sctx->gfx_level, dstA));
      }
      ac_pm4_set_reg(&pm4->base, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);
      last_blend_cntl = blend_cntl;

      blend->blend_enable_4bit |= 0xfu << (i * 4);

      if (sctx->gfx_level >= GFX8 && sctx->gfx_level <= GFX10)
         blend->dcc_msaa_corruption_4bit |= 0xfu << (i * 4);

      /* This is only important for formats without alpha. */
      if (srcRGB == PIPE_BLENDFACTOR_SRC_ALPHA || dstRGB == PIPE_BLENDFACTOR_SRC_ALPHA ||
          srcRGB == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE ||
          dstRGB == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE ||
          srcRGB == PIPE_BLENDFACTOR_INV_SRC_ALPHA || dstRGB == PIPE_BLENDFACTOR_INV_SRC_ALPHA)
         blend->need_src_alpha_4bit |= 0xfu << (i * 4);
   }

   if (sctx->gfx_level >= GFX8 && sctx->gfx_level <= GFX10 && logicop_enable)
      blend->dcc_msaa_corruption_4bit |= blend->cb_target_enabled_4bit;

   if (blend->cb_target_mask) {
      color_control |= S_028808_MODE(mode);
   } else {
      color_control |= S_028808_MODE(V_028808_CB_DISABLE);
   }

   if (sctx->screen->info.rbplus_allowed) {
      /* Disable RB+ blend optimizations for dual source blending.
       * Vulkan does this.
       */
      if (blend->dual_src_blend) {
         for (int i = 0; i < num_shader_outputs; i++) {
            sx_mrt_blend_opt[i] = S_028760_COLOR_COMB_FCN(V_028760_OPT_COMB_NONE) |
                                  S_028760_ALPHA_COMB_FCN(V_028760_OPT_COMB_NONE);
         }
      }

      for (int i = 0; i < num_shader_outputs; i++)
         ac_pm4_set_reg(&pm4->base, R_028760_SX_MRT0_BLEND_OPT + i * 4, sx_mrt_blend_opt[i]);

      /* RB+ doesn't work with dual source blending, logic op, and RESOLVE. */
      if (blend->dual_src_blend || logicop_enable || mode == V_028808_CB_RESOLVE ||
          /* Disabling RB+ improves blending performance in synthetic tests on GFX11. */
          (sctx->gfx_level == GFX11 && blend->blend_enable_4bit))
         color_control |= S_028808_DISABLE_DUAL_QUAD(1);
   }

   if (sctx->gfx_level >= GFX12)
      ac_pm4_set_reg(&pm4->base, R_028858_CB_COLOR_CONTROL, color_control);
   else
      ac_pm4_set_reg(&pm4->base, R_028808_CB_COLOR_CONTROL, color_control);

   ac_pm4_finalize(&pm4->base);
   return blend;
}

static void *si_create_blend_state(struct pipe_context *ctx, const struct pipe_blend_state *state)
{
   return si_create_blend_state_mode(ctx, state, V_028808_CB_NORMAL);
}

static bool si_check_blend_dst_sampler_noop(struct si_context *sctx)
{
   if (sctx->framebuffer.state.nr_cbufs == 1) {
      struct si_shader_selector *sel = sctx->shader.ps.cso;

      if (unlikely(sel->info.writes_1_if_tex_is_1 == 0xff)) {
         /* Wait for the shader to be ready. */
         util_queue_fence_wait(&sel->ready);
         assert(sel->nir_binary);

         struct nir_shader *nir = si_deserialize_shader(sel);

         /* Determine if this fragment shader always writes vec4(1) if a specific texture
          * is all 1s.
          */
         float in[4] = { 1.0, 1.0, 1.0, 1.0 };
         float out[4];
         int texunit;
         if (si_nir_is_output_const_if_tex_is_const(nir, in, out, &texunit) &&
             !memcmp(in, out, 4 * sizeof(float))) {
            sel->info.writes_1_if_tex_is_1 = 1 + texunit;
         } else {
            sel->info.writes_1_if_tex_is_1 = 0;
         }

         ralloc_free(nir);
      }

      if (sel->info.writes_1_if_tex_is_1 &&
          sel->info.writes_1_if_tex_is_1 != 0xff) {
         /* Now check if the texture is cleared to 1 */
         int unit = sctx->shader.ps.cso->info.writes_1_if_tex_is_1 - 1;
         struct si_samplers *samp = &sctx->samplers[PIPE_SHADER_FRAGMENT];
         if ((1u << unit) & samp->enabled_mask) {
            struct si_texture* tex = (struct si_texture*) samp->views[unit]->texture;
            if (tex->is_depth &&
                tex->depth_cleared_level_mask & BITFIELD_BIT(samp->views[unit]->u.tex.first_level) &&
                tex->depth_clear_value[0] == 1) {
               return false;
            }
            /* TODO: handle color textures */
         }
      }
   }

   return true;
}

static void si_draw_blend_dst_sampler_noop(struct pipe_context *ctx,
                                           const struct pipe_draw_info *info,
                                           unsigned drawid_offset,
                                           const struct pipe_draw_indirect_info *indirect,
                                           const struct pipe_draw_start_count_bias *draws,
                                           unsigned num_draws) {
   struct si_context *sctx = (struct si_context *)ctx;

   if (!si_check_blend_dst_sampler_noop(sctx))
      return;

   sctx->real_draw_vbo(ctx, info, drawid_offset, indirect, draws, num_draws);
}

static void si_draw_vstate_blend_dst_sampler_noop(struct pipe_context *ctx,
                                                  struct pipe_vertex_state *state,
                                                  uint32_t partial_velem_mask,
                                                  struct pipe_draw_vertex_state_info info,
                                                  const struct pipe_draw_start_count_bias *draws,
                                                  unsigned num_draws) {
   struct si_context *sctx = (struct si_context *)ctx;

   if (!si_check_blend_dst_sampler_noop(sctx))
      return;

   sctx->real_draw_vertex_state(ctx, state, partial_velem_mask, info, draws, num_draws);
}

static void si_bind_blend_state(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_state_blend *old_blend = sctx->queued.named.blend;
   struct si_state_blend *blend = (struct si_state_blend *)state;

   if (!blend)
      blend = (struct si_state_blend *)sctx->noop_blend;

   si_pm4_bind_state(sctx, blend, blend);

   if (old_blend->cb_target_mask != blend->cb_target_mask ||
       old_blend->dual_src_blend != blend->dual_src_blend ||
       (old_blend->dcc_msaa_corruption_4bit != blend->dcc_msaa_corruption_4bit &&
        sctx->framebuffer.has_dcc_msaa))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.cb_render_state);

   if ((sctx->screen->info.has_export_conflict_bug &&
        old_blend->blend_enable_4bit != blend->blend_enable_4bit) ||
       (sctx->occlusion_query_mode == SI_OCCLUSION_QUERY_MODE_PRECISE_BOOLEAN &&
        !!old_blend->cb_target_mask != !!blend->cb_target_enabled_4bit))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);

   if (old_blend->cb_target_enabled_4bit != blend->cb_target_enabled_4bit ||
       old_blend->alpha_to_coverage != blend->alpha_to_coverage ||
       old_blend->alpha_to_one != blend->alpha_to_one ||
       old_blend->dual_src_blend != blend->dual_src_blend ||
       old_blend->blend_enable_4bit != blend->blend_enable_4bit ||
       old_blend->need_src_alpha_4bit != blend->need_src_alpha_4bit)
      si_ps_key_update_framebuffer_blend_dsa_rasterizer(sctx);

   if (old_blend->cb_target_enabled_4bit != blend->cb_target_enabled_4bit ||
       old_blend->alpha_to_coverage != blend->alpha_to_coverage)
      si_update_ps_inputs_read_or_disabled(sctx);

   if (sctx->screen->dpbb_allowed &&
       (old_blend->alpha_to_coverage != blend->alpha_to_coverage ||
        old_blend->blend_enable_4bit != blend->blend_enable_4bit ||
        old_blend->cb_target_enabled_4bit != blend->cb_target_enabled_4bit))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.dpbb_state);

   if (sctx->screen->info.has_out_of_order_rast &&
       ((old_blend->blend_enable_4bit != blend->blend_enable_4bit ||
         old_blend->cb_target_enabled_4bit != blend->cb_target_enabled_4bit ||
         old_blend->commutative_4bit != blend->commutative_4bit ||
         old_blend->logicop_enable != blend->logicop_enable)))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);

   /* RB+ depth-only rendering. See the comment where we set rbplus_depth_only_opt for more
    * information.
    */
   if (sctx->screen->info.rbplus_allowed &&
       !!old_blend->cb_target_mask != !!blend->cb_target_mask) {
      sctx->framebuffer.dirty_cbufs |= BITFIELD_BIT(0);
      si_mark_atom_dirty(sctx, &sctx->atoms.s.framebuffer);
   }

   if (likely(!radeon_uses_secure_bos(sctx->ws))) {
      if (unlikely(blend->allows_noop_optimization)) {
         si_install_draw_wrapper(sctx, si_draw_blend_dst_sampler_noop,
                                 si_draw_vstate_blend_dst_sampler_noop);
      } else {
         si_install_draw_wrapper(sctx, NULL, NULL);
      }
   }
}

static void si_delete_blend_state(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;

   if (sctx->queued.named.blend == state)
      si_bind_blend_state(ctx, sctx->noop_blend);

   si_pm4_free_state(sctx, (struct si_pm4_state*)state, SI_STATE_IDX(blend));
}

static void si_set_blend_color(struct pipe_context *ctx, const struct pipe_blend_color *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   static const struct pipe_blend_color zeros;

   sctx->blend_color = *state;
   sctx->blend_color_any_nonzeros = memcmp(state, &zeros, sizeof(*state)) != 0;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.blend_color);
}

static void si_emit_blend_color(struct si_context *sctx, unsigned index)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   radeon_begin(cs);
   radeon_set_context_reg_seq(R_028414_CB_BLEND_RED, 4);
   radeon_emit_array((uint32_t *)sctx->blend_color.color, 4);
   radeon_end();
}

/*
 * Clipping
 */

static void si_set_clip_state(struct pipe_context *ctx, const struct pipe_clip_state *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct pipe_constant_buffer cb;
   static const struct pipe_clip_state zeros;

   if (memcmp(&sctx->clip_state, state, sizeof(*state)) == 0)
      return;

   sctx->clip_state = *state;
   sctx->clip_state_any_nonzeros = memcmp(state, &zeros, sizeof(*state)) != 0;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.clip_state);

   cb.buffer = NULL;
   cb.user_buffer = state->ucp;
   cb.buffer_offset = 0;
   cb.buffer_size = 4 * 4 * 8;
   si_set_internal_const_buffer(sctx, SI_VS_CONST_CLIP_PLANES, &cb);
}

static void si_emit_clip_state(struct si_context *sctx, unsigned index)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   radeon_begin(cs);
   if (sctx->gfx_level >= GFX12)
      radeon_set_context_reg_seq(R_0282D0_PA_CL_UCP_0_X, 6 * 4);
   else
      radeon_set_context_reg_seq(R_0285BC_PA_CL_UCP_0_X, 6 * 4);
   radeon_emit_array((uint32_t *)sctx->clip_state.ucp, 6 * 4);
   radeon_end();
}

static void si_emit_clip_regs(struct si_context *sctx, unsigned index)
{
   struct si_shader *vs = si_get_vs(sctx)->current;
   struct si_shader_selector *vs_sel = vs->selector;
   struct si_shader_info *info = &vs_sel->info;
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
   bool window_space = vs_sel->stage == MESA_SHADER_VERTEX ?
                          info->base.vs.window_space_position : 0;
   unsigned clipdist_mask = vs_sel->info.clipdist_mask;
   unsigned ucp_mask = clipdist_mask ? 0 : rs->clip_plane_enable & SI_USER_CLIP_PLANE_MASK;
   unsigned culldist_mask = vs_sel->info.culldist_mask;

   /* Clip distances on points have no effect, so need to be implemented
    * as cull distances. This applies for the clipvertex case as well.
    *
    * Setting this for primitives other than points should have no adverse
    * effects.
    */
   clipdist_mask &= rs->clip_plane_enable;
   culldist_mask |= clipdist_mask;

   unsigned pa_cl_cntl = S_02881C_BYPASS_VTX_RATE_COMBINER(sctx->gfx_level >= GFX10_3 &&
                                                           !sctx->screen->options.vrs2x2) |
                         S_02881C_BYPASS_PRIM_RATE_COMBINER(sctx->gfx_level >= GFX10_3) |
                         clipdist_mask | (culldist_mask << 8);

   unsigned pa_cl_clip_cntl = rs->pa_cl_clip_cntl | ucp_mask |
                              S_028810_CLIP_DISABLE(window_space);
   unsigned pa_cl_vs_out_cntl = pa_cl_cntl | vs->pa_cl_vs_out_cntl;

   if (sctx->gfx_level >= GFX12) {
      radeon_begin(&sctx->gfx_cs);
      gfx12_begin_context_regs();
      gfx12_opt_set_context_reg(R_028810_PA_CL_CLIP_CNTL, SI_TRACKED_PA_CL_CLIP_CNTL,
                                pa_cl_clip_cntl);
      gfx12_opt_set_context_reg(R_028818_PA_CL_VS_OUT_CNTL, SI_TRACKED_PA_CL_VS_OUT_CNTL,
                                pa_cl_vs_out_cntl);
      gfx12_end_context_regs();
      radeon_end(); /* don't track context rolls on GFX12 */
   } else if (sctx->screen->info.has_set_context_pairs_packed) {
      radeon_begin(&sctx->gfx_cs);
      gfx11_begin_packed_context_regs();
      gfx11_opt_set_context_reg(R_028810_PA_CL_CLIP_CNTL, SI_TRACKED_PA_CL_CLIP_CNTL,
                                pa_cl_clip_cntl);
      gfx11_opt_set_context_reg(R_02881C_PA_CL_VS_OUT_CNTL, SI_TRACKED_PA_CL_VS_OUT_CNTL,
                                pa_cl_vs_out_cntl);
      gfx11_end_packed_context_regs();
      radeon_end(); /* don't track context rolls on GFX11 */
   } else {
      radeon_begin(&sctx->gfx_cs);
      radeon_opt_set_context_reg(R_028810_PA_CL_CLIP_CNTL, SI_TRACKED_PA_CL_CLIP_CNTL,
                                 pa_cl_clip_cntl);
      radeon_opt_set_context_reg(R_02881C_PA_CL_VS_OUT_CNTL, SI_TRACKED_PA_CL_VS_OUT_CNTL,
                                 pa_cl_vs_out_cntl);
      radeon_end_update_context_roll();
   }
}

/*
 * Rasterizer
 */

static uint32_t si_translate_fill(uint32_t func)
{
   switch (func) {
   case PIPE_POLYGON_MODE_FILL:
      return V_028814_X_DRAW_TRIANGLES;
   case PIPE_POLYGON_MODE_LINE:
      return V_028814_X_DRAW_LINES;
   case PIPE_POLYGON_MODE_POINT:
      return V_028814_X_DRAW_POINTS;
   default:
      assert(0);
      return V_028814_X_DRAW_POINTS;
   }
}

static void *si_create_rs_state(struct pipe_context *ctx, const struct pipe_rasterizer_state *state)
{
   struct si_screen *sscreen = ((struct si_context *)ctx)->screen;
   struct si_state_rasterizer *rs = CALLOC_STRUCT(si_state_rasterizer);

   if (!rs) {
      return NULL;
   }

   rs->scissor_enable = state->scissor;
   rs->clip_halfz = state->clip_halfz;
   rs->two_side = state->light_twoside;
   rs->multisample_enable = state->multisample;
   rs->clip_plane_enable = state->clip_plane_enable;
   rs->half_pixel_center = state->half_pixel_center;
   rs->line_stipple_enable = state->line_stipple_enable;
   rs->poly_stipple_enable = state->poly_stipple_enable;
   rs->line_smooth = state->line_smooth;
   rs->line_width = state->line_width;
   rs->poly_smooth = state->poly_smooth;
   rs->point_smooth = state->point_smooth;
   rs->uses_poly_offset = state->offset_point || state->offset_line || state->offset_tri;
   rs->clamp_fragment_color = state->clamp_fragment_color;
   rs->clamp_vertex_color = state->clamp_vertex_color;
   rs->flatshade = state->flatshade;
   rs->flatshade_first = state->flatshade_first;
   rs->sprite_coord_enable = state->sprite_coord_enable;
   rs->rasterizer_discard = state->rasterizer_discard;
   rs->bottom_edge_rule = state->bottom_edge_rule;
   rs->polygon_mode_is_lines =
      (state->fill_front == PIPE_POLYGON_MODE_LINE && !(state->cull_face & PIPE_FACE_FRONT)) ||
      (state->fill_back == PIPE_POLYGON_MODE_LINE && !(state->cull_face & PIPE_FACE_BACK));
   rs->polygon_mode_is_points =
      (state->fill_front == PIPE_POLYGON_MODE_POINT && !(state->cull_face & PIPE_FACE_FRONT)) ||
      (state->fill_back == PIPE_POLYGON_MODE_POINT && !(state->cull_face & PIPE_FACE_BACK));
   rs->pa_sc_line_stipple = state->line_stipple_enable ?
                               S_028A0C_LINE_PATTERN(state->line_stipple_pattern) |
                               S_028A0C_REPEAT_COUNT(state->line_stipple_factor) : 0;
   /* TODO: implement line stippling with perpendicular end caps. */
   /* Line width > 2 is an internal recommendation. */
   rs->perpendicular_end_caps = state->multisample &&
                                state->line_width > 2 && !state->line_stipple_enable;

   rs->pa_cl_clip_cntl = S_028810_DX_CLIP_SPACE_DEF(state->clip_halfz) |
                         S_028810_ZCLIP_NEAR_DISABLE(!state->depth_clip_near) |
                         S_028810_ZCLIP_FAR_DISABLE(!state->depth_clip_far) |
                         S_028810_DX_RASTERIZATION_KILL(state->rasterizer_discard) |
                         S_028810_DX_LINEAR_ATTR_CLIP_ENA(1);

   rs->ngg_cull_flags_tris = SI_NGG_CULL_CLIP_PLANE_ENABLE(state->clip_plane_enable);
   rs->ngg_cull_flags_lines = (!rs->perpendicular_end_caps ? SI_NGG_CULL_SMALL_LINES_DIAMOND_EXIT : 0) |
                              SI_NGG_CULL_CLIP_PLANE_ENABLE(state->clip_plane_enable);

   if (!state->front_ccw) {
      rs->ngg_cull_front = state->cull_face & PIPE_FACE_FRONT || rs->rasterizer_discard;
      rs->ngg_cull_back = state->cull_face & PIPE_FACE_BACK || rs->rasterizer_discard;
   } else {
      rs->ngg_cull_front = state->cull_face & PIPE_FACE_BACK || rs->rasterizer_discard;
      rs->ngg_cull_back = state->cull_face & PIPE_FACE_FRONT || rs->rasterizer_discard;
   }

   /* Force gl_FrontFacing to true or false if the other face is culled. */
   if (util_bitcount(state->cull_face) == 1) {
      if (state->cull_face & PIPE_FACE_FRONT)
         rs->force_front_face_input = -1;
      else
         rs->force_front_face_input = 1;
   }

   rs->spi_interp_control_0 = S_0286D4_FLAT_SHADE_ENA(1) |
                              S_0286D4_PNT_SPRITE_ENA(state->point_quad_rasterization) |
                              S_0286D4_PNT_SPRITE_OVRD_X(V_0286D4_SPI_PNT_SPRITE_SEL_S) |
                              S_0286D4_PNT_SPRITE_OVRD_Y(V_0286D4_SPI_PNT_SPRITE_SEL_T) |
                              S_0286D4_PNT_SPRITE_OVRD_Z(V_0286D4_SPI_PNT_SPRITE_SEL_0) |
                              S_0286D4_PNT_SPRITE_OVRD_W(V_0286D4_SPI_PNT_SPRITE_SEL_1) |
                              S_0286D4_PNT_SPRITE_TOP_1(state->sprite_coord_mode !=
                                                        PIPE_SPRITE_COORD_UPPER_LEFT);

   /* point size 12.4 fixed point */
   float psize_min, psize_max;
   unsigned tmp = (unsigned)(state->point_size * 8.0);
   rs->pa_su_point_size = S_028A00_HEIGHT(tmp) | S_028A00_WIDTH(tmp);

   if (state->point_size_per_vertex) {
      psize_min = util_get_min_point_size(state);
      psize_max = SI_MAX_POINT_SIZE;
   } else {
      /* Force the point size to be as if the vertex output was disabled. */
      psize_min = state->point_size;
      psize_max = state->point_size;
   }
   rs->max_point_size = psize_max;

   /* Divide by two, because 0.5 = 1 pixel. */
   rs->pa_su_point_minmax = S_028A04_MIN_SIZE(si_pack_float_12p4(psize_min / 2)) |
                            S_028A04_MAX_SIZE(si_pack_float_12p4(psize_max / 2));
   rs->pa_su_line_cntl = S_028A08_WIDTH(si_pack_float_12p4(state->line_width / 2));

   rs->pa_sc_mode_cntl_0 = S_028A48_LINE_STIPPLE_ENABLE(state->line_stipple_enable) |
                           S_028A48_MSAA_ENABLE(state->multisample || state->poly_smooth ||
                                                state->line_smooth) |
                           S_028A48_VPORT_SCISSOR_ENABLE(1) |
                           S_028A48_ALTERNATE_RBS_PER_TILE(sscreen->info.gfx_level >= GFX9);

   bool polygon_mode_enabled =
      (state->fill_front != PIPE_POLYGON_MODE_FILL && !(state->cull_face & PIPE_FACE_FRONT)) ||
      (state->fill_back != PIPE_POLYGON_MODE_FILL && !(state->cull_face & PIPE_FACE_BACK));

   rs->pa_su_sc_mode_cntl = S_028814_PROVOKING_VTX_LAST(!state->flatshade_first) |
                            S_028814_CULL_FRONT((state->cull_face & PIPE_FACE_FRONT) ? 1 : 0) |
                            S_028814_CULL_BACK((state->cull_face & PIPE_FACE_BACK) ? 1 : 0) |
                            S_028814_FACE(!state->front_ccw) |
                            S_028814_POLY_OFFSET_FRONT_ENABLE(util_get_offset(state, state->fill_front)) |
                            S_028814_POLY_OFFSET_BACK_ENABLE(util_get_offset(state, state->fill_back)) |
                            S_028814_POLY_OFFSET_PARA_ENABLE(state->offset_point || state->offset_line) |
                            S_028814_POLY_MODE(polygon_mode_enabled) |
                            S_028814_POLYMODE_FRONT_PTYPE(si_translate_fill(state->fill_front)) |
                            S_028814_POLYMODE_BACK_PTYPE(si_translate_fill(state->fill_back)) |
                            /* this must be set if POLY_MODE or PERPENDICULAR_ENDCAP_ENA is set */
                            S_028814_KEEP_TOGETHER_ENABLE(sscreen->info.gfx_level >= GFX10 &&
                                                          sscreen->info.gfx_level < GFX12 ?
                                                             polygon_mode_enabled ||
                                                             rs->perpendicular_end_caps : 0);
   if (sscreen->info.gfx_level >= GFX10) {
      rs->pa_cl_ngg_cntl = S_028838_INDEX_BUF_EDGE_FLAG_ENA(rs->polygon_mode_is_points ||
                                                            rs->polygon_mode_is_lines) |
                           S_028838_VERTEX_REUSE_DEPTH(sscreen->info.gfx_level >= GFX10_3 ? 30 : 0);
   }

   if (state->bottom_edge_rule) {
      /* OpenGL windows should set this. */
      rs->pa_sc_edgerule = S_028230_ER_TRI(0xA) |
                           S_028230_ER_POINT(0x5) |
                           S_028230_ER_RECT(0x9) |
                           S_028230_ER_LINE_LR(0x2A) |
                           S_028230_ER_LINE_RL(0x2A) |
                           S_028230_ER_LINE_TB(0xA) |
                           S_028230_ER_LINE_BT(0xA);
   } else {
      /* OpenGL FBOs and Direct3D should set this. */
      rs->pa_sc_edgerule = S_028230_ER_TRI(0xA) |
                           S_028230_ER_POINT(0x6) |
                           S_028230_ER_RECT(0xA) |
                           S_028230_ER_LINE_LR(0x19) |
                           S_028230_ER_LINE_RL(0x25) |
                           S_028230_ER_LINE_TB(0xA) |
                           S_028230_ER_LINE_BT(0xA);
   }

   if (rs->uses_poly_offset) {
      /* Calculate polygon offset states for 16-bit, 24-bit, and 32-bit zbuffers. */
      rs->pa_su_poly_offset_clamp = fui(state->offset_clamp);
      rs->pa_su_poly_offset_frontback_scale = fui(state->offset_scale * 16);

      if (!state->offset_units_unscaled) {
         /* 16-bit zbuffer */
         rs->pa_su_poly_offset_db_fmt_cntl[0] = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-16);
         rs->pa_su_poly_offset_frontback_offset[0] = fui(state->offset_units * 4);

         /* 24-bit zbuffer */
         rs->pa_su_poly_offset_db_fmt_cntl[1] = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-24);
         rs->pa_su_poly_offset_frontback_offset[1] = fui(state->offset_units * 2);

         /* 32-bit zbuffer */
         rs->pa_su_poly_offset_db_fmt_cntl[2] = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-23) |
                                                S_028B78_POLY_OFFSET_DB_IS_FLOAT_FMT(1);
         rs->pa_su_poly_offset_frontback_offset[2] = fui(state->offset_units);
      } else {
         rs->pa_su_poly_offset_frontback_offset[0] = fui(state->offset_units);
         rs->pa_su_poly_offset_frontback_offset[1] = fui(state->offset_units);
         rs->pa_su_poly_offset_frontback_offset[2] = fui(state->offset_units);
      }
   }

   return rs;
}

static void si_pm4_emit_rasterizer(struct si_context *sctx, unsigned index)
{
   struct si_state_rasterizer *state = sctx->queued.named.rasterizer;

   if (sctx->screen->info.gfx_level >= GFX12) {
      radeon_begin(&sctx->gfx_cs);
      gfx12_begin_context_regs();
      if (state->line_stipple_enable) {
         gfx12_opt_set_context_reg(R_028A0C_PA_SC_LINE_STIPPLE, SI_TRACKED_PA_SC_LINE_STIPPLE,
                                   state->pa_sc_line_stipple);
      }

      gfx12_opt_set_context_reg(R_028644_SPI_INTERP_CONTROL_0, SI_TRACKED_SPI_INTERP_CONTROL_0,
                                state->spi_interp_control_0);
      gfx12_opt_set_context_reg(R_028A00_PA_SU_POINT_SIZE, SI_TRACKED_PA_SU_POINT_SIZE,
                                state->pa_su_point_size);
      gfx12_opt_set_context_reg(R_028A04_PA_SU_POINT_MINMAX, SI_TRACKED_PA_SU_POINT_MINMAX,
                                state->pa_su_point_minmax);
      gfx12_opt_set_context_reg(R_028A08_PA_SU_LINE_CNTL, SI_TRACKED_PA_SU_LINE_CNTL,
                                state->pa_su_line_cntl);
      gfx12_opt_set_context_reg(R_028A48_PA_SC_MODE_CNTL_0, SI_TRACKED_PA_SC_MODE_CNTL_0,
                                state->pa_sc_mode_cntl_0);
      gfx12_opt_set_context_reg(R_02881C_PA_SU_SC_MODE_CNTL, SI_TRACKED_PA_SU_SC_MODE_CNTL,
                                state->pa_su_sc_mode_cntl);
      gfx12_opt_set_context_reg(R_028838_PA_CL_NGG_CNTL, SI_TRACKED_PA_CL_NGG_CNTL,
                                state->pa_cl_ngg_cntl);
      gfx12_opt_set_context_reg(R_028230_PA_SC_EDGERULE, SI_TRACKED_PA_SC_EDGERULE,
                                state->pa_sc_edgerule);

      if (state->uses_poly_offset && sctx->framebuffer.state.zsbuf) {
         unsigned db_format_index =
            ((struct si_surface *)sctx->framebuffer.state.zsbuf)->db_format_index;

         gfx12_opt_set_context_reg(R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
                                   state->pa_su_poly_offset_db_fmt_cntl[db_format_index]);
         gfx12_opt_set_context_reg(R_028B7C_PA_SU_POLY_OFFSET_CLAMP,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_CLAMP,
                                   state->pa_su_poly_offset_clamp);
         gfx12_opt_set_context_reg(R_028B80_PA_SU_POLY_OFFSET_FRONT_SCALE,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_FRONT_SCALE,
                                   state->pa_su_poly_offset_frontback_scale);
         gfx12_opt_set_context_reg(R_028B84_PA_SU_POLY_OFFSET_FRONT_OFFSET,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_FRONT_OFFSET,
                                   state->pa_su_poly_offset_frontback_offset[db_format_index]);
         gfx12_opt_set_context_reg(R_028B88_PA_SU_POLY_OFFSET_BACK_SCALE,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_BACK_SCALE,
                                   state->pa_su_poly_offset_frontback_scale);
         gfx12_opt_set_context_reg(R_028B8C_PA_SU_POLY_OFFSET_BACK_OFFSET,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_BACK_OFFSET,
                                   state->pa_su_poly_offset_frontback_offset[db_format_index]);
      }
      gfx12_end_context_regs();
      radeon_end(); /* don't track context rolls on GFX12 */
   } else if (sctx->screen->info.has_set_context_pairs_packed) {
      radeon_begin(&sctx->gfx_cs);
      gfx11_begin_packed_context_regs();
      gfx11_opt_set_context_reg(R_0286D4_SPI_INTERP_CONTROL_0, SI_TRACKED_SPI_INTERP_CONTROL_0,
                                state->spi_interp_control_0);
      gfx11_opt_set_context_reg(R_028A00_PA_SU_POINT_SIZE, SI_TRACKED_PA_SU_POINT_SIZE,
                                state->pa_su_point_size);
      gfx11_opt_set_context_reg(R_028A04_PA_SU_POINT_MINMAX, SI_TRACKED_PA_SU_POINT_MINMAX,
                                state->pa_su_point_minmax);
      gfx11_opt_set_context_reg(R_028A08_PA_SU_LINE_CNTL, SI_TRACKED_PA_SU_LINE_CNTL,
                                state->pa_su_line_cntl);
      gfx11_opt_set_context_reg(R_028A48_PA_SC_MODE_CNTL_0, SI_TRACKED_PA_SC_MODE_CNTL_0,
                                state->pa_sc_mode_cntl_0);
      gfx11_opt_set_context_reg(R_028814_PA_SU_SC_MODE_CNTL, SI_TRACKED_PA_SU_SC_MODE_CNTL,
                                state->pa_su_sc_mode_cntl);
      gfx11_opt_set_context_reg(R_028838_PA_CL_NGG_CNTL, SI_TRACKED_PA_CL_NGG_CNTL,
                                state->pa_cl_ngg_cntl);
      gfx11_opt_set_context_reg(R_028230_PA_SC_EDGERULE, SI_TRACKED_PA_SC_EDGERULE,
                                state->pa_sc_edgerule);

      if (state->uses_poly_offset && sctx->framebuffer.state.zsbuf) {
         unsigned db_format_index =
            ((struct si_surface *)sctx->framebuffer.state.zsbuf)->db_format_index;

         gfx11_opt_set_context_reg(R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
                                   state->pa_su_poly_offset_db_fmt_cntl[db_format_index]);
         gfx11_opt_set_context_reg(R_028B7C_PA_SU_POLY_OFFSET_CLAMP,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_CLAMP,
                                   state->pa_su_poly_offset_clamp);
         gfx11_opt_set_context_reg(R_028B80_PA_SU_POLY_OFFSET_FRONT_SCALE,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_FRONT_SCALE,
                                   state->pa_su_poly_offset_frontback_scale);
         gfx11_opt_set_context_reg(R_028B84_PA_SU_POLY_OFFSET_FRONT_OFFSET,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_FRONT_OFFSET,
                                   state->pa_su_poly_offset_frontback_offset[db_format_index]);
         gfx11_opt_set_context_reg(R_028B88_PA_SU_POLY_OFFSET_BACK_SCALE,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_BACK_SCALE,
                                   state->pa_su_poly_offset_frontback_scale);
         gfx11_opt_set_context_reg(R_028B8C_PA_SU_POLY_OFFSET_BACK_OFFSET,
                                   SI_TRACKED_PA_SU_POLY_OFFSET_BACK_OFFSET,
                                   state->pa_su_poly_offset_frontback_offset[db_format_index]);
      }
      gfx11_end_packed_context_regs();
      radeon_end(); /* don't track context rolls on GFX11 */
   } else {
      radeon_begin(&sctx->gfx_cs);
      radeon_opt_set_context_reg(R_0286D4_SPI_INTERP_CONTROL_0,
                                 SI_TRACKED_SPI_INTERP_CONTROL_0,
                                 state->spi_interp_control_0);
      radeon_opt_set_context_reg(R_028A00_PA_SU_POINT_SIZE, SI_TRACKED_PA_SU_POINT_SIZE,
                                 state->pa_su_point_size);
      radeon_opt_set_context_reg(R_028A04_PA_SU_POINT_MINMAX, SI_TRACKED_PA_SU_POINT_MINMAX,
                                 state->pa_su_point_minmax);
      radeon_opt_set_context_reg(R_028A08_PA_SU_LINE_CNTL, SI_TRACKED_PA_SU_LINE_CNTL,
                                 state->pa_su_line_cntl);
      radeon_opt_set_context_reg(R_028A48_PA_SC_MODE_CNTL_0, SI_TRACKED_PA_SC_MODE_CNTL_0,
                                 state->pa_sc_mode_cntl_0);
      radeon_opt_set_context_reg(R_028814_PA_SU_SC_MODE_CNTL,
                                 SI_TRACKED_PA_SU_SC_MODE_CNTL, state->pa_su_sc_mode_cntl);
      if (sctx->gfx_level >= GFX10) {
         radeon_opt_set_context_reg(R_028838_PA_CL_NGG_CNTL, SI_TRACKED_PA_CL_NGG_CNTL,
                                    state->pa_cl_ngg_cntl);
      }
      radeon_opt_set_context_reg(R_028230_PA_SC_EDGERULE, SI_TRACKED_PA_SC_EDGERULE,
                                 state->pa_sc_edgerule);

      if (state->uses_poly_offset && sctx->framebuffer.state.zsbuf) {
         unsigned db_format_index =
            ((struct si_surface *)sctx->framebuffer.state.zsbuf)->db_format_index;

         radeon_opt_set_context_reg6(R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
                                     SI_TRACKED_PA_SU_POLY_OFFSET_DB_FMT_CNTL,
                                     state->pa_su_poly_offset_db_fmt_cntl[db_format_index],
                                     state->pa_su_poly_offset_clamp,
                                     state->pa_su_poly_offset_frontback_scale,
                                     state->pa_su_poly_offset_frontback_offset[db_format_index],
                                     state->pa_su_poly_offset_frontback_scale,
                                     state->pa_su_poly_offset_frontback_offset[db_format_index]);
      }
      radeon_end_update_context_roll();
   }

   sctx->emitted.named.rasterizer = state;
}

static void si_bind_rs_state(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_state_rasterizer *old_rs = (struct si_state_rasterizer *)sctx->queued.named.rasterizer;
   struct si_state_rasterizer *rs = (struct si_state_rasterizer *)state;

   if (!rs)
      rs = (struct si_state_rasterizer *)sctx->discard_rasterizer_state;

   if (old_rs->multisample_enable != rs->multisample_enable) {
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);

      /* Update the small primitive filter workaround if necessary. */
      if (sctx->screen->info.has_small_prim_filter_sample_loc_bug && sctx->framebuffer.nr_samples > 1)
         si_mark_atom_dirty(sctx, &sctx->atoms.s.sample_locations);

      /* NGG cull state uses multisample_enable. */
      if (sctx->screen->use_ngg_culling)
         si_mark_atom_dirty(sctx, &sctx->atoms.s.ngg_cull_state);
   }

   if (old_rs->perpendicular_end_caps != rs->perpendicular_end_caps)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);

   if (sctx->screen->use_ngg_culling &&
       (old_rs->half_pixel_center != rs->half_pixel_center ||
        old_rs->line_width != rs->line_width))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.ngg_cull_state);

   SET_FIELD(sctx->current_vs_state, VS_STATE_CLAMP_VERTEX_COLOR, rs->clamp_vertex_color);

   si_pm4_bind_state(sctx, rasterizer, rs);
   si_update_ngg_cull_face_state(sctx);

   if (old_rs->scissor_enable != rs->scissor_enable)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.scissors);

   /* This never changes for OpenGL. */
   if (old_rs->half_pixel_center != rs->half_pixel_center)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.guardband);

   if (util_prim_is_lines(sctx->current_rast_prim))
      si_set_clip_discard_distance(sctx, rs->line_width);
   else if (sctx->current_rast_prim == MESA_PRIM_POINTS)
      si_set_clip_discard_distance(sctx, rs->max_point_size);

   if (old_rs->clip_halfz != rs->clip_halfz)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.viewports);

   if (old_rs->clip_plane_enable != rs->clip_plane_enable ||
       old_rs->pa_cl_clip_cntl != rs->pa_cl_clip_cntl)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.clip_regs);

   if (old_rs->sprite_coord_enable != rs->sprite_coord_enable ||
       old_rs->flatshade != rs->flatshade)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.spi_map);

   if (sctx->screen->dpbb_allowed && (old_rs->bottom_edge_rule != rs->bottom_edge_rule))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.dpbb_state);

   if (old_rs->multisample_enable != rs->multisample_enable)
      si_ps_key_update_framebuffer_blend_dsa_rasterizer(sctx);

   if (old_rs->flatshade != rs->flatshade ||
       old_rs->clamp_fragment_color != rs->clamp_fragment_color)
      si_ps_key_update_rasterizer(sctx);

   if (old_rs->flatshade != rs->flatshade ||
       old_rs->multisample_enable != rs->multisample_enable)
      si_ps_key_update_framebuffer_rasterizer_sample_shading(sctx);

   if (old_rs->rasterizer_discard != rs->rasterizer_discard ||
       old_rs->two_side != rs->two_side ||
       old_rs->poly_stipple_enable != rs->poly_stipple_enable ||
       old_rs->point_smooth != rs->point_smooth)
      si_update_ps_inputs_read_or_disabled(sctx);

   if (old_rs->point_smooth != rs->point_smooth ||
       old_rs->line_smooth != rs->line_smooth ||
       old_rs->poly_smooth != rs->poly_smooth ||
       old_rs->polygon_mode_is_points != rs->polygon_mode_is_points ||
       old_rs->poly_stipple_enable != rs->poly_stipple_enable ||
       old_rs->two_side != rs->two_side ||
       old_rs->force_front_face_input != rs->force_front_face_input)
      si_vs_ps_key_update_rast_prim_smooth_stipple(sctx);

   /* Used by si_get_vs_key_outputs in si_update_shaders: */
   if (old_rs->clip_plane_enable != rs->clip_plane_enable)
      sctx->do_update_shaders = true;

   if (old_rs->line_smooth != rs->line_smooth ||
       old_rs->poly_smooth != rs->poly_smooth ||
       old_rs->point_smooth != rs->point_smooth ||
       old_rs->poly_stipple_enable != rs->poly_stipple_enable ||
       old_rs->flatshade != rs->flatshade)
      si_update_vrs_flat_shading(sctx);

   if (old_rs->flatshade_first != rs->flatshade_first)
      si_update_ngg_sgpr_state_provoking_vtx(sctx, si_get_vs(sctx)->current, sctx->ngg);
}

static void si_delete_rs_state(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_state_rasterizer *rs = (struct si_state_rasterizer *)state;

   if (sctx->queued.named.rasterizer == state)
      si_bind_rs_state(ctx, sctx->discard_rasterizer_state);

   si_pm4_free_state(sctx, &rs->pm4, SI_STATE_IDX(rasterizer));
}

/*
 * inferred state between dsa and stencil ref
 */
static void si_emit_stencil_ref(struct si_context *sctx, unsigned index)
{
   struct pipe_stencil_ref *ref = &sctx->stencil_ref.state;

   if (sctx->gfx_level >= GFX12) {
      radeon_begin(&sctx->gfx_cs);
      radeon_set_context_reg(R_028088_DB_STENCIL_REF,
                             S_028088_TESTVAL(ref->ref_value[0]) |
                             S_028088_TESTVAL_BF(ref->ref_value[1]));
      radeon_end();
   } else {
      struct si_dsa_stencil_ref_part *dsa = &sctx->stencil_ref.dsa_part;

      radeon_begin(&sctx->gfx_cs);
      radeon_set_context_reg_seq(R_028430_DB_STENCILREFMASK, 2);
      radeon_emit(S_028430_STENCILTESTVAL(ref->ref_value[0]) |
                  S_028430_STENCILMASK(dsa->valuemask[0]) |
                  S_028430_STENCILWRITEMASK(dsa->writemask[0]) |
                  S_028430_STENCILOPVAL(1));
      radeon_emit(S_028434_STENCILTESTVAL_BF(ref->ref_value[1]) |
                  S_028434_STENCILMASK_BF(dsa->valuemask[1]) |
                  S_028434_STENCILWRITEMASK_BF(dsa->writemask[1]) |
                  S_028434_STENCILOPVAL_BF(1));
      radeon_end();
   }
}

static void si_set_stencil_ref(struct pipe_context *ctx, const struct pipe_stencil_ref state)
{
   struct si_context *sctx = (struct si_context *)ctx;

   if (memcmp(&sctx->stencil_ref.state, &state, sizeof(state)) == 0)
      return;

   sctx->stencil_ref.state = state;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.stencil_ref);
}

/*
 * DSA
 */

static uint32_t si_translate_stencil_op(int s_op)
{
   switch (s_op) {
   case PIPE_STENCIL_OP_KEEP:
      return V_02842C_STENCIL_KEEP;
   case PIPE_STENCIL_OP_ZERO:
      return V_02842C_STENCIL_ZERO;
   case PIPE_STENCIL_OP_REPLACE:
      return V_02842C_STENCIL_REPLACE_TEST;
   case PIPE_STENCIL_OP_INCR:
      return V_02842C_STENCIL_ADD_CLAMP;
   case PIPE_STENCIL_OP_DECR:
      return V_02842C_STENCIL_SUB_CLAMP;
   case PIPE_STENCIL_OP_INCR_WRAP:
      return V_02842C_STENCIL_ADD_WRAP;
   case PIPE_STENCIL_OP_DECR_WRAP:
      return V_02842C_STENCIL_SUB_WRAP;
   case PIPE_STENCIL_OP_INVERT:
      return V_02842C_STENCIL_INVERT;
   default:
      PRINT_ERR("Unknown stencil op %d", s_op);
      assert(0);
      break;
   }
   return 0;
}

static bool si_order_invariant_stencil_op(enum pipe_stencil_op op)
{
   /* REPLACE is normally order invariant, except when the stencil
    * reference value is written by the fragment shader. Tracking this
    * interaction does not seem worth the effort, so be conservative. */
   return op != PIPE_STENCIL_OP_INCR && op != PIPE_STENCIL_OP_DECR && op != PIPE_STENCIL_OP_REPLACE;
}

/* Compute whether, assuming Z writes are disabled, this stencil state is order
 * invariant in the sense that the set of passing fragments as well as the
 * final stencil buffer result does not depend on the order of fragments. */
static bool si_order_invariant_stencil_state(const struct pipe_stencil_state *state)
{
   return !state->enabled || !state->writemask ||
          /* The following assumes that Z writes are disabled. */
          (state->func == PIPE_FUNC_ALWAYS && si_order_invariant_stencil_op(state->zpass_op) &&
           si_order_invariant_stencil_op(state->zfail_op)) ||
          (state->func == PIPE_FUNC_NEVER && si_order_invariant_stencil_op(state->fail_op));
}

static void *si_create_dsa_state(struct pipe_context *ctx,
                                 const struct pipe_depth_stencil_alpha_state *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_state_dsa *dsa = CALLOC_STRUCT(si_state_dsa);
   if (!dsa) {
      return NULL;
   }

   dsa->stencil_ref.valuemask[0] = state->stencil[0].valuemask;
   dsa->stencil_ref.valuemask[1] = state->stencil[1].valuemask;
   dsa->stencil_ref.writemask[0] = state->stencil[0].writemask;
   dsa->stencil_ref.writemask[1] = state->stencil[1].writemask;

   dsa->db_depth_control =
      S_028800_Z_ENABLE(state->depth_enabled) | S_028800_Z_WRITE_ENABLE(state->depth_writemask) |
      S_028800_ZFUNC(state->depth_func) | S_028800_DEPTH_BOUNDS_ENABLE(state->depth_bounds_test);

   /* stencil */
   if (state->stencil[0].enabled) {
      dsa->db_depth_control |= S_028800_STENCIL_ENABLE(1);
      dsa->db_depth_control |= S_028800_STENCILFUNC(state->stencil[0].func);
      dsa->db_stencil_control |=
         S_02842C_STENCILFAIL(si_translate_stencil_op(state->stencil[0].fail_op));
      dsa->db_stencil_control |=
         S_02842C_STENCILZPASS(si_translate_stencil_op(state->stencil[0].zpass_op));
      dsa->db_stencil_control |=
         S_02842C_STENCILZFAIL(si_translate_stencil_op(state->stencil[0].zfail_op));

      if (state->stencil[1].enabled) {
         dsa->db_depth_control |= S_028800_BACKFACE_ENABLE(1);
         dsa->db_depth_control |= S_028800_STENCILFUNC_BF(state->stencil[1].func);
         dsa->db_stencil_control |=
            S_02842C_STENCILFAIL_BF(si_translate_stencil_op(state->stencil[1].fail_op));
         dsa->db_stencil_control |=
            S_02842C_STENCILZPASS_BF(si_translate_stencil_op(state->stencil[1].zpass_op));
         dsa->db_stencil_control |=
            S_02842C_STENCILZFAIL_BF(si_translate_stencil_op(state->stencil[1].zfail_op));
      }
   }

   dsa->db_depth_bounds_min = fui(state->depth_bounds_min);
   dsa->db_depth_bounds_max = fui(state->depth_bounds_max);

   /* alpha */
   if (state->alpha_enabled) {
      dsa->alpha_func = state->alpha_func;
      dsa->spi_shader_user_data_ps_alpha_ref = fui(state->alpha_ref_value);
   } else {
      dsa->alpha_func = PIPE_FUNC_ALWAYS;
   }

   dsa->depth_enabled = state->depth_enabled &&
                        (state->depth_writemask || state->depth_func != PIPE_FUNC_ALWAYS);
   dsa->depth_write_enabled = state->depth_enabled && state->depth_writemask;
   dsa->stencil_enabled = state->stencil[0].enabled;
   dsa->stencil_write_enabled =
      (util_writes_stencil(&state->stencil[0]) || util_writes_stencil(&state->stencil[1]));
   dsa->db_can_write = dsa->depth_write_enabled || dsa->stencil_write_enabled;
   dsa->depth_bounds_enabled = state->depth_bounds_test;

   if (sctx->gfx_level >= GFX12) {
      dsa->db_stencil_read_mask = S_028090_TESTMASK(state->stencil[0].valuemask) |
                                  S_028090_TESTMASK_BF(state->stencil[1].valuemask);
      dsa->db_stencil_write_mask = S_028094_WRITEMASK(state->stencil[0].writemask) |
                                   S_028094_WRITEMASK_BF(state->stencil[1].writemask);

      bool force_s_valid = state->stencil[0].zpass_op != state->stencil[0].zfail_op ||
                           (state->stencil[1].enabled &&
                            state->stencil[1].zpass_op != state->stencil[1].zfail_op);
      dsa->db_render_override = S_02800C_FORCE_STENCIL_READ(1) |
                                S_02800C_FORCE_STENCIL_VALID(force_s_valid);
   }

   bool zfunc_is_ordered =
      state->depth_func == PIPE_FUNC_NEVER || state->depth_func == PIPE_FUNC_LESS ||
      state->depth_func == PIPE_FUNC_LEQUAL || state->depth_func == PIPE_FUNC_GREATER ||
      state->depth_func == PIPE_FUNC_GEQUAL;

   bool nozwrite_and_order_invariant_stencil =
      !dsa->db_can_write ||
      (!dsa->depth_write_enabled && si_order_invariant_stencil_state(&state->stencil[0]) &&
       si_order_invariant_stencil_state(&state->stencil[1]));

   dsa->order_invariance[1].zs =
      nozwrite_and_order_invariant_stencil || (!dsa->stencil_write_enabled && zfunc_is_ordered);
   dsa->order_invariance[0].zs = !dsa->depth_write_enabled || zfunc_is_ordered;

   dsa->order_invariance[1].pass_set =
      nozwrite_and_order_invariant_stencil ||
      (!dsa->stencil_write_enabled &&
       (state->depth_func == PIPE_FUNC_ALWAYS || state->depth_func == PIPE_FUNC_NEVER));
   dsa->order_invariance[0].pass_set =
      !dsa->depth_write_enabled ||
      (state->depth_func == PIPE_FUNC_ALWAYS || state->depth_func == PIPE_FUNC_NEVER);

   return dsa;
}

static void si_pm4_emit_dsa(struct si_context *sctx, unsigned index)
{
   struct si_state_dsa *state = sctx->queued.named.dsa;
   assert(state && state != sctx->emitted.named.dsa);

   if (sctx->gfx_level >= GFX12) {
      radeon_begin(&sctx->gfx_cs);
      gfx12_begin_context_regs();
      gfx12_opt_set_context_reg(R_02800C_DB_RENDER_OVERRIDE, SI_TRACKED_DB_RENDER_OVERRIDE,
                                state->db_render_override);
      gfx12_opt_set_context_reg(R_028070_DB_DEPTH_CONTROL, SI_TRACKED_DB_DEPTH_CONTROL,
                                state->db_depth_control);
      if (state->stencil_enabled) {
         gfx12_opt_set_context_reg(R_028074_DB_STENCIL_CONTROL, SI_TRACKED_DB_STENCIL_CONTROL,
                                   state->db_stencil_control);
         gfx12_opt_set_context_reg(R_028090_DB_STENCIL_READ_MASK, SI_TRACKED_DB_STENCIL_READ_MASK,
                                   state->db_stencil_read_mask);
         gfx12_opt_set_context_reg(R_028094_DB_STENCIL_WRITE_MASK, SI_TRACKED_DB_STENCIL_WRITE_MASK,
                                   state->db_stencil_write_mask);
      }
      if (state->depth_bounds_enabled) {
         gfx12_opt_set_context_reg(R_028050_DB_DEPTH_BOUNDS_MIN, SI_TRACKED_DB_DEPTH_BOUNDS_MIN,
                                   state->db_depth_bounds_min);
         gfx12_opt_set_context_reg(R_028054_DB_DEPTH_BOUNDS_MAX, SI_TRACKED_DB_DEPTH_BOUNDS_MAX,
                                   state->db_depth_bounds_max);
      }
      gfx12_end_context_regs();
      radeon_end(); /* don't track context rolls on GFX12 */

      if (state->alpha_func != PIPE_FUNC_ALWAYS && state->alpha_func != PIPE_FUNC_NEVER) {
         gfx12_opt_push_gfx_sh_reg(R_00B030_SPI_SHADER_USER_DATA_PS_0 + SI_SGPR_ALPHA_REF * 4,
                                   SI_TRACKED_SPI_SHADER_USER_DATA_PS__ALPHA_REF,
                                   state->spi_shader_user_data_ps_alpha_ref);
      }
   } else if (sctx->screen->info.has_set_context_pairs_packed) {
      radeon_begin(&sctx->gfx_cs);
      gfx11_begin_packed_context_regs();
      gfx11_opt_set_context_reg(R_028800_DB_DEPTH_CONTROL, SI_TRACKED_DB_DEPTH_CONTROL,
                                state->db_depth_control);
      if (state->stencil_enabled) {
         gfx11_opt_set_context_reg(R_02842C_DB_STENCIL_CONTROL, SI_TRACKED_DB_STENCIL_CONTROL,
                                   state->db_stencil_control);
      }
      if (state->depth_bounds_enabled) {
         gfx11_opt_set_context_reg(R_028020_DB_DEPTH_BOUNDS_MIN, SI_TRACKED_DB_DEPTH_BOUNDS_MIN,
                                   state->db_depth_bounds_min);
         gfx11_opt_set_context_reg(R_028024_DB_DEPTH_BOUNDS_MAX, SI_TRACKED_DB_DEPTH_BOUNDS_MAX,
                                   state->db_depth_bounds_max);
      }
      gfx11_end_packed_context_regs();

      if (state->alpha_func != PIPE_FUNC_ALWAYS && state->alpha_func != PIPE_FUNC_NEVER) {
         if (sctx->screen->info.has_set_sh_pairs_packed) {
            gfx11_opt_push_gfx_sh_reg(R_00B030_SPI_SHADER_USER_DATA_PS_0 + SI_SGPR_ALPHA_REF * 4,
                                      SI_TRACKED_SPI_SHADER_USER_DATA_PS__ALPHA_REF,
                                      state->spi_shader_user_data_ps_alpha_ref);
         } else {
            radeon_opt_set_sh_reg(R_00B030_SPI_SHADER_USER_DATA_PS_0 + SI_SGPR_ALPHA_REF * 4,
                                  SI_TRACKED_SPI_SHADER_USER_DATA_PS__ALPHA_REF,
                                  state->spi_shader_user_data_ps_alpha_ref);
         }
      }
      radeon_end(); /* don't track context rolls on GFX11 */
   } else {
      radeon_begin(&sctx->gfx_cs);
      radeon_opt_set_context_reg(R_028800_DB_DEPTH_CONTROL, SI_TRACKED_DB_DEPTH_CONTROL,
                                 state->db_depth_control);
      if (state->stencil_enabled) {
         radeon_opt_set_context_reg(R_02842C_DB_STENCIL_CONTROL, SI_TRACKED_DB_STENCIL_CONTROL,
                                    state->db_stencil_control);
      }
      if (state->depth_bounds_enabled) {
         radeon_opt_set_context_reg2(R_028020_DB_DEPTH_BOUNDS_MIN,
                                     SI_TRACKED_DB_DEPTH_BOUNDS_MIN,
                                     state->db_depth_bounds_min,
                                     state->db_depth_bounds_max);
      }
      radeon_end_update_context_roll();

      if (state->alpha_func != PIPE_FUNC_ALWAYS && state->alpha_func != PIPE_FUNC_NEVER) {
         radeon_begin(&sctx->gfx_cs);
         radeon_opt_set_sh_reg(R_00B030_SPI_SHADER_USER_DATA_PS_0 + SI_SGPR_ALPHA_REF * 4,
                               SI_TRACKED_SPI_SHADER_USER_DATA_PS__ALPHA_REF,
                               state->spi_shader_user_data_ps_alpha_ref);
         radeon_end();
      }
   }

   sctx->emitted.named.dsa = state;
}

static void si_bind_dsa_state(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_state_dsa *old_dsa = sctx->queued.named.dsa;
   struct si_state_dsa *dsa = state;

   if (!dsa)
      dsa = (struct si_state_dsa *)sctx->noop_dsa;

   si_pm4_bind_state(sctx, dsa, dsa);

   /* Gfx12 doesn't need to combine a DSA state with a stencil ref state. */
   if (sctx->gfx_level < GFX12 &&
       memcmp(&dsa->stencil_ref, &sctx->stencil_ref.dsa_part,
              sizeof(struct si_dsa_stencil_ref_part)) != 0) {
      sctx->stencil_ref.dsa_part = dsa->stencil_ref;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.stencil_ref);
   }

   struct pipe_surface *zssurf = sctx->framebuffer.state.zsbuf;
   struct si_texture *zstex = (struct si_texture*)(zssurf ? zssurf->texture : NULL);

   if (sctx->gfx_level == GFX12 && !sctx->screen->options.alt_hiz_logic &&
       sctx->framebuffer.has_stencil && dsa->stencil_enabled && !zstex->force_disable_hiz_his) {
      zstex->force_disable_hiz_his = true;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.framebuffer);

      if (sctx->framebuffer.has_hiz_his) {
         sctx->framebuffer.has_hiz_his = false;
         si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);
      }
   }

   if (old_dsa->alpha_func != dsa->alpha_func) {
      si_ps_key_update_dsa(sctx);
      si_update_ps_inputs_read_or_disabled(sctx);
      sctx->do_update_shaders = true;
   }

   if (old_dsa->depth_enabled != dsa->depth_enabled ||
       old_dsa->stencil_enabled != dsa->stencil_enabled) {
      si_ps_key_update_framebuffer_blend_dsa_rasterizer(sctx);
      sctx->do_update_shaders = true;
   }

   if (sctx->occlusion_query_mode == SI_OCCLUSION_QUERY_MODE_PRECISE_BOOLEAN &&
       (old_dsa->depth_enabled != dsa->depth_enabled ||
        old_dsa->depth_write_enabled != dsa->depth_write_enabled))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);

   if (sctx->screen->dpbb_allowed && ((old_dsa->depth_enabled != dsa->depth_enabled ||
                                       old_dsa->stencil_enabled != dsa->stencil_enabled ||
                                       old_dsa->db_can_write != dsa->db_can_write)))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.dpbb_state);

   if (sctx->screen->info.has_out_of_order_rast &&
       (memcmp(old_dsa->order_invariance, dsa->order_invariance,
               sizeof(old_dsa->order_invariance))))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);
}

static void si_delete_dsa_state(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;

   if (sctx->queued.named.dsa == state)
      si_bind_dsa_state(ctx, sctx->noop_dsa);

   si_pm4_free_state(sctx, (struct si_pm4_state*)state, SI_STATE_IDX(dsa));
}

static void *si_create_db_flush_dsa(struct si_context *sctx)
{
   struct pipe_depth_stencil_alpha_state dsa = {};

   return sctx->b.create_depth_stencil_alpha_state(&sctx->b, &dsa);
}

/* DB RENDER STATE */

static void si_set_active_query_state(struct pipe_context *ctx, bool enable)
{
   struct si_context *sctx = (struct si_context *)ctx;

   /* Pipeline stat & streamout queries. */
   if (enable) {
      /* Disable pipeline stats if there are no active queries. */
      if (sctx->num_hw_pipestat_streamout_queries) {
         sctx->barrier_flags &= ~SI_BARRIER_EVENT_PIPELINESTAT_STOP;
         sctx->barrier_flags |= SI_BARRIER_EVENT_PIPELINESTAT_START;
         si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
      }
   } else {
      if (sctx->num_hw_pipestat_streamout_queries) {
         sctx->barrier_flags &= ~SI_BARRIER_EVENT_PIPELINESTAT_START;
         sctx->barrier_flags |= SI_BARRIER_EVENT_PIPELINESTAT_STOP;
         si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
      }
   }

   /* Occlusion queries. */
   if (sctx->occlusion_queries_disabled != !enable) {
      sctx->occlusion_queries_disabled = !enable;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);
   }
}

void si_save_qbo_state(struct si_context *sctx, struct si_qbo_state *st)
{
   si_get_pipe_constant_buffer(sctx, PIPE_SHADER_COMPUTE, 0, &st->saved_const0);
}

void si_restore_qbo_state(struct si_context *sctx, struct si_qbo_state *st)
{
   sctx->b.set_constant_buffer(&sctx->b, PIPE_SHADER_COMPUTE, 0, true, &st->saved_const0);
}

static void si_emit_db_render_state(struct si_context *sctx, unsigned index)
{
   unsigned db_shader_control = 0, db_render_control = 0, db_count_control = 0, vrs_override_cntl = 0;

   /* DB_RENDER_CONTROL */
   /* Program OREO_MODE optimally for GFX11+. */
   if (sctx->gfx_level >= GFX11) {
      bool z_export = G_02880C_Z_EXPORT_ENABLE(sctx->ps_db_shader_control);
      db_render_control |= S_028000_OREO_MODE(z_export ? V_028000_OMODE_BLEND : V_028000_OMODE_O_THEN_B);
   }

   if (sctx->gfx_level >= GFX12) {
      assert(!sctx->dbcb_depth_copy_enabled && !sctx->dbcb_stencil_copy_enabled);
      assert(!sctx->db_flush_depth_inplace && !sctx->db_flush_stencil_inplace);
      assert(!sctx->db_depth_clear && !sctx->db_stencil_clear);
   } else {
      if (sctx->dbcb_depth_copy_enabled || sctx->dbcb_stencil_copy_enabled) {
         db_render_control |= S_028000_DEPTH_COPY(sctx->dbcb_depth_copy_enabled) |
                              S_028000_STENCIL_COPY(sctx->dbcb_stencil_copy_enabled) |
                              S_028000_COPY_CENTROID(1) | S_028000_COPY_SAMPLE(sctx->dbcb_copy_sample);
      } else if (sctx->db_flush_depth_inplace || sctx->db_flush_stencil_inplace) {
         db_render_control |= S_028000_DEPTH_COMPRESS_DISABLE(sctx->db_flush_depth_inplace) |
                              S_028000_STENCIL_COMPRESS_DISABLE(sctx->db_flush_stencil_inplace);
      } else {
         db_render_control |= S_028000_DEPTH_CLEAR_ENABLE(sctx->db_depth_clear) |
                              S_028000_STENCIL_CLEAR_ENABLE(sctx->db_stencil_clear);
      }

      if (sctx->gfx_level >= GFX11) {
         unsigned max_allowed_tiles_in_wave;

         if (sctx->screen->info.has_dedicated_vram) {
            if (sctx->framebuffer.nr_samples == 8)
               max_allowed_tiles_in_wave = 6;
            else if (sctx->framebuffer.nr_samples == 4)
               max_allowed_tiles_in_wave = 13;
            else
               max_allowed_tiles_in_wave = 0;
         } else {
            if (sctx->framebuffer.nr_samples == 8)
               max_allowed_tiles_in_wave = 7;
            else if (sctx->framebuffer.nr_samples == 4)
               max_allowed_tiles_in_wave = 15;
            else
               max_allowed_tiles_in_wave = 0;
         }

         db_render_control |= S_028000_MAX_ALLOWED_TILES_IN_WAVE(max_allowed_tiles_in_wave);
      }
   }

   /* DB_COUNT_CONTROL (occlusion queries) */
   if (sctx->occlusion_query_mode == SI_OCCLUSION_QUERY_MODE_DISABLE ||
       sctx->occlusion_queries_disabled) {
      /* Occlusion queries disabled. */
      if (sctx->gfx_level >= GFX7)
         db_count_control |= S_028004_ZPASS_ENABLE(0);
      else
         db_count_control |= S_028004_ZPASS_INCREMENT_DISABLE(1);
   } else {
      /* Occlusion queries enabled. */
      if (sctx->gfx_level < GFX12)
         db_count_control |= S_028004_SAMPLE_RATE(sctx->framebuffer.log_samples);

      if (sctx->gfx_level >= GFX7) {
         db_count_control |= S_028004_ZPASS_ENABLE(1) |
                             S_028004_SLICE_EVEN_ENABLE(1) |
                             S_028004_SLICE_ODD_ENABLE(1);
      }

      if (sctx->occlusion_query_mode == SI_OCCLUSION_QUERY_MODE_PRECISE_INTEGER ||
          /* Boolean occlusion queries must set PERFECT_ZPASS_COUNTS for depth-only rendering
           * without depth writes or when depth testing is disabled. */
          (sctx->occlusion_query_mode == SI_OCCLUSION_QUERY_MODE_PRECISE_BOOLEAN &&
           (!sctx->queued.named.dsa->depth_enabled ||
            (!sctx->queued.named.blend->cb_target_mask &&
             !sctx->queued.named.dsa->depth_write_enabled))))
         db_count_control |= S_028004_PERFECT_ZPASS_COUNTS(1);

      if (sctx->gfx_level >= GFX10 &&
          sctx->occlusion_query_mode != SI_OCCLUSION_QUERY_MODE_CONSERVATIVE_BOOLEAN)
         db_count_control |= S_028004_DISABLE_CONSERVATIVE_ZPASS_COUNTS(1);
   }

   /* This should always be set on GFX11. */
   if (sctx->gfx_level >= GFX11)
      db_count_control |= S_028004_DISABLE_CONSERVATIVE_ZPASS_COUNTS(1);

   db_shader_control |= sctx->ps_db_shader_control;

   if (sctx->screen->info.has_export_conflict_bug &&
       sctx->queued.named.blend->blend_enable_4bit &&
       si_get_num_coverage_samples(sctx) == 1) {
      db_shader_control |= S_02880C_OVERRIDE_INTRINSIC_RATE_ENABLE(1) |
                           S_02880C_OVERRIDE_INTRINSIC_RATE(2);
   }

   if (sctx->gfx_level >= GFX10_3) {
      /* Variable rate shading. */
      unsigned mode, log_rate_x, log_rate_y;

      if (sctx->allow_flat_shading) {
         mode = V_028064_SC_VRS_COMB_MODE_OVERRIDE;
         log_rate_x = log_rate_y = 1; /* 2x2 VRS (log2(2) == 1) */
      } else {
         /* If the shader is using discard, turn off coarse shading because discarding at 2x2 pixel
          * granularity degrades quality too much.
          *
          * The shader writes the VRS rate and we either pass it through or do MIN(shader, 1x1)
          * to disable coarse shading.
          */
         mode = sctx->screen->options.vrs2x2 && G_02880C_KILL_ENABLE(db_shader_control) ?
                   V_028064_SC_VRS_COMB_MODE_MIN : V_028064_SC_VRS_COMB_MODE_PASSTHRU;
         log_rate_x = log_rate_y = 0; /* 1x1 VRS (log2(1) == 0) */
      }

      if (sctx->gfx_level >= GFX11) {
         vrs_override_cntl = S_0283D0_VRS_OVERRIDE_RATE_COMBINER_MODE(mode) |
                             S_0283D0_VRS_RATE(log_rate_x * 4 + log_rate_y);
      } else {
         vrs_override_cntl = S_028064_VRS_OVERRIDE_RATE_COMBINER_MODE(mode) |
                             S_028064_VRS_OVERRIDE_RATE_X(log_rate_x) |
                             S_028064_VRS_OVERRIDE_RATE_Y(log_rate_y);
      }
   }

   unsigned db_render_override2 =
         S_028010_DISABLE_ZMASK_EXPCLEAR_OPTIMIZATION(sctx->db_depth_disable_expclear) |
         S_028010_DISABLE_SMEM_EXPCLEAR_OPTIMIZATION(sctx->db_stencil_disable_expclear) |
         S_028010_DECOMPRESS_Z_ON_FLUSH(sctx->framebuffer.nr_samples >= 4) |
         S_028010_CENTROID_COMPUTATION_MODE(sctx->gfx_level >= GFX10_3 ? 1 : 0);

   if (sctx->gfx_level >= GFX12) {
      radeon_begin(&sctx->gfx_cs);
      gfx12_begin_context_regs();
      gfx12_opt_set_context_reg(R_028000_DB_RENDER_CONTROL, SI_TRACKED_DB_RENDER_CONTROL,
                                db_render_control);
      gfx12_opt_set_context_reg(R_028010_DB_RENDER_OVERRIDE2, SI_TRACKED_DB_RENDER_OVERRIDE2,
                                S_028010_DECOMPRESS_Z_ON_FLUSH(sctx->framebuffer.nr_samples >= 4) |
                                S_028010_CENTROID_COMPUTATION_MODE(1));
      gfx12_opt_set_context_reg(R_028060_DB_COUNT_CONTROL, SI_TRACKED_DB_COUNT_CONTROL,
                                db_count_control);
      gfx12_opt_set_context_reg(R_02806C_DB_SHADER_CONTROL, SI_TRACKED_DB_SHADER_CONTROL,
                                db_shader_control);
      gfx12_opt_set_context_reg(R_0283D0_PA_SC_VRS_OVERRIDE_CNTL,
                                SI_TRACKED_DB_PA_SC_VRS_OVERRIDE_CNTL, vrs_override_cntl);
      gfx12_end_context_regs();
      radeon_end(); /* don't track context rolls on GFX12 */
   } else if (sctx->screen->info.has_set_context_pairs_packed) {
      radeon_begin(&sctx->gfx_cs);
      gfx11_begin_packed_context_regs();
      gfx11_opt_set_context_reg(R_028000_DB_RENDER_CONTROL, SI_TRACKED_DB_RENDER_CONTROL,
                                db_render_control);
      gfx11_opt_set_context_reg(R_028004_DB_COUNT_CONTROL, SI_TRACKED_DB_COUNT_CONTROL,
                                db_count_control);
      gfx11_opt_set_context_reg(R_028010_DB_RENDER_OVERRIDE2, SI_TRACKED_DB_RENDER_OVERRIDE2,
                                db_render_override2);
      gfx11_opt_set_context_reg(R_02880C_DB_SHADER_CONTROL, SI_TRACKED_DB_SHADER_CONTROL,
                                db_shader_control);
      gfx11_opt_set_context_reg(R_0283D0_PA_SC_VRS_OVERRIDE_CNTL,
                                SI_TRACKED_DB_PA_SC_VRS_OVERRIDE_CNTL, vrs_override_cntl);
      gfx11_end_packed_context_regs();
      radeon_end(); /* don't track context rolls on GFX11 */
   } else {
      radeon_begin(&sctx->gfx_cs);
      radeon_opt_set_context_reg2(R_028000_DB_RENDER_CONTROL, SI_TRACKED_DB_RENDER_CONTROL,
                                  db_render_control, db_count_control);
      radeon_opt_set_context_reg(R_028010_DB_RENDER_OVERRIDE2,
                                 SI_TRACKED_DB_RENDER_OVERRIDE2, db_render_override2);
      radeon_opt_set_context_reg(R_02880C_DB_SHADER_CONTROL, SI_TRACKED_DB_SHADER_CONTROL,
                                 db_shader_control);

      if (sctx->gfx_level >= GFX11) {
         radeon_opt_set_context_reg(R_0283D0_PA_SC_VRS_OVERRIDE_CNTL,
                                    SI_TRACKED_DB_PA_SC_VRS_OVERRIDE_CNTL, vrs_override_cntl);
      } else if (sctx->gfx_level >= GFX10_3) {
         radeon_opt_set_context_reg(R_028064_DB_VRS_OVERRIDE_CNTL,
                                    SI_TRACKED_DB_PA_SC_VRS_OVERRIDE_CNTL, vrs_override_cntl);
      }
      radeon_end_update_context_roll();
   }
}

/*
 * Texture translation
 */

static uint32_t si_translate_texformat(struct pipe_screen *screen, enum pipe_format format,
                                       const struct util_format_description *desc,
                                       int first_non_void)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   assert(sscreen->info.gfx_level <= GFX9);

   return ac_translate_tex_dataformat(&sscreen->info, desc, first_non_void);
}

static unsigned is_wrap_mode_legal(struct si_screen *screen, unsigned wrap)
{
   if (!screen->info.has_3d_cube_border_color_mipmap) {
      switch (wrap) {
      case PIPE_TEX_WRAP_CLAMP:
      case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
      case PIPE_TEX_WRAP_MIRROR_CLAMP:
      case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
         return false;
      }
   }
   return true;
}

static unsigned si_tex_wrap(unsigned wrap)
{
   switch (wrap) {
   default:
   case PIPE_TEX_WRAP_REPEAT:
      return V_008F30_SQ_TEX_WRAP;
   case PIPE_TEX_WRAP_CLAMP:
      return V_008F30_SQ_TEX_CLAMP_HALF_BORDER;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
      return V_008F30_SQ_TEX_CLAMP_LAST_TEXEL;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
      return V_008F30_SQ_TEX_CLAMP_BORDER;
   case PIPE_TEX_WRAP_MIRROR_REPEAT:
      return V_008F30_SQ_TEX_MIRROR;
   case PIPE_TEX_WRAP_MIRROR_CLAMP:
      return V_008F30_SQ_TEX_MIRROR_ONCE_HALF_BORDER;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
      return V_008F30_SQ_TEX_MIRROR_ONCE_LAST_TEXEL;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
      return V_008F30_SQ_TEX_MIRROR_ONCE_BORDER;
   }
}

static unsigned si_tex_mipfilter(unsigned filter)
{
   switch (filter) {
   case PIPE_TEX_MIPFILTER_NEAREST:
      return V_008F38_SQ_TEX_Z_FILTER_POINT;
   case PIPE_TEX_MIPFILTER_LINEAR:
      return V_008F38_SQ_TEX_Z_FILTER_LINEAR;
   default:
   case PIPE_TEX_MIPFILTER_NONE:
      return V_008F38_SQ_TEX_Z_FILTER_NONE;
   }
}

static unsigned si_tex_compare(unsigned mode, unsigned compare)
{
   if (mode == PIPE_TEX_COMPARE_NONE)
      return V_008F30_SQ_TEX_DEPTH_COMPARE_NEVER;

   switch (compare) {
   default:
   case PIPE_FUNC_NEVER:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_NEVER;
   case PIPE_FUNC_LESS:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_LESS;
   case PIPE_FUNC_EQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_EQUAL;
   case PIPE_FUNC_LEQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_LESSEQUAL;
   case PIPE_FUNC_GREATER:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATER;
   case PIPE_FUNC_NOTEQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_NOTEQUAL;
   case PIPE_FUNC_GEQUAL:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_GREATEREQUAL;
   case PIPE_FUNC_ALWAYS:
      return V_008F30_SQ_TEX_DEPTH_COMPARE_ALWAYS;
   }
}

static unsigned si_tex_dim(struct si_screen *sscreen, struct si_texture *tex, unsigned view_target,
                           unsigned nr_samples)
{
   unsigned res_target = tex->buffer.b.b.target;

   if (view_target == PIPE_TEXTURE_CUBE || view_target == PIPE_TEXTURE_CUBE_ARRAY)
      res_target = view_target;
   /* If interpreting cubemaps as something else, set 2D_ARRAY. */
   else if (res_target == PIPE_TEXTURE_CUBE || res_target == PIPE_TEXTURE_CUBE_ARRAY)
      res_target = PIPE_TEXTURE_2D_ARRAY;

   /* GFX9 allocates 1D textures as 2D. */
   if ((res_target == PIPE_TEXTURE_1D || res_target == PIPE_TEXTURE_1D_ARRAY) &&
       sscreen->info.gfx_level == GFX9 &&
       tex->surface.u.gfx9.resource_type == RADEON_RESOURCE_2D) {
      if (res_target == PIPE_TEXTURE_1D)
         res_target = PIPE_TEXTURE_2D;
      else
         res_target = PIPE_TEXTURE_2D_ARRAY;
   }

   switch (res_target) {
   default:
   case PIPE_TEXTURE_1D:
      return V_008F1C_SQ_RSRC_IMG_1D;
   case PIPE_TEXTURE_1D_ARRAY:
      return V_008F1C_SQ_RSRC_IMG_1D_ARRAY;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
      return nr_samples > 1 ? V_008F1C_SQ_RSRC_IMG_2D_MSAA : V_008F1C_SQ_RSRC_IMG_2D;
   case PIPE_TEXTURE_2D_ARRAY:
      return nr_samples > 1 ? V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY : V_008F1C_SQ_RSRC_IMG_2D_ARRAY;
   case PIPE_TEXTURE_3D:
      return V_008F1C_SQ_RSRC_IMG_3D;
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return V_008F1C_SQ_RSRC_IMG_CUBE;
   }
}

/*
 * Format support testing
 */

static bool si_is_sampler_format_supported(struct pipe_screen *screen, enum pipe_format format)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   const struct util_format_description *desc = util_format_description(format);

   /* Samplers don't support 64 bits per channel. */
   if (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN &&
       desc->channel[0].size == 64)
      return false;

   if (sscreen->info.gfx_level >= GFX10) {
      const struct gfx10_format *fmt = &ac_get_gfx10_format_table(sscreen->info.gfx_level)[format];
      if (!fmt->img_format || fmt->buffers_only)
         return false;
      return true;
   }

   const int first_non_void =  util_format_get_first_non_void_channel(format);

   if (si_translate_texformat(screen, format, desc, first_non_void) == ~0U)
      return false;

   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB &&
       desc->nr_channels != 4 && desc->nr_channels != 1)
      return false;

   if (desc->layout == UTIL_FORMAT_LAYOUT_ETC && !sscreen->info.has_etc_support)
      return false;

   if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED &&
       (desc->format == PIPE_FORMAT_G8B8_G8R8_UNORM ||
        desc->format == PIPE_FORMAT_B8G8_R8G8_UNORM))
      return false;

   /* Other "OTHER" layouts are unsupported. */
   if (desc->layout == UTIL_FORMAT_LAYOUT_OTHER &&
       desc->format != PIPE_FORMAT_R11G11B10_FLOAT &&
       desc->format != PIPE_FORMAT_R9G9B9E5_FLOAT)
      return false;

   /* This must be before using first_non_void. */
   if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
      return true;

   if (first_non_void < 0 || first_non_void > 3)
      return false;

   /* Reject SCALED formats because we don't implement them for CB and do the same for texturing. */
   if ((desc->channel[first_non_void].type == UTIL_FORMAT_TYPE_UNSIGNED ||
        desc->channel[first_non_void].type == UTIL_FORMAT_TYPE_SIGNED) &&
       !desc->channel[first_non_void].normalized &&
       !desc->channel[first_non_void].pure_integer)
      return false;

   /* Reject unsupported 32_*NORM and FIXED formats. */
   if (desc->channel[first_non_void].size == 32 &&
       (desc->channel[first_non_void].normalized ||
        desc->channel[first_non_void].type == UTIL_FORMAT_TYPE_FIXED))
      return false;

   /* Luminace-alpha formats fail tests on Tahiti. */
   if (sscreen->info.gfx_level == GFX6 && util_format_is_luminance_alpha(format))
      return false;

   /* This format fails on Gfx8/Carrizo´. */
   if (sscreen->info.family == CHIP_CARRIZO && format == PIPE_FORMAT_A8R8_UNORM)
      return false;

   /* Reject unsupported 3x 32-bit formats for CB. */
   if (desc->nr_channels == 3 && desc->channel[0].size == 32 && desc->channel[1].size == 32 &&
       desc->channel[2].size == 32)
      return false;

   /* Reject all 64-bit formats. */
   if (desc->channel[first_non_void].size == 64)
      return false;

   return true;
}

static uint32_t si_translate_buffer_dataformat(struct pipe_screen *screen,
                                               const struct util_format_description *desc,
                                               int first_non_void)
{
   assert(((struct si_screen *)screen)->info.gfx_level <= GFX9);

   return ac_translate_buffer_dataformat(desc, first_non_void);
}

static unsigned si_is_vertex_format_supported(struct pipe_screen *screen, enum pipe_format format,
                                              unsigned usage)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   const struct util_format_description *desc;
   int first_non_void;
   unsigned data_format;

   assert((usage & ~(PIPE_BIND_SHADER_IMAGE | PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_VERTEX_BUFFER)) ==
          0);

   desc = util_format_description(format);

   /* There are no native 8_8_8 or 16_16_16 data formats, and we currently
    * select 8_8_8_8 and 16_16_16_16 instead. This works reasonably well
    * for read-only access (with caveats surrounding bounds checks), but
    * obviously fails for write access which we have to implement for
    * shader images. Luckily, OpenGL doesn't expect this to be supported
    * anyway, and so the only impact is on PBO uploads / downloads, which
    * shouldn't be expected to be fast for GL_RGB anyway.
    */
   if (desc->block.bits == 3 * 8 || desc->block.bits == 3 * 16) {
      if (usage & (PIPE_BIND_SHADER_IMAGE | PIPE_BIND_SAMPLER_VIEW)) {
         usage &= ~(PIPE_BIND_SHADER_IMAGE | PIPE_BIND_SAMPLER_VIEW);
         if (!usage)
            return 0;
      }
   }

   if (sscreen->info.gfx_level >= GFX10) {
      const struct gfx10_format *fmt = &ac_get_gfx10_format_table(sscreen->info.gfx_level)[format];
      unsigned first_image_only_format = sscreen->info.gfx_level >= GFX11 ? 64 : 128;

      if (!fmt->img_format || fmt->img_format >= first_image_only_format)
         return 0;
      return usage;
   }

   first_non_void = util_format_get_first_non_void_channel(format);
   data_format = si_translate_buffer_dataformat(screen, desc, first_non_void);
   if (data_format == V_008F0C_BUF_DATA_FORMAT_INVALID)
      return 0;

   return usage;
}

static bool si_is_zs_format_supported(enum pipe_format format)
{
   if (format == PIPE_FORMAT_Z16_UNORM_S8_UINT)
      return false;

   return ac_is_zs_format_supported(format);
}

static bool si_is_reduction_mode_supported(struct pipe_screen *screen, enum pipe_format format)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   return ac_is_reduction_mode_supported(&sscreen->info, format, true);
}

static bool si_is_format_supported(struct pipe_screen *screen, enum pipe_format format,
                                   enum pipe_texture_target target, unsigned sample_count,
                                   unsigned storage_sample_count, unsigned usage)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   unsigned retval = 0;

   if (target >= PIPE_MAX_TEXTURE_TYPES) {
      PRINT_ERR("radeonsi: unsupported texture type %d\n", target);
      return false;
   }

   /* Require PIPE_BIND_SAMPLER_VIEW support when PIPE_BIND_RENDER_TARGET
    * is requested.
    */
   if (usage & PIPE_BIND_RENDER_TARGET)
      usage |= PIPE_BIND_SAMPLER_VIEW;

   if ((target == PIPE_TEXTURE_3D || target == PIPE_TEXTURE_CUBE) &&
        !sscreen->info.has_3d_cube_border_color_mipmap)
      return false;

   if (util_format_get_num_planes(format) >= 2)
      return false;

   if (MAX2(1, sample_count) < MAX2(1, storage_sample_count))
      return false;

   if (sample_count > 1) {
      if (!screen->caps.texture_multisample)
         return false;

      /* Only power-of-two sample counts are supported. */
      if (!util_is_power_of_two_or_zero(sample_count) ||
          !util_is_power_of_two_or_zero(storage_sample_count))
         return false;

      /* Chips with 1 RB don't increment occlusion queries at 16x MSAA sample rate,
       * so don't expose 16 samples there.
       *
       * EQAA also uses max 8 samples because our FMASK fetches only load 32 bits and
       * would need to be changed to 64 bits for 16 samples.
       */
      const unsigned max_samples = 8;

      /* MSAA support without framebuffer attachments. */
      if (format == PIPE_FORMAT_NONE && sample_count <= max_samples)
         return true;

      if (!sscreen->info.has_eqaa_surface_allocator || util_format_is_depth_or_stencil(format)) {
         /* Color without EQAA or depth/stencil. */
         if (sample_count > max_samples || sample_count != storage_sample_count)
            return false;
      } else {
         /* Color with EQAA. */
         if (sample_count > max_samples || storage_sample_count > max_samples)
            return false;
      }
   }

   if (usage & (PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHADER_IMAGE)) {
      if (target == PIPE_BUFFER) {
         retval |= si_is_vertex_format_supported(
            screen, format, usage & (PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHADER_IMAGE));
      } else {
         if (si_is_sampler_format_supported(screen, format))
            retval |= usage & (PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHADER_IMAGE);
      }
   }

   if ((usage & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT |
                 PIPE_BIND_SHARED | PIPE_BIND_BLENDABLE)) &&
       ac_is_colorbuffer_format_supported(sscreen->info.gfx_level, format)) {
      retval |= usage & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SCANOUT |
                         PIPE_BIND_SHARED);
      if (!util_format_is_pure_integer(format) && !util_format_is_depth_or_stencil(format))
         retval |= usage & PIPE_BIND_BLENDABLE;
   }

   if ((usage & PIPE_BIND_DEPTH_STENCIL) && si_is_zs_format_supported(format)) {
      retval |= PIPE_BIND_DEPTH_STENCIL;
   }

   if (usage & PIPE_BIND_VERTEX_BUFFER) {
      retval |= si_is_vertex_format_supported(screen, format, PIPE_BIND_VERTEX_BUFFER);
   }

   if (usage & PIPE_BIND_INDEX_BUFFER) {
      if (format == PIPE_FORMAT_R8_UINT ||
          format == PIPE_FORMAT_R16_UINT ||
          format == PIPE_FORMAT_R32_UINT)
         retval |= PIPE_BIND_INDEX_BUFFER;
   }

   if ((usage & PIPE_BIND_LINEAR) && !util_format_is_compressed(format) &&
       !(usage & PIPE_BIND_DEPTH_STENCIL))
      retval |= PIPE_BIND_LINEAR;

   if ((usage & PIPE_BIND_SAMPLER_REDUCTION_MINMAX) &&
       screen->caps.sampler_reduction_minmax &&
       si_is_reduction_mode_supported(screen, format))
      retval |= PIPE_BIND_SAMPLER_REDUCTION_MINMAX;

   return retval == usage;
}

/*
 * framebuffer handling
 */

static void si_choose_spi_color_formats(struct si_surface *surf, unsigned format, unsigned swap,
                                        unsigned ntype, bool is_depth)
{
   struct ac_spi_color_formats formats = {};

   ac_choose_spi_color_formats(format, swap, ntype, is_depth, true, &formats);

   surf->spi_shader_col_format = formats.normal;
   surf->spi_shader_col_format_alpha = formats.alpha;
   surf->spi_shader_col_format_blend = formats.blend;
   surf->spi_shader_col_format_blend_alpha = formats.blend_alpha;
}

static void si_initialize_color_surface(struct si_context *sctx, struct si_surface *surf)
{
   struct si_texture *tex = (struct si_texture *)surf->base.texture;
   unsigned format, swap, ntype;//, endian;

   ntype = ac_get_cb_number_type(surf->base.format);
   format = ac_get_cb_format(sctx->gfx_level, surf->base.format);

   if (format == V_028C70_COLOR_INVALID) {
      PRINT_ERR("Invalid CB format: %d, disabling CB.\n", surf->base.format);
   }
   assert(format != V_028C70_COLOR_INVALID);
   swap = ac_translate_colorswap(sctx->gfx_level, surf->base.format, false);

   if (ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT) {
      if (format == V_028C70_COLOR_8 || format == V_028C70_COLOR_8_8 ||
          format == V_028C70_COLOR_8_8_8_8)
         surf->color_is_int8 = true;
      else if (format == V_028C70_COLOR_10_10_10_2 || format == V_028C70_COLOR_2_10_10_10)
         surf->color_is_int10 = true;
   }

   const struct ac_cb_state cb_state = {
      .surf = &tex->surface,
      .format = surf->base.format,
      .width = surf->width0,
      .height = surf->height0,
      .first_layer = surf->base.u.tex.first_layer,
      .last_layer = surf->base.u.tex.last_layer,
      .num_layers = util_max_layer(&tex->buffer.b.b, 0),
      .num_samples = tex->buffer.b.b.nr_samples,
      .num_storage_samples = tex->buffer.b.b.nr_storage_samples,
      .base_level = surf->base.u.tex.level,
      .num_levels = tex->buffer.b.b.last_level + 1,
   };

   ac_init_cb_surface(&sctx->screen->info, &cb_state, &surf->cb);

   /* Determine pixel shader export format */
   si_choose_spi_color_formats(surf, format, swap, ntype, tex->is_depth);

   surf->color_initialized = true;
}

static void si_init_depth_surface(struct si_context *sctx, struct si_surface *surf)
{
   struct si_texture *tex = (struct si_texture *)surf->base.texture;
   unsigned level = surf->base.u.tex.level;
   unsigned format;

   format = ac_translate_dbformat(tex->db_render_format);

   assert(format != V_028040_Z_24 || sctx->gfx_level < GFX12);
   assert(format != V_028040_Z_INVALID);

   if (format == V_028040_Z_INVALID)
      PRINT_ERR("Invalid DB format: %d, disabling DB.\n", tex->buffer.b.b.format);

   /* Use the original Z format, not db_render_format, so that the polygon offset behaves as
    * expected by applications.
    */
   switch (tex->buffer.b.b.format) {
   case PIPE_FORMAT_Z16_UNORM:
      surf->db_format_index = 0;
      break;
   default: /* 24-bit */
      surf->db_format_index = 1;
      break;
   case PIPE_FORMAT_Z32_FLOAT:
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      surf->db_format_index = 2;
      break;
   }

   const struct ac_ds_state ds_state = {
      .surf = &tex->surface,
      .va = tex->buffer.gpu_address,
      .format = tex->db_render_format,
      .width = tex->buffer.b.b.width0,
      .height = tex->buffer.b.b.height0,
      .level = level,
      .num_levels = tex->buffer.b.b.last_level + 1,
      .num_samples = tex->buffer.b.b.nr_samples,
      .first_layer = surf->base.u.tex.first_layer,
      .last_layer = surf->base.u.tex.last_layer,
      .allow_expclear = true,
      .htile_enabled = sctx->gfx_level < GFX12 && si_htile_enabled(tex, level, PIPE_MASK_ZS),
      .htile_stencil_disabled = tex->htile_stencil_disabled,
   };

   ac_init_ds_surface(&sctx->screen->info, &ds_state, &surf->ds);

   surf->depth_initialized = true;
}

static void si_dec_framebuffer_counters(const struct pipe_framebuffer_state *state)
{
   for (int i = 0; i < state->nr_cbufs; ++i) {
      struct si_surface *surf = NULL;
      struct si_texture *tex;

      if (!state->cbufs[i])
         continue;
      surf = (struct si_surface *)state->cbufs[i];
      tex = (struct si_texture *)surf->base.texture;

      p_atomic_dec(&tex->framebuffers_bound);
   }
}

void si_mark_display_dcc_dirty(struct si_context *sctx, struct si_texture *tex)
{
   assert(sctx->gfx_level < GFX12);

   if (!tex->surface.display_dcc_offset || tex->displayable_dcc_dirty)
      return;

   if (!(tex->buffer.external_usage & PIPE_HANDLE_USAGE_EXPLICIT_FLUSH)) {
      struct hash_entry *entry = _mesa_hash_table_search(sctx->dirty_implicit_resources, tex);
      if (!entry) {
         struct pipe_resource *dummy = NULL;
         pipe_resource_reference(&dummy, &tex->buffer.b.b);
         _mesa_hash_table_insert(sctx->dirty_implicit_resources, tex, tex);
      }
   }
   tex->displayable_dcc_dirty = true;
}

static void si_update_display_dcc_dirty(struct si_context *sctx)
{
   const struct pipe_framebuffer_state *state = &sctx->framebuffer.state;

   for (unsigned i = 0; i < state->nr_cbufs; i++) {
      if (state->cbufs[i])
         si_mark_display_dcc_dirty(sctx, (struct si_texture *)state->cbufs[i]->texture);
   }
}

static void si_set_framebuffer_state(struct pipe_context *ctx,
                                     const struct pipe_framebuffer_state *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_surface *surf = NULL;
   struct si_texture *tex;
   bool old_any_dst_linear = sctx->framebuffer.any_dst_linear;
   unsigned old_nr_samples = sctx->framebuffer.nr_samples;
   unsigned old_colorbuf_enabled_4bit = sctx->framebuffer.colorbuf_enabled_4bit;
   bool old_has_zsbuf = !!sctx->framebuffer.state.zsbuf;
   bool old_has_stencil =
      old_has_zsbuf &&
      ((struct si_texture *)sctx->framebuffer.state.zsbuf->texture)->surface.has_stencil;
   uint8_t old_db_format_index =
      old_has_zsbuf ?
      ((struct si_surface *)sctx->framebuffer.state.zsbuf)->db_format_index : -1;
   bool old_has_hiz_his = sctx->framebuffer.has_hiz_his;
   int i;

   /* Reject zero-sized framebuffers due to a hw bug on GFX6 that occurs
    * when PA_SU_HARDWARE_SCREEN_OFFSET != 0 and any_scissor.BR_X/Y <= 0.
    * We could implement the full workaround here, but it's a useless case.
    */
   if ((!state->width || !state->height) && (state->nr_cbufs || state->zsbuf)) {
      unreachable("the framebuffer shouldn't have zero area");
      return;
   }

   si_fb_barrier_after_rendering(sctx, SI_FB_BARRIER_SYNC_ALL);

   /* Disable DCC if the formats are incompatible. */
   if (sctx->gfx_level >= GFX8 && sctx->gfx_level < GFX11) {
      for (i = 0; i < state->nr_cbufs; i++) {
         if (!state->cbufs[i])
            continue;

         surf = (struct si_surface *)state->cbufs[i];
         tex = (struct si_texture *)surf->base.texture;

         if (!surf->dcc_incompatible)
            continue;

         if (vi_dcc_enabled(tex, surf->base.u.tex.level))
            if (!si_texture_disable_dcc(sctx, tex))
               si_decompress_dcc(sctx, tex);

         surf->dcc_incompatible = false;
      }
   }

   /* Take the maximum of the old and new count. If the new count is lower,
    * dirtying is needed to disable the unbound colorbuffers.
    */
   sctx->framebuffer.dirty_cbufs |=
      (1 << MAX2(sctx->framebuffer.state.nr_cbufs, state->nr_cbufs)) - 1;
   sctx->framebuffer.dirty_zsbuf |= sctx->framebuffer.state.zsbuf != state->zsbuf;

   si_dec_framebuffer_counters(&sctx->framebuffer.state);
   util_copy_framebuffer_state(&sctx->framebuffer.state, state);

   /* The framebuffer state must be set before the barrier. */
   si_fb_barrier_before_rendering(sctx);

   /* Recompute layers because frontends and utils might not set it. */
   sctx->framebuffer.state.layers = util_framebuffer_get_num_layers(state);

   sctx->framebuffer.colorbuf_enabled_4bit = 0;
   sctx->framebuffer.spi_shader_col_format = 0;
   sctx->framebuffer.spi_shader_col_format_alpha = 0;
   sctx->framebuffer.spi_shader_col_format_blend = 0;
   sctx->framebuffer.spi_shader_col_format_blend_alpha = 0;
   sctx->framebuffer.color_is_int8 = 0;
   sctx->framebuffer.color_is_int10 = 0;

   sctx->framebuffer.compressed_cb_mask = 0;
   sctx->framebuffer.uncompressed_cb_mask = 0;
   sctx->framebuffer.nr_samples = util_framebuffer_get_num_samples(state);
   sctx->framebuffer.nr_color_samples = sctx->framebuffer.nr_samples;
   sctx->framebuffer.log_samples = util_logbase2(sctx->framebuffer.nr_samples);
   sctx->framebuffer.any_dst_linear = false;
   sctx->framebuffer.CB_has_shader_readable_metadata = false;
   sctx->framebuffer.DB_has_shader_readable_metadata = false;
   sctx->framebuffer.all_DCC_pipe_aligned = true;
   sctx->framebuffer.has_dcc_msaa = false;
   sctx->framebuffer.min_bytes_per_pixel = 0;
   sctx->framebuffer.disable_vrs_flat_shading = false;
   sctx->framebuffer.has_stencil = false;
   sctx->framebuffer.has_hiz_his = false;

   for (i = 0; i < state->nr_cbufs; i++) {
      if (!state->cbufs[i])
         continue;

      surf = (struct si_surface *)state->cbufs[i];
      tex = (struct si_texture *)surf->base.texture;

      if (!surf->color_initialized) {
         si_initialize_color_surface(sctx, surf);
      }

      sctx->framebuffer.colorbuf_enabled_4bit |= 0xf << (i * 4);
      sctx->framebuffer.spi_shader_col_format |= surf->spi_shader_col_format << (i * 4);
      sctx->framebuffer.spi_shader_col_format_alpha |= surf->spi_shader_col_format_alpha << (i * 4);
      sctx->framebuffer.spi_shader_col_format_blend |= surf->spi_shader_col_format_blend << (i * 4);
      sctx->framebuffer.spi_shader_col_format_blend_alpha |= surf->spi_shader_col_format_blend_alpha
                                                             << (i * 4);

      if (surf->color_is_int8)
         sctx->framebuffer.color_is_int8 |= 1 << i;
      if (surf->color_is_int10)
         sctx->framebuffer.color_is_int10 |= 1 << i;

      if (tex->surface.fmask_offset)
         sctx->framebuffer.compressed_cb_mask |= 1 << i;
      else
         sctx->framebuffer.uncompressed_cb_mask |= 1 << i;

      /* Don't update nr_color_samples for non-AA buffers.
       * (e.g. destination of MSAA resolve)
       */
      if (tex->buffer.b.b.nr_samples >= 2 &&
          tex->buffer.b.b.nr_storage_samples < tex->buffer.b.b.nr_samples) {
         sctx->framebuffer.nr_color_samples =
            MIN2(sctx->framebuffer.nr_color_samples, tex->buffer.b.b.nr_storage_samples);
         sctx->framebuffer.nr_color_samples = MAX2(1, sctx->framebuffer.nr_color_samples);
      }

      if (tex->surface.is_linear)
         sctx->framebuffer.any_dst_linear = true;

      if (vi_dcc_enabled(tex, surf->base.u.tex.level)) {
         sctx->framebuffer.CB_has_shader_readable_metadata = true;

         if (sctx->gfx_level >= GFX9 && sctx->gfx_level < GFX12 &&
             !tex->surface.u.gfx9.color.dcc.pipe_aligned)
            sctx->framebuffer.all_DCC_pipe_aligned = false;

         if (tex->buffer.b.b.nr_storage_samples >= 2)
            sctx->framebuffer.has_dcc_msaa = true;
      }

      p_atomic_inc(&tex->framebuffers_bound);

      /* Update the minimum but don't keep 0. */
      if (!sctx->framebuffer.min_bytes_per_pixel ||
          tex->surface.bpe < sctx->framebuffer.min_bytes_per_pixel)
         sctx->framebuffer.min_bytes_per_pixel = tex->surface.bpe;

      /* Disable VRS flat shading where it decreases performance.
       * This gives the best results for slow clears for AMD_TEST=blitperf on Navi31.
       */
      if ((sctx->framebuffer.nr_samples == 8 && tex->surface.bpe != 2) ||
          (tex->surface.thick_tiling && tex->surface.bpe == 4 &&
           util_format_get_nr_components(surf->base.format) == 4))
         sctx->framebuffer.disable_vrs_flat_shading = true;
   }

   struct si_texture *zstex = NULL;

   if (state->zsbuf) {
      surf = (struct si_surface *)state->zsbuf;
      zstex = (struct si_texture *)surf->base.texture;

      if (!surf->depth_initialized) {
         si_init_depth_surface(sctx, surf);
      }

      if (sctx->gfx_level < GFX12 &&
          vi_tc_compat_htile_enabled(zstex, surf->base.u.tex.level, PIPE_MASK_ZS))
         sctx->framebuffer.DB_has_shader_readable_metadata = true;

      /* Update the minimum but don't keep 0. */
      if (!sctx->framebuffer.min_bytes_per_pixel ||
          zstex->surface.bpe < sctx->framebuffer.min_bytes_per_pixel)
         sctx->framebuffer.min_bytes_per_pixel = zstex->surface.bpe;

      /* Update polygon offset based on the Z format. */
      if (sctx->queued.named.rasterizer->uses_poly_offset &&
          surf->db_format_index != old_db_format_index)
         sctx->dirty_atoms |= SI_STATE_BIT(rasterizer);

      if (util_format_has_stencil(util_format_description(zstex->buffer.b.b.format)))
         sctx->framebuffer.has_stencil = true;

      if (sctx->gfx_level == GFX12 && !sctx->screen->options.alt_hiz_logic &&
          sctx->framebuffer.has_stencil && sctx->queued.named.dsa->stencil_enabled)
         zstex->force_disable_hiz_his = true;

      if (sctx->gfx_level >= GFX12) {
         sctx->framebuffer.has_hiz_his = (zstex->surface.u.gfx9.zs.hiz.offset ||
                                          zstex->surface.u.gfx9.zs.his.offset) &&
                                         !zstex->force_disable_hiz_his;
      }
   }

   si_update_ps_colorbuf0_slot(sctx);
   si_mark_atom_dirty(sctx, &sctx->atoms.s.cb_render_state);
   si_mark_atom_dirty(sctx, &sctx->atoms.s.framebuffer);

   /* NGG cull state uses the sample count. */
   if (sctx->screen->use_ngg_culling)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.ngg_cull_state);

   if (sctx->screen->dpbb_allowed)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.dpbb_state);

   if (sctx->framebuffer.any_dst_linear != old_any_dst_linear ||
       sctx->framebuffer.has_hiz_his != old_has_hiz_his)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);

   if (sctx->screen->info.has_out_of_order_rast &&
       (sctx->framebuffer.colorbuf_enabled_4bit != old_colorbuf_enabled_4bit ||
        !!sctx->framebuffer.state.zsbuf != old_has_zsbuf ||
        (zstex && zstex->surface.has_stencil != old_has_stencil)))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);

   if (sctx->framebuffer.nr_samples != old_nr_samples) {
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);
      si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);
      si_mark_atom_dirty(sctx, &sctx->atoms.s.sample_locations);
   }

   si_ps_key_update_framebuffer(sctx);
   si_ps_key_update_framebuffer_blend_dsa_rasterizer(sctx);
   si_ps_key_update_framebuffer_rasterizer_sample_shading(sctx);
   si_ps_key_update_sample_shading(sctx);
   si_vs_ps_key_update_rast_prim_smooth_stipple(sctx);
   si_update_ps_inputs_read_or_disabled(sctx);
   si_update_vrs_flat_shading(sctx);
   sctx->do_update_shaders = true;

   if (sctx->gfx_level < GFX12 && !sctx->decompression_enabled) {
      /* Prevent textures decompression when the framebuffer state
       * changes come from the decompression passes themselves.
       */
      sctx->need_check_render_feedback = true;
   }
}

static void gfx6_emit_framebuffer_state(struct si_context *sctx, unsigned index)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct pipe_framebuffer_state *state = &sctx->framebuffer.state;
   unsigned i, nr_cbufs = state->nr_cbufs;
   struct si_texture *tex = NULL;
   struct si_surface *cb = NULL;
   bool is_msaa_resolve = state->nr_cbufs == 2 &&
                          state->cbufs[0] && state->cbufs[0]->texture->nr_samples > 1 &&
                          state->cbufs[1] && state->cbufs[1]->texture->nr_samples <= 1;

   /* CB can't do MSAA resolve on gfx11. */
   assert(!is_msaa_resolve || sctx->gfx_level < GFX11);

   radeon_begin(cs);

   /* Colorbuffers. */
   for (i = 0; i < nr_cbufs; i++) {
      if (!(sctx->framebuffer.dirty_cbufs & (1 << i)))
         continue;

      /* RB+ depth-only rendering. See the comment where we set rbplus_depth_only_opt for more
       * information.
       */
      if (i == 0 &&
          sctx->screen->info.rbplus_allowed &&
          !sctx->queued.named.blend->cb_target_mask) {
         radeon_set_context_reg(R_028C70_CB_COLOR0_INFO + i * 0x3C,
                                (sctx->gfx_level >= GFX11 ?
                                   S_028C70_FORMAT_GFX11(V_028C70_COLOR_32) :
                                   S_028C70_FORMAT_GFX6(V_028C70_COLOR_32)) |
                                S_028C70_NUMBER_TYPE(V_028C70_NUMBER_FLOAT));
         continue;
      }

      cb = (struct si_surface *)state->cbufs[i];
      if (!cb) {
         radeon_set_context_reg(R_028C70_CB_COLOR0_INFO + i * 0x3C,
                                sctx->gfx_level >= GFX11 ?
                                   S_028C70_FORMAT_GFX11(V_028C70_COLOR_INVALID) :
                                   S_028C70_FORMAT_GFX6(V_028C70_COLOR_INVALID));
         continue;
      }

      tex = (struct si_texture *)cb->base.texture;
      radeon_add_to_buffer_list(
         sctx, &sctx->gfx_cs, &tex->buffer, RADEON_USAGE_READWRITE | RADEON_USAGE_CB_NEEDS_IMPLICIT_SYNC |
         (tex->buffer.b.b.nr_samples > 1 ? RADEON_PRIO_COLOR_BUFFER_MSAA : RADEON_PRIO_COLOR_BUFFER));

      if (tex->cmask_buffer && tex->cmask_buffer != &tex->buffer) {
         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, tex->cmask_buffer,
                                   RADEON_USAGE_READWRITE | RADEON_USAGE_CB_NEEDS_IMPLICIT_SYNC |
                                   RADEON_PRIO_SEPARATE_META);
      }

      /* Compute mutable surface parameters. */
      const struct ac_mutable_cb_state mutable_cb_state = {
         .surf = &tex->surface,
         .cb = &cb->cb,
         .va = tex->buffer.gpu_address,
         .base_level = cb->base.u.tex.level,
         .num_samples = cb->base.texture->nr_samples,
         .fmask_enabled = !!tex->surface.fmask_offset,
         /* CMASK and fast clears are configured elsewhere. */
         .cmask_enabled = false,
         .fast_clear_enabled = false,
         .dcc_enabled = vi_dcc_enabled(tex, cb->base.u.tex.level) &&
                        (i != 1 || !is_msaa_resolve),
      };
      struct ac_cb_surface cb_surf;

      ac_set_mutable_cb_surface_fields(&sctx->screen->info, &mutable_cb_state, &cb_surf);

      cb_surf.cb_color_info |= tex->cb_color_info;

      if (sctx->gfx_level < GFX11) {
         if (tex->swap_rgb_to_bgr) {
            /* Swap R and B channels. */
            static unsigned rgb_to_bgr[4] = {
               [V_028C70_SWAP_STD] = V_028C70_SWAP_ALT,
               [V_028C70_SWAP_ALT] = V_028C70_SWAP_STD,
               [V_028C70_SWAP_STD_REV] = V_028C70_SWAP_ALT_REV,
               [V_028C70_SWAP_ALT_REV] = V_028C70_SWAP_STD_REV,
            };
            unsigned swap = rgb_to_bgr[G_028C70_COMP_SWAP(cb_surf.cb_color_info)];

            cb_surf.cb_color_info &= C_028C70_COMP_SWAP;
            cb_surf.cb_color_info |= S_028C70_COMP_SWAP(swap);
         }

         if (cb->base.u.tex.level > 0)
            cb_surf.cb_color_info &= C_028C70_FAST_CLEAR;
         else
            cb_surf.cb_color_cmask = tex->cmask_base_address_reg;
      }

      if (sctx->gfx_level >= GFX11) {
         radeon_set_context_reg(R_028C60_CB_COLOR0_BASE + i * 0x3C, cb_surf.cb_color_base);

         radeon_set_context_reg_seq(R_028C6C_CB_COLOR0_VIEW + i * 0x3C, 4);
         radeon_emit(cb_surf.cb_color_view);                      /* CB_COLOR0_VIEW */
         radeon_emit(cb_surf.cb_color_info);                          /* CB_COLOR0_INFO */
         radeon_emit(cb_surf.cb_color_attrib);                    /* CB_COLOR0_ATTRIB */
         radeon_emit(cb_surf.cb_dcc_control);                        /* CB_COLOR0_FDCC_CONTROL */

         radeon_set_context_reg(R_028C94_CB_COLOR0_DCC_BASE + i * 0x3C, cb_surf.cb_dcc_base);
         radeon_set_context_reg(R_028E40_CB_COLOR0_BASE_EXT + i * 4, cb_surf.cb_color_base >> 32);
         radeon_set_context_reg(R_028EA0_CB_COLOR0_DCC_BASE_EXT + i * 4, cb_surf.cb_dcc_base >> 32);
         radeon_set_context_reg(R_028EC0_CB_COLOR0_ATTRIB2 + i * 4, cb_surf.cb_color_attrib2);
         radeon_set_context_reg(R_028EE0_CB_COLOR0_ATTRIB3 + i * 4, cb_surf.cb_color_attrib3);
      } else if (sctx->gfx_level >= GFX10) {
         radeon_set_context_reg_seq(R_028C60_CB_COLOR0_BASE + i * 0x3C, 14);
         radeon_emit(cb_surf.cb_color_base);             /* CB_COLOR0_BASE */
         radeon_emit(0);                         /* hole */
         radeon_emit(0);                         /* hole */
         radeon_emit(cb_surf.cb_color_view);         /* CB_COLOR0_VIEW */
         radeon_emit(cb_surf.cb_color_info);             /* CB_COLOR0_INFO */
         radeon_emit(cb_surf.cb_color_attrib);       /* CB_COLOR0_ATTRIB */
         radeon_emit(cb_surf.cb_dcc_control);        /* CB_COLOR0_DCC_CONTROL */
         radeon_emit(cb_surf.cb_color_cmask);            /* CB_COLOR0_CMASK */
         radeon_emit(0);                         /* hole */
         radeon_emit(cb_surf.cb_color_fmask);            /* CB_COLOR0_FMASK */
         radeon_emit(0);                         /* hole */
         radeon_emit(tex->color_clear_value[0]); /* CB_COLOR0_CLEAR_WORD0 */
         radeon_emit(tex->color_clear_value[1]); /* CB_COLOR0_CLEAR_WORD1 */
         radeon_emit(cb_surf.cb_dcc_base);               /* CB_COLOR0_DCC_BASE */

         radeon_set_context_reg(R_028E40_CB_COLOR0_BASE_EXT + i * 4, cb_surf.cb_color_base >> 32);
         radeon_set_context_reg(R_028E60_CB_COLOR0_CMASK_BASE_EXT + i * 4,
                                cb_surf.cb_color_cmask >> 32);
         radeon_set_context_reg(R_028E80_CB_COLOR0_FMASK_BASE_EXT + i * 4,
                                cb_surf.cb_color_fmask >> 32);
         radeon_set_context_reg(R_028EA0_CB_COLOR0_DCC_BASE_EXT + i * 4, cb_surf.cb_dcc_base >> 32);
         radeon_set_context_reg(R_028EC0_CB_COLOR0_ATTRIB2 + i * 4, cb_surf.cb_color_attrib2);
         radeon_set_context_reg(R_028EE0_CB_COLOR0_ATTRIB3 + i * 4, cb_surf.cb_color_attrib3);
      } else if (sctx->gfx_level == GFX9) {
         radeon_set_context_reg_seq(R_028C60_CB_COLOR0_BASE + i * 0x3C, 15);
         radeon_emit(cb_surf.cb_color_base);                            /* CB_COLOR0_BASE */
         radeon_emit(S_028C64_BASE_256B(cb_surf.cb_color_base >> 32));  /* CB_COLOR0_BASE_EXT */
         radeon_emit(cb_surf.cb_color_attrib2);                     /* CB_COLOR0_ATTRIB2 */
         radeon_emit(cb_surf.cb_color_view);                        /* CB_COLOR0_VIEW */
         radeon_emit(cb_surf.cb_color_info);                            /* CB_COLOR0_INFO */
         radeon_emit(cb_surf.cb_color_attrib);                          /* CB_COLOR0_ATTRIB */
         radeon_emit(cb_surf.cb_dcc_control);                       /* CB_COLOR0_DCC_CONTROL */
         radeon_emit(cb_surf.cb_color_cmask);                           /* CB_COLOR0_CMASK */
         radeon_emit(S_028C80_BASE_256B(cb_surf.cb_color_cmask >> 32)); /* CB_COLOR0_CMASK_BASE_EXT */
         radeon_emit(cb_surf.cb_color_fmask);                           /* CB_COLOR0_FMASK */
         radeon_emit(S_028C88_BASE_256B(cb_surf.cb_color_fmask >> 32)); /* CB_COLOR0_FMASK_BASE_EXT */
         radeon_emit(tex->color_clear_value[0]);                /* CB_COLOR0_CLEAR_WORD0 */
         radeon_emit(tex->color_clear_value[1]);                /* CB_COLOR0_CLEAR_WORD1 */
         radeon_emit(cb_surf.cb_dcc_base);                              /* CB_COLOR0_DCC_BASE */
         radeon_emit(S_028C98_BASE_256B(cb_surf.cb_dcc_base >> 32));    /* CB_COLOR0_DCC_BASE_EXT */

         radeon_set_context_reg(R_0287A0_CB_MRT0_EPITCH + i * 4, cb_surf.cb_mrt_epitch);
      } else {
         /* GFX6-8 */
         radeon_set_context_reg_seq(R_028C60_CB_COLOR0_BASE + i * 0x3C,
                                    sctx->gfx_level >= GFX8 ? 14 : 13);
         radeon_emit(cb_surf.cb_color_base);                              /* CB_COLOR0_BASE */
         radeon_emit(cb_surf.cb_color_pitch);                             /* CB_COLOR0_PITCH */
         radeon_emit(cb_surf.cb_color_slice);                             /* CB_COLOR0_SLICE */
         radeon_emit(cb_surf.cb_color_view);                          /* CB_COLOR0_VIEW */
         radeon_emit(cb_surf.cb_color_info);                              /* CB_COLOR0_INFO */
         radeon_emit(cb_surf.cb_color_attrib);                            /* CB_COLOR0_ATTRIB */
         radeon_emit(cb_surf.cb_dcc_control);                         /* CB_COLOR0_DCC_CONTROL */
         radeon_emit(cb_surf.cb_color_cmask);                             /* CB_COLOR0_CMASK */
         radeon_emit(tex->surface.u.legacy.color.cmask_slice_tile_max); /* CB_COLOR0_CMASK_SLICE */
         radeon_emit(cb_surf.cb_color_fmask);                             /* CB_COLOR0_FMASK */
         radeon_emit(cb_surf.cb_color_fmask_slice);                       /* CB_COLOR0_FMASK_SLICE */
         radeon_emit(tex->color_clear_value[0]);                  /* CB_COLOR0_CLEAR_WORD0 */
         radeon_emit(tex->color_clear_value[1]);                  /* CB_COLOR0_CLEAR_WORD1 */

         if (sctx->gfx_level >= GFX8) /* R_028C94_CB_COLOR0_DCC_BASE */
            radeon_emit(cb_surf.cb_dcc_base);
      }
   }
   for (; i < 8; i++)
      if (sctx->framebuffer.dirty_cbufs & (1 << i))
         radeon_set_context_reg(R_028C70_CB_COLOR0_INFO + i * 0x3C, 0);

   /* ZS buffer. */
   if (state->zsbuf && sctx->framebuffer.dirty_zsbuf) {
      struct si_surface *zb = (struct si_surface *)state->zsbuf;
      struct si_texture *tex = (struct si_texture *)zb->base.texture;

      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, &tex->buffer, RADEON_USAGE_READWRITE |
                                (zb->base.texture->nr_samples > 1 ? RADEON_PRIO_DEPTH_BUFFER_MSAA
                                                                  : RADEON_PRIO_DEPTH_BUFFER));

      const unsigned level = zb->base.u.tex.level;

      /* Set mutable fields. */
      const struct ac_mutable_ds_state mutable_ds_state = {
         .ds = &zb->ds,
         .format = tex->db_render_format,
         .tc_compat_htile_enabled = vi_tc_compat_htile_enabled(tex, level, PIPE_MASK_ZS),
         .zrange_precision = tex->depth_clear_value[level] != 0,
      };
      struct ac_ds_surface ds;

      ac_set_mutable_ds_surface_fields(&sctx->screen->info, &mutable_ds_state, &ds);

      if (sctx->gfx_level >= GFX10) {
         radeon_set_context_reg(R_028014_DB_HTILE_DATA_BASE, ds.u.gfx6.db_htile_data_base);
         radeon_set_context_reg(R_02801C_DB_DEPTH_SIZE_XY, ds.db_depth_size);

         if (sctx->gfx_level >= GFX11) {
            radeon_set_context_reg_seq(R_028040_DB_Z_INFO, 6);
         } else {
            radeon_set_context_reg_seq(R_02803C_DB_DEPTH_INFO, 7);
            radeon_emit(S_02803C_RESOURCE_LEVEL(1)); /* DB_DEPTH_INFO */
         }
         radeon_emit(ds.db_z_info);                  /* DB_Z_INFO */
         radeon_emit(ds.db_stencil_info);     /* DB_STENCIL_INFO */
         radeon_emit(ds.db_depth_base);   /* DB_Z_READ_BASE */
         radeon_emit(ds.db_stencil_base); /* DB_STENCIL_READ_BASE */
         radeon_emit(ds.db_depth_base);   /* DB_Z_WRITE_BASE */
         radeon_emit(ds.db_stencil_base); /* DB_STENCIL_WRITE_BASE */

         radeon_set_context_reg_seq(R_028068_DB_Z_READ_BASE_HI, 5);
         radeon_emit(ds.db_depth_base >> 32);      /* DB_Z_READ_BASE_HI */
         radeon_emit(ds.db_stencil_base >> 32);    /* DB_STENCIL_READ_BASE_HI */
         radeon_emit(ds.db_depth_base >> 32);      /* DB_Z_WRITE_BASE_HI */
         radeon_emit(ds.db_stencil_base >> 32);    /* DB_STENCIL_WRITE_BASE_HI */
         radeon_emit(ds.u.gfx6.db_htile_data_base >> 32); /* DB_HTILE_DATA_BASE_HI */
      } else if (sctx->gfx_level == GFX9) {
         radeon_set_context_reg_seq(R_028014_DB_HTILE_DATA_BASE, 3);
         radeon_emit(ds.u.gfx6.db_htile_data_base); /* DB_HTILE_DATA_BASE */
         radeon_emit(S_028018_BASE_HI(ds.u.gfx6.db_htile_data_base >> 32)); /* DB_HTILE_DATA_BASE_HI */
         radeon_emit(ds.db_depth_size);                          /* DB_DEPTH_SIZE */

         radeon_set_context_reg_seq(R_028038_DB_Z_INFO, 10);
         radeon_emit(ds.db_z_info);                                   /* DB_Z_INFO */
         radeon_emit(ds.db_stencil_info);                             /* DB_STENCIL_INFO */
         radeon_emit(ds.db_depth_base);                           /* DB_Z_READ_BASE */
         radeon_emit(S_028044_BASE_HI(ds.db_depth_base >> 32));   /* DB_Z_READ_BASE_HI */
         radeon_emit(ds.db_stencil_base);                         /* DB_STENCIL_READ_BASE */
         radeon_emit(S_02804C_BASE_HI(ds.db_stencil_base >> 32)); /* DB_STENCIL_READ_BASE_HI */
         radeon_emit(ds.db_depth_base);                           /* DB_Z_WRITE_BASE */
         radeon_emit(S_028054_BASE_HI(ds.db_depth_base >> 32));   /* DB_Z_WRITE_BASE_HI */
         radeon_emit(ds.db_stencil_base);                         /* DB_STENCIL_WRITE_BASE */
         radeon_emit(S_02805C_BASE_HI(ds.db_stencil_base >> 32)); /* DB_STENCIL_WRITE_BASE_HI */

         radeon_set_context_reg_seq(R_028068_DB_Z_INFO2, 2);
         radeon_emit(ds.u.gfx6.db_z_info2);       /* DB_Z_INFO2 */
         radeon_emit(ds.u.gfx6.db_stencil_info2); /* DB_STENCIL_INFO2 */
      } else {
         /* GFX6-GFX8 */
         radeon_set_context_reg(R_028014_DB_HTILE_DATA_BASE, ds.u.gfx6.db_htile_data_base);

         radeon_set_context_reg_seq(R_02803C_DB_DEPTH_INFO, 9);
         radeon_emit(ds.u.gfx6.db_depth_info);   /* DB_DEPTH_INFO */
         radeon_emit(ds.db_z_info);           /* DB_Z_INFO */
         radeon_emit(ds.db_stencil_info);     /* DB_STENCIL_INFO */
         radeon_emit(ds.db_depth_base);   /* DB_Z_READ_BASE */
         radeon_emit(ds.db_stencil_base); /* DB_STENCIL_READ_BASE */
         radeon_emit(ds.db_depth_base);   /* DB_Z_WRITE_BASE */
         radeon_emit(ds.db_stencil_base); /* DB_STENCIL_WRITE_BASE */
         radeon_emit(ds.db_depth_size);   /* DB_DEPTH_SIZE */
         radeon_emit(ds.u.gfx6.db_depth_slice);  /* DB_DEPTH_SLICE */
      }

      radeon_set_context_reg_seq(R_028028_DB_STENCIL_CLEAR, 2);
      radeon_emit(tex->stencil_clear_value[level]);    /* R_028028_DB_STENCIL_CLEAR */
      radeon_emit(fui(tex->depth_clear_value[level])); /* R_02802C_DB_DEPTH_CLEAR */

      radeon_set_context_reg(R_028008_DB_DEPTH_VIEW, ds.db_depth_view);
      radeon_set_context_reg(R_028ABC_DB_HTILE_SURFACE, ds.u.gfx6.db_htile_surface);
   } else if (sctx->framebuffer.dirty_zsbuf) {
      if (sctx->gfx_level == GFX9)
         radeon_set_context_reg_seq(R_028038_DB_Z_INFO, 2);
      else
         radeon_set_context_reg_seq(R_028040_DB_Z_INFO, 2);

      /* Gfx11+: DB_Z_INFO.NUM_SAMPLES should match the framebuffer samples if no Z/S is bound.
       * It determines the sample count for VRS, primitive-ordered pixel shading, and occlusion
       * queries.
       */
      radeon_emit(S_028040_FORMAT(V_028040_Z_INVALID) |       /* DB_Z_INFO */
                  S_028040_NUM_SAMPLES(sctx->gfx_level >= GFX11 ? sctx->framebuffer.log_samples : 0));
      radeon_emit(S_028044_FORMAT(V_028044_STENCIL_INVALID)); /* DB_STENCIL_INFO */
   }

   /* Framebuffer dimensions. */
   /* PA_SC_WINDOW_SCISSOR_TL is set to 0,0 in gfx*_init_gfx_preamble_state */
   radeon_set_context_reg(R_028208_PA_SC_WINDOW_SCISSOR_BR,
                          S_028208_BR_X(state->width) | S_028208_BR_Y(state->height));

   if (sctx->screen->dpbb_allowed &&
       sctx->screen->pbb_context_states_per_bin > 1)
      radeon_event_write(V_028A90_BREAK_BATCH);

   radeon_end();

   si_update_display_dcc_dirty(sctx);

   sctx->framebuffer.dirty_cbufs = 0;
   sctx->framebuffer.dirty_zsbuf = false;
}

static void gfx11_dgpu_emit_framebuffer_state(struct si_context *sctx, unsigned index)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct pipe_framebuffer_state *state = &sctx->framebuffer.state;
   unsigned i, nr_cbufs = state->nr_cbufs;
   struct si_texture *tex = NULL;
   struct si_surface *cb = NULL;
   bool is_msaa_resolve = state->nr_cbufs == 2 &&
                          state->cbufs[0] && state->cbufs[0]->texture->nr_samples > 1 &&
                          state->cbufs[1] && state->cbufs[1]->texture->nr_samples <= 1;

   /* CB can't do MSAA resolve on gfx11. */
   assert(!is_msaa_resolve);

   radeon_begin(cs);
   gfx11_begin_packed_context_regs();

   /* Colorbuffers. */
   for (i = 0; i < nr_cbufs; i++) {
      if (!(sctx->framebuffer.dirty_cbufs & (1 << i)))
         continue;

      /* RB+ depth-only rendering. See the comment where we set rbplus_depth_only_opt for more
       * information.
       */
      if (i == 0 &&
          sctx->screen->info.rbplus_allowed &&
          !sctx->queued.named.blend->cb_target_mask) {
         gfx11_set_context_reg(R_028C70_CB_COLOR0_INFO + i * 0x3C,
                               S_028C70_FORMAT_GFX11(V_028C70_COLOR_32) |
                               S_028C70_NUMBER_TYPE(V_028C70_NUMBER_FLOAT));
         continue;
      }

      cb = (struct si_surface *)state->cbufs[i];
      if (!cb) {
         gfx11_set_context_reg(R_028C70_CB_COLOR0_INFO + i * 0x3C,
                               S_028C70_FORMAT_GFX11(V_028C70_COLOR_INVALID));
         continue;
      }

      tex = (struct si_texture *)cb->base.texture;
      radeon_add_to_buffer_list(
         sctx, &sctx->gfx_cs, &tex->buffer, RADEON_USAGE_READWRITE | RADEON_USAGE_CB_NEEDS_IMPLICIT_SYNC |
         (tex->buffer.b.b.nr_samples > 1 ? RADEON_PRIO_COLOR_BUFFER_MSAA : RADEON_PRIO_COLOR_BUFFER));

      if (tex->cmask_buffer && tex->cmask_buffer != &tex->buffer) {
         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, tex->cmask_buffer,
                                   RADEON_USAGE_READWRITE | RADEON_USAGE_CB_NEEDS_IMPLICIT_SYNC |
                                   RADEON_PRIO_SEPARATE_META);
      }

      /* Compute mutable surface parameters. */
      const struct ac_mutable_cb_state mutable_cb_state = {
         .surf = &tex->surface,
         .cb = &cb->cb,
         .va = tex->buffer.gpu_address,
         .num_samples = cb->base.texture->nr_samples,
         .dcc_enabled = vi_dcc_enabled(tex, cb->base.u.tex.level),
      };
      struct ac_cb_surface cb_surf;

      ac_set_mutable_cb_surface_fields(&sctx->screen->info, &mutable_cb_state, &cb_surf);

      cb_surf.cb_color_info |= tex->cb_color_info;

      gfx11_set_context_reg(R_028C60_CB_COLOR0_BASE + i * 0x3C, cb_surf.cb_color_base);
      gfx11_set_context_reg(R_028C6C_CB_COLOR0_VIEW + i * 0x3C, cb_surf.cb_color_view);
      gfx11_set_context_reg(R_028C70_CB_COLOR0_INFO + i * 0x3C, cb_surf.cb_color_info);
      gfx11_set_context_reg(R_028C74_CB_COLOR0_ATTRIB + i * 0x3C, cb_surf.cb_color_attrib);
      gfx11_set_context_reg(R_028C78_CB_COLOR0_DCC_CONTROL + i * 0x3C, cb_surf.cb_dcc_control);
      gfx11_set_context_reg(R_028C94_CB_COLOR0_DCC_BASE + i * 0x3C, cb_surf.cb_dcc_base);
      gfx11_set_context_reg(R_028E40_CB_COLOR0_BASE_EXT + i * 4, cb_surf.cb_color_base >> 32);
      gfx11_set_context_reg(R_028EA0_CB_COLOR0_DCC_BASE_EXT + i * 4, cb_surf.cb_dcc_base >> 32);
      gfx11_set_context_reg(R_028EC0_CB_COLOR0_ATTRIB2 + i * 4, cb_surf.cb_color_attrib2);
      gfx11_set_context_reg(R_028EE0_CB_COLOR0_ATTRIB3 + i * 4, cb_surf.cb_color_attrib3);
   }
   for (; i < 8; i++)
      if (sctx->framebuffer.dirty_cbufs & (1 << i))
         gfx11_set_context_reg(R_028C70_CB_COLOR0_INFO + i * 0x3C, 0);

   /* ZS buffer. */
   if (state->zsbuf && sctx->framebuffer.dirty_zsbuf) {
      struct si_surface *zb = (struct si_surface *)state->zsbuf;
      struct si_texture *tex = (struct si_texture *)zb->base.texture;

      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, &tex->buffer, RADEON_USAGE_READWRITE |
                                (zb->base.texture->nr_samples > 1 ? RADEON_PRIO_DEPTH_BUFFER_MSAA
                                                                  : RADEON_PRIO_DEPTH_BUFFER));

      const unsigned level = zb->base.u.tex.level;

      /* Set mutable fields. */
      const struct ac_mutable_ds_state mutable_ds_state = {
         .ds = &zb->ds,
         .format = tex->db_render_format,
         .tc_compat_htile_enabled = vi_tc_compat_htile_enabled(tex, level, PIPE_MASK_ZS),
         .zrange_precision = tex->depth_clear_value[level] != 0,
      };
      struct ac_ds_surface ds;

      ac_set_mutable_ds_surface_fields(&sctx->screen->info, &mutable_ds_state, &ds);

      gfx11_set_context_reg(R_028014_DB_HTILE_DATA_BASE, ds.u.gfx6.db_htile_data_base);
      gfx11_set_context_reg(R_02801C_DB_DEPTH_SIZE_XY, ds.db_depth_size);
      gfx11_set_context_reg(R_028040_DB_Z_INFO, ds.db_z_info);
      gfx11_set_context_reg(R_028044_DB_STENCIL_INFO, ds.db_stencil_info);
      gfx11_set_context_reg(R_028048_DB_Z_READ_BASE, ds.db_depth_base);
      gfx11_set_context_reg(R_02804C_DB_STENCIL_READ_BASE, ds.db_stencil_base);
      gfx11_set_context_reg(R_028050_DB_Z_WRITE_BASE, ds.db_depth_base);
      gfx11_set_context_reg(R_028054_DB_STENCIL_WRITE_BASE, ds.db_stencil_base);
      gfx11_set_context_reg(R_028068_DB_Z_READ_BASE_HI, ds.db_depth_base >> 32);
      gfx11_set_context_reg(R_02806C_DB_STENCIL_READ_BASE_HI, ds.db_stencil_base >> 32);
      gfx11_set_context_reg(R_028070_DB_Z_WRITE_BASE_HI, ds.db_depth_base >> 32);
      gfx11_set_context_reg(R_028074_DB_STENCIL_WRITE_BASE_HI, ds.db_stencil_base >> 32);
      gfx11_set_context_reg(R_028078_DB_HTILE_DATA_BASE_HI, ds.u.gfx6.db_htile_data_base >> 32);
      gfx11_set_context_reg(R_028028_DB_STENCIL_CLEAR, tex->stencil_clear_value[level]);
      gfx11_set_context_reg(R_02802C_DB_DEPTH_CLEAR, fui(tex->depth_clear_value[level]));
      gfx11_set_context_reg(R_028008_DB_DEPTH_VIEW, ds.db_depth_view);
      gfx11_set_context_reg(R_028ABC_DB_HTILE_SURFACE, ds.u.gfx6.db_htile_surface);
   } else if (sctx->framebuffer.dirty_zsbuf) {
      /* Gfx11+: DB_Z_INFO.NUM_SAMPLES should match the framebuffer samples if no Z/S is bound.
       * It determines the sample count for VRS, primitive-ordered pixel shading, and occlusion
       * queries.
       */
      gfx11_set_context_reg(R_028040_DB_Z_INFO,
                            S_028040_FORMAT(V_028040_Z_INVALID) |
                            S_028040_NUM_SAMPLES(sctx->framebuffer.log_samples));
      gfx11_set_context_reg(R_028044_DB_STENCIL_INFO, S_028044_FORMAT(V_028044_STENCIL_INVALID));
   }

   /* Framebuffer dimensions. */
   /* PA_SC_WINDOW_SCISSOR_TL is set to 0,0 in gfx*_init_gfx_preamble_state */
   gfx11_set_context_reg(R_028208_PA_SC_WINDOW_SCISSOR_BR,
                         S_028208_BR_X(state->width) | S_028208_BR_Y(state->height));
   gfx11_end_packed_context_regs();

   if (sctx->screen->dpbb_allowed &&
       sctx->screen->pbb_context_states_per_bin > 1)
      radeon_event_write(V_028A90_BREAK_BATCH);

   radeon_end();

   si_update_display_dcc_dirty(sctx);

   sctx->framebuffer.dirty_cbufs = 0;
   sctx->framebuffer.dirty_zsbuf = false;
}

static void gfx12_emit_framebuffer_state(struct si_context *sctx, unsigned index)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct pipe_framebuffer_state *state = &sctx->framebuffer.state;
   unsigned i, nr_cbufs = state->nr_cbufs;
   struct si_texture *tex = NULL;
   struct si_surface *cb = NULL;
   bool is_msaa_resolve = state->nr_cbufs == 2 &&
                          state->cbufs[0] && state->cbufs[0]->texture->nr_samples > 1 &&
                          state->cbufs[1] && state->cbufs[1]->texture->nr_samples <= 1;

   /* CB can't do MSAA resolve. */
   assert(!is_msaa_resolve);

   radeon_begin(cs);
   gfx12_begin_context_regs();

   /* Colorbuffers. */
   for (i = 0; i < nr_cbufs; i++) {
      if (!(sctx->framebuffer.dirty_cbufs & (1 << i)))
         continue;

      /* RB+ depth-only rendering. See the comment where we set rbplus_depth_only_opt for more
       * information.
       */
      if (i == 0 &&
          sctx->screen->info.rbplus_allowed &&
          !sctx->queued.named.blend->cb_target_mask) {
         gfx12_set_context_reg(R_028EC0_CB_COLOR0_INFO + i * 4,
                               S_028EC0_FORMAT(V_028C70_COLOR_32) |
                               S_028EC0_NUMBER_TYPE(V_028C70_NUMBER_FLOAT));
         continue;
      }

      cb = (struct si_surface *)state->cbufs[i];
      if (!cb) {
         gfx12_set_context_reg(R_028EC0_CB_COLOR0_INFO + i * 4,
                               S_028EC0_FORMAT(V_028C70_COLOR_INVALID));
         continue;
      }

      tex = (struct si_texture *)cb->base.texture;
      radeon_add_to_buffer_list(
         sctx, &sctx->gfx_cs, &tex->buffer, RADEON_USAGE_READWRITE | RADEON_USAGE_CB_NEEDS_IMPLICIT_SYNC |
         (tex->buffer.b.b.nr_samples > 1 ? RADEON_PRIO_COLOR_BUFFER_MSAA : RADEON_PRIO_COLOR_BUFFER));

      /* Compute mutable surface parameters. */
      const struct ac_mutable_cb_state mutable_cb_state = {
         .surf = &tex->surface,
         .cb = &cb->cb,
         .va = tex->buffer.gpu_address,
      };
      struct ac_cb_surface cb_surf;

      ac_set_mutable_cb_surface_fields(&sctx->screen->info, &mutable_cb_state, &cb_surf);

      gfx12_set_context_reg(R_028C60_CB_COLOR0_BASE + i * 0x24, cb_surf.cb_color_base);
      gfx12_set_context_reg(R_028C64_CB_COLOR0_VIEW + i * 0x24, cb_surf.cb_color_view);
      gfx12_set_context_reg(R_028C68_CB_COLOR0_VIEW2 + i * 0x24, cb_surf.cb_color_view2);
      gfx12_set_context_reg(R_028C6C_CB_COLOR0_ATTRIB + i * 0x24, cb_surf.cb_color_attrib);
      gfx12_set_context_reg(R_028C70_CB_COLOR0_FDCC_CONTROL + i * 0x24, cb_surf.cb_dcc_control);
      gfx12_set_context_reg(R_028C78_CB_COLOR0_ATTRIB2 + i * 0x24, cb_surf.cb_color_attrib2);
      gfx12_set_context_reg(R_028C7C_CB_COLOR0_ATTRIB3 + i * 0x24, cb_surf.cb_color_attrib3);
      gfx12_set_context_reg(R_028E40_CB_COLOR0_BASE_EXT + i * 4, cb_surf.cb_color_base >> 32);
      gfx12_set_context_reg(R_028EC0_CB_COLOR0_INFO + i * 4, cb_surf.cb_color_info);
   }
   /* Set unbound colorbuffers. */
   for (; i < 8; i++)
      if (sctx->framebuffer.dirty_cbufs & (1 << i))
         gfx12_set_context_reg(R_028EC0_CB_COLOR0_INFO + i * 4, 0);

   /* ZS buffer. */
   if (state->zsbuf && sctx->framebuffer.dirty_zsbuf) {
      struct si_surface *zb = (struct si_surface *)state->zsbuf;
      struct si_texture *tex = (struct si_texture *)zb->base.texture;

      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, &tex->buffer,
                                RADEON_USAGE_READWRITE | RADEON_USAGE_DB_NEEDS_IMPLICIT_SYNC |
                                (zb->base.texture->nr_samples > 1 ? RADEON_PRIO_DEPTH_BUFFER_MSAA
                                                                  : RADEON_PRIO_DEPTH_BUFFER));
      gfx12_set_context_reg(R_028004_DB_DEPTH_VIEW, zb->ds.db_depth_view);
      gfx12_set_context_reg(R_028008_DB_DEPTH_VIEW1, zb->ds.u.gfx12.db_depth_view1);
      gfx12_set_context_reg(R_028014_DB_DEPTH_SIZE_XY, zb->ds.db_depth_size);
      gfx12_set_context_reg(R_028018_DB_Z_INFO, zb->ds.db_z_info);
      gfx12_set_context_reg(R_02801C_DB_STENCIL_INFO, zb->ds.db_stencil_info);
      gfx12_set_context_reg(R_028020_DB_Z_READ_BASE, zb->ds.db_depth_base);
      gfx12_set_context_reg(R_028024_DB_Z_READ_BASE_HI, zb->ds.db_depth_base >> 32);
      gfx12_set_context_reg(R_028028_DB_Z_WRITE_BASE, zb->ds.db_depth_base);
      gfx12_set_context_reg(R_02802C_DB_Z_WRITE_BASE_HI, zb->ds.db_depth_base >> 32);
      gfx12_set_context_reg(R_028030_DB_STENCIL_READ_BASE, zb->ds.db_stencil_base);
      gfx12_set_context_reg(R_028034_DB_STENCIL_READ_BASE_HI, zb->ds.db_stencil_base >> 32);
      gfx12_set_context_reg(R_028038_DB_STENCIL_WRITE_BASE, zb->ds.db_stencil_base);
      gfx12_set_context_reg(R_02803C_DB_STENCIL_WRITE_BASE_HI, zb->ds.db_stencil_base >> 32);

      if (tex->force_disable_hiz_his) {
         gfx12_set_context_reg(R_028B94_PA_SC_HIZ_INFO, S_028B94_SURFACE_ENABLE(0));
         gfx12_set_context_reg(R_028B98_PA_SC_HIS_INFO, S_028B98_SURFACE_ENABLE(0));
      } else {
         gfx12_set_context_reg(R_028B94_PA_SC_HIZ_INFO, zb->ds.u.gfx12.hiz_info);
         gfx12_set_context_reg(R_028B98_PA_SC_HIS_INFO, zb->ds.u.gfx12.his_info);

         if (zb->ds.u.gfx12.hiz_info) {
            gfx12_set_context_reg(R_028B9C_PA_SC_HIZ_BASE, zb->ds.u.gfx12.hiz_base);
            gfx12_set_context_reg(R_028BA0_PA_SC_HIZ_BASE_EXT, zb->ds.u.gfx12.hiz_base >> 32);
            gfx12_set_context_reg(R_028BA4_PA_SC_HIZ_SIZE_XY, zb->ds.u.gfx12.hiz_size_xy);
         }
         if (zb->ds.u.gfx12.his_info) {
            gfx12_set_context_reg(R_028BA8_PA_SC_HIS_BASE, zb->ds.u.gfx12.his_base);
            gfx12_set_context_reg(R_028BAC_PA_SC_HIS_BASE_EXT, zb->ds.u.gfx12.his_base >> 32);
            gfx12_set_context_reg(R_028BB0_PA_SC_HIS_SIZE_XY, zb->ds.u.gfx12.his_size_xy);
         }
      }
   } else if (sctx->framebuffer.dirty_zsbuf) {
      gfx12_set_context_reg(R_028018_DB_Z_INFO,
                            S_028040_FORMAT(V_028040_Z_INVALID) |
                            S_028040_NUM_SAMPLES(sctx->framebuffer.log_samples));
      gfx12_set_context_reg(R_02801C_DB_STENCIL_INFO,
                            S_028044_FORMAT(V_028044_STENCIL_INVALID)|
                            S_028044_TILE_STENCIL_DISABLE(1));
      gfx12_set_context_reg(R_028B94_PA_SC_HIZ_INFO, S_028B94_SURFACE_ENABLE(0));
      gfx12_set_context_reg(R_028B98_PA_SC_HIS_INFO, S_028B98_SURFACE_ENABLE(0));
   }

   /* Framebuffer dimensions. */
   /* PA_SC_WINDOW_SCISSOR_TL is set in gfx12_init_gfx_preamble_state */
   gfx12_set_context_reg(R_028208_PA_SC_WINDOW_SCISSOR_BR,
                         S_028208_BR_X(state->width - 1) |    /* inclusive */
                         S_028208_BR_Y(state->height - 1));   /* inclusive */
   gfx12_end_context_regs();

   if (sctx->screen->dpbb_allowed &&
       sctx->screen->pbb_context_states_per_bin > 1)
      radeon_event_write(V_028A90_BREAK_BATCH);

   radeon_end();

   sctx->framebuffer.dirty_cbufs = 0;
   sctx->framebuffer.dirty_zsbuf = false;
}

static bool si_out_of_order_rasterization(struct si_context *sctx)
{
   struct si_state_blend *blend = sctx->queued.named.blend;
   struct si_state_dsa *dsa = sctx->queued.named.dsa;

   if (!sctx->screen->info.has_out_of_order_rast)
      return false;

   unsigned colormask = sctx->framebuffer.colorbuf_enabled_4bit;

   colormask &= blend->cb_target_enabled_4bit;

   /* Conservative: No logic op. */
   if (colormask && blend->logicop_enable)
      return false;

   struct si_dsa_order_invariance dsa_order_invariant = {.zs = true,
                                                         .pass_set = true};

   if (sctx->framebuffer.state.zsbuf) {
      struct si_texture *zstex = (struct si_texture *)sctx->framebuffer.state.zsbuf->texture;
      bool has_stencil = zstex->surface.has_stencil;
      dsa_order_invariant = dsa->order_invariance[has_stencil];
      if (!dsa_order_invariant.zs)
         return false;

      /* The set of PS invocations is always order invariant,
       * except when early Z/S tests are requested. */
      if (sctx->shader.ps.cso && sctx->shader.ps.cso->info.base.writes_memory &&
          sctx->shader.ps.cso->info.base.fs.early_fragment_tests &&
          !dsa_order_invariant.pass_set)
         return false;

      if (sctx->occlusion_query_mode == SI_OCCLUSION_QUERY_MODE_PRECISE_INTEGER &&
          !dsa_order_invariant.pass_set)
         return false;
   }

   if (!colormask)
      return true;

   unsigned blendmask = colormask & blend->blend_enable_4bit;

   if (blendmask) {
      /* Only commutative blending. */
      if (blendmask & ~blend->commutative_4bit)
         return false;

      if (!dsa_order_invariant.pass_set)
         return false;
   }

   if (colormask & ~blendmask)
      return false;

   return true;
}

static void si_emit_msaa_config(struct si_context *sctx, unsigned index)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned num_tile_pipes = sctx->screen->info.num_tile_pipes;
   /* 33% faster rendering to linear color buffers */
   bool dst_is_linear = sctx->framebuffer.any_dst_linear;
   bool out_of_order_rast = si_out_of_order_rasterization(sctx);
   unsigned sc_mode_cntl_1 =
      S_028A4C_WALK_SIZE(dst_is_linear) | S_028A4C_WALK_FENCE_ENABLE(!dst_is_linear) |
      S_028A4C_WALK_FENCE_SIZE(num_tile_pipes == 2 ? 2 : 3) |
      S_028A4C_OUT_OF_ORDER_PRIMITIVE_ENABLE(out_of_order_rast) |
      S_028A4C_OUT_OF_ORDER_WATER_MARK(sctx->gfx_level >= GFX12 ? 0 : 0x7) |
      /* This should also be 0 when the VRS image is enabled. */
      S_028A4C_WALK_ALIGN8_PRIM_FITS_ST(!sctx->framebuffer.has_hiz_his) |
      /* always 1: */
      S_028A4C_SUPERTILE_WALK_ORDER_ENABLE(1) |
      S_028A4C_TILE_WALK_ORDER_ENABLE(1) | S_028A4C_MULTI_SHADER_ENGINE_PRIM_DISCARD_ENABLE(1) |
      S_028A4C_FORCE_EOV_CNTDWN_ENABLE(1) | S_028A4C_FORCE_EOV_REZ_ENABLE(1);
   unsigned db_eqaa = S_028804_HIGH_QUALITY_INTERSECTIONS(1) |
                      S_028804_INCOHERENT_EQAA_READS(sctx->gfx_level < GFX12) |
                      S_028804_STATIC_ANCHOR_ASSOCIATIONS(1);
   unsigned coverage_samples, z_samples;
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;

   /* S: Coverage samples (up to 16x):
    * - Scan conversion samples (PA_SC_AA_CONFIG.MSAA_NUM_SAMPLES)
    * - CB FMASK samples (CB_COLORi_ATTRIB.NUM_SAMPLES)
    *
    * Z: Z/S samples (up to 8x, must be <= coverage samples and >= color samples):
    * - Value seen by DB (DB_Z_INFO.NUM_SAMPLES)
    * - Value seen by CB, must be correct even if Z/S is unbound (DB_EQAA.MAX_ANCHOR_SAMPLES)
    * # Missing samples are derived from Z planes if Z is compressed (up to 16x quality), or
    * # from the closest defined sample if Z is uncompressed (same quality as the number of
    * # Z samples).
    *
    * F: Color samples (up to 8x, must be <= coverage samples):
    * - CB color samples (CB_COLORi_ATTRIB.NUM_FRAGMENTS)
    * - PS iter samples (DB_EQAA.PS_ITER_SAMPLES)
    *
    * Can be anything between coverage and color samples:
    * - SampleMaskIn samples (PA_SC_AA_CONFIG.MSAA_EXPOSED_SAMPLES)
    * - SampleMaskOut samples (DB_EQAA.MASK_EXPORT_NUM_SAMPLES)
    * - Alpha-to-coverage samples (DB_EQAA.ALPHA_TO_MASK_NUM_SAMPLES)
    * - Occlusion query samples (DB_COUNT_CONTROL.SAMPLE_RATE)
    * # All are currently set the same as coverage samples.
    *
    * If color samples < coverage samples, FMASK has a higher bpp to store an "unknown"
    * flag for undefined color samples. A shader-based resolve must handle unknowns
    * or mask them out with AND. Unknowns can also be guessed from neighbors via
    * an edge-detect shader-based resolve, which is required to make "color samples = 1"
    * useful. The CB resolve always drops unknowns.
    *
    * Sensible AA configurations:
    *   EQAA 16s 8z 8f - might look the same as 16x MSAA if Z is compressed
    *   EQAA 16s 8z 4f - might look the same as 16x MSAA if Z is compressed
    *   EQAA 16s 4z 4f - might look the same as 16x MSAA if Z is compressed
    *   EQAA  8s 8z 8f = 8x MSAA
    *   EQAA  8s 8z 4f - might look the same as 8x MSAA
    *   EQAA  8s 8z 2f - might look the same as 8x MSAA with low-density geometry
    *   EQAA  8s 4z 4f - might look the same as 8x MSAA if Z is compressed
    *   EQAA  8s 4z 2f - might look the same as 8x MSAA with low-density geometry if Z is compressed
    *   EQAA  4s 4z 4f = 4x MSAA
    *   EQAA  4s 4z 2f - might look the same as 4x MSAA with low-density geometry
    *   EQAA  2s 2z 2f = 2x MSAA
    */
   coverage_samples = si_get_num_coverage_samples(sctx);

   /* DCC_DECOMPRESS and ELIMINATE_FAST_CLEAR require MSAA_NUM_SAMPLES=0. */
   if (sctx->gfx_level >= GFX11 && sctx->gfx11_force_msaa_num_samples_zero)
      coverage_samples = 1;

   /* The DX10 diamond test is not required by GL and decreases line rasterization
    * performance, so don't use it.
    */
   unsigned sc_line_cntl = 0;
   unsigned sc_aa_config = 0;

   if (coverage_samples > 1 && (rs->multisample_enable ||
                                sctx->smoothing_enabled)) {
      unsigned log_samples = util_logbase2(coverage_samples);

      sc_line_cntl |= S_028BDC_EXPAND_LINE_WIDTH(1) |
                      S_028BDC_PERPENDICULAR_ENDCAP_ENA(rs->perpendicular_end_caps) |
                      S_028BDC_EXTRA_DX_DY_PRECISION(rs->perpendicular_end_caps &&
                                                     (sctx->family == CHIP_VEGA20 ||
                                                      sctx->gfx_level >= GFX10));
      sc_aa_config = S_028BE0_MSAA_NUM_SAMPLES(log_samples) |
                     S_028BE0_MSAA_EXPOSED_SAMPLES(log_samples);

      if (sctx->gfx_level < GFX12) {
         sc_aa_config |= S_028BE0_MAX_SAMPLE_DIST(si_msaa_max_distance[log_samples]) |
                         S_028BE0_COVERED_CENTROID_IS_CENTER(sctx->gfx_level >= GFX10_3);
      }
   }

   if (sctx->framebuffer.nr_samples > 1 ||
       sctx->smoothing_enabled) {
      if (sctx->framebuffer.state.zsbuf) {
         z_samples = sctx->framebuffer.state.zsbuf->texture->nr_samples;
         z_samples = MAX2(1, z_samples);
      } else {
         z_samples = coverage_samples;
      }
      unsigned log_samples = util_logbase2(coverage_samples);
      unsigned log_z_samples = util_logbase2(z_samples);
      unsigned ps_iter_samples = si_get_ps_iter_samples(sctx);
      unsigned log_ps_iter_samples = util_logbase2(ps_iter_samples);
      if (sctx->framebuffer.nr_samples > 1) {
         if (sctx->gfx_level >= GFX12) {
            sc_aa_config |= S_028BE0_PS_ITER_SAMPLES(log_ps_iter_samples);
            db_eqaa |= S_028078_MASK_EXPORT_NUM_SAMPLES(log_samples) |
                       S_028078_ALPHA_TO_MASK_NUM_SAMPLES(log_samples);
         } else {
            db_eqaa |= S_028804_MAX_ANCHOR_SAMPLES(log_z_samples) |
                       S_028804_PS_ITER_SAMPLES(log_ps_iter_samples) |
                       S_028804_MASK_EXPORT_NUM_SAMPLES(log_samples) |
                       S_028804_ALPHA_TO_MASK_NUM_SAMPLES(log_samples);
         }
         sc_mode_cntl_1 |= S_028A4C_PS_ITER_SAMPLE(ps_iter_samples > 1);
      } else if (sctx->smoothing_enabled) {
         db_eqaa |= S_028804_OVERRASTERIZATION_AMOUNT(log_samples);
      }
   }

   if (sctx->gfx_level >= GFX12) {
      radeon_begin(cs);
      gfx12_begin_context_regs();
      gfx12_opt_set_context_reg(R_028BDC_PA_SC_LINE_CNTL, SI_TRACKED_PA_SC_LINE_CNTL,
                                sc_line_cntl);
      gfx12_opt_set_context_reg(R_028BE0_PA_SC_AA_CONFIG, SI_TRACKED_PA_SC_AA_CONFIG,
                                sc_aa_config);
      gfx12_opt_set_context_reg(R_028078_DB_EQAA, SI_TRACKED_DB_EQAA, db_eqaa);
      gfx12_opt_set_context_reg(R_028A4C_PA_SC_MODE_CNTL_1, SI_TRACKED_PA_SC_MODE_CNTL_1,
                                sc_mode_cntl_1);
      gfx12_end_context_regs();
      radeon_end(); /* don't track context rolls on GFX12 */
   } else if (sctx->screen->info.has_set_context_pairs_packed) {
      radeon_begin(cs);
      gfx11_begin_packed_context_regs();
      gfx11_opt_set_context_reg(R_028BDC_PA_SC_LINE_CNTL, SI_TRACKED_PA_SC_LINE_CNTL,
                                sc_line_cntl);
      gfx11_opt_set_context_reg(R_028BE0_PA_SC_AA_CONFIG, SI_TRACKED_PA_SC_AA_CONFIG,
                                sc_aa_config);
      gfx11_opt_set_context_reg(R_028804_DB_EQAA, SI_TRACKED_DB_EQAA, db_eqaa);
      gfx11_opt_set_context_reg(R_028A4C_PA_SC_MODE_CNTL_1, SI_TRACKED_PA_SC_MODE_CNTL_1,
                                sc_mode_cntl_1);
      gfx11_end_packed_context_regs();
      radeon_end(); /* don't track context rolls on GFX11 */
   } else {
      radeon_begin(cs);
      radeon_opt_set_context_reg2(R_028BDC_PA_SC_LINE_CNTL, SI_TRACKED_PA_SC_LINE_CNTL,
                                  sc_line_cntl, sc_aa_config);
      radeon_opt_set_context_reg(R_028804_DB_EQAA, SI_TRACKED_DB_EQAA, db_eqaa);
      radeon_opt_set_context_reg(R_028A4C_PA_SC_MODE_CNTL_1, SI_TRACKED_PA_SC_MODE_CNTL_1,
                                 sc_mode_cntl_1);
      radeon_end_update_context_roll();
   }
}

void si_update_ps_iter_samples(struct si_context *sctx)
{
   if (sctx->ps_iter_samples == sctx->last_ps_iter_samples)
      return;

   sctx->last_ps_iter_samples = sctx->ps_iter_samples;
   si_ps_key_update_sample_shading(sctx);
   if (sctx->framebuffer.nr_samples > 1)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);
   if (sctx->screen->dpbb_allowed)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.dpbb_state);
}

static void si_set_min_samples(struct pipe_context *ctx, unsigned min_samples)
{
   struct si_context *sctx = (struct si_context *)ctx;

   /* The hardware can only do sample shading with 2^n samples. */
   min_samples = util_next_power_of_two(min_samples);

   if (sctx->ps_iter_samples == min_samples)
      return;

   sctx->ps_iter_samples = min_samples;

   si_ps_key_update_framebuffer_rasterizer_sample_shading(sctx);
   sctx->do_update_shaders = true;

   si_update_ps_iter_samples(sctx);
}

/*
 * Samplers
 */

/**
 * Build the sampler view descriptor for a buffer texture.
 * @param state 256-bit descriptor; only the high 128 bits are filled in
 */
void si_make_buffer_descriptor(struct si_screen *screen, struct si_resource *buf,
                               enum pipe_format format, unsigned offset, unsigned num_elements,
                               uint32_t *state)
{
   const struct util_format_description *desc;
   unsigned stride;
   unsigned num_records;

   desc = util_format_description(format);
   stride = desc->block.bits / 8;

   num_records = num_elements;
   num_records = MIN2(num_records, (buf->b.b.width0 - offset) / stride);

   /* The NUM_RECORDS field has a different meaning depending on the chip,
    * instruction type, STRIDE, and SWIZZLE_ENABLE.
    *
    * GFX6-7,10:
    * - If STRIDE == 0, it's in byte units.
    * - If STRIDE != 0, it's in units of STRIDE, used with inst.IDXEN.
    *
    * GFX8:
    * - For SMEM and STRIDE == 0, it's in byte units.
    * - For SMEM and STRIDE != 0, it's in units of STRIDE.
    * - For VMEM and STRIDE == 0 or SWIZZLE_ENABLE == 0, it's in byte units.
    * - For VMEM and STRIDE != 0 and SWIZZLE_ENABLE == 1, it's in units of STRIDE.
    * NOTE: There is incompatibility between VMEM and SMEM opcodes due to SWIZZLE_-
    *       ENABLE. The workaround is to set STRIDE = 0 if SWIZZLE_ENABLE == 0 when
    *       using SMEM. This can be done in the shader by clearing STRIDE with s_and.
    *       That way the same descriptor can be used by both SMEM and VMEM.
    *
    * GFX9:
    * - For SMEM and STRIDE == 0, it's in byte units.
    * - For SMEM and STRIDE != 0, it's in units of STRIDE.
    * - For VMEM and inst.IDXEN == 0 or STRIDE == 0, it's in byte units.
    * - For VMEM and inst.IDXEN == 1 and STRIDE != 0, it's in units of STRIDE.
    */
   if (screen->info.gfx_level == GFX8)
      num_records *= stride;

   const struct ac_buffer_state buffer_state = {
      .size = num_records,
      .format = format,
      .swizzle =
         {
            desc->swizzle[0],
            desc->swizzle[1],
            desc->swizzle[2],
            desc->swizzle[3],
         },
      .stride = stride,
      .gfx10_oob_select = V_008F0C_OOB_SELECT_STRUCTURED_WITH_OFFSET,
   };

   ac_build_buffer_descriptor(screen->info.gfx_level, &buffer_state, &state[4]);
}

/**
 * Translate the parameters to an image descriptor for CDNA image emulation.
 * In this function, we choose our own image descriptor format because we emulate image opcodes
 * using buffer opcodes.
 */
static void cdna_emu_make_image_descriptor(struct si_screen *screen, struct si_texture *tex,
                                           bool sampler, enum pipe_texture_target target,
                                           enum pipe_format pipe_format,
                                           const unsigned char state_swizzle[4], unsigned first_level,
                                           unsigned last_level, unsigned first_layer,
                                           unsigned last_layer, unsigned width, unsigned height,
                                           unsigned depth, uint32_t *state, uint32_t *fmask_state)
{
   const struct util_format_description *desc = util_format_description(pipe_format);

   /* We don't need support these. We only need enough to support VAAPI and OpenMAX. */
   if (target == PIPE_TEXTURE_CUBE ||
       target == PIPE_TEXTURE_CUBE_ARRAY ||
       tex->buffer.b.b.last_level > 0 ||
       tex->buffer.b.b.nr_samples >= 2 ||
       desc->colorspace != UTIL_FORMAT_COLORSPACE_RGB ||
       desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED ||
       util_format_is_compressed(pipe_format)) {
      assert(!"unexpected texture type");
      memset(state, 0, 8 * 4);
      return;
   }

   /* Adjust the image parameters according to the texture type. */
   switch (target) {
   case PIPE_TEXTURE_1D:
      height = 1;
      FALLTHROUGH;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
      depth = 1;
      break;

   case PIPE_TEXTURE_1D_ARRAY:
      height = 1;
      FALLTHROUGH;
   case PIPE_TEXTURE_2D_ARRAY:
      first_layer = MIN2(first_layer, tex->buffer.b.b.array_size - 1);
      last_layer = MIN2(last_layer, tex->buffer.b.b.array_size - 1);
      last_layer = MAX2(last_layer, first_layer);
      depth = last_layer - first_layer + 1;
      break;

   case PIPE_TEXTURE_3D:
      first_layer = 0;
      break;

   default:
      unreachable("invalid texture target");
   }

   unsigned stride = desc->block.bits / 8;
   uint64_t num_records = tex->surface.surf_size / stride;
   assert(num_records <= UINT32_MAX);

   /* Prepare the format fields. */
   unsigned char swizzle[4];
   util_format_compose_swizzles(desc->swizzle, state_swizzle, swizzle);

   /* Buffer descriptor */
   const struct ac_buffer_state buffer_state = {
      .size = num_records,
      .format = pipe_format,
      .swizzle =
         {
            desc->swizzle[0],
            desc->swizzle[1],
            desc->swizzle[2],
            desc->swizzle[3],
         },
      .stride = stride,
      .gfx10_oob_select = V_008F0C_OOB_SELECT_STRUCTURED_WITH_OFFSET,
   };

   ac_build_buffer_descriptor(screen->info.gfx_level, &buffer_state, &state[0]);

   /* Additional fields used by image opcode emulation. */
   state[4] = width | (height << 16);
   state[5] = depth | (first_layer << 16);
   state[6] = tex->surface.u.gfx9.surf_pitch;
   state[7] = (uint32_t)tex->surface.u.gfx9.surf_pitch * tex->surface.u.gfx9.surf_height;
}

/**
 * Build the sampler view descriptor for a texture.
 */
static void gfx10_make_texture_descriptor(
   struct si_screen *screen, struct si_texture *tex, bool sampler, enum pipe_texture_target target,
   enum pipe_format pipe_format, const unsigned char state_swizzle[4], unsigned first_level,
   unsigned last_level, unsigned first_layer, unsigned last_layer, unsigned width, unsigned height,
   unsigned depth, bool get_bo_metadata, uint32_t *state, uint32_t *fmask_state)
{
   struct pipe_resource *res = &tex->buffer.b.b;
   const struct util_format_description *desc;
   unsigned char swizzle[4];
   unsigned type;

   desc = util_format_description(pipe_format);

   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS) {
      const unsigned char swizzle_xxxx[4] = {0, 0, 0, 0};
      const unsigned char swizzle_yyyy[4] = {1, 1, 1, 1};
      const unsigned char swizzle_wwww[4] = {3, 3, 3, 3};

      switch (pipe_format) {
      case PIPE_FORMAT_S8_UINT_Z24_UNORM:
      case PIPE_FORMAT_X32_S8X24_UINT:
      case PIPE_FORMAT_X8Z24_UNORM:
         util_format_compose_swizzles(swizzle_yyyy, state_swizzle, swizzle);
         break;
      case PIPE_FORMAT_X24S8_UINT:
         /*
          * X24S8 is implemented as an 8_8_8_8 data format, to
          * fix texture gathers. This affects at least
          * GL45-CTS.texture_cube_map_array.sampling on GFX8.
          */
         util_format_compose_swizzles(swizzle_wwww, state_swizzle, swizzle);
         break;
      default:
         util_format_compose_swizzles(swizzle_xxxx, state_swizzle, swizzle);
      }
   } else {
      util_format_compose_swizzles(desc->swizzle, state_swizzle, swizzle);
   }

   if (!sampler && (res->target == PIPE_TEXTURE_CUBE || res->target == PIPE_TEXTURE_CUBE_ARRAY)) {
      /* For the purpose of shader images, treat cube maps as 2D
       * arrays.
       */
      type = V_008F1C_SQ_RSRC_IMG_2D_ARRAY;
   } else {
      type = si_tex_dim(screen, tex, target, res->nr_samples);
   }

   if (type == V_008F1C_SQ_RSRC_IMG_1D_ARRAY) {
      height = 1;
      depth = res->array_size;
   } else if (type == V_008F1C_SQ_RSRC_IMG_2D_ARRAY || type == V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY) {
      if (sampler || res->target != PIPE_TEXTURE_3D)
         depth = res->array_size;
   } else if (type == V_008F1C_SQ_RSRC_IMG_CUBE)
      depth = res->array_size / 6;

   const struct ac_texture_state tex_state = {
      .surf = &tex->surface,
      .format = pipe_format,
      .img_format = res->format,
      .width = width,
      .height = height,
      .depth =  (type == V_008F1C_SQ_RSRC_IMG_3D && sampler) ? depth - 1 : last_layer,
      .type = type,
      .swizzle =
         {
            swizzle[0],
            swizzle[1],
            swizzle[2],
            swizzle[3],
         },
      .num_samples = res->nr_samples,
      .num_storage_samples = res->nr_storage_samples,
      .first_level = first_level,
      .last_level = last_level,
      .num_levels = res->last_level + 1,
      .first_layer = first_layer,
      .last_layer = last_layer,
      .gfx10 = {
         .uav3d = !!(type == V_008F1C_SQ_RSRC_IMG_3D && !sampler),
         .upgraded_depth = tex->upgraded_depth,
      },
      .dcc_enabled = vi_dcc_enabled(tex, first_level),
   };

   ac_build_texture_descriptor(&screen->info, &tex_state, &state[0]);

   /* Initialize the sampler view for FMASK. */
   if (tex->surface.fmask_offset) {
      const struct ac_fmask_state ac_state = {
         .surf = &tex->surface,
         .va = tex->buffer.gpu_address,
         .width = width,
         .height = height,
         .depth = depth,
         .type = si_tex_dim(screen, tex, target, 0),
         .first_layer = first_layer,
         .last_layer = last_layer,
         .num_samples = res->nr_samples,
         .num_storage_samples = res->nr_storage_samples,
      };

      ac_build_fmask_descriptor(screen->info.gfx_level, &ac_state, &fmask_state[0]);
   }
}

/**
 * Build the sampler view descriptor for a texture (SI-GFX9).
 */
void si_make_texture_descriptor(struct si_screen *screen, struct si_texture *tex,
                                bool sampler, enum pipe_texture_target target,
                                enum pipe_format pipe_format,
                                const unsigned char state_swizzle[4], unsigned first_level,
                                unsigned last_level, unsigned first_layer,
                                unsigned last_layer, unsigned width, unsigned height,
                                unsigned depth, bool get_bo_metadata,
                                uint32_t *state, uint32_t *fmask_state)
{
   if (!screen->info.has_image_opcodes && !get_bo_metadata) {
      cdna_emu_make_image_descriptor(screen, tex, sampler, target, pipe_format, state_swizzle,
                                     first_level, last_level, first_layer, last_layer, width,
                                     height, depth, state, fmask_state);
      return;
   }

   if (screen->info.gfx_level >= GFX10) {
      gfx10_make_texture_descriptor(screen, tex, sampler, target, pipe_format, state_swizzle,
                                    first_level, last_level, first_layer, last_layer, width,
                                    height, depth, get_bo_metadata, state, fmask_state);
      return;
   }

   struct pipe_resource *res = &tex->buffer.b.b;
   const struct util_format_description *desc;
   unsigned char swizzle[4];
   unsigned type, num_samples;

   desc = util_format_description(pipe_format);

   num_samples = desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS ? MAX2(1, res->nr_samples)
                                                               : MAX2(1, res->nr_storage_samples);

   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS) {
      const unsigned char swizzle_xxxx[4] = {0, 0, 0, 0};
      const unsigned char swizzle_yyyy[4] = {1, 1, 1, 1};
      const unsigned char swizzle_wwww[4] = {3, 3, 3, 3};

      switch (pipe_format) {
      case PIPE_FORMAT_S8_UINT_Z24_UNORM:
      case PIPE_FORMAT_X32_S8X24_UINT:
      case PIPE_FORMAT_X8Z24_UNORM:
         util_format_compose_swizzles(swizzle_yyyy, state_swizzle, swizzle);
         break;
      case PIPE_FORMAT_X24S8_UINT:
         /*
          * X24S8 is implemented as an 8_8_8_8 data format, to
          * fix texture gathers. This affects at least
          * GL45-CTS.texture_cube_map_array.sampling on GFX8.
          */
         if (screen->info.gfx_level <= GFX8)
            util_format_compose_swizzles(swizzle_wwww, state_swizzle, swizzle);
         else
            util_format_compose_swizzles(swizzle_yyyy, state_swizzle, swizzle);
         break;
      default:
         util_format_compose_swizzles(swizzle_xxxx, state_swizzle, swizzle);
      }
   } else {
      util_format_compose_swizzles(desc->swizzle, state_swizzle, swizzle);
   }

   if (!sampler && (res->target == PIPE_TEXTURE_CUBE || res->target == PIPE_TEXTURE_CUBE_ARRAY ||
                    (screen->info.gfx_level <= GFX8 && res->target == PIPE_TEXTURE_3D))) {
      /* For the purpose of shader images, treat cube maps and 3D
       * textures as 2D arrays. For 3D textures, the address
       * calculations for mipmaps are different, so we rely on the
       * caller to effectively disable mipmaps.
       */
      type = V_008F1C_SQ_RSRC_IMG_2D_ARRAY;

      assert(res->target != PIPE_TEXTURE_3D || (first_level == 0 && last_level == 0));
   } else {
      type = si_tex_dim(screen, tex, target, num_samples);
   }

   if (type == V_008F1C_SQ_RSRC_IMG_1D_ARRAY) {
      height = 1;
      depth = res->array_size;
   } else if (type == V_008F1C_SQ_RSRC_IMG_2D_ARRAY || type == V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY) {
      if (sampler || res->target != PIPE_TEXTURE_3D)
         depth = res->array_size;
   } else if (type == V_008F1C_SQ_RSRC_IMG_CUBE)
      depth = res->array_size / 6;

   const struct ac_texture_state tex_state = {
      .surf = &tex->surface,
      .format = pipe_format,
      .img_format = res->format,
      .width = width,
      .height = height,
      .depth = depth,
      .type = type,
      .swizzle =
         {
            swizzle[0],
            swizzle[1],
            swizzle[2],
            swizzle[3],
         },
      .num_samples = res->nr_samples,
      .num_storage_samples = res->nr_storage_samples,
      .first_level = first_level,
      .last_level = last_level,
      .num_levels = res->last_level + 1,
      .first_layer = first_layer,
      .last_layer = last_layer,
      .dcc_enabled = vi_dcc_enabled(tex, first_level),
      .tc_compat_htile_enabled = true,
   };

   ac_build_texture_descriptor(&screen->info, &tex_state, &state[0]);

   /* Initialize the sampler view for FMASK. */
   if (tex->surface.fmask_offset) {
      const struct ac_fmask_state ac_state = {
         .surf = &tex->surface,
         .va = tex->buffer.gpu_address,
         .width = width,
         .height = height,
         .depth = depth,
         .type = si_tex_dim(screen, tex, target, 0),
         .first_layer = first_layer,
         .last_layer = last_layer,
         .num_samples = res->nr_samples,
         .num_storage_samples = res->nr_storage_samples,
      };

      ac_build_fmask_descriptor(screen->info.gfx_level, &ac_state, &fmask_state[0]);
   }
}

/**
 * Create a sampler view.
 *
 * @param ctx      context
 * @param texture  texture
 * @param state    sampler view template
 */
static struct pipe_sampler_view *si_create_sampler_view(struct pipe_context *ctx,
                                                        struct pipe_resource *texture,
                                                        const struct pipe_sampler_view *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_sampler_view *view = CALLOC_STRUCT_CL(si_sampler_view);
   struct si_texture *tex = (struct si_texture *)texture;
   unsigned char state_swizzle[4];
   unsigned last_layer = state->u.tex.last_layer;
   enum pipe_format pipe_format;
   const struct legacy_surf_level *surflevel;

   if (!view)
      return NULL;

   /* initialize base object */
   view->base = *state;
   view->base.texture = NULL;
   view->base.reference.count = 1;
   view->base.context = ctx;

   assert(texture);
   pipe_resource_reference(&view->base.texture, texture);

   if (state->format == PIPE_FORMAT_X24S8_UINT || state->format == PIPE_FORMAT_S8X24_UINT ||
       state->format == PIPE_FORMAT_X32_S8X24_UINT || state->format == PIPE_FORMAT_S8_UINT)
      view->is_stencil_sampler = true;

   /* Buffer resource. */
   if (texture->target == PIPE_BUFFER) {
      uint32_t elements = si_clamp_texture_texel_count(sctx->screen->b.caps.max_texel_buffer_elements,
                                                       state->format, state->u.buf.size);

      si_make_buffer_descriptor(sctx->screen, si_resource(texture), state->format,
                                state->u.buf.offset, elements, view->state);
      return &view->base;
   }

   state_swizzle[0] = state->swizzle_r;
   state_swizzle[1] = state->swizzle_g;
   state_swizzle[2] = state->swizzle_b;
   state_swizzle[3] = state->swizzle_a;

   /* This is not needed if gallium frontends set last_layer correctly. */
   if (state->target == PIPE_TEXTURE_1D || state->target == PIPE_TEXTURE_2D ||
       state->target == PIPE_TEXTURE_RECT || state->target == PIPE_TEXTURE_CUBE)
      last_layer = state->u.tex.first_layer;

   /* Texturing with separate depth and stencil. */
   pipe_format = state->format;

   /* Depth/stencil texturing sometimes needs separate texture. */
   if (tex->is_depth && !si_can_sample_zs(tex, view->is_stencil_sampler)) {
      if (!tex->flushed_depth_texture && !si_init_flushed_depth_texture(ctx, texture)) {
         pipe_resource_reference(&view->base.texture, NULL);
         FREE(view);
         return NULL;
      }

      assert(tex->flushed_depth_texture);

      /* Override format for the case where the flushed texture
       * contains only Z or only S.
       */
      if (tex->flushed_depth_texture->buffer.b.b.format != tex->buffer.b.b.format)
         pipe_format = tex->flushed_depth_texture->buffer.b.b.format;

      tex = tex->flushed_depth_texture;
   }

   surflevel = tex->surface.u.legacy.level;

   if (tex->db_compatible) {
      if (!view->is_stencil_sampler)
         pipe_format = tex->db_render_format;

      switch (pipe_format) {
      case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
         pipe_format = PIPE_FORMAT_Z32_FLOAT;
         break;
      case PIPE_FORMAT_X8Z24_UNORM:
      case PIPE_FORMAT_S8_UINT_Z24_UNORM:
         /* Z24 is always stored like this for DB
          * compatibility.
          */
         pipe_format = PIPE_FORMAT_Z24X8_UNORM;
         break;
      case PIPE_FORMAT_X24S8_UINT:
      case PIPE_FORMAT_S8X24_UINT:
      case PIPE_FORMAT_X32_S8X24_UINT:
         pipe_format = PIPE_FORMAT_S8_UINT;
         surflevel = tex->surface.u.legacy.zs.stencil_level;
         break;
      default:;
      }
   }

   view->dcc_incompatible =
      vi_dcc_formats_are_incompatible(texture, state->u.tex.first_level, state->format);

   si_make_texture_descriptor(sctx->screen, tex, true, state->target, pipe_format, state_swizzle,
                              state->u.tex.first_level, state->u.tex.last_level,
                              state->u.tex.first_layer, last_layer, texture->width0,
                              texture->height0, texture->depth0, false, view->state,
                              view->fmask_state);

   view->base_level_info = &surflevel[0];
   view->block_width = util_format_get_blockwidth(pipe_format);
   return &view->base;
}

static void si_sampler_view_destroy(struct pipe_context *ctx, struct pipe_sampler_view *state)
{
   struct si_sampler_view *view = (struct si_sampler_view *)state;

   pipe_resource_reference(&state->texture, NULL);
   FREE_CL(view);
}

static bool wrap_mode_uses_border_color(unsigned wrap, bool linear_filter)
{
   return wrap == PIPE_TEX_WRAP_CLAMP_TO_BORDER || wrap == PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER ||
          (linear_filter && (wrap == PIPE_TEX_WRAP_CLAMP || wrap == PIPE_TEX_WRAP_MIRROR_CLAMP));
}

static uint32_t si_translate_border_color(struct si_context *sctx,
                                          const struct pipe_sampler_state *state,
                                          const union pipe_color_union *color, bool is_integer,
                                          uint32_t *border_color_ptr)
{
   bool linear_filter = state->min_img_filter != PIPE_TEX_FILTER_NEAREST ||
                        state->mag_img_filter != PIPE_TEX_FILTER_NEAREST;

   if (!wrap_mode_uses_border_color(state->wrap_s, linear_filter) &&
       !wrap_mode_uses_border_color(state->wrap_t, linear_filter) &&
       !wrap_mode_uses_border_color(state->wrap_r, linear_filter))
      return V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK;

#define simple_border_types(elt)                                                                   \
   do {                                                                                            \
      if (color->elt[0] == 0 && color->elt[1] == 0 && color->elt[2] == 0 && color->elt[3] == 0)    \
         return V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK;                                          \
      if (color->elt[0] == 0 && color->elt[1] == 0 && color->elt[2] == 0 && color->elt[3] == 1)    \
         return V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_BLACK;                                         \
      if (color->elt[0] == 1 && color->elt[1] == 1 && color->elt[2] == 1 && color->elt[3] == 1)    \
         return V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_WHITE;                                         \
   } while (false)

   if (is_integer)
      simple_border_types(ui);
   else
      simple_border_types(f);

#undef simple_border_types

   int i;

   /* Check if the border has been uploaded already. */
   for (i = 0; i < sctx->border_color_count; i++)
      if (memcmp(&sctx->border_color_table[i], color, sizeof(*color)) == 0)
         break;

   if (i >= SI_MAX_BORDER_COLORS) {
      /* Getting 4096 unique border colors is very unlikely. */
      static bool printed;
      if (!printed) {
         fprintf(stderr, "radeonsi: The border color table is full. "
                         "Any new border colors will be just black. "
                         "This is a hardware limitation.\n");
         printed = true;
      }
      return V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK;
   }

   if (i == sctx->border_color_count) {
      /* Upload a new border color. */
      memcpy(&sctx->border_color_table[i], color, sizeof(*color));
      util_memcpy_cpu_to_le32(&sctx->border_color_map[i], color, sizeof(*color));
      sctx->border_color_count++;
   }

   *border_color_ptr = i;

   return V_008F3C_SQ_TEX_BORDER_COLOR_REGISTER;
}

static inline unsigned si_tex_filter(unsigned filter, unsigned max_aniso)
{
   if (filter == PIPE_TEX_FILTER_LINEAR)
      return max_aniso > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_BILINEAR
                           : V_008F38_SQ_TEX_XY_FILTER_BILINEAR;
   else
      return max_aniso > 1 ? V_008F38_SQ_TEX_XY_FILTER_ANISO_POINT
                           : V_008F38_SQ_TEX_XY_FILTER_POINT;
}

static inline unsigned si_tex_aniso_filter(unsigned filter)
{
   if (filter < 2)
      return 0;
   if (filter < 4)
      return 1;
   if (filter < 8)
      return 2;
   if (filter < 16)
      return 3;
   return 4;
}

static unsigned si_tex_filter_mode(unsigned mode)
{
   switch (mode) {
   case PIPE_TEX_REDUCTION_WEIGHTED_AVERAGE:
      return V_008F30_SQ_IMG_FILTER_MODE_BLEND;
   case PIPE_TEX_REDUCTION_MIN:
      return V_008F30_SQ_IMG_FILTER_MODE_MIN;
   case PIPE_TEX_REDUCTION_MAX:
      return V_008F30_SQ_IMG_FILTER_MODE_MAX;
   default:
      break;
   }
   return 0;
}

static void *si_create_sampler_state(struct pipe_context *ctx,
                                     const struct pipe_sampler_state *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_screen *sscreen = sctx->screen;
   struct si_sampler_state *rstate = CALLOC_STRUCT(si_sampler_state);
   unsigned max_aniso = sscreen->force_aniso >= 0 ? sscreen->force_aniso : state->max_anisotropy;
   unsigned max_aniso_ratio = si_tex_aniso_filter(max_aniso);
   unsigned filter_mode = si_tex_filter_mode(state->reduction_mode);
   bool trunc_coord = (state->min_img_filter == PIPE_TEX_FILTER_NEAREST &&
                       state->mag_img_filter == PIPE_TEX_FILTER_NEAREST &&
                       state->compare_mode == PIPE_TEX_COMPARE_NONE) ||
                      sscreen->info.conformant_trunc_coord;
   union pipe_color_union clamped_border_color;

   if (!rstate) {
      return NULL;
   }

   /* Validate inputs. */
   if (!is_wrap_mode_legal(sscreen, state->wrap_s) ||
       !is_wrap_mode_legal(sscreen, state->wrap_t) ||
       !is_wrap_mode_legal(sscreen, state->wrap_r) ||
       (!sscreen->info.has_3d_cube_border_color_mipmap &&
        (state->min_mip_filter != PIPE_TEX_MIPFILTER_NONE ||
         state->max_anisotropy > 0))) {
      assert(0);
      return NULL;
   }

#ifndef NDEBUG
   rstate->magic = SI_SAMPLER_STATE_MAGIC;
#endif

   unsigned border_color_ptr = 0;
   unsigned border_color_type =
      si_translate_border_color(sctx, state, &state->border_color,
                                state->border_color_is_integer,
                                &border_color_ptr);

   struct ac_sampler_state ac_state = {
      .address_mode_u = si_tex_wrap(state->wrap_s),
      .address_mode_v = si_tex_wrap(state->wrap_t),
      .address_mode_w = si_tex_wrap(state->wrap_r),
      .max_aniso_ratio = max_aniso_ratio,
      .depth_compare_func = si_tex_compare(state->compare_mode, state->compare_func),
      .unnormalized_coords = state->unnormalized_coords,
      .cube_wrap = state->seamless_cube_map,
      .trunc_coord = trunc_coord,
      .filter_mode = filter_mode,
      .mag_filter = si_tex_filter(state->mag_img_filter, max_aniso),
      .min_filter = si_tex_filter(state->min_img_filter, max_aniso),
      .mip_filter = si_tex_mipfilter(state->min_mip_filter),
      .min_lod = state->min_lod,
      .max_lod = state->max_lod,
      .lod_bias = state->lod_bias,
      .border_color_type = border_color_type,
      .border_color_ptr = border_color_ptr,
   };

   ac_build_sampler_descriptor(sscreen->info.gfx_level, &ac_state, rstate->val);

   /* Create sampler resource for upgraded depth textures. */
   memcpy(rstate->upgraded_depth_val, rstate->val, sizeof(rstate->val));

   for (unsigned i = 0; i < 4; ++i) {
      /* Use channel 0 on purpose, so that we can use OPAQUE_WHITE
       * when the border color is 1.0. */
      clamped_border_color.f[i] = CLAMP(state->border_color.f[0], 0, 1);
   }

   if (memcmp(&state->border_color, &clamped_border_color, sizeof(clamped_border_color)) == 0) {
      if (sscreen->info.gfx_level <= GFX9)
         rstate->upgraded_depth_val[3] |= S_008F3C_UPGRADED_DEPTH(1);
   } else {
      border_color_ptr = 0;
      border_color_type = si_translate_border_color(sctx, state, &clamped_border_color, false, &border_color_ptr);

      rstate->upgraded_depth_val[3] = S_008F3C_BORDER_COLOR_TYPE(border_color_type);

      if (sscreen->info.gfx_level >= GFX11) {
         rstate->upgraded_depth_val[3] |= S_008F3C_BORDER_COLOR_PTR_GFX11(border_color_ptr);
      } else {
         rstate->upgraded_depth_val[3] |= S_008F3C_BORDER_COLOR_PTR_GFX6(border_color_ptr);
      }
   }

   return rstate;
}

static void si_set_sample_mask(struct pipe_context *ctx, unsigned sample_mask)
{
   struct si_context *sctx = (struct si_context *)ctx;

   if (sctx->sample_mask == (uint16_t)sample_mask)
      return;

   sctx->sample_mask = sample_mask;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.sample_mask);
}

static void si_emit_sample_mask(struct si_context *sctx, unsigned index)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   unsigned mask = sctx->sample_mask;

   /* Needed for line and polygon smoothing as well as for the Polaris
    * small primitive filter. We expect the gallium frontend to take care of
    * this for us.
    */
   assert(mask == 0xffff || sctx->framebuffer.nr_samples > 1 ||
          (mask & 1 && sctx->blitter_running));

   radeon_begin(cs);
   radeon_set_context_reg_seq(R_028C38_PA_SC_AA_MASK_X0Y0_X1Y0, 2);
   radeon_emit(mask | (mask << 16));
   radeon_emit(mask | (mask << 16));
   radeon_end();
}

static void si_delete_sampler_state(struct pipe_context *ctx, void *state)
{
#ifndef NDEBUG
   struct si_sampler_state *s = state;

   assert(s->magic == SI_SAMPLER_STATE_MAGIC);
   s->magic = 0;
#endif
   free(state);
}

/*
 * Vertex elements & buffers
 */

struct si_fast_udiv_info32 {
   unsigned multiplier; /* the "magic number" multiplier */
   unsigned pre_shift;  /* shift for the dividend before multiplying */
   unsigned post_shift; /* shift for the dividend after multiplying */
   int increment;       /* 0 or 1; if set then increment the numerator, using one of
                           the two strategies */
};

static struct si_fast_udiv_info32 si_compute_fast_udiv_info32(uint32_t D, unsigned num_bits)
{
   struct util_fast_udiv_info info = util_compute_fast_udiv_info(D, num_bits, 32);

   struct si_fast_udiv_info32 result = {
      info.multiplier,
      info.pre_shift,
      info.post_shift,
      info.increment,
   };
   return result;
}

static void *si_create_vertex_elements(struct pipe_context *ctx, unsigned count,
                                       const struct pipe_vertex_element *elements)
{
   struct si_screen *sscreen = (struct si_screen *)ctx->screen;

   if (sscreen->debug_flags & DBG(VERTEX_ELEMENTS)) {
      for (int i = 0; i < count; ++i) {
         const struct pipe_vertex_element *e = elements + i;
         fprintf(stderr, "elements[%d]: offset %2d, buffer_index %d, dual_slot %d, format %3d, divisor %u\n",
                i, e->src_offset, e->vertex_buffer_index, e->dual_slot, e->src_format, e->instance_divisor);
      }
   }

   struct si_vertex_elements *v = CALLOC_STRUCT(si_vertex_elements);
   struct si_fast_udiv_info32 divisor_factors[SI_MAX_ATTRIBS] = {};
   STATIC_ASSERT(sizeof(struct si_fast_udiv_info32) == 16);
   STATIC_ASSERT(sizeof(divisor_factors[0].multiplier) == 4);
   STATIC_ASSERT(sizeof(divisor_factors[0].pre_shift) == 4);
   STATIC_ASSERT(sizeof(divisor_factors[0].post_shift) == 4);
   STATIC_ASSERT(sizeof(divisor_factors[0].increment) == 4);
   int i;

   assert(count <= SI_MAX_ATTRIBS);
   if (!v)
      return NULL;

   v->count = count;

   unsigned num_vbos_in_user_sgprs = si_num_vbos_in_user_sgprs_inline(sscreen->info.gfx_level);
   unsigned alloc_count =
      count > num_vbos_in_user_sgprs ? count - num_vbos_in_user_sgprs : 0;
   v->vb_desc_list_alloc_size = align(alloc_count * 16, SI_CPDMA_ALIGNMENT);

   for (i = 0; i < count; ++i) {
      const struct util_format_description *desc;
      const struct util_format_channel_description *channel;
      int first_non_void;
      unsigned vbo_index = elements[i].vertex_buffer_index;

      if (vbo_index >= SI_NUM_VERTEX_BUFFERS) {
         FREE(v);
         return NULL;
      }

      unsigned instance_divisor = elements[i].instance_divisor;
      if (instance_divisor) {
         if (instance_divisor == 1) {
            v->instance_divisor_is_one |= 1u << i;
         } else {
            v->instance_divisor_is_fetched |= 1u << i;
            divisor_factors[i] = si_compute_fast_udiv_info32(instance_divisor, 32);
         }
      }

      desc = util_format_description(elements[i].src_format);
      first_non_void = util_format_get_first_non_void_channel(elements[i].src_format);
      channel = first_non_void >= 0 ? &desc->channel[first_non_void] : NULL;

      v->elem[i].format_size = desc->block.bits / 8;
      v->elem[i].src_offset = elements[i].src_offset;
      v->elem[i].stride = elements[i].src_stride;
      v->vertex_buffer_index[i] = vbo_index;

      bool always_fix = false;
      union si_vs_fix_fetch fix_fetch;
      unsigned log_hw_load_size; /* the load element size as seen by the hardware */

      fix_fetch.bits = 0;
      log_hw_load_size = MIN2(2, util_logbase2(desc->block.bits) - 3);

      if (channel) {
         switch (channel->type) {
         case UTIL_FORMAT_TYPE_FLOAT:
            fix_fetch.u.format = AC_FETCH_FORMAT_FLOAT;
            break;
         case UTIL_FORMAT_TYPE_FIXED:
            fix_fetch.u.format = AC_FETCH_FORMAT_FIXED;
            break;
         case UTIL_FORMAT_TYPE_SIGNED: {
            if (channel->pure_integer)
               fix_fetch.u.format = AC_FETCH_FORMAT_SINT;
            else if (channel->normalized)
               fix_fetch.u.format = AC_FETCH_FORMAT_SNORM;
            else
               fix_fetch.u.format = AC_FETCH_FORMAT_SSCALED;
            break;
         }
         case UTIL_FORMAT_TYPE_UNSIGNED: {
            if (channel->pure_integer)
               fix_fetch.u.format = AC_FETCH_FORMAT_UINT;
            else if (channel->normalized)
               fix_fetch.u.format = AC_FETCH_FORMAT_UNORM;
            else
               fix_fetch.u.format = AC_FETCH_FORMAT_USCALED;
            break;
         }
         default:
            unreachable("bad format type");
         }
      } else {
         switch (elements[i].src_format) {
         case PIPE_FORMAT_R11G11B10_FLOAT:
            fix_fetch.u.format = AC_FETCH_FORMAT_FLOAT;
            break;
         default:
            unreachable("bad other format");
         }
      }

      if (desc->channel[0].size == 10) {
         fix_fetch.u.log_size = 3; /* special encoding for 2_10_10_10 */
         log_hw_load_size = 2;

         /* The hardware always treats the 2-bit alpha channel as
          * unsigned, so a shader workaround is needed. The affected
          * chips are GFX8 and older except Stoney (GFX8.1).
          */
         always_fix = sscreen->info.gfx_level <= GFX8 && sscreen->info.family != CHIP_STONEY &&
                      channel->type == UTIL_FORMAT_TYPE_SIGNED;
      } else if (elements[i].src_format == PIPE_FORMAT_R11G11B10_FLOAT) {
         fix_fetch.u.log_size = 3; /* special encoding */
         fix_fetch.u.format = AC_FETCH_FORMAT_FIXED;
         log_hw_load_size = 2;
      } else {
         fix_fetch.u.log_size = util_logbase2(channel->size) - 3;
         fix_fetch.u.num_channels_m1 = desc->nr_channels - 1;

         /* Always fix up:
          * - doubles (multiple loads + truncate to float)
          * - 32-bit requiring a conversion
          */
         always_fix = (fix_fetch.u.log_size == 3) ||
                      (fix_fetch.u.log_size == 2 && fix_fetch.u.format != AC_FETCH_FORMAT_FLOAT &&
                       fix_fetch.u.format != AC_FETCH_FORMAT_UINT &&
                       fix_fetch.u.format != AC_FETCH_FORMAT_SINT);

         /* Also fixup 8_8_8 and 16_16_16. */
         if (desc->nr_channels == 3 && fix_fetch.u.log_size <= 1) {
            always_fix = true;
            log_hw_load_size = fix_fetch.u.log_size;
         }
      }

      if (desc->swizzle[0] != PIPE_SWIZZLE_X) {
         assert(desc->swizzle[0] == PIPE_SWIZZLE_Z &&
                (desc->swizzle[2] == PIPE_SWIZZLE_X || desc->swizzle[2] == PIPE_SWIZZLE_0));
         fix_fetch.u.reverse = 1;
      }

      /* Force the workaround for unaligned access here already if the
       * offset relative to the vertex buffer base is unaligned.
       *
       * There is a theoretical case in which this is too conservative:
       * if the vertex buffer's offset is also unaligned in just the
       * right way, we end up with an aligned address after all.
       * However, this case should be extremely rare in practice (it
       * won't happen in well-behaved applications), and taking it
       * into account would complicate the fast path (where everything
       * is nicely aligned).
       */
      bool check_alignment =
            log_hw_load_size >= 1 &&
            (sscreen->info.gfx_level == GFX6 || sscreen->info.gfx_level >= GFX10);
      bool opencode = sscreen->options.vs_fetch_always_opencode;

      if (check_alignment && ((elements[i].src_offset & ((1 << log_hw_load_size) - 1)) != 0 ||
                              elements[i].src_stride & 3))
         opencode = true;

      if (always_fix || check_alignment || opencode)
         v->fix_fetch[i] = fix_fetch.bits;

      if (opencode)
         v->fix_fetch_opencode |= 1 << i;
      if (opencode || always_fix)
         v->fix_fetch_always |= 1 << i;

      if (check_alignment && !opencode) {
         assert(log_hw_load_size == 1 || log_hw_load_size == 2);

         v->fix_fetch_unaligned |= 1 << i;
         v->hw_load_is_dword |= (log_hw_load_size - 1) << i;
         v->vb_alignment_check_mask |= 1 << vbo_index;
      }

      const struct ac_buffer_state buffer_state = {
         .format = elements[i].src_format,
         .swizzle =
            {
               desc->swizzle[0],
               desc->swizzle[1],
               desc->swizzle[2],
               desc->swizzle[3],
            },
         /* OOB_SELECT chooses the out-of-bounds check:
          *  - 1: index >= NUM_RECORDS (Structured)
          *  - 3: offset >= NUM_RECORDS (Raw)
          */
         .gfx10_oob_select = v->elem[i].stride ? V_008F0C_OOB_SELECT_STRUCTURED
                                               : V_008F0C_OOB_SELECT_RAW,
      };

      ac_set_buf_desc_word3(sscreen->info.gfx_level, &buffer_state, &v->elem[i].rsrc_word3);
   }

   if (v->instance_divisor_is_fetched) {
      unsigned num_divisors = util_last_bit(v->instance_divisor_is_fetched);

      v->instance_divisor_factor_buffer = (struct si_resource *)pipe_buffer_create(
         &sscreen->b, 0, PIPE_USAGE_DEFAULT, num_divisors * sizeof(divisor_factors[0]));
      if (!v->instance_divisor_factor_buffer) {
         FREE(v);
         return NULL;
      }
      void *map =
         sscreen->ws->buffer_map(sscreen->ws, v->instance_divisor_factor_buffer->buf, NULL, PIPE_MAP_WRITE);
      memcpy(map, divisor_factors, num_divisors * sizeof(divisor_factors[0]));
   }
   return v;
}

static void si_bind_vertex_elements(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_vertex_elements *old = sctx->vertex_elements;
   struct si_vertex_elements *v = (struct si_vertex_elements *)state;

   if (!v)
      v = sctx->no_velems_state;

   sctx->vertex_elements = v;
   sctx->num_vertex_elements = v->count;
   sctx->vertex_buffers_dirty = sctx->num_vertex_elements > 0;

   if (old->instance_divisor_is_one != v->instance_divisor_is_one ||
       old->instance_divisor_is_fetched != v->instance_divisor_is_fetched ||
       (old->vb_alignment_check_mask ^ v->vb_alignment_check_mask) &
       sctx->vertex_buffer_unaligned ||
       ((v->vb_alignment_check_mask & sctx->vertex_buffer_unaligned) &&
        memcmp(old->vertex_buffer_index, v->vertex_buffer_index,
               sizeof(v->vertex_buffer_index[0]) * MAX2(old->count, v->count))) ||
       /* fix_fetch_{always,opencode,unaligned} and hw_load_is_dword are
        * functions of fix_fetch and the src_offset alignment.
        * If they change and fix_fetch doesn't, it must be due to different
        * src_offset alignment, which is reflected in fix_fetch_opencode. */
       old->fix_fetch_opencode != v->fix_fetch_opencode ||
       memcmp(old->fix_fetch, v->fix_fetch, sizeof(v->fix_fetch[0]) *
              MAX2(old->count, v->count))) {
      si_vs_key_update_inputs(sctx);
      sctx->do_update_shaders = true;
   }

   if (v->instance_divisor_is_fetched) {
      struct pipe_constant_buffer cb;

      cb.buffer = &v->instance_divisor_factor_buffer->b.b;
      cb.user_buffer = NULL;
      cb.buffer_offset = 0;
      cb.buffer_size = 0xffffffff;
      si_set_internal_const_buffer(sctx, SI_VS_CONST_INSTANCE_DIVISORS, &cb);
   }
}

static void si_delete_vertex_element(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_vertex_elements *v = (struct si_vertex_elements *)state;

   if (sctx->vertex_elements == state)
      si_bind_vertex_elements(ctx, sctx->no_velems_state);

   si_resource_reference(&v->instance_divisor_factor_buffer, NULL);
   FREE(state);
}

static void si_set_vertex_buffers(struct pipe_context *ctx, unsigned count,
                                  const struct pipe_vertex_buffer *buffers)
{
   struct si_context *sctx = (struct si_context *)ctx;
   uint32_t unaligned = 0;
   unsigned i;

   assert(count <= ARRAY_SIZE(sctx->vertex_buffer));
   assert(!count || buffers);

   for (i = 0; i < count; i++) {
      const struct pipe_vertex_buffer *src = buffers + i;
      struct pipe_vertex_buffer *dst = sctx->vertex_buffer + i;
      struct pipe_resource *buf = src->buffer.resource;

      dst->buffer_offset = src->buffer_offset;

      /* Only unreference bound vertex buffers. */
      pipe_resource_reference(&dst->buffer.resource, NULL);
      dst->buffer.resource = src->buffer.resource;

      if (src->buffer_offset & 3)
         unaligned |= BITFIELD_BIT(i);

      if (buf) {
         si_resource(buf)->bind_history |= SI_BIND_VERTEX_BUFFER;
         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, si_resource(buf),
                                   RADEON_USAGE_READ | RADEON_PRIO_VERTEX_BUFFER);
      }
   }

   unsigned last_count = sctx->num_vertex_buffers;
   for (; i < last_count; i++)
      pipe_resource_reference(&sctx->vertex_buffer[i].buffer.resource, NULL);

   sctx->num_vertex_buffers = count;
   sctx->vertex_buffers_dirty = sctx->num_vertex_elements > 0;
   sctx->vertex_buffer_unaligned = unaligned;

   /* Check whether alignment may have changed in a way that requires
    * shader changes. This check is conservative: a vertex buffer can only
    * trigger a shader change if the misalignment amount changes (e.g.
    * from byte-aligned to short-aligned), but we only keep track of
    * whether buffers are at least dword-aligned, since that should always
    * be the case in well-behaved applications anyway.
    */
   if (sctx->vertex_elements->vb_alignment_check_mask & unaligned) {
      si_vs_key_update_inputs(sctx);
      sctx->do_update_shaders = true;
   }
}

static struct pipe_vertex_state *
si_create_vertex_state(struct pipe_screen *screen,
                       struct pipe_vertex_buffer *buffer,
                       const struct pipe_vertex_element *elements,
                       unsigned num_elements,
                       struct pipe_resource *indexbuf,
                       uint32_t full_velem_mask)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   struct si_vertex_state *state = CALLOC_STRUCT(si_vertex_state);

   util_init_pipe_vertex_state(screen, buffer, elements, num_elements, indexbuf, full_velem_mask,
                               &state->b);

   /* Initialize the vertex element state in state->element.
    * Do it by creating a vertex element state object and copying it there.
    */
   struct si_context ctx = {};
   ctx.b.screen = screen;
   struct si_vertex_elements *velems = si_create_vertex_elements(&ctx.b, num_elements, elements);
   state->velems = *velems;
   si_delete_vertex_element(&ctx.b, velems);

   assert(!state->velems.instance_divisor_is_one);
   assert(!state->velems.instance_divisor_is_fetched);
   assert(!state->velems.fix_fetch_always);
   assert(buffer->buffer_offset % 4 == 0);
   assert(!buffer->is_user_buffer);
   for (unsigned i = 0; i < num_elements; i++) {
      assert(elements[i].src_offset % 4 == 0);
      assert(!elements[i].dual_slot);
      assert(elements[i].src_stride % 4 == 0);
   }

   for (unsigned i = 0; i < num_elements; i++) {
      si_set_vertex_buffer_descriptor(sscreen, &state->velems, &state->b.input.vbuffer, i,
                                      &state->descriptors[i * 4]);
   }

   return &state->b;
}

static void si_vertex_state_destroy(struct pipe_screen *screen,
                                    struct pipe_vertex_state *state)
{
   pipe_vertex_buffer_unreference(&state->input.vbuffer);
   pipe_resource_reference(&state->input.indexbuf, NULL);
   FREE(state);
}

static struct pipe_vertex_state *
si_pipe_create_vertex_state(struct pipe_screen *screen,
                            struct pipe_vertex_buffer *buffer,
                            const struct pipe_vertex_element *elements,
                            unsigned num_elements,
                            struct pipe_resource *indexbuf,
                            uint32_t full_velem_mask)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   return util_vertex_state_cache_get(screen, buffer, elements, num_elements, indexbuf,
                                      full_velem_mask, &sscreen->vertex_state_cache);
}

static void si_pipe_vertex_state_destroy(struct pipe_screen *screen,
                                         struct pipe_vertex_state *state)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   util_vertex_state_destroy(screen, &sscreen->vertex_state_cache, state);
}

/*
 * Misc
 */

static void si_set_tess_state(struct pipe_context *ctx, const float default_outer_level[4],
                              const float default_inner_level[2])
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct pipe_constant_buffer cb;
   float array[8];

   memcpy(array, default_outer_level, sizeof(float) * 4);
   memcpy(array + 4, default_inner_level, sizeof(float) * 2);

   cb.buffer = NULL;
   cb.user_buffer = array;
   cb.buffer_offset = 0;
   cb.buffer_size = sizeof(array);

   si_set_internal_const_buffer(sctx, SI_HS_CONST_DEFAULT_TESS_LEVELS, &cb);
}

static void *si_create_blend_custom(struct si_context *sctx, unsigned mode)
{
   struct pipe_blend_state blend;

   memset(&blend, 0, sizeof(blend));
   blend.independent_blend_enable = true;
   blend.rt[0].colormask = 0xf;
   return si_create_blend_state_mode(&sctx->b, &blend, mode);
}

static void si_pm4_emit_sqtt_pipeline(struct si_context *sctx, unsigned index)
{
   struct si_pm4_state *state = sctx->queued.array[index];

   si_pm4_emit_state(sctx, index);

   radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, ((struct si_sqtt_fake_pipeline*)state)->bo,
                             RADEON_USAGE_READ | RADEON_PRIO_SHADER_BINARY);
}

void si_init_state_compute_functions(struct si_context *sctx)
{
   sctx->b.create_sampler_state = si_create_sampler_state;
   sctx->b.delete_sampler_state = si_delete_sampler_state;
   sctx->b.create_sampler_view = si_create_sampler_view;
   sctx->b.sampler_view_destroy = si_sampler_view_destroy;
}

void si_init_state_functions(struct si_context *sctx)
{
   sctx->atoms.s.pm4_states[SI_STATE_IDX(blend)].emit = si_pm4_emit_state;
   sctx->atoms.s.pm4_states[SI_STATE_IDX(rasterizer)].emit = si_pm4_emit_rasterizer;
   sctx->atoms.s.pm4_states[SI_STATE_IDX(dsa)].emit = si_pm4_emit_dsa;
   sctx->atoms.s.pm4_states[SI_STATE_IDX(sqtt_pipeline)].emit = si_pm4_emit_sqtt_pipeline;
   sctx->atoms.s.pm4_states[SI_STATE_IDX(ls)].emit = si_pm4_emit_shader;
   sctx->atoms.s.pm4_states[SI_STATE_IDX(hs)].emit = si_pm4_emit_shader;
   sctx->atoms.s.pm4_states[SI_STATE_IDX(es)].emit = si_pm4_emit_shader;
   sctx->atoms.s.pm4_states[SI_STATE_IDX(gs)].emit = si_pm4_emit_shader;
   sctx->atoms.s.pm4_states[SI_STATE_IDX(vs)].emit = si_pm4_emit_shader;
   sctx->atoms.s.pm4_states[SI_STATE_IDX(ps)].emit = si_pm4_emit_shader;

   if (sctx->gfx_level >= GFX12)
      sctx->atoms.s.framebuffer.emit = gfx12_emit_framebuffer_state;
   else if (sctx->screen->info.has_set_context_pairs_packed)
      sctx->atoms.s.framebuffer.emit = gfx11_dgpu_emit_framebuffer_state;
   else
      sctx->atoms.s.framebuffer.emit = gfx6_emit_framebuffer_state;

   sctx->atoms.s.db_render_state.emit = si_emit_db_render_state;
   sctx->atoms.s.dpbb_state.emit = si_emit_dpbb_state;
   sctx->atoms.s.msaa_config.emit = si_emit_msaa_config;
   sctx->atoms.s.sample_mask.emit = si_emit_sample_mask;
   sctx->atoms.s.cb_render_state.emit = si_emit_cb_render_state;
   sctx->atoms.s.blend_color.emit = si_emit_blend_color;
   sctx->atoms.s.clip_regs.emit = si_emit_clip_regs;
   sctx->atoms.s.clip_state.emit = si_emit_clip_state;
   sctx->atoms.s.stencil_ref.emit = si_emit_stencil_ref;

   sctx->b.create_blend_state = si_create_blend_state;
   sctx->b.bind_blend_state = si_bind_blend_state;
   sctx->b.delete_blend_state = si_delete_blend_state;
   sctx->b.set_blend_color = si_set_blend_color;

   sctx->b.create_rasterizer_state = si_create_rs_state;
   sctx->b.bind_rasterizer_state = si_bind_rs_state;
   sctx->b.delete_rasterizer_state = si_delete_rs_state;

   sctx->b.create_depth_stencil_alpha_state = si_create_dsa_state;
   sctx->b.bind_depth_stencil_alpha_state = si_bind_dsa_state;
   sctx->b.delete_depth_stencil_alpha_state = si_delete_dsa_state;

   sctx->custom_dsa_flush = si_create_db_flush_dsa(sctx);

   if (sctx->gfx_level < GFX11) {
      sctx->custom_blend_resolve = si_create_blend_custom(sctx, V_028808_CB_RESOLVE);
      sctx->custom_blend_fmask_decompress = si_create_blend_custom(sctx, V_028808_CB_FMASK_DECOMPRESS);
      sctx->custom_blend_eliminate_fastclear =
         si_create_blend_custom(sctx, V_028808_CB_ELIMINATE_FAST_CLEAR);
   }

   sctx->custom_blend_dcc_decompress =
      si_create_blend_custom(sctx,
                             sctx->gfx_level >= GFX12 ? V_028858_CB_DCC_DECOMPRESS :
                             sctx->gfx_level >= GFX11 ? V_028808_CB_DCC_DECOMPRESS_GFX11 :
                                                        V_028808_CB_DCC_DECOMPRESS_GFX8);

   sctx->b.set_clip_state = si_set_clip_state;
   sctx->b.set_stencil_ref = si_set_stencil_ref;

   sctx->b.set_framebuffer_state = si_set_framebuffer_state;

   sctx->b.set_sample_mask = si_set_sample_mask;

   sctx->b.create_vertex_elements_state = si_create_vertex_elements;
   sctx->b.bind_vertex_elements_state = si_bind_vertex_elements;
   sctx->b.delete_vertex_elements_state = si_delete_vertex_element;
   sctx->b.set_vertex_buffers = si_set_vertex_buffers;

   sctx->b.set_min_samples = si_set_min_samples;
   sctx->b.set_tess_state = si_set_tess_state;

   sctx->b.set_active_query_state = si_set_active_query_state;
}

void si_init_screen_state_functions(struct si_screen *sscreen)
{
   sscreen->b.is_format_supported = si_is_format_supported;
   sscreen->b.create_vertex_state = si_pipe_create_vertex_state;
   sscreen->b.vertex_state_destroy = si_pipe_vertex_state_destroy;

   util_vertex_state_cache_init(&sscreen->vertex_state_cache,
                                si_create_vertex_state, si_vertex_state_destroy);
}

static void si_init_compute_preamble_state(struct si_context *sctx,
                                           struct si_pm4_state *pm4)
{
   uint64_t border_color_va =
      sctx->border_color_buffer ? sctx->border_color_buffer->gpu_address : 0;

   const struct ac_preamble_state preamble_state = {
      .border_color_va = border_color_va,
      .gfx11 = {
         .compute_dispatch_interleave = 256,
      },
   };

   ac_init_compute_preamble_state(&preamble_state, &pm4->base);
}

static void si_init_graphics_preamble_state(struct si_context *sctx,
                                            struct si_pm4_state *pm4)
{
   struct si_screen *sscreen = sctx->screen;
   uint64_t border_color_va =
      sctx->border_color_buffer ? sctx->border_color_buffer->gpu_address : 0;

   const struct ac_preamble_state preamble_state = {
      .border_color_va = border_color_va,
      .gfx10.cache_rb_gl2 = sctx->gfx_level >= GFX10 && sscreen->options.cache_rb_gl2,
   };

   ac_init_graphics_preamble_state(&preamble_state, &pm4->base);

   if (sctx->gfx_level >= GFX7) {
      /* If any sample location uses the -8 coordinate, the EXCLUSION fields should be set to 0. */
      ac_pm4_set_reg(&pm4->base, R_02882C_PA_SU_PRIM_FILTER_CNTL,
                     S_02882C_XMAX_RIGHT_EXCLUSION(1) |
                     S_02882C_YMAX_BOTTOM_EXCLUSION(1));
   }
}

static void gfx6_init_gfx_preamble_state(struct si_context *sctx)
{
   struct si_screen *sscreen = sctx->screen;
   bool has_clear_state = sscreen->info.has_clear_state;

   /* We need more space because the preamble is large. */
   struct si_pm4_state *pm4 = si_pm4_create_sized(sscreen, 214, sctx->has_graphics);
   if (!pm4)
      return;

   if (sctx->has_graphics && !sctx->shadowing.registers) {
      ac_pm4_cmd_add(&pm4->base, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
      ac_pm4_cmd_add(&pm4->base, CC0_UPDATE_LOAD_ENABLES(1));
      ac_pm4_cmd_add(&pm4->base, CC1_UPDATE_SHADOW_ENABLES(1));

      if (sscreen->dpbb_allowed) {
         ac_pm4_cmd_add(&pm4->base, PKT3(PKT3_EVENT_WRITE, 0, 0));
         ac_pm4_cmd_add(&pm4->base, EVENT_TYPE(V_028A90_BREAK_BATCH) | EVENT_INDEX(0));
      }

      if (has_clear_state) {
         ac_pm4_cmd_add(&pm4->base, PKT3(PKT3_CLEAR_STATE, 0, 0));
         ac_pm4_cmd_add(&pm4->base, 0);
      }
   }

   si_init_compute_preamble_state(sctx, pm4);

   if (!sctx->has_graphics)
      goto done;

   /* Graphics registers. */
   si_init_graphics_preamble_state(sctx, pm4);

   if (!has_clear_state) {
      ac_pm4_set_reg(&pm4->base, R_02800C_DB_RENDER_OVERRIDE, 0);
      ac_pm4_set_reg(&pm4->base, R_0286E0_SPI_BARYC_CNTL, 0);
   }

   if (sctx->family >= CHIP_POLARIS10 && !sctx->screen->info.has_small_prim_filter_sample_loc_bug) {
      /* Polaris10-12 should disable small line culling, but those also have the sample loc bug,
       * so they never enter this branch.
       */
      assert(sctx->family > CHIP_POLARIS12);
      ac_pm4_set_reg(&pm4->base, R_028830_PA_SU_SMALL_PRIM_FILTER_CNTL,
                     S_028830_SMALL_PRIM_FILTER_ENABLE(1));
   }

   if (sctx->gfx_level <= GFX7 || !has_clear_state) {
      ac_pm4_set_reg(&pm4->base, R_028B28_VGT_STRMOUT_DRAW_OPAQUE_OFFSET, 0);
      ac_pm4_set_reg(&pm4->base, R_028034_PA_SC_SCREEN_SCISSOR_BR,
                     S_028034_BR_X(16384) | S_028034_BR_Y(16384));
   }

   if (sctx->gfx_level == GFX9) {
      ac_pm4_set_reg(&pm4->base, R_028C4C_PA_SC_CONSERVATIVE_RASTERIZATION_CNTL,
                     S_028C4C_NULL_SQUAD_AA_MASK_ENABLE(1));
   }

done:
   ac_pm4_finalize(&pm4->base);
   sctx->cs_preamble_state = pm4;
   sctx->cs_preamble_state_tmz = si_pm4_clone(sscreen, pm4); /* Make a copy of the preamble for TMZ. */
}

static void cdna_init_compute_preamble_state(struct si_context *sctx)
{
   struct si_screen *sscreen = sctx->screen;
   uint64_t border_color_va =
      sctx->border_color_buffer ? sctx->border_color_buffer->gpu_address : 0;
   uint32_t compute_cu_en = S_00B858_SH0_CU_EN(sscreen->info.spi_cu_en) |
                            S_00B858_SH1_CU_EN(sscreen->info.spi_cu_en);

   struct si_pm4_state *pm4 = si_pm4_create_sized(sscreen, 48, true);
   if (!pm4)
      return;

   /* Compute registers. */
   /* Disable profiling on compute chips. */
   ac_pm4_set_reg(&pm4->base, R_00B82C_COMPUTE_PERFCOUNT_ENABLE, 0);
   ac_pm4_set_reg(&pm4->base, R_00B834_COMPUTE_PGM_HI, S_00B834_DATA(sctx->screen->info.address32_hi >> 8));
   ac_pm4_set_reg(&pm4->base, R_00B858_COMPUTE_STATIC_THREAD_MGMT_SE0, compute_cu_en);
   ac_pm4_set_reg(&pm4->base, R_00B85C_COMPUTE_STATIC_THREAD_MGMT_SE1, compute_cu_en);
   ac_pm4_set_reg(&pm4->base, R_00B864_COMPUTE_STATIC_THREAD_MGMT_SE2, compute_cu_en);
   ac_pm4_set_reg(&pm4->base, R_00B868_COMPUTE_STATIC_THREAD_MGMT_SE3, compute_cu_en);
   ac_pm4_set_reg(&pm4->base, R_00B878_COMPUTE_THREAD_TRACE_ENABLE, 0);

   if (sscreen->info.family >= CHIP_GFX940) {
      ac_pm4_set_reg(&pm4->base, R_00B89C_COMPUTE_TG_CHUNK_SIZE, 0);
      ac_pm4_set_reg(&pm4->base, R_00B8B4_COMPUTE_PGM_RSRC3, 0);
   } else {
      ac_pm4_set_reg(&pm4->base, R_00B894_COMPUTE_STATIC_THREAD_MGMT_SE4, compute_cu_en);
      ac_pm4_set_reg(&pm4->base, R_00B898_COMPUTE_STATIC_THREAD_MGMT_SE5, compute_cu_en);
      ac_pm4_set_reg(&pm4->base, R_00B89C_COMPUTE_STATIC_THREAD_MGMT_SE6, compute_cu_en);
      ac_pm4_set_reg(&pm4->base, R_00B8A0_COMPUTE_STATIC_THREAD_MGMT_SE7, compute_cu_en);
   }

   ac_pm4_set_reg(&pm4->base, R_0301EC_CP_COHER_START_DELAY, 0);

   /* Set the pointer to border colors. Only MI100 supports border colors. */
   if (sscreen->info.family == CHIP_MI100) {
      ac_pm4_set_reg(&pm4->base, R_030E00_TA_CS_BC_BASE_ADDR, border_color_va >> 8);
      ac_pm4_set_reg(&pm4->base, R_030E04_TA_CS_BC_BASE_ADDR_HI,
                     S_030E04_ADDRESS(border_color_va >> 40));
   }

   ac_pm4_finalize(&pm4->base);
   sctx->cs_preamble_state = pm4;
   sctx->cs_preamble_state_tmz = si_pm4_clone(sscreen, pm4); /* Make a copy of the preamble for TMZ. */
}

static void gfx10_init_gfx_preamble_state(struct si_context *sctx)
{
   struct si_screen *sscreen = sctx->screen;

   /* We need more space because the preamble is large. */
   struct si_pm4_state *pm4 = si_pm4_create_sized(sscreen, 214, sctx->has_graphics);
   if (!pm4)
      return;

   if (sctx->has_graphics && !sctx->shadowing.registers) {
      ac_pm4_cmd_add(&pm4->base, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
      ac_pm4_cmd_add(&pm4->base, CC0_UPDATE_LOAD_ENABLES(1));
      ac_pm4_cmd_add(&pm4->base, CC1_UPDATE_SHADOW_ENABLES(1));

      if (sscreen->dpbb_allowed) {
         ac_pm4_cmd_add(&pm4->base, PKT3(PKT3_EVENT_WRITE, 0, 0));
         ac_pm4_cmd_add(&pm4->base, EVENT_TYPE(V_028A90_BREAK_BATCH) | EVENT_INDEX(0));
      }

      ac_pm4_cmd_add(&pm4->base, PKT3(PKT3_CLEAR_STATE, 0, 0));
      ac_pm4_cmd_add(&pm4->base, 0);
   }

   si_init_compute_preamble_state(sctx, pm4);

   if (!sctx->has_graphics)
      goto done;

   /* Graphics registers. */
   si_init_graphics_preamble_state(sctx, pm4);

   ac_pm4_set_reg(&pm4->base, R_028708_SPI_SHADER_IDX_FORMAT,
                  S_028708_IDX0_EXPORT_FORMAT(V_028708_SPI_SHADER_1COMP));

   if (sctx->gfx_level >= GFX10_3) {
      /* The rate combiners have no effect if they are disabled like this:
       *   VERTEX_RATE:    BYPASS_VTX_RATE_COMBINER = 1
       *   PRIMITIVE_RATE: BYPASS_PRIM_RATE_COMBINER = 1
       *   HTILE_RATE:     VRS_HTILE_ENCODING = 0
       *   SAMPLE_ITER:    PS_ITER_SAMPLE = 0
       *
       * Use OVERRIDE, which will ignore results from previous combiners.
       * (e.g. enabled sample shading overrides the vertex rate)
       */
      ac_pm4_set_reg(&pm4->base, R_028848_PA_CL_VRS_CNTL,
                     S_028848_VERTEX_RATE_COMBINER_MODE(V_028848_SC_VRS_COMB_MODE_OVERRIDE) |
                     S_028848_SAMPLE_ITER_COMBINER_MODE(V_028848_SC_VRS_COMB_MODE_OVERRIDE));
   }

done:
   ac_pm4_finalize(&pm4->base);
   sctx->cs_preamble_state = pm4;
   sctx->cs_preamble_state_tmz = si_pm4_clone(sscreen, pm4); /* Make a copy of the preamble for TMZ. */
}

static void gfx12_init_gfx_preamble_state(struct si_context *sctx)
{
   struct si_screen *sscreen = sctx->screen;

   struct si_pm4_state *pm4 = si_pm4_create_sized(sscreen, 300, sctx->has_graphics);
   if (!pm4)
      return;

   if (sctx->has_graphics && !sctx->shadowing.registers) {
      ac_pm4_cmd_add(&pm4->base, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
      ac_pm4_cmd_add(&pm4->base, CC0_UPDATE_LOAD_ENABLES(1));
      ac_pm4_cmd_add(&pm4->base, CC1_UPDATE_SHADOW_ENABLES(1));
   }

   if (sctx->has_graphics && sscreen->dpbb_allowed) {
      ac_pm4_cmd_add(&pm4->base, PKT3(PKT3_EVENT_WRITE, 0, 0));
      ac_pm4_cmd_add(&pm4->base, EVENT_TYPE(V_028A90_BREAK_BATCH) | EVENT_INDEX(0));
   }

   si_init_compute_preamble_state(sctx, pm4);

   if (!sctx->has_graphics)
      goto done;

   /* Graphics registers. */
   si_init_graphics_preamble_state(sctx, pm4);

   ac_pm4_set_reg(&pm4->base, R_028648_SPI_SHADER_IDX_FORMAT,
                  S_028648_IDX0_EXPORT_FORMAT(V_028648_SPI_SHADER_1COMP));
   ac_pm4_set_reg(&pm4->base, R_028658_SPI_BARYC_CNTL, 0);

   ac_pm4_set_reg(&pm4->base, R_028B28_VGT_STRMOUT_DRAW_OPAQUE_OFFSET, 0);

   /* The rate combiners have no effect if they are disabled like this:
    *   VERTEX_RATE:    BYPASS_VTX_RATE_COMBINER = 1
    *   PRIMITIVE_RATE: BYPASS_PRIM_RATE_COMBINER = 1
    *   HTILE_RATE:     VRS_HTILE_ENCODING = 0
    *   SAMPLE_ITER:    PS_ITER_SAMPLE = 0
    *
    * Use OVERRIDE, which will ignore results from previous combiners.
    * (e.g. enabled sample shading overrides the vertex rate)
    */
   ac_pm4_set_reg(&pm4->base, R_028848_PA_CL_VRS_CNTL,
                  S_028848_VERTEX_RATE_COMBINER_MODE(V_028848_SC_VRS_COMB_MODE_OVERRIDE) |
                  S_028848_SAMPLE_ITER_COMBINER_MODE(V_028848_SC_VRS_COMB_MODE_OVERRIDE));

   ac_pm4_set_reg(&pm4->base, R_028C54_PA_SC_CONSERVATIVE_RASTERIZATION_CNTL,
                  S_028C54_NULL_SQUAD_AA_MASK_ENABLE(1));

   ac_pm4_set_reg(&pm4->base, R_00B2B8_SPI_SHADER_GS_MESHLET_CTRL, 0);

done:
   sctx->cs_preamble_state = pm4;
   sctx->cs_preamble_state_tmz = si_pm4_clone(sscreen, pm4); /* Make a copy of the preamble for TMZ. */
}

void si_init_gfx_preamble_state(struct si_context *sctx)
{
   if (!sctx->screen->info.has_graphics)
      cdna_init_compute_preamble_state(sctx);
   else if (sctx->gfx_level >= GFX12)
      gfx12_init_gfx_preamble_state(sctx);
   else if (sctx->gfx_level >= GFX10)
      gfx10_init_gfx_preamble_state(sctx);
   else
      gfx6_init_gfx_preamble_state(sctx);
}
