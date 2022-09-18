/*
 * Copyright © 2016 Bas Nieuwenhuizen
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

#include "ac_nir.h"
#include "nir_builder.h"

nir_ssa_def *
ac_nir_load_arg(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg)
{
   unsigned num_components = ac_args->args[arg.arg_index].size;

   if (ac_args->args[arg.arg_index].file == AC_ARG_SGPR)
      return nir_load_scalar_arg_amd(b, num_components, .base = arg.arg_index);
   else
      return nir_load_vector_arg_amd(b, num_components, .base = arg.arg_index);
}

/**
 * This function takes an I/O intrinsic like load/store_input,
 * and emits a sequence that calculates the full offset of that instruction,
 * including a stride to the base and component offsets.
 */
nir_ssa_def *
ac_nir_calc_io_offset(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      nir_ssa_def *base_stride,
                      unsigned component_stride,
                      ac_nir_map_io_driver_location map_io)
{
   unsigned base = nir_intrinsic_base(intrin);
   unsigned semantic = nir_intrinsic_io_semantics(intrin).location;
   unsigned mapped_driver_location = map_io ? map_io(semantic) : base;

   /* base is the driver_location, which is in slots (1 slot = 4x4 bytes) */
   nir_ssa_def *base_op = nir_imul_imm(b, base_stride, mapped_driver_location);

   /* offset should be interpreted in relation to the base,
    * so the instruction effectively reads/writes another input/output
    * when it has an offset
    */
   nir_ssa_def *offset_op = nir_imul(b, base_stride, nir_ssa_for_src(b, *nir_get_io_offset_src(intrin), 1));

   /* component is in bytes */
   unsigned const_op = nir_intrinsic_component(intrin) * component_stride;

   return nir_iadd_imm_nuw(b, nir_iadd_nuw(b, base_op, offset_op), const_op);
}

bool
ac_nir_lower_indirect_derefs(nir_shader *shader,
                             enum amd_gfx_level gfx_level)
{
   bool progress = false;

   /* Lower large variables to scratch first so that we won't bloat the
    * shader by generating large if ladders for them. We later lower
    * scratch to alloca's, assuming LLVM won't generate VGPR indexing.
    */
   NIR_PASS(progress, shader, nir_lower_vars_to_scratch, nir_var_function_temp, 256,
            glsl_get_natural_size_align_bytes);

   /* LLVM doesn't support VGPR indexing on GFX9. */
   bool llvm_has_working_vgpr_indexing = gfx_level != GFX9;

   /* TODO: Indirect indexing of GS inputs is unimplemented.
    *
    * TCS and TES load inputs directly from LDS or offchip memory, so
    * indirect indexing is trivial.
    */
   nir_variable_mode indirect_mask = 0;
   if (shader->info.stage == MESA_SHADER_GEOMETRY ||
       (shader->info.stage != MESA_SHADER_TESS_CTRL && shader->info.stage != MESA_SHADER_TESS_EVAL &&
        !llvm_has_working_vgpr_indexing)) {
      indirect_mask |= nir_var_shader_in;
   }
   if (!llvm_has_working_vgpr_indexing && shader->info.stage != MESA_SHADER_TESS_CTRL)
      indirect_mask |= nir_var_shader_out;

   /* TODO: We shouldn't need to do this, however LLVM isn't currently
    * smart enough to handle indirects without causing excess spilling
    * causing the gpu to hang.
    *
    * See the following thread for more details of the problem:
    * https://lists.freedesktop.org/archives/mesa-dev/2017-July/162106.html
    */
   indirect_mask |= nir_var_function_temp;

   NIR_PASS(progress, shader, nir_lower_indirect_derefs, indirect_mask, UINT32_MAX);
   return progress;
}
