/*
 * Copyright © 2018 Intel Corporation
 * Copyright © 2018 Broadcom
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

#include "v3d_compiler.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"

/** @file v3d_nir_lower_scratch.c
 *
 * Swizzles around the addresses of
 * nir_intrinsic_load_scratch/nir_intrinsic_store_scratch so that a QPU stores
 * a cacheline at a time per dword of scratch access.
 */

static nir_def *
v3d_nir_scratch_offset(nir_builder *b, nir_intrinsic_instr *instr)
{
        b->cursor = nir_before_instr(&instr->instr);
        nir_def *offset = nir_get_io_offset_src(instr)->ssa;

        assert(nir_intrinsic_align_mul(instr) >= 4);
        assert(nir_intrinsic_align_offset(instr) % 4 == 0);

        /* The spill_offset register will already have the subgroup ID (EIDX)
         * shifted and ORed in at bit 2, so all we need to do is to move the
         * dword index up above V3D_CHANNELS.
         */
        return nir_imul_imm(b, offset, V3D_CHANNELS);
}

static void
v3d_nir_lower_scratch_instr(nir_builder *b, nir_intrinsic_instr *instr)
{
        /* scalarized through nir_lower_mem_access_bit_sizes */
        assert(instr->num_components == 1);

        nir_def *offset = v3d_nir_scratch_offset(b, instr);
        nir_src_rewrite(nir_get_io_offset_src(instr), offset);
}

static bool
v3d_nir_lower_scratch_cb(nir_builder *b,
                         nir_intrinsic_instr *intr,
                         void *_state)
{
        switch (intr->intrinsic) {
        case nir_intrinsic_load_scratch:
        case nir_intrinsic_store_scratch:
                v3d_nir_lower_scratch_instr(b, intr);
                return true;
        default:
                return false;
        }

        return false;
}

bool
v3d_nir_lower_scratch(nir_shader *s)
{
        return nir_shader_intrinsics_pass(s, v3d_nir_lower_scratch_cb,
                                            nir_metadata_control_flow, NULL);
}
