/* Author(s):
 *   Connor Abbott
 *   Alyssa Rosenzweig
 *
 * Copyright (c) 2013 Connor Abbott (connor@abbott.cx)
 * Copyright (c) 2018 Alyssa Rosenzweig (alyssa@rosenzweig.io)
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
#include <assert.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include "midgard.h"
#include "midgard-parse.h"
#include "midgard_ops.h"
#include "disassemble.h"
#include "helpers.h"
#include "util/half_float.h"
#include "util/u_math.h"

#define DEFINE_CASE(define, str) case define: { printf(str); break; }

static bool is_instruction_int = false;

/* Stats */

static struct midgard_disasm_stats midg_stats;

/* Prints a short form of the tag for branching, the minimum needed to be
 * legible and unambiguous */

static void
print_tag_short(unsigned tag)
{
        switch (midgard_word_types[tag]) {
        case midgard_word_type_texture:
                printf("tex/%X", tag);
                break;

        case midgard_word_type_load_store:
                printf("ldst");
                break;

        case midgard_word_type_alu:
                printf("alu%u/%X", midgard_word_size[tag], tag);
                break;

        default:
                printf("%s%X", (tag > 0) ? "" : "unk", tag);
                break;
        }
}

static void
print_alu_opcode(midgard_alu_op op)
{
        bool int_op = false;

        if (alu_opcode_props[op].name) {
                printf("%s", alu_opcode_props[op].name);

                int_op = midgard_is_integer_op(op);
        } else
                printf("alu_op_%02X", op);

        /* For constant analysis */
        is_instruction_int = int_op;
}

static void
print_ld_st_opcode(midgard_load_store_op op)
{
        if (load_store_opcode_names[op])
                printf("%s", load_store_opcode_names[op]);
        else
                printf("ldst_op_%02X", op);
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
print_reg(unsigned reg, unsigned bits)
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
                putchar(prefix);

        printf("r%u", reg);
}

static char *outmod_names_float[4] = {
        "",
        ".pos",
        ".unk2",
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
print_outmod(unsigned outmod, bool is_int)
{
        printf("%s", is_int ? outmod_names_int[outmod] :
               outmod_names_float[outmod]);
}

static void
print_quad_word(uint32_t *words, unsigned tabs)
{
        unsigned i;

        for (i = 0; i < 4; i++)
                printf("0x%08X%s ", words[i], i == 3 ? "" : ",");

        printf("\n");
}

static const char components[16] = "xyzwefghijklmnop";

/* Helper to print 4 chars of a swizzle */
static void
print_swizzle_helper(unsigned swizzle, bool upper)
{
        for (unsigned i = 0; i < 4; ++i) {
                unsigned c = (swizzle >> (i * 2)) & 3;
                c += upper*4;
                printf("%c", components[c]);
        }
}

/* Helper to print 8 chars of a swizzle, duplicating over */
static void
print_swizzle_helper_8(unsigned swizzle, bool upper)
{
        for (unsigned i = 0; i < 4; ++i) {
                unsigned c = (swizzle >> (i * 2)) & 3;
                c *= 2;
                c += upper*8;
                printf("%c%c", components[c], components[c+1]);
        }
}

static void
print_swizzle_vec16(unsigned swizzle, bool rep_high, bool rep_low,
                    midgard_dest_override override)
{
        printf(".");

        if (override == midgard_dest_override_upper) {
                if (rep_high)
                        printf(" /* rep_high */ ");
                if (rep_low)
                        printf(" /* rep_low */ ");

                if (!rep_high && rep_low)
                        print_swizzle_helper_8(swizzle, true);
                else
                        print_swizzle_helper_8(swizzle, false);
        } else {
                print_swizzle_helper_8(swizzle, rep_high & 1);
                print_swizzle_helper_8(swizzle, !(rep_low & 1));
        }
}

static void
print_swizzle_vec8(unsigned swizzle, bool rep_high, bool rep_low)
{
        printf(".");

        print_swizzle_helper(swizzle, rep_high & 1);
        print_swizzle_helper(swizzle, !(rep_low & 1));
}

static void
print_swizzle_vec4(unsigned swizzle, bool rep_high, bool rep_low)
{
        if (rep_high)
                printf(" /* rep_high */ ");
        if (rep_low)
                printf(" /* rep_low */ ");

        if (swizzle == 0xE4) return; /* xyzw */

        printf(".");
        print_swizzle_helper(swizzle, 0);
}
static void
print_swizzle_vec2(unsigned swizzle, bool rep_high, bool rep_low)
{
        if (rep_high)
                printf(" /* rep_high */ ");
        if (rep_low)
                printf(" /* rep_low */ ");

        if (swizzle == 0xE4) return; /* XY */

        printf(".");

        for (unsigned i = 0; i < 4; i += 2) {
                unsigned a = (swizzle >> (i * 2)) & 3;
                unsigned b = (swizzle >> ((i+1) * 2)) & 3;

                /* Normally we're adjacent, but if there's an issue, don't make
                 * it ambiguous */

                if (a & 0x1)
                        printf("[%c%c]", components[a], components[b]);
                else if (a == b)
                        printf("%c", components[a >> 1]);
                else if (b == (a + 1))
                        printf("%c", "XY"[a >> 1]);
                else
                        printf("[%c%c]", components[a], components[b]);
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
print_vector_src(unsigned src_binary,
                 midgard_reg_mode mode, unsigned reg,
                 midgard_dest_override override, bool is_int)
{
        midgard_vector_alu_src *src = (midgard_vector_alu_src *)&src_binary;

        /* Modifiers change meaning depending on the op's context */

        midgard_int_mod int_mod = src->mod;

        if (is_int) {
                printf("%s", srcmod_names_int[int_mod]);
        } else {
                if (src->mod & MIDGARD_FLOAT_MOD_NEG)
                        printf("-");

                if (src->mod & MIDGARD_FLOAT_MOD_ABS)
                        printf("abs(");
        }

        //register
        unsigned bits = bits_for_mode_halved(mode, src->half);
        print_reg(reg, bits);

        //swizzle
        if (bits == 16)
                print_swizzle_vec8(src->swizzle, src->rep_high, src->rep_low);
        else if (bits == 8)
                print_swizzle_vec16(src->swizzle, src->rep_high, src->rep_low, override);
        else if (bits == 32)
                print_swizzle_vec4(src->swizzle, src->rep_high, src->rep_low);
        else if (bits == 64)
                print_swizzle_vec2(src->swizzle, src->rep_high, src->rep_low);

        /* Since we wrapped with a function-looking thing */

        if (is_int && int_mod == midgard_int_shift)
                printf(") << %u", bits);
        else if ((is_int && (int_mod != midgard_int_normal))
                 || (!is_int && src->mod & MIDGARD_FLOAT_MOD_ABS))
                printf(")");
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
print_immediate(uint16_t imm)
{
        if (is_instruction_int)
                printf("#%u", imm);
        else
                printf("#%g", _mesa_half_to_float(imm));
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

static unsigned
print_dest(unsigned reg, midgard_reg_mode mode, midgard_dest_override override)
{
        /* Depending on the mode and override, we determine the type of
         * destination addressed. Absent an override, we address just the
         * type of the operation itself */

        unsigned bits = bits_for_mode(mode);

        if (override != midgard_dest_override_none)
                bits /= 2;

        update_dest(reg);
        print_reg(reg, bits);

        return bits;
}

static void
print_mask_vec16(uint8_t mask, midgard_dest_override override)
{
        printf(".");

        if (override == midgard_dest_override_none) {
                for (unsigned i = 0; i < 8; i++) {
                        if (mask & (1 << i))
                                printf("%c%c",
                                       components[i*2 + 0],
                                       components[i*2 + 1]);
                }
        } else {
                bool upper = (override == midgard_dest_override_upper);

                for (unsigned i = 0; i < 8; i++) {
                        if (mask & (1 << i))
                                printf("%c", components[i + (upper ? 8 : 0)]);
                }
        }
}

/* For 16-bit+ masks, we read off from the 8-bit mask field. For 16-bit (vec8),
 * it's just one bit per channel, easy peasy. For 32-bit (vec4), it's one bit
 * per channel with one duplicate bit in the middle. For 64-bit (vec2), it's
 * one-bit per channel with _3_ duplicate bits in the middle. Basically, just
 * subdividing the 128-bit word in 16-bit increments. For 64-bit, we uppercase
 * the mask to make it obvious what happened */

static void
print_mask(uint8_t mask, unsigned bits, midgard_dest_override override)
{
        if (bits == 8) {
                print_mask_vec16(mask, override);
                return;
        }

        if (bits < 16) {
                /* Shouldn't happen but with junk / out-of-spec shaders it
                 * would cause an infinite loop */

                printf("/* XXX: bits = %u */", bits);
                return;
        }

        /* Skip 'complete' masks */

        if (bits >= 32 && mask == 0xFF) return;

        if (bits == 16) {
                if (mask == 0x0F)
                        return;
                else if (mask == 0xF0) {
                        printf("'");
                        return;
                }
        }

        printf(".");

        unsigned skip = (bits / 16);
        bool uppercase = bits > 32;
        bool tripped = false;

        for (unsigned i = 0; i < 8; i += skip) {
                bool a = (mask & (1 << i)) != 0;

                for (unsigned j = 1; j < skip; ++j) {
                        bool dupe = (mask & (1 << (i + j))) != 0;
                        tripped |= (dupe != a);
                }

                if (a) {
                        char c = components[i / skip];

                        if (uppercase)
                                c = toupper(c);

                        printf("%c", c);
                }
        }

        if (tripped)
                printf(" /* %X */", mask);
}

/* Prints the 4-bit masks found in texture and load/store ops, as opposed to
 * the 8-bit masks found in (vector) ALU ops */

static void
print_mask_4(unsigned mask)
{
        if (mask == 0xF) return;

        printf(".");

        for (unsigned i = 0; i < 4; ++i) {
                bool a = (mask & (1 << i)) != 0;
                if (a)
                        printf("%c", components[i]);
        }
}

static void
print_vector_field(const char *name, uint16_t *words, uint16_t reg_word,
                   unsigned tabs)
{
        midgard_reg_info *reg_info = (midgard_reg_info *)&reg_word;
        midgard_vector_alu *alu_field = (midgard_vector_alu *) words;
        midgard_reg_mode mode = alu_field->reg_mode;
        unsigned override = alu_field->dest_override;

        /* For now, prefix instruction names with their unit, until we
         * understand how this works on a deeper level */
        printf("%s.", name);

        print_alu_opcode(alu_field->op);

        /* Postfix with the size to disambiguate if necessary */
        char postfix = prefix_for_bits(bits_for_mode(mode));
        bool size_ambiguous = override != midgard_dest_override_none;

        if (size_ambiguous)
                printf("%c", postfix ? postfix : 'r');

        /* Print the outmod, if there is one */
        print_outmod(alu_field->outmod,
                     midgard_is_integer_out_op(alu_field->op));

        printf(" ");

        /* Mask denoting status of 8-lanes */
        uint8_t mask = alu_field->mask;

        /* First, print the destination */
        unsigned dest_size =
                print_dest(reg_info->out_reg, mode, alu_field->dest_override);

        /* Apply the destination override to the mask */

        if (mode == midgard_reg_mode_32 || mode == midgard_reg_mode_64) {
                if (override == midgard_dest_override_lower)
                        mask &= 0x0F;
                else if (override == midgard_dest_override_upper)
                        mask &= 0xF0;
        } else if (mode == midgard_reg_mode_16
                   && override == midgard_dest_override_lower) {
                /* stub */
        }

        if (override != midgard_dest_override_none) {
                bool modeable = (mode != midgard_reg_mode_8);
                bool known = override != 0x3; /* Unused value */

                if (!(modeable && known))
                        printf("/* do%u */ ", override);
        }

        print_mask(mask, dest_size, override);

        printf(", ");

        bool is_int = midgard_is_integer_op(alu_field->op);
        print_vector_src(alu_field->src1, mode, reg_info->src1_reg, override, is_int);

        printf(", ");

        if (reg_info->src2_imm) {
                uint16_t imm = decode_vector_imm(reg_info->src2_reg, alu_field->src2 >> 2);
                print_immediate(imm);
        } else {
                print_vector_src(alu_field->src2, mode,
                                 reg_info->src2_reg, override, is_int);
        }

        midg_stats.instruction_count++;
        printf("\n");
}

static void
print_scalar_src(unsigned src_binary, unsigned reg)
{
        midgard_scalar_alu_src *src = (midgard_scalar_alu_src *)&src_binary;

        if (src->negate)
                printf("-");

        if (src->abs)
                printf("abs(");

        print_reg(reg, src->full ? 32 : 16);

        unsigned c = src->component;

        if (src->full) {
                assert((c & 1) == 0);
                c >>= 1;
        }

        printf(".%c", components[c]);

        if (src->abs)
                printf(")");

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
print_scalar_field(const char *name, uint16_t *words, uint16_t reg_word,
                   unsigned tabs)
{
        midgard_reg_info *reg_info = (midgard_reg_info *)&reg_word;
        midgard_scalar_alu *alu_field = (midgard_scalar_alu *) words;

        if (alu_field->unknown)
                printf("scalar ALU unknown bit set\n");

        printf("%s.", name);
        print_alu_opcode(alu_field->op);
        print_outmod(alu_field->outmod,
                     midgard_is_integer_out_op(alu_field->op));
        printf(" ");

        bool full = alu_field->output_full;
        update_dest(reg_info->out_reg);
        print_reg(reg_info->out_reg, full ? 32 : 16);
        unsigned c = alu_field->output_component;

        if (full) {
                assert((c & 1) == 0);
                c >>= 1;
        }

        printf(".%c, ", components[c]);

        print_scalar_src(alu_field->src1, reg_info->src1_reg);

        printf(", ");

        if (reg_info->src2_imm) {
                uint16_t imm = decode_scalar_imm(reg_info->src2_reg,
                                                 alu_field->src2);
                print_immediate(imm);
        } else
                print_scalar_src(alu_field->src2, reg_info->src2_reg);

        midg_stats.instruction_count++;
        printf("\n");
}

static void
print_branch_op(unsigned op)
{
        switch (op) {
        case midgard_jmp_writeout_op_branch_uncond:
                printf("uncond.");
                break;

        case midgard_jmp_writeout_op_branch_cond:
                printf("cond.");
                break;

        case midgard_jmp_writeout_op_writeout:
                printf("write.");
                break;

        case midgard_jmp_writeout_op_tilebuffer_pending:
                printf("tilebuffer.");
                break;

        case midgard_jmp_writeout_op_discard:
                printf("discard.");
                break;

        default:
                printf("unk%u.", op);
                break;
        }
}

static void
print_branch_cond(int cond)
{
        switch (cond) {
        case midgard_condition_write0:
                printf("write0");
                break;

        case midgard_condition_false:
                printf("false");
                break;

        case midgard_condition_true:
                printf("true");
                break;

        case midgard_condition_always:
                printf("always");
                break;

        default:
                printf("unk%X", cond);
                break;
        }
}

static void
print_compact_branch_writeout_field(uint16_t word)
{
        midgard_jmp_writeout_op op = word & 0x7;

        switch (op) {
        case midgard_jmp_writeout_op_branch_uncond: {
                midgard_branch_uncond br_uncond;
                memcpy((char *) &br_uncond, (char *) &word, sizeof(br_uncond));
                printf("br.uncond ");

                if (br_uncond.unknown != 1)
                        printf("unknown:%u, ", br_uncond.unknown);

                if (br_uncond.offset >= 0)
                        printf("+");

                printf("%d -> ", br_uncond.offset);
                print_tag_short(br_uncond.dest_tag);
                printf("\n");

                break;
        }

        case midgard_jmp_writeout_op_branch_cond:
        case midgard_jmp_writeout_op_writeout:
        case midgard_jmp_writeout_op_discard:
        default: {
                midgard_branch_cond br_cond;
                memcpy((char *) &br_cond, (char *) &word, sizeof(br_cond));

                printf("br.");

                print_branch_op(br_cond.op);
                print_branch_cond(br_cond.cond);

                printf(" ");

                if (br_cond.offset >= 0)
                        printf("+");

                printf("%d -> ", br_cond.offset);
                print_tag_short(br_cond.dest_tag);
                printf("\n");

                break;
        }
        }

        midg_stats.instruction_count++;
}

static void
print_extended_branch_writeout_field(uint8_t *words)
{
        midgard_branch_extended br;
        memcpy((char *) &br, (char *) words, sizeof(br));

        printf("brx.");

        print_branch_op(br.op);

        /* Condition codes are a LUT in the general case, but simply repeated 8 times for single-channel conditions.. Check this. */

        bool single_channel = true;

        for (unsigned i = 0; i < 16; i += 2) {
                single_channel &= (((br.cond >> i) & 0x3) == (br.cond & 0x3));
        }

        if (single_channel)
                print_branch_cond(br.cond & 0x3);
        else
                printf("lut%X", br.cond);

        if (br.unknown)
                printf(".unknown%u", br.unknown);

        printf(" ");

        if (br.offset >= 0)
                printf("+");

        printf("%d -> ", br.offset);
        print_tag_short(br.dest_tag);
        printf("\n");

        midg_stats.instruction_count++;
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

static float
float_bitcast(uint32_t integer)
{
        union {
                uint32_t i;
                float f;
        } v;

        v.i = integer;
        return v.f;
}

static void
print_alu_word(uint32_t *words, unsigned num_quad_words,
               unsigned tabs)
{
        uint32_t control_word = words[0];
        uint16_t *beginning_ptr = (uint16_t *)(words + 1);
        unsigned num_fields = num_alu_fields_enabled(control_word);
        uint16_t *word_ptr = beginning_ptr + num_fields;
        unsigned num_words = 2 + num_fields;

        if ((control_word >> 16) & 1)
                printf("unknown bit 16 enabled\n");

        if ((control_word >> 17) & 1) {
                print_vector_field("vmul", word_ptr, *beginning_ptr, tabs);
                beginning_ptr += 1;
                word_ptr += 3;
                num_words += 3;
        }

        if ((control_word >> 18) & 1)
                printf("unknown bit 18 enabled\n");

        if ((control_word >> 19) & 1) {
                print_scalar_field("sadd", word_ptr, *beginning_ptr, tabs);
                beginning_ptr += 1;
                word_ptr += 2;
                num_words += 2;
        }

        if ((control_word >> 20) & 1)
                printf("unknown bit 20 enabled\n");

        if ((control_word >> 21) & 1) {
                print_vector_field("vadd", word_ptr, *beginning_ptr, tabs);
                beginning_ptr += 1;
                word_ptr += 3;
                num_words += 3;
        }

        if ((control_word >> 22) & 1)
                printf("unknown bit 22 enabled\n");

        if ((control_word >> 23) & 1) {
                print_scalar_field("smul", word_ptr, *beginning_ptr, tabs);
                beginning_ptr += 1;
                word_ptr += 2;
                num_words += 2;
        }

        if ((control_word >> 24) & 1)
                printf("unknown bit 24 enabled\n");

        if ((control_word >> 25) & 1) {
                print_vector_field("lut", word_ptr, *beginning_ptr, tabs);
                word_ptr += 3;
                num_words += 3;
        }

        if ((control_word >> 26) & 1) {
                print_compact_branch_writeout_field(*word_ptr);
                word_ptr += 1;
                num_words += 1;
        }

        if ((control_word >> 27) & 1) {
                print_extended_branch_writeout_field((uint8_t *) word_ptr);
                word_ptr += 3;
                num_words += 3;
        }

        if (num_quad_words > (num_words + 7) / 8) {
                assert(num_quad_words == (num_words + 15) / 8);
                //Assume that the extra quadword is constants
                void *consts = words + (4 * num_quad_words - 4);

                if (is_embedded_constant_int) {
                        if (is_embedded_constant_half) {
                                int16_t *sconsts = (int16_t *) consts;
                                printf("sconstants %d, %d, %d, %d\n",
                                       sconsts[0],
                                       sconsts[1],
                                       sconsts[2],
                                       sconsts[3]);
                        } else {
                                uint32_t *iconsts = (uint32_t *) consts;
                                printf("iconstants 0x%X, 0x%X, 0x%X, 0x%X\n",
                                       iconsts[0],
                                       iconsts[1],
                                       iconsts[2],
                                       iconsts[3]);
                        }
                } else {
                        if (is_embedded_constant_half) {
                                uint16_t *hconsts = (uint16_t *) consts;
                                printf("hconstants %g, %g, %g, %g\n",
                                       _mesa_half_to_float(hconsts[0]),
                                       _mesa_half_to_float(hconsts[1]),
                                       _mesa_half_to_float(hconsts[2]),
                                       _mesa_half_to_float(hconsts[3]));
                        } else {
                                uint32_t *fconsts = (uint32_t *) consts;
                                printf("fconstants %g, %g, %g, %g\n",
                                       float_bitcast(fconsts[0]),
                                       float_bitcast(fconsts[1]),
                                       float_bitcast(fconsts[2]),
                                       float_bitcast(fconsts[3]));
                        }

                }
        }
}

static void
print_varying_parameters(midgard_load_store_word *word)
{
        midgard_varying_parameter param;
        unsigned v = word->varying_parameters;
        memcpy(&param, &v, sizeof(param));

        if (param.is_varying) {
                /* If a varying, there are qualifiers */
                if (param.flat)
                        printf(".flat");

                if (param.interpolation != midgard_interp_default) {
                        if (param.interpolation == midgard_interp_centroid)
                                printf(".centroid");
                        else
                                printf(".interp%d", param.interpolation);
                }

                if (param.modifier != midgard_varying_mod_none) {
                        if (param.modifier == midgard_varying_mod_perspective_w)
                                printf(".perspectivew");
                        else if (param.modifier == midgard_varying_mod_perspective_z)
                                printf(".perspectivez");
                        else
                                printf(".mod%d", param.modifier);
                }
        } else if (param.flat || param.interpolation || param.modifier) {
                printf(" /* is_varying not set but varying metadata attached */");
        }

        if (param.zero0 || param.zero1 || param.zero2)
                printf(" /* zero tripped, %u %u %u */ ", param.zero0, param.zero1, param.zero2);
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
print_load_store_arg(uint8_t arg, unsigned index)
{
        /* Try to interpret as a register */
        midgard_ldst_register_select sel;
        memcpy(&sel, &arg, sizeof(arg));

        /* If unknown is set, we're not sure what this is or how to
         * interpret it. But if it's zero, we get it. */

        if (sel.unknown) {
                printf("0x%02X", arg);
                return;
        }

        unsigned reg = REGISTER_LDST_BASE + sel.select;
        char comp = components[sel.component];

        printf("r%u.%c", reg, comp);

        /* Only print a shift if it's non-zero. Shifts only make sense for the
         * second index. For the first, we're not sure what it means yet */

        if (index == 1) {
                if (sel.shift)
                        printf(" << %u", sel.shift);
        } else {
                printf(" /* %X */", sel.shift);
        }
}

static void
update_stats(signed *stat, unsigned address)
{
        if (*stat >= 0)
                *stat = MAX2(*stat, address + 1);
}

static void
print_load_store_instr(uint64_t data,
                       unsigned tabs)
{
        midgard_load_store_word *word = (midgard_load_store_word *) &data;

        print_ld_st_opcode(word->op);

        unsigned address = word->address;

        if (is_op_varying(word->op)) {
                print_varying_parameters(word);

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

        printf(" r%u", word->reg);
        print_mask_4(word->mask);

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

        printf(", %u", address);

        print_swizzle_vec4(word->swizzle, false, false);

        printf(", ");

        if (is_ubo) {
                printf("ubo%u", word->arg_1);
                update_stats(&midg_stats.uniform_buffer_count, word->arg_1);
        } else
                print_load_store_arg(word->arg_1, 0);

        printf(", ");
        print_load_store_arg(word->arg_2, 1);
        printf(" /* %X */\n", word->varying_parameters);

        midg_stats.instruction_count++;
}

static void
print_load_store_word(uint32_t *word, unsigned tabs)
{
        midgard_load_store *load_store = (midgard_load_store *) word;

        if (load_store->word1 != 3) {
                print_load_store_instr(load_store->word1, tabs);
        }

        if (load_store->word2 != 3) {
                print_load_store_instr(load_store->word2, tabs);
        }
}

static void
print_texture_reg(bool full, bool select, bool upper)
{
        if (full)
                printf("r%d", REG_TEX_BASE + select);
        else
                printf("hr%d", (REG_TEX_BASE + select) * 2 + upper);

        if (full && upper)
                printf("// error: out full / upper mutually exclusive\n");

}

static void
print_texture_reg_triple(unsigned triple)
{
        bool full = triple & 1;
        bool select = triple & 2;
        bool upper = triple & 4;

        print_texture_reg(full, select, upper);
}

static void
print_texture_reg_select(uint8_t u)
{
        midgard_tex_register_select sel;
        memcpy(&sel, &u, sizeof(u));

        if (!sel.full)
                printf("h");

        printf("r%u", REG_TEX_BASE + sel.select);

        unsigned component = sel.component;

        /* Use the upper half in half-reg mode */
        if (sel.upper) {
                assert(!sel.full);
                component += 4;
        }

        printf(".%c", components[component]);

        assert(sel.zero == 0);
}

static void
print_texture_format(int format)
{
        /* Act like a modifier */
        printf(".");

        switch (format) {
                DEFINE_CASE(MALI_TEX_1D, "1d");
                DEFINE_CASE(MALI_TEX_2D, "2d");
                DEFINE_CASE(MALI_TEX_3D, "3d");
                DEFINE_CASE(MALI_TEX_CUBE, "cube");

        default:
                unreachable("Bad format");
        }
}

static bool
midgard_op_has_helpers(unsigned op, bool gather)
{
        if (gather)
                return true;

        switch (op) {
        case TEXTURE_OP_NORMAL:
        case TEXTURE_OP_DFDX:
        case TEXTURE_OP_DFDY:
                return true;
        default:
                return false;
        }
}

static void
print_texture_op(unsigned op, bool gather)
{
        /* Act like a bare name, like ESSL functions */

        if (gather) {
                printf("textureGather");

                unsigned component = op >> 4;
                unsigned bottom = op & 0xF;

                if (bottom != 0x2)
                        printf("_unk%u", bottom);

                printf(".%c", components[component]);
                return;
        }

        switch (op) {
                DEFINE_CASE(TEXTURE_OP_NORMAL, "texture");
                DEFINE_CASE(TEXTURE_OP_LOD, "textureLod");
                DEFINE_CASE(TEXTURE_OP_TEXEL_FETCH, "texelFetch");
                DEFINE_CASE(TEXTURE_OP_DFDX, "dFdx");
                DEFINE_CASE(TEXTURE_OP_DFDY, "dFdy");

        default:
                printf("tex_%X", op);
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

#undef DEFINE_CASE

static void
print_texture_word(uint32_t *word, unsigned tabs)
{
        midgard_texture_word *texture = (midgard_texture_word *) word;

        midg_stats.helper_invocations |=
                midgard_op_has_helpers(texture->op, texture->is_gather);

        /* Broad category of texture operation in question */
        print_texture_op(texture->op, texture->is_gather);

        /* Specific format in question */
        print_texture_format(texture->format);

        /* Instruction "modifiers" parallel the ALU instructions. */

        if (texture->shadow)
                printf(".shadow");

        if (texture->cont)
                printf(".cont");

        if (texture->last)
                printf(".last");

        /* Output modifiers are always interpreted floatly */
        print_outmod(texture->outmod, false);

        printf(" ");

        print_texture_reg(texture->out_full, texture->out_reg_select, texture->out_upper);
        print_mask_4(texture->mask);
        printf(", ");

        /* Depending on whether we read from textures directly or indirectly,
         * we may be able to update our analysis */

        if (texture->texture_register) {
                printf("texture[");
                print_texture_reg_select(texture->texture_handle);
                printf("], ");

                /* Indirect, tut tut */
                midg_stats.texture_count = -16;
        } else {
                printf("texture%u, ", texture->texture_handle);
                update_stats(&midg_stats.texture_count, texture->texture_handle);
        }

        /* Print the type, GL style */
        printf("%csampler", sampler_type_name(texture->sampler_type));

        if (texture->sampler_register) {
                printf("[");
                print_texture_reg_select(texture->sampler_handle);
                printf("]");

                midg_stats.sampler_count = -16;
        } else {
                printf("%u", texture->sampler_handle);
                update_stats(&midg_stats.sampler_count, texture->sampler_handle);
        }

        print_swizzle_vec4(texture->swizzle, false, false);
        printf(", ");

        print_texture_reg(texture->in_reg_full, texture->in_reg_select, texture->in_reg_upper);
        print_swizzle_vec4(texture->in_reg_swizzle, false, false);

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
                printf(" + ");
                print_texture_reg_triple(texture->offset_x);

                /* The less questions you ask, the better. */

                unsigned swizzle_lo, swizzle_hi;
                unsigned orig_y = texture->offset_y;
                unsigned orig_z = texture->offset_z;

                memcpy(&swizzle_lo, &orig_y, sizeof(unsigned));
                memcpy(&swizzle_hi, &orig_z, sizeof(unsigned));

                /* Duplicate hi swizzle over */
                assert(swizzle_hi < 4);
                swizzle_hi = (swizzle_hi << 2) | swizzle_hi;

                unsigned swiz = (swizzle_lo << 4) | swizzle_hi;
                unsigned reversed = util_bitreverse(swiz) >> 24;
                print_swizzle_vec4(reversed, false, false);

                printf(", ");
        } else if (texture->offset_x || texture->offset_y || texture->offset_z) {
                /* Only select ops allow negative immediate offsets, verify */

                bool neg_x = texture->offset_x < 0;
                bool neg_y = texture->offset_y < 0;
                bool neg_z = texture->offset_z < 0;
                bool any_neg = neg_x || neg_y || neg_z;

                if (any_neg && texture->op != TEXTURE_OP_TEXEL_FETCH)
                        printf("/* invalid negative */ ");

                /* Regardless, just print the immediate offset */

                printf(" + <%d, %d, %d>, ",
                       texture->offset_x,
                       texture->offset_y,
                       texture->offset_z);
        } else {
                printf(", ");
        }

        char lod_operand = texture_op_takes_bias(texture->op) ? '+' : '=';

        if (texture->lod_register) {
                printf("lod %c ", lod_operand);
                print_texture_reg_select(texture->bias);
                printf(", ");

                if (texture->bias_int)
                        printf(" /* bias_int = 0x%X */", texture->bias_int);
        } else if (texture->op == TEXTURE_OP_TEXEL_FETCH) {
                /* For texel fetch, the int LOD is in the fractional place and
                 * there is no fraction / possibility of bias. We *always* have
                 * an explicit LOD, even if it's zero. */

                if (texture->bias_int)
                        printf(" /* bias_int = 0x%X */ ", texture->bias_int);

                printf("lod = %u, ", texture->bias);
        } else if (texture->bias || texture->bias_int) {
                signed bias_int = texture->bias_int;
                float bias_frac = texture->bias / 256.0f;
                float bias = bias_int + bias_frac;

                bool is_bias = texture_op_takes_bias(texture->op);
                char sign = (bias >= 0.0) ? '+' : '-';
                char operand = is_bias ? sign : '=';

                printf("lod %c %f, ", operand, fabsf(bias));
        }

        printf("\n");

        /* While not zero in general, for these simple instructions the
         * following unknowns are zero, so we don't include them */

        if (texture->unknown4 ||
            texture->unknownA ||
            texture->unknown8) {
                printf("// unknown4 = 0x%x\n", texture->unknown4);
                printf("// unknownA = 0x%x\n", texture->unknownA);
                printf("// unknown8 = 0x%x\n", texture->unknown8);
        }

        midg_stats.instruction_count++;
}

struct midgard_disasm_stats
disassemble_midgard(uint8_t *code, size_t size)
{
        uint32_t *words = (uint32_t *) code;
        unsigned num_words = size / 4;
        int tabs = 0;

        bool prefetch_flag = false;

        int last_next_tag = -1;

        unsigned i = 0;

        /* Stats for shader-db */
        memset(&midg_stats, 0, sizeof(midg_stats));
        midg_ever_written = 0;

        while (i < num_words) {
                unsigned tag = words[i] & 0xF;
                unsigned next_tag = (words[i] >> 4) & 0xF;
                unsigned num_quad_words = midgard_word_size[tag];

                /* Check the tag */
                if (last_next_tag > 1) {
                        if (last_next_tag != tag) {
                                printf("/* TAG ERROR got ");
                                print_tag_short(tag);
                                printf(" expected ");
                                print_tag_short(last_next_tag);
                                printf(" */ ");
                        }
                } else {
                        /* TODO: Check ALU case */
                }

                last_next_tag = next_tag;

                switch (midgard_word_types[tag]) {
                case midgard_word_type_texture:
                        print_texture_word(&words[i], tabs);
                        break;

                case midgard_word_type_load_store:
                        print_load_store_word(&words[i], tabs);
                        break;

                case midgard_word_type_alu:
                        print_alu_word(&words[i], num_quad_words, tabs);

                        /* Reset word static analysis state */
                        is_embedded_constant_half = false;
                        is_embedded_constant_int = false;

                        break;

                default:
                        printf("Unknown word type %u:\n", words[i] & 0xF);
                        num_quad_words = 1;
                        print_quad_word(&words[i], tabs);
                        printf("\n");
                        break;
                }

                if (prefetch_flag && midgard_word_types[tag] == midgard_word_type_alu)
                        break;

                printf("\n");

                unsigned next = (words[i] & 0xF0) >> 4;

                /* We are parsing per bundle anyway */
                midg_stats.bundle_count++;
                midg_stats.quadword_count += num_quad_words;

                /* Break based on instruction prefetch flag */

                if (i < num_words && next == 1) {
                        prefetch_flag = true;

                        if (midgard_word_types[words[i] & 0xF] != midgard_word_type_alu)
                                break;
                }

                i += 4 * num_quad_words;
        }

        /* We computed work_count as max_work_registers, so add one to get the
         * count. If no work registers are written, you still have one work
         * reported, which is exactly what the hardware expects */

        midg_stats.work_count++;

        return midg_stats;
}
