/*
 * Copyright © 2021 Google, Inc.
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
 */

#include "ir3_nir.h"

/*
 * Lowering for 64b intrinsics generated with OpenCL or with
 * VK_KHR_buffer_device_address. All our intrinsics from a hw
 * standpoint are 32b, so we just need to combine in zero for
 * the upper 32bits and let the other nir passes clean up the mess.
 */

static bool
lower_64b_intrinsics_filter(const nir_instr *instr, const void *unused)
{
   (void)unused;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   if (intr->intrinsic == nir_intrinsic_load_deref ||
       intr->intrinsic == nir_intrinsic_store_deref)
      return false;

   if (is_intrinsic_store(intr->intrinsic))
      return nir_src_bit_size(intr->src[0]) == 64;

   if (nir_intrinsic_dest_components(intr) == 0)
      return false;

   return nir_dest_bit_size(intr->dest) == 64;
}

static nir_ssa_def *
lower_64b_intrinsics(nir_builder *b, nir_instr *instr, void *unused)
{
   (void)unused;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   /* We could be *slightly* more clever and, for ex, turn a 64b vec4
    * load into two 32b vec4 loads, rather than 4 32b vec2 loads.
    */

   if (is_intrinsic_store(intr->intrinsic)) {
      unsigned offset_src_idx;
      switch (intr->intrinsic) {
      case nir_intrinsic_store_ssbo:
      case nir_intrinsic_store_global_ir3:
         offset_src_idx = 2;
         break;
      default:
         offset_src_idx = 1;
      }

      unsigned num_comp = nir_intrinsic_src_components(intr, 0);
      unsigned wrmask = nir_intrinsic_has_write_mask(intr) ?
         nir_intrinsic_write_mask(intr) : BITSET_MASK(num_comp);
      nir_ssa_def *val = nir_ssa_for_src(b, intr->src[0], num_comp);
      nir_ssa_def *off = nir_ssa_for_src(b, intr->src[offset_src_idx], 1);

      for (unsigned i = 0; i < num_comp; i++) {
         if (!(wrmask & BITFIELD_BIT(i)))
            continue;

         nir_ssa_def *c64 = nir_channel(b, val, i);
         nir_ssa_def *c32 = nir_unpack_64_2x32(b, c64);

         nir_intrinsic_instr *store =
            nir_instr_as_intrinsic(nir_instr_clone(b->shader, &intr->instr));
         store->num_components = 2;
         store->src[0] = nir_src_for_ssa(c32);
         store->src[offset_src_idx] = nir_src_for_ssa(off);

         if (nir_intrinsic_has_write_mask(intr))
            nir_intrinsic_set_write_mask(store, 0x3);
         nir_builder_instr_insert(b, &store->instr);

         off = nir_iadd(b, off, nir_imm_intN_t(b, 8, off->bit_size));
      }

      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   }

   unsigned num_comp = nir_intrinsic_dest_components(intr);

   nir_ssa_def *def = &intr->dest.ssa;
   def->bit_size = 32;

   /* load_kernel_input is handled specially, lowering to two 32b inputs:
    */
   if (intr->intrinsic == nir_intrinsic_load_kernel_input) {
      assert(num_comp == 1);

      nir_ssa_def *offset = nir_iadd(b,
            nir_ssa_for_src(b, intr->src[0], 1),
            nir_imm_int(b, 4));

      nir_ssa_def *upper = nir_build_load_kernel_input(
            b, 1, 32, offset);

      return nir_pack_64_2x32_split(b, def, upper);
   }

   nir_ssa_def *components[num_comp];

   if (is_intrinsic_load(intr->intrinsic)) {
      unsigned offset_src_idx;
      switch(intr->intrinsic) {
      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_global_ir3:
         offset_src_idx = 1;
         break;
      default:
         offset_src_idx = 0;
      }

      nir_ssa_def *off = nir_ssa_for_src(b, intr->src[offset_src_idx], 1);

      for (unsigned i = 0; i < num_comp; i++) {
         nir_intrinsic_instr *load =
            nir_instr_as_intrinsic(nir_instr_clone(b->shader, &intr->instr));
         load->num_components = 2;
         load->src[offset_src_idx] = nir_src_for_ssa(off);

         nir_ssa_dest_init(&load->instr, &load->dest, 2, 32, NULL);
         nir_builder_instr_insert(b, &load->instr);

         components[i] = nir_pack_64_2x32(b, &load->dest.ssa);

         off = nir_iadd(b, off, nir_imm_intN_t(b, 8, off->bit_size));
      }
   } else {
      /* The remaining (non load/store) intrinsics just get zero-
       * extended from 32b to 64b:
       */
      for (unsigned i = 0; i < num_comp; i++) {
         nir_ssa_def *c = nir_channel(b, def, i);
         components[i] = nir_pack_64_2x32_split(b, c, nir_imm_zero(b, 1, 32));
      }
   }

   return nir_build_alu_src_arr(b, nir_op_vec(num_comp), components);
}

bool
ir3_nir_lower_64b_intrinsics(nir_shader *shader)
{
   return nir_shader_lower_instructions(
         shader, lower_64b_intrinsics_filter,
         lower_64b_intrinsics, NULL);
}

/*
 * Lowering for 64b undef instructions, splitting into a two 32b undefs
 */

static nir_ssa_def *
lower_64b_undef(nir_builder *b, nir_instr *instr, void *unused)
{
   (void)unused;

   nir_ssa_undef_instr *undef = nir_instr_as_ssa_undef(instr);
   unsigned num_comp = undef->def.num_components;
   nir_ssa_def *components[num_comp];

   for (unsigned i = 0; i < num_comp; i++) {
      nir_ssa_def *lowered = nir_ssa_undef(b, 2, 32);

      components[i] = nir_pack_64_2x32_split(b,
                                             nir_channel(b, lowered, 0),
                                             nir_channel(b, lowered, 1));
   }

   return nir_build_alu_src_arr(b, nir_op_vec(num_comp), components);
}

static bool
lower_64b_undef_filter(const nir_instr *instr, const void *unused)
{
   (void)unused;

   return instr->type == nir_instr_type_ssa_undef &&
      nir_instr_as_ssa_undef(instr)->def.bit_size == 64;
}

bool
ir3_nir_lower_64b_undef(nir_shader *shader)
{
   return nir_shader_lower_instructions(
         shader, lower_64b_undef_filter,
         lower_64b_undef, NULL);
}

/*
 * Lowering for load_global/store_global with 64b addresses to ir3
 * variants, which instead take a uvec2_32
 */

static bool
lower_64b_global_filter(const nir_instr *instr, const void *unused)
{
   (void)unused;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   switch (intr->intrinsic) {
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
   case nir_intrinsic_store_global:
   case nir_intrinsic_global_atomic_add:
   case nir_intrinsic_global_atomic_imin:
   case nir_intrinsic_global_atomic_umin:
   case nir_intrinsic_global_atomic_imax:
   case nir_intrinsic_global_atomic_umax:
   case nir_intrinsic_global_atomic_and:
   case nir_intrinsic_global_atomic_or:
   case nir_intrinsic_global_atomic_xor:
   case nir_intrinsic_global_atomic_exchange:
   case nir_intrinsic_global_atomic_comp_swap:
      return true;
   default:
      return false;
   }
}

static nir_ssa_def *
lower_64b_global(nir_builder *b, nir_instr *instr, void *unused)
{
   (void)unused;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   bool load = intr->intrinsic != nir_intrinsic_store_global;

   nir_ssa_def *addr64 = nir_ssa_for_src(b, intr->src[load ? 0 : 1], 1);
   nir_ssa_def *addr = nir_unpack_64_2x32(b, addr64);

   /*
    * Note that we can get vec8/vec16 with OpenCL.. we need to split
    * those up into max 4 components per load/store.
    */

#define GLOBAL_IR3_2SRC(name)                                                 \
   case nir_intrinsic_##name: {                                               \
      return nir_build_##name##_ir3(b, nir_dest_bit_size(intr->dest), addr,   \
                                  nir_ssa_for_src(b, intr->src[1], 1));       \
   }

   switch (intr->intrinsic) {
   GLOBAL_IR3_2SRC(global_atomic_add)
   GLOBAL_IR3_2SRC(global_atomic_imin)
   GLOBAL_IR3_2SRC(global_atomic_umin)
   GLOBAL_IR3_2SRC(global_atomic_imax)
   GLOBAL_IR3_2SRC(global_atomic_umax)
   GLOBAL_IR3_2SRC(global_atomic_and)
   GLOBAL_IR3_2SRC(global_atomic_or)
   GLOBAL_IR3_2SRC(global_atomic_xor)
   GLOBAL_IR3_2SRC(global_atomic_exchange)
   case nir_intrinsic_global_atomic_comp_swap:
      return nir_build_global_atomic_comp_swap_ir3(
         b, nir_dest_bit_size(intr->dest), addr,
         nir_ssa_for_src(b, intr->src[1], 1),
         nir_ssa_for_src(b, intr->src[2], 1));
   default:
      break;
   }
#undef GLOBAL_IR3_2SRC

   if (load) {
      unsigned num_comp = nir_intrinsic_dest_components(intr);
      nir_ssa_def *components[num_comp];
      for (unsigned off = 0; off < num_comp;) {
         unsigned c = MIN2(num_comp - off, 4);
         nir_ssa_def *val = nir_build_load_global_ir3(
               b, c, nir_dest_bit_size(intr->dest),
               addr, nir_imm_int(b, off));
         for (unsigned i = 0; i < c; i++) {
            components[off++] = nir_channel(b, val, i);
         }
      }
      return nir_build_alu_src_arr(b, nir_op_vec(num_comp), components);
   } else {
      unsigned num_comp = nir_intrinsic_src_components(intr, 0);
      nir_ssa_def *value = nir_ssa_for_src(b, intr->src[0], num_comp);
      for (unsigned off = 0; off < num_comp; off += 4) {
         unsigned c = MIN2(num_comp - off, 4);
         nir_ssa_def *v = nir_channels(b, value, BITFIELD_MASK(c) << off);
         nir_build_store_global_ir3(b, v, addr, nir_imm_int(b, off));
      }
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;
   }
}

bool
ir3_nir_lower_64b_global(nir_shader *shader)
{
   return nir_shader_lower_instructions(
         shader, lower_64b_global_filter,
         lower_64b_global, NULL);
}
