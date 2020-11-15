/*
 * Copyright (C) 2018-2020 Collabora, Ltd.
 * Copyright (C) 2019-2020 Icecream95
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

/* Midgard can write all of color, depth and stencil in a single writeout
 * operation, so we merge depth/stencil stores with color stores.
 * If there are no color stores, we add a write to the "depth RT".
 *
 * For Bifrost, we want these combined so we can properly order
 * +ZS_EMIT with respect to +ATEST and +BLEND, as well as combining
 * depth/stencil stores into a single +ZS_EMIT op.
 */
bool
pan_nir_lower_zs_store(nir_shader *nir)
{
        if (nir->info.stage != MESA_SHADER_FRAGMENT)
                return false;

        nir_variable *z_var = NULL, *s_var = NULL;

        nir_foreach_shader_out_variable(var, nir) {
                if (var->data.location == FRAG_RESULT_DEPTH)
                        z_var = var;
                else if (var->data.location == FRAG_RESULT_STENCIL)
                        s_var = var;
        }

        if (!z_var && !s_var)
                return false;

        bool progress = false;

        nir_foreach_function(function, nir) {
                if (!function->impl) continue;

                nir_intrinsic_instr *z_store = NULL, *s_store = NULL;

                nir_foreach_block(block, function->impl) {
                        nir_foreach_instr_safe(instr, block) {
                                if (instr->type != nir_instr_type_intrinsic)
                                        continue;

                                nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
                                if (intr->intrinsic != nir_intrinsic_store_output)
                                        continue;

                                if (z_var && nir_intrinsic_base(intr) == z_var->data.driver_location) {
                                        assert(!z_store);
                                        z_store = intr;
                                }

                                if (s_var && nir_intrinsic_base(intr) == s_var->data.driver_location) {
                                        assert(!s_store);
                                        s_store = intr;
                                }
                        }
                }

                if (!z_store && !s_store) continue;

                bool replaced = false;

                nir_foreach_block(block, function->impl) {
                        nir_foreach_instr_safe(instr, block) {
                                if (instr->type != nir_instr_type_intrinsic)
                                        continue;

                                nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
                                if (intr->intrinsic != nir_intrinsic_store_output)
                                        continue;

                                const nir_variable *var = nir_find_variable_with_driver_location(nir, nir_var_shader_out, nir_intrinsic_base(intr));
                                assert(var);

                                if (var->data.location != FRAG_RESULT_COLOR &&
                                    var->data.location < FRAG_RESULT_DATA0)
                                        continue;

                                if (var->data.index)
                                        continue;

                                assert(nir_src_is_const(intr->src[1]) && "no indirect outputs");

                                nir_builder b;
                                nir_builder_init(&b, function->impl);

                                assert(!z_store || z_store->instr.block == instr->block);
                                assert(!s_store || s_store->instr.block == instr->block);
                                b.cursor = nir_after_block_before_jump(instr->block);

                                nir_intrinsic_instr *combined_store;
                                combined_store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_combined_output_pan);

                                combined_store->num_components = intr->src[0].ssa->num_components;

                                nir_intrinsic_set_base(combined_store, nir_intrinsic_base(intr));
                                nir_intrinsic_set_src_type(combined_store, nir_intrinsic_src_type(intr));

                                unsigned writeout = PAN_WRITEOUT_C;
                                if (z_store)
                                        writeout |= PAN_WRITEOUT_Z;
                                if (s_store)
                                        writeout |= PAN_WRITEOUT_S;

                                nir_intrinsic_set_component(combined_store, writeout);

                                struct nir_ssa_def *zero = nir_imm_int(&b, 0);

                                struct nir_ssa_def *src[4] = {
                                   intr->src[0].ssa,
                                   intr->src[1].ssa,
                                   z_store ? z_store->src[0].ssa : zero,
                                   s_store ? s_store->src[0].ssa : zero,
                                };

                                for (int i = 0; i < 4; ++i)
                                   combined_store->src[i] = nir_src_for_ssa(src[i]);

                                nir_builder_instr_insert(&b, &combined_store->instr);

                                nir_instr_remove(instr);

                                replaced = true;
                        }
                }

                /* Insert a store to the depth RT (0xff) if needed */
                if (!replaced) {
                        nir_builder b;
                        nir_builder_init(&b, function->impl);

                        nir_block *block = NULL;
                        if (z_store && s_store)
                                assert(z_store->instr.block == s_store->instr.block);

                        if (z_store)
                                block = z_store->instr.block;
                        else
                                block = s_store->instr.block;

                        b.cursor = nir_after_block_before_jump(block);

                        nir_intrinsic_instr *combined_store;
                        combined_store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_combined_output_pan);

                        combined_store->num_components = 4;

                        unsigned base;
                        if (z_store)
                                base = nir_intrinsic_base(z_store);
                        else
                                base = nir_intrinsic_base(s_store);
                        nir_intrinsic_set_base(combined_store, base);
                        nir_intrinsic_set_src_type(combined_store, nir_type_float32);

                        unsigned writeout = 0;
                        if (z_store)
                                writeout |= PAN_WRITEOUT_Z;
                        if (s_store)
                                writeout |= PAN_WRITEOUT_S;

                        nir_intrinsic_set_component(combined_store, writeout);

                        struct nir_ssa_def *zero = nir_imm_int(&b, 0);

                        struct nir_ssa_def *src[4] = {
                                nir_imm_vec4(&b, 0, 0, 0, 0),
                                zero,
                                z_store ? z_store->src[0].ssa : zero,
                                s_store ? s_store->src[0].ssa : zero,
                        };

                        for (int i = 0; i < 4; ++i)
                                combined_store->src[i] = nir_src_for_ssa(src[i]);

                        nir_builder_instr_insert(&b, &combined_store->instr);
                }

                if (z_store)
                        nir_instr_remove(&z_store->instr);

                if (s_store)
                        nir_instr_remove(&s_store->instr);

                nir_metadata_preserve(function->impl, nir_metadata_block_index | nir_metadata_dominance);
                progress = true;
        }

        return progress;
}

/* Real writeout stores, which break execution, need to be moved to after
 * dual-source stores, which are just standard register writes. */
bool
pan_nir_reorder_writeout(nir_shader *nir)
{
        bool progress = false;

        nir_foreach_function(function, nir) {
                if (!function->impl) continue;

                nir_foreach_block(block, function->impl) {
                        nir_instr *last_writeout = NULL;

                        nir_foreach_instr_reverse_safe(instr, block) {
                                if (instr->type != nir_instr_type_intrinsic)
                                        continue;

                                nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
                                if (intr->intrinsic != nir_intrinsic_store_output)
                                        continue;

                                const nir_variable *var = nir_find_variable_with_driver_location(nir, nir_var_shader_out, nir_intrinsic_base(intr));

                                if (var->data.index) {
                                        if (!last_writeout)
                                                last_writeout = instr;
                                        continue;
                                }

                                if (!last_writeout)
                                        continue;

                                /* This is a real store, so move it to after dual-source stores */
                                exec_node_remove(&instr->node);
                                exec_node_insert_after(&last_writeout->node, &instr->node);

                                progress = true;
                        }
                }
        }

        return progress;
}
