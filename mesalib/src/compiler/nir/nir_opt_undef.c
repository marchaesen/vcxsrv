/*
 * Copyright © 2015 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <math.h>
#include "util/mesa-blake3.h"
#include "nir.h"
#include "nir_builder.h"

/** @file nir_opt_undef.c
 *
 * Handles optimization of operations involving ssa_undef.
 */

struct undef_options {
   bool disallow_undef_to_nan;
};

/**
 * Turn conditional selects between an undef and some other value into a move
 * of that other value (on the assumption that the condition's going to be
 * choosing the defined value).  This reduces work after if flattening when
 * each side of the if is defining a variable.
 */
static bool
opt_undef_csel(nir_builder *b, nir_alu_instr *instr)
{
   if (!nir_op_is_selection(instr->op))
      return false;

   for (int i = 1; i <= 2; i++) {
      if (!nir_src_is_undef(instr->src[i].src))
         continue;

      b->cursor = nir_instr_remove(&instr->instr);
      nir_def *mov = nir_mov_alu(b, instr->src[i == 1 ? 2 : 1],
                                 instr->def.num_components);
      nir_def_rewrite_uses(&instr->def, mov);

      return true;
   }

   return false;
}

static bool
op_is_mov_or_vec_or_pack_or_unpack(nir_op op)
{
   switch (op) {
   case nir_op_pack_32_2x16:
   case nir_op_pack_32_2x16_split:
   case nir_op_pack_32_4x8:
   case nir_op_pack_32_4x8_split:
   case nir_op_pack_64_2x32:
   case nir_op_pack_64_2x32_split:
   case nir_op_pack_64_4x16:
   case nir_op_unpack_32_2x16:
   case nir_op_unpack_32_2x16_split_x:
   case nir_op_unpack_32_2x16_split_y:
   case nir_op_unpack_32_4x8:
   case nir_op_unpack_64_2x32:
   case nir_op_unpack_64_2x32_split_x:
   case nir_op_unpack_64_2x32_split_y:
   case nir_op_unpack_64_4x16:
      return true;
   default:
      return nir_op_is_vec_or_mov(op);
   }
}

/**
 * Replace vecN(undef, undef, ...) with a single undef.
 */
static bool
opt_undef_vecN(nir_builder *b, nir_alu_instr *alu)
{
   if (!op_is_mov_or_vec_or_pack_or_unpack(alu->op))
      return false;

   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
      if (!nir_src_is_undef(alu->src[i].src))
         return false;
   }

   b->cursor = nir_before_instr(&alu->instr);
   nir_def *undef = nir_undef(b, alu->def.num_components,
                              alu->def.bit_size);
   nir_def_replace(&alu->def, undef);

   return true;
}

static uint32_t
nir_get_undef_mask(nir_def *def)
{
   nir_instr *instr = def->parent_instr;

   if (instr->type == nir_instr_type_undef)
      return BITSET_MASK(def->num_components);

   if (instr->type != nir_instr_type_alu)
      return 0;

   nir_alu_instr *alu = nir_instr_as_alu(instr);
   unsigned undef = 0;

   /* nir_op_mov of undef is handled by opt_undef_vecN() */
   if (nir_op_is_vec(alu->op)) {
      for (int i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
         if (nir_src_is_undef(alu->src[i].src)) {
            undef |= BITSET_MASK(nir_ssa_alu_instr_src_components(alu, i)) << i;
         }
      }
   }

   return undef;
}

/**
 * Remove any store intrinsic writemask channels whose value is undefined (the
 * existing value is a fine representation of "undefined").
 */
static bool
opt_undef_store(nir_intrinsic_instr *intrin)
{
   int arg_index;
   switch (intrin->intrinsic) {
   case nir_intrinsic_store_deref:
      arg_index = 1;
      break;
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_per_view_output:
   case nir_intrinsic_store_per_primitive_output:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_scratch:
      arg_index = 0;
      break;
   default:
      return false;
   }

   nir_def *def = intrin->src[arg_index].ssa;

   unsigned write_mask = nir_intrinsic_write_mask(intrin);
   unsigned undef_mask = nir_get_undef_mask(def);

   if (!(write_mask & undef_mask))
      return false;

   write_mask &= ~undef_mask;
   if (!write_mask)
      nir_instr_remove(&intrin->instr);
   else
      nir_intrinsic_set_write_mask(intrin, write_mask);

   return true;
}

struct visit_info {
   bool replace_undef_with_constant;
   bool prefer_nan;
   bool must_keep_undef;
};

/**
 * Analyze an undef use to see if replacing undef with a constant is
 * beneficial.
 */
static void
visit_undef_use(nir_src *src, struct visit_info *info)
{
   if (nir_src_is_if(src)) {
      /* If the use is "if", keep undef because the branch will be eliminated
       * by nir_opt_dead_cf.
       */
      info->must_keep_undef = true;
      return;
   }

   nir_instr *instr = nir_src_parent_instr(src);

   if (instr->type == nir_instr_type_alu) {
      /* Replacing undef with a constant is only beneficial with ALU
       * instructions because it can eliminate them or simplify them.
       */
      nir_alu_instr *alu = nir_instr_as_alu(instr);

      /* opt_undef_vecN already copy propagated. */
      if (op_is_mov_or_vec_or_pack_or_unpack(alu->op)) {
         info->must_keep_undef = true;
         return;
      }

      unsigned num_srcs = nir_op_infos[alu->op].num_inputs;

      for (unsigned i = 0; i < num_srcs; i++) {
         if (&alu->src[i].src != src)
            continue;

         info->replace_undef_with_constant = true;
         if (nir_op_infos[alu->op].input_types[i] & nir_type_float &&
             alu->op != nir_op_fmulz &&
             (alu->op != nir_op_ffmaz || i == 2))
            info->prefer_nan = true;
      }
   } else {
      /* If the use is not ALU, don't replace undef. We need to preserve
       * undef for stores and phis because those are handled differently,
       * and replacing undef with a constant would result in worse code.
       */
      info->must_keep_undef = true;
      return;
   }
}

/**
 * Replace ssa_undef used by ALU opcodes with 0 or NaN, whichever eliminates
 * more code.
 *
 * Replace it with NaN if an FP opcode uses undef, which will cause the opcode
 * to be eliminated by nir_opt_algebraic. 0 would not eliminate the FP opcode.
 */
static bool
replace_ssa_undef(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_undef)
      return false;

   const struct undef_options *options = data;

   nir_undef_instr *undef = nir_instr_as_undef(instr);
   struct visit_info info = { 0 };

   nir_foreach_use_including_if(src, &undef->def) {
      visit_undef_use(src, &info);
   }

   if (info.must_keep_undef || !info.replace_undef_with_constant)
      return false;

   b->cursor = nir_before_instr(&undef->instr);
   nir_def *replacement;

   /* If undef is used as float, replace it with NaN, which will
    * eliminate all FP instructions that consume it. Else, replace it
    * with 0, which is more likely to eliminate non-FP instructions.
    */
   if (info.prefer_nan && !options->disallow_undef_to_nan)
      replacement = nir_imm_floatN_t(b, NAN, undef->def.bit_size);
   else
      replacement = nir_imm_intN_t(b, 0, undef->def.bit_size);

   if (undef->def.num_components > 1)
      replacement = nir_replicate(b, replacement, undef->def.num_components);

   nir_def_replace(&undef->def, replacement);
   return true;
}

static bool
opt_undef_uses(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      return opt_undef_csel(b, alu) ||
             opt_undef_vecN(b, alu);
   } else if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      return opt_undef_store(intrin);
   }

   return false;
}

bool
nir_opt_undef(nir_shader *shader)
{
   struct undef_options options = { 0 };

   /* Disallow the undef->NaN transformation only for those shaders where
    * it's known to break rendering. These are shader source BLAKE3s printed by
    * nir_print_shader().
    */
   uint32_t shader_blake3s[][BLAKE3_OUT_LEN32] = {
      /* gputest/gimark */
      { 0x582c214b, 0x25478275, 0xc9a835d2, 0x95c9b643, 0x69deae47, 0x213c7427, 0xa9da66a5, 0xac254ed2 },

      /* Viewperf13/CATIA_car_01 */
      { 0x880dfa0f, 0x60e32201, 0xe3a89f59, 0xb1cc6f07, 0xcdbebe66, 0x20122aec, 0x83450d4e, 0x8f42843d }, /* Taillights */
      { 0x624e53bb, 0x8eb635ba, 0xb1e4ed9b, 0x651b0fec, 0x86fcf79a, 0xde0863fb, 0x09ce80c1, 0xd972e40f }, /* Grill */
      { 0x01a8db39, 0xfa175175, 0x621f7302, 0xfcde9177, 0x72d873bf, 0x048d38c1, 0xe669d2de, 0xaa6584af }, /* Headlights */
      { 0x32029770, 0xab295b41, 0x3f1daf07, 0x9dd9153e, 0xd598be73, 0xe555b2f3, 0x6e087eaf, 0x084d329c }, /* Rims */

      /* Viewperf13/CATIA_car_04 */
      { 0x55207b90, 0x08fa2f8f, 0x9db62464, 0xadba6570, 0xb6d5d962, 0xf434bff5, 0x46a34d64, 0x021bfb45 }, /* Headlights */
      { 0x83fbdd6a, 0x231b027e, 0x6f142248, 0x2b3045de, 0xd2a4f460, 0x59dfb8d8, 0x6dbc00f9, 0xcca13143 }, /* Rims */
      { 0x88ed3a0a, 0xf128d384, 0x8161fdac, 0xd10cb257, 0x5e63db2d, 0x56798b6f, 0x881e81ee, 0xa4e937d4 }, /* Tires */
      { 0xbf84697c, 0x3bc75bb6, 0x9d012175, 0x2dd90bcf, 0x0562f0ed, 0x5aa80e62, 0xb5793ae3, 0x9127bcab }, /* Windows */
      { 0x47a3eb4b, 0x136f676d, 0x94045ed3, 0x57b00972, 0x8cda7550, 0x88327fda, 0x37f7cf37, 0x66db05e3 }, /* Body */
   };

   for (unsigned i = 0; i < ARRAY_SIZE(shader_blake3s); i++) {
      if (_mesa_printed_blake3_equal(shader->info.source_blake3, shader_blake3s[i])) {
         options.disallow_undef_to_nan = true;
         break;
      }
   }

   if (shader->info.use_legacy_math_rules)
      options.disallow_undef_to_nan = true;

   bool progress = nir_shader_instructions_pass(shader,
                                                opt_undef_uses,
                                                nir_metadata_control_flow,
                                                &options);
   progress |= nir_shader_instructions_pass(shader,
                                            replace_ssa_undef,
                                            nir_metadata_control_flow,
                                            &options);

   return progress;
}
