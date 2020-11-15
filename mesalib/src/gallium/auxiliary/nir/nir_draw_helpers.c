/**************************************************************************
 *
 * Copyright 2019 Red Hat.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **************************************************************************/

/*
 * NIR lowering passes to handle the draw stages for
 * - pstipple
 * - aaline
 * - aapoint.
 *
 * These are all ported from the equivalent TGSI transforms.
 */

#include "nir.h"
#include "tgsi/tgsi_from_mesa.h"
#include "nir_builder.h"

#include "nir_draw_helpers.h"

typedef struct {
   nir_builder b;
   nir_shader *shader;
   bool fs_pos_is_sysval;
   nir_variable *stip_tex;
   nir_ssa_def *fragcoord;
} lower_pstipple;

static nir_ssa_def *
load_frag_coord(nir_builder *b)
{
   nir_foreach_shader_in_variable(var, b->shader) {
      if (var->data.location == VARYING_SLOT_POS)
         return nir_load_var(b, var);
   }

   nir_variable *pos = nir_variable_create(b->shader, nir_var_shader_in,
                                           glsl_vec4_type(), NULL);
   pos->data.location = VARYING_SLOT_POS;
   pos->data.interpolation = INTERP_MODE_NOPERSPECTIVE;
   pos->data.driver_location = b->shader->num_inputs++;
   return nir_load_var(b, pos);
}

static void
nir_lower_pstipple_block(nir_block *block,
                         lower_pstipple *state)
{
   nir_builder *b = &state->b;
   nir_ssa_def *texcoord;

   b->cursor = nir_before_block(block);

   nir_ssa_def *frag_coord = state->fs_pos_is_sysval ? nir_load_frag_coord(b) : load_frag_coord(b);

   texcoord = nir_fmul(b, nir_channels(b, frag_coord, 0x3),
                       nir_imm_vec2(b, 1.0/32.0, 1.0/32.0));

   nir_tex_instr *tex = nir_tex_instr_create(b->shader, 1);
   tex->op = nir_texop_tex;
   tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   tex->coord_components = 2;
   tex->dest_type = nir_type_float;
   tex->texture_index = state->stip_tex->data.binding;
   tex->sampler_index = state->stip_tex->data.binding;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(texcoord);
   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, NULL);

   nir_builder_instr_insert(b, &tex->instr);

   nir_ssa_def *condition = nir_f2b32(b, nir_channel(b, &tex->dest.ssa, 3));
   nir_intrinsic_instr *discard = nir_intrinsic_instr_create(b->shader, nir_intrinsic_discard_if);
   discard->src[0] = nir_src_for_ssa(condition);
   nir_builder_instr_insert(b, &discard->instr);
   b->shader->info.fs.uses_discard = true;
}

static void
nir_lower_pstipple_impl(nir_function_impl *impl,
                        lower_pstipple *state)
{
   nir_builder *b = &state->b;

   nir_builder_init(b, impl);

   nir_block *start = nir_start_block(impl);
   nir_lower_pstipple_block(start, state);
}

void
nir_lower_pstipple_fs(struct nir_shader *shader,
                      unsigned *samplerUnitOut,
                      unsigned fixedUnit,
                      bool fs_pos_is_sysval)
{
   lower_pstipple state = {
      .shader = shader,
      .fs_pos_is_sysval = fs_pos_is_sysval,
   };
   if (shader->info.stage != MESA_SHADER_FRAGMENT)
      return;

   int binding = 0;
   nir_foreach_uniform_variable(var, shader) {
      if (glsl_type_is_sampler(var->type)) {
         if (var->data.binding >= binding)
            binding = var->data.binding + 1;
      }
   }
   const struct glsl_type *sampler2D =
      glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, false, GLSL_TYPE_FLOAT);

   nir_variable *tex_var = nir_variable_create(shader, nir_var_uniform, sampler2D, "stipple_tex");
   tex_var->data.binding = binding;
   tex_var->data.explicit_binding = true;
   tex_var->data.how_declared = nir_var_hidden;

   shader->info.textures_used |= (1 << binding);
   state.stip_tex = tex_var;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_lower_pstipple_impl(function->impl, &state);
      }
   }
   *samplerUnitOut = binding;
}

typedef struct {
   nir_builder b;
   nir_shader *shader;
   nir_variable *line_width_input;
} lower_aaline;

static void
nir_lower_aaline_block(nir_block *block,
                       lower_aaline *state)
{
  nir_builder *b = &state->b;
  nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic != nir_intrinsic_store_deref)
         continue;

      nir_variable *var = nir_intrinsic_get_var(intrin, 0);
      if (var->data.mode != nir_var_shader_out)
         continue;
      if (var->data.location != FRAG_RESULT_COLOR)
         continue;

      nir_ssa_def *out_input = intrin->src[1].ssa;
      b->cursor = nir_before_instr(instr);
      nir_ssa_def *lw = nir_load_var(b, state->line_width_input);
      nir_ssa_def *tmp = nir_fsat(b, nir_fadd(b, nir_channel(b, lw, 1),
                                              nir_fneg(b, nir_fabs(b, nir_channel(b, lw, 0)))));
      nir_ssa_def *tmp1 = nir_fsat(b, nir_fadd(b, nir_channel(b, lw, 3),
                                               nir_fneg(b, nir_fabs(b, nir_channel(b, lw, 2)))));

      tmp = nir_fmul(b, tmp, tmp1);
      tmp = nir_fmul(b, nir_channel(b, out_input, 3), tmp);

      nir_ssa_def *out = nir_vec4(b, nir_channel(b, out_input, 0),
                                  nir_channel(b, out_input, 1),
                                  nir_channel(b, out_input, 2),
                                  tmp);
      nir_instr_rewrite_src(instr, &intrin->src[1], nir_src_for_ssa(out));
   }

}

static void
nir_lower_aaline_impl(nir_function_impl *impl,
                      lower_aaline *state)
{
   nir_builder *b = &state->b;

   nir_builder_init(b, impl);

   nir_foreach_block(block, impl) {
      nir_lower_aaline_block(block, state);
   }
}

void
nir_lower_aaline_fs(struct nir_shader *shader, int *varying)
{
   lower_aaline state = {
      .shader = shader,
   };
   if (shader->info.stage != MESA_SHADER_FRAGMENT)
      return;

   int highest_location = -1, highest_drv_location = -1;
   nir_foreach_shader_in_variable(var, shader) {
     if ((int)var->data.location > highest_location)
         highest_location = var->data.location;
     if ((int)var->data.driver_location > highest_drv_location)
         highest_drv_location = var->data.driver_location;
   }

   nir_variable *line_width = nir_variable_create(shader, nir_var_shader_in,
                                                  glsl_vec4_type(), "aaline");
   if (highest_location == -1 || highest_location < VARYING_SLOT_VAR0) {
     line_width->data.location = VARYING_SLOT_VAR0;
     line_width->data.driver_location = highest_drv_location + 1;
   } else {
     line_width->data.location = highest_location + 1;
     line_width->data.driver_location = highest_drv_location + 1;
   }
   shader->num_inputs++;
   *varying = tgsi_get_generic_gl_varying_index(line_width->data.location, true);
   state.line_width_input = line_width;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_lower_aaline_impl(function->impl, &state);
      }
   }
}

typedef struct {
   nir_builder b;
   nir_shader *shader;
   nir_variable *input;
} lower_aapoint;

static void
nir_lower_aapoint_block(nir_block *block,
                        lower_aapoint *state, nir_ssa_def *sel)
{
  nir_builder *b = &state->b;
  nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic != nir_intrinsic_store_deref)
         continue;

      nir_variable *var = nir_intrinsic_get_var(intrin, 0);
      if (var->data.mode != nir_var_shader_out)
         continue;
      if (var->data.location != FRAG_RESULT_COLOR)
         continue;

      nir_ssa_def *out_input = intrin->src[1].ssa;
      b->cursor = nir_before_instr(instr);

      nir_ssa_def *tmp = nir_fmul(b, nir_channel(b, out_input, 3), sel);
      nir_ssa_def *out = nir_vec4(b, nir_channel(b, out_input, 0),
                                  nir_channel(b, out_input, 1),
                                  nir_channel(b, out_input, 2),
                                  tmp);
      nir_instr_rewrite_src(instr, &intrin->src[1], nir_src_for_ssa(out));
   }

}

static void
nir_lower_aapoint_impl(nir_function_impl *impl,
                      lower_aapoint *state)
{
   nir_builder *b = &state->b;

   nir_builder_init(b, impl);

   nir_block *block = nir_start_block(impl);
   b->cursor = nir_before_block(block);

   nir_ssa_def *aainput = nir_load_var(b, state->input);

   nir_ssa_def *dist = nir_fadd(b, nir_fmul(b, nir_channel(b, aainput, 0), nir_channel(b, aainput, 0)),
                                nir_fmul(b, nir_channel(b, aainput, 1), nir_channel(b, aainput, 1)));

   nir_ssa_def *k = nir_channel(b, aainput, 2);
   nir_ssa_def *chan_val_one = nir_channel(b, aainput, 3);
   nir_ssa_def *comp = nir_flt32(b, chan_val_one, dist);

   nir_intrinsic_instr *discard = nir_intrinsic_instr_create(b->shader, nir_intrinsic_discard_if);
   discard->src[0] = nir_src_for_ssa(comp);
   nir_builder_instr_insert(b, &discard->instr);
   b->shader->info.fs.uses_discard = true;

   /* compute coverage factor = (1-d)/(1-k) */
   /* 1 - k */
   nir_ssa_def *tmp = nir_fadd(b, chan_val_one, nir_fneg(b, k));
   /* 1.0 / (1 - k) */
   tmp = nir_frcp(b, tmp);

   /* 1 - d */
   nir_ssa_def *tmp2 = nir_fadd(b, chan_val_one, nir_fneg(b, dist));

   /* (1 - d) / (1 - k) */
   nir_ssa_def *coverage = nir_fmul(b, tmp, tmp2);

   /* if (k >= distance)
    *    sel = coverage;
    * else
    *    sel = 1.0;
    */
   nir_ssa_def *sel = nir_b32csel(b, nir_fge32(b, k, dist), coverage, chan_val_one);

   nir_foreach_block(block, impl) {
     nir_lower_aapoint_block(block, state, sel);
   }
}

void
nir_lower_aapoint_fs(struct nir_shader *shader, int *varying)
{
   lower_aapoint state = {
      .shader = shader,
   };
   if (shader->info.stage != MESA_SHADER_FRAGMENT)
      return;

   int highest_location = -1, highest_drv_location = -1;
   nir_foreach_shader_in_variable(var, shader) {
     if ((int)var->data.location > highest_location)
         highest_location = var->data.location;
     if ((int)var->data.driver_location > highest_drv_location)
         highest_drv_location = var->data.driver_location;
   }

   nir_variable *aapoint_input = nir_variable_create(shader, nir_var_shader_in,
                                                     glsl_vec4_type(), "aapoint");
   if (highest_location == -1 || highest_location < VARYING_SLOT_VAR0) {
     aapoint_input->data.location = VARYING_SLOT_VAR0;
   } else {
     aapoint_input->data.location = highest_location + 1;
   }
   aapoint_input->data.driver_location = highest_drv_location + 1;

   shader->num_inputs++;
   *varying = tgsi_get_generic_gl_varying_index(aapoint_input->data.location, true);
   state.input = aapoint_input;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_lower_aapoint_impl(function->impl, &state);
      }
   }
}
