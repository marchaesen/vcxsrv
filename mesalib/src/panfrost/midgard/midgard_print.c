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

#include <math.h>

#include "util/bitscan.h"
#include "util/half_float.h"
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
mir_print_constant_component(FILE *fp, const midgard_constants *consts, unsigned c,
                             midgard_reg_mode reg_mode, bool half,
                             unsigned mod, midgard_alu_op op)
{
        bool is_sint = false, is_uint = false, is_hex = false;
        const char *opname = alu_opcode_props[op].name;

        /* Add a sentinel name to prevent crashing */
        if (!opname)
                opname = "unknown";

        if (opname[0] == 'u') {
                /* If the opcode starts with a 'u' we are sure we deal with an
                 * unsigned int operation
		 */
                is_uint = true;
	} else if (opname[0] == 'i') {
                /* Bit ops are easier to follow when the constant is printed in
                 * hexadecimal. Other operations starting with a 'i' are
                 * considered to operate on signed integers. That might not
                 * be true for all of them, but it's good enough for traces.
                 */
                if (op >= midgard_alu_op_iand &&
                    op <= midgard_alu_op_ibitcount8)
                        is_hex = true;
                else
                        is_sint = true;
        }

        if (half)
                reg_mode--;

        switch (reg_mode) {
        case midgard_reg_mode_64:
                if (is_sint) {
                        fprintf(fp, "%"PRIi64, consts->i64[c]);
                } else if (is_uint) {
                        fprintf(fp, "%"PRIu64, consts->u64[c]);
                } else if (is_hex) {
                        fprintf(fp, "0x%"PRIX64, consts->u64[c]);
                } else {
                        double v = consts->f64[c];

                        if (mod & MIDGARD_FLOAT_MOD_ABS) v = fabs(v);
                        if (mod & MIDGARD_FLOAT_MOD_NEG) v = -v;

                        printf("%g", v);
                }
                break;

        case midgard_reg_mode_32:
                if (is_sint) {
                        int64_t v;

                        if (half && mod == midgard_int_zero_extend)
                                v = consts->u32[c];
                        else if (half && mod == midgard_int_shift)
                                v = (uint64_t)consts->u32[c] << 32;
                        else
                                v = consts->i32[c];

                        fprintf(fp, "%"PRIi64, v);
                } else if (is_uint || is_hex) {
                        uint64_t v;

                        if (half && mod == midgard_int_shift)
                                v = (uint64_t)consts->u32[c] << 32;
                        else
                                v = consts->u32[c];

                        fprintf(fp, is_uint ? "%"PRIu64 : "0x%"PRIX64, v);
                } else {
                        float v = consts->f32[c];

                        if (mod & MIDGARD_FLOAT_MOD_ABS) v = fabsf(v);
                        if (mod & MIDGARD_FLOAT_MOD_NEG) v = -v;

                        fprintf(fp, "%g", v);
                }
                break;

        case midgard_reg_mode_16:
                if (is_sint) {
                        int32_t v;

                        if (half && mod == midgard_int_zero_extend)
                                v = consts->u16[c];
                        else if (half && mod == midgard_int_shift)
                                v = (uint32_t)consts->u16[c] << 16;
                        else
                                v = consts->i16[c];

                        fprintf(fp, "%d", v);
                } else if (is_uint || is_hex) {
                        uint32_t v;

                        if (half && mod == midgard_int_shift)
                                v = (uint32_t)consts->u16[c] << 16;
                        else
                                v = consts->u16[c];

                        fprintf(fp, is_uint ? "%u" : "0x%X", v);
                } else {
                        float v = _mesa_half_to_float(consts->f16[c]);

                        if (mod & MIDGARD_FLOAT_MOD_ABS) v = fabsf(v);
                        if (mod & MIDGARD_FLOAT_MOD_NEG) v = -v;

                        fprintf(fp, "%g", v);
                }
                break;

        case midgard_reg_mode_8:
                unreachable("XXX TODO: sort out how 8-bit constant encoding works");
                break;
        }
}

static void
mir_print_embedded_constant(midgard_instruction *ins, unsigned src_idx)
{
        unsigned type_size = mir_bytes_for_mode(ins->alu.reg_mode);
        midgard_vector_alu_src src;

        assert(src_idx <= 1);
        if (src_idx == 0)
                src = vector_alu_from_unsigned(ins->alu.src1);
        else
                src = vector_alu_from_unsigned(ins->alu.src2);

        unsigned *swizzle = ins->swizzle[src_idx];
        unsigned comp_mask = effective_writemask(&ins->alu, ins->mask);
        unsigned num_comp = util_bitcount(comp_mask);
        unsigned max_comp = 16 / type_size;
        bool first = true;

        printf("#");

        if (num_comp > 1)
                printf("vec%d(", num_comp);

        for (unsigned comp = 0; comp < max_comp; comp++) {
                if (!(comp_mask & (1 << comp)))
                        continue;

                if (first)
                        first = false;
                else
                        printf(", ");

                mir_print_constant_component(stdout, &ins->constants,
                                             swizzle[comp], ins->alu.reg_mode,
                                             src.half, src.mod, ins->alu.op);
        }

        if (num_comp > 1)
                printf(")");
}

void
mir_print_instruction(midgard_instruction *ins)
{
        printf("\t");

        if (midgard_is_branch_unit(ins->unit)) {
                const char *branch_target_names[] = {
                        "goto", "break", "continue", "discard"
                };

                printf("%s.", mir_get_unit(ins->unit));
                if (ins->branch.target_type == TARGET_DISCARD)
                        printf("discard.");
                else if (ins->writeout)
                        printf("write.");
                else if (ins->unit == ALU_ENAB_BR_COMPACT &&
                         !ins->branch.conditional)
                        printf("uncond.");
                else
                        printf("cond.");

                if (!ins->branch.conditional)
                        printf("always");
                else if (ins->branch.invert_conditional)
                        printf("false");
                else
                        printf("true");

                if (ins->branch.target_type != TARGET_DISCARD)
                        printf(" %s -> block(%d)\n",
                               branch_target_names[ins->branch.target_type],
                               ins->branch.target_block);

                return;
        }

        switch (ins->type) {
        case TAG_ALU_4: {
                midgard_alu_op op = ins->alu.op;
                const char *name = alu_opcode_props[op].name;

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

        unsigned r_constant = SSA_FIXED_REGISTER(REGISTER_CONSTANT);

        if (ins->src[0] == r_constant)
                mir_print_embedded_constant(ins, 0);
        else {
                mir_print_index(ins->src[0]);
                mir_print_swizzle(ins->swizzle[0]);
        }
        printf(", ");

        if (ins->has_inline_constant)
                printf("#%d", ins->inline_constant);
        else if (ins->src[1] == r_constant)
                mir_print_embedded_constant(ins, 1);
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

        if (ins->no_spill)
                printf(" /* no spill */");

        printf("\n");
}

/* Dumps MIR for a block or entire shader respective */

void
mir_print_block(midgard_block *block)
{
        printf("block%u: {\n", block->base.name);

        if (block->scheduled) {
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

        if (block->base.successors[0]) {
                printf(" -> ");
                pan_foreach_successor((&block->base), succ)
                        printf(" block%u ", succ->name);
        }

        printf(" from { ");
        mir_foreach_predecessor(block, pred)
                printf("block%u ", pred->base.name);
        printf("}");

        printf("\n\n");
}

void
mir_print_shader(compiler_context *ctx)
{
        mir_foreach_block(ctx, block) {
                mir_print_block((midgard_block *) block);
        }
}
