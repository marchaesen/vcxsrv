/*
 * Copyright © 2024 Igalia
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

#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"
#include "nir_intrinsics.h"

/*
 * Convert atomic arithmetic to regular arithmetic along with cmpxchg
 * by repeating the operation until the result is expected.
 *
 * eg:
 * atomicAdd(a[0], 1) ->
 *
 * uint expected = a[0];
 * while (true) {
 *    uint before = expected;
 *    expected += 1;
 *    uint original = atomicCompareExchange(a[0], before, expected);
 *    if (original == before) {break;}
 *    expected = original;
 * }
 */

static nir_def *
build_atomic(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_def *load;
   switch (intr->intrinsic) {
   case nir_intrinsic_ssbo_atomic:
      load = nir_load_ssbo(b, 1, intr->def.bit_size, intr->src[0].ssa,
                           intr->src[1].ssa,
                           .align_mul = intr->def.bit_size / 8,
                           .align_offset = 0);
      break;
   case nir_intrinsic_shared_atomic:
      load = nir_load_shared(b, 1, intr->def.bit_size,
                             intr->src[0].ssa,
                             .align_mul = intr->def.bit_size / 8,
                             .align_offset = 0);
      break;
   case nir_intrinsic_global_atomic:
      load = nir_load_global(b, intr->src[0].ssa,
                             intr->def.bit_size / 8,
                             1, intr->def.bit_size);
      break;
   default:
      unreachable("unsupported atomic type");
   }

   nir_def *data = intr->intrinsic == nir_intrinsic_ssbo_atomic ? intr->src[2].ssa : intr->src[1].ssa;
   nir_loop *loop = nir_push_loop(b);
   nir_def *xchg;
   {
      nir_phi_instr *phi = nir_phi_instr_create(b->shader);
      nir_def_init(&phi->instr, &phi->def, 1, intr->def.bit_size);
      nir_phi_instr_add_src(phi, load->parent_instr->block, load);
      nir_def *before = &phi->def;
      nir_def *expected = nir_build_alu2(
         b, nir_atomic_op_to_alu(nir_intrinsic_atomic_op(intr)), before, data);
      nir_alu_instr *op = nir_instr_as_alu(expected->parent_instr);
      op->exact = true;
      op->fp_fast_math = 0;
      switch (intr->intrinsic) {
      case nir_intrinsic_ssbo_atomic:
         xchg = nir_ssbo_atomic_swap(b, intr->def.bit_size,
                                     intr->src[0].ssa,
                                     intr->src[1].ssa,
                                     before, expected,
                                     .atomic_op = nir_atomic_op_cmpxchg);
         break;
      case nir_intrinsic_shared_atomic:
         xchg = nir_shared_atomic_swap(b, intr->def.bit_size,
                                       intr->src[0].ssa,
                                       before, expected,
                                       .atomic_op = nir_atomic_op_cmpxchg);
         break;
      case nir_intrinsic_global_atomic:
         xchg = nir_global_atomic_swap(b, intr->def.bit_size,
                                       intr->src[0].ssa,
                                       before, expected,
                                       .atomic_op = nir_atomic_op_cmpxchg);
         break;
      default:
         unreachable("unsupported atomic type");
      }
      nir_break_if(b, nir_ieq(b, xchg, before));
      nir_phi_instr_add_src(phi, nir_loop_last_block(loop), xchg);
      b->cursor = nir_before_block(nir_loop_first_block(loop));
      nir_builder_instr_insert(b, &phi->instr);
   }
   nir_pop_loop(b, loop);
   return xchg;
}

static bool
lower_atomics(struct nir_builder *b, nir_intrinsic_instr *intr,
              void *supported)
{
   nir_instr_filter_cb supported_cb = supported;

   if (intr->intrinsic != nir_intrinsic_ssbo_atomic &&
       intr->intrinsic != nir_intrinsic_shared_atomic &&
       intr->intrinsic != nir_intrinsic_global_atomic)
      return false;
   if (supported_cb(&intr->instr, NULL))
      return false;
   b->cursor = nir_before_instr(&intr->instr);
   switch (nir_intrinsic_atomic_op(intr)) {
   case nir_atomic_op_imin:
   case nir_atomic_op_umin:
   case nir_atomic_op_imax:
   case nir_atomic_op_umax:
   case nir_atomic_op_iand:
   case nir_atomic_op_ior:
   case nir_atomic_op_ixor:
   case nir_atomic_op_fadd:
   case nir_atomic_op_fmin:
   case nir_atomic_op_fmax:
   case nir_atomic_op_iadd: {
      nir_def_replace(&intr->def, build_atomic(b, intr));
      return true;
   }
   case nir_atomic_op_cmpxchg:
   case nir_atomic_op_xchg:
      return false;
   case nir_atomic_op_fcmpxchg: /* unimplemented */
   default:
      unreachable("Invalid nir_atomic_op");
   }
}

bool
nir_lower_atomics(nir_shader *shader, nir_instr_filter_cb supported)
{
   return nir_shader_intrinsics_pass(shader, lower_atomics,
                                     nir_metadata_none, supported);
}
