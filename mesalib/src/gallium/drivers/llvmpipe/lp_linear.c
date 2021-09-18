/**************************************************************************
 *
 * Copyright 2010-2021 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 **************************************************************************/


#include "pipe/p_config.h"

#include "util/u_math.h"
#include "util/u_cpu_detect.h"
#include "util/u_pack_color.h"
#include "util/u_rect.h"
#include "util/u_sse.h"

#include "lp_jit.h"
#include "lp_rast.h"
#include "lp_debug.h"
#include "lp_state_fs.h"
#include "lp_linear_priv.h"


#if defined(PIPE_ARCH_SSE)


/* For debugging (LP_DEBUG=linear), shade areas of run-time fallback
 * purple.  Keep blending active so we can see more of what's going
 * on.
 */
static boolean
linear_fallback(const struct lp_rast_state *state,
                unsigned x, unsigned y,
                unsigned width, unsigned height,
                uint8_t *color,
                unsigned stride)
{
   unsigned col = 0x808000ff;
   int i;

   for (y = 0; y < height; y++) {
      for (i = 0; i < 64; i++) {
         *((uint32_t *)(color + y*stride) + x + i) = col;
      }
   }

   return TRUE;
}


/* Run our configurable linear shader pipeline:
 */
static boolean
lp_fs_linear_run(const struct lp_rast_state *state,
                 unsigned x, unsigned y,
                 unsigned width, unsigned height,
                 const float (*a0)[4],
                 const float (*dadx)[4],
                 const float (*dady)[4],
                 uint8_t *color,
                 unsigned stride)
{
   const struct lp_fragment_shader_variant *variant = state->variant;
   const struct lp_tgsi_info *info = &variant->shader->info;
   struct lp_jit_linear_context jit;
   lp_jit_linear_llvm_func jit_func = variant->jit_linear_llvm;

   struct lp_linear_sampler samp[LP_MAX_LINEAR_TEXTURES];
   struct lp_linear_interp interp[LP_MAX_LINEAR_INPUTS];
   uint8_t constants[LP_MAX_LINEAR_CONSTANTS][4];

   const float w0 = a0[0][3];
   float oow = 1.0f/w0;

   unsigned input_mask = variant->linear_input_mask;
   int nr_consts = info->base.file_max[TGSI_FILE_CONSTANT]+1;
   int nr_tex = info->num_texs;
   int i, j;

   LP_DBG(DEBUG_RAST, "%s\n", __FUNCTION__);

   /* Require constant w in these rectangles:
    */
   if (dadx[0][3] != 0.0f ||
       dady[0][3] != 0.0f) {
      if (LP_DEBUG & DEBUG_LINEAR2)
         debug_printf("  -- w not constant\n");
      goto fail;
   }

   /* XXX: Per statechange:
    */
   for (i = 0; i < nr_consts; i++) {
      for (j = 0; j < 4; j++) {
         float val = state->jit_context.constants[0][i*4+j];
         if (val < 0.0f || val > 1.0f) {
            if (LP_DEBUG & DEBUG_LINEAR2)
               debug_printf("  -- const[%d] out of range\n", i);
            goto fail;
         }
         constants[i][j] = (uint8_t)(val * 255.0f);
      }
   }
   jit.constants = (const uint8_t (*)[4])constants;

   /* We assume BGRA ordering */
   assert(variant->key.cbuf_format[0] == PIPE_FORMAT_B8G8R8X8_UNORM ||
          variant->key.cbuf_format[0] == PIPE_FORMAT_B8G8R8A8_UNORM);

   jit.blend_color =
         state->jit_context.u8_blend_color[32] +
         (state->jit_context.u8_blend_color[16] << 8) +
         (state->jit_context.u8_blend_color[0] << 16) +
         (state->jit_context.u8_blend_color[48] << 24);

   jit.alpha_ref_value = float_to_ubyte(state->jit_context.alpha_ref_value);

   /* XXX: Per primitive:
    */
   while (input_mask) {
      int i = u_bit_scan(&input_mask);
      unsigned usage_mask = info->base.input_usage_mask[i];
      boolean perspective =
            info->base.input_interpolate[i] == TGSI_INTERPOLATE_PERSPECTIVE ||
            (info->base.input_interpolate[i] == TGSI_INTERPOLATE_COLOR &&
             !variant->key.flatshade);

      if (!lp_linear_init_interp(&interp[i],
                                 x, y, width, height,
                                 usage_mask,
                                 perspective,
                                 oow,
                                 a0[i+1],
                                 dadx[i+1],
                                 dady[i+1])) {
         if (LP_DEBUG & DEBUG_LINEAR2)
            debug_printf("  -- init_interp(%d) failed\n", i);
         goto fail;
      }

      jit.inputs[i] = &interp[i].base;
   }


   /* XXX: Per primitive: Initialize linear or nearest samplers:
    */
   for (i = 0; i < nr_tex; i++) {
      const struct lp_tgsi_texture_info *tex_info = &info->tex[i];
      unsigned unit = tex_info->sampler_unit;

      /* XXX: some texture coordinates are linear!
       */
      //boolean perspective = (info->base.input_interpolate[i] ==
      //                       TGSI_INTERPOLATE_PERSPECTIVE);

      if (!lp_linear_init_sampler(&samp[i],
                                  tex_info,
                                  &variant->key.samplers[unit],
                                  &state->jit_context.textures[unit],
                                  x, y, width, height,
                                  a0, dadx, dady)) {
         if (LP_DEBUG & DEBUG_LINEAR2)
            debug_printf("  -- init_sampler(%d) failed\n", i);
         goto fail;
      }

      jit.tex[i] = &samp[i].base;
   }

   /* JIT function already does blending */
   jit.color0 = color + x * 4 + y * stride;
   for (y = 0; y < height; y++) {
      jit_func(&jit, 0, 0, width);
      jit.color0 += stride;
   }

   return TRUE;

fail:
   /* Visually distinguish this from other fallbacks:
    */
   if (LP_DEBUG & DEBUG_LINEAR) {
      return linear_fallback(state, x, y, width, height, color, stride);
   }

   return FALSE;
}


static void
check_linear_interp_mask_a(struct lp_fragment_shader_variant *variant)
{
   const struct lp_tgsi_info *info = &variant->shader->info;
   struct lp_jit_linear_context jit;

   struct lp_linear_sampler samp[LP_MAX_LINEAR_TEXTURES];
   struct lp_linear_interp interp[LP_MAX_LINEAR_INPUTS];
   uint8_t constants[LP_MAX_LINEAR_CONSTANTS][4];
   PIPE_ALIGN_VAR(16) uint8_t color0[TILE_SIZE*4];

   int nr_inputs = info->base.file_max[TGSI_FILE_INPUT]+1;
   int nr_tex = info->num_texs;
   int i;

   LP_DBG(DEBUG_RAST, "%s\n", __FUNCTION__);

   jit.constants = (const uint8_t (*)[4])constants;

   for (i = 0; i < nr_tex; i++) {
      lp_linear_init_noop_sampler(&samp[i]);
      jit.tex[i] = &samp[i].base;
   }

   for (i = 0; i < nr_inputs; i++) {
      lp_linear_init_noop_interp(&interp[i]);
      jit.inputs[i] = &interp[i].base;
   }

   jit.color0 = color0;

   (void)variant->jit_linear_llvm(&jit, 0, 0, 0);

   /* Find out which interpolators were called, and store this as a
    * mask:
    */
   for (i = 0; i < nr_inputs; i++)
      variant->linear_input_mask |= (interp[i].row[0] << i);
}


/* Until the above is working, look at texture information and guess
 * that any input used as a texture coordinate is not used for
 * anything else.
 */
static void
check_linear_interp_mask_b(struct lp_fragment_shader_variant *variant)
{
   const struct lp_tgsi_info *info = &variant->shader->info;
   int nr_inputs = info->base.file_max[TGSI_FILE_INPUT]+1;
   int nr_tex = info->num_texs;
   unsigned tex_mask = 0;
   int i;

   LP_DBG(DEBUG_RAST, "%s\n", __FUNCTION__);

   for (i = 0; i < nr_tex; i++) {
      const struct lp_tgsi_texture_info *tex_info = &info->tex[i];
      const struct lp_tgsi_channel_info *schan = &tex_info->coord[0];
      const struct lp_tgsi_channel_info *tchan = &tex_info->coord[1];
      tex_mask |= 1 << schan->u.index;
      tex_mask |= 1 << tchan->u.index;
   }

   variant->linear_input_mask = ((1 << nr_inputs) - 1) & ~tex_mask;
}


void
lp_linear_check_variant(struct lp_fragment_shader_variant *variant)
{
   const struct lp_fragment_shader_variant_key *key = &variant->key;
   const struct lp_fragment_shader *shader = variant->shader;
   const struct lp_tgsi_info *info = &shader->info;
   int i;

   if (info->base.file_max[TGSI_FILE_CONSTANT] >= LP_MAX_LINEAR_CONSTANTS ||
       info->base.file_max[TGSI_FILE_INPUT] >= LP_MAX_LINEAR_INPUTS) {
      if (LP_DEBUG & DEBUG_LINEAR)
         debug_printf("  -- too many inputs/constants\n");
      goto fail;
   }

   /* If we have a fastpath which implements the entire varient, use
    * that.
    */
   if (lp_linear_check_fastpath(variant)) {
      return;
   }

   /* Otherwise, can we build up a spanline-based linear path for this
    * variant?
    */

   /* Check static sampler state.
    */
   for (i = 0; i < info->num_texs; i++) {
      const struct lp_tgsi_texture_info *tex_info = &info->tex[i];
      unsigned unit = tex_info->sampler_unit;

      /* XXX: Relax this once setup premultiplies by oow:
       */
      if (info->base.input_interpolate[unit] != TGSI_INTERPOLATE_PERSPECTIVE) {
         if (LP_DEBUG & DEBUG_LINEAR)
            debug_printf(" -- samp[%d]: texcoord not perspective\n", i);
         goto fail;
      }

      if (!lp_linear_check_sampler(&key->samplers[unit], tex_info)) {
         if (LP_DEBUG & DEBUG_LINEAR)
            debug_printf(" -- samp[%d]: check_sampler failed\n", i);
         goto fail;
      }
   }

   /* Check shader.  May not have been jitted.
    */
   if (variant->linear_function == NULL) {
      if (LP_DEBUG & DEBUG_LINEAR)
         debug_printf("  -- no linear shader\n");
      goto fail;
   }

   /* Hook in the catchall shader runner:
    */
   variant->jit_linear = lp_fs_linear_run;

   /* Figure out which inputs we don't need to interpolate (because
    * they are only used as texture coordinates).  This is important
    * as we can cope with texture coordinates which exceed 1.0, but
    * cannot do so for regular inputs.
    */
   if (1)
      check_linear_interp_mask_a(variant);
   else
      check_linear_interp_mask_b(variant);


   if (0) {
      lp_debug_fs_variant(variant);
      debug_printf("linear input mask: 0x%x\n", variant->linear_input_mask);
   }

   return;

fail:
   if (LP_DEBUG & DEBUG_LINEAR) {
      lp_debug_fs_variant(variant);
      debug_printf("    ----> no linear path for this variant\n");
   }
}


#else
void
lp_linear_check_variant(struct lp_fragment_shader_variant *variant)
{
}
#endif
