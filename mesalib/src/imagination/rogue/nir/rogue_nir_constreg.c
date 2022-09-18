/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_search_helpers.h"
#include "rogue_constreg.h"
#include "rogue_nir.h"

/* TODO: optimize: if value is in const regs, replace, else, use shared regs and
 * notify driver they need to be populated?
 */

/* Replaces multiple ssa uses from load_const with a single use -> a register.
 */
void rogue_nir_constreg(nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_builder b;

   nir_builder_init(&b, impl);

   /* Find load_const instructions. */
   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_load_const)
            continue;

         nir_load_const_instr *load_const = nir_instr_as_load_const(instr);

         /* Skip values that can be pulled from constant registers. */
         uint32_t value = nir_const_value_as_uint(load_const->value[0], 32);
         size_t const_reg = rogue_constreg_lookup(value);
         if (const_reg != ROGUE_NO_CONST_REG)
            continue;

         b.cursor = nir_after_instr(&load_const->instr);
         nir_ssa_def *mov = nir_mov(&b, &load_const->def);

         nir_foreach_use_safe (use_src, &load_const->def) {
            if (use_src->parent_instr == mov->parent_instr)
               continue;

            /* Skip when used as an index for intrinsics, as we want to
             * access that value directly.
             */
            if (use_src->parent_instr->type == nir_instr_type_intrinsic)
               continue;

            nir_instr_rewrite_src_ssa(use_src->parent_instr, use_src, mov);
         }
      }
   }
}
