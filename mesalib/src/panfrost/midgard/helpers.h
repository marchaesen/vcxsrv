/* Copyright (c) 2018-2019 Alyssa Rosenzweig (alyssa@rosenzweig.io)
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

#ifndef __MDG_HELPERS_H
#define __MDG_HELPERS_H

#include "util/macros.h"
#include <string.h>

#define OP_IS_LOAD_VARY_F(op) (\
                op == midgard_op_ld_vary_16 || \
                op == midgard_op_ld_vary_32 \
        )

#define OP_IS_PROJECTION(op) ( \
                op == midgard_op_ldst_perspective_division_z || \
                op == midgard_op_ldst_perspective_division_w \
        )

#define OP_IS_VEC4_ONLY(op) ( \
                OP_IS_PROJECTION(op) || \
                op == midgard_op_ld_cubemap_coords \
        )

#define OP_IS_MOVE(op) ( \
                op == midgard_alu_op_fmov || \
                op == midgard_alu_op_imov \
        )

#define OP_IS_UBO_READ(op) ( \
                op == midgard_op_ld_ubo_char  || \
                op == midgard_op_ld_ubo_char2  || \
                op == midgard_op_ld_ubo_char4  || \
                op == midgard_op_ld_ubo_short4  || \
                op == midgard_op_ld_ubo_int4 \
        )

#define OP_IS_CSEL_V(op) ( \
                op == midgard_alu_op_icsel_v || \
                op == midgard_alu_op_fcsel_v \
        )

#define OP_IS_CSEL(op) ( \
                OP_IS_CSEL_V(op) || \
                op == midgard_alu_op_icsel || \
                op == midgard_alu_op_fcsel \
        )

#define OP_IS_DERIVATIVE(op) ( \
                op == TEXTURE_OP_DFDX || \
                op == TEXTURE_OP_DFDY \
        )

/* ALU control words are single bit fields with a lot of space */

#define ALU_ENAB_VEC_MUL  (1 << 17)
#define ALU_ENAB_SCAL_ADD  (1 << 19)
#define ALU_ENAB_VEC_ADD  (1 << 21)
#define ALU_ENAB_SCAL_MUL  (1 << 23)
#define ALU_ENAB_VEC_LUT  (1 << 25)
#define ALU_ENAB_BR_COMPACT (1 << 26)
#define ALU_ENAB_BRANCH   (1 << 27)

/* Other opcode properties that don't conflict with the ALU_ENABs, non-ISA */

/* Denotes an opcode that takes a vector input with a fixed-number of
 * channels, but outputs to only a single output channel, like dot products.
 * For these, to determine the effective mask, this quirk can be set. We have
 * an intentional off-by-one (a la MALI_POSITIVE), since 0-channel makes no
 * sense but we need to fit 4 channels in 2-bits. Similarly, 1-channel doesn't
 * make sense (since then why are we quirked?), so that corresponds to "no
 * count set" */

#define OP_CHANNEL_COUNT(c) ((c - 1) << 0)
#define GET_CHANNEL_COUNT(c) ((c & (0x3 << 0)) ? ((c & (0x3 << 0)) + 1) : 0)

/* For instructions that take a single argument, normally the first argument
 * slot is used for the argument and the second slot is a dummy #0 constant.
 * However, there are exceptions: instructions like fmov store their argument
 * in the _second_ slot and store a dummy r24 in the first slot, designated by
 * QUIRK_FLIPPED_R24 */

#define QUIRK_FLIPPED_R24 (1 << 2)

/* Is the op commutative? */
#define OP_COMMUTES (1 << 3)

/* Does the op convert types between int- and float- space (i2f/f2u/etc) */
#define OP_TYPE_CONVERT (1 << 4)

/* Vector-independant shorthands for the above; these numbers are arbitrary and
 * not from the ISA. Convert to the above with unit_enum_to_midgard */

#define UNIT_MUL 0
#define UNIT_ADD 1
#define UNIT_LUT 2

/* 4-bit type tags */

#define TAG_TEXTURE_4_VTX 0x2
#define TAG_TEXTURE_4 0x3
#define TAG_LOAD_STORE_4 0x5
#define TAG_ALU_4 0x8
#define TAG_ALU_8 0x9
#define TAG_ALU_12 0xA
#define TAG_ALU_16 0xB

static inline int
quadword_size(int tag)
{
        switch (tag) {
        case TAG_ALU_4:
        case TAG_LOAD_STORE_4:
        case TAG_TEXTURE_4:
        case TAG_TEXTURE_4_VTX:
                return 1;
        case TAG_ALU_8:
                return 2;
        case TAG_ALU_12:
                return 3;
        case TAG_ALU_16:
                return 4;
        default:
                unreachable("Unknown tag");
        }
}

#define IS_ALU(tag) (tag == TAG_ALU_4 || tag == TAG_ALU_8 ||  \
		     tag == TAG_ALU_12 || tag == TAG_ALU_16)

/* Special register aliases */

#define MAX_WORK_REGISTERS 16

/* Uniforms are begin at (REGISTER_UNIFORMS - uniform_count) */
#define REGISTER_UNIFORMS 24

#define REGISTER_UNUSED 24
#define REGISTER_CONSTANT 26
#define REGISTER_LDST_BASE 26
#define REGISTER_TEXTURE_BASE 28
#define REGISTER_SELECT 31

/* SSA helper aliases to mimic the registers. */

#define SSA_UNUSED ~0
#define SSA_FIXED_SHIFT 24
#define SSA_FIXED_REGISTER(reg) (((1 + (reg)) << SSA_FIXED_SHIFT) | 1)
#define SSA_REG_FROM_FIXED(reg) ((((reg) & ~1) >> SSA_FIXED_SHIFT) - 1)
#define SSA_FIXED_MINIMUM SSA_FIXED_REGISTER(0)

#define COMPONENT_X 0x0
#define COMPONENT_Y 0x1
#define COMPONENT_Z 0x2
#define COMPONENT_W 0x3

#define SWIZZLE_IDENTITY { \
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, \
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 }, \
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } \
}

#define SWIZZLE_IDENTITY_4 { \
        { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0 }, \
        { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0 }, \
        { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0 }, \
}

static inline unsigned
mask_of(unsigned nr_comp)
{
        return (1 << nr_comp) - 1;
}

/* See ISA notes */

#define LDST_NOP (3)

/* There are five ALU units: VMUL, VADD, SMUL, SADD, LUT. A given opcode is
 * implemented on some subset of these units (or occassionally all of them).
 * This table encodes a bit mask of valid units for each opcode, so the
 * scheduler can figure where to plonk the instruction. */

/* Shorthands for each unit */
#define UNIT_VMUL ALU_ENAB_VEC_MUL
#define UNIT_SADD ALU_ENAB_SCAL_ADD
#define UNIT_VADD ALU_ENAB_VEC_ADD
#define UNIT_SMUL ALU_ENAB_SCAL_MUL
#define UNIT_VLUT ALU_ENAB_VEC_LUT

/* Shorthands for usual combinations of units */

#define UNITS_MUL (UNIT_VMUL | UNIT_SMUL)
#define UNITS_ADD (UNIT_VADD | UNIT_SADD)
#define UNITS_MOST (UNITS_MUL | UNITS_ADD)
#define UNITS_ALL (UNITS_MOST | UNIT_VLUT)
#define UNITS_SCALAR (UNIT_SADD | UNIT_SMUL)
#define UNITS_VECTOR (UNIT_VMUL | UNIT_VADD)
#define UNITS_ANY_VECTOR (UNITS_VECTOR | UNIT_VLUT)

struct mir_op_props {
        const char *name;
        unsigned props;
};

/* For load/store */

struct mir_ldst_op_props {
        const char *name;
        unsigned props;
};

/* Lower 2-bits are a midgard_reg_mode */
#define GET_LDST_SIZE(c) (c & 3)

/* Store (so the primary register is a source, not a destination */
#define LDST_STORE (1 << 2)

/* Mask has special meaning and should not be manipulated directly */
#define LDST_SPECIAL_MASK (1 << 3)

/* Non-store operation has side effects and should not be eliminated even if
 * its mask is 0 */
#define LDST_SIDE_FX (1 << 4)

/* This file is common, so don't define the tables themselves. #include
 * midgard_op.h if you need that, or edit midgard_ops.c directly */

/* Duplicate bits to convert a 4-bit writemask to duplicated 8-bit format,
 * which is used for 32-bit vector units */

static inline unsigned
expand_writemask_32(unsigned mask)
{
        unsigned o = 0;

        for (int i = 0; i < 4; ++i)
                if (mask & (1 << i))
                        o |= (3 << (2 * i));

        return o;
}

/* Coerce structs to integer */

static inline unsigned
vector_alu_srco_unsigned(midgard_vector_alu_src src)
{
        unsigned u;
        memcpy(&u, &src, sizeof(src));
        return u;
}

static inline midgard_vector_alu_src
vector_alu_from_unsigned(unsigned u)
{
        midgard_vector_alu_src s;
        memcpy(&s, &u, sizeof(s));
        return s;
}

static inline void
mir_compose_swizzle(unsigned *left, unsigned *right, unsigned *final_out)
{
        unsigned out[16];

        for (unsigned c = 0; c < 16; ++c)
                out[c] = right[left[c]];

        memcpy(final_out, out, sizeof(out));
}

/* Checks for an xyzw.. swizzle, given a mask */

static inline bool
mir_is_simple_swizzle(unsigned *swizzle, unsigned mask)
{
        for (unsigned i = 0; i < 16; ++i) {
                if (!(mask & (1 << i))) continue;

                if (swizzle[i] != i)
                        return false;
        }

        return true;
}

/* Packs a load/store argument */

static inline uint8_t
midgard_ldst_reg(unsigned reg, unsigned component)
{
        assert((reg == REGISTER_LDST_BASE) || (reg == REGISTER_LDST_BASE + 1));

        midgard_ldst_register_select sel = {
                .component = component,
                .select = reg - 26
        };

        uint8_t packed;
        memcpy(&packed, &sel, sizeof(packed));

        return packed;
}

/* Unpacks a load/store argument */

static inline midgard_ldst_register_select
midgard_ldst_select(uint8_t u)
{
        midgard_ldst_register_select sel;
        memcpy(&sel, &u, sizeof(u));
        return sel;
}

static inline uint8_t
midgard_ldst_pack(midgard_ldst_register_select sel)
{
        uint8_t packed;
        memcpy(&packed, &sel, sizeof(packed));
        return packed;
}

#endif
