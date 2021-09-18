/*
 * Copyright (C) 2020 Icecream95 <ixn@disroot.org>
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

#include "pan_ir.h"
#include "compiler/nir/nir_builder.h"

/* OpenCL uses 64-bit types for some intrinsic functions, including
 * global_invocation_id(). This could be worked around during conversion to
 * MIR, except that global_invocation_id is a vec3, and the 128-bit registers
 * on Midgard can only hold a 64-bit vec2.
 * Rather than attempting to add hacky 64-bit vec3 support, convert these
 * intrinsics to 32-bit and add a cast back to 64-bit, and rely on NIR not
 * vectorizing back to vec3.
 */

static bool
nir_lower_64bit_intrin_instr(nir_builder *b, nir_instr *instr, void *data)
{
        if (instr->type != nir_instr_type_intrinsic)
                return false;

        nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

        switch (intr->intrinsic) {
        case nir_intrinsic_load_global_invocation_id:
        case nir_intrinsic_load_global_invocation_id_zero_base:
        case nir_intrinsic_load_workgroup_id:
        case nir_intrinsic_load_num_workgroups:
                break;

        default:
                return false;
        }

        if (nir_dest_bit_size(intr->dest) != 64)
                return false;

        b->cursor = nir_after_instr(instr);

        assert(intr->dest.is_ssa);
        intr->dest.ssa.bit_size = 32;

        nir_ssa_def *conv = nir_u2u64(b, &intr->dest.ssa);

        nir_ssa_def_rewrite_uses_after(&intr->dest.ssa, conv,
                                       conv->parent_instr);

        return true;
}

bool
pan_nir_lower_64bit_intrin(nir_shader *shader)
{
        return nir_shader_instructions_pass(shader,
                                            nir_lower_64bit_intrin_instr,
                                            nir_metadata_block_index | nir_metadata_dominance,
                                            NULL);
}
