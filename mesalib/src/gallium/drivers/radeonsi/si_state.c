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

#include "si_build_pm4.h"
#include "si_query.h"
#include "si_shader_internal.h"
#include "sid.h"
#include "util/fast_idiv_by_const.h"
#include "util/format/u_format.h"
#include "util/format/u_format_s3tc.h"
#include "util/u_dual_blend.h"
#include "util/u_memory.h"
#include "util/u_resource.h"
#include "util/u_upload_mgr.h"
#include "util/u_blend.h"

#include "gfx10_format_table.h"

static unsigned si_map_swizzle(unsigned swizzle)
{
   switch (swizzle) {
   case PIPE_SWIZZLE_Y:
      return V_008F0C_SQ_SEL_Y;
   case PIPE_SWIZZLE_Z:
      return V_008F0C_SQ_SEL_Z;
   case PIPE_SWIZZLE_W:
      return V_008F0C_SQ_SEL_W;
   case PIPE_SWIZZLE_0:
      return V_008F0C_SQ_SEL_0;
   case PIPE_SWIZZLE_1:
      return V_008F0C_SQ_SEL_1;
   default: /* PIPE_SWIZZLE_X */
      return V_008F0C_SQ_SEL_X;
   }
}

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
static void si_emit_cb_render_state(struct si_context *sctx)
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
   if (sctx->screen->dpbb_allowed && sctx->last_cb_target_mask != cb_target_mask) {
      sctx->last_cb_target_mask = cb_target_mask;

      radeon_begin(cs);
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_BREAK_BATCH) | EVENT_INDEX(0));
      radeon_end();
   }

   radeon_begin(cs);
   radeon_opt_set_context_reg(sctx, R_028238_CB_TARGET_MASK, SI_TRACKED_CB_TARGET_MASK,
                              cb_target_mask);

   if (sctx->chip_class >= GFX8) {
      /* DCC MSAA workaround.
       * Alternatively, we can set CB_COLORi_DCC_CONTROL.OVERWRITE_-
       * COMBINER_DISABLE, but that would be more complicated.
       */
      bool oc_disable =
         blend->dcc_msaa_corruption_4bit & cb_target_mask && sctx->framebuffer.nr_samples >= 2;
      unsigned watermark = sctx->framebuffer.dcc_overwrite_combiner_watermark;

      radeon_opt_set_context_reg(
         sctx, R_028424_CB_DCC_CONTROL, SI_TRACKED_CB_DCC_CONTROL,
         S_028424_OVERWRITE_COMBINER_MRT_SHARING_DISABLE(sctx->chip_class <= GFX9) |
            S_028424_OVERWRITE_COMBINER_WATERMARK(watermark) |
            S_028424_OVERWRITE_COMBINER_DISABLE(oc_disable) |
            S_028424_DISABLE_CONSTANT_ENCODE_REG(sctx->screen->info.has_dcc_constant_encode));
   }

   /* RB+ register settings. */
   if (sctx->screen->info.rbplus_allowed) {
      unsigned spi_shader_col_format =
         sctx->shader.ps.cso ? sctx->shader.ps.current->key.part.ps.epilog.spi_shader_col_format
                             : 0;
      unsigned sx_ps_downconvert = 0;
      unsigned sx_blend_opt_epsilon = 0;
      unsigned sx_blend_opt_control = 0;

      for (i = 0; i < sctx->framebuffer.state.nr_cbufs; i++) {
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

         format = G_028C70_FORMAT(surf->cb_color_info);
         swap = G_028C70_COMP_SWAP(surf->cb_color_info);
         spi_format = (spi_shader_col_format >> (i * 4)) & 0xf;
         colormask = (cb_target_mask >> (i * 4)) & 0xf;

         /* Set if RGB and A are present. */
         has_alpha = !G_028C74_FORCE_DST_ALPHA_1(surf->cb_color_attrib);

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
               sx_blend_opt_epsilon |= V_028758_8BIT_FORMAT << (i * 4);
            }
            break;

         case V_028C70_COLOR_5_6_5:
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_5_6_5 << (i * 4);
               sx_blend_opt_epsilon |= V_028758_6BIT_FORMAT << (i * 4);
            }
            break;

         case V_028C70_COLOR_1_5_5_5:
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_1_5_5_5 << (i * 4);
               sx_blend_opt_epsilon |= V_028758_5BIT_FORMAT << (i * 4);
            }
            break;

         case V_028C70_COLOR_4_4_4_4:
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_4_4_4_4 << (i * 4);
               sx_blend_opt_epsilon |= V_028758_4BIT_FORMAT << (i * 4);
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
            if (spi_format == V_028714_SPI_SHADER_FP16_ABGR) {
               sx_ps_downconvert |= V_028754_SX_RT_EXPORT_2_10_10_10 << (i * 4);
               sx_blend_opt_epsilon |= V_028758_10BIT_FORMAT << (i * 4);
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

      /* SX_PS_DOWNCONVERT, SX_BLEND_OPT_EPSILON, SX_BLEND_OPT_CONTROL */
      radeon_opt_set_context_reg3(sctx, R_028754_SX_PS_DOWNCONVERT, SI_TRACKED_SX_PS_DOWNCONVERT,
                                  sx_ps_downconvert, sx_blend_opt_epsilon, sx_blend_opt_control);
   }
   radeon_end_update_context_roll(sctx);
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

static uint32_t si_translate_blend_factor(int blend_fact)
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
      return V_028780_BLEND_CONSTANT_COLOR;
   case PIPE_BLENDFACTOR_CONST_ALPHA:
      return V_028780_BLEND_CONSTANT_ALPHA;
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
      return V_028780_BLEND_ONE_MINUS_CONSTANT_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA:
      return V_028780_BLEND_ONE_MINUS_CONSTANT_ALPHA;
   case PIPE_BLENDFACTOR_SRC1_COLOR:
      return V_028780_BLEND_SRC1_COLOR;
   case PIPE_BLENDFACTOR_SRC1_ALPHA:
      return V_028780_BLEND_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
      return V_028780_BLEND_INV_SRC1_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
      return V_028780_BLEND_INV_SRC1_ALPHA;
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

   if (dst == PIPE_BLENDFACTOR_ONE && (src_allowed & (1u << src))) {
      /* Addition is commutative, but floating point addition isn't
       * associative: subtle changes can be introduced via different
       * rounding.
       *
       * Out-of-order is also non-deterministic, which means that
       * this breaks OpenGL invariance requirements. So only enable
       * out-of-order additive blending if explicitly allowed by a
       * setting.
       */
      if (func == PIPE_BLEND_MAX || func == PIPE_BLEND_MIN ||
          (func == PIPE_BLEND_ADD && sscreen->commutative_blend_add))
         blend->commutative_4bit |= chanmask;
   }
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

   if (state->alpha_to_coverage && state->alpha_to_coverage_dither) {
      si_pm4_set_reg(pm4, R_028B70_DB_ALPHA_TO_MASK,
                     S_028B70_ALPHA_TO_MASK_ENABLE(state->alpha_to_coverage) |
                        S_028B70_ALPHA_TO_MASK_OFFSET0(3) | S_028B70_ALPHA_TO_MASK_OFFSET1(1) |
                        S_028B70_ALPHA_TO_MASK_OFFSET2(0) | S_028B70_ALPHA_TO_MASK_OFFSET3(2) |
                        S_028B70_OFFSET_ROUND(1));
   } else {
      si_pm4_set_reg(pm4, R_028B70_DB_ALPHA_TO_MASK,
                     S_028B70_ALPHA_TO_MASK_ENABLE(state->alpha_to_coverage) |
                        S_028B70_ALPHA_TO_MASK_OFFSET0(2) | S_028B70_ALPHA_TO_MASK_OFFSET1(2) |
                        S_028B70_ALPHA_TO_MASK_OFFSET2(2) | S_028B70_ALPHA_TO_MASK_OFFSET3(2) |
                        S_028B70_OFFSET_ROUND(0));
   }

   if (state->alpha_to_coverage)
      blend->need_src_alpha_4bit |= 0xf;

   blend->cb_target_mask = 0;
   blend->cb_target_enabled_4bit = 0;

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
         /* Vulkan does this for dual source blending. */
         if (i == 1)
            blend_cntl |= S_028780_ENABLE(1);

         si_pm4_set_reg(pm4, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);
         continue;
      }

      /* Only addition and subtraction equations are supported with
       * dual source blending.
       */
      if (blend->dual_src_blend && (eqRGB == PIPE_BLEND_MIN || eqRGB == PIPE_BLEND_MAX ||
                                    eqA == PIPE_BLEND_MIN || eqA == PIPE_BLEND_MAX)) {
         assert(!"Unsupported equation for dual source blending");
         si_pm4_set_reg(pm4, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);
         continue;
      }

      /* cb_render_state will disable unused ones */
      blend->cb_target_mask |= (unsigned)state->rt[j].colormask << (4 * i);
      if (state->rt[j].colormask)
         blend->cb_target_enabled_4bit |= 0xf << (4 * i);

      if (!state->rt[j].colormask || !state->rt[j].blend_enable) {
         si_pm4_set_reg(pm4, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);
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

      /* Set blend state. */
      blend_cntl |= S_028780_ENABLE(1);
      blend_cntl |= S_028780_COLOR_COMB_FCN(si_translate_blend_function(eqRGB));
      blend_cntl |= S_028780_COLOR_SRCBLEND(si_translate_blend_factor(srcRGB));
      blend_cntl |= S_028780_COLOR_DESTBLEND(si_translate_blend_factor(dstRGB));

      if (srcA != srcRGB || dstA != dstRGB || eqA != eqRGB) {
         blend_cntl |= S_028780_SEPARATE_ALPHA_BLEND(1);
         blend_cntl |= S_028780_ALPHA_COMB_FCN(si_translate_blend_function(eqA));
         blend_cntl |= S_028780_ALPHA_SRCBLEND(si_translate_blend_factor(srcA));
         blend_cntl |= S_028780_ALPHA_DESTBLEND(si_translate_blend_factor(dstA));
      }
      si_pm4_set_reg(pm4, R_028780_CB_BLEND0_CONTROL + i * 4, blend_cntl);

      blend->blend_enable_4bit |= 0xfu << (i * 4);

      if (sctx->chip_class >= GFX8 && sctx->chip_class <= GFX10)
         blend->dcc_msaa_corruption_4bit |= 0xfu << (i * 4);

      /* This is only important for formats without alpha. */
      if (srcRGB == PIPE_BLENDFACTOR_SRC_ALPHA || dstRGB == PIPE_BLENDFACTOR_SRC_ALPHA ||
          srcRGB == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE ||
          dstRGB == PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE ||
          srcRGB == PIPE_BLENDFACTOR_INV_SRC_ALPHA || dstRGB == PIPE_BLENDFACTOR_INV_SRC_ALPHA)
         blend->need_src_alpha_4bit |= 0xfu << (i * 4);
   }

   if (sctx->chip_class >= GFX8 && sctx->chip_class <= GFX10 && logicop_enable)
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
         si_pm4_set_reg(pm4, R_028760_SX_MRT0_BLEND_OPT + i * 4, sx_mrt_blend_opt[i]);

      /* RB+ doesn't work with dual source blending, logic op, and RESOLVE. */
      if (blend->dual_src_blend || logicop_enable || mode == V_028808_CB_RESOLVE)
         color_control |= S_028808_DISABLE_DUAL_QUAD(1);
   }

   si_pm4_set_reg(pm4, R_028808_CB_COLOR_CONTROL, color_control);
   return blend;
}

static void *si_create_blend_state(struct pipe_context *ctx, const struct pipe_blend_state *state)
{
   return si_create_blend_state_mode(ctx, state, V_028808_CB_NORMAL);
}

static void si_draw_blend_dst_sampler_noop(struct pipe_context *ctx,
                                           const struct pipe_draw_info *info,
                                           unsigned drawid_offset,
                                           const struct pipe_draw_indirect_info *indirect,
                                           const struct pipe_draw_start_count_bias *draws,
                                           unsigned num_draws) {
   struct si_context *sctx = (struct si_context *)ctx;

   if (sctx->framebuffer.state.nr_cbufs == 1) {
      struct si_shader_selector *sel = sctx->shader.ps.cso;
      bool free_nir;
      if (unlikely(sel->info.writes_1_if_tex_is_1 == 0xff)) {
         struct nir_shader *nir = si_get_nir_shader(sel, NULL, &free_nir);

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

         if (free_nir)
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
               return;
            }
            /* TODO: handle color textures */
         }
      }
   }

   sctx->real_draw_vbo(ctx, info, drawid_offset, indirect, draws, num_draws);
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

   if (old_blend->cb_target_mask != blend->cb_target_mask ||
       old_blend->alpha_to_coverage != blend->alpha_to_coverage ||
       old_blend->alpha_to_one != blend->alpha_to_one ||
       old_blend->dual_src_blend != blend->dual_src_blend ||
       old_blend->blend_enable_4bit != blend->blend_enable_4bit ||
       old_blend->need_src_alpha_4bit != blend->need_src_alpha_4bit) {
      si_ps_key_update_framebuffer_blend(sctx);
      si_ps_key_update_blend_rasterizer(sctx);
      si_update_ps_inputs_read_or_disabled(sctx);
      sctx->do_update_shaders = true;
   }

   if (sctx->screen->dpbb_allowed &&
       (old_blend->alpha_to_coverage != blend->alpha_to_coverage ||
        old_blend->blend_enable_4bit != blend->blend_enable_4bit ||
        old_blend->cb_target_enabled_4bit != blend->cb_target_enabled_4bit))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.dpbb_state);

   if (sctx->screen->has_out_of_order_rast &&
       ((old_blend->blend_enable_4bit != blend->blend_enable_4bit ||
         old_blend->cb_target_enabled_4bit != blend->cb_target_enabled_4bit ||
         old_blend->commutative_4bit != blend->commutative_4bit ||
         old_blend->logicop_enable != blend->logicop_enable)))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);

   if (likely(!radeon_uses_secure_bos(sctx->ws))) {
      if (unlikely(blend->allows_noop_optimization)) {
         si_install_draw_wrapper(sctx, si_draw_blend_dst_sampler_noop);
      } else {
         si_install_draw_wrapper(sctx, NULL);
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

static void si_emit_blend_color(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   radeon_begin(cs);
   radeon_set_context_reg_seq(cs, R_028414_CB_BLEND_RED, 4);
   radeon_emit_array(cs, (uint32_t *)sctx->blend_color.color, 4);
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

static void si_emit_clip_state(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   radeon_begin(cs);
   radeon_set_context_reg_seq(cs, R_0285BC_PA_CL_UCP_0_X, 6 * 4);
   radeon_emit_array(cs, (uint32_t *)sctx->clip_state.ucp, 6 * 4);
   radeon_end();
}

static void si_emit_clip_regs(struct si_context *sctx)
{
   struct si_shader *vs = si_get_vs(sctx)->current;
   struct si_shader_selector *vs_sel = vs->selector;
   struct si_shader_info *info = &vs_sel->info;
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
   bool window_space = info->stage == MESA_SHADER_VERTEX ?
                          info->base.vs.window_space_position : 0;
   unsigned clipdist_mask = vs_sel->clipdist_mask;
   unsigned ucp_mask = clipdist_mask ? 0 : rs->clip_plane_enable & SIX_BITS;
   unsigned culldist_mask = vs_sel->culldist_mask;
   unsigned vs_out_mask = (clipdist_mask & ~vs->key.opt.kill_clip_distances) | culldist_mask;

   /* Clip distances on points have no effect, so need to be implemented
    * as cull distances. This applies for the clipvertex case as well.
    *
    * Setting this for primitives other than points should have no adverse
    * effects.
    */
   clipdist_mask &= rs->clip_plane_enable;
   culldist_mask |= clipdist_mask;

   unsigned pa_cl_cntl = S_02881C_VS_OUT_CCDIST0_VEC_ENA((vs_out_mask & 0x0F) != 0) |
                         S_02881C_VS_OUT_CCDIST1_VEC_ENA((vs_out_mask & 0xF0) != 0) |
                         S_02881C_BYPASS_VTX_RATE_COMBINER(sctx->chip_class >= GFX10_3 &&
                                                           !sctx->screen->options.vrs2x2) |
                         S_02881C_BYPASS_PRIM_RATE_COMBINER(sctx->chip_class >= GFX10_3) |
                         clipdist_mask | (culldist_mask << 8);

   radeon_begin(&sctx->gfx_cs);

   if (sctx->chip_class >= GFX10) {
      radeon_opt_set_context_reg_rmw(sctx, R_02881C_PA_CL_VS_OUT_CNTL,
                                     SI_TRACKED_PA_CL_VS_OUT_CNTL__CL, pa_cl_cntl,
                                     ~SI_TRACKED_PA_CL_VS_OUT_CNTL__VS_MASK);
   } else {
      radeon_opt_set_context_reg(sctx, R_02881C_PA_CL_VS_OUT_CNTL, SI_TRACKED_PA_CL_VS_OUT_CNTL__CL,
                                 vs_sel->pa_cl_vs_out_cntl | pa_cl_cntl);
   }
   radeon_opt_set_context_reg(sctx, R_028810_PA_CL_CLIP_CNTL, SI_TRACKED_PA_CL_CLIP_CNTL,
                              rs->pa_cl_clip_cntl | ucp_mask | S_028810_CLIP_DISABLE(window_space));
   radeon_end_update_context_roll(sctx);
}

/*
 * inferred state between framebuffer and rasterizer
 */
static void si_update_poly_offset_state(struct si_context *sctx)
{
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;

   if (!rs->uses_poly_offset || !sctx->framebuffer.state.zsbuf) {
      si_pm4_bind_state(sctx, poly_offset, NULL);
      return;
   }

   /* Use the user format, not db_render_format, so that the polygon
    * offset behaves as expected by applications.
    */
   switch (sctx->framebuffer.state.zsbuf->texture->format) {
   case PIPE_FORMAT_Z16_UNORM:
      si_pm4_bind_state(sctx, poly_offset, &rs->pm4_poly_offset[0]);
      break;
   default: /* 24-bit */
      si_pm4_bind_state(sctx, poly_offset, &rs->pm4_poly_offset[1]);
      break;
   case PIPE_FORMAT_Z32_FLOAT:
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      si_pm4_bind_state(sctx, poly_offset, &rs->pm4_poly_offset[2]);
      break;
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
   struct si_pm4_state *pm4 = &rs->pm4;
   unsigned tmp, i;
   float psize_min, psize_max;

   if (!rs) {
      return NULL;
   }

   rs->scissor_enable = state->scissor;
   rs->clip_halfz = state->clip_halfz;
   rs->two_side = state->light_twoside;
   rs->multisample_enable = state->multisample;
   rs->force_persample_interp = state->force_persample_interp;
   rs->clip_plane_enable = state->clip_plane_enable;
   rs->half_pixel_center = state->half_pixel_center;
   rs->line_stipple_enable = state->line_stipple_enable;
   rs->poly_stipple_enable = state->poly_stipple_enable;
   rs->line_smooth = state->line_smooth;
   rs->line_width = state->line_width;
   rs->poly_smooth = state->poly_smooth;
   rs->uses_poly_offset = state->offset_point || state->offset_line || state->offset_tri;
   rs->clamp_fragment_color = state->clamp_fragment_color;
   rs->clamp_vertex_color = state->clamp_vertex_color;
   rs->flatshade = state->flatshade;
   rs->flatshade_first = state->flatshade_first;
   rs->sprite_coord_enable = state->sprite_coord_enable;
   rs->rasterizer_discard = state->rasterizer_discard;
   rs->polygon_mode_is_lines =
      (state->fill_front == PIPE_POLYGON_MODE_LINE && !(state->cull_face & PIPE_FACE_FRONT)) ||
      (state->fill_back == PIPE_POLYGON_MODE_LINE && !(state->cull_face & PIPE_FACE_BACK));
   rs->polygon_mode_is_points =
      (state->fill_front == PIPE_POLYGON_MODE_POINT && !(state->cull_face & PIPE_FACE_FRONT)) ||
      (state->fill_back == PIPE_POLYGON_MODE_POINT && !(state->cull_face & PIPE_FACE_BACK));
   rs->pa_sc_line_stipple = state->line_stipple_enable
                               ? S_028A0C_LINE_PATTERN(state->line_stipple_pattern) |
                                    S_028A0C_REPEAT_COUNT(state->line_stipple_factor)
                               : 0;
   rs->pa_cl_clip_cntl = S_028810_DX_CLIP_SPACE_DEF(state->clip_halfz) |
                         S_028810_ZCLIP_NEAR_DISABLE(!state->depth_clip_near) |
                         S_028810_ZCLIP_FAR_DISABLE(!state->depth_clip_far) |
                         S_028810_DX_RASTERIZATION_KILL(state->rasterizer_discard) |
                         S_028810_DX_LINEAR_ATTR_CLIP_ENA(1);

   if (rs->rasterizer_discard) {
      rs->ngg_cull_flags = SI_NGG_CULL_ENABLED |
                           SI_NGG_CULL_FRONT_FACE |
                           SI_NGG_CULL_BACK_FACE;
      rs->ngg_cull_flags_y_inverted = rs->ngg_cull_flags;
   } else {
      rs->ngg_cull_flags = SI_NGG_CULL_ENABLED;
      rs->ngg_cull_flags_y_inverted = rs->ngg_cull_flags;

      bool cull_front, cull_back;

      if (!state->front_ccw) {
         cull_front = !!(state->cull_face & PIPE_FACE_FRONT);
         cull_back = !!(state->cull_face & PIPE_FACE_BACK);
      } else {
         cull_back = !!(state->cull_face & PIPE_FACE_FRONT);
         cull_front = !!(state->cull_face & PIPE_FACE_BACK);
      }

      if (cull_front) {
         rs->ngg_cull_flags |= SI_NGG_CULL_FRONT_FACE;
         rs->ngg_cull_flags_y_inverted |= SI_NGG_CULL_BACK_FACE;
      }

      if (cull_back) {
         rs->ngg_cull_flags |= SI_NGG_CULL_BACK_FACE;
         rs->ngg_cull_flags_y_inverted |= SI_NGG_CULL_FRONT_FACE;
      }
   }

   si_pm4_set_reg(
      pm4, R_0286D4_SPI_INTERP_CONTROL_0,
      S_0286D4_FLAT_SHADE_ENA(1) | S_0286D4_PNT_SPRITE_ENA(state->point_quad_rasterization) |
         S_0286D4_PNT_SPRITE_OVRD_X(V_0286D4_SPI_PNT_SPRITE_SEL_S) |
         S_0286D4_PNT_SPRITE_OVRD_Y(V_0286D4_SPI_PNT_SPRITE_SEL_T) |
         S_0286D4_PNT_SPRITE_OVRD_Z(V_0286D4_SPI_PNT_SPRITE_SEL_0) |
         S_0286D4_PNT_SPRITE_OVRD_W(V_0286D4_SPI_PNT_SPRITE_SEL_1) |
         S_0286D4_PNT_SPRITE_TOP_1(state->sprite_coord_mode != PIPE_SPRITE_COORD_UPPER_LEFT));

   /* point size 12.4 fixed point */
   tmp = (unsigned)(state->point_size * 8.0);
   si_pm4_set_reg(pm4, R_028A00_PA_SU_POINT_SIZE, S_028A00_HEIGHT(tmp) | S_028A00_WIDTH(tmp));

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
   si_pm4_set_reg(pm4, R_028A04_PA_SU_POINT_MINMAX,
                  S_028A04_MIN_SIZE(si_pack_float_12p4(psize_min / 2)) |
                     S_028A04_MAX_SIZE(si_pack_float_12p4(psize_max / 2)));

   si_pm4_set_reg(pm4, R_028A08_PA_SU_LINE_CNTL,
                  S_028A08_WIDTH(si_pack_float_12p4(state->line_width / 2)));
   si_pm4_set_reg(
      pm4, R_028A48_PA_SC_MODE_CNTL_0,
      S_028A48_LINE_STIPPLE_ENABLE(state->line_stipple_enable) |
         S_028A48_MSAA_ENABLE(state->multisample || state->poly_smooth || state->line_smooth) |
         S_028A48_VPORT_SCISSOR_ENABLE(1) |
         S_028A48_ALTERNATE_RBS_PER_TILE(sscreen->info.chip_class >= GFX9));

   bool polygon_mode_enabled =
      (state->fill_front != PIPE_POLYGON_MODE_FILL && !(state->cull_face & PIPE_FACE_FRONT)) ||
      (state->fill_back != PIPE_POLYGON_MODE_FILL && !(state->cull_face & PIPE_FACE_BACK));

   si_pm4_set_reg(pm4, R_028814_PA_SU_SC_MODE_CNTL,
                  S_028814_PROVOKING_VTX_LAST(!state->flatshade_first) |
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
                     S_028814_KEEP_TOGETHER_ENABLE(sscreen->info.chip_class >= GFX10 ? polygon_mode_enabled : 0));

   if (!rs->uses_poly_offset)
      return rs;

   rs->pm4_poly_offset = CALLOC(3, sizeof(struct si_pm4_state));
   if (!rs->pm4_poly_offset) {
      FREE(rs);
      return NULL;
   }

   /* Precalculate polygon offset states for 16-bit, 24-bit, and 32-bit zbuffers. */
   for (i = 0; i < 3; i++) {
      struct si_pm4_state *pm4 = &rs->pm4_poly_offset[i];
      float offset_units = state->offset_units;
      float offset_scale = state->offset_scale * 16.0f;
      uint32_t pa_su_poly_offset_db_fmt_cntl = 0;

      if (!state->offset_units_unscaled) {
         switch (i) {
         case 0: /* 16-bit zbuffer */
            offset_units *= 4.0f;
            pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-16);
            break;
         case 1: /* 24-bit zbuffer */
            offset_units *= 2.0f;
            pa_su_poly_offset_db_fmt_cntl = S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-24);
            break;
         case 2: /* 32-bit zbuffer */
            offset_units *= 1.0f;
            pa_su_poly_offset_db_fmt_cntl =
               S_028B78_POLY_OFFSET_NEG_NUM_DB_BITS(-23) | S_028B78_POLY_OFFSET_DB_IS_FLOAT_FMT(1);
            break;
         }
      }

      si_pm4_set_reg(pm4, R_028B78_PA_SU_POLY_OFFSET_DB_FMT_CNTL, pa_su_poly_offset_db_fmt_cntl);
      si_pm4_set_reg(pm4, R_028B7C_PA_SU_POLY_OFFSET_CLAMP, fui(state->offset_clamp));
      si_pm4_set_reg(pm4, R_028B80_PA_SU_POLY_OFFSET_FRONT_SCALE, fui(offset_scale));
      si_pm4_set_reg(pm4, R_028B84_PA_SU_POLY_OFFSET_FRONT_OFFSET, fui(offset_units));
      si_pm4_set_reg(pm4, R_028B88_PA_SU_POLY_OFFSET_BACK_SCALE, fui(offset_scale));
      si_pm4_set_reg(pm4, R_028B8C_PA_SU_POLY_OFFSET_BACK_OFFSET, fui(offset_units));
   }

   return rs;
}

static void si_bind_rs_state(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_state_rasterizer *old_rs = (struct si_state_rasterizer *)sctx->queued.named.rasterizer;
   struct si_state_rasterizer *rs = (struct si_state_rasterizer *)state;

   if (!rs)
      rs = (struct si_state_rasterizer *)sctx->discard_rasterizer_state;

   if (old_rs->multisample_enable != rs->multisample_enable) {
      si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);

      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);

      /* Update the small primitive filter workaround if necessary. */
      if (sctx->screen->info.has_msaa_sample_loc_bug && sctx->framebuffer.nr_samples > 1)
         si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_sample_locs);

      /* NGG cull state uses multisample_enable. */
      if (sctx->screen->use_ngg_culling)
         si_mark_atom_dirty(sctx, &sctx->atoms.s.ngg_cull_state);
   }

   sctx->current_vs_state &= C_VS_STATE_CLAMP_VERTEX_COLOR;
   sctx->current_vs_state |= S_VS_STATE_CLAMP_VERTEX_COLOR(rs->clamp_vertex_color);

   si_pm4_bind_state(sctx, rasterizer, rs);
   si_update_poly_offset_state(sctx);

   if (old_rs->scissor_enable != rs->scissor_enable)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.scissors);

   if (old_rs->line_width != rs->line_width || old_rs->max_point_size != rs->max_point_size ||
       old_rs->half_pixel_center != rs->half_pixel_center)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.guardband);

   if (old_rs->clip_halfz != rs->clip_halfz)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.viewports);

   if (old_rs->clip_plane_enable != rs->clip_plane_enable ||
       old_rs->pa_cl_clip_cntl != rs->pa_cl_clip_cntl)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.clip_regs);

   if (old_rs->sprite_coord_enable != rs->sprite_coord_enable ||
       old_rs->flatshade != rs->flatshade)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.spi_map);

   if (old_rs->clip_plane_enable != rs->clip_plane_enable ||
       old_rs->rasterizer_discard != rs->rasterizer_discard ||
       old_rs->sprite_coord_enable != rs->sprite_coord_enable ||
       old_rs->flatshade != rs->flatshade || old_rs->two_side != rs->two_side ||
       old_rs->multisample_enable != rs->multisample_enable ||
       old_rs->poly_stipple_enable != rs->poly_stipple_enable ||
       old_rs->poly_smooth != rs->poly_smooth || old_rs->line_smooth != rs->line_smooth ||
       old_rs->clamp_fragment_color != rs->clamp_fragment_color ||
       old_rs->force_persample_interp != rs->force_persample_interp ||
       old_rs->polygon_mode_is_points != rs->polygon_mode_is_points) {
      si_ps_key_update_blend_rasterizer(sctx);
      si_ps_key_update_rasterizer(sctx);
      si_ps_key_update_framebuffer_rasterizer_sample_shading(sctx);
      si_update_ps_inputs_read_or_disabled(sctx);
      sctx->do_update_shaders = true;
   }

   if (old_rs->line_smooth != rs->line_smooth ||
       old_rs->poly_smooth != rs->poly_smooth ||
       old_rs->poly_stipple_enable != rs->poly_stipple_enable ||
       old_rs->flatshade != rs->flatshade)
      si_update_vrs_flat_shading(sctx);
}

static void si_delete_rs_state(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_state_rasterizer *rs = (struct si_state_rasterizer *)state;

   if (sctx->queued.named.rasterizer == state)
      si_bind_rs_state(ctx, sctx->discard_rasterizer_state);

   FREE(rs->pm4_poly_offset);
   si_pm4_free_state(sctx, &rs->pm4, SI_STATE_IDX(rasterizer));
}

/*
 * inferred state between dsa and stencil ref
 */
static void si_emit_stencil_ref(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct pipe_stencil_ref *ref = &sctx->stencil_ref.state;
   struct si_dsa_stencil_ref_part *dsa = &sctx->stencil_ref.dsa_part;

   radeon_begin(cs);
   radeon_set_context_reg_seq(cs, R_028430_DB_STENCILREFMASK, 2);
   radeon_emit(cs, S_028430_STENCILTESTVAL(ref->ref_value[0]) |
                      S_028430_STENCILMASK(dsa->valuemask[0]) |
                      S_028430_STENCILWRITEMASK(dsa->writemask[0]) | S_028430_STENCILOPVAL(1));
   radeon_emit(cs, S_028434_STENCILTESTVAL_BF(ref->ref_value[1]) |
                      S_028434_STENCILMASK_BF(dsa->valuemask[1]) |
                      S_028434_STENCILWRITEMASK_BF(dsa->writemask[1]) |
                      S_028434_STENCILOPVAL_BF(1));
   radeon_end();
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
   struct si_pm4_state *pm4 = &dsa->pm4;
   unsigned db_depth_control;
   uint32_t db_stencil_control = 0;

   if (!dsa) {
      return NULL;
   }

   dsa->stencil_ref.valuemask[0] = state->stencil[0].valuemask;
   dsa->stencil_ref.valuemask[1] = state->stencil[1].valuemask;
   dsa->stencil_ref.writemask[0] = state->stencil[0].writemask;
   dsa->stencil_ref.writemask[1] = state->stencil[1].writemask;

   db_depth_control =
      S_028800_Z_ENABLE(state->depth_enabled) | S_028800_Z_WRITE_ENABLE(state->depth_writemask) |
      S_028800_ZFUNC(state->depth_func) | S_028800_DEPTH_BOUNDS_ENABLE(state->depth_bounds_test);

   /* stencil */
   if (state->stencil[0].enabled) {
      db_depth_control |= S_028800_STENCIL_ENABLE(1);
      db_depth_control |= S_028800_STENCILFUNC(state->stencil[0].func);
      db_stencil_control |=
         S_02842C_STENCILFAIL(si_translate_stencil_op(state->stencil[0].fail_op));
      db_stencil_control |=
         S_02842C_STENCILZPASS(si_translate_stencil_op(state->stencil[0].zpass_op));
      db_stencil_control |=
         S_02842C_STENCILZFAIL(si_translate_stencil_op(state->stencil[0].zfail_op));

      if (state->stencil[1].enabled) {
         db_depth_control |= S_028800_BACKFACE_ENABLE(1);
         db_depth_control |= S_028800_STENCILFUNC_BF(state->stencil[1].func);
         db_stencil_control |=
            S_02842C_STENCILFAIL_BF(si_translate_stencil_op(state->stencil[1].fail_op));
         db_stencil_control |=
            S_02842C_STENCILZPASS_BF(si_translate_stencil_op(state->stencil[1].zpass_op));
         db_stencil_control |=
            S_02842C_STENCILZFAIL_BF(si_translate_stencil_op(state->stencil[1].zfail_op));
      }
   }

   /* alpha */
   if (state->alpha_enabled) {
      dsa->alpha_func = state->alpha_func;

      si_pm4_set_reg(pm4, R_00B030_SPI_SHADER_USER_DATA_PS_0 + SI_SGPR_ALPHA_REF * 4,
                     fui(state->alpha_ref_value));
   } else {
      dsa->alpha_func = PIPE_FUNC_ALWAYS;
   }

   si_pm4_set_reg(pm4, R_028800_DB_DEPTH_CONTROL, db_depth_control);
   if (state->stencil[0].enabled)
      si_pm4_set_reg(pm4, R_02842C_DB_STENCIL_CONTROL, db_stencil_control);
   if (state->depth_bounds_test) {
      si_pm4_set_reg(pm4, R_028020_DB_DEPTH_BOUNDS_MIN, fui(state->depth_bounds_min));
      si_pm4_set_reg(pm4, R_028024_DB_DEPTH_BOUNDS_MAX, fui(state->depth_bounds_max));
   }

   dsa->depth_enabled = state->depth_enabled;
   dsa->depth_write_enabled = state->depth_enabled && state->depth_writemask;
   dsa->stencil_enabled = state->stencil[0].enabled;
   dsa->stencil_write_enabled =
      (util_writes_stencil(&state->stencil[0]) || util_writes_stencil(&state->stencil[1]));
   dsa->db_can_write = dsa->depth_write_enabled || dsa->stencil_write_enabled;

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

   dsa->order_invariance[1].pass_last = sctx->screen->assume_no_z_fights &&
                                        !dsa->stencil_write_enabled && dsa->depth_write_enabled &&
                                        zfunc_is_ordered;
   dsa->order_invariance[0].pass_last =
      sctx->screen->assume_no_z_fights && dsa->depth_write_enabled && zfunc_is_ordered;

   return dsa;
}

static void si_bind_dsa_state(struct pipe_context *ctx, void *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_state_dsa *old_dsa = sctx->queued.named.dsa;
   struct si_state_dsa *dsa = state;

   if (!dsa)
      dsa = (struct si_state_dsa *)sctx->noop_dsa;

   si_pm4_bind_state(sctx, dsa, dsa);

   if (memcmp(&dsa->stencil_ref, &sctx->stencil_ref.dsa_part,
              sizeof(struct si_dsa_stencil_ref_part)) != 0) {
      sctx->stencil_ref.dsa_part = dsa->stencil_ref;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.stencil_ref);
   }

   if (old_dsa->alpha_func != dsa->alpha_func) {
      si_ps_key_update_dsa(sctx);
      si_update_ps_inputs_read_or_disabled(sctx);
      si_update_ps_kill_enable(sctx);
      sctx->do_update_shaders = true;
   }

   if (sctx->screen->dpbb_allowed && ((old_dsa->depth_enabled != dsa->depth_enabled ||
                                       old_dsa->stencil_enabled != dsa->stencil_enabled ||
                                       old_dsa->db_can_write != dsa->db_can_write)))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.dpbb_state);

   if (sctx->screen->has_out_of_order_rast &&
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
      sctx->flags &= ~SI_CONTEXT_STOP_PIPELINE_STATS;
      sctx->flags |= SI_CONTEXT_START_PIPELINE_STATS;
   } else {
      sctx->flags &= ~SI_CONTEXT_START_PIPELINE_STATS;
      sctx->flags |= SI_CONTEXT_STOP_PIPELINE_STATS;
   }

   /* Occlusion queries. */
   if (sctx->occlusion_queries_disabled != !enable) {
      sctx->occlusion_queries_disabled = !enable;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);
   }
}

void si_set_occlusion_query_state(struct si_context *sctx, bool old_perfect_enable)
{
   si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);

   bool perfect_enable = sctx->num_perfect_occlusion_queries != 0;

   if (perfect_enable != old_perfect_enable)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);
}

void si_save_qbo_state(struct si_context *sctx, struct si_qbo_state *st)
{
   si_get_pipe_constant_buffer(sctx, PIPE_SHADER_COMPUTE, 0, &st->saved_const0);
}

void si_restore_qbo_state(struct si_context *sctx, struct si_qbo_state *st)
{
   sctx->b.set_constant_buffer(&sctx->b, PIPE_SHADER_COMPUTE, 0, true, &st->saved_const0);
}

static void si_emit_db_render_state(struct si_context *sctx)
{
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
   unsigned db_shader_control, db_render_control, db_count_control;

   /* DB_RENDER_CONTROL */
   if (sctx->dbcb_depth_copy_enabled || sctx->dbcb_stencil_copy_enabled) {
      db_render_control = S_028000_DEPTH_COPY(sctx->dbcb_depth_copy_enabled) |
                          S_028000_STENCIL_COPY(sctx->dbcb_stencil_copy_enabled) |
                          S_028000_COPY_CENTROID(1) | S_028000_COPY_SAMPLE(sctx->dbcb_copy_sample);
   } else if (sctx->db_flush_depth_inplace || sctx->db_flush_stencil_inplace) {
      db_render_control = S_028000_DEPTH_COMPRESS_DISABLE(sctx->db_flush_depth_inplace) |
                          S_028000_STENCIL_COMPRESS_DISABLE(sctx->db_flush_stencil_inplace);
   } else {
      db_render_control = S_028000_DEPTH_CLEAR_ENABLE(sctx->db_depth_clear) |
                          S_028000_STENCIL_CLEAR_ENABLE(sctx->db_stencil_clear);
   }

   /* DB_COUNT_CONTROL (occlusion queries) */
   if (sctx->num_occlusion_queries > 0 && !sctx->occlusion_queries_disabled) {
      bool perfect = sctx->num_perfect_occlusion_queries > 0;
      bool gfx10_perfect = sctx->chip_class >= GFX10 && perfect;

      if (sctx->chip_class >= GFX7) {
         unsigned log_sample_rate = sctx->framebuffer.log_samples;

         db_count_control = S_028004_PERFECT_ZPASS_COUNTS(perfect) |
                            S_028004_DISABLE_CONSERVATIVE_ZPASS_COUNTS(gfx10_perfect) |
                            S_028004_SAMPLE_RATE(log_sample_rate) | S_028004_ZPASS_ENABLE(1) |
                            S_028004_SLICE_EVEN_ENABLE(1) | S_028004_SLICE_ODD_ENABLE(1);
      } else {
         db_count_control = S_028004_PERFECT_ZPASS_COUNTS(perfect) |
                            S_028004_SAMPLE_RATE(sctx->framebuffer.log_samples);
      }
   } else {
      /* Disable occlusion queries. */
      if (sctx->chip_class >= GFX7) {
         db_count_control = 0;
      } else {
         db_count_control = S_028004_ZPASS_INCREMENT_DISABLE(1);
      }
   }

   radeon_begin(&sctx->gfx_cs);
   radeon_opt_set_context_reg2(sctx, R_028000_DB_RENDER_CONTROL, SI_TRACKED_DB_RENDER_CONTROL,
                               db_render_control, db_count_control);

   /* DB_RENDER_OVERRIDE2 */
   radeon_opt_set_context_reg(
      sctx, R_028010_DB_RENDER_OVERRIDE2, SI_TRACKED_DB_RENDER_OVERRIDE2,
      S_028010_DISABLE_ZMASK_EXPCLEAR_OPTIMIZATION(sctx->db_depth_disable_expclear) |
      S_028010_DISABLE_SMEM_EXPCLEAR_OPTIMIZATION(sctx->db_stencil_disable_expclear) |
      S_028010_DECOMPRESS_Z_ON_FLUSH(sctx->framebuffer.nr_samples >= 4) |
      S_028010_CENTROID_COMPUTATION_MODE(sctx->chip_class >= GFX10_3 ? 1 : 0));

   db_shader_control = sctx->ps_db_shader_control;

   /* Bug workaround for smoothing (overrasterization) on GFX6. */
   if (sctx->chip_class == GFX6 && sctx->smoothing_enabled) {
      db_shader_control &= C_02880C_Z_ORDER;
      db_shader_control |= S_02880C_Z_ORDER(V_02880C_LATE_Z);
   }

   /* Disable the gl_SampleMask fragment shader output if MSAA is disabled. */
   if (!rs->multisample_enable)
      db_shader_control &= C_02880C_MASK_EXPORT_ENABLE;

   if (sctx->screen->info.has_rbplus && !sctx->screen->info.rbplus_allowed)
      db_shader_control |= S_02880C_DUAL_QUAD_DISABLE(1);

   radeon_opt_set_context_reg(sctx, R_02880C_DB_SHADER_CONTROL, SI_TRACKED_DB_SHADER_CONTROL,
                              db_shader_control);

   if (sctx->chip_class >= GFX10_3) {
      if (sctx->allow_flat_shading) {
         radeon_opt_set_context_reg(sctx, R_028064_DB_VRS_OVERRIDE_CNTL,
                                    SI_TRACKED_DB_VRS_OVERRIDE_CNTL,
                                    S_028064_VRS_OVERRIDE_RATE_COMBINER_MODE(
                                    V_028064_VRS_COMB_MODE_OVERRIDE) |
                                    S_028064_VRS_OVERRIDE_RATE_X(1) |
                                    S_028064_VRS_OVERRIDE_RATE_Y(1));
      } else {
         /* If the shader is using discard, turn off coarse shading because
          * discard at 2x2 pixel granularity degrades quality too much.
          *
          * MIN allows sample shading but not coarse shading.
          */
         unsigned mode = sctx->screen->options.vrs2x2 && G_02880C_KILL_ENABLE(db_shader_control) ?
            V_028064_VRS_COMB_MODE_MIN : V_028064_VRS_COMB_MODE_PASSTHRU;

         radeon_opt_set_context_reg(sctx, R_028064_DB_VRS_OVERRIDE_CNTL,
                                    SI_TRACKED_DB_VRS_OVERRIDE_CNTL,
                                    S_028064_VRS_OVERRIDE_RATE_COMBINER_MODE(mode) |
                                    S_028064_VRS_OVERRIDE_RATE_X(0) |
                                    S_028064_VRS_OVERRIDE_RATE_Y(0));
      }
   }
   radeon_end_update_context_roll(sctx);
}

/*
 * format translation
 */
static uint32_t si_translate_colorformat(enum chip_class chip_class,
                                         enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   if (!desc)
      return V_028C70_COLOR_INVALID;

#define HAS_SIZE(x, y, z, w)                                                                       \
   (desc->channel[0].size == (x) && desc->channel[1].size == (y) &&                                \
    desc->channel[2].size == (z) && desc->channel[3].size == (w))

   if (format == PIPE_FORMAT_R11G11B10_FLOAT) /* isn't plain */
      return V_028C70_COLOR_10_11_11;

   if (chip_class >= GFX10_3 &&
       format == PIPE_FORMAT_R9G9B9E5_FLOAT) /* isn't plain */
      return V_028C70_COLOR_5_9_9_9;

   if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
      return V_028C70_COLOR_INVALID;

   /* hw cannot support mixed formats (except depth/stencil, since
    * stencil is not written to). */
   if (desc->is_mixed && desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
      return V_028C70_COLOR_INVALID;

   switch (desc->nr_channels) {
   case 1:
      switch (desc->channel[0].size) {
      case 8:
         return V_028C70_COLOR_8;
      case 16:
         return V_028C70_COLOR_16;
      case 32:
         return V_028C70_COLOR_32;
      }
      break;
   case 2:
      if (desc->channel[0].size == desc->channel[1].size) {
         switch (desc->channel[0].size) {
         case 8:
            return V_028C70_COLOR_8_8;
         case 16:
            return V_028C70_COLOR_16_16;
         case 32:
            return V_028C70_COLOR_32_32;
         }
      } else if (HAS_SIZE(8, 24, 0, 0)) {
         return V_028C70_COLOR_24_8;
      } else if (HAS_SIZE(24, 8, 0, 0)) {
         return V_028C70_COLOR_8_24;
      }
      break;
   case 3:
      if (HAS_SIZE(5, 6, 5, 0)) {
         return V_028C70_COLOR_5_6_5;
      } else if (HAS_SIZE(32, 8, 24, 0)) {
         return V_028C70_COLOR_X24_8_32_FLOAT;
      }
      break;
   case 4:
      if (desc->channel[0].size == desc->channel[1].size &&
          desc->channel[0].size == desc->channel[2].size &&
          desc->channel[0].size == desc->channel[3].size) {
         switch (desc->channel[0].size) {
         case 4:
            return V_028C70_COLOR_4_4_4_4;
         case 8:
            return V_028C70_COLOR_8_8_8_8;
         case 16:
            return V_028C70_COLOR_16_16_16_16;
         case 32:
            return V_028C70_COLOR_32_32_32_32;
         }
      } else if (HAS_SIZE(5, 5, 5, 1)) {
         return V_028C70_COLOR_1_5_5_5;
      } else if (HAS_SIZE(1, 5, 5, 5)) {
         return V_028C70_COLOR_5_5_5_1;
      } else if (HAS_SIZE(10, 10, 10, 2)) {
         return V_028C70_COLOR_2_10_10_10;
      }
      break;
   }
   return V_028C70_COLOR_INVALID;
}

static uint32_t si_colorformat_endian_swap(uint32_t colorformat)
{
   if (SI_BIG_ENDIAN) {
      switch (colorformat) {
      /* 8-bit buffers. */
      case V_028C70_COLOR_8:
         return V_028C70_ENDIAN_NONE;

      /* 16-bit buffers. */
      case V_028C70_COLOR_5_6_5:
      case V_028C70_COLOR_1_5_5_5:
      case V_028C70_COLOR_4_4_4_4:
      case V_028C70_COLOR_16:
      case V_028C70_COLOR_8_8:
         return V_028C70_ENDIAN_8IN16;

      /* 32-bit buffers. */
      case V_028C70_COLOR_8_8_8_8:
      case V_028C70_COLOR_2_10_10_10:
      case V_028C70_COLOR_8_24:
      case V_028C70_COLOR_24_8:
      case V_028C70_COLOR_16_16:
         return V_028C70_ENDIAN_8IN32;

      /* 64-bit buffers. */
      case V_028C70_COLOR_16_16_16_16:
         return V_028C70_ENDIAN_8IN16;

      case V_028C70_COLOR_32_32:
         return V_028C70_ENDIAN_8IN32;

      /* 128-bit buffers. */
      case V_028C70_COLOR_32_32_32_32:
         return V_028C70_ENDIAN_8IN32;
      default:
         return V_028C70_ENDIAN_NONE; /* Unsupported. */
      }
   } else {
      return V_028C70_ENDIAN_NONE;
   }
}

static uint32_t si_translate_dbformat(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_Z16_UNORM:
      return V_028040_Z_16;
   case PIPE_FORMAT_S8_UINT_Z24_UNORM:
   case PIPE_FORMAT_X8Z24_UNORM:
   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      return V_028040_Z_24; /* deprecated on AMD GCN */
   case PIPE_FORMAT_Z32_FLOAT:
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      return V_028040_Z_32_FLOAT;
   default:
      return V_028040_Z_INVALID;
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
   bool uniform = true;
   int i;

   assert(sscreen->info.chip_class <= GFX9);

   /* Colorspace (return non-RGB formats directly). */
   switch (desc->colorspace) {
   /* Depth stencil formats */
   case UTIL_FORMAT_COLORSPACE_ZS:
      switch (format) {
      case PIPE_FORMAT_Z16_UNORM:
         return V_008F14_IMG_DATA_FORMAT_16;
      case PIPE_FORMAT_X24S8_UINT:
      case PIPE_FORMAT_S8X24_UINT:
         /*
          * Implemented as an 8_8_8_8 data format to fix texture
          * gathers in stencil sampling. This affects at least
          * GL45-CTS.texture_cube_map_array.sampling on GFX8.
          */
         if (sscreen->info.chip_class <= GFX8)
            return V_008F14_IMG_DATA_FORMAT_8_8_8_8;

         if (format == PIPE_FORMAT_X24S8_UINT)
            return V_008F14_IMG_DATA_FORMAT_8_24;
         else
            return V_008F14_IMG_DATA_FORMAT_24_8;
      case PIPE_FORMAT_Z24X8_UNORM:
      case PIPE_FORMAT_Z24_UNORM_S8_UINT:
         return V_008F14_IMG_DATA_FORMAT_8_24;
      case PIPE_FORMAT_X8Z24_UNORM:
      case PIPE_FORMAT_S8_UINT_Z24_UNORM:
         return V_008F14_IMG_DATA_FORMAT_24_8;
      case PIPE_FORMAT_S8_UINT:
         return V_008F14_IMG_DATA_FORMAT_8;
      case PIPE_FORMAT_Z32_FLOAT:
         return V_008F14_IMG_DATA_FORMAT_32;
      case PIPE_FORMAT_X32_S8X24_UINT:
      case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
         return V_008F14_IMG_DATA_FORMAT_X24_8_32;
      default:
         goto out_unknown;
      }

   case UTIL_FORMAT_COLORSPACE_YUV:
      goto out_unknown; /* TODO */

   case UTIL_FORMAT_COLORSPACE_SRGB:
      if (desc->nr_channels != 4 && desc->nr_channels != 1)
         goto out_unknown;
      break;

   default:
      break;
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_RGTC) {
      if (!sscreen->info.has_format_bc1_through_bc7)
         goto out_unknown;

      switch (format) {
      case PIPE_FORMAT_RGTC1_SNORM:
      case PIPE_FORMAT_LATC1_SNORM:
      case PIPE_FORMAT_RGTC1_UNORM:
      case PIPE_FORMAT_LATC1_UNORM:
         return V_008F14_IMG_DATA_FORMAT_BC4;
      case PIPE_FORMAT_RGTC2_SNORM:
      case PIPE_FORMAT_LATC2_SNORM:
      case PIPE_FORMAT_RGTC2_UNORM:
      case PIPE_FORMAT_LATC2_UNORM:
         return V_008F14_IMG_DATA_FORMAT_BC5;
      default:
         goto out_unknown;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_ETC &&
       (sscreen->info.family == CHIP_STONEY || sscreen->info.family == CHIP_VEGA10 ||
        sscreen->info.family == CHIP_RAVEN || sscreen->info.family == CHIP_RAVEN2)) {
      switch (format) {
      case PIPE_FORMAT_ETC1_RGB8:
      case PIPE_FORMAT_ETC2_RGB8:
      case PIPE_FORMAT_ETC2_SRGB8:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RGB;
      case PIPE_FORMAT_ETC2_RGB8A1:
      case PIPE_FORMAT_ETC2_SRGB8A1:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RGBA1;
      case PIPE_FORMAT_ETC2_RGBA8:
      case PIPE_FORMAT_ETC2_SRGBA8:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RGBA;
      case PIPE_FORMAT_ETC2_R11_UNORM:
      case PIPE_FORMAT_ETC2_R11_SNORM:
         return V_008F14_IMG_DATA_FORMAT_ETC2_R;
      case PIPE_FORMAT_ETC2_RG11_UNORM:
      case PIPE_FORMAT_ETC2_RG11_SNORM:
         return V_008F14_IMG_DATA_FORMAT_ETC2_RG;
      default:
         goto out_unknown;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_BPTC) {
      if (!sscreen->info.has_format_bc1_through_bc7)
         goto out_unknown;

      switch (format) {
      case PIPE_FORMAT_BPTC_RGBA_UNORM:
      case PIPE_FORMAT_BPTC_SRGBA:
         return V_008F14_IMG_DATA_FORMAT_BC7;
      case PIPE_FORMAT_BPTC_RGB_FLOAT:
      case PIPE_FORMAT_BPTC_RGB_UFLOAT:
         return V_008F14_IMG_DATA_FORMAT_BC6;
      default:
         goto out_unknown;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
      switch (format) {
      case PIPE_FORMAT_R8G8_B8G8_UNORM:
      case PIPE_FORMAT_G8R8_B8R8_UNORM:
         return V_008F14_IMG_DATA_FORMAT_GB_GR;
      case PIPE_FORMAT_G8R8_G8B8_UNORM:
      case PIPE_FORMAT_R8G8_R8B8_UNORM:
         return V_008F14_IMG_DATA_FORMAT_BG_RG;
      default:
         goto out_unknown;
      }
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_S3TC) {
      if (!sscreen->info.has_format_bc1_through_bc7)
         goto out_unknown;

      switch (format) {
      case PIPE_FORMAT_DXT1_RGB:
      case PIPE_FORMAT_DXT1_RGBA:
      case PIPE_FORMAT_DXT1_SRGB:
      case PIPE_FORMAT_DXT1_SRGBA:
         return V_008F14_IMG_DATA_FORMAT_BC1;
      case PIPE_FORMAT_DXT3_RGBA:
      case PIPE_FORMAT_DXT3_SRGBA:
         return V_008F14_IMG_DATA_FORMAT_BC2;
      case PIPE_FORMAT_DXT5_RGBA:
      case PIPE_FORMAT_DXT5_SRGBA:
         return V_008F14_IMG_DATA_FORMAT_BC3;
      default:
         goto out_unknown;
      }
   }

   if (format == PIPE_FORMAT_R9G9B9E5_FLOAT) {
      return V_008F14_IMG_DATA_FORMAT_5_9_9_9;
   } else if (format == PIPE_FORMAT_R11G11B10_FLOAT) {
      return V_008F14_IMG_DATA_FORMAT_10_11_11;
   }

   /* R8G8Bx_SNORM - TODO CxV8U8 */

   /* hw cannot support mixed formats (except depth/stencil, since only
    * depth is read).*/
   if (desc->is_mixed && desc->colorspace != UTIL_FORMAT_COLORSPACE_ZS)
      goto out_unknown;

   /* See whether the components are of the same size. */
   for (i = 1; i < desc->nr_channels; i++) {
      uniform = uniform && desc->channel[0].size == desc->channel[i].size;
   }

   /* Non-uniform formats. */
   if (!uniform) {
      switch (desc->nr_channels) {
      case 3:
         if (desc->channel[0].size == 5 && desc->channel[1].size == 6 &&
             desc->channel[2].size == 5) {
            return V_008F14_IMG_DATA_FORMAT_5_6_5;
         }
         goto out_unknown;
      case 4:
         if (desc->channel[0].size == 5 && desc->channel[1].size == 5 &&
             desc->channel[2].size == 5 && desc->channel[3].size == 1) {
            return V_008F14_IMG_DATA_FORMAT_1_5_5_5;
         }
         if (desc->channel[0].size == 1 && desc->channel[1].size == 5 &&
             desc->channel[2].size == 5 && desc->channel[3].size == 5) {
            return V_008F14_IMG_DATA_FORMAT_5_5_5_1;
         }
         if (desc->channel[0].size == 10 && desc->channel[1].size == 10 &&
             desc->channel[2].size == 10 && desc->channel[3].size == 2) {
            return V_008F14_IMG_DATA_FORMAT_2_10_10_10;
         }
         goto out_unknown;
      }
      goto out_unknown;
   }

   if (first_non_void < 0 || first_non_void > 3)
      goto out_unknown;

   /* uniform formats */
   switch (desc->channel[first_non_void].size) {
   case 4:
      switch (desc->nr_channels) {
#if 0 /* Not supported for render targets */
      case 2:
         return V_008F14_IMG_DATA_FORMAT_4_4;
#endif
      case 4:
         return V_008F14_IMG_DATA_FORMAT_4_4_4_4;
      }
      break;
   case 8:
      switch (desc->nr_channels) {
      case 1:
         return V_008F14_IMG_DATA_FORMAT_8;
      case 2:
         return V_008F14_IMG_DATA_FORMAT_8_8;
      case 4:
         return V_008F14_IMG_DATA_FORMAT_8_8_8_8;
      }
      break;
   case 16:
      switch (desc->nr_channels) {
      case 1:
         return V_008F14_IMG_DATA_FORMAT_16;
      case 2:
         return V_008F14_IMG_DATA_FORMAT_16_16;
      case 4:
         return V_008F14_IMG_DATA_FORMAT_16_16_16_16;
      }
      break;
   case 32:
      switch (desc->nr_channels) {
      case 1:
         return V_008F14_IMG_DATA_FORMAT_32;
      case 2:
         return V_008F14_IMG_DATA_FORMAT_32_32;
#if 0 /* Not supported for render targets */
      case 3:
         return V_008F14_IMG_DATA_FORMAT_32_32_32;
#endif
      case 4:
         return V_008F14_IMG_DATA_FORMAT_32_32_32_32;
      }
   }

out_unknown:
   return ~0;
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

static unsigned si_tex_compare(unsigned compare)
{
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
       sscreen->info.chip_class == GFX9 &&
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

   if (sscreen->info.chip_class >= GFX10) {
      const struct gfx10_format *fmt = &gfx10_format_table[format];
      if (!fmt->img_format || fmt->buffers_only)
         return false;
      return true;
   }

   const struct util_format_description *desc = util_format_description(format);
   if (!desc)
      return false;

   return si_translate_texformat(screen, format, desc,
                                 util_format_get_first_non_void_channel(format)) != ~0U;
}

static uint32_t si_translate_buffer_dataformat(struct pipe_screen *screen,
                                               const struct util_format_description *desc,
                                               int first_non_void)
{
   int i;

   assert(((struct si_screen *)screen)->info.chip_class <= GFX9);

   if (desc->format == PIPE_FORMAT_R11G11B10_FLOAT)
      return V_008F0C_BUF_DATA_FORMAT_10_11_11;

   assert(first_non_void >= 0);

   if (desc->nr_channels == 4 && desc->channel[0].size == 10 && desc->channel[1].size == 10 &&
       desc->channel[2].size == 10 && desc->channel[3].size == 2)
      return V_008F0C_BUF_DATA_FORMAT_2_10_10_10;

   /* See whether the components are of the same size. */
   for (i = 0; i < desc->nr_channels; i++) {
      if (desc->channel[first_non_void].size != desc->channel[i].size)
         return V_008F0C_BUF_DATA_FORMAT_INVALID;
   }

   switch (desc->channel[first_non_void].size) {
   case 8:
      switch (desc->nr_channels) {
      case 1:
      case 3: /* 3 loads */
         return V_008F0C_BUF_DATA_FORMAT_8;
      case 2:
         return V_008F0C_BUF_DATA_FORMAT_8_8;
      case 4:
         return V_008F0C_BUF_DATA_FORMAT_8_8_8_8;
      }
      break;
   case 16:
      switch (desc->nr_channels) {
      case 1:
      case 3: /* 3 loads */
         return V_008F0C_BUF_DATA_FORMAT_16;
      case 2:
         return V_008F0C_BUF_DATA_FORMAT_16_16;
      case 4:
         return V_008F0C_BUF_DATA_FORMAT_16_16_16_16;
      }
      break;
   case 32:
      switch (desc->nr_channels) {
      case 1:
         return V_008F0C_BUF_DATA_FORMAT_32;
      case 2:
         return V_008F0C_BUF_DATA_FORMAT_32_32;
      case 3:
         return V_008F0C_BUF_DATA_FORMAT_32_32_32;
      case 4:
         return V_008F0C_BUF_DATA_FORMAT_32_32_32_32;
      }
      break;
   case 64:
      /* Legacy double formats. */
      switch (desc->nr_channels) {
      case 1: /* 1 load */
         return V_008F0C_BUF_DATA_FORMAT_32_32;
      case 2: /* 1 load */
         return V_008F0C_BUF_DATA_FORMAT_32_32_32_32;
      case 3: /* 3 loads */
         return V_008F0C_BUF_DATA_FORMAT_32_32;
      case 4: /* 2 loads */
         return V_008F0C_BUF_DATA_FORMAT_32_32_32_32;
      }
      break;
   }

   return V_008F0C_BUF_DATA_FORMAT_INVALID;
}

static uint32_t si_translate_buffer_numformat(struct pipe_screen *screen,
                                              const struct util_format_description *desc,
                                              int first_non_void)
{
   assert(((struct si_screen *)screen)->info.chip_class <= GFX9);

   if (desc->format == PIPE_FORMAT_R11G11B10_FLOAT)
      return V_008F0C_BUF_NUM_FORMAT_FLOAT;

   assert(first_non_void >= 0);

   switch (desc->channel[first_non_void].type) {
   case UTIL_FORMAT_TYPE_SIGNED:
   case UTIL_FORMAT_TYPE_FIXED:
      if (desc->channel[first_non_void].size >= 32 || desc->channel[first_non_void].pure_integer)
         return V_008F0C_BUF_NUM_FORMAT_SINT;
      else if (desc->channel[first_non_void].normalized)
         return V_008F0C_BUF_NUM_FORMAT_SNORM;
      else
         return V_008F0C_BUF_NUM_FORMAT_SSCALED;
      break;
   case UTIL_FORMAT_TYPE_UNSIGNED:
      if (desc->channel[first_non_void].size >= 32 || desc->channel[first_non_void].pure_integer)
         return V_008F0C_BUF_NUM_FORMAT_UINT;
      else if (desc->channel[first_non_void].normalized)
         return V_008F0C_BUF_NUM_FORMAT_UNORM;
      else
         return V_008F0C_BUF_NUM_FORMAT_USCALED;
      break;
   case UTIL_FORMAT_TYPE_FLOAT:
   default:
      return V_008F0C_BUF_NUM_FORMAT_FLOAT;
   }
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
   if (!desc)
      return 0;

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

   if (sscreen->info.chip_class >= GFX10) {
      const struct gfx10_format *fmt = &gfx10_format_table[format];
      if (!fmt->img_format || fmt->img_format >= 128)
         return 0;
      return usage;
   }

   first_non_void = util_format_get_first_non_void_channel(format);
   data_format = si_translate_buffer_dataformat(screen, desc, first_non_void);
   if (data_format == V_008F0C_BUF_DATA_FORMAT_INVALID)
      return 0;

   return usage;
}

static bool si_is_colorbuffer_format_supported(enum chip_class chip_class,
                                               enum pipe_format format)
{
   return si_translate_colorformat(chip_class, format) != V_028C70_COLOR_INVALID &&
          si_translate_colorswap(format, false) != ~0U;
}

static bool si_is_zs_format_supported(enum pipe_format format)
{
   return si_translate_dbformat(format) != V_028040_Z_INVALID;
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

   if ((target == PIPE_TEXTURE_3D || target == PIPE_TEXTURE_CUBE) &&
        !sscreen->info.has_3d_cube_border_color_mipmap)
      return false;

   if (util_format_get_num_planes(format) >= 2)
      return false;

   if (MAX2(1, sample_count) < MAX2(1, storage_sample_count))
      return false;

   if (sample_count > 1) {
      if (!screen->get_param(screen, PIPE_CAP_TEXTURE_MULTISAMPLE))
         return false;

      /* Only power-of-two sample counts are supported. */
      if (!util_is_power_of_two_or_zero(sample_count) ||
          !util_is_power_of_two_or_zero(storage_sample_count))
         return false;

      /* Chips with 1 RB don't increment occlusion queries at 16x MSAA sample rate,
       * so don't expose 16 samples there.
       */
      const unsigned max_eqaa_samples = util_bitcount(sscreen->info.enabled_rb_mask) <= 1 ? 8 : 16;
      const unsigned max_samples = 8;

      /* MSAA support without framebuffer attachments. */
      if (format == PIPE_FORMAT_NONE && sample_count <= max_eqaa_samples)
         return true;

      if (!sscreen->info.has_eqaa_surface_allocator || util_format_is_depth_or_stencil(format)) {
         /* Color without EQAA or depth/stencil. */
         if (sample_count > max_samples || sample_count != storage_sample_count)
            return false;
      } else {
         /* Color with EQAA. */
         if (sample_count > max_eqaa_samples || storage_sample_count > max_samples)
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
       si_is_colorbuffer_format_supported(sscreen->info.chip_class, format)) {
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
   unsigned color_info, color_attrib;
   unsigned format, swap, ntype, endian;
   const struct util_format_description *desc;
   int firstchan;
   unsigned blend_clamp = 0, blend_bypass = 0;

   desc = util_format_description(surf->base.format);
   for (firstchan = 0; firstchan < 4; firstchan++) {
      if (desc->channel[firstchan].type != UTIL_FORMAT_TYPE_VOID) {
         break;
      }
   }
   if (firstchan == 4 || desc->channel[firstchan].type == UTIL_FORMAT_TYPE_FLOAT) {
      ntype = V_028C70_NUMBER_FLOAT;
   } else {
      ntype = V_028C70_NUMBER_UNORM;
      if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
         ntype = V_028C70_NUMBER_SRGB;
      else if (desc->channel[firstchan].type == UTIL_FORMAT_TYPE_SIGNED) {
         if (desc->channel[firstchan].pure_integer) {
            ntype = V_028C70_NUMBER_SINT;
         } else {
            assert(desc->channel[firstchan].normalized);
            ntype = V_028C70_NUMBER_SNORM;
         }
      } else if (desc->channel[firstchan].type == UTIL_FORMAT_TYPE_UNSIGNED) {
         if (desc->channel[firstchan].pure_integer) {
            ntype = V_028C70_NUMBER_UINT;
         } else {
            assert(desc->channel[firstchan].normalized);
            ntype = V_028C70_NUMBER_UNORM;
         }
      }
   }

   format = si_translate_colorformat(sctx->chip_class, surf->base.format);
   if (format == V_028C70_COLOR_INVALID) {
      PRINT_ERR("Invalid CB format: %d, disabling CB.\n", surf->base.format);
   }
   assert(format != V_028C70_COLOR_INVALID);
   swap = si_translate_colorswap(surf->base.format, false);
   endian = si_colorformat_endian_swap(format);

   /* blend clamp should be set for all NORM/SRGB types */
   if (ntype == V_028C70_NUMBER_UNORM || ntype == V_028C70_NUMBER_SNORM ||
       ntype == V_028C70_NUMBER_SRGB)
      blend_clamp = 1;

   /* set blend bypass according to docs if SINT/UINT or
      8/24 COLOR variants */
   if (ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT ||
       format == V_028C70_COLOR_8_24 || format == V_028C70_COLOR_24_8 ||
       format == V_028C70_COLOR_X24_8_32_FLOAT) {
      blend_clamp = 0;
      blend_bypass = 1;
   }

   if (ntype == V_028C70_NUMBER_UINT || ntype == V_028C70_NUMBER_SINT) {
      if (format == V_028C70_COLOR_8 || format == V_028C70_COLOR_8_8 ||
          format == V_028C70_COLOR_8_8_8_8)
         surf->color_is_int8 = true;
      else if (format == V_028C70_COLOR_10_10_10_2 || format == V_028C70_COLOR_2_10_10_10)
         surf->color_is_int10 = true;
   }

   color_info =
      S_028C70_FORMAT(format) | S_028C70_COMP_SWAP(swap) | S_028C70_BLEND_CLAMP(blend_clamp) |
      S_028C70_BLEND_BYPASS(blend_bypass) | S_028C70_SIMPLE_FLOAT(1) |
      S_028C70_ROUND_MODE(ntype != V_028C70_NUMBER_UNORM && ntype != V_028C70_NUMBER_SNORM &&
                          ntype != V_028C70_NUMBER_SRGB && format != V_028C70_COLOR_8_24 &&
                          format != V_028C70_COLOR_24_8) |
      S_028C70_NUMBER_TYPE(ntype) | S_028C70_ENDIAN(endian);

   /* Intensity is implemented as Red, so treat it that way. */
   color_attrib = S_028C74_FORCE_DST_ALPHA_1(desc->swizzle[3] == PIPE_SWIZZLE_1 ||
                                             util_format_is_intensity(surf->base.format));

   if (tex->buffer.b.b.nr_samples > 1) {
      unsigned log_samples = util_logbase2(tex->buffer.b.b.nr_samples);
      unsigned log_fragments = util_logbase2(tex->buffer.b.b.nr_storage_samples);

      color_attrib |= S_028C74_NUM_SAMPLES(log_samples) | S_028C74_NUM_FRAGMENTS(log_fragments);

      if (tex->surface.fmask_offset) {
         color_info |= S_028C70_COMPRESSION(1);
         unsigned fmask_bankh = util_logbase2(tex->surface.u.legacy.color.fmask.bankh);

         if (sctx->chip_class == GFX6) {
            /* due to a hw bug, FMASK_BANK_HEIGHT must be set on GFX6 too */
            color_attrib |= S_028C74_FMASK_BANK_HEIGHT(fmask_bankh);
         }
      }
   }

   /* amdvlk: [min-compressed-block-size] should be set to 32 for dGPU and
    * 64 for APU because all of our APUs to date use DIMMs which have
    * a request granularity size of 64B while all other chips have a
    * 32B request size */
   unsigned min_compressed_block_size = V_028C78_MIN_BLOCK_SIZE_32B;
   if (!sctx->screen->info.has_dedicated_vram)
      min_compressed_block_size = V_028C78_MIN_BLOCK_SIZE_64B;

   if (sctx->chip_class >= GFX10) {
      surf->cb_dcc_control = S_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(V_028C78_MAX_BLOCK_SIZE_256B) |
                             S_028C78_MAX_COMPRESSED_BLOCK_SIZE(tex->surface.u.gfx9.color.dcc.max_compressed_block_size) |
                             S_028C78_MIN_COMPRESSED_BLOCK_SIZE(min_compressed_block_size) |
                             S_028C78_INDEPENDENT_64B_BLOCKS(tex->surface.u.gfx9.color.dcc.independent_64B_blocks) |
                             S_028C78_INDEPENDENT_128B_BLOCKS(tex->surface.u.gfx9.color.dcc.independent_128B_blocks);
   } else if (sctx->chip_class >= GFX8) {
      unsigned max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_256B;

      if (tex->buffer.b.b.nr_storage_samples > 1) {
         if (tex->surface.bpe == 1)
            max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_64B;
         else if (tex->surface.bpe == 2)
            max_uncompressed_block_size = V_028C78_MAX_BLOCK_SIZE_128B;
      }

      surf->cb_dcc_control = S_028C78_MAX_UNCOMPRESSED_BLOCK_SIZE(max_uncompressed_block_size) |
                             S_028C78_MIN_COMPRESSED_BLOCK_SIZE(min_compressed_block_size) |
                             S_028C78_INDEPENDENT_64B_BLOCKS(1);
   }

   /* This must be set for fast clear to work without FMASK. */
   if (!tex->surface.fmask_size && sctx->chip_class == GFX6) {
      unsigned bankh = util_logbase2(tex->surface.u.legacy.bankh);
      color_attrib |= S_028C74_FMASK_BANK_HEIGHT(bankh);
   }

   /* GFX10 field has the same base shift as the GFX6 field */
   unsigned color_view = S_028C6C_SLICE_START(surf->base.u.tex.first_layer) |
                         S_028C6C_SLICE_MAX_GFX10(surf->base.u.tex.last_layer);
   unsigned mip0_depth = util_max_layer(&tex->buffer.b.b, 0);

   if (sctx->chip_class >= GFX10) {
      color_view |= S_028C6C_MIP_LEVEL_GFX10(surf->base.u.tex.level);

      surf->cb_color_attrib3 = S_028EE0_MIP0_DEPTH(mip0_depth) |
                               S_028EE0_RESOURCE_TYPE(tex->surface.u.gfx9.resource_type) |
                               S_028EE0_RESOURCE_LEVEL(1);
   } else if (sctx->chip_class == GFX9) {
      color_view |= S_028C6C_MIP_LEVEL_GFX9(surf->base.u.tex.level);
      color_attrib |= S_028C74_MIP0_DEPTH(mip0_depth) |
                      S_028C74_RESOURCE_TYPE(tex->surface.u.gfx9.resource_type);
   }

   if (sctx->chip_class >= GFX9) {
      surf->cb_color_attrib2 = S_028C68_MIP0_WIDTH(surf->width0 - 1) |
                               S_028C68_MIP0_HEIGHT(surf->height0 - 1) |
                               S_028C68_MAX_MIP(tex->buffer.b.b.last_level);
   }

   surf->cb_color_view = color_view;
   surf->cb_color_info = color_info;
   surf->cb_color_attrib = color_attrib;

   /* Determine pixel shader export format */
   si_choose_spi_color_formats(surf, format, swap, ntype, tex->is_depth);

   surf->color_initialized = true;
}

static void si_init_depth_surface(struct si_context *sctx, struct si_surface *surf)
{
   struct si_texture *tex = (struct si_texture *)surf->base.texture;
   unsigned level = surf->base.u.tex.level;
   unsigned format, stencil_format;
   uint32_t z_info, s_info;

   format = si_translate_dbformat(tex->db_render_format);
   stencil_format = tex->surface.has_stencil ? V_028044_STENCIL_8 : V_028044_STENCIL_INVALID;

   assert(format != V_028040_Z_INVALID);
   if (format == V_028040_Z_INVALID)
      PRINT_ERR("Invalid DB format: %d, disabling DB.\n", tex->buffer.b.b.format);

   surf->db_depth_view = S_028008_SLICE_START(surf->base.u.tex.first_layer) |
                         S_028008_SLICE_MAX(surf->base.u.tex.last_layer);
   surf->db_htile_data_base = 0;
   surf->db_htile_surface = 0;

   if (sctx->chip_class >= GFX10) {
      surf->db_depth_view |= S_028008_SLICE_START_HI(surf->base.u.tex.first_layer >> 11) |
                             S_028008_SLICE_MAX_HI(surf->base.u.tex.last_layer >> 11);
   }

   if (sctx->chip_class >= GFX9) {
      assert(tex->surface.u.gfx9.surf_offset == 0);
      surf->db_depth_base = tex->buffer.gpu_address >> 8;
      surf->db_stencil_base = (tex->buffer.gpu_address + tex->surface.u.gfx9.zs.stencil_offset) >> 8;
      z_info = S_028038_FORMAT(format) |
               S_028038_NUM_SAMPLES(util_logbase2(tex->buffer.b.b.nr_samples)) |
               S_028038_SW_MODE(tex->surface.u.gfx9.swizzle_mode) |
               S_028038_MAXMIP(tex->buffer.b.b.last_level);
      s_info = S_02803C_FORMAT(stencil_format) |
               S_02803C_SW_MODE(tex->surface.u.gfx9.zs.stencil_swizzle_mode);

      if (sctx->chip_class == GFX9) {
         surf->db_z_info2 = S_028068_EPITCH(tex->surface.u.gfx9.epitch);
         surf->db_stencil_info2 = S_02806C_EPITCH(tex->surface.u.gfx9.zs.stencil_epitch);
      }
      surf->db_depth_view |= S_028008_MIPID(level);
      surf->db_depth_size =
         S_02801C_X_MAX(tex->buffer.b.b.width0 - 1) | S_02801C_Y_MAX(tex->buffer.b.b.height0 - 1);

      if (si_htile_enabled(tex, level, PIPE_MASK_ZS)) {
         z_info |= S_028038_TILE_SURFACE_ENABLE(1) | S_028038_ALLOW_EXPCLEAR(1);
         s_info |= S_02803C_TILE_STENCIL_DISABLE(tex->htile_stencil_disabled);

         if (tex->surface.has_stencil && !tex->htile_stencil_disabled) {
            /* Stencil buffer workaround ported from the GFX6-GFX8 code.
             * See that for explanation.
             */
            s_info |= S_02803C_ALLOW_EXPCLEAR(tex->buffer.b.b.nr_samples <= 1);
         }

         surf->db_htile_data_base = (tex->buffer.gpu_address + tex->surface.meta_offset) >> 8;
         surf->db_htile_surface =
            S_028ABC_FULL_CACHE(1) | S_028ABC_PIPE_ALIGNED(1);
         if (sctx->chip_class == GFX9) {
            surf->db_htile_surface |= S_028ABC_RB_ALIGNED(1);
         }
      }
   } else {
      /* GFX6-GFX8 */
      struct legacy_surf_level *levelinfo = &tex->surface.u.legacy.level[level];

      assert(levelinfo->nblk_x % 8 == 0 && levelinfo->nblk_y % 8 == 0);

      surf->db_depth_base =
         (tex->buffer.gpu_address >> 8) + tex->surface.u.legacy.level[level].offset_256B;
      surf->db_stencil_base =
         (tex->buffer.gpu_address >> 8) + tex->surface.u.legacy.zs.stencil_level[level].offset_256B;

      z_info =
         S_028040_FORMAT(format) | S_028040_NUM_SAMPLES(util_logbase2(tex->buffer.b.b.nr_samples));
      s_info = S_028044_FORMAT(stencil_format);
      surf->db_depth_info = 0;

      if (sctx->chip_class >= GFX7) {
         struct radeon_info *info = &sctx->screen->info;
         unsigned index = tex->surface.u.legacy.tiling_index[level];
         unsigned stencil_index = tex->surface.u.legacy.zs.stencil_tiling_index[level];
         unsigned macro_index = tex->surface.u.legacy.macro_tile_index;
         unsigned tile_mode = info->si_tile_mode_array[index];
         unsigned stencil_tile_mode = info->si_tile_mode_array[stencil_index];
         unsigned macro_mode = info->cik_macrotile_mode_array[macro_index];

         surf->db_depth_info |= S_02803C_ARRAY_MODE(G_009910_ARRAY_MODE(tile_mode)) |
                                S_02803C_PIPE_CONFIG(G_009910_PIPE_CONFIG(tile_mode)) |
                                S_02803C_BANK_WIDTH(G_009990_BANK_WIDTH(macro_mode)) |
                                S_02803C_BANK_HEIGHT(G_009990_BANK_HEIGHT(macro_mode)) |
                                S_02803C_MACRO_TILE_ASPECT(G_009990_MACRO_TILE_ASPECT(macro_mode)) |
                                S_02803C_NUM_BANKS(G_009990_NUM_BANKS(macro_mode));
         z_info |= S_028040_TILE_SPLIT(G_009910_TILE_SPLIT(tile_mode));
         s_info |= S_028044_TILE_SPLIT(G_009910_TILE_SPLIT(stencil_tile_mode));
      } else {
         unsigned tile_mode_index = si_tile_mode_index(tex, level, false);
         z_info |= S_028040_TILE_MODE_INDEX(tile_mode_index);
         tile_mode_index = si_tile_mode_index(tex, level, true);
         s_info |= S_028044_TILE_MODE_INDEX(tile_mode_index);
      }

      surf->db_depth_size = S_028058_PITCH_TILE_MAX((levelinfo->nblk_x / 8) - 1) |
                            S_028058_HEIGHT_TILE_MAX((levelinfo->nblk_y / 8) - 1);
      surf->db_depth_slice =
         S_02805C_SLICE_TILE_MAX((levelinfo->nblk_x * levelinfo->nblk_y) / 64 - 1);

      if (si_htile_enabled(tex, level, PIPE_MASK_ZS)) {
         z_info |= S_028040_TILE_SURFACE_ENABLE(1) | S_028040_ALLOW_EXPCLEAR(1);
         s_info |= S_028044_TILE_STENCIL_DISABLE(tex->htile_stencil_disabled);

         if (tex->surface.has_stencil) {
            /* Workaround: For a not yet understood reason, the
             * combination of MSAA, fast stencil clear and stencil
             * decompress messes with subsequent stencil buffer
             * uses. Problem was reproduced on Verde, Bonaire,
             * Tonga, and Carrizo.
             *
             * Disabling EXPCLEAR works around the problem.
             *
             * Check piglit's arb_texture_multisample-stencil-clear
             * test if you want to try changing this.
             */
            if (tex->buffer.b.b.nr_samples <= 1)
               s_info |= S_028044_ALLOW_EXPCLEAR(1);
         }

         surf->db_htile_data_base = (tex->buffer.gpu_address + tex->surface.meta_offset) >> 8;
         surf->db_htile_surface = S_028ABC_FULL_CACHE(1);
      }
   }

   surf->db_z_info = z_info;
   surf->db_stencil_info = s_info;

   surf->depth_initialized = true;
}

void si_update_fb_dirtiness_after_rendering(struct si_context *sctx)
{
   if (sctx->decompression_enabled)
      return;

   if (sctx->framebuffer.state.zsbuf) {
      struct pipe_surface *surf = sctx->framebuffer.state.zsbuf;
      struct si_texture *tex = (struct si_texture *)surf->texture;

      tex->dirty_level_mask |= 1 << surf->u.tex.level;

      if (tex->surface.has_stencil)
         tex->stencil_dirty_level_mask |= 1 << surf->u.tex.level;
   }

   unsigned compressed_cb_mask = sctx->framebuffer.compressed_cb_mask;
   while (compressed_cb_mask) {
      unsigned i = u_bit_scan(&compressed_cb_mask);
      struct pipe_surface *surf = sctx->framebuffer.state.cbufs[i];
      struct si_texture *tex = (struct si_texture *)surf->texture;

      if (tex->surface.fmask_offset) {
         tex->dirty_level_mask |= 1 << surf->u.tex.level;
         tex->fmask_is_identity = false;
      }
   }
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
   bool unbound = false;
   int i;

   /* Reject zero-sized framebuffers due to a hw bug on GFX6 that occurs
    * when PA_SU_HARDWARE_SCREEN_OFFSET != 0 and any_scissor.BR_X/Y <= 0.
    * We could implement the full workaround here, but it's a useless case.
    */
   if ((!state->width || !state->height) && (state->nr_cbufs || state->zsbuf)) {
      unreachable("the framebuffer shouldn't have zero area");
      return;
   }

   si_update_fb_dirtiness_after_rendering(sctx);

   /* Disable DCC if the formats are incompatible. */
   for (i = 0; i < state->nr_cbufs; i++) {
      if (!state->cbufs[i])
         continue;

      surf = (struct si_surface *)state->cbufs[i];
      tex = (struct si_texture *)surf->base.texture;

      if (!surf->dcc_incompatible)
         continue;

      /* Since the DCC decompression calls back into set_framebuffer-
       * _state, we need to unbind the framebuffer, so that
       * vi_separate_dcc_stop_query isn't called twice with the same
       * color buffer.
       */
      if (!unbound) {
         util_copy_framebuffer_state(&sctx->framebuffer.state, NULL);
         unbound = true;
      }

      if (vi_dcc_enabled(tex, surf->base.u.tex.level))
         if (!si_texture_disable_dcc(sctx, tex))
            si_decompress_dcc(sctx, tex);

      surf->dcc_incompatible = false;
   }

   /* Only flush TC when changing the framebuffer state, because
    * the only client not using TC that can change textures is
    * the framebuffer.
    *
    * Wait for compute shaders because of possible transitions:
    * - FB write -> shader read
    * - shader write -> FB read
    *
    * DB caches are flushed on demand (using si_decompress_textures).
    *
    * When MSAA is enabled, CB and TC caches are flushed on demand
    * (after FMASK decompression). Shader write -> FB read transitions
    * cannot happen for MSAA textures, because MSAA shader images are
    * not supported.
    *
    * Only flush and wait for CB if there is actually a bound color buffer.
    */
   if (sctx->framebuffer.uncompressed_cb_mask) {
      si_make_CB_shader_coherent(sctx, sctx->framebuffer.nr_samples,
                                 sctx->framebuffer.CB_has_shader_readable_metadata,
                                 sctx->framebuffer.all_DCC_pipe_aligned);
   }

   sctx->flags |= SI_CONTEXT_CS_PARTIAL_FLUSH;

   /* u_blitter doesn't invoke depth decompression when it does multiple
    * blits in a row, but the only case when it matters for DB is when
    * doing generate_mipmap. So here we flush DB manually between
    * individual generate_mipmap blits.
    * Note that lower mipmap levels aren't compressed.
    */
   if (sctx->generate_mipmap_for_depth) {
      si_make_DB_shader_coherent(sctx, 1, false, sctx->framebuffer.DB_has_shader_readable_metadata);
   } else if (sctx->chip_class == GFX9) {
      /* It appears that DB metadata "leaks" in a sequence of:
       *  - depth clear
       *  - DCC decompress for shader image writes (with DB disabled)
       *  - render with DEPTH_BEFORE_SHADER=1
       * Flushing DB metadata works around the problem.
       */
      sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_DB_META;
   }

   /* Take the maximum of the old and new count. If the new count is lower,
    * dirtying is needed to disable the unbound colorbuffers.
    */
   sctx->framebuffer.dirty_cbufs |=
      (1 << MAX2(sctx->framebuffer.state.nr_cbufs, state->nr_cbufs)) - 1;
   sctx->framebuffer.dirty_zsbuf |= sctx->framebuffer.state.zsbuf != state->zsbuf;

   si_dec_framebuffer_counters(&sctx->framebuffer.state);
   util_copy_framebuffer_state(&sctx->framebuffer.state, state);

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

         if (sctx->chip_class >= GFX9 && !tex->surface.u.gfx9.color.dcc.pipe_aligned)
            sctx->framebuffer.all_DCC_pipe_aligned = false;

         if (tex->buffer.b.b.nr_storage_samples >= 2)
            sctx->framebuffer.has_dcc_msaa = true;
      }

      si_context_add_resource_size(sctx, surf->base.texture);

      p_atomic_inc(&tex->framebuffers_bound);

      /* Update the minimum but don't keep 0. */
      if (!sctx->framebuffer.min_bytes_per_pixel ||
          tex->surface.bpe < sctx->framebuffer.min_bytes_per_pixel)
         sctx->framebuffer.min_bytes_per_pixel = tex->surface.bpe;
   }

   /* For optimal DCC performance. */
   if (sctx->chip_class >= GFX10)
      sctx->framebuffer.dcc_overwrite_combiner_watermark = 6;
   else
      sctx->framebuffer.dcc_overwrite_combiner_watermark = 4;

   struct si_texture *zstex = NULL;

   if (state->zsbuf) {
      surf = (struct si_surface *)state->zsbuf;
      zstex = (struct si_texture *)surf->base.texture;

      if (!surf->depth_initialized) {
         si_init_depth_surface(sctx, surf);
      }

      if (vi_tc_compat_htile_enabled(zstex, surf->base.u.tex.level, PIPE_MASK_ZS))
         sctx->framebuffer.DB_has_shader_readable_metadata = true;

      si_context_add_resource_size(sctx, surf->base.texture);

      /* Update the minimum but don't keep 0. */
      if (!sctx->framebuffer.min_bytes_per_pixel ||
          zstex->surface.bpe < sctx->framebuffer.min_bytes_per_pixel)
         sctx->framebuffer.min_bytes_per_pixel = zstex->surface.bpe;
   }

   si_update_ps_colorbuf0_slot(sctx);
   si_update_poly_offset_state(sctx);
   si_mark_atom_dirty(sctx, &sctx->atoms.s.cb_render_state);
   si_mark_atom_dirty(sctx, &sctx->atoms.s.framebuffer);

   /* NGG cull state uses the sample count. */
   if (sctx->screen->use_ngg_culling)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.ngg_cull_state);

   if (sctx->screen->dpbb_allowed)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.dpbb_state);

   if (sctx->framebuffer.any_dst_linear != old_any_dst_linear)
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);

   if (sctx->screen->has_out_of_order_rast &&
       (sctx->framebuffer.colorbuf_enabled_4bit != old_colorbuf_enabled_4bit ||
        !!sctx->framebuffer.state.zsbuf != old_has_zsbuf ||
        (zstex && zstex->surface.has_stencil != old_has_stencil)))
      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);

   if (sctx->framebuffer.nr_samples != old_nr_samples) {
      struct pipe_constant_buffer constbuf = {0};

      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_config);
      si_mark_atom_dirty(sctx, &sctx->atoms.s.db_render_state);

      if (!sctx->sample_pos_buffer) {
         sctx->sample_pos_buffer = pipe_buffer_create_with_data(&sctx->b, 0, PIPE_USAGE_DEFAULT,
                                                      sizeof(sctx->sample_positions),
                                                      &sctx->sample_positions);
      }
      constbuf.buffer = sctx->sample_pos_buffer;

      /* Set sample locations as fragment shader constants. */
      switch (sctx->framebuffer.nr_samples) {
      case 1:
         constbuf.buffer_offset = 0;
         break;
      case 2:
         constbuf.buffer_offset =
            (ubyte *)sctx->sample_positions.x2 - (ubyte *)sctx->sample_positions.x1;
         break;
      case 4:
         constbuf.buffer_offset =
            (ubyte *)sctx->sample_positions.x4 - (ubyte *)sctx->sample_positions.x1;
         break;
      case 8:
         constbuf.buffer_offset =
            (ubyte *)sctx->sample_positions.x8 - (ubyte *)sctx->sample_positions.x1;
         break;
      case 16:
         constbuf.buffer_offset =
            (ubyte *)sctx->sample_positions.x16 - (ubyte *)sctx->sample_positions.x1;
         break;
      default:
         PRINT_ERR("Requested an invalid number of samples %i.\n", sctx->framebuffer.nr_samples);
         assert(0);
      }
      constbuf.buffer_size = sctx->framebuffer.nr_samples * 2 * 4;
      si_set_internal_const_buffer(sctx, SI_PS_CONST_SAMPLE_POSITIONS, &constbuf);

      si_mark_atom_dirty(sctx, &sctx->atoms.s.msaa_sample_locs);
   }

   si_ps_key_update_framebuffer(sctx);
   si_ps_key_update_framebuffer_blend(sctx);
   si_ps_key_update_framebuffer_rasterizer_sample_shading(sctx);
   si_update_ps_inputs_read_or_disabled(sctx);
   sctx->do_update_shaders = true;

   if (!sctx->decompression_enabled) {
      /* Prevent textures decompression when the framebuffer state
       * changes come from the decompression passes themselves.
       */
      sctx->need_check_render_feedback = true;
   }
}

static void si_emit_framebuffer_state(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct pipe_framebuffer_state *state = &sctx->framebuffer.state;
   unsigned i, nr_cbufs = state->nr_cbufs;
   struct si_texture *tex = NULL;
   struct si_surface *cb = NULL;
   unsigned cb_color_info = 0;

   radeon_begin(cs);

   /* Colorbuffers. */
   for (i = 0; i < nr_cbufs; i++) {
      uint64_t cb_color_base, cb_color_fmask, cb_color_cmask, cb_dcc_base;
      unsigned cb_color_attrib;

      if (!(sctx->framebuffer.dirty_cbufs & (1 << i)))
         continue;

      cb = (struct si_surface *)state->cbufs[i];
      if (!cb) {
         radeon_set_context_reg(cs, R_028C70_CB_COLOR0_INFO + i * 0x3C,
                                S_028C70_FORMAT(V_028C70_COLOR_INVALID));
         continue;
      }

      tex = (struct si_texture *)cb->base.texture;
      radeon_add_to_buffer_list(
         sctx, &sctx->gfx_cs, &tex->buffer, RADEON_USAGE_READWRITE | RADEON_USAGE_NEEDS_IMPLICIT_SYNC,
         tex->buffer.b.b.nr_samples > 1 ? RADEON_PRIO_COLOR_BUFFER_MSAA : RADEON_PRIO_COLOR_BUFFER);

      if (tex->cmask_buffer && tex->cmask_buffer != &tex->buffer) {
         radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, tex->cmask_buffer,
                                   RADEON_USAGE_READWRITE | RADEON_USAGE_NEEDS_IMPLICIT_SYNC,
                                   RADEON_PRIO_SEPARATE_META);
      }

      /* Compute mutable surface parameters. */
      cb_color_base = tex->buffer.gpu_address >> 8;
      cb_color_fmask = 0;
      cb_color_cmask = tex->cmask_base_address_reg;
      cb_dcc_base = 0;
      cb_color_info = cb->cb_color_info | tex->cb_color_info;
      cb_color_attrib = cb->cb_color_attrib;

      if (tex->swap_rgb_to_bgr) {
         /* Swap R and B channels. */
         static unsigned rgb_to_bgr[4] = {
            [V_028C70_SWAP_STD] = V_028C70_SWAP_ALT,
            [V_028C70_SWAP_ALT] = V_028C70_SWAP_STD,
            [V_028C70_SWAP_STD_REV] = V_028C70_SWAP_ALT_REV,
            [V_028C70_SWAP_ALT_REV] = V_028C70_SWAP_STD_REV,
         };
         unsigned swap = rgb_to_bgr[G_028C70_COMP_SWAP(cb_color_info)];

         cb_color_info &= C_028C70_COMP_SWAP;
         cb_color_info |= S_028C70_COMP_SWAP(swap);
      }

      if (cb->base.u.tex.level > 0)
         cb_color_info &= C_028C70_FAST_CLEAR;

      if (tex->surface.fmask_offset) {
         cb_color_fmask = (tex->buffer.gpu_address + tex->surface.fmask_offset) >> 8;
         cb_color_fmask |= tex->surface.fmask_tile_swizzle;
      }

      /* Set up DCC. */
      if (vi_dcc_enabled(tex, cb->base.u.tex.level)) {
         bool is_msaa_resolve_dst = state->cbufs[0] && state->cbufs[0]->texture->nr_samples > 1 &&
                                    state->cbufs[1] == &cb->base &&
                                    state->cbufs[1]->texture->nr_samples <= 1;

         if (!is_msaa_resolve_dst)
            cb_color_info |= S_028C70_DCC_ENABLE(1);

         cb_dcc_base = (tex->buffer.gpu_address + tex->surface.meta_offset) >> 8;

         unsigned dcc_tile_swizzle = tex->surface.tile_swizzle;
         dcc_tile_swizzle &= ((1 << tex->surface.meta_alignment_log2) - 1) >> 8;
         cb_dcc_base |= dcc_tile_swizzle;
      }

      if (sctx->chip_class >= GFX10) {
         unsigned cb_color_attrib3;

         /* Set mutable surface parameters. */
         cb_color_base += tex->surface.u.gfx9.surf_offset >> 8;
         cb_color_base |= tex->surface.tile_swizzle;
         if (!tex->surface.fmask_offset)
            cb_color_fmask = cb_color_base;
         if (cb->base.u.tex.level > 0)
            cb_color_cmask = cb_color_base;

         cb_color_attrib3 = cb->cb_color_attrib3 |
                            S_028EE0_COLOR_SW_MODE(tex->surface.u.gfx9.swizzle_mode) |
                            S_028EE0_FMASK_SW_MODE(tex->surface.u.gfx9.color.fmask_swizzle_mode) |
                            S_028EE0_CMASK_PIPE_ALIGNED(1) |
                            S_028EE0_DCC_PIPE_ALIGNED(tex->surface.u.gfx9.color.dcc.pipe_aligned);

         radeon_set_context_reg_seq(cs, R_028C60_CB_COLOR0_BASE + i * 0x3C, 14);
         radeon_emit(cs, cb_color_base);             /* CB_COLOR0_BASE */
         radeon_emit(cs, 0);                         /* hole */
         radeon_emit(cs, 0);                         /* hole */
         radeon_emit(cs, cb->cb_color_view);         /* CB_COLOR0_VIEW */
         radeon_emit(cs, cb_color_info);             /* CB_COLOR0_INFO */
         radeon_emit(cs, cb_color_attrib);           /* CB_COLOR0_ATTRIB */
         radeon_emit(cs, cb->cb_dcc_control);        /* CB_COLOR0_DCC_CONTROL */
         radeon_emit(cs, cb_color_cmask);            /* CB_COLOR0_CMASK */
         radeon_emit(cs, 0);                         /* hole */
         radeon_emit(cs, cb_color_fmask);            /* CB_COLOR0_FMASK */
         radeon_emit(cs, 0);                         /* hole */
         radeon_emit(cs, tex->color_clear_value[0]); /* CB_COLOR0_CLEAR_WORD0 */
         radeon_emit(cs, tex->color_clear_value[1]); /* CB_COLOR0_CLEAR_WORD1 */
         radeon_emit(cs, cb_dcc_base);               /* CB_COLOR0_DCC_BASE */

         radeon_set_context_reg(cs, R_028E40_CB_COLOR0_BASE_EXT + i * 4, cb_color_base >> 32);
         radeon_set_context_reg(cs, R_028E60_CB_COLOR0_CMASK_BASE_EXT + i * 4,
                                cb_color_cmask >> 32);
         radeon_set_context_reg(cs, R_028E80_CB_COLOR0_FMASK_BASE_EXT + i * 4,
                                cb_color_fmask >> 32);
         radeon_set_context_reg(cs, R_028EA0_CB_COLOR0_DCC_BASE_EXT + i * 4, cb_dcc_base >> 32);
         radeon_set_context_reg(cs, R_028EC0_CB_COLOR0_ATTRIB2 + i * 4, cb->cb_color_attrib2);
         radeon_set_context_reg(cs, R_028EE0_CB_COLOR0_ATTRIB3 + i * 4, cb_color_attrib3);
      } else if (sctx->chip_class == GFX9) {
         struct gfx9_surf_meta_flags meta = {
            .rb_aligned = 1,
            .pipe_aligned = 1,
         };

         if (!tex->is_depth && tex->surface.meta_offset)
            meta = tex->surface.u.gfx9.color.dcc;

         /* Set mutable surface parameters. */
         cb_color_base += tex->surface.u.gfx9.surf_offset >> 8;
         cb_color_base |= tex->surface.tile_swizzle;
         if (!tex->surface.fmask_offset)
            cb_color_fmask = cb_color_base;
         if (cb->base.u.tex.level > 0)
            cb_color_cmask = cb_color_base;
         cb_color_attrib |= S_028C74_COLOR_SW_MODE(tex->surface.u.gfx9.swizzle_mode) |
                            S_028C74_FMASK_SW_MODE(tex->surface.u.gfx9.color.fmask_swizzle_mode) |
                            S_028C74_RB_ALIGNED(meta.rb_aligned) |
                            S_028C74_PIPE_ALIGNED(meta.pipe_aligned);

         radeon_set_context_reg_seq(cs, R_028C60_CB_COLOR0_BASE + i * 0x3C, 15);
         radeon_emit(cs, cb_color_base);                            /* CB_COLOR0_BASE */
         radeon_emit(cs, S_028C64_BASE_256B(cb_color_base >> 32));  /* CB_COLOR0_BASE_EXT */
         radeon_emit(cs, cb->cb_color_attrib2);                     /* CB_COLOR0_ATTRIB2 */
         radeon_emit(cs, cb->cb_color_view);                        /* CB_COLOR0_VIEW */
         radeon_emit(cs, cb_color_info);                            /* CB_COLOR0_INFO */
         radeon_emit(cs, cb_color_attrib);                          /* CB_COLOR0_ATTRIB */
         radeon_emit(cs, cb->cb_dcc_control);                       /* CB_COLOR0_DCC_CONTROL */
         radeon_emit(cs, cb_color_cmask);                           /* CB_COLOR0_CMASK */
         radeon_emit(cs, S_028C80_BASE_256B(cb_color_cmask >> 32)); /* CB_COLOR0_CMASK_BASE_EXT */
         radeon_emit(cs, cb_color_fmask);                           /* CB_COLOR0_FMASK */
         radeon_emit(cs, S_028C88_BASE_256B(cb_color_fmask >> 32)); /* CB_COLOR0_FMASK_BASE_EXT */
         radeon_emit(cs, tex->color_clear_value[0]);                /* CB_COLOR0_CLEAR_WORD0 */
         radeon_emit(cs, tex->color_clear_value[1]);                /* CB_COLOR0_CLEAR_WORD1 */
         radeon_emit(cs, cb_dcc_base);                              /* CB_COLOR0_DCC_BASE */
         radeon_emit(cs, S_028C98_BASE_256B(cb_dcc_base >> 32));    /* CB_COLOR0_DCC_BASE_EXT */

         radeon_set_context_reg(cs, R_0287A0_CB_MRT0_EPITCH + i * 4,
                                S_0287A0_EPITCH(tex->surface.u.gfx9.epitch));
      } else {
         /* Compute mutable surface parameters (GFX6-GFX8). */
         const struct legacy_surf_level *level_info =
            &tex->surface.u.legacy.level[cb->base.u.tex.level];
         unsigned pitch_tile_max, slice_tile_max, tile_mode_index;
         unsigned cb_color_pitch, cb_color_slice, cb_color_fmask_slice;

         cb_color_base += level_info->offset_256B;
         /* Only macrotiled modes can set tile swizzle. */
         if (level_info->mode == RADEON_SURF_MODE_2D)
            cb_color_base |= tex->surface.tile_swizzle;

         if (!tex->surface.fmask_offset)
            cb_color_fmask = cb_color_base;
         if (cb->base.u.tex.level > 0)
            cb_color_cmask = cb_color_base;
         if (cb_dcc_base)
            cb_dcc_base += tex->surface.u.legacy.color.dcc_level[cb->base.u.tex.level].dcc_offset >> 8;

         pitch_tile_max = level_info->nblk_x / 8 - 1;
         slice_tile_max = level_info->nblk_x * level_info->nblk_y / 64 - 1;
         tile_mode_index = si_tile_mode_index(tex, cb->base.u.tex.level, false);

         cb_color_attrib |= S_028C74_TILE_MODE_INDEX(tile_mode_index);
         cb_color_pitch = S_028C64_TILE_MAX(pitch_tile_max);
         cb_color_slice = S_028C68_TILE_MAX(slice_tile_max);

         if (tex->surface.fmask_offset) {
            if (sctx->chip_class >= GFX7)
               cb_color_pitch |=
                  S_028C64_FMASK_TILE_MAX(tex->surface.u.legacy.color.fmask.pitch_in_pixels / 8 - 1);
            cb_color_attrib |=
               S_028C74_FMASK_TILE_MODE_INDEX(tex->surface.u.legacy.color.fmask.tiling_index);
            cb_color_fmask_slice = S_028C88_TILE_MAX(tex->surface.u.legacy.color.fmask.slice_tile_max);
         } else {
            /* This must be set for fast clear to work without FMASK. */
            if (sctx->chip_class >= GFX7)
               cb_color_pitch |= S_028C64_FMASK_TILE_MAX(pitch_tile_max);
            cb_color_attrib |= S_028C74_FMASK_TILE_MODE_INDEX(tile_mode_index);
            cb_color_fmask_slice = S_028C88_TILE_MAX(slice_tile_max);
         }

         radeon_set_context_reg_seq(cs, R_028C60_CB_COLOR0_BASE + i * 0x3C,
                                    sctx->chip_class >= GFX8 ? 14 : 13);
         radeon_emit(cs, cb_color_base);                              /* CB_COLOR0_BASE */
         radeon_emit(cs, cb_color_pitch);                             /* CB_COLOR0_PITCH */
         radeon_emit(cs, cb_color_slice);                             /* CB_COLOR0_SLICE */
         radeon_emit(cs, cb->cb_color_view);                          /* CB_COLOR0_VIEW */
         radeon_emit(cs, cb_color_info);                              /* CB_COLOR0_INFO */
         radeon_emit(cs, cb_color_attrib);                            /* CB_COLOR0_ATTRIB */
         radeon_emit(cs, cb->cb_dcc_control);                         /* CB_COLOR0_DCC_CONTROL */
         radeon_emit(cs, cb_color_cmask);                             /* CB_COLOR0_CMASK */
         radeon_emit(cs, tex->surface.u.legacy.color.cmask_slice_tile_max); /* CB_COLOR0_CMASK_SLICE */
         radeon_emit(cs, cb_color_fmask);                             /* CB_COLOR0_FMASK */
         radeon_emit(cs, cb_color_fmask_slice);                       /* CB_COLOR0_FMASK_SLICE */
         radeon_emit(cs, tex->color_clear_value[0]);                  /* CB_COLOR0_CLEAR_WORD0 */
         radeon_emit(cs, tex->color_clear_value[1]);                  /* CB_COLOR0_CLEAR_WORD1 */

         if (sctx->chip_class >= GFX8) /* R_028C94_CB_COLOR0_DCC_BASE */
            radeon_emit(cs, cb_dcc_base);
      }
   }
   for (; i < 8; i++)
      if (sctx->framebuffer.dirty_cbufs & (1 << i))
         radeon_set_context_reg(cs, R_028C70_CB_COLOR0_INFO + i * 0x3C, 0);

   /* ZS buffer. */
   if (state->zsbuf && sctx->framebuffer.dirty_zsbuf) {
      struct si_surface *zb = (struct si_surface *)state->zsbuf;
      struct si_texture *tex = (struct si_texture *)zb->base.texture;
      unsigned db_z_info = zb->db_z_info;
      unsigned db_stencil_info = zb->db_stencil_info;
      unsigned db_htile_surface = zb->db_htile_surface;

      radeon_add_to_buffer_list(sctx, &sctx->gfx_cs, &tex->buffer, RADEON_USAGE_READWRITE,
                                zb->base.texture->nr_samples > 1 ? RADEON_PRIO_DEPTH_BUFFER_MSAA
                                                                 : RADEON_PRIO_DEPTH_BUFFER);

      /* Set fields dependent on tc_compatile_htile. */
      if (sctx->chip_class >= GFX9 &&
          vi_tc_compat_htile_enabled(tex, zb->base.u.tex.level, PIPE_MASK_ZS)) {
         unsigned max_zplanes = 4;

         if (tex->db_render_format == PIPE_FORMAT_Z16_UNORM && tex->buffer.b.b.nr_samples > 1)
            max_zplanes = 2;

         db_z_info |= S_028038_DECOMPRESS_ON_N_ZPLANES(max_zplanes + 1);

         if (sctx->chip_class >= GFX10) {
            db_z_info |= S_028040_ITERATE_FLUSH(1);
            db_stencil_info |= S_028044_ITERATE_FLUSH(!tex->htile_stencil_disabled);
         } else {
            db_z_info |= S_028038_ITERATE_FLUSH(1);
            db_stencil_info |= S_02803C_ITERATE_FLUSH(1);
         }
      }

      unsigned level = zb->base.u.tex.level;

      if (sctx->chip_class >= GFX10) {
         radeon_set_context_reg(cs, R_028014_DB_HTILE_DATA_BASE, zb->db_htile_data_base);
         radeon_set_context_reg(cs, R_02801C_DB_DEPTH_SIZE_XY, zb->db_depth_size);

         radeon_set_context_reg_seq(cs, R_02803C_DB_DEPTH_INFO, 7);
         radeon_emit(cs, S_02803C_RESOURCE_LEVEL(1)); /* DB_DEPTH_INFO */
         radeon_emit(cs, db_z_info |                  /* DB_Z_INFO */
                            S_028038_ZRANGE_PRECISION(tex->depth_clear_value[level] != 0));
         radeon_emit(cs, db_stencil_info);     /* DB_STENCIL_INFO */
         radeon_emit(cs, zb->db_depth_base);   /* DB_Z_READ_BASE */
         radeon_emit(cs, zb->db_stencil_base); /* DB_STENCIL_READ_BASE */
         radeon_emit(cs, zb->db_depth_base);   /* DB_Z_WRITE_BASE */
         radeon_emit(cs, zb->db_stencil_base); /* DB_STENCIL_WRITE_BASE */

         radeon_set_context_reg_seq(cs, R_028068_DB_Z_READ_BASE_HI, 5);
         radeon_emit(cs, zb->db_depth_base >> 32);      /* DB_Z_READ_BASE_HI */
         radeon_emit(cs, zb->db_stencil_base >> 32);    /* DB_STENCIL_READ_BASE_HI */
         radeon_emit(cs, zb->db_depth_base >> 32);      /* DB_Z_WRITE_BASE_HI */
         radeon_emit(cs, zb->db_stencil_base >> 32);    /* DB_STENCIL_WRITE_BASE_HI */
         radeon_emit(cs, zb->db_htile_data_base >> 32); /* DB_HTILE_DATA_BASE_HI */
      } else if (sctx->chip_class == GFX9) {
         radeon_set_context_reg_seq(cs, R_028014_DB_HTILE_DATA_BASE, 3);
         radeon_emit(cs, zb->db_htile_data_base); /* DB_HTILE_DATA_BASE */
         radeon_emit(cs,
                     S_028018_BASE_HI(zb->db_htile_data_base >> 32)); /* DB_HTILE_DATA_BASE_HI */
         radeon_emit(cs, zb->db_depth_size);                          /* DB_DEPTH_SIZE */

         radeon_set_context_reg_seq(cs, R_028038_DB_Z_INFO, 10);
         radeon_emit(cs, db_z_info |                                   /* DB_Z_INFO */
                            S_028038_ZRANGE_PRECISION(tex->depth_clear_value[level] != 0));
         radeon_emit(cs, db_stencil_info);                             /* DB_STENCIL_INFO */
         radeon_emit(cs, zb->db_depth_base);                           /* DB_Z_READ_BASE */
         radeon_emit(cs, S_028044_BASE_HI(zb->db_depth_base >> 32));   /* DB_Z_READ_BASE_HI */
         radeon_emit(cs, zb->db_stencil_base);                         /* DB_STENCIL_READ_BASE */
         radeon_emit(cs, S_02804C_BASE_HI(zb->db_stencil_base >> 32)); /* DB_STENCIL_READ_BASE_HI */
         radeon_emit(cs, zb->db_depth_base);                           /* DB_Z_WRITE_BASE */
         radeon_emit(cs, S_028054_BASE_HI(zb->db_depth_base >> 32));   /* DB_Z_WRITE_BASE_HI */
         radeon_emit(cs, zb->db_stencil_base);                         /* DB_STENCIL_WRITE_BASE */
         radeon_emit(cs,
                     S_02805C_BASE_HI(zb->db_stencil_base >> 32)); /* DB_STENCIL_WRITE_BASE_HI */

         radeon_set_context_reg_seq(cs, R_028068_DB_Z_INFO2, 2);
         radeon_emit(cs, zb->db_z_info2);       /* DB_Z_INFO2 */
         radeon_emit(cs, zb->db_stencil_info2); /* DB_STENCIL_INFO2 */
      } else {
         /* GFX6-GFX8 */
         /* Set fields dependent on tc_compatile_htile. */
         if (si_htile_enabled(tex, zb->base.u.tex.level, PIPE_MASK_ZS)) {
            if (tex->tc_compatible_htile) {
               db_htile_surface |= S_028ABC_TC_COMPATIBLE(1);

               /* 0 = full compression. N = only compress up to N-1 Z planes. */
               if (tex->buffer.b.b.nr_samples <= 1)
                  db_z_info |= S_028040_DECOMPRESS_ON_N_ZPLANES(5);
               else if (tex->buffer.b.b.nr_samples <= 4)
                  db_z_info |= S_028040_DECOMPRESS_ON_N_ZPLANES(3);
               else
                  db_z_info |= S_028040_DECOMPRESS_ON_N_ZPLANES(2);
            }
         }

         radeon_set_context_reg(cs, R_028014_DB_HTILE_DATA_BASE, zb->db_htile_data_base);

         radeon_set_context_reg_seq(cs, R_02803C_DB_DEPTH_INFO, 9);
         radeon_emit(cs, zb->db_depth_info |   /* DB_DEPTH_INFO */
                     S_02803C_ADDR5_SWIZZLE_MASK(!tex->tc_compatible_htile));
         radeon_emit(cs, db_z_info |           /* DB_Z_INFO */
                            S_028040_ZRANGE_PRECISION(tex->depth_clear_value[level] != 0));
         radeon_emit(cs, db_stencil_info);     /* DB_STENCIL_INFO */
         radeon_emit(cs, zb->db_depth_base);   /* DB_Z_READ_BASE */
         radeon_emit(cs, zb->db_stencil_base); /* DB_STENCIL_READ_BASE */
         radeon_emit(cs, zb->db_depth_base);   /* DB_Z_WRITE_BASE */
         radeon_emit(cs, zb->db_stencil_base); /* DB_STENCIL_WRITE_BASE */
         radeon_emit(cs, zb->db_depth_size);   /* DB_DEPTH_SIZE */
         radeon_emit(cs, zb->db_depth_slice);  /* DB_DEPTH_SLICE */
      }

      radeon_set_context_reg_seq(cs, R_028028_DB_STENCIL_CLEAR, 2);
      radeon_emit(cs, tex->stencil_clear_value[level]);    /* R_028028_DB_STENCIL_CLEAR */
      radeon_emit(cs, fui(tex->depth_clear_value[level])); /* R_02802C_DB_DEPTH_CLEAR */

      radeon_set_context_reg(cs, R_028008_DB_DEPTH_VIEW, zb->db_depth_view);
      radeon_set_context_reg(cs, R_028ABC_DB_HTILE_SURFACE, db_htile_surface);
   } else if (sctx->framebuffer.dirty_zsbuf) {
      if (sctx->chip_class == GFX9)
         radeon_set_context_reg_seq(cs, R_028038_DB_Z_INFO, 2);
      else
         radeon_set_context_reg_seq(cs, R_028040_DB_Z_INFO, 2);

      radeon_emit(cs, S_028040_FORMAT(V_028040_Z_INVALID));       /* DB_Z_INFO */
      radeon_emit(cs, S_028044_FORMAT(V_028044_STENCIL_INVALID)); /* DB_STENCIL_INFO */
   }

   /* Framebuffer dimensions. */
   /* PA_SC_WINDOW_SCISSOR_TL is set in si_init_cs_preamble_state */
   radeon_set_context_reg(cs, R_028208_PA_SC_WINDOW_SCISSOR_BR,
                          S_028208_BR_X(state->width) | S_028208_BR_Y(state->height));

   if (sctx->screen->dpbb_allowed) {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_BREAK_BATCH) | EVENT_INDEX(0));
   }
   radeon_end();

   si_update_display_dcc_dirty(sctx);

   sctx->framebuffer.dirty_cbufs = 0;
   sctx->framebuffer.dirty_zsbuf = false;
}

static void si_emit_msaa_sample_locs(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   struct si_state_rasterizer *rs = sctx->queued.named.rasterizer;
   unsigned nr_samples = sctx->framebuffer.nr_samples;
   bool has_msaa_sample_loc_bug = sctx->screen->info.has_msaa_sample_loc_bug;

   /* Smoothing (only possible with nr_samples == 1) uses the same
    * sample locations as the MSAA it simulates.
    */
   if (nr_samples <= 1 && sctx->smoothing_enabled)
      nr_samples = SI_NUM_SMOOTH_AA_SAMPLES;

   /* On Polaris, the small primitive filter uses the sample locations
    * even when MSAA is off, so we need to make sure they're set to 0.
    *
    * GFX10 uses sample locations unconditionally, so they always need
    * to be set up.
    */
   if ((nr_samples >= 2 || has_msaa_sample_loc_bug || sctx->chip_class >= GFX10) &&
       nr_samples != sctx->sample_locs_num_samples) {
      sctx->sample_locs_num_samples = nr_samples;
      si_emit_sample_locations(cs, nr_samples);
   }

   radeon_begin(cs);

   if (sctx->family >= CHIP_POLARIS10) {
      unsigned small_prim_filter_cntl =
         S_028830_SMALL_PRIM_FILTER_ENABLE(1) |
         /* line bug */
         S_028830_LINE_FILTER_DISABLE(sctx->family <= CHIP_POLARIS12);

      /* For hardware with the sample location bug, the problem is that in order to use the small
       * primitive filter, we need to explicitly set the sample locations to 0. But the DB doesn't
       * properly process the change of sample locations without a flush, and so we can end up
       * with incorrect Z values.
       *
       * Instead of doing a flush, just disable the small primitive filter when MSAA is
       * force-disabled.
       *
       * The alternative of setting sample locations to 0 would require a DB flush to avoid
       * Z errors, see https://bugs.freedesktop.org/show_bug.cgi?id=96908
       */
      if (has_msaa_sample_loc_bug && sctx->framebuffer.nr_samples > 1 && !rs->multisample_enable)
         small_prim_filter_cntl &= C_028830_SMALL_PRIM_FILTER_ENABLE;

      radeon_opt_set_context_reg(sctx, R_028830_PA_SU_SMALL_PRIM_FILTER_CNTL,
                                 SI_TRACKED_PA_SU_SMALL_PRIM_FILTER_CNTL, small_prim_filter_cntl);
   }

   /* The exclusion bits can be set to improve rasterization efficiency
    * if no sample lies on the pixel boundary (-8 sample offset).
    */
   bool exclusion = sctx->chip_class >= GFX7 && (!rs->multisample_enable || nr_samples != 16);
   radeon_opt_set_context_reg(
      sctx, R_02882C_PA_SU_PRIM_FILTER_CNTL, SI_TRACKED_PA_SU_PRIM_FILTER_CNTL,
      S_02882C_XMAX_RIGHT_EXCLUSION(exclusion) | S_02882C_YMAX_BOTTOM_EXCLUSION(exclusion));
   radeon_end();
}

static bool si_out_of_order_rasterization(struct si_context *sctx)
{
   struct si_state_blend *blend = sctx->queued.named.blend;
   struct si_state_dsa *dsa = sctx->queued.named.dsa;

   if (!sctx->screen->has_out_of_order_rast)
      return false;

   unsigned colormask = sctx->framebuffer.colorbuf_enabled_4bit;

   colormask &= blend->cb_target_enabled_4bit;

   /* Conservative: No logic op. */
   if (colormask && blend->logicop_enable)
      return false;

   struct si_dsa_order_invariance dsa_order_invariant = {.zs = true,
                                                         .pass_set = true,
                                                         .pass_last = false};

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

      if (sctx->num_perfect_occlusion_queries != 0 && !dsa_order_invariant.pass_set)
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

   if (colormask & ~blendmask) {
      if (!dsa_order_invariant.pass_last)
         return false;
   }

   return true;
}

static void si_emit_msaa_config(struct si_context *sctx)
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
      S_028A4C_OUT_OF_ORDER_WATER_MARK(0x7) |
      /* always 1: */
      S_028A4C_WALK_ALIGN8_PRIM_FITS_ST(1) | S_028A4C_SUPERTILE_WALK_ORDER_ENABLE(1) |
      S_028A4C_TILE_WALK_ORDER_ENABLE(1) | S_028A4C_MULTI_SHADER_ENGINE_PRIM_DISCARD_ENABLE(1) |
      S_028A4C_FORCE_EOV_CNTDWN_ENABLE(1) | S_028A4C_FORCE_EOV_REZ_ENABLE(1);
   unsigned db_eqaa = S_028804_HIGH_QUALITY_INTERSECTIONS(1) | S_028804_INCOHERENT_EQAA_READS(1) |
                      S_028804_INTERPOLATE_COMP_Z(1) | S_028804_STATIC_ANCHOR_ASSOCIATIONS(1);
   unsigned coverage_samples, color_samples, z_samples;
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
   coverage_samples = color_samples = z_samples = si_get_num_coverage_samples(sctx);

   if (sctx->framebuffer.nr_samples > 1 && rs->multisample_enable) {
      color_samples = sctx->framebuffer.nr_color_samples;

      if (sctx->framebuffer.state.zsbuf) {
         z_samples = sctx->framebuffer.state.zsbuf->texture->nr_samples;
         z_samples = MAX2(1, z_samples);
      } else {
         z_samples = coverage_samples;
      }
   }

   /* Required by OpenGL line rasterization.
    *
    * TODO: We should also enable perpendicular endcaps for AA lines,
    *       but that requires implementing line stippling in the pixel
    *       shader. SC can only do line stippling with axis-aligned
    *       endcaps.
    */
   unsigned sc_line_cntl = S_028BDC_DX10_DIAMOND_TEST_ENA(1);
   unsigned sc_aa_config = 0;

   if (coverage_samples > 1) {
      /* distance from the pixel center, indexed by log2(nr_samples) */
      static unsigned max_dist[] = {
         0, /* unused */
         4, /* 2x MSAA */
         6, /* 4x MSAA */
         7, /* 8x MSAA */
         8, /* 16x MSAA */
      };
      unsigned log_samples = util_logbase2(coverage_samples);
      unsigned log_z_samples = util_logbase2(z_samples);
      unsigned ps_iter_samples = si_get_ps_iter_samples(sctx);
      unsigned log_ps_iter_samples = util_logbase2(ps_iter_samples);

      sc_line_cntl |= S_028BDC_EXPAND_LINE_WIDTH(1);
      sc_aa_config = S_028BE0_MSAA_NUM_SAMPLES(log_samples) |
                     S_028BE0_MAX_SAMPLE_DIST(max_dist[log_samples]) |
                     S_028BE0_MSAA_EXPOSED_SAMPLES(log_samples) |
                     S_028BE0_COVERED_CENTROID_IS_CENTER(sctx->chip_class >= GFX10_3);

      if (sctx->framebuffer.nr_samples > 1) {
         db_eqaa |= S_028804_MAX_ANCHOR_SAMPLES(log_z_samples) |
                    S_028804_PS_ITER_SAMPLES(log_ps_iter_samples) |
                    S_028804_MASK_EXPORT_NUM_SAMPLES(log_samples) |
                    S_028804_ALPHA_TO_MASK_NUM_SAMPLES(log_samples);
         sc_mode_cntl_1 |= S_028A4C_PS_ITER_SAMPLE(ps_iter_samples > 1);
      } else if (sctx->smoothing_enabled) {
         db_eqaa |= S_028804_OVERRASTERIZATION_AMOUNT(log_samples);
      }
   }

   radeon_begin(cs);

   /* R_028BDC_PA_SC_LINE_CNTL, R_028BE0_PA_SC_AA_CONFIG */
   radeon_opt_set_context_reg2(sctx, R_028BDC_PA_SC_LINE_CNTL, SI_TRACKED_PA_SC_LINE_CNTL,
                               sc_line_cntl, sc_aa_config);
   /* R_028804_DB_EQAA */
   radeon_opt_set_context_reg(sctx, R_028804_DB_EQAA, SI_TRACKED_DB_EQAA, db_eqaa);
   /* R_028A4C_PA_SC_MODE_CNTL_1 */
   radeon_opt_set_context_reg(sctx, R_028A4C_PA_SC_MODE_CNTL_1, SI_TRACKED_PA_SC_MODE_CNTL_1,
                              sc_mode_cntl_1);
   radeon_end_update_context_roll(sctx);
}

void si_update_ps_iter_samples(struct si_context *sctx)
{
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

   si_ps_key_update_sample_shading(sctx);
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
                               enum pipe_format format, unsigned offset, unsigned size,
                               uint32_t *state)
{
   const struct util_format_description *desc;
   unsigned stride;
   unsigned num_records;

   desc = util_format_description(format);
   stride = desc->block.bits / 8;

   num_records = size / stride;
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
   if (screen->info.chip_class == GFX8)
      num_records *= stride;

   state[4] = 0;
   state[5] = S_008F04_STRIDE(stride);
   state[6] = num_records;
   state[7] = S_008F0C_DST_SEL_X(si_map_swizzle(desc->swizzle[0])) |
              S_008F0C_DST_SEL_Y(si_map_swizzle(desc->swizzle[1])) |
              S_008F0C_DST_SEL_Z(si_map_swizzle(desc->swizzle[2])) |
              S_008F0C_DST_SEL_W(si_map_swizzle(desc->swizzle[3]));

   if (screen->info.chip_class >= GFX10) {
      const struct gfx10_format *fmt = &gfx10_format_table[format];

      /* OOB_SELECT chooses the out-of-bounds check:
       *  - 0: (index >= NUM_RECORDS) || (offset >= STRIDE)
       *  - 1: index >= NUM_RECORDS
       *  - 2: NUM_RECORDS == 0
       *  - 3: if SWIZZLE_ENABLE == 0: offset >= NUM_RECORDS
       *       else: swizzle_address >= NUM_RECORDS
       */
      state[7] |= S_008F0C_FORMAT(fmt->img_format) |
                  S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_STRUCTURED_WITH_OFFSET) |
                  S_008F0C_RESOURCE_LEVEL(1);
   } else {
      int first_non_void;
      unsigned num_format, data_format;

      first_non_void = util_format_get_first_non_void_channel(format);
      num_format = si_translate_buffer_numformat(&screen->b, desc, first_non_void);
      data_format = si_translate_buffer_dataformat(&screen->b, desc, first_non_void);

      state[7] |= S_008F0C_NUM_FORMAT(num_format) | S_008F0C_DATA_FORMAT(data_format);
   }
}

static unsigned gfx9_border_color_swizzle(const unsigned char swizzle[4])
{
   unsigned bc_swizzle = V_008F20_BC_SWIZZLE_XYZW;

   if (swizzle[3] == PIPE_SWIZZLE_X) {
      /* For the pre-defined border color values (white, opaque
       * black, transparent black), the only thing that matters is
       * that the alpha channel winds up in the correct place
       * (because the RGB channels are all the same) so either of
       * these enumerations will work.
       */
      if (swizzle[2] == PIPE_SWIZZLE_Y)
         bc_swizzle = V_008F20_BC_SWIZZLE_WZYX;
      else
         bc_swizzle = V_008F20_BC_SWIZZLE_WXYZ;
   } else if (swizzle[0] == PIPE_SWIZZLE_X) {
      if (swizzle[1] == PIPE_SWIZZLE_Y)
         bc_swizzle = V_008F20_BC_SWIZZLE_XYZW;
      else
         bc_swizzle = V_008F20_BC_SWIZZLE_XWYZ;
   } else if (swizzle[1] == PIPE_SWIZZLE_X) {
      bc_swizzle = V_008F20_BC_SWIZZLE_YXWZ;
   } else if (swizzle[2] == PIPE_SWIZZLE_X) {
      bc_swizzle = V_008F20_BC_SWIZZLE_ZYXW;
   }

   return bc_swizzle;
}

/**
 * Build the sampler view descriptor for a texture.
 */
static void gfx10_make_texture_descriptor(
   struct si_screen *screen, struct si_texture *tex, bool sampler, enum pipe_texture_target target,
   enum pipe_format pipe_format, const unsigned char state_swizzle[4], unsigned first_level,
   unsigned last_level, unsigned first_layer, unsigned last_layer, unsigned width, unsigned height,
   unsigned depth, uint32_t *state, uint32_t *fmask_state)
{
   struct pipe_resource *res = &tex->buffer.b.b;
   const struct util_format_description *desc;
   unsigned img_format;
   unsigned char swizzle[4];
   unsigned type;
   uint64_t va;

   desc = util_format_description(pipe_format);
   img_format = gfx10_format_table[pipe_format].img_format;

   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS) {
      const unsigned char swizzle_xxxx[4] = {0, 0, 0, 0};
      const unsigned char swizzle_yyyy[4] = {1, 1, 1, 1};
      const unsigned char swizzle_wwww[4] = {3, 3, 3, 3};
      bool is_stencil = false;

      switch (pipe_format) {
      case PIPE_FORMAT_S8_UINT_Z24_UNORM:
      case PIPE_FORMAT_X32_S8X24_UINT:
      case PIPE_FORMAT_X8Z24_UNORM:
         util_format_compose_swizzles(swizzle_yyyy, state_swizzle, swizzle);
         is_stencil = true;
         break;
      case PIPE_FORMAT_X24S8_UINT:
         /*
          * X24S8 is implemented as an 8_8_8_8 data format, to
          * fix texture gathers. This affects at least
          * GL45-CTS.texture_cube_map_array.sampling on GFX8.
          */
         util_format_compose_swizzles(swizzle_wwww, state_swizzle, swizzle);
         is_stencil = true;
         break;
      default:
         util_format_compose_swizzles(swizzle_xxxx, state_swizzle, swizzle);
         is_stencil = pipe_format == PIPE_FORMAT_S8_UINT;
      }

      if (tex->upgraded_depth && !is_stencil) {
         assert(img_format == V_008F0C_GFX10_FORMAT_32_FLOAT);
         img_format = V_008F0C_GFX10_FORMAT_32_FLOAT_CLAMP;
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

   state[0] = 0;
   state[1] = S_00A004_FORMAT(img_format) | S_00A004_WIDTH_LO(width - 1);
   state[2] = S_00A008_WIDTH_HI((width - 1) >> 2) | S_00A008_HEIGHT(height - 1) |
              S_00A008_RESOURCE_LEVEL(1);
   state[3] =
      S_00A00C_DST_SEL_X(si_map_swizzle(swizzle[0])) |
      S_00A00C_DST_SEL_Y(si_map_swizzle(swizzle[1])) |
      S_00A00C_DST_SEL_Z(si_map_swizzle(swizzle[2])) |
      S_00A00C_DST_SEL_W(si_map_swizzle(swizzle[3])) |
      S_00A00C_BASE_LEVEL(res->nr_samples > 1 ? 0 : first_level) |
      S_00A00C_LAST_LEVEL(res->nr_samples > 1 ? util_logbase2(res->nr_samples) : last_level) |
      S_00A00C_BC_SWIZZLE(gfx9_border_color_swizzle(desc->swizzle)) | S_00A00C_TYPE(type);
   /* Depth is the the last accessible layer on gfx9+. The hw doesn't need
    * to know the total number of layers.
    */
   state[4] =
      S_00A010_DEPTH((type == V_008F1C_SQ_RSRC_IMG_3D && sampler) ? depth - 1 : last_layer) |
      S_00A010_BASE_ARRAY(first_layer);
   state[5] = S_00A014_ARRAY_PITCH(!!(type == V_008F1C_SQ_RSRC_IMG_3D && !sampler)) |
              S_00A014_MAX_MIP(res->nr_samples > 1 ? util_logbase2(res->nr_samples)
                                                   : tex->buffer.b.b.last_level) |
              S_00A014_PERF_MOD(4);
   state[6] = 0;
   state[7] = 0;

   if (vi_dcc_enabled(tex, first_level)) {
      state[6] |= S_00A018_MAX_UNCOMPRESSED_BLOCK_SIZE(V_028C78_MAX_BLOCK_SIZE_256B) |
                  S_00A018_MAX_COMPRESSED_BLOCK_SIZE(tex->surface.u.gfx9.color.dcc.max_compressed_block_size) |
                  S_00A018_ALPHA_IS_ON_MSB(vi_alpha_is_on_msb(screen, pipe_format));
   }

   /* Initialize the sampler view for FMASK. */
   if (tex->surface.fmask_offset) {
      uint32_t format;

      va = tex->buffer.gpu_address + tex->surface.fmask_offset;

#define FMASK(s, f) (((unsigned)(MAX2(1, s)) * 16) + (MAX2(1, f)))
      switch (FMASK(res->nr_samples, res->nr_storage_samples)) {
      case FMASK(2, 1):
         format = V_008F0C_GFX10_FORMAT_FMASK8_S2_F1;
         break;
      case FMASK(2, 2):
         format = V_008F0C_GFX10_FORMAT_FMASK8_S2_F2;
         break;
      case FMASK(4, 1):
         format = V_008F0C_GFX10_FORMAT_FMASK8_S4_F1;
         break;
      case FMASK(4, 2):
         format = V_008F0C_GFX10_FORMAT_FMASK8_S4_F2;
         break;
      case FMASK(4, 4):
         format = V_008F0C_GFX10_FORMAT_FMASK8_S4_F4;
         break;
      case FMASK(8, 1):
         format = V_008F0C_GFX10_FORMAT_FMASK8_S8_F1;
         break;
      case FMASK(8, 2):
         format = V_008F0C_GFX10_FORMAT_FMASK16_S8_F2;
         break;
      case FMASK(8, 4):
         format = V_008F0C_GFX10_FORMAT_FMASK32_S8_F4;
         break;
      case FMASK(8, 8):
         format = V_008F0C_GFX10_FORMAT_FMASK32_S8_F8;
         break;
      case FMASK(16, 1):
         format = V_008F0C_GFX10_FORMAT_FMASK16_S16_F1;
         break;
      case FMASK(16, 2):
         format = V_008F0C_GFX10_FORMAT_FMASK32_S16_F2;
         break;
      case FMASK(16, 4):
         format = V_008F0C_GFX10_FORMAT_FMASK64_S16_F4;
         break;
      case FMASK(16, 8):
         format = V_008F0C_GFX10_FORMAT_FMASK64_S16_F8;
         break;
      default:
         unreachable("invalid nr_samples");
      }
#undef FMASK
      fmask_state[0] = (va >> 8) | tex->surface.fmask_tile_swizzle;
      fmask_state[1] = S_00A004_BASE_ADDRESS_HI(va >> 40) | S_00A004_FORMAT(format) |
                       S_00A004_WIDTH_LO(width - 1);
      fmask_state[2] = S_00A008_WIDTH_HI((width - 1) >> 2) | S_00A008_HEIGHT(height - 1) |
                       S_00A008_RESOURCE_LEVEL(1);
      fmask_state[3] =
         S_00A00C_DST_SEL_X(V_008F1C_SQ_SEL_X) | S_00A00C_DST_SEL_Y(V_008F1C_SQ_SEL_X) |
         S_00A00C_DST_SEL_Z(V_008F1C_SQ_SEL_X) | S_00A00C_DST_SEL_W(V_008F1C_SQ_SEL_X) |
         S_00A00C_SW_MODE(tex->surface.u.gfx9.color.fmask_swizzle_mode) |
         S_00A00C_TYPE(si_tex_dim(screen, tex, target, 0));
      fmask_state[4] = S_00A010_DEPTH(last_layer) | S_00A010_BASE_ARRAY(first_layer);
      fmask_state[5] = 0;
      fmask_state[6] = S_00A018_META_PIPE_ALIGNED(1);
      fmask_state[7] = 0;
   }
}

/**
 * Build the sampler view descriptor for a texture (SI-GFX9).
 */
static void si_make_texture_descriptor(struct si_screen *screen, struct si_texture *tex,
                                       bool sampler, enum pipe_texture_target target,
                                       enum pipe_format pipe_format,
                                       const unsigned char state_swizzle[4], unsigned first_level,
                                       unsigned last_level, unsigned first_layer,
                                       unsigned last_layer, unsigned width, unsigned height,
                                       unsigned depth, uint32_t *state, uint32_t *fmask_state)
{
   struct pipe_resource *res = &tex->buffer.b.b;
   const struct util_format_description *desc;
   unsigned char swizzle[4];
   int first_non_void;
   unsigned num_format, data_format, type, num_samples;
   uint64_t va;

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
         if (screen->info.chip_class <= GFX8)
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

   first_non_void = util_format_get_first_non_void_channel(pipe_format);

   switch (pipe_format) {
   case PIPE_FORMAT_S8_UINT_Z24_UNORM:
      num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
      break;
   default:
      if (first_non_void < 0) {
         if (util_format_is_compressed(pipe_format)) {
            switch (pipe_format) {
            case PIPE_FORMAT_DXT1_SRGB:
            case PIPE_FORMAT_DXT1_SRGBA:
            case PIPE_FORMAT_DXT3_SRGBA:
            case PIPE_FORMAT_DXT5_SRGBA:
            case PIPE_FORMAT_BPTC_SRGBA:
            case PIPE_FORMAT_ETC2_SRGB8:
            case PIPE_FORMAT_ETC2_SRGB8A1:
            case PIPE_FORMAT_ETC2_SRGBA8:
               num_format = V_008F14_IMG_NUM_FORMAT_SRGB;
               break;
            case PIPE_FORMAT_RGTC1_SNORM:
            case PIPE_FORMAT_LATC1_SNORM:
            case PIPE_FORMAT_RGTC2_SNORM:
            case PIPE_FORMAT_LATC2_SNORM:
            case PIPE_FORMAT_ETC2_R11_SNORM:
            case PIPE_FORMAT_ETC2_RG11_SNORM:
            /* implies float, so use SNORM/UNORM to determine
               whether data is signed or not */
            case PIPE_FORMAT_BPTC_RGB_FLOAT:
               num_format = V_008F14_IMG_NUM_FORMAT_SNORM;
               break;
            default:
               num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
               break;
            }
         } else if (desc->layout == UTIL_FORMAT_LAYOUT_SUBSAMPLED) {
            num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
         } else {
            num_format = V_008F14_IMG_NUM_FORMAT_FLOAT;
         }
      } else if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
         num_format = V_008F14_IMG_NUM_FORMAT_SRGB;
      } else {
         num_format = V_008F14_IMG_NUM_FORMAT_UNORM;

         switch (desc->channel[first_non_void].type) {
         case UTIL_FORMAT_TYPE_FLOAT:
            num_format = V_008F14_IMG_NUM_FORMAT_FLOAT;
            break;
         case UTIL_FORMAT_TYPE_SIGNED:
            if (desc->channel[first_non_void].normalized)
               num_format = V_008F14_IMG_NUM_FORMAT_SNORM;
            else if (desc->channel[first_non_void].pure_integer)
               num_format = V_008F14_IMG_NUM_FORMAT_SINT;
            else
               num_format = V_008F14_IMG_NUM_FORMAT_SSCALED;
            break;
         case UTIL_FORMAT_TYPE_UNSIGNED:
            if (desc->channel[first_non_void].normalized)
               num_format = V_008F14_IMG_NUM_FORMAT_UNORM;
            else if (desc->channel[first_non_void].pure_integer)
               num_format = V_008F14_IMG_NUM_FORMAT_UINT;
            else
               num_format = V_008F14_IMG_NUM_FORMAT_USCALED;
         }
      }
   }

   data_format = si_translate_texformat(&screen->b, pipe_format, desc, first_non_void);
   if (data_format == ~0) {
      data_format = 0;
   }

   /* S8 with Z32 HTILE needs a special format. */
   if (screen->info.chip_class == GFX9 && pipe_format == PIPE_FORMAT_S8_UINT)
      data_format = V_008F14_IMG_DATA_FORMAT_S8_32;

   if (!sampler && (res->target == PIPE_TEXTURE_CUBE || res->target == PIPE_TEXTURE_CUBE_ARRAY ||
                    (screen->info.chip_class <= GFX8 && res->target == PIPE_TEXTURE_3D))) {
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

   state[0] = 0;
   state[1] = (S_008F14_DATA_FORMAT(data_format) | S_008F14_NUM_FORMAT(num_format));
   state[2] = (S_008F18_WIDTH(width - 1) | S_008F18_HEIGHT(height - 1) | S_008F18_PERF_MOD(4));
   state[3] = (S_008F1C_DST_SEL_X(si_map_swizzle(swizzle[0])) |
               S_008F1C_DST_SEL_Y(si_map_swizzle(swizzle[1])) |
               S_008F1C_DST_SEL_Z(si_map_swizzle(swizzle[2])) |
               S_008F1C_DST_SEL_W(si_map_swizzle(swizzle[3])) |
               S_008F1C_BASE_LEVEL(num_samples > 1 ? 0 : first_level) |
               S_008F1C_LAST_LEVEL(num_samples > 1 ? util_logbase2(num_samples) : last_level) |
               S_008F1C_TYPE(type));
   state[4] = 0;
   state[5] = S_008F24_BASE_ARRAY(first_layer);
   state[6] = 0;
   state[7] = 0;

   if (screen->info.chip_class == GFX9) {
      unsigned bc_swizzle = gfx9_border_color_swizzle(desc->swizzle);

      /* Depth is the the last accessible layer on Gfx9.
       * The hw doesn't need to know the total number of layers.
       */
      if (type == V_008F1C_SQ_RSRC_IMG_3D)
         state[4] |= S_008F20_DEPTH(depth - 1);
      else
         state[4] |= S_008F20_DEPTH(last_layer);

      state[4] |= S_008F20_BC_SWIZZLE(bc_swizzle);
      state[5] |= S_008F24_MAX_MIP(num_samples > 1 ? util_logbase2(num_samples)
                                                   : tex->buffer.b.b.last_level);
   } else {
      state[3] |= S_008F1C_POW2_PAD(res->last_level > 0);
      state[4] |= S_008F20_DEPTH(depth - 1);
      state[5] |= S_008F24_LAST_ARRAY(last_layer);
   }

   if (vi_dcc_enabled(tex, first_level)) {
      state[6] = S_008F28_ALPHA_IS_ON_MSB(vi_alpha_is_on_msb(screen, pipe_format));
   } else {
      /* The last dword is unused by hw. The shader uses it to clear
       * bits in the first dword of sampler state.
       */
      if (screen->info.chip_class <= GFX7 && res->nr_samples <= 1) {
         if (first_level == last_level)
            state[7] = C_008F30_MAX_ANISO_RATIO;
         else
            state[7] = 0xffffffff;
      }
   }

   /* Initialize the sampler view for FMASK. */
   if (tex->surface.fmask_offset) {
      uint32_t data_format, num_format;

      va = tex->buffer.gpu_address + tex->surface.fmask_offset;

#define FMASK(s, f) (((unsigned)(MAX2(1, s)) * 16) + (MAX2(1, f)))
      if (screen->info.chip_class == GFX9) {
         data_format = V_008F14_IMG_DATA_FORMAT_FMASK;
         switch (FMASK(res->nr_samples, res->nr_storage_samples)) {
         case FMASK(2, 1):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_2_1;
            break;
         case FMASK(2, 2):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_2_2;
            break;
         case FMASK(4, 1):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_4_1;
            break;
         case FMASK(4, 2):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_4_2;
            break;
         case FMASK(4, 4):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_4_4;
            break;
         case FMASK(8, 1):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_8_8_1;
            break;
         case FMASK(8, 2):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_16_8_2;
            break;
         case FMASK(8, 4):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_32_8_4;
            break;
         case FMASK(8, 8):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_32_8_8;
            break;
         case FMASK(16, 1):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_16_16_1;
            break;
         case FMASK(16, 2):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_32_16_2;
            break;
         case FMASK(16, 4):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_64_16_4;
            break;
         case FMASK(16, 8):
            num_format = V_008F14_IMG_NUM_FORMAT_FMASK_64_16_8;
            break;
         default:
            unreachable("invalid nr_samples");
         }
      } else {
         switch (FMASK(res->nr_samples, res->nr_storage_samples)) {
         case FMASK(2, 1):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S2_F1;
            break;
         case FMASK(2, 2):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S2_F2;
            break;
         case FMASK(4, 1):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F1;
            break;
         case FMASK(4, 2):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F2;
            break;
         case FMASK(4, 4):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S4_F4;
            break;
         case FMASK(8, 1):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK8_S8_F1;
            break;
         case FMASK(8, 2):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK16_S8_F2;
            break;
         case FMASK(8, 4):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK32_S8_F4;
            break;
         case FMASK(8, 8):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK32_S8_F8;
            break;
         case FMASK(16, 1):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK16_S16_F1;
            break;
         case FMASK(16, 2):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK32_S16_F2;
            break;
         case FMASK(16, 4):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK64_S16_F4;
            break;
         case FMASK(16, 8):
            data_format = V_008F14_IMG_DATA_FORMAT_FMASK64_S16_F8;
            break;
         default:
            unreachable("invalid nr_samples");
         }
         num_format = V_008F14_IMG_NUM_FORMAT_UINT;
      }
#undef FMASK

      fmask_state[0] = (va >> 8) | tex->surface.fmask_tile_swizzle;
      fmask_state[1] = S_008F14_BASE_ADDRESS_HI(va >> 40) | S_008F14_DATA_FORMAT(data_format) |
                       S_008F14_NUM_FORMAT(num_format);
      fmask_state[2] = S_008F18_WIDTH(width - 1) | S_008F18_HEIGHT(height - 1);
      fmask_state[3] =
         S_008F1C_DST_SEL_X(V_008F1C_SQ_SEL_X) | S_008F1C_DST_SEL_Y(V_008F1C_SQ_SEL_X) |
         S_008F1C_DST_SEL_Z(V_008F1C_SQ_SEL_X) | S_008F1C_DST_SEL_W(V_008F1C_SQ_SEL_X) |
         S_008F1C_TYPE(si_tex_dim(screen, tex, target, 0));
      fmask_state[4] = 0;
      fmask_state[5] = S_008F24_BASE_ARRAY(first_layer);
      fmask_state[6] = 0;
      fmask_state[7] = 0;

      if (screen->info.chip_class == GFX9) {
         fmask_state[3] |= S_008F1C_SW_MODE(tex->surface.u.gfx9.color.fmask_swizzle_mode);
         fmask_state[4] |=
            S_008F20_DEPTH(last_layer) | S_008F20_PITCH(tex->surface.u.gfx9.color.fmask_epitch);
         fmask_state[5] |= S_008F24_META_PIPE_ALIGNED(1) |
                           S_008F24_META_RB_ALIGNED(1);
      } else {
         fmask_state[3] |= S_008F1C_TILING_INDEX(tex->surface.u.legacy.color.fmask.tiling_index);
         fmask_state[4] |= S_008F20_DEPTH(depth - 1) |
                           S_008F20_PITCH(tex->surface.u.legacy.color.fmask.pitch_in_pixels - 1);
         fmask_state[5] |= S_008F24_LAST_ARRAY(last_layer);
      }
   }
}

/**
 * Create a sampler view.
 *
 * @param ctx		context
 * @param texture	texture
 * @param state		sampler view template
 * @param width0	width0 override (for compressed textures as int)
 * @param height0	height0 override (for compressed textures as int)
 * @param force_level   set the base address to the level (for compressed textures)
 */
struct pipe_sampler_view *si_create_sampler_view_custom(struct pipe_context *ctx,
                                                        struct pipe_resource *texture,
                                                        const struct pipe_sampler_view *state,
                                                        unsigned width0, unsigned height0,
                                                        unsigned force_level)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_sampler_view *view = CALLOC_STRUCT(si_sampler_view);
   struct si_texture *tex = (struct si_texture *)texture;
   unsigned base_level, first_level, last_level;
   unsigned char state_swizzle[4];
   unsigned height, depth, width;
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
      si_make_buffer_descriptor(sctx->screen, si_resource(texture), state->format,
                                state->u.buf.offset, state->u.buf.size, view->state);
      return &view->base;
   }

   state_swizzle[0] = state->swizzle_r;
   state_swizzle[1] = state->swizzle_g;
   state_swizzle[2] = state->swizzle_b;
   state_swizzle[3] = state->swizzle_a;

   base_level = 0;
   first_level = state->u.tex.first_level;
   last_level = state->u.tex.last_level;
   width = width0;
   height = height0;
   depth = texture->depth0;

   if (sctx->chip_class <= GFX8 && force_level) {
      assert(force_level == first_level && force_level == last_level);
      base_level = force_level;
      first_level = 0;
      last_level = 0;
      width = u_minify(width, force_level);
      height = u_minify(height, force_level);
      depth = u_minify(depth, force_level);
   }

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

   sctx->screen->make_texture_descriptor(
      sctx->screen, tex, true, state->target, pipe_format, state_swizzle, first_level, last_level,
      state->u.tex.first_layer, last_layer, width, height, depth, view->state, view->fmask_state);

   view->base_level_info = &surflevel[base_level];
   view->base_level = base_level;
   view->block_width = util_format_get_blockwidth(pipe_format);
   return &view->base;
}

static struct pipe_sampler_view *si_create_sampler_view(struct pipe_context *ctx,
                                                        struct pipe_resource *texture,
                                                        const struct pipe_sampler_view *state)
{
   return si_create_sampler_view_custom(ctx, texture, state, texture ? texture->width0 : 0,
                                        texture ? texture->height0 : 0, 0);
}

static void si_sampler_view_destroy(struct pipe_context *ctx, struct pipe_sampler_view *state)
{
   struct si_sampler_view *view = (struct si_sampler_view *)state;

   pipe_resource_reference(&state->texture, NULL);
   FREE(view);
}

static bool wrap_mode_uses_border_color(unsigned wrap, bool linear_filter)
{
   return wrap == PIPE_TEX_WRAP_CLAMP_TO_BORDER || wrap == PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER ||
          (linear_filter && (wrap == PIPE_TEX_WRAP_CLAMP || wrap == PIPE_TEX_WRAP_MIRROR_CLAMP));
}

static uint32_t si_translate_border_color(struct si_context *sctx,
                                          const struct pipe_sampler_state *state,
                                          const union pipe_color_union *color, bool is_integer)
{
   bool linear_filter = state->min_img_filter != PIPE_TEX_FILTER_NEAREST ||
                        state->mag_img_filter != PIPE_TEX_FILTER_NEAREST;

   if (!wrap_mode_uses_border_color(state->wrap_s, linear_filter) &&
       !wrap_mode_uses_border_color(state->wrap_t, linear_filter) &&
       !wrap_mode_uses_border_color(state->wrap_r, linear_filter))
      return S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK);

#define simple_border_types(elt)                                                                   \
   do {                                                                                            \
      if (color->elt[0] == 0 && color->elt[1] == 0 && color->elt[2] == 0 && color->elt[3] == 0)    \
         return S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK);              \
      if (color->elt[0] == 0 && color->elt[1] == 0 && color->elt[2] == 0 && color->elt[3] == 1)    \
         return S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_BLACK);             \
      if (color->elt[0] == 1 && color->elt[1] == 1 && color->elt[2] == 1 && color->elt[3] == 1)    \
         return S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_OPAQUE_WHITE);             \
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
      fprintf(stderr, "radeonsi: The border color table is full. "
                      "Any new border colors will be just black. "
                      "Please file a bug.\n");
      return S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_TRANS_BLACK);
   }

   if (i == sctx->border_color_count) {
      /* Upload a new border color. */
      memcpy(&sctx->border_color_table[i], color, sizeof(*color));
      util_memcpy_cpu_to_le32(&sctx->border_color_map[i], color, sizeof(*color));
      sctx->border_color_count++;
   }

   return S_008F3C_BORDER_COLOR_PTR(i) |
          S_008F3C_BORDER_COLOR_TYPE(V_008F3C_SQ_TEX_BORDER_COLOR_REGISTER);
}

static inline int S_FIXED(float value, unsigned frac_bits)
{
   return value * (1 << frac_bits);
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

static void *si_create_sampler_state(struct pipe_context *ctx,
                                     const struct pipe_sampler_state *state)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct si_screen *sscreen = sctx->screen;
   struct si_sampler_state *rstate = CALLOC_STRUCT(si_sampler_state);
   unsigned max_aniso = sscreen->force_aniso >= 0 ? sscreen->force_aniso : state->max_anisotropy;
   unsigned max_aniso_ratio = si_tex_aniso_filter(max_aniso);
   bool trunc_coord = state->min_img_filter == PIPE_TEX_FILTER_NEAREST &&
                      state->mag_img_filter == PIPE_TEX_FILTER_NEAREST &&
                      state->compare_mode == PIPE_TEX_COMPARE_NONE;
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
   rstate->val[0] =
      (S_008F30_CLAMP_X(si_tex_wrap(state->wrap_s)) | S_008F30_CLAMP_Y(si_tex_wrap(state->wrap_t)) |
       S_008F30_CLAMP_Z(si_tex_wrap(state->wrap_r)) | S_008F30_MAX_ANISO_RATIO(max_aniso_ratio) |
       S_008F30_DEPTH_COMPARE_FUNC(si_tex_compare(state->compare_func)) |
       S_008F30_FORCE_UNNORMALIZED(!state->normalized_coords) |
       S_008F30_ANISO_THRESHOLD(max_aniso_ratio >> 1) | S_008F30_ANISO_BIAS(max_aniso_ratio) |
       S_008F30_DISABLE_CUBE_WRAP(!state->seamless_cube_map) |
       S_008F30_TRUNC_COORD(trunc_coord) |
       S_008F30_COMPAT_MODE(sctx->chip_class == GFX8 || sctx->chip_class == GFX9));
   rstate->val[1] = (S_008F34_MIN_LOD(S_FIXED(CLAMP(state->min_lod, 0, 15), 8)) |
                     S_008F34_MAX_LOD(S_FIXED(CLAMP(state->max_lod, 0, 15), 8)) |
                     S_008F34_PERF_MIP(max_aniso_ratio ? max_aniso_ratio + 6 : 0));
   rstate->val[2] = (S_008F38_LOD_BIAS(S_FIXED(CLAMP(state->lod_bias, -16, 16), 8)) |
                     S_008F38_XY_MAG_FILTER(si_tex_filter(state->mag_img_filter, max_aniso)) |
                     S_008F38_XY_MIN_FILTER(si_tex_filter(state->min_img_filter, max_aniso)) |
                     S_008F38_MIP_FILTER(si_tex_mipfilter(state->min_mip_filter)) |
                     S_008F38_MIP_POINT_PRECLAMP(0));
   rstate->val[3] = si_translate_border_color(sctx, state, &state->border_color,
                                              state->border_color_is_integer);

   if (sscreen->info.chip_class >= GFX10) {
      rstate->val[2] |= S_008F38_ANISO_OVERRIDE_GFX10(1);
   } else {
      rstate->val[2] |= S_008F38_DISABLE_LSB_CEIL(sctx->chip_class <= GFX8) |
                        S_008F38_FILTER_PREC_FIX(1) |
                        S_008F38_ANISO_OVERRIDE_GFX8(sctx->chip_class >= GFX8);
   }

   /* Create sampler resource for upgraded depth textures. */
   memcpy(rstate->upgraded_depth_val, rstate->val, sizeof(rstate->val));

   for (unsigned i = 0; i < 4; ++i) {
      /* Use channel 0 on purpose, so that we can use OPAQUE_WHITE
       * when the border color is 1.0. */
      clamped_border_color.f[i] = CLAMP(state->border_color.f[0], 0, 1);
   }

   if (memcmp(&state->border_color, &clamped_border_color, sizeof(clamped_border_color)) == 0) {
      if (sscreen->info.chip_class <= GFX9)
         rstate->upgraded_depth_val[3] |= S_008F3C_UPGRADED_DEPTH(1);
   } else {
      rstate->upgraded_depth_val[3] =
         si_translate_border_color(sctx, state, &clamped_border_color, false);
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

static void si_emit_sample_mask(struct si_context *sctx)
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
   radeon_set_context_reg_seq(cs, R_028C38_PA_SC_AA_MASK_X0Y0_X1Y0, 2);
   radeon_emit(cs, mask | (mask << 16));
   radeon_emit(cs, mask | (mask << 16));
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

struct si_fast_udiv_info32 si_compute_fast_udiv_info32(uint32_t D, unsigned num_bits)
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
   struct si_vertex_elements *v = CALLOC_STRUCT(si_vertex_elements);
   bool used[SI_NUM_VERTEX_BUFFERS] = {};
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

   unsigned num_vbos_in_user_sgprs = si_num_vbos_in_user_sgprs(sscreen);
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

      if (!used[vbo_index]) {
         v->first_vb_use_mask |= 1 << i;
         used[vbo_index] = true;
      }

      desc = util_format_description(elements[i].src_format);
      first_non_void = util_format_get_first_non_void_channel(elements[i].src_format);
      channel = first_non_void >= 0 ? &desc->channel[first_non_void] : NULL;

      v->format_size[i] = desc->block.bits / 8;
      v->src_offset[i] = elements[i].src_offset;
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
         always_fix = sscreen->info.chip_class <= GFX8 && sscreen->info.family != CHIP_STONEY &&
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
            (sscreen->info.chip_class == GFX6 || sscreen->info.chip_class >= GFX10);
      bool opencode = sscreen->options.vs_fetch_always_opencode;

      if (check_alignment && (elements[i].src_offset & ((1 << log_hw_load_size) - 1)) != 0)
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

      v->rsrc_word3[i] = S_008F0C_DST_SEL_X(si_map_swizzle(desc->swizzle[0])) |
                         S_008F0C_DST_SEL_Y(si_map_swizzle(desc->swizzle[1])) |
                         S_008F0C_DST_SEL_Z(si_map_swizzle(desc->swizzle[2])) |
                         S_008F0C_DST_SEL_W(si_map_swizzle(desc->swizzle[3]));

      if (sscreen->info.chip_class >= GFX10) {
         const struct gfx10_format *fmt = &gfx10_format_table[elements[i].src_format];
         assert(fmt->img_format != 0 && fmt->img_format < 128);
         v->rsrc_word3[i] |= S_008F0C_FORMAT(fmt->img_format) | S_008F0C_RESOURCE_LEVEL(1);
      } else {
         unsigned data_format, num_format;
         data_format = si_translate_buffer_dataformat(ctx->screen, desc, first_non_void);
         num_format = si_translate_buffer_numformat(ctx->screen, desc, first_non_void);
         v->rsrc_word3[i] |= S_008F0C_NUM_FORMAT(num_format) | S_008F0C_DATA_FORMAT(data_format);
      }
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

   if (sctx->num_vertex_elements) {
      sctx->vertex_buffers_dirty = true;
   } else {
      sctx->vertex_buffers_dirty = false;
      sctx->vertex_buffer_pointer_dirty = false;
      sctx->vertex_buffer_user_sgprs_dirty = false;
   }

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

static void si_set_vertex_buffers(struct pipe_context *ctx, unsigned start_slot, unsigned count,
                                  unsigned unbind_num_trailing_slots, bool take_ownership,
                                  const struct pipe_vertex_buffer *buffers)
{
   struct si_context *sctx = (struct si_context *)ctx;
   struct pipe_vertex_buffer *dst = sctx->vertex_buffer + start_slot;
   unsigned updated_mask = u_bit_consecutive(start_slot, count + unbind_num_trailing_slots);
   uint32_t orig_unaligned = sctx->vertex_buffer_unaligned;
   uint32_t unaligned = 0;
   int i;

   assert(start_slot + count + unbind_num_trailing_slots <= ARRAY_SIZE(sctx->vertex_buffer));

   if (buffers) {
      if (take_ownership) {
         for (i = 0; i < count; i++) {
            const struct pipe_vertex_buffer *src = buffers + i;
            struct pipe_vertex_buffer *dsti = dst + i;
            struct pipe_resource *buf = src->buffer.resource;
            unsigned slot_bit = 1 << (start_slot + i);

            /* Only unreference bound vertex buffers. (take_ownership) */
            pipe_resource_reference(&dsti->buffer.resource, NULL);

            if (src->buffer_offset & 3 || src->stride & 3)
               unaligned |= slot_bit;

            si_context_add_resource_size(sctx, buf);
            if (buf)
               si_resource(buf)->bind_history |= PIPE_BIND_VERTEX_BUFFER;
         }
         /* take_ownership allows us to copy pipe_resource pointers without refcounting. */
         memcpy(dst, buffers, count * sizeof(struct pipe_vertex_buffer));
      } else {
         for (i = 0; i < count; i++) {
            const struct pipe_vertex_buffer *src = buffers + i;
            struct pipe_vertex_buffer *dsti = dst + i;
            struct pipe_resource *buf = src->buffer.resource;
            unsigned slot_bit = 1 << (start_slot + i);

            pipe_resource_reference(&dsti->buffer.resource, buf);
            dsti->buffer_offset = src->buffer_offset;
            dsti->stride = src->stride;

            if (dsti->buffer_offset & 3 || dsti->stride & 3)
               unaligned |= slot_bit;

            si_context_add_resource_size(sctx, buf);
            if (buf)
               si_resource(buf)->bind_history |= PIPE_BIND_VERTEX_BUFFER;
         }
      }
   } else {
      for (i = 0; i < count; i++)
         pipe_resource_reference(&dst[i].buffer.resource, NULL);
   }

   for (i = 0; i < unbind_num_trailing_slots; i++)
      pipe_resource_reference(&dst[count + i].buffer.resource, NULL);

   sctx->vertex_buffers_dirty = sctx->num_vertex_elements > 0;
   sctx->vertex_buffer_unaligned = (orig_unaligned & ~updated_mask) | unaligned;

   /* Check whether alignment may have changed in a way that requires
    * shader changes. This check is conservative: a vertex buffer can only
    * trigger a shader change if the misalignment amount changes (e.g.
    * from byte-aligned to short-aligned), but we only keep track of
    * whether buffers are at least dword-aligned, since that should always
    * be the case in well-behaved applications anyway.
    */
   if ((sctx->vertex_elements->vb_alignment_check_mask &
        (unaligned | orig_unaligned) & updated_mask)) {
      si_vs_key_update_inputs(sctx);
      sctx->do_update_shaders = true;
   }
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

static void si_set_patch_vertices(struct pipe_context *ctx, uint8_t patch_vertices)
{
   struct si_context *sctx = (struct si_context *)ctx;

   sctx->patch_vertices = patch_vertices;
}

static void si_texture_barrier(struct pipe_context *ctx, unsigned flags)
{
   struct si_context *sctx = (struct si_context *)ctx;

   si_update_fb_dirtiness_after_rendering(sctx);

   /* Multisample surfaces are flushed in si_decompress_textures. */
   if (sctx->framebuffer.uncompressed_cb_mask) {
      si_make_CB_shader_coherent(sctx, sctx->framebuffer.nr_samples,
                                 sctx->framebuffer.CB_has_shader_readable_metadata,
                                 sctx->framebuffer.all_DCC_pipe_aligned);
   }
}

/* This only ensures coherency for shader image/buffer stores. */
static void si_memory_barrier(struct pipe_context *ctx, unsigned flags)
{
   struct si_context *sctx = (struct si_context *)ctx;

   if (!(flags & ~PIPE_BARRIER_UPDATE))
      return;

   /* Subsequent commands must wait for all shader invocations to
    * complete. */
   sctx->flags |= SI_CONTEXT_PS_PARTIAL_FLUSH | SI_CONTEXT_CS_PARTIAL_FLUSH |
                  SI_CONTEXT_PFP_SYNC_ME;

   if (flags & PIPE_BARRIER_CONSTANT_BUFFER)
      sctx->flags |= SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE;

   if (flags & (PIPE_BARRIER_VERTEX_BUFFER | PIPE_BARRIER_SHADER_BUFFER | PIPE_BARRIER_TEXTURE |
                PIPE_BARRIER_IMAGE | PIPE_BARRIER_STREAMOUT_BUFFER | PIPE_BARRIER_GLOBAL_BUFFER)) {
      /* As far as I can tell, L1 contents are written back to L2
       * automatically at end of shader, but the contents of other
       * L1 caches might still be stale. */
      sctx->flags |= SI_CONTEXT_INV_VCACHE;
   }

   if (flags & PIPE_BARRIER_INDEX_BUFFER) {
      /* Indices are read through TC L2 since GFX8.
       * L1 isn't used.
       */
      if (sctx->screen->info.chip_class <= GFX7)
         sctx->flags |= SI_CONTEXT_WB_L2;
   }

   /* MSAA color, any depth and any stencil are flushed in
    * si_decompress_textures when needed.
    */
   if (flags & PIPE_BARRIER_FRAMEBUFFER && sctx->framebuffer.uncompressed_cb_mask) {
      sctx->flags |= SI_CONTEXT_FLUSH_AND_INV_CB;

      if (sctx->chip_class <= GFX8)
         sctx->flags |= SI_CONTEXT_WB_L2;
   }

   /* Indirect buffers use TC L2 on GFX9, but not older hw. */
   if (sctx->screen->info.chip_class <= GFX8 && flags & PIPE_BARRIER_INDIRECT_BUFFER)
      sctx->flags |= SI_CONTEXT_WB_L2;
}

static void *si_create_blend_custom(struct si_context *sctx, unsigned mode)
{
   struct pipe_blend_state blend;

   memset(&blend, 0, sizeof(blend));
   blend.independent_blend_enable = true;
   blend.rt[0].colormask = 0xf;
   return si_create_blend_state_mode(&sctx->b, &blend, mode);
}

void si_init_state_compute_functions(struct si_context *sctx)
{
   sctx->b.create_sampler_state = si_create_sampler_state;
   sctx->b.delete_sampler_state = si_delete_sampler_state;
   sctx->b.create_sampler_view = si_create_sampler_view;
   sctx->b.sampler_view_destroy = si_sampler_view_destroy;
   sctx->b.memory_barrier = si_memory_barrier;
}

void si_init_state_functions(struct si_context *sctx)
{
   sctx->atoms.s.framebuffer.emit = si_emit_framebuffer_state;
   sctx->atoms.s.msaa_sample_locs.emit = si_emit_msaa_sample_locs;
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
   sctx->custom_blend_resolve = si_create_blend_custom(sctx, V_028808_CB_RESOLVE);
   sctx->custom_blend_fmask_decompress = si_create_blend_custom(sctx, V_028808_CB_FMASK_DECOMPRESS);
   sctx->custom_blend_eliminate_fastclear =
      si_create_blend_custom(sctx, V_028808_CB_ELIMINATE_FAST_CLEAR);
   sctx->custom_blend_dcc_decompress = si_create_blend_custom(sctx, V_028808_CB_DCC_DECOMPRESS);

   sctx->b.set_clip_state = si_set_clip_state;
   sctx->b.set_stencil_ref = si_set_stencil_ref;

   sctx->b.set_framebuffer_state = si_set_framebuffer_state;

   sctx->b.set_sample_mask = si_set_sample_mask;

   sctx->b.create_vertex_elements_state = si_create_vertex_elements;
   sctx->b.bind_vertex_elements_state = si_bind_vertex_elements;
   sctx->b.delete_vertex_elements_state = si_delete_vertex_element;
   sctx->b.set_vertex_buffers = si_set_vertex_buffers;

   sctx->b.texture_barrier = si_texture_barrier;
   sctx->b.set_min_samples = si_set_min_samples;
   sctx->b.set_tess_state = si_set_tess_state;
   sctx->b.set_patch_vertices = si_set_patch_vertices;

   sctx->b.set_active_query_state = si_set_active_query_state;
}

void si_init_screen_state_functions(struct si_screen *sscreen)
{
   sscreen->b.is_format_supported = si_is_format_supported;

   if (sscreen->info.chip_class >= GFX10) {
      sscreen->make_texture_descriptor = gfx10_make_texture_descriptor;
   } else {
      sscreen->make_texture_descriptor = si_make_texture_descriptor;
   }
}

static void si_set_grbm_gfx_index(struct si_context *sctx, struct si_pm4_state *pm4, unsigned value)
{
   unsigned reg = sctx->chip_class >= GFX7 ? R_030800_GRBM_GFX_INDEX : R_00802C_GRBM_GFX_INDEX;
   si_pm4_set_reg(pm4, reg, value);
}

static void si_set_grbm_gfx_index_se(struct si_context *sctx, struct si_pm4_state *pm4, unsigned se)
{
   assert(se == ~0 || se < sctx->screen->info.max_se);
   si_set_grbm_gfx_index(sctx, pm4,
                         (se == ~0 ? S_030800_SE_BROADCAST_WRITES(1) : S_030800_SE_INDEX(se)) |
                            S_030800_SH_BROADCAST_WRITES(1) |
                            S_030800_INSTANCE_BROADCAST_WRITES(1));
}

static void si_write_harvested_raster_configs(struct si_context *sctx, struct si_pm4_state *pm4,
                                              unsigned raster_config, unsigned raster_config_1)
{
   unsigned num_se = MAX2(sctx->screen->info.max_se, 1);
   unsigned raster_config_se[4];
   unsigned se;

   ac_get_harvested_configs(&sctx->screen->info, raster_config, &raster_config_1, raster_config_se);

   for (se = 0; se < num_se; se++) {
      si_set_grbm_gfx_index_se(sctx, pm4, se);
      si_pm4_set_reg(pm4, R_028350_PA_SC_RASTER_CONFIG, raster_config_se[se]);
   }
   si_set_grbm_gfx_index(sctx, pm4, ~0);

   if (sctx->chip_class >= GFX7) {
      si_pm4_set_reg(pm4, R_028354_PA_SC_RASTER_CONFIG_1, raster_config_1);
   }
}

static void si_set_raster_config(struct si_context *sctx, struct si_pm4_state *pm4)
{
   struct si_screen *sscreen = sctx->screen;
   unsigned num_rb = MIN2(sscreen->info.max_render_backends, 16);
   unsigned rb_mask = sscreen->info.enabled_rb_mask;
   unsigned raster_config = sscreen->pa_sc_raster_config;
   unsigned raster_config_1 = sscreen->pa_sc_raster_config_1;

   if (!rb_mask || util_bitcount(rb_mask) >= num_rb) {
      /* Always use the default config when all backends are enabled
       * (or when we failed to determine the enabled backends).
       */
      si_pm4_set_reg(pm4, R_028350_PA_SC_RASTER_CONFIG, raster_config);
      if (sctx->chip_class >= GFX7)
         si_pm4_set_reg(pm4, R_028354_PA_SC_RASTER_CONFIG_1, raster_config_1);
   } else {
      si_write_harvested_raster_configs(sctx, pm4, raster_config, raster_config_1);
   }
}

void si_init_cs_preamble_state(struct si_context *sctx, bool uses_reg_shadowing)
{
   struct si_screen *sscreen = sctx->screen;
   uint64_t border_color_va = sctx->border_color_buffer->gpu_address;
   bool has_clear_state = sscreen->info.has_clear_state;
   struct si_pm4_state *pm4 = CALLOC_STRUCT(si_pm4_state);

   if (!pm4)
      return;

   if (!uses_reg_shadowing) {
      si_pm4_cmd_add(pm4, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
      si_pm4_cmd_add(pm4, CC0_UPDATE_LOAD_ENABLES(1));
      si_pm4_cmd_add(pm4, CC1_UPDATE_SHADOW_ENABLES(1));

      if (has_clear_state) {
         si_pm4_cmd_add(pm4, PKT3(PKT3_CLEAR_STATE, 0, 0));
         si_pm4_cmd_add(pm4, 0);
      }
   }

   /* CLEAR_STATE doesn't restore these correctly. */
   si_pm4_set_reg(pm4, R_028240_PA_SC_GENERIC_SCISSOR_TL, S_028240_WINDOW_OFFSET_DISABLE(1));
   si_pm4_set_reg(pm4, R_028244_PA_SC_GENERIC_SCISSOR_BR,
                  S_028244_BR_X(16384) | S_028244_BR_Y(16384));

   si_pm4_set_reg(pm4, R_028A18_VGT_HOS_MAX_TESS_LEVEL, fui(64));
   if (!has_clear_state)
      si_pm4_set_reg(pm4, R_028A1C_VGT_HOS_MIN_TESS_LEVEL, fui(0));

   if (!has_clear_state) {
      si_pm4_set_reg(pm4, R_028230_PA_SC_EDGERULE,
                     S_028230_ER_TRI(0xA) | S_028230_ER_POINT(0xA) | S_028230_ER_RECT(0xA) |
                        /* Required by DX10_DIAMOND_TEST_ENA: */
                        S_028230_ER_LINE_LR(0x1A) | S_028230_ER_LINE_RL(0x26) |
                        S_028230_ER_LINE_TB(0xA) | S_028230_ER_LINE_BT(0xA));
      si_pm4_set_reg(pm4, R_028820_PA_CL_NANINF_CNTL, 0);
      si_pm4_set_reg(pm4, R_028AC0_DB_SRESULTS_COMPARE_STATE0, 0x0);
      si_pm4_set_reg(pm4, R_028AC4_DB_SRESULTS_COMPARE_STATE1, 0x0);
      si_pm4_set_reg(pm4, R_028AC8_DB_PRELOAD_CONTROL, 0x0);
      si_pm4_set_reg(pm4, R_02800C_DB_RENDER_OVERRIDE, 0);
      si_pm4_set_reg(pm4, R_028A5C_VGT_GS_PER_VS, 0x2);
      si_pm4_set_reg(pm4, R_028A8C_VGT_PRIMITIVEID_RESET, 0x0);
      si_pm4_set_reg(pm4, R_028B98_VGT_STRMOUT_BUFFER_CONFIG, 0x0);
      si_pm4_set_reg(pm4, R_028AB8_VGT_VTX_CNT_EN, 0x0);
   }

   si_pm4_set_reg(pm4, R_028080_TA_BC_BASE_ADDR, border_color_va >> 8);
   if (sctx->chip_class >= GFX7)
      si_pm4_set_reg(pm4, R_028084_TA_BC_BASE_ADDR_HI, S_028084_ADDRESS(border_color_va >> 40));

   if (sctx->chip_class == GFX6) {
      si_pm4_set_reg(pm4, R_008A14_PA_CL_ENHANCE,
                     S_008A14_NUM_CLIP_SEQ(3) | S_008A14_CLIP_VTX_REORDER_ENA(1));
   }

   if (sctx->chip_class <= GFX7 || !has_clear_state) {
      si_pm4_set_reg(pm4, R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL, 14);
      si_pm4_set_reg(pm4, R_028C5C_VGT_OUT_DEALLOC_CNTL, 16);

      /* CLEAR_STATE doesn't clear these correctly on certain generations.
       * I don't know why. Deduced by trial and error.
       */
      si_pm4_set_reg(pm4, R_028B28_VGT_STRMOUT_DRAW_OPAQUE_OFFSET, 0);
      si_pm4_set_reg(pm4, R_028204_PA_SC_WINDOW_SCISSOR_TL, S_028204_WINDOW_OFFSET_DISABLE(1));
      si_pm4_set_reg(pm4, R_028030_PA_SC_SCREEN_SCISSOR_TL, 0);
      si_pm4_set_reg(pm4, R_028034_PA_SC_SCREEN_SCISSOR_BR,
                     S_028034_BR_X(16384) | S_028034_BR_Y(16384));
   }

   if (sctx->chip_class >= GFX10) {
      si_pm4_set_reg(pm4, R_028038_DB_DFSM_CONTROL,
                     S_028038_PUNCHOUT_MODE(V_028038_FORCE_OFF) |
                     S_028038_POPS_DRAIN_PS_ON_OVERLAP(1));
   }

   unsigned cu_mask_ps = 0xffffffff;

   /* It's wasteful to enable all CUs for PS if shader arrays have a different
    * number of CUs. The reason is that the hardware sends the same number of PS
    * waves to each shader array, so the slowest shader array limits the performance.
    * Disable the extra CUs for PS in other shader arrays to save power and thus
    * increase clocks for busy CUs. In the future, we might disable or enable this
    * tweak only for certain apps.
    */
   if (sctx->chip_class >= GFX10_3)
      cu_mask_ps = u_bit_consecutive(0, sscreen->info.min_good_cu_per_sa);

   if (sctx->chip_class >= GFX7) {
      si_pm4_set_reg(pm4, R_00B01C_SPI_SHADER_PGM_RSRC3_PS,
                     S_00B01C_CU_EN(cu_mask_ps) | S_00B01C_WAVE_LIMIT(0x3F));
   }

   if (sctx->chip_class <= GFX8) {
      si_set_raster_config(sctx, pm4);

      /* FIXME calculate these values somehow ??? */
      si_pm4_set_reg(pm4, R_028A54_VGT_GS_PER_ES, SI_GS_PER_ES);
      si_pm4_set_reg(pm4, R_028A58_VGT_ES_PER_GS, 0x40);

      /* These registers, when written, also overwrite the CLEAR_STATE
       * context, so we can't rely on CLEAR_STATE setting them.
       * It would be an issue if there was another UMD changing them.
       */
      si_pm4_set_reg(pm4, R_028400_VGT_MAX_VTX_INDX, ~0);
      si_pm4_set_reg(pm4, R_028404_VGT_MIN_VTX_INDX, 0);
      si_pm4_set_reg(pm4, R_028408_VGT_INDX_OFFSET, 0);
   }

   if (sscreen->info.chip_class >= GFX10) {
      si_pm4_set_reg(pm4, R_00B524_SPI_SHADER_PGM_HI_LS,
                     S_00B524_MEM_BASE(sscreen->info.address32_hi >> 8));
      si_pm4_set_reg(pm4, R_00B324_SPI_SHADER_PGM_HI_ES,
                     S_00B324_MEM_BASE(sscreen->info.address32_hi >> 8));
   } else if (sscreen->info.chip_class == GFX9) {
      si_pm4_set_reg(pm4, R_00B414_SPI_SHADER_PGM_HI_LS,
                     S_00B414_MEM_BASE(sscreen->info.address32_hi >> 8));
      si_pm4_set_reg(pm4, R_00B214_SPI_SHADER_PGM_HI_ES,
                     S_00B214_MEM_BASE(sscreen->info.address32_hi >> 8));
   } else {
      si_pm4_set_reg(pm4, R_00B524_SPI_SHADER_PGM_HI_LS,
                     S_00B524_MEM_BASE(sscreen->info.address32_hi >> 8));
   }

   if (sctx->chip_class >= GFX7 && sctx->chip_class <= GFX8) {
      si_pm4_set_reg(pm4, R_00B51C_SPI_SHADER_PGM_RSRC3_LS,
                     S_00B51C_CU_EN(0xffff) | S_00B51C_WAVE_LIMIT(0x3F));
      si_pm4_set_reg(pm4, R_00B41C_SPI_SHADER_PGM_RSRC3_HS, S_00B41C_WAVE_LIMIT(0x3F));
      si_pm4_set_reg(pm4, R_00B31C_SPI_SHADER_PGM_RSRC3_ES,
                     S_00B31C_CU_EN(0xffff) | S_00B31C_WAVE_LIMIT(0x3F));

      /* If this is 0, Bonaire can hang even if GS isn't being used.
       * Other chips are unaffected. These are suboptimal values,
       * but we don't use on-chip GS.
       */
      si_pm4_set_reg(pm4, R_028A44_VGT_GS_ONCHIP_CNTL,
                     S_028A44_ES_VERTS_PER_SUBGRP(64) | S_028A44_GS_PRIMS_PER_SUBGRP(4));
   }

   if (sctx->chip_class == GFX8) {
      unsigned vgt_tess_distribution;

      vgt_tess_distribution = S_028B50_ACCUM_ISOLINE(32) | S_028B50_ACCUM_TRI(11) |
                              S_028B50_ACCUM_QUAD(11) | S_028B50_DONUT_SPLIT_GFX81(16);

      /* Testing with Unigine Heaven extreme tesselation yielded best results
       * with TRAP_SPLIT = 3.
       */
      if (sctx->family == CHIP_FIJI || sctx->family >= CHIP_POLARIS10)
         vgt_tess_distribution |= S_028B50_TRAP_SPLIT(3);

      si_pm4_set_reg(pm4, R_028B50_VGT_TESS_DISTRIBUTION, vgt_tess_distribution);
   }

   if (sscreen->info.chip_class <= GFX9) {
      si_pm4_set_reg(pm4, R_028AA0_VGT_INSTANCE_STEP_RATE_0, 1);
   }

   if (sctx->chip_class == GFX9) {
      si_pm4_set_reg(pm4, R_030920_VGT_MAX_VTX_INDX, ~0);
      si_pm4_set_reg(pm4, R_030924_VGT_MIN_VTX_INDX, 0);
      si_pm4_set_reg(pm4, R_030928_VGT_INDX_OFFSET, 0);

      si_pm4_set_reg(pm4, R_028060_DB_DFSM_CONTROL,
                     S_028060_PUNCHOUT_MODE(V_028060_FORCE_OFF) |
                     S_028060_POPS_DRAIN_PS_ON_OVERLAP(1));
   }

   if (sctx->chip_class >= GFX9) {
      si_pm4_set_reg(pm4, R_00B41C_SPI_SHADER_PGM_RSRC3_HS,
                     S_00B41C_CU_EN(0xffff) | S_00B41C_WAVE_LIMIT(0x3F));

      si_pm4_set_reg(pm4, R_028B50_VGT_TESS_DISTRIBUTION,
                     S_028B50_ACCUM_ISOLINE(40) | S_028B50_ACCUM_TRI(30) | S_028B50_ACCUM_QUAD(24) |
                     S_028B50_DONUT_SPLIT_GFX9(24) | S_028B50_TRAP_SPLIT(6));
      si_pm4_set_reg(pm4, R_028C48_PA_SC_BINNER_CNTL_1,
                     S_028C48_MAX_ALLOC_COUNT(sscreen->info.pbb_max_alloc_count - 1) |
                     S_028C48_MAX_PRIM_PER_BATCH(1023));
      si_pm4_set_reg(pm4, R_028C4C_PA_SC_CONSERVATIVE_RASTERIZATION_CNTL,
                     S_028C4C_NULL_SQUAD_AA_MASK_ENABLE(1));

      si_pm4_set_reg(pm4, R_030968_VGT_INSTANCE_BASE_ID, 0);
      si_pm4_set_reg(pm4, R_0301EC_CP_COHER_START_DELAY,
                     sctx->chip_class >= GFX10 ? 0x20 : 0);
   }

   if (sctx->chip_class >= GFX10) {
      /* Logical CUs 16 - 31 */
      si_pm4_set_reg(pm4, R_00B004_SPI_SHADER_PGM_RSRC4_PS, S_00B004_CU_EN(cu_mask_ps >> 16));
      si_pm4_set_reg(pm4, R_00B104_SPI_SHADER_PGM_RSRC4_VS, S_00B104_CU_EN(0xffff));
      si_pm4_set_reg(pm4, R_00B404_SPI_SHADER_PGM_RSRC4_HS, S_00B404_CU_EN(0xffff));

      si_pm4_set_reg(pm4, R_00B0C8_SPI_SHADER_USER_ACCUM_PS_0, 0);
      si_pm4_set_reg(pm4, R_00B0CC_SPI_SHADER_USER_ACCUM_PS_1, 0);
      si_pm4_set_reg(pm4, R_00B0D0_SPI_SHADER_USER_ACCUM_PS_2, 0);
      si_pm4_set_reg(pm4, R_00B0D4_SPI_SHADER_USER_ACCUM_PS_3, 0);
      si_pm4_set_reg(pm4, R_00B1C8_SPI_SHADER_USER_ACCUM_VS_0, 0);
      si_pm4_set_reg(pm4, R_00B1CC_SPI_SHADER_USER_ACCUM_VS_1, 0);
      si_pm4_set_reg(pm4, R_00B1D0_SPI_SHADER_USER_ACCUM_VS_2, 0);
      si_pm4_set_reg(pm4, R_00B1D4_SPI_SHADER_USER_ACCUM_VS_3, 0);
      si_pm4_set_reg(pm4, R_00B2C8_SPI_SHADER_USER_ACCUM_ESGS_0, 0);
      si_pm4_set_reg(pm4, R_00B2CC_SPI_SHADER_USER_ACCUM_ESGS_1, 0);
      si_pm4_set_reg(pm4, R_00B2D0_SPI_SHADER_USER_ACCUM_ESGS_2, 0);
      si_pm4_set_reg(pm4, R_00B2D4_SPI_SHADER_USER_ACCUM_ESGS_3, 0);
      si_pm4_set_reg(pm4, R_00B4C8_SPI_SHADER_USER_ACCUM_LSHS_0, 0);
      si_pm4_set_reg(pm4, R_00B4CC_SPI_SHADER_USER_ACCUM_LSHS_1, 0);
      si_pm4_set_reg(pm4, R_00B4D0_SPI_SHADER_USER_ACCUM_LSHS_2, 0);
      si_pm4_set_reg(pm4, R_00B4D4_SPI_SHADER_USER_ACCUM_LSHS_3, 0);

      si_pm4_set_reg(pm4, R_00B0C0_SPI_SHADER_REQ_CTRL_PS,
                     S_00B0C0_SOFT_GROUPING_EN(1) |
                     S_00B0C0_NUMBER_OF_REQUESTS_PER_CU(4 - 1));
      si_pm4_set_reg(pm4, R_00B1C0_SPI_SHADER_REQ_CTRL_VS, 0);

      /* Enable CMASK/HTILE/DCC caching in L2 for small chips. */
      unsigned meta_write_policy, meta_read_policy;
      if (sscreen->info.max_render_backends <= 4) {
         meta_write_policy = V_02807C_CACHE_LRU_WR; /* cache writes */
         meta_read_policy = V_02807C_CACHE_LRU_RD;  /* cache reads */
      } else {
         meta_write_policy = V_02807C_CACHE_STREAM; /* write combine */
         meta_read_policy = V_02807C_CACHE_NOA;     /* don't cache reads */
      }

      si_pm4_set_reg(pm4, R_02807C_DB_RMI_L2_CACHE_CONTROL,
                     S_02807C_Z_WR_POLICY(V_02807C_CACHE_STREAM) |
                     S_02807C_S_WR_POLICY(V_02807C_CACHE_STREAM) |
                     S_02807C_HTILE_WR_POLICY(meta_write_policy) |
                     S_02807C_ZPCPSD_WR_POLICY(V_02807C_CACHE_STREAM) |
                     S_02807C_Z_RD_POLICY(V_02807C_CACHE_NOA) |
                     S_02807C_S_RD_POLICY(V_02807C_CACHE_NOA) |
                     S_02807C_HTILE_RD_POLICY(meta_read_policy));
      si_pm4_set_reg(pm4, R_028410_CB_RMI_GL2_CACHE_CONTROL,
                     S_028410_CMASK_WR_POLICY(meta_write_policy) |
                     S_028410_FMASK_WR_POLICY(V_028410_CACHE_STREAM) |
                     S_028410_DCC_WR_POLICY(meta_write_policy) |
                     S_028410_COLOR_WR_POLICY(V_028410_CACHE_STREAM) |
                     S_028410_CMASK_RD_POLICY(meta_read_policy) |
                     S_028410_FMASK_RD_POLICY(V_028410_CACHE_NOA) |
                     S_028410_DCC_RD_POLICY(meta_read_policy) |
                     S_028410_COLOR_RD_POLICY(V_028410_CACHE_NOA));

      si_pm4_set_reg(pm4, R_028428_CB_COVERAGE_OUT_CONTROL, 0);
      si_pm4_set_reg(pm4, R_028A98_VGT_DRAW_PAYLOAD_CNTL, 0);

      /* Break up a pixel wave if it contains deallocs for more than
       * half the parameter cache.
       *
       * To avoid a deadlock where pixel waves aren't launched
       * because they're waiting for more pixels while the frontend
       * is stuck waiting for PC space, the maximum allowed value is
       * the size of the PC minus the largest possible allocation for
       * a single primitive shader subgroup.
       */
      si_pm4_set_reg(pm4, R_028C50_PA_SC_NGG_MODE_CNTL, S_028C50_MAX_DEALLOCS_IN_WAVE(512));
      /* Reuse for legacy (non-NGG) only. */
      si_pm4_set_reg(pm4, R_028C58_VGT_VERTEX_REUSE_BLOCK_CNTL, 14);

      if (!has_clear_state) {
         si_pm4_set_reg(pm4, R_02835C_PA_SC_TILE_STEERING_OVERRIDE,
                        sscreen->info.pa_sc_tile_steering_override);
      }


      si_pm4_set_reg(pm4, R_030964_GE_MAX_VTX_INDX, ~0);
      si_pm4_set_reg(pm4, R_030924_GE_MIN_VTX_INDX, 0);
      si_pm4_set_reg(pm4, R_030928_GE_INDX_OFFSET, 0);
      si_pm4_set_reg(pm4, R_03097C_GE_STEREO_CNTL, 0);
      si_pm4_set_reg(pm4, R_030988_GE_USER_VGPR_EN, 0);
   }

   if (sctx->chip_class >= GFX10_3) {
      si_pm4_set_reg(pm4, R_028750_SX_PS_DOWNCONVERT_CONTROL, 0xff);
      /* The rate combiners have no effect if they are disabled like this:
       *   VERTEX_RATE:    BYPASS_VTX_RATE_COMBINER = 1
       *   PRIMITIVE_RATE: BYPASS_PRIM_RATE_COMBINER = 1
       *   HTILE_RATE:     VRS_HTILE_ENCODING = 0
       *   SAMPLE_ITER:    PS_ITER_SAMPLE = 0
       *
       * Use OVERRIDE, which will ignore results from previous combiners.
       * (e.g. enabled sample shading overrides the vertex rate)
       */
      si_pm4_set_reg(pm4, R_028848_PA_CL_VRS_CNTL,
                     S_028848_VERTEX_RATE_COMBINER_MODE(V_028848_VRS_COMB_MODE_OVERRIDE) |
                     S_028848_SAMPLE_ITER_COMBINER_MODE(V_028848_VRS_COMB_MODE_OVERRIDE));
   }

   sctx->cs_preamble_state = pm4;
}
