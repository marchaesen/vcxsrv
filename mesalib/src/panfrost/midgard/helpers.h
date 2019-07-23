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

#define OP_IS_STORE_VARY(op) (\
		op == midgard_op_st_vary_16 || \
		op == midgard_op_st_vary_32 || \
                op == midgard_op_st_vary_32u || \
		op == midgard_op_st_vary_32i \
	)

#define OP_IS_STORE_R26(op) (\
                OP_IS_STORE_VARY(op) || \
                op == midgard_op_st_char || \
                op == midgard_op_st_char2 || \
                op == midgard_op_st_char4 || \
                op == midgard_op_st_short4 || \
                op == midgard_op_st_int4 \
        )

#define OP_IS_STORE(op) (\
                OP_IS_STORE_VARY(op) || \
                op == midgard_op_st_cubemap_coords \
	)

#define OP_IS_MOVE(op) ( \
                op == midgard_alu_op_fmov || \
                op == midgard_alu_op_imov \
        )

#define OP_IS_UBO_READ(op) ( \
                op == midgard_op_ld_uniform_32  || \
                op == midgard_op_ld_uniform_16  || \
                op == midgard_op_ld_uniform_32i \
        )

#define OP_IS_CSEL(op) ( \
                op == midgard_alu_op_icsel || \
                op == midgard_alu_op_icsel_v || \
                op == midgard_alu_op_fcsel_v || \
                op == midgard_alu_op_fcsel \
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
#define REGISTER_VARYING_BASE 26
#define REGISTER_OFFSET 27
#define REGISTER_TEXTURE_BASE 28
#define REGISTER_SELECT 31

/* SSA helper aliases to mimic the registers. UNUSED_0 encoded as an inline
 * constant. UNUSED_1 encoded as REGISTER_UNUSED */

#define SSA_UNUSED_0 0
#define SSA_UNUSED_1 -2

#define SSA_FIXED_SHIFT 24
#define SSA_FIXED_REGISTER(reg) ((1 + reg) << SSA_FIXED_SHIFT)
#define SSA_REG_FROM_FIXED(reg) ((reg >> SSA_FIXED_SHIFT) - 1)
#define SSA_FIXED_MINIMUM SSA_FIXED_REGISTER(0)

/* Swizzle support */

#define SWIZZLE(A, B, C, D) ((D << 6) | (C << 4) | (B << 2) | (A << 0))
#define SWIZZLE_FROM_ARRAY(r) SWIZZLE(r[0], r[1], r[2], r[3])
#define COMPONENT_X 0x0
#define COMPONENT_Y 0x1
#define COMPONENT_Z 0x2
#define COMPONENT_W 0x3

#define SWIZZLE_XXXX SWIZZLE(COMPONENT_X, COMPONENT_X, COMPONENT_X, COMPONENT_X)
#define SWIZZLE_XYXX SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_X, COMPONENT_X)
#define SWIZZLE_XYZX SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_Z, COMPONENT_X)
#define SWIZZLE_XYZW SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_Z, COMPONENT_W)
#define SWIZZLE_XYXZ SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_X, COMPONENT_Z)
#define SWIZZLE_XYZZ SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_Z, COMPONENT_Z)
#define SWIZZLE_WWWW SWIZZLE(COMPONENT_W, COMPONENT_W, COMPONENT_W, COMPONENT_W)

static inline unsigned
swizzle_of(unsigned comp)
{
        switch (comp) {
        case 1:
                return SWIZZLE_XXXX;
        case 2:
                return SWIZZLE_XYXX;
        case 3:
                return SWIZZLE_XYZX;
        case 4:
                return SWIZZLE_XYZW;
        default:
                unreachable("Invalid component count");
        }
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

/* Composes two swizzles */
static inline unsigned
pan_compose_swizzle(unsigned left, unsigned right)
{
        unsigned out = 0;

        for (unsigned c = 0; c < 4; ++c) {
                unsigned s = (left >> (2*c)) & 0x3;
                unsigned q = (right >> (2*s)) & 0x3;

                out |= (q << (2*c));
        }

        return out;
}

/* Applies a swizzle to an ALU source */

static inline unsigned
vector_alu_apply_swizzle(unsigned src, unsigned swizzle)
{
        midgard_vector_alu_src s =
                vector_alu_from_unsigned(src);

        s.swizzle = pan_compose_swizzle(s.swizzle, swizzle);

        return vector_alu_srco_unsigned(s);
}

#endif
