/*
 * Copyright © 2020 Raspberry Pi Ltd
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

#include "compiler/v3d_compiler.h"
#include "compiler/nir/nir_builder.h"

/* Vulkan's robustBufferAccess feature is only concerned with buffers that are
 * bound through descriptor sets, so shared memory is not included, but it may
 * be useful to enable this for debugging.
 */
const bool robust_shared_enabled = false;

static void
rewrite_offset(nir_builder *b,
               nir_intrinsic_instr *instr,
               uint32_t type_sz,
               uint32_t buffer_idx,
               uint32_t offset_src,
               nir_intrinsic_op buffer_size_op)
{
        b->cursor = nir_before_instr(&instr->instr);

        /* Get size of the buffer */
        nir_intrinsic_instr *size =
                nir_intrinsic_instr_create(b->shader, buffer_size_op);
        size->src[0] = nir_src_for_ssa(nir_imm_int(b, buffer_idx));
        nir_ssa_dest_init(&size->instr, &size->dest, 1, 32, NULL);
        nir_builder_instr_insert(b, &size->instr);

        /* Compute the maximum offset being accessed and if it is
         * out of bounds rewrite it to 0 to ensure the access is
         * within bounds.
         */
        const uint32_t access_size = instr->num_components * type_sz;
        nir_ssa_def *max_access_offset =
                nir_iadd(b, instr->src[offset_src].ssa,
                            nir_imm_int(b, access_size - 1));
        nir_ssa_def *offset =
                nir_bcsel(b, nir_uge(b, max_access_offset, &size->dest.ssa),
                             nir_imm_int(b, 0),
                             instr->src[offset_src].ssa);

        /* Rewrite offset */
        nir_instr_rewrite_src(&instr->instr, &instr->src[offset_src],
                              nir_src_for_ssa(offset));
}

static void
lower_load(struct v3d_compile *c,
           nir_builder *b,
           nir_intrinsic_instr *instr)
{
        uint32_t type_sz = nir_dest_bit_size(instr->dest) / 8;
        uint32_t index = nir_src_comp_as_uint(instr->src[0], 0);

        nir_intrinsic_op op;
        if (instr->intrinsic == nir_intrinsic_load_ubo) {
                op = nir_intrinsic_get_ubo_size;
                if (c->key->environment == V3D_ENVIRONMENT_VULKAN)
                        index--;
        } else {
                op = nir_intrinsic_get_ssbo_size;
        }

        rewrite_offset(b, instr, type_sz, index, 1, op);
}

static void
lower_store(struct v3d_compile *c,
            nir_builder *b,
            nir_intrinsic_instr *instr)
{
        uint32_t type_sz = nir_src_bit_size(instr->src[0]) / 8;
        uint32_t index = nir_src_comp_as_uint(instr->src[1], 0);
        rewrite_offset(b, instr, type_sz, index, 2, nir_intrinsic_get_ssbo_size);
}

static void
lower_atomic(struct v3d_compile *c,
             nir_builder *b,
             nir_intrinsic_instr *instr)
{
        uint32_t index = nir_src_comp_as_uint(instr->src[0], 0);
        rewrite_offset(b, instr, 4, index, 1, nir_intrinsic_get_ssbo_size);
}

static void
lower_shared(struct v3d_compile *c,
             nir_builder *b,
             nir_intrinsic_instr *instr)
{
        uint32_t type_sz, offset_src;
        if (instr->intrinsic == nir_intrinsic_load_shared) {
                offset_src = 0;
                type_sz = nir_dest_bit_size(instr->dest) / 8;
        } else if (instr->intrinsic == nir_intrinsic_store_shared) {
                offset_src = 1;
                type_sz = nir_src_bit_size(instr->src[0]) / 8;
        } else {
                /* atomic */
                offset_src = 0;
                type_sz = 4;
        }

        b->cursor = nir_before_instr(&instr->instr);
        const uint32_t access_size = instr->num_components * type_sz;
        nir_ssa_def *max_access_offset =
                nir_iadd(b, instr->src[offset_src].ssa,
                            nir_imm_int(b, access_size - 1));
        nir_ssa_def *offset =
                nir_bcsel(b, nir_uge(b, max_access_offset,
                                        nir_imm_int(b, c->s->info.shared_size)),
                             nir_imm_int(b, 0),
                             instr->src[offset_src].ssa);

        nir_instr_rewrite_src(&instr->instr, &instr->src[offset_src],
                              nir_src_for_ssa(offset));
}

static bool
lower_instr(nir_builder *b, nir_instr *instr, void *_state)
{
        struct v3d_compile *c = _state;

        if (instr->type != nir_instr_type_intrinsic)
                return false;
        nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

        switch (intr->intrinsic) {
        case nir_intrinsic_load_ubo:
        case nir_intrinsic_load_ssbo:
                lower_load(c, b, intr);
                return true;
        case nir_intrinsic_store_ssbo:
                lower_store(c, b, intr);
                return true;
        case nir_intrinsic_ssbo_atomic_add:
        case nir_intrinsic_ssbo_atomic_imin:
        case nir_intrinsic_ssbo_atomic_umin:
        case nir_intrinsic_ssbo_atomic_imax:
        case nir_intrinsic_ssbo_atomic_umax:
        case nir_intrinsic_ssbo_atomic_and:
        case nir_intrinsic_ssbo_atomic_or:
        case nir_intrinsic_ssbo_atomic_xor:
        case nir_intrinsic_ssbo_atomic_exchange:
        case nir_intrinsic_ssbo_atomic_comp_swap:
                lower_atomic(c, b, intr);
                return true;
        case nir_intrinsic_store_shared:
        case nir_intrinsic_load_shared:
        case nir_intrinsic_shared_atomic_add:
        case nir_intrinsic_shared_atomic_imin:
        case nir_intrinsic_shared_atomic_umin:
        case nir_intrinsic_shared_atomic_imax:
        case nir_intrinsic_shared_atomic_umax:
        case nir_intrinsic_shared_atomic_and:
        case nir_intrinsic_shared_atomic_or:
        case nir_intrinsic_shared_atomic_xor:
        case nir_intrinsic_shared_atomic_exchange:
        case nir_intrinsic_shared_atomic_comp_swap:
                if (robust_shared_enabled) {
                        lower_shared(c, b, intr);
                        return true;
                }
                return false;
        default:
                return false;
        }
}

bool
v3d_nir_lower_robust_buffer_access(nir_shader *s, struct v3d_compile *c)
{
        return nir_shader_instructions_pass(s, lower_instr,
                                            nir_metadata_block_index |
                                            nir_metadata_dominance, c);
}
