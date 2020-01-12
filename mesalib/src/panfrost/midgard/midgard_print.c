/*
 * Copyright (C) 2018-2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

#include "compiler.h"
#include "helpers.h"
#include "midgard_ops.h"

/* Pretty printer for Midgard IR, for use debugging compiler-internal
 * passes like register allocation. The output superficially resembles
 * Midgard assembly, with the exception that unit information and such is
 * (normally) omitted, and generic indices are usually used instead of
 * registers */

static void
mir_print_index(int source)
{
        if (source == ~0) {
                printf("_");
                return;
        }

        if (source >= SSA_FIXED_MINIMUM) {
                /* Specific register */
                int reg = SSA_REG_FROM_FIXED(source);

                /* TODO: Moving threshold */
                if (reg > 16 && reg < 24)
                        printf("u%d", 23 - reg);
                else
                        printf("r%d", reg);
        } else {
                printf("%d", source);
        }
}

static const char components[16] = "xyzwefghijklmnop";

static void
mir_print_mask(unsigned mask)
{
        printf(".");

        for (unsigned i = 0; i < 16; ++i) {
                if (mask & (1 << i))
                        putchar(components[i]);
        }
}

static void
mir_print_swizzle(unsigned *swizzle)
{
        printf(".");

        for (unsigned i = 0; i < 16; ++i)
                putchar(components[swizzle[i]]);
}

static const char *
mir_get_unit(unsigned unit)
{
        switch (unit) {
        case ALU_ENAB_VEC_MUL:
                return "vmul";
        case ALU_ENAB_SCAL_ADD:
                return "sadd";
        case ALU_ENAB_VEC_ADD:
                return "vadd";
        case ALU_ENAB_SCAL_MUL:
                return "smul";
        case ALU_ENAB_VEC_LUT:
                return "lut";
        case ALU_ENAB_BR_COMPACT:
                return "br";
        case ALU_ENAB_BRANCH:
                return "brx";
        default:
                return "???";
        }
}

void
mir_print_instruction(midgard_instruction *ins)
{
        printf("\t");

        switch (ins->type) {
        case TAG_ALU_4: {
                midgard_alu_op op = ins->alu.op;
                const char *name = alu_opcode_props[op].name;

                const char *branch_target_names[] = {
                        "goto", "break", "continue", "discard"
                };

                if (ins->compact_branch)
                        name = branch_target_names[ins->branch.target_type];

                if (ins->unit)
                        printf("%s.", mir_get_unit(ins->unit));

                printf("%s", name ? name : "??");
                break;
        }

        case TAG_LOAD_STORE_4: {
                midgard_load_store_op op = ins->load_store.op;
                const char *name = load_store_opcode_props[op].name;

                assert(name);
                printf("%s", name);
                break;
        }

        case TAG_TEXTURE_4: {
                printf("texture");
                break;
        }

        default:
                assert(0);
        }

        if (ins->invert || (ins->compact_branch && ins->branch.invert_conditional))
                printf(".not");

        printf(" ");
        mir_print_index(ins->dest);

        if (ins->mask != 0xF)
                mir_print_mask(ins->mask);

        printf(", ");

        mir_print_index(ins->src[0]);
        mir_print_swizzle(ins->swizzle[0]);
        printf(", ");

        if (ins->has_inline_constant)
                printf("#%d", ins->inline_constant);
        else {
                mir_print_index(ins->src[1]);
                mir_print_swizzle(ins->swizzle[1]);
        }

        printf(", ");
        mir_print_index(ins->src[2]);
        mir_print_swizzle(ins->swizzle[2]);

        printf(", ");
        mir_print_index(ins->src[3]);
        mir_print_swizzle(ins->swizzle[3]);

        if (ins->has_constants) {
                uint32_t *uc = ins->constants;
                float *fc = (float *) uc;

                if (midgard_is_integer_op(ins->alu.op))
                        printf(" <0x%X, 0x%X, 0x%X, 0x%x>", uc[0], uc[1], uc[2], uc[3]);
                else
                        printf(" <%f, %f, %f, %f>", fc[0], fc[1], fc[2], fc[3]);
        }

        if (ins->no_spill)
                printf(" /* no spill */");

        printf("\n");
}

/* Dumps MIR for a block or entire shader respective */

void
mir_print_block(midgard_block *block)
{
        printf("block%u: {\n", block->source_id);

        if (block->is_scheduled) {
                mir_foreach_bundle_in_block(block, bundle) {
                        for (unsigned i = 0; i < bundle->instruction_count; ++i)
                                mir_print_instruction(bundle->instructions[i]);

                        printf("\n");
                }
        } else {
                mir_foreach_instr_in_block(block, ins) {
                        mir_print_instruction(ins);
                }
        }

        printf("}");

        if (block->nr_successors) {
                printf(" -> ");
                for (unsigned i = 0; i < block->nr_successors; ++i) {
                        printf("block%u%s", block->successors[i]->source_id,
                                        (i + 1) != block->nr_successors ? ", " : "");
                }
        }

        printf(" from { ");
        mir_foreach_predecessor(block, pred)
                printf("block%u ", pred->source_id);
        printf("}");

        printf("\n\n");
}

void
mir_print_shader(compiler_context *ctx)
{
        mir_foreach_block(ctx, block) {
                mir_print_block(block);
        }
}
