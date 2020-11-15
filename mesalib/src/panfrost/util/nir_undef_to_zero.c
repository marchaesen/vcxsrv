/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 *
 * Authors (Collabora):
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

/**
 * @file
 *
 * Flushes undefined SSA values to a zero vector fo the appropriate component
 * count, to avoid undefined behaviour in the resulting shader. Not required
 * for conformance as use of uninitialized variables is explicitly left
 * undefined by the spec.  Works around buggy apps, however.
 *
 * Call immediately after nir_opt_undef. If called before, larger optimization
 * opportunities from the former pass will be missed. If called outside of an
 * optimization loop, constant propagation and algebraic optimizations won't be
 * able to kick in to reduce stuff consuming the zero.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

bool nir_undef_to_zero(nir_shader *shader);

bool
nir_undef_to_zero(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (!function->impl) continue;

      nir_builder b;
      nir_builder_init(&b, function->impl);

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_ssa_undef) continue;

            nir_ssa_undef_instr *und = nir_instr_as_ssa_undef(instr);

            /* Get the required size */
            unsigned c = und->def.num_components;
            unsigned s = und->def.bit_size;

            nir_const_value v[NIR_MAX_VEC_COMPONENTS];
            memset(v, 0, sizeof(v));

            b.cursor = nir_before_instr(instr);
            nir_ssa_def *zero = nir_build_imm(&b, c, s, v);
            nir_src zerosrc = nir_src_for_ssa(zero);

            nir_ssa_def_rewrite_uses(&und->def, zerosrc);

            progress |= true;
         }
      }

      nir_metadata_preserve(function->impl, nir_metadata_block_index | nir_metadata_dominance);

   }

   return progress;
}


