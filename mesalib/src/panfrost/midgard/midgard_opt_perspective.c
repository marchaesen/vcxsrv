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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Midgard has some accelerated support for perspective projection on the
 * load/store pipes. So the first perspective projection pass looks for
 * lowered/open-coded perspective projection of the form "fmul (A.xyz,
 * frcp(A.w))" or "fmul (A.xy, frcp(A.z))" and rewrite with a native
 * perspective division opcode (on the load/store pipe). Caveats apply: the
 * frcp should be used only once to make this optimization worthwhile. And the
 * source of the frcp ought to be a varying to make it worthwhile...
 *
 * The second pass in this file is a step #2 of sorts: fusing that load/store
 * projection into a varying load instruction (they can be done together
 * implicitly). This depends on the combination pass. Again caveat: the vary
 * should only be used once to make this worthwhile.
 */

#include "compiler.h"

bool
midgard_opt_combine_projection(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                /* First search for fmul */
                if (ins->type != TAG_ALU_4) continue;
                if (ins->alu.op != midgard_alu_op_fmul) continue;

                /* TODO: Flip */

                /* Check the swizzles */
                
                midgard_vector_alu_src src1 =
                        vector_alu_from_unsigned(ins->alu.src1);

                midgard_vector_alu_src src2 =
                        vector_alu_from_unsigned(ins->alu.src2);

                if (!mir_is_simple_swizzle(src1.swizzle, ins->mask)) continue;
                if (src2.swizzle != SWIZZLE_XXXX) continue;

                /* Awesome, we're the right form. Now check where src2 is from */
                unsigned frcp = ins->src[1];
                unsigned to = ins->dest;

                if (frcp & IS_REG) continue;
                if (to & IS_REG) continue;

                bool frcp_found = false;
                unsigned frcp_component = 0;
                unsigned frcp_from = 0;

                mir_foreach_instr_in_block_safe(block, sub) {
                        if (sub->dest != frcp) continue;

                        midgard_vector_alu_src s =
                                vector_alu_from_unsigned(sub->alu.src1);

                        frcp_component = s.swizzle & 3;
                        frcp_from = sub->src[0];

                        frcp_found =
                                (sub->type == TAG_ALU_4) &&
                                (sub->alu.op == midgard_alu_op_frcp);
                        break;
                }

                if (!frcp_found) continue;
                if (frcp_component != COMPONENT_W && frcp_component != COMPONENT_Z) continue;
                if (!mir_single_use(ctx, frcp)) continue;

                /* Heuristic: check if the frcp is from a single-use varying */

                bool ok = false;

                /* One for frcp and one for fmul */
                if (mir_use_count(ctx, frcp_from) > 2) continue;

                mir_foreach_instr_in_block_safe(block, v) {
                        if (v->dest != frcp_from) continue;
                        if (v->type != TAG_LOAD_STORE_4) break;
                        if (!OP_IS_LOAD_VARY_F(v->load_store.op)) break;

                        ok = true;
                        break;
                }

                if (!ok)
                        continue;

                /* Nice, we got the form spot on. Let's convert! */

                midgard_instruction accel = {
                        .type = TAG_LOAD_STORE_4,
                        .mask = ins->mask,
                        .dest = to,
                        .src = { frcp_from, ~0, ~0 },
                        .load_store = {
                                .op = frcp_component == COMPONENT_W ?
                                        midgard_op_ldst_perspective_division_w : 
                                        midgard_op_ldst_perspective_division_z,
                                .swizzle = SWIZZLE_XYZW,
                                .arg_1 = 0x20
                        }
                };

                mir_insert_instruction_before(ctx, ins, accel);
                mir_remove_instruction(ins);

                progress |= true;
        }

        return progress;
}

bool
midgard_opt_varying_projection(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                /* Search for a projection */
                if (ins->type != TAG_LOAD_STORE_4) continue;
                if (!OP_IS_PROJECTION(ins->load_store.op)) continue;

                unsigned vary = ins->src[0];
                unsigned to = ins->dest;

                if (vary & IS_REG) continue;
                if (to & IS_REG) continue;
                if (!mir_single_use(ctx, vary)) continue;

                /* Check for a varying source. If we find it, we rewrite */

                bool rewritten = false;

                mir_foreach_instr_in_block_safe(block, v) {
                        if (v->dest != vary) continue;
                        if (v->type != TAG_LOAD_STORE_4) break;
                        if (!OP_IS_LOAD_VARY_F(v->load_store.op)) break;

                        /* We found it, so rewrite it to project. Grab the
                         * modifier */

                        unsigned param = v->load_store.varying_parameters;
                        midgard_varying_parameter p;
                        memcpy(&p, &param, sizeof(p));

                        if (p.modifier != midgard_varying_mod_none)
                                break;

                        bool projects_w =
                                ins->load_store.op == midgard_op_ldst_perspective_division_w;

                        p.modifier = projects_w ?
                                midgard_varying_mod_perspective_w :
                                midgard_varying_mod_perspective_z;

                        /* Aliasing rules are annoying */
                        memcpy(&param, &p, sizeof(p));
                        v->load_store.varying_parameters = param;

                        /* Use the new destination */
                        v->dest = to;

                        rewritten = true;
                        break;
                }

                if (rewritten)
                        mir_remove_instruction(ins);

                progress |= rewritten;
        }

        return progress;
}
