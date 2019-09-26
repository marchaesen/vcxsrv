/*
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
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

#include "compiler_defines.h"
#include "bifrost_print.h"

const char *ir_names[op_last + 1] = {
        "fma.f32",
        "fmul.f32",
        "fadd.f32",
        "frcp_fast.f32",
        "max.f32",
        "min.f32",
        "add.i32",
        "sub.i32",
        "imad",
        "mul.i32",
        "or.i32",
        "and.i32",
        "lshift.i32",
        "xor.i32",
        "rshift.i32",
        "arshift.i32",
        "csel.i32",
        "imin3.i32",
        "umin3.i32",
        "imax3.i32",
        "umax3.i32",

        "branch",

        // unary
        "trunc",
        "ceil",
        "floor",
        "round",
        "roundeven",

        "mov",
        "movi",

        "ld_ubo.v1",
        "ld_ubo.v2",
        "ld_ubo.v3",
        "ld_ubo.v4",

        "ld_attr.v1",
        "ld_attr.v2",
        "ld_attr.v3",
        "ld_attr.v4",

        "ld_var_addr",
        "st_vary.v1",
        "st_vary.v2",
        "st_vary.v3",
        "st_vary.v4",

        "store.v1",
        "store.v2",
        "store.v3",
        "store.v4",

        "create_vector",
        "extract_element",
        "last",
};

void
print_mir_instruction(struct bifrost_instruction *instr, bool post_ra)
{
        printf("\t");
        if (instr->dest_components != 0) {
                if (post_ra) {
                        if (instr->dest_components == 1) {
                                printf("r%d = ", instr->args.dest);
                        } else {
                                printf("r%d..r%d = ", instr->args.dest, instr->args.dest + instr->dest_components - 1);

                        }
                } else {
                        printf("%%0x%08x = ", instr->ssa_args.dest);
                }
        }

        printf("%s ", ir_names[instr->op]);

        if (post_ra) {
                uint32_t sources[4] = {
                        instr->args.src0,
                        instr->args.src1,
                        instr->args.src2,
                        instr->args.src3
                };
                for (unsigned i = 0; i < 4; ++i) {
                        if (sources[i] == SSA_INVALID_VALUE) break;
                        bool last = i + 1 == 4 ||
                                    sources[i + 1] == SSA_INVALID_VALUE;

                        if (sources[i] == SSA_FIXED_CONST_0) {
                                printf("#0%s", last ? "" : ", ");
                        } else if (sources[i] >= SSA_FIXED_UREG_MINIMUM) {
                                printf("u%d%s", SSA_UREG_FROM_FIXED(sources[i]), last ? "" : ", ");
                        } else {
                                printf("r%d%s", sources[i], last ? "" : ", ");
                        }
                }
        } else {
                uint32_t sources[4] = {
                        instr->ssa_args.src0,
                        instr->ssa_args.src1,
                        instr->ssa_args.src2,
                        instr->ssa_args.src3
                };
                for (unsigned i = 0; i < 4; ++i) {
                        if (sources[i] == SSA_INVALID_VALUE) break;
                        bool last = i + 1 == 4 ||
                                    sources[i + 1] == SSA_INVALID_VALUE;

                        printf("%%0x%08x%s", sources[i], last ? "" : ", ");
                }
        }

        printf("\n");
}

void
print_mir_block(struct bifrost_block *block, bool post_ra)
{
        printf("{\n");

        mir_foreach_instr_in_block(block, instr) {
                print_mir_instruction(instr, post_ra);
        }

        printf("}\n");
}
