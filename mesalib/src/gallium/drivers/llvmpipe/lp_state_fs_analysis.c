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


#include "util/u_memory.h"
#include "util/u_math.h"
#include "tgsi/tgsi_parse.h"
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_util.h"
#include "tgsi/tgsi_dump.h"
#include "lp_debug.h"
#include "lp_state.h"
#include "nir.h"

/*
 * Detect Aero minification shaders.
 *
 * Aero does not use texture mimaps when a window gets animated and its shaped
 * bended. Instead it uses the average of 4 nearby texels. This is the simplest
 * of such shader, but there are several variations:
 *
 *   FRAG
 *   DCL IN[0], GENERIC[1], PERSPECTIVE
 *   DCL IN[1], GENERIC[2], PERSPECTIVE
 *   DCL IN[2], GENERIC[3], PERSPECTIVE
 *   DCL OUT[0], COLOR
 *   DCL SAMP[0]
 *   DCL TEMP[0..3]
 *   IMM FLT32 {     0.2500,     0.0000,     0.0000,     0.0000 }
 *   MOV TEMP[0].x, IN[0].zzzz
 *   MOV TEMP[0].y, IN[0].wwww
 *   MOV TEMP[1].x, IN[1].zzzz
 *   MOV TEMP[1].y, IN[1].wwww
 *   TEX TEMP[0], TEMP[0], SAMP[0], 2D
 *   TEX TEMP[2], IN[0], SAMP[0], 2D
 *   TEX TEMP[3], IN[1], SAMP[0], 2D
 *   TEX TEMP[1], TEMP[1], SAMP[0], 2D
 *   ADD TEMP[0], TEMP[0], TEMP[2]
 *   ADD TEMP[0], TEMP[3], TEMP[0]
 *   ADD TEMP[0], TEMP[1], TEMP[0]
 *   MUL TEMP[0], TEMP[0], IN[2]
 *   MUL TEMP[0], TEMP[0], IMM[0].xxxx
 *   MOV OUT[0], TEMP[0]
 *   END
 *
 * Texture coordinates are interleaved like the Gaussian blur shaders, but
 * unlike the later there isn't structure in the sub-pixel positioning of the
 * texels, other than being disposed in a diamond-like shape. For example,
 * these are the relative offsets of the texels relative to the average:
 *
 *    x offset   y offset
 *   --------------------
 *    0.691834   -0.21360
 *   -0.230230   -0.64160
 *   -0.692406    0.21356
 *    0.230802    0.64160
 *
 *  These shaders are typically used with linear min/mag filtering, but the
 *  linear filtering provides very little visual improvement compared to the
 *  performance impact it has. The ultimate purpose of detecting these shaders
 *  is to override with nearest texture filtering.
 */
static inline boolean
match_aero_minification_shader(const struct tgsi_token *tokens,
                               const struct lp_tgsi_info *info)
{
   struct tgsi_parse_context parse;
   unsigned coord_mask;
   boolean has_quarter_imm;
   unsigned index, chan;

   if ((info->base.opcode_count[TGSI_OPCODE_TEX] != 4 &&
        info->base.opcode_count[TGSI_OPCODE_SAMPLE] != 4) ||
       info->num_texs != 4) {
      return FALSE;
   }

   /*
    * Ensure the texture coordinates are interleaved as in the example above.
    */

   coord_mask = 0;
   for (index = 0; index < 4; ++index) {
      const struct lp_tgsi_texture_info *tex = &info->tex[index];
      if (tex->sampler_unit != 0 ||
          tex->texture_unit != 0 ||
          tex->coord[0].file != TGSI_FILE_INPUT ||
          tex->coord[1].file != TGSI_FILE_INPUT ||
          tex->coord[0].u.index != tex->coord[1].u.index ||
          (tex->coord[0].swizzle % 2) != 0 ||
          tex->coord[1].swizzle != tex->coord[0].swizzle + 1) {
         return FALSE;
      }

      coord_mask |= 1 << (tex->coord[0].u.index*2 + tex->coord[0].swizzle/2);
   }
   if (coord_mask != 0xf) {
      return FALSE;
   }

   /*
    * Ensure it has the 0.25 immediate.
    */

   has_quarter_imm = FALSE;

   tgsi_parse_init(&parse, tokens);

   while (!tgsi_parse_end_of_tokens(&parse)) {
      tgsi_parse_token(&parse);

      switch (parse.FullToken.Token.Type) {
      case TGSI_TOKEN_TYPE_DECLARATION:
         break;

      case TGSI_TOKEN_TYPE_INSTRUCTION:
         goto finished;

      case TGSI_TOKEN_TYPE_IMMEDIATE:
         {
            const unsigned size =
                  parse.FullToken.FullImmediate.Immediate.NrTokens - 1;
            assert(size <= 4);
            for (chan = 0; chan < size; ++chan) {
               if (parse.FullToken.FullImmediate.u[chan].Float == 0.25f) {
                  has_quarter_imm = TRUE;
                  goto finished;
               }
            }
         }
         break;

      case TGSI_TOKEN_TYPE_PROPERTY:
         break;

      default:
         assert(0);
         goto finished;
      }
   }
finished:

   tgsi_parse_free(&parse);

   if (!has_quarter_imm) {
      return FALSE;
   }

   return TRUE;
}


/*
 * Determine whether the given alu src comes directly from an input
 * register.  If so, return true and the input register index and
 * component.  Return false otherwise.
 */
static bool
get_nir_input_info(const nir_alu_src *src,
                   unsigned *input_index,
                   int *input_component)
{
   if (!src->src.is_ssa) {
      return false;
   }

   // The parent instr should be a nir_intrinsic_load_deref.
   const nir_instr *parent = src->src.ssa[0].parent_instr;
   if (!parent || parent->type != nir_instr_type_intrinsic) {
      return false;
   }
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(parent);
   if (!intrin ||
       intrin->intrinsic != nir_intrinsic_load_deref ||
       !intrin->src[0].is_ssa) {
      return false;
   }

   // The parent of the load should be a type_deref.
   parent = intrin->src->ssa->parent_instr;
   if (!parent || parent->type != nir_instr_type_deref) {
      return false;
   }

   // The var being deref'd should be a shader input register.
   nir_deref_instr *deref = nir_instr_as_deref(parent);
   if (!deref || deref->deref_type != nir_deref_type_var ||
       deref->modes != nir_var_shader_in) {
      return false;
   }

   /*
    * If the texture coordinate input is declared as two variables like this:
    * decl_var shader_in INTERP_MODE_NONE float coord (VARYING_SLOT_VAR0.x, 0, 0)
    * decl_var shader_in INTERP_MODE_NONE float coord@0 (VARYING_SLOT_VAR0.y, 0, 0)
    * Then deref->var->data.location_frac will be 0 for the first var and 1
    * for the second var and the texcoord will be set up with:
    *   vec2 32 ssa_5 = vec2 ssa_2, ssa_4  (note: no swizzles)
    *
    * Alternately, if the texture coordinate input is declared as one
    * variable like this:
    * decl_var shader_in INTERP_MODE_NONE vec4 i1xyzw (VARYING_SLOT_VAR1.xyzw, 0, 0)
    * then deref->var->data.location_frac will be 0 and the
    * tex coord will be setup with:
    *   vec2 32 ssa_2 = vec2 ssa_1.x, ssa_1.y
    *
    * We can handle both cases by adding deref->var->data.location_frac and
    * src->swizzle[0].
    */
   *input_index = deref->var->data.driver_location;
   *input_component = deref->var->data.location_frac + src->swizzle[0];
   assert(*input_component >= 0);
   assert(*input_component <= 3);

   return true;
}


/*
 * Examine the texcoord argument to a texture instruction to determine
 * if the texcoord comes directly from a fragment shader input.  If so
 * return true and return the FS input register index for the coordinate
 * and the (2-component) swizzle.  Return false otherwise.
 */
static bool
get_texcoord_provenance(const nir_tex_src *texcoord,
                        unsigned *coord_fs_input_index, // out
                        int swizzle[4]) // out
{
   assert(texcoord->src_type == nir_tex_src_coord);

   // The parent instr of the coord should be an nir_op_vec2 alu op
   const nir_instr *parent = texcoord->src.ssa->parent_instr;
   if (!parent || parent->type != nir_instr_type_alu) {
      return false;
   }
   const nir_alu_instr *alu = nir_instr_as_alu(parent);
   if (!alu || alu->op != nir_op_vec2) {
      return false;
   }

   // Loop over nir_op_vec2 instruction arguments to find the
   // input register index and component.
   unsigned input_reg_indexes[2];
   for (unsigned comp = 0; comp < 2; comp++) {
      if (!get_nir_input_info(&alu->src[comp],
                              &input_reg_indexes[comp], &swizzle[comp])) {
         return false;
      }
   }

   // Both texcoord components should come from the same input register.
   if (input_reg_indexes[0] != input_reg_indexes[1]) {
      return false;
   }

   *coord_fs_input_index = input_reg_indexes[0];

   return true;
}


/*
 * Check if all the values of a nir_load_const_instr are 32-bit
 * floats in the range [0,1].  If so, return true, else return false.
 */
static bool
check_load_const_in_zero_one(const nir_load_const_instr *load)
{
   if (load->def.bit_size != 32)
      return false;
   for (unsigned c = 0; c < load->def.num_components; c++) {
      float val = load->value[c].f32;
      if (val < 0.0 || val > 1.0 || isnan(val)) {
         return false;
      }
   }
   return true;
}


/*
 * Examine the NIR shader to determine if it's "linear".
 */
static bool
llvmpipe_nir_fn_is_linear_compat(const struct nir_shader *shader,
                                 nir_function_impl *impl,
                                 struct lp_tgsi_info *info)
{
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         switch (instr->type) {
         case nir_instr_type_deref:
            break;
         case nir_instr_type_load_const: {
            nir_load_const_instr *load = nir_instr_as_load_const(instr);
            if (!check_load_const_in_zero_one(load)) {
               return false;
            }
            break;
         }
         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_deref &&
                intrin->intrinsic != nir_intrinsic_store_deref &&
                intrin->intrinsic != nir_intrinsic_load_ubo)
               return false;

            if (intrin->intrinsic == nir_intrinsic_load_ubo) {
               if (!nir_src_is_const(intrin->src[0]))
                  return false;
               nir_load_const_instr *load =
                  nir_instr_as_load_const(intrin->src[0].ssa->parent_instr);
               if (load->value[0].u32 != 0)
                  return false;
            }
            break;
         }
         case nir_instr_type_tex: {
            nir_tex_instr *tex = nir_instr_as_tex(instr);
            struct lp_tgsi_texture_info *tex_info = &info->tex[info->num_texs];
            int texcoord_swizzle[4] = {-1, -1, -1, -1};
            unsigned coord_fs_input_index = 0;

            for (unsigned i = 0; i < tex->num_srcs; i++) {
               if (tex->src[i].src_type == nir_tex_src_coord) {
                  if (!get_texcoord_provenance(&tex->src[i],
                                               &coord_fs_input_index,
                                               texcoord_swizzle)) {
                     //debug nir_print_shader((nir_shader *) shader, stdout);
                     return false;
                  }
               }
            }

            switch (tex->op) {
            case nir_texop_tex:
               tex_info->modifier = LP_BLD_TEX_MODIFIER_NONE;
               break;
            default:
               /* inaccurate but sufficient. */
               tex_info->modifier = LP_BLD_TEX_MODIFIER_EXPLICIT_LOD;
               return false;
            }
            switch (tex->sampler_dim) {
            case GLSL_SAMPLER_DIM_2D:
               tex_info->target = TGSI_TEXTURE_2D;
               break;
            default:
               /* inaccurate but sufficient. */
               tex_info->target = TGSI_TEXTURE_1D;
               return false;
            }

            tex_info->sampler_unit = tex->sampler_index;
            tex_info->texture_unit = tex->texture_index;

            /* this is enforced in the scanner previously. */
            tex_info->coord[0].file = TGSI_FILE_INPUT;  // S
            tex_info->coord[1].file = TGSI_FILE_INPUT;  // T
            assert(texcoord_swizzle[0] >= 0);
            assert(texcoord_swizzle[1] >= 0);
            tex_info->coord[0].swizzle = texcoord_swizzle[0]; // S
            tex_info->coord[1].swizzle = texcoord_swizzle[1]; // T
            tex_info->coord[0].u.index = coord_fs_input_index;
            tex_info->coord[1].u.index = coord_fs_input_index;

            info->num_texs++;
            break;
         }
         case nir_instr_type_alu: {
            const nir_alu_instr *alu = nir_instr_as_alu(instr);
            switch (alu->op) {
            case nir_op_mov:
            case nir_op_vec2:
            case nir_op_vec4:
               // these instructions are OK
               break;
            case nir_op_fmul: {
               unsigned num_src = nir_op_infos[alu->op].num_inputs;;
               for (unsigned s = 0; s < num_src; s++) {
                  /* If the MUL uses immediate values, the values must
                   * be 32-bit floats in the range [0,1].
                   */
                  if (nir_src_is_const(alu->src[s].src)) {
                     nir_load_const_instr *load =
                        nir_instr_as_load_const(alu->src[s].src.ssa->parent_instr);
                     if (!check_load_const_in_zero_one(load)) {
                        return false;
                     }
                  }
               }
               break;
            }
            default:
               // disallowed instruction
               return false;
            }
            break;
         }
         default:
            return false;
         }
      }
   }
   return true;
}


static bool
llvmpipe_nir_is_linear_compat(struct nir_shader *shader,
                              struct lp_tgsi_info *info)
{
   nir_foreach_function(function, shader) {
      if (function->impl) {
         if (!llvmpipe_nir_fn_is_linear_compat(shader, function->impl, info))
            return false;
      }
   }
   return true;
}


/*
 * Analyze the given NIR fragment shader and set its shader->kind field
 * to LP_FS_KIND_x.
 */
void
llvmpipe_fs_analyse_nir(struct lp_fragment_shader *shader)
{
   if (shader->info.base.num_inputs <= LP_MAX_LINEAR_INPUTS &&
       shader->info.base.num_outputs == 1 &&
       !shader->info.indirect_textures &&
       !shader->info.sampler_texture_units_different &&
       shader->info.num_texs <= LP_MAX_LINEAR_TEXTURES &&
       llvmpipe_nir_is_linear_compat(shader->base.ir.nir, &shader->info)) {
      shader->kind = LP_FS_KIND_LLVM_LINEAR;
   } else {
      shader->kind = LP_FS_KIND_GENERAL;
   }
}


/*
 * Analyze the given TGSI fragment shader and set its shader->kind field
 * to LP_FS_KIND_x.
 */
void
llvmpipe_fs_analyse(struct lp_fragment_shader *shader,
                    const struct tgsi_token *tokens)
{
   if (shader->info.base.num_inputs <= LP_MAX_LINEAR_INPUTS &&
       shader->info.base.num_outputs == 1 &&
       !shader->info.indirect_textures &&
       !shader->info.sampler_texture_units_different &&
       !shader->info.unclamped_immediates &&
       shader->info.num_texs <= LP_MAX_LINEAR_TEXTURES &&
       (shader->info.base.opcode_count[TGSI_OPCODE_TEX] +
        shader->info.base.opcode_count[TGSI_OPCODE_SAMPLE] +
        shader->info.base.opcode_count[TGSI_OPCODE_MOV] +
        shader->info.base.opcode_count[TGSI_OPCODE_MUL] +
        shader->info.base.opcode_count[TGSI_OPCODE_RET] +
        shader->info.base.opcode_count[TGSI_OPCODE_END] ==
        shader->info.base.num_instructions)) {
      shader->kind = LP_FS_KIND_LLVM_LINEAR;
   } else {
      shader->kind = LP_FS_KIND_GENERAL;
   }

   if (shader->kind == LP_FS_KIND_GENERAL &&
       match_aero_minification_shader(tokens, &shader->info)) {
      shader->kind = LP_FS_KIND_AERO_MINIFICATION;
   }
}
