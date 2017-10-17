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

#include "compiler/v3d_compiler.h"
#include "compiler/nir/nir_builder.h"

/**
 * Walks the NIR generated by TGSI-to-NIR or GLSL-to-NIR to lower its io
 * intrinsics into something amenable to the V3D architecture.
 *
 * Currently, it splits VS inputs and uniforms into scalars, drops any
 * non-position outputs in coordinate shaders, and fixes up the addressing on
 * indirect uniform loads.  FS input and VS output scalarization is handled by
 * nir_lower_io_to_scalar().
 */

static void
replace_intrinsic_with_vec(nir_builder *b, nir_intrinsic_instr *intr,
                           nir_ssa_def **comps)
{

        /* Batch things back together into a vector.  This will get split by
         * the later ALU scalarization pass.
         */
        nir_ssa_def *vec = nir_vec(b, comps, intr->num_components);

        /* Replace the old intrinsic with a reference to our reconstructed
         * vector.
         */
        nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_src_for_ssa(vec));
        nir_instr_remove(&intr->instr);
}

static void
v3d_nir_lower_output(struct v3d_compile *c, nir_builder *b,
                     nir_intrinsic_instr *intr)
{
        nir_variable *output_var = NULL;
        nir_foreach_variable(var, &c->s->outputs) {
                if (var->data.driver_location == nir_intrinsic_base(intr)) {
                        output_var = var;
                        break;
                }
        }
        assert(output_var);

        if (c->vs_key) {
                int slot = output_var->data.location;
                bool used = false;

                switch (slot) {
                case VARYING_SLOT_PSIZ:
                case VARYING_SLOT_POS:
                        used = true;
                        break;

                default:
                        for (int i = 0; i < c->vs_key->num_fs_inputs; i++) {
                                if (v3d_slot_get_slot(c->vs_key->fs_inputs[i]) == slot) {
                                        used = true;
                                        break;
                                }
                        }
                        break;
                }

                if (!used)
                        nir_instr_remove(&intr->instr);
        }
}

static void
v3d_nir_lower_uniform(struct v3d_compile *c, nir_builder *b,
                      nir_intrinsic_instr *intr)
{
        b->cursor = nir_before_instr(&intr->instr);

        /* Generate scalar loads equivalent to the original vector. */
        nir_ssa_def *dests[4];
        for (unsigned i = 0; i < intr->num_components; i++) {
                nir_intrinsic_instr *intr_comp =
                        nir_intrinsic_instr_create(c->s, intr->intrinsic);
                intr_comp->num_components = 1;
                nir_ssa_dest_init(&intr_comp->instr, &intr_comp->dest, 1, 32, NULL);

                /* Convert the uniform offset to bytes.  If it happens
                 * to be a constant, constant-folding will clean up
                 * the shift for us.
                 */
                nir_intrinsic_set_base(intr_comp,
                                       nir_intrinsic_base(intr) * 16 +
                                       i * 4);

                intr_comp->src[0] =
                        nir_src_for_ssa(nir_ishl(b, intr->src[0].ssa,
                                                 nir_imm_int(b, 4)));

                dests[i] = &intr_comp->dest.ssa;

                nir_builder_instr_insert(b, &intr_comp->instr);
        }

        replace_intrinsic_with_vec(b, intr, dests);
}

static void
v3d_nir_lower_io_instr(struct v3d_compile *c, nir_builder *b,
                       struct nir_instr *instr)
{
        if (instr->type != nir_instr_type_intrinsic)
                return;
        nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

        switch (intr->intrinsic) {
        case nir_intrinsic_load_input:
                break;

        case nir_intrinsic_store_output:
                v3d_nir_lower_output(c, b, intr);
                break;

        case nir_intrinsic_load_uniform:
                v3d_nir_lower_uniform(c, b, intr);
                break;

        case nir_intrinsic_load_user_clip_plane:
        default:
                break;
        }
}

static bool
v3d_nir_lower_io_impl(struct v3d_compile *c, nir_function_impl *impl)
{
        nir_builder b;
        nir_builder_init(&b, impl);

        nir_foreach_block(block, impl) {
                nir_foreach_instr_safe(instr, block)
                        v3d_nir_lower_io_instr(c, &b, instr);
        }

        nir_metadata_preserve(impl, nir_metadata_block_index |
                              nir_metadata_dominance);

        return true;
}

void
v3d_nir_lower_io(nir_shader *s, struct v3d_compile *c)
{
        nir_foreach_function(function, s) {
                if (function->impl)
                        v3d_nir_lower_io_impl(c, function->impl);
        }
}