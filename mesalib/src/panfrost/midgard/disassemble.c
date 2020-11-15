/* Author(s):
 *   Connor Abbott
 *   Alyssa Rosenzweig
 *
 * Copyright (c) 2013 Connor Abbott (connor@abbott.cx)
 * Copyright (c) 2018 Alyssa Rosenzweig (alyssa@rosenzweig.io)
 * Copyright (C) 2019-2020 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include "midgard.h"
#include "midgard_ops.h"
#include "midgard_quirks.h"
#include "disassemble.h"
#include "helpers.h"
#include "util/bitscan.h"
#include "util/half_float.h"
#include "util/u_math.h"

#define DEFINE_CASE(define, str) case define: { fprintf(fp, str); break; }

static unsigned *midg_tags;
static bool is_instruction_int = false;

/* Stats */

static struct midgard_disasm_stats midg_stats;

/* Transform an expanded writemask (duplicated 8-bit format) into its condensed
 * form (one bit per component) */

static inline unsigned
condense_writemask(unsigned expanded_mask,
                   unsigned bits_per_component)
{
        if (bits_per_component == 8) {
                /* Duplicate every bit to go from 8 to 16-channel wrmask */
                unsigned omask = 0;

                for (unsigned i = 0; i < 8; ++i) {
                        if (expanded_mask & (1 << i))
                                omask |= (3 << (2 * i));
                }

                return omask;
        }

        unsigned slots_per_component = bits_per_component / 16;
        unsigned max_comp = (16 * 8) / bits_per_component;
        unsigned condensed_mask = 0;

        for (unsigned i = 0; i < max_comp; i++) {
                if (expanded_mask & (1 << (i * slots_per_component)))
                        condensed_mask |= (1 << i);
        }

        return condensed_mask;
}

static void
print_alu_opcode(FILE *fp, midgard_alu_op op)
{
        bool int_op = false;

        if (alu_opcode_props[op].name) {
                fprintf(fp, "%s", alu_opcode_props[op].name);

                int_op = midgard_is_integer_op(op);
        } else
                fprintf(fp, "alu_op_%02X", op);

        /* For constant analysis */
        is_instruction_int = int_op;
}

static void
print_ld_st_opcode(FILE *fp, midgard_load_store_op op)
{
        if (load_store_opcode_props[op].name)
                fprintf(fp, "%s", load_store_opcode_props[op].name);
        else
                fprintf(fp, "ldst_op_%02X", op);
}

static bool is_embedded_constant_half = false;
static bool is_embedded_constant_int = false;

static char
prefix_for_bits(unsigned bits)
{
        switch (bits) {
        case 8:
                return 'q';
        case 16:
                return 'h';
        case 64:
                return 'd';
        default:
                return 0;
        }
}

/* For static analysis to ensure all registers are written at least once before
 * use along the source code path (TODO: does this break done for complex CF?)
 */

uint16_t midg_ever_written = 0;

static void
print_reg(FILE *fp, unsigned reg, unsigned bits)
{
        /* Perform basic static analysis for expanding constants correctly */

        if (reg == 26) {
                is_embedded_constant_int = is_instruction_int;
                is_embedded_constant_half = (bits < 32);
        }

        unsigned uniform_reg = 23 - reg;
        bool is_uniform = false;

        /* For r8-r15, it could be a work or uniform. We distinguish based on
         * the fact work registers are ALWAYS written before use, but uniform
         * registers are NEVER written before use. */

        if ((reg >= 8 && reg < 16) && !(midg_ever_written & (1 << reg)))
                is_uniform = true;

        /* r16-r23 are always uniform */

        if (reg >= 16 && reg <= 23)
                is_uniform = true;

        /* Update the uniform count appropriately */

        if (is_uniform)
                midg_stats.uniform_count =
                        MAX2(uniform_reg + 1, midg_stats.uniform_count);

        char prefix = prefix_for_bits(bits);

        if (prefix)
                fputc(prefix, fp);

        fprintf(fp, "r%u", reg);
}

static char *outmod_names_float[4] = {
        "",
        ".pos",
        ".sat_signed",
        ".sat"
};

static char *outmod_names_int[4] = {
        ".isat",
        ".usat",
        "",
        ".hi"
};

static char *srcmod_names_int[4] = {
        "sext(",
        "zext(",
        "",
        "("
};

static void
print_outmod(FILE *fp, unsigned outmod, bool is_int)
{
        fprintf(fp, "%s", is_int ? outmod_names_int[outmod] :
               outmod_names_float[outmod]);
}

static void
print_quad_word(FILE *fp, uint32_t *words, unsigned tabs)
{
        unsigned i;

        for (i = 0; i < 4; i++)
                fprintf(fp, "0x%08X%s ", words[i], i == 3 ? "" : ",");

        fprintf(fp, "\n");
}

static const char components[16] = "xyzwefghijklmnop";

/* Helper to print 4 chars of a swizzle */
static void
print_swizzle_helper(FILE *fp, unsigned swizzle, unsigned offset)
{
        for (unsigned i = 0; i < 4; ++i) {
                unsigned c = (swizzle >> (i * 2)) & 3;
                c += offset;
                fprintf(fp, "%c", components[c]);
        }
}

/* Helper to print 8 chars of a swizzle, duplicating over */
static void
print_swizzle_helper_8(FILE *fp, unsigned swizzle, bool upper)
{
        for (unsigned i = 0; i < 4; ++i) {
                unsigned c = (swizzle >> (i * 2)) & 3;
                c *= 2;
                c += upper*8;
                fprintf(fp, "%c%c", components[c], components[c+1]);
        }
}

static void
print_swizzle_vec16(FILE *fp, unsigned swizzle, bool rep_high, bool rep_low,
                    midgard_dest_override override)
{
        fprintf(fp, ".");

        if (override == midgard_dest_override_upper) {
                if (rep_high)
                        fprintf(fp, " /* rep_high */ ");
                if (rep_low)
                        fprintf(fp, " /* rep_low */ ");

                if (!rep_high && rep_low)
                        print_swizzle_helper_8(fp, swizzle, true);
                else
                        print_swizzle_helper_8(fp, swizzle, false);
        } else {
                print_swizzle_helper_8(fp, swizzle, rep_high & 1);
                print_swizzle_helper_8(fp, swizzle, !(rep_low & 1));
        }
}

static void
print_swizzle_vec8(FILE *fp, unsigned swizzle, bool rep_high, bool rep_low, bool half)
{
        fprintf(fp, ".");

        /* TODO: Is it possible to unify half/full? */

        if (half) {
                print_swizzle_helper(fp, swizzle, (rep_low * 8));
                print_swizzle_helper(fp, swizzle, (rep_low * 8) + !rep_high * 4);
        } else {
                print_swizzle_helper(fp, swizzle, rep_high * 4);
                print_swizzle_helper(fp, swizzle, !rep_low * 4);
        }
}

static void
print_swizzle_vec4(FILE *fp, unsigned swizzle, bool rep_high, bool rep_low, bool half)
{
        if (rep_high)
                fprintf(fp, " /* rep_high */ ");

        if (!half && rep_low)
                fprintf(fp, " /* rep_low */ ");

        if (swizzle == 0xE4 && !half) return; /* xyzw */

        fprintf(fp, ".");
        print_swizzle_helper(fp, swizzle, rep_low * 4);
}
static void
print_swizzle_vec2(FILE *fp, unsigned swizzle, bool rep_high, bool rep_low, bool half)
{
        char *alphabet = "XY";

        if (half) {
                alphabet = rep_low ? "zw" : "xy";
        } else if (rep_low)
                fprintf(fp, " /* rep_low */ ");

        if (rep_high)
                fprintf(fp, " /* rep_high */ ");

        if (swizzle == 0xE4 && !half) return; /* XY */

        fprintf(fp, ".");

        for (unsigned i = 0; i < 4; i += 2) {
                unsigned a = (swizzle >> (i * 2)) & 3;
                unsigned b = (swizzle >> ((i+1) * 2)) & 3;

                /* Normally we're adjacent, but if there's an issue, don't make
                 * it ambiguous */

                if (b == (a + 1))
                        fprintf(fp, "%c", alphabet[a >> 1]);
                else
                        fprintf(fp, "[%c%c]", components[a], components[b]);
        }
}

static int
bits_for_mode(midgard_reg_mode mode)
{
        switch (mode) {
        case midgard_reg_mode_8:
                return 8;
        case midgard_reg_mode_16:
                return 16;
        case midgard_reg_mode_32:
                return 32;
        case midgard_reg_mode_64:
                return 64;
        default:
                unreachable("Invalid reg mode");
                return 0;
        }
}

static int
bits_for_mode_halved(midgard_reg_mode mode, bool half)
{
        unsigned bits = bits_for_mode(mode);

        if (half)
                bits >>= 1;

        return bits;
}

static void
print_scalar_constant(FILE *fp, unsigned src_binary,
                      const midgard_constants *consts,
                      midgard_scalar_alu *alu)
{
        midgard_scalar_alu_src *src = (midgard_scalar_alu_src *)&src_binary;
        assert(consts != NULL);

        fprintf(fp, "#");
        mir_print_constant_component(fp, consts, src->component,
                                     src->full ?
                                     midgard_reg_mode_32 : midgard_reg_mode_16,
                                     false, src->mod, alu->op);
}

static void
print_vector_constants(FILE *fp, unsigned src_binary,
                       const midgard_constants *consts,
                       midgard_vector_alu *alu)
{
        midgard_vector_alu_src *src = (midgard_vector_alu_src *)&src_binary;
        unsigned bits = bits_for_mode_halved(alu->reg_mode, src->half);
        unsigned max_comp = (sizeof(*consts) * 8) / bits;
        unsigned comp_mask, num_comp = 0;

        assert(consts);
        assert(max_comp <= 16);

        comp_mask = effective_writemask(alu->op, condense_writemask(alu->mask, bits));
        num_comp = util_bitcount(comp_mask);

        fprintf(fp, "<");
        bool first = true;

	for (unsigned i = 0; i < max_comp; ++i) {
                if (!(comp_mask & (1 << i))) continue;

                unsigned c = (src->swizzle >> (i * 2)) & 3;

                if (bits == 16 && !src->half) {
                        if (i < 4)
                                c += (src->rep_high * 4);
                        else
                                c += (!src->rep_low * 4);
                } else if (bits == 32 && !src->half) {
                        /* Implicitly ok */
                } else if (bits == 8) {
                        assert (!src->half);
                        unsigned index = (i >> 1) & 3;
                        unsigned base = (src->swizzle >> (index * 2)) & 3;
                        c = base * 2;

                        if (i < 8)
                                c += (src->rep_high) * 8;
                        else
                                c += (!src->rep_low) * 8;

                        /* We work on twos, actually */
                        if (i & 1)
                                c++;
                } else {
                        printf(" (%d%d%d)", src->rep_low, src->rep_high, src->half);
                }

                if (first)
                        first = false;
                else
                        fprintf(fp, ", ");

                mir_print_constant_component(fp, consts, c, alu->reg_mode,
                                             src->half, src->mod, alu->op);
        }

        if (num_comp > 1)
                fprintf(fp, ">");
}

static void
print_srcmod(FILE *fp, bool is_int, unsigned mod, bool scalar)
{
        /* Modifiers change meaning depending on the op's context */

        midgard_int_mod int_mod = mod;

        if (is_int) {
                if (scalar && mod == 2) {
                        fprintf(fp, "unk2");
                }

                fprintf(fp, "%s", srcmod_names_int[int_mod]);
        } else {
                if (mod & MIDGARD_FLOAT_MOD_NEG)
                        fprintf(fp, "-");

                if (mod & MIDGARD_FLOAT_MOD_ABS)
                        fprintf(fp, "abs(");
        }
}

static void
print_srcmod_end(FILE *fp, bool is_int, unsigned mod, unsigned bits)
{
        /* Since we wrapped with a function-looking thing */

        if (is_int && mod == midgard_int_shift)
                fprintf(fp, ") << %u", bits);
        else if ((is_int && (mod != midgard_int_normal))
                 || (!is_int && mod & MIDGARD_FLOAT_MOD_ABS))
                fprintf(fp, ")");
}

static void
print_vector_src(FILE *fp, unsigned src_binary,
                 midgard_reg_mode mode, unsigned reg,
                 midgard_dest_override override, bool is_int)
{
        midgard_vector_alu_src *src = (midgard_vector_alu_src *)&src_binary;
        print_srcmod(fp, is_int, src->mod, false);

        //register
        unsigned bits = bits_for_mode_halved(mode, src->half);
        print_reg(fp, reg, bits);

        /* When the source was stepped down via `half`, rep_low means "higher
         * half" and rep_high is never seen. When it's not native,
         * rep_low/rep_high are for, well, replication */

        if (mode == midgard_reg_mode_8) {
                assert(!src->half);
                print_swizzle_vec16(fp, src->swizzle, src->rep_high, src->rep_low, override);
        } else if (mode == midgard_reg_mode_16) {
                print_swizzle_vec8(fp, src->swizzle, src->rep_high, src->rep_low, src->half);
        } else if (mode == midgard_reg_mode_32) {
                print_swizzle_vec4(fp, src->swizzle, src->rep_high, src->rep_low, src->half);
        } else if (mode == midgard_reg_mode_64) {
                print_swizzle_vec2(fp, src->swizzle, src->rep_high, src->rep_low, src->half);
        }

        print_srcmod_end(fp, is_int, src->mod, bits);
}

static uint16_t
decode_vector_imm(unsigned src2_reg, unsigned imm)
{
        uint16_t ret;
        ret = src2_reg << 11;
        ret |= (imm & 0x7) << 8;
        ret |= (imm >> 3) & 0xFF;
        return ret;
}

static void
print_immediate(FILE *fp, uint16_t imm)
{
        if (is_instruction_int)
                fprintf(fp, "#%u", imm);
        else
                fprintf(fp, "#%g", _mesa_half_to_float(imm));
}

static void
update_dest(unsigned reg)
{
        /* We should record writes as marking this as a work register. Store
         * the max register in work_count; we'll add one at the end */

        if (reg < 16) {
                midg_stats.work_count = MAX2(reg, midg_stats.work_count);
                midg_ever_written |= (1 << reg);
        }
}

static void
print_dest(FILE *fp, unsigned reg, midgard_reg_mode mode, midgard_dest_override override)
{
        /* Depending on the mode and override, we determine the type of
         * destination addressed. Absent an override, we address just the
         * type of the operation itself */

        unsigned bits = bits_for_mode(mode);

        if (override != midgard_dest_override_none)
                bits /= 2;

        update_dest(reg);
        print_reg(fp, reg, bits);
}

static void
print_mask_vec16(FILE *fp, uint8_t mask, midgard_dest_override override)
{
        fprintf(fp, ".");

        for (unsigned i = 0; i < 8; i++) {
                if (mask & (1 << i))
                        fprintf(fp, "%c%c",
                               components[i*2 + 0],
                               components[i*2 + 1]);
        }
}

/* For 16-bit+ masks, we read off from the 8-bit mask field. For 16-bit (vec8),
 * it's just one bit per channel, easy peasy. For 32-bit (vec4), it's one bit
 * per channel with one duplicate bit in the middle. For 64-bit (vec2), it's
 * one-bit per channel with _3_ duplicate bits in the middle. Basically, just
 * subdividing the 128-bit word in 16-bit increments. For 64-bit, we uppercase
 * the mask to make it obvious what happened */

static void
print_mask(FILE *fp, uint8_t mask, unsigned bits, midgard_dest_override override)
{
        if (bits == 8) {
                print_mask_vec16(fp, mask, override);
                return;
        }

        /* Skip 'complete' masks */

        if (override == midgard_dest_override_none)
                if (bits >= 32 && mask == 0xFF) return;

        fprintf(fp, ".");

        unsigned skip = (bits / 16);
        bool uppercase = bits > 32;
        bool tripped = false;

        /* To apply an upper destination override, we "shift" the alphabet.
         * E.g. with an upper override on 32-bit, instead of xyzw, print efgh.
         * For upper 16-bit, instead of xyzwefgh, print ijklmnop */

        const char *alphabet = components;

        if (override == midgard_dest_override_upper)
                alphabet += (128 / bits);

        for (unsigned i = 0; i < 8; i += skip) {
                bool a = (mask & (1 << i)) != 0;

                for (unsigned j = 1; j < skip; ++j) {
                        bool dupe = (mask & (1 << (i + j))) != 0;
                        tripped |= (dupe != a);
                }

                if (a) {
                        char c = alphabet[i / skip];

                        if (uppercase)
                                c = toupper(c);

                        fprintf(fp, "%c", c);
                }
        }

        if (tripped)
                fprintf(fp, " /* %X */", mask);
}

/* Prints the 4-bit masks found in texture and load/store ops, as opposed to
 * the 8-bit masks found in (vector) ALU ops. Supports texture-style 16-bit
 * mode as well, but not load/store-style 16-bit mode. */

static void
print_mask_4(FILE *fp, unsigned mask, bool upper)
{
        if (mask == 0xF) {
                if (upper)
                        fprintf(fp, "'");

                return;
        }

        fprintf(fp, ".");

        for (unsigned i = 0; i < 4; ++i) {
                bool a = (mask & (1 << i)) != 0;
                if (a)
                        fprintf(fp, "%c", components[i + (upper ? 4 : 0)]);
        }
}

static void
print_vector_field(FILE *fp, const char *name, uint16_t *words, uint16_t reg_word,
                   const midgard_constants *consts, unsigned tabs)
{
        midgard_reg_info *reg_info = (midgard_reg_info *)&reg_word;
        midgard_vector_alu *alu_field = (midgard_vector_alu *) words;
        midgard_reg_mode mode = alu_field->reg_mode;
        unsigned override = alu_field->dest_override;

        /* For now, prefix instruction names with their unit, until we
         * understand how this works on a deeper level */
        fprintf(fp, "%s.", name);

        print_alu_opcode(fp, alu_field->op);

        /* Postfix with the size to disambiguate if necessary */
        char postfix = prefix_for_bits(bits_for_mode(mode));
        bool size_ambiguous = override != midgard_dest_override_none;

        if (size_ambiguous)
                fprintf(fp, "%c", postfix ? postfix : 'r');

        /* Print the outmod, if there is one */
        print_outmod(fp, alu_field->outmod,
                     midgard_is_integer_out_op(alu_field->op));

        fprintf(fp, " ");

        /* Mask denoting status of 8-lanes */
        uint8_t mask = alu_field->mask;

        /* First, print the destination */
        print_dest(fp, reg_info->out_reg, mode, alu_field->dest_override);

        if (override != midgard_dest_override_none) {
                bool modeable = (mode != midgard_reg_mode_8);
                bool known = override != 0x3; /* Unused value */

                if (!(modeable && known))
                        fprintf(fp, "/* do%u */ ", override);
        }

        /* Instructions like fdot4 do *not* replicate, ensure the
         * mask is of only a single component */

        unsigned rep = GET_CHANNEL_COUNT(alu_opcode_props[alu_field->op].props);

        if (rep) {
                unsigned comp_mask = condense_writemask(mask, bits_for_mode(mode));
                unsigned num_comp = util_bitcount(comp_mask);
                if (num_comp != 1)
                        fprintf(fp, "/* err too many components */");
        }
        print_mask(fp, mask, bits_for_mode(mode), override);

        fprintf(fp, ", ");

        bool is_int = midgard_is_integer_op(alu_field->op);

        if (reg_info->src1_reg == 26)
                print_vector_constants(fp, alu_field->src1, consts, alu_field);
        else
                print_vector_src(fp, alu_field->src1, mode, reg_info->src1_reg, override, is_int);

        fprintf(fp, ", ");

        if (reg_info->src2_imm) {
                uint16_t imm = decode_vector_imm(reg_info->src2_reg, alu_field->src2 >> 2);
                print_immediate(fp, imm);
        } else if (reg_info->src2_reg == 26) {
                print_vector_constants(fp, alu_field->src2, consts, alu_field);
        } else {
                print_vector_src(fp, alu_field->src2, mode,
                                 reg_info->src2_reg, override, is_int);
        }

        midg_stats.instruction_count++;
        fprintf(fp, "\n");
}

static void
print_scalar_src(FILE *fp, bool is_int, unsigned src_binary, unsigned reg)
{
        midgard_scalar_alu_src *src = (midgard_scalar_alu_src *)&src_binary;

        print_srcmod(fp, is_int, src->mod, true);
        print_reg(fp, reg, src->full ? 32 : 16);

        unsigned c = src->component;

        if (src->full) {
                assert((c & 1) == 0);
                c >>= 1;
        }

        fprintf(fp, ".%c", components[c]);

        print_srcmod_end(fp, is_int, src->mod, src->full ? 32 : 16);
}

static uint16_t
decode_scalar_imm(unsigned src2_reg, unsigned imm)
{
        uint16_t ret;
        ret = src2_reg << 11;
        ret |= (imm & 3) << 9;
        ret |= (imm & 4) << 6;
        ret |= (imm & 0x38) << 2;
        ret |= imm >> 6;
        return ret;
}

static void
print_scalar_field(FILE *fp, const char *name, uint16_t *words, uint16_t reg_word,
                   const midgard_constants *consts, unsigned tabs)
{
        midgard_reg_info *reg_info = (midgard_reg_info *)&reg_word;
        midgard_scalar_alu *alu_field = (midgard_scalar_alu *) words;

        if (alu_field->unknown)
                fprintf(fp, "scalar ALU unknown bit set\n");

        fprintf(fp, "%s.", name);
        print_alu_opcode(fp, alu_field->op);
        print_outmod(fp, alu_field->outmod,
                     midgard_is_integer_out_op(alu_field->op));
        fprintf(fp, " ");

        bool full = alu_field->output_full;
        update_dest(reg_info->out_reg);
        print_reg(fp, reg_info->out_reg, full ? 32 : 16);
        unsigned c = alu_field->output_component;
        bool is_int = midgard_is_integer_op(alu_field->op);

        if (full) {
                assert((c & 1) == 0);
                c >>= 1;
        }

        fprintf(fp, ".%c, ", components[c]);

        if (reg_info->src1_reg == 26)
                print_scalar_constant(fp, alu_field->src1, consts, alu_field);
        else
                print_scalar_src(fp, is_int, alu_field->src1, reg_info->src1_reg);

        fprintf(fp, ", ");

        if (reg_info->src2_imm) {
                uint16_t imm = decode_scalar_imm(reg_info->src2_reg,
                                                 alu_field->src2);
                print_immediate(fp, imm);
	} else if (reg_info->src2_reg == 26) {
                print_scalar_constant(fp, alu_field->src2, consts, alu_field);
        } else
                print_scalar_src(fp, is_int, alu_field->src2, reg_info->src2_reg);

        midg_stats.instruction_count++;
        fprintf(fp, "\n");
}

static void
print_branch_op(FILE *fp, unsigned op)
{
        switch (op) {
        case midgard_jmp_writeout_op_branch_uncond:
                fprintf(fp, "uncond.");
                break;

        case midgard_jmp_writeout_op_branch_cond:
                fprintf(fp, "cond.");
                break;

        case midgard_jmp_writeout_op_writeout:
                fprintf(fp, "write.");
                break;

        case midgard_jmp_writeout_op_tilebuffer_pending:
                fprintf(fp, "tilebuffer.");
                break;

        case midgard_jmp_writeout_op_discard:
                fprintf(fp, "discard.");
                break;

        default:
                fprintf(fp, "unk%u.", op);
                break;
        }
}

static void
print_branch_cond(FILE *fp, int cond)
{
        switch (cond) {
        case midgard_condition_write0:
                fprintf(fp, "write0");
                break;

        case midgard_condition_false:
                fprintf(fp, "false");
                break;

        case midgard_condition_true:
                fprintf(fp, "true");
                break;

        case midgard_condition_always:
                fprintf(fp, "always");
                break;

        default:
                fprintf(fp, "unk%X", cond);
                break;
        }
}

static bool
print_compact_branch_writeout_field(FILE *fp, uint16_t word)
{
        midgard_jmp_writeout_op op = word & 0x7;
        midg_stats.instruction_count++;

        switch (op) {
        case midgard_jmp_writeout_op_branch_uncond: {
                midgard_branch_uncond br_uncond;
                memcpy((char *) &br_uncond, (char *) &word, sizeof(br_uncond));
                fprintf(fp, "br.uncond ");

                if (br_uncond.unknown != 1)
                        fprintf(fp, "unknown:%u, ", br_uncond.unknown);

                if (br_uncond.offset >= 0)
                        fprintf(fp, "+");

                fprintf(fp, "%d -> %s", br_uncond.offset,
                                midgard_tag_props[br_uncond.dest_tag].name);
                fprintf(fp, "\n");

                return br_uncond.offset >= 0;
        }

        case midgard_jmp_writeout_op_branch_cond:
        case midgard_jmp_writeout_op_writeout:
        case midgard_jmp_writeout_op_discard:
        default: {
                midgard_branch_cond br_cond;
                memcpy((char *) &br_cond, (char *) &word, sizeof(br_cond));

                fprintf(fp, "br.");

                print_branch_op(fp, br_cond.op);
                print_branch_cond(fp, br_cond.cond);

                fprintf(fp, " ");

                if (br_cond.offset >= 0)
                        fprintf(fp, "+");

                fprintf(fp, "%d -> %s", br_cond.offset,
                                midgard_tag_props[br_cond.dest_tag].name);
                fprintf(fp, "\n");

                return br_cond.offset >= 0;
        }
        }

        return false;
}

static bool
print_extended_branch_writeout_field(FILE *fp, uint8_t *words, unsigned next)
{
        midgard_branch_extended br;
        memcpy((char *) &br, (char *) words, sizeof(br));

        fprintf(fp, "brx.");

        print_branch_op(fp, br.op);

        /* Condition codes are a LUT in the general case, but simply repeated 8 times for single-channel conditions.. Check this. */

        bool single_channel = true;

        for (unsigned i = 0; i < 16; i += 2) {
                single_channel &= (((br.cond >> i) & 0x3) == (br.cond & 0x3));
        }

        if (single_channel)
                print_branch_cond(fp, br.cond & 0x3);
        else
                fprintf(fp, "lut%X", br.cond);

        if (br.unknown)
                fprintf(fp, ".unknown%u", br.unknown);

        fprintf(fp, " ");

        if (br.offset >= 0)
                fprintf(fp, "+");

        fprintf(fp, "%d -> %s\n", br.offset,
                        midgard_tag_props[br.dest_tag].name);

        unsigned I = next + br.offset * 4;

        if (midg_tags[I] && midg_tags[I] != br.dest_tag) {
                fprintf(fp, "\t/* XXX TAG ERROR: jumping to %s but tagged %s \n",
                        midgard_tag_props[br.dest_tag].name,
                        midgard_tag_props[midg_tags[I]].name);
        }

        midg_tags[I] = br.dest_tag;

        midg_stats.instruction_count++;
        return br.offset >= 0;
}

static unsigned
num_alu_fields_enabled(uint32_t control_word)
{
        unsigned ret = 0;

        if ((control_word >> 17) & 1)
                ret++;

        if ((control_word >> 19) & 1)
                ret++;

        if ((control_word >> 21) & 1)
                ret++;

        if ((control_word >> 23) & 1)
                ret++;

        if ((control_word >> 25) & 1)
                ret++;

        return ret;
}

static bool
print_alu_word(FILE *fp, uint32_t *words, unsigned num_quad_words,
               unsigned tabs, unsigned next)
{
        uint32_t control_word = words[0];
        uint16_t *beginning_ptr = (uint16_t *)(words + 1);
        unsigned num_fields = num_alu_fields_enabled(control_word);
        uint16_t *word_ptr = beginning_ptr + num_fields;
        unsigned num_words = 2 + num_fields;
        const midgard_constants *consts = NULL;
        bool branch_forward = false;

        if ((control_word >> 17) & 1)
                num_words += 3;

        if ((control_word >> 19) & 1)
                num_words += 2;

        if ((control_word >> 21) & 1)
                num_words += 3;

        if ((control_word >> 23) & 1)
                num_words += 2;

        if ((control_word >> 25) & 1)
                num_words += 3;

        if ((control_word >> 26) & 1)
                num_words += 1;

        if ((control_word >> 27) & 1)
                num_words += 3;

        if (num_quad_words > (num_words + 7) / 8) {
                assert(num_quad_words == (num_words + 15) / 8);
                //Assume that the extra quadword is constants
                consts = (midgard_constants *)(words + (4 * num_quad_words - 4));
        }

        if ((control_word >> 16) & 1)
                fprintf(fp, "unknown bit 16 enabled\n");

        if ((control_word >> 17) & 1) {
                print_vector_field(fp, "vmul", word_ptr, *beginning_ptr, consts, tabs);
                beginning_ptr += 1;
                word_ptr += 3;
        }

        if ((control_word >> 18) & 1)
                fprintf(fp, "unknown bit 18 enabled\n");

        if ((control_word >> 19) & 1) {
                print_scalar_field(fp, "sadd", word_ptr, *beginning_ptr, consts, tabs);
                beginning_ptr += 1;
                word_ptr += 2;
        }

        if ((control_word >> 20) & 1)
                fprintf(fp, "unknown bit 20 enabled\n");

        if ((control_word >> 21) & 1) {
                print_vector_field(fp, "vadd", word_ptr, *beginning_ptr, consts, tabs);
                beginning_ptr += 1;
                word_ptr += 3;
        }

        if ((control_word >> 22) & 1)
                fprintf(fp, "unknown bit 22 enabled\n");

        if ((control_word >> 23) & 1) {
                print_scalar_field(fp, "smul", word_ptr, *beginning_ptr, consts, tabs);
                beginning_ptr += 1;
                word_ptr += 2;
        }

        if ((control_word >> 24) & 1)
                fprintf(fp, "unknown bit 24 enabled\n");

        if ((control_word >> 25) & 1) {
                print_vector_field(fp, "lut", word_ptr, *beginning_ptr, consts, tabs);
                word_ptr += 3;
        }

        if ((control_word >> 26) & 1) {
                branch_forward |= print_compact_branch_writeout_field(fp, *word_ptr);
                word_ptr += 1;
        }

        if ((control_word >> 27) & 1) {
                branch_forward |= print_extended_branch_writeout_field(fp, (uint8_t *) word_ptr, next);
                word_ptr += 3;
        }

        if (consts)
                fprintf(fp, "uconstants 0x%X, 0x%X, 0x%X, 0x%X\n",
                        consts->u32[0], consts->u32[1],
                        consts->u32[2], consts->u32[3]);

        return branch_forward;
}

static void
print_varying_parameters(FILE *fp, midgard_load_store_word *word)
{
        midgard_varying_parameter param;
        unsigned v = word->varying_parameters;
        memcpy(&param, &v, sizeof(param));

        if (param.is_varying) {
                /* If a varying, there are qualifiers */
                if (param.flat)
                        fprintf(fp, ".flat");

                if (param.interpolation != midgard_interp_default) {
                        if (param.interpolation == midgard_interp_centroid)
                                fprintf(fp, ".centroid");
                        else if (param.interpolation == midgard_interp_sample)
                                fprintf(fp, ".sample");
                        else
                                fprintf(fp, ".interp%d", param.interpolation);
                }

                if (param.modifier != midgard_varying_mod_none) {
                        if (param.modifier == midgard_varying_mod_perspective_w)
                                fprintf(fp, ".perspectivew");
                        else if (param.modifier == midgard_varying_mod_perspective_z)
                                fprintf(fp, ".perspectivez");
                        else
                                fprintf(fp, ".mod%d", param.modifier);
                }
        } else if (param.flat || param.interpolation || param.modifier) {
                fprintf(fp, " /* is_varying not set but varying metadata attached */");
        }

        if (param.zero0 || param.zero1 || param.zero2)
                fprintf(fp, " /* zero tripped, %u %u %u */ ", param.zero0, param.zero1, param.zero2);
}

static bool
is_op_varying(unsigned op)
{
        switch (op) {
        case midgard_op_st_vary_16:
        case midgard_op_st_vary_32:
        case midgard_op_st_vary_32i:
        case midgard_op_st_vary_32u:
        case midgard_op_ld_vary_16:
        case midgard_op_ld_vary_32:
        case midgard_op_ld_vary_32i:
        case midgard_op_ld_vary_32u:
                return true;
        }

        return false;
}

static bool
is_op_attribute(unsigned op)
{
        switch (op) {
        case midgard_op_ld_attr_16:
        case midgard_op_ld_attr_32:
        case midgard_op_ld_attr_32i:
        case midgard_op_ld_attr_32u:
                return true;
        }

        return false;
}

static void
print_load_store_arg(FILE *fp, uint8_t arg, unsigned index)
{
        /* Try to interpret as a register */
        midgard_ldst_register_select sel;
        memcpy(&sel, &arg, sizeof(arg));

        /* If unknown is set, we're not sure what this is or how to
         * interpret it. But if it's zero, we get it. */

        if (sel.unknown) {
                fprintf(fp, "0x%02X", arg);
                return;
        }

        unsigned reg = REGISTER_LDST_BASE + sel.select;
        char comp = components[sel.component];

        fprintf(fp, "r%u.%c", reg, comp);

        /* Only print a shift if it's non-zero. Shifts only make sense for the
         * second index. For the first, we're not sure what it means yet */

        if (index == 1) {
                if (sel.shift)
                        fprintf(fp, " << %u", sel.shift);
        } else {
                fprintf(fp, " /* %X */", sel.shift);
        }
}

static void
update_stats(signed *stat, unsigned address)
{
        if (*stat >= 0)
                *stat = MAX2(*stat, address + 1);
}

static void
print_load_store_instr(FILE *fp, uint64_t data,
                       unsigned tabs)
{
        midgard_load_store_word *word = (midgard_load_store_word *) &data;

        print_ld_st_opcode(fp, word->op);

        unsigned address = word->address;

        if (is_op_varying(word->op)) {
                print_varying_parameters(fp, word);

                /* Do some analysis: check if direct cacess */

                if ((word->arg_2 == 0x1E) && midg_stats.varying_count >= 0)
                        update_stats(&midg_stats.varying_count, address);
                else
                        midg_stats.varying_count = -16;
        } else if (is_op_attribute(word->op)) {
                if ((word->arg_2 == 0x1E) && midg_stats.attribute_count >= 0)
                        update_stats(&midg_stats.attribute_count, address);
                else
                        midg_stats.attribute_count = -16;
        }

        fprintf(fp, " r%u", word->reg + (OP_IS_STORE(word->op) ? 26 : 0));
        print_mask_4(fp, word->mask, false);

        if (!OP_IS_STORE(word->op))
                update_dest(word->reg);

        bool is_ubo = OP_IS_UBO_READ(word->op);

        if (is_ubo) {
                /* UBOs use their own addressing scheme */

                int lo = word->varying_parameters >> 7;
                int hi = word->address;

                /* TODO: Combine fields logically */
                address = (hi << 3) | lo;
        }

        fprintf(fp, ", %u", address);

        print_swizzle_vec4(fp, word->swizzle, false, false, false);

        fprintf(fp, ", ");

        if (is_ubo) {
                fprintf(fp, "ubo%u", word->arg_1);
                update_stats(&midg_stats.uniform_buffer_count, word->arg_1);
        } else
                print_load_store_arg(fp, word->arg_1, 0);

        fprintf(fp, ", ");
        print_load_store_arg(fp, word->arg_2, 1);
        fprintf(fp, " /* %X */\n", word->varying_parameters);

        midg_stats.instruction_count++;
}

static void
print_load_store_word(FILE *fp, uint32_t *word, unsigned tabs)
{
        midgard_load_store *load_store = (midgard_load_store *) word;

        if (load_store->word1 != 3) {
                print_load_store_instr(fp, load_store->word1, tabs);
        }

        if (load_store->word2 != 3) {
                print_load_store_instr(fp, load_store->word2, tabs);
        }
}

static void
print_texture_reg_select(FILE *fp, uint8_t u, unsigned base)
{
        midgard_tex_register_select sel;
        memcpy(&sel, &u, sizeof(u));

        if (!sel.full)
                fprintf(fp, "h");

        fprintf(fp, "r%u", base + sel.select);

        unsigned component = sel.component;

        /* Use the upper half in half-reg mode */
        if (sel.upper) {
                assert(!sel.full);
                component += 4;
        }

        fprintf(fp, ".%c", components[component]);

        assert(sel.zero == 0);
}

static void
print_texture_format(FILE *fp, int format)
{
        /* Act like a modifier */
        fprintf(fp, ".");

        switch (format) {
                DEFINE_CASE(1, "1d");
                DEFINE_CASE(2, "2d");
                DEFINE_CASE(3, "3d");
                DEFINE_CASE(0, "cube");

        default:
                unreachable("Bad format");
        }
}

static bool
midgard_op_has_helpers(unsigned op)
{
        switch (op) {
        case TEXTURE_OP_NORMAL:
        case TEXTURE_OP_DERIVATIVE:
                return true;
        default:
                return false;
        }
}

static void
print_texture_op(FILE *fp, unsigned op)
{
        switch (op) {
                DEFINE_CASE(TEXTURE_OP_NORMAL, "texture");
                DEFINE_CASE(TEXTURE_OP_LOD, "textureLod");
                DEFINE_CASE(TEXTURE_OP_TEXEL_FETCH, "texelFetch");
                DEFINE_CASE(TEXTURE_OP_BARRIER, "barrier");
                DEFINE_CASE(TEXTURE_OP_DERIVATIVE, "derivative");

        default:
                fprintf(fp, "tex_%X", op);
                break;
        }
}

static bool
texture_op_takes_bias(unsigned op)
{
        return op == TEXTURE_OP_NORMAL;
}

static char
sampler_type_name(enum mali_sampler_type t)
{
        switch (t) {
        case MALI_SAMPLER_FLOAT:
                return 'f';
        case MALI_SAMPLER_UNSIGNED:
                return 'u';
        case MALI_SAMPLER_SIGNED:
                return 'i';
        default:
                return '?';
        }

}

static void
print_texture_barrier(FILE *fp, uint32_t *word)
{
        midgard_texture_barrier_word *barrier = (midgard_texture_barrier_word *) word;

        if (barrier->type != TAG_TEXTURE_4_BARRIER)
                fprintf(fp, "/* barrier tag %X != tex/bar */ ", barrier->type);

        if (!barrier->cont)
                fprintf(fp, "/* cont missing? */");

        if (!barrier->last)
                fprintf(fp, "/* last missing? */");

        if (barrier->zero1)
                fprintf(fp, "/* zero1 = 0x%X */ ", barrier->zero1);

        if (barrier->zero2)
                fprintf(fp, "/* zero2 = 0x%X */ ", barrier->zero2);

        if (barrier->zero3)
                fprintf(fp, "/* zero3 = 0x%X */ ", barrier->zero3);

        if (barrier->zero4)
                fprintf(fp, "/* zero4 = 0x%X */ ", barrier->zero4);

        if (barrier->zero5)
                fprintf(fp, "/* zero4 = 0x%" PRIx64 " */ ", barrier->zero5);

        if (barrier->out_of_order)
                fprintf(fp, ".ooo%u", barrier->out_of_order);

        fprintf(fp, "\n");
}

#undef DEFINE_CASE

static const char *
texture_mode(enum mali_texture_mode mode)
{
        switch (mode) {
        case TEXTURE_NORMAL: return "";
        case TEXTURE_SHADOW: return ".shadow";
        case TEXTURE_GATHER_SHADOW: return ".gather.shadow";
        case TEXTURE_GATHER_X: return ".gatherX";
        case TEXTURE_GATHER_Y: return ".gatherY";
        case TEXTURE_GATHER_Z: return ".gatherZ";
        case TEXTURE_GATHER_W: return ".gatherW";
        default: return "unk";
        }
}

static const char *
derivative_mode(enum mali_derivative_mode mode)
{
        switch (mode) {
        case TEXTURE_DFDX: return ".x";
        case TEXTURE_DFDY: return ".y";
        default: return "unk";
        }
}

static void
print_texture_word(FILE *fp, uint32_t *word, unsigned tabs, unsigned in_reg_base, unsigned out_reg_base)
{
        midgard_texture_word *texture = (midgard_texture_word *) word;
        midg_stats.helper_invocations |= midgard_op_has_helpers(texture->op);

        /* Broad category of texture operation in question */
        print_texture_op(fp, texture->op);

        /* Barriers use a dramatically different code path */
        if (texture->op == TEXTURE_OP_BARRIER) {
                print_texture_barrier(fp, word);
                return;
        } else if (texture->type == TAG_TEXTURE_4_BARRIER)
                fprintf (fp, "/* nonbarrier had tex/bar tag */ ");
        else if (texture->type == TAG_TEXTURE_4_VTX)
                fprintf (fp, ".vtx");

        if (texture->op == TEXTURE_OP_DERIVATIVE)
                fprintf(fp, "%s", derivative_mode(texture->mode));
        else
                fprintf(fp, "%s", texture_mode(texture->mode));

        /* Specific format in question */
        print_texture_format(fp, texture->format);

        /* Instruction "modifiers" parallel the ALU instructions. */

        if (texture->cont)
                fprintf(fp, ".cont");

        if (texture->last)
                fprintf(fp, ".last");

        if (texture->out_of_order)
                fprintf(fp, ".ooo%u", texture->out_of_order);

        /* Output modifiers are always interpreted floatly */
        print_outmod(fp, texture->outmod, false);

        fprintf(fp, " %sr%u", texture->out_full ? "" : "h",
                        out_reg_base + texture->out_reg_select);
        print_mask_4(fp, texture->mask, texture->out_upper);
        assert(!(texture->out_full && texture->out_upper));
        fprintf(fp, ", ");

        /* Depending on whether we read from textures directly or indirectly,
         * we may be able to update our analysis */

        if (texture->texture_register) {
                fprintf(fp, "texture[");
                print_texture_reg_select(fp, texture->texture_handle, in_reg_base);
                fprintf(fp, "], ");

                /* Indirect, tut tut */
                midg_stats.texture_count = -16;
        } else {
                fprintf(fp, "texture%u, ", texture->texture_handle);
                update_stats(&midg_stats.texture_count, texture->texture_handle);
        }

        /* Print the type, GL style */
        fprintf(fp, "%csampler", sampler_type_name(texture->sampler_type));

        if (texture->sampler_register) {
                fprintf(fp, "[");
                print_texture_reg_select(fp, texture->sampler_handle, in_reg_base);
                fprintf(fp, "]");

                midg_stats.sampler_count = -16;
        } else {
                fprintf(fp, "%u", texture->sampler_handle);
                update_stats(&midg_stats.sampler_count, texture->sampler_handle);
        }

        print_swizzle_vec4(fp, texture->swizzle, false, false, false);
        fprintf(fp, ", %sr%u", texture->in_reg_full ? "" : "h", in_reg_base + texture->in_reg_select);
        assert(!(texture->in_reg_full && texture->in_reg_upper));

        /* TODO: integrate with swizzle */
        if (texture->in_reg_upper)
                fprintf(fp, "'");

        print_swizzle_vec4(fp, texture->in_reg_swizzle, false, false, false);

        /* There is *always* an offset attached. Of
         * course, that offset is just immediate #0 for a
         * GLES call that doesn't take an offset. If there
         * is a non-negative non-zero offset, this is
         * specified in immediate offset mode, with the
         * values in the offset_* fields as immediates. If
         * this is a negative offset, we instead switch to
         * a register offset mode, where the offset_*
         * fields become register triplets */

        if (texture->offset_register) {
                fprintf(fp, " + ");

                bool full = texture->offset & 1;
                bool select = texture->offset & 2;
                bool upper = texture->offset & 4;

                fprintf(fp, "%sr%u", full ? "" : "h", in_reg_base + select);
                assert(!(texture->out_full && texture->out_upper));

                /* TODO: integrate with swizzle */
                if (upper)
                        fprintf(fp, "'");

                print_swizzle_vec4(fp, texture->offset >> 3, false, false, false);

                fprintf(fp, ", ");
        } else if (texture->offset) {
                /* Only select ops allow negative immediate offsets, verify */

                signed offset_x = (texture->offset & 0xF);
                signed offset_y = ((texture->offset >> 4) & 0xF);
                signed offset_z = ((texture->offset >> 8) & 0xF);

                bool neg_x = offset_x < 0;
                bool neg_y = offset_y < 0;
                bool neg_z = offset_z < 0;
                bool any_neg = neg_x || neg_y || neg_z;

                if (any_neg && texture->op != TEXTURE_OP_TEXEL_FETCH)
                        fprintf(fp, "/* invalid negative */ ");

                /* Regardless, just print the immediate offset */

                fprintf(fp, " + <%d, %d, %d>, ", offset_x, offset_y, offset_z);
        } else {
                fprintf(fp, ", ");
        }

        char lod_operand = texture_op_takes_bias(texture->op) ? '+' : '=';

        if (texture->lod_register) {
                fprintf(fp, "lod %c ", lod_operand);
                print_texture_reg_select(fp, texture->bias, in_reg_base);
                fprintf(fp, ", ");

                if (texture->bias_int)
                        fprintf(fp, " /* bias_int = 0x%X */", texture->bias_int);
        } else if (texture->op == TEXTURE_OP_TEXEL_FETCH) {
                /* For texel fetch, the int LOD is in the fractional place and
                 * there is no fraction. We *always* have an explicit LOD, even
                 * if it's zero. */

                if (texture->bias_int)
                        fprintf(fp, " /* bias_int = 0x%X */ ", texture->bias_int);

                fprintf(fp, "lod = %u, ", texture->bias);
        } else if (texture->bias || texture->bias_int) {
                signed bias_int = texture->bias_int;
                float bias_frac = texture->bias / 256.0f;
                float bias = bias_int + bias_frac;

                bool is_bias = texture_op_takes_bias(texture->op);
                char sign = (bias >= 0.0) ? '+' : '-';
                char operand = is_bias ? sign : '=';

                fprintf(fp, "lod %c %f, ", operand, fabsf(bias));
        }

        fprintf(fp, "\n");

        /* While not zero in general, for these simple instructions the
         * following unknowns are zero, so we don't include them */

        if (texture->unknown4 ||
            texture->unknown8) {
                fprintf(fp, "// unknown4 = 0x%x\n", texture->unknown4);
                fprintf(fp, "// unknown8 = 0x%x\n", texture->unknown8);
        }

        midg_stats.instruction_count++;
}

struct midgard_disasm_stats
disassemble_midgard(FILE *fp, uint8_t *code, size_t size, unsigned gpu_id, gl_shader_stage stage)
{
        uint32_t *words = (uint32_t *) code;
        unsigned num_words = size / 4;
        int tabs = 0;

        bool branch_forward = false;

        int last_next_tag = -1;

        unsigned i = 0;

        midg_tags = calloc(sizeof(midg_tags[0]), num_words);

        /* Stats for shader-db */
        memset(&midg_stats, 0, sizeof(midg_stats));
        midg_ever_written = 0;

        while (i < num_words) {
                unsigned tag = words[i] & 0xF;
                unsigned next_tag = (words[i] >> 4) & 0xF;
                unsigned num_quad_words = midgard_tag_props[tag].size;

                if (midg_tags[i] && midg_tags[i] != tag) {
                        fprintf(fp, "\t/* XXX: TAG ERROR branch, got %s expected %s */\n",
                                        midgard_tag_props[tag].name,
                                        midgard_tag_props[midg_tags[i]].name);
                }

                midg_tags[i] = tag;

                /* Check the tag. The idea is to ensure that next_tag is
                 * *always* recoverable from the disassembly, such that we may
                 * safely omit printing next_tag. To show this, we first
                 * consider that next tags are semantically off-byone -- we end
                 * up parsing tag n during step n+1. So, we ensure after we're
                 * done disassembling the next tag of the final bundle is BREAK
                 * and warn otherwise. We also ensure that the next tag is
                 * never INVALID. Beyond that, since the last tag is checked
                 * outside the loop, we can check one tag prior. If equal to
                 * the current tag (which is unique), we're done. Otherwise, we
                 * print if that tag was > TAG_BREAK, which implies the tag was
                 * not TAG_BREAK or TAG_INVALID. But we already checked for
                 * TAG_INVALID, so it's just if the last tag was TAG_BREAK that
                 * we're silent. So we throw in a print for break-next on at
                 * the end of the bundle (if it's not the final bundle, which
                 * we already check for above), disambiguating this case as
                 * well.  Hence in all cases we are unambiguous, QED. */

                if (next_tag == TAG_INVALID)
                        fprintf(fp, "\t/* XXX: invalid next tag */\n");

                if (last_next_tag > TAG_BREAK && last_next_tag != tag) {
                        fprintf(fp, "\t/* XXX: TAG ERROR sequence, got %s expexted %s */\n",
                                        midgard_tag_props[tag].name,
                                        midgard_tag_props[last_next_tag].name);
                }

                last_next_tag = next_tag;

                /* Tags are unique in the following way:
                 *
                 * INVALID, BREAK, UNKNOWN_*: verbosely printed
                 * TEXTURE_4_BARRIER: verified by barrier/!barrier op
                 * TEXTURE_4_VTX: .vtx tag printed
                 * TEXTURE_4: tetxure lack of barriers or .vtx
                 * TAG_LOAD_STORE_4: only load/store
                 * TAG_ALU_4/8/12/16: by number of instructions/constants
                 * TAG_ALU_4_8/12/16_WRITEOUT: ^^ with .writeout tag
                 */

                switch (tag) {
                case TAG_TEXTURE_4_VTX ... TAG_TEXTURE_4_BARRIER: {
                        bool interpipe_aliasing =
                                midgard_get_quirks(gpu_id) & MIDGARD_INTERPIPE_REG_ALIASING;

                        print_texture_word(fp, &words[i], tabs,
                                        interpipe_aliasing ? 0 : REG_TEX_BASE,
                                        interpipe_aliasing ? REGISTER_LDST_BASE : REG_TEX_BASE);
                        break;
                }

                case TAG_LOAD_STORE_4:
                        print_load_store_word(fp, &words[i], tabs);
                        break;

                case TAG_ALU_4 ... TAG_ALU_16_WRITEOUT:
                        branch_forward = print_alu_word(fp, &words[i], num_quad_words, tabs, i + 4*num_quad_words);

                        /* Reset word static analysis state */
                        is_embedded_constant_half = false;
                        is_embedded_constant_int = false;

                        /* TODO: infer/verify me */
                        if (tag >= TAG_ALU_4_WRITEOUT)
                                fprintf(fp, "writeout\n");

                        break;

                default:
                        fprintf(fp, "Unknown word type %u:\n", words[i] & 0xF);
                        num_quad_words = 1;
                        print_quad_word(fp, &words[i], tabs);
                        fprintf(fp, "\n");
                        break;
                }

                /* We are parsing per bundle anyway. Add before we start
                 * breaking out so we don't miss the final bundle. */

                midg_stats.bundle_count++;
                midg_stats.quadword_count += num_quad_words;

                /* Include a synthetic "break" instruction at the end of the
                 * bundle to signify that if, absent a branch, the shader
                 * execution will stop here. Stop disassembly at such a break
                 * based on a heuristic */

                if (next_tag == TAG_BREAK) {
                        if (branch_forward) {
                                fprintf(fp, "break\n");
                        } else {
                                fprintf(fp, "\n");
                                break;
                        }
                }

                fprintf(fp, "\n");

                i += 4 * num_quad_words;
        }

        if (last_next_tag != TAG_BREAK) {
                fprintf(fp, "/* XXX: shader ended with tag %s */\n",
                                midgard_tag_props[last_next_tag].name);
        }

        free(midg_tags);

        /* We computed work_count as max_work_registers, so add one to get the
         * count. If no work registers are written, you still have one work
         * reported, which is exactly what the hardware expects */

        midg_stats.work_count++;

        return midg_stats;
}
