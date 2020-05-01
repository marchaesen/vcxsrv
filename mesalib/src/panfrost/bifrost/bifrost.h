/*
 * Copyright (C) 2019 Connor Abbott <cwabbott0@gmail.com>
 * Copyright (C) 2019 Lyude Paul <thatslyude@gmail.com>
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

#ifndef __bifrost_h__
#define __bifrost_h__

#include <stdint.h>
#include <stdbool.h>

enum bifrost_clause_type {
        BIFROST_CLAUSE_NONE       = 0,
        BIFROST_CLAUSE_LOAD_VARY  = 1,
        BIFROST_CLAUSE_UBO        = 2,
        BIFROST_CLAUSE_TEX        = 3,
        BIFROST_CLAUSE_SSBO_LOAD  = 5,
        BIFROST_CLAUSE_SSBO_STORE = 6,
        BIFROST_CLAUSE_BLEND      = 9,
        BIFROST_CLAUSE_ATEST      = 13,
        BIFROST_CLAUSE_64BIT      = 15
};

struct bifrost_header {
        unsigned unk0 : 7;
        // If true, convert any infinite result of any floating-point operation to
        // the biggest representable number.
        unsigned suppress_inf: 1;
        // Convert any NaN results to 0.
        unsigned suppress_nan : 1;
        unsigned unk1 : 2;
        // true if the execution mask of the next clause is the same as the mask of
        // the current clause.
        unsigned back_to_back : 1;
        unsigned no_end_of_shader: 1;
        unsigned unk2 : 2;
        // Set to true for fragment shaders, to implement this bit of spec text
        // from section 7.1.5 of the GLSL ES spec:
        //
        // "Stores to image and buffer variables performed by helper invocations
        // have no effect on the underlying image or buffer memory."
        //
        // Helper invocations are threads (invocations) corresponding to pixels in
        // a quad that aren't actually part of the triangle, but are included to
        // make derivatives work correctly. They're usually turned on, but they
        // need to be masked off for GLSL-level stores. This bit seems to be the
        // only bit that's actually different between fragment shaders and other
        // shaders, so this is probably what it's doing.
        unsigned elide_writes : 1;
        // If backToBack is off:
        // - true for conditional branches and fallthrough
        // - false for unconditional branches
        // The blob seems to always set it to true if back-to-back is on.
        unsigned branch_cond : 1;
        // This bit is set when the next clause writes to the data register of some
        // previous clause.
        unsigned datareg_writebarrier: 1;
        unsigned datareg : 6;
        unsigned scoreboard_deps: 8;
        unsigned scoreboard_index: 3;
        enum bifrost_clause_type clause_type: 4;
        unsigned unk3 : 1; // part of clauseType?
        enum bifrost_clause_type next_clause_type: 4;
        unsigned unk4 : 1; // part of nextClauseType?
} __attribute__((packed));

enum bifrost_packed_src {
        BIFROST_SRC_PORT0    = 0,
        BIFROST_SRC_PORT1    = 1,
        BIFROST_SRC_PORT3    = 2,
        BIFROST_SRC_STAGE    = 3,
        BIFROST_SRC_CONST_LO = 4,
        BIFROST_SRC_CONST_HI = 5,
        BIFROST_SRC_PASS_FMA = 6,
        BIFROST_SRC_PASS_ADD = 7,
};

#define BIFROST_FMA_EXT (0xe0000)
#define BIFROST_FMA_OP_MOV BIFROST_FMA_EXT | (0x32d)
#define BIFROST_FMA_OP_FREXPE_LOG BIFROST_FMA_EXT | 0x3c5
#define BIFROST_FMA_OP_ADD_FREXPM ((BIFROST_FMA_EXT | 0x1e80) >> 3)
#define BIFROST_FMA_SEL_16(swiz) (((BIFROST_FMA_EXT | 0x1e00) >> 3) | (swiz))

#define BIFROST_FMA_ROUND_16(mode, swiz) (BIFROST_FMA_EXT | 0x1800 | (swiz) | ((mode) << 6))
#define BIFROST_FMA_ROUND_32(mode) (BIFROST_FMA_EXT | 0x1805 | ((mode) << 6))

struct bifrost_fma_inst {
        unsigned src0 : 3;
        unsigned op   : 20;
} __attribute__((packed));

struct bifrost_fma_2src {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned op   : 17;
} __attribute__((packed));

#define BIFROST_FMA_OP_SEL8 (0x71)

struct bifrost_fma_sel8 {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src2 : 3;
        unsigned src3 : 3;
        unsigned swizzle : 4;
        unsigned op   : 7;
} __attribute__((packed));

#define BIFROST_FMA_OP_MSCALE (0x50 >> 3)

struct bifrost_fma_mscale {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src2 : 3;
        unsigned src3 : 3;

        /* If mscale_mode is set - an MSCALE specific mode. If it is not set, a
         * regular outmod */
        unsigned mode : 2;
        unsigned mscale_mode : 1;

        unsigned src0_abs : 1;
        unsigned src1_neg : 1;
        unsigned src2_neg : 1;
        unsigned op   : 5;
} __attribute__((packed));

#define BIFROST_ADD_OP_BLEND (0x1952c)
#define BIFROST_ADD_OP_FRCP_FAST_F32 (0x0cc00)
#define BIFROST_ADD_OP_FRCP_FAST_F16_X (0x0ce10)
#define BIFROST_ADD_OP_FRCP_FAST_F16_Y (0x0ce30)
#define BIFROST_ADD_OP_FRSQ_FAST_F32 (0x0cc20)
#define BIFROST_ADD_OP_FRSQ_FAST_F16_X (0x0ce50)
#define BIFROST_ADD_OP_FRSQ_FAST_F16_Y (0x0ce70)
#define BIFROST_ADD_OP_LOG2_HELP  (0x0cc68)
#define BIFROST_ADD_OP_FEXP2_FAST (0x0cd58)

struct bifrost_add_inst {
        unsigned src0 : 3;
        unsigned op   : 17;
} __attribute__((packed));

#define BIFROST_ADD_OP_LD_UBO_1 (0x0c1a0 >> 3)
#define BIFROST_ADD_OP_LD_UBO_2 (0x0c1e0 >> 3)
#define BIFROST_ADD_OP_LD_UBO_3 (0x0caa0 >> 3)
#define BIFROST_ADD_OP_LD_UBO_4 (0x0c220 >> 3)
#define BIFROST_ADD_SEL_16(swiz) ((0xea60 >> 3) | (swiz))

struct bifrost_add_2src {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned op   : 14;
} __attribute__((packed));

#define BIFROST_ADD_OP_FMAX32 (0x00)
#define BIFROST_ADD_OP_FMIN32 (0x01)
#define BIFROST_ADD_OP_FADD32 (0x02)

#define BIFROST_ADD_OP_FADD16 (0x0A)

struct bifrost_add_faddmin {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src1_abs : 1;
        unsigned src0_neg : 1;
        unsigned src1_neg : 1;
        unsigned select : 2; /* swizzle_0 for fp16 */
        unsigned outmod : 2; /* swizzle_1 for fp16 */
        unsigned mode : 2;
        unsigned src0_abs : 1;
        unsigned op   : 4;
} __attribute__((packed));

#define BIFROST_ADD_OP_FMAX16 (0x10)
#define BIFROST_ADD_OP_FMIN16 (0x12)

struct bifrost_add_fmin16 {
        unsigned src0 : 3;
        unsigned src1 : 3;
        /* abs2 inferred as with FMA */
        unsigned abs1 : 1;
        unsigned src0_neg : 1;
        unsigned src1_neg : 1;
        unsigned src0_swizzle : 2;
        unsigned src1_swizzle : 2;
        unsigned mode : 2;
        unsigned op : 5;
} __attribute__((packed));

#define BIFROST_ADD_OP_ST_VAR (0x19300 >> 8)

struct bifrost_st_vary {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src2 : 3;
        unsigned channels : 2;
        unsigned op   : 9;
} __attribute__((packed));

#define BIFROST_ADD_OP_ATEST (0xc8f)

struct bifrost_add_atest {
        /* gl_SampleMask (R60) */
        unsigned src0 : 3;

        /* Alpha value */
        unsigned src1 : 3;

        /* If half, X/Y select. If !half, always set */
        unsigned component : 1;
        unsigned half : 1;

        unsigned op   : 12;
} __attribute__((packed));

enum bifrost_outmod {
        BIFROST_NONE = 0x0,
        BIFROST_POS = 0x1,
        BIFROST_SAT_SIGNED = 0x2,
        BIFROST_SAT = 0x3,
};

enum bifrost_roundmode {
        BIFROST_RTE = 0x0, /* round to even */
        BIFROST_RTP = 0x1, /* round to positive */
        BIFROST_RTN = 0x2, /* round to negative */
        BIFROST_RTZ = 0x3 /* round to zero */
};

/* NONE: Same as fmax() and fmin() -- return the other
 * number if any number is NaN.  Also always return +0 if
 * one argument is +0 and the other is -0.
 *
 * NAN_WINS: Instead of never returning a NaN, always return
 * one. The "greater"/"lesser" NaN is always returned, first
 * by checking the sign and then the mantissa bits.
 *
 * SRC1_WINS: For max, implement src0 > src1 ? src0 : src1.
 * For min, implement src0 < src1 ? src0 : src1.  This
 * includes handling NaN's and signedness of 0 differently
 * from above, since +0 and -0 compare equal and comparisons
 * always return false for NaN's. As a result, this mode is
 * *not* commutative.
 *
 * SRC0_WINS: For max, implement src0 < src1 ? src1 : src0
 * For min, implement src0 > src1 ? src1 : src0
 */


enum bifrost_minmax_mode {
        BIFROST_MINMAX_NONE = 0x0,
        BIFROST_NAN_WINS    = 0x1,
        BIFROST_SRC1_WINS   = 0x2,
        BIFROST_SRC0_WINS   = 0x3,
};

#define BIFROST_FMA_OP_FADD32 (0x58 >> 2)
#define BIFROST_FMA_OP_FMAX32 (0x40 >> 2)
#define BIFROST_FMA_OP_FMIN32 (0x44 >> 2)

struct bifrost_fma_add {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src1_abs : 1;
        unsigned src0_neg : 1;
        unsigned src1_neg : 1;
        unsigned unk : 3;
        unsigned src0_abs : 1;
        enum bifrost_roundmode roundmode : 2;
        enum bifrost_outmod outmod : 2;
        unsigned op : 6;
} __attribute__((packed));

#define BIFROST_FMA_OP_FMAX16 (0xC0 >> 2)
#define BIFROST_FMA_OP_FMIN16 (0xCC >> 2)
#define BIFROST_FMA_OP_FADD16 (0xD8 >> 2)

struct bifrost_fma_add_minmax16 {
        unsigned src0 : 3;
        unsigned src1 : 3;
        /* abs2 inferred as (src1 < src0) */
        unsigned abs1 : 1;
        unsigned src0_neg : 1;
        unsigned src1_neg : 1;
        unsigned src0_swizzle : 2;
        unsigned src1_swizzle : 2;
        unsigned mode : 2;
        enum bifrost_outmod outmod : 2;
        /* roundmode for add, min/max mode for min/max */
        unsigned op : 6;
} __attribute__((packed));

#define BIFROST_FMA_OP_FMA (0x00)

struct bifrost_fma_fma {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src2 : 3;
        unsigned src_expand : 3;
        unsigned src0_abs : 1;
        enum bifrost_roundmode roundmode : 2;
        enum bifrost_outmod outmod : 2;
        unsigned src0_neg : 1; /* 14 */
        unsigned src2_neg : 1;
        unsigned src1_abs : 1;
        unsigned src2_abs : 1; /* 17 */
        unsigned op : 2;
} __attribute__((packed));

#define BIFROST_FMA_OP_FMA16 (0x2)

struct bifrost_fma_fma16 {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src2 : 3;
        unsigned swizzle_0 : 2;
        unsigned swizzle_1 : 2;
        enum bifrost_roundmode roundmode : 2;
        enum bifrost_outmod outmod : 2;
        unsigned src0_neg : 1;
        unsigned src2_neg : 1;
        unsigned swizzle_2 : 2;
        unsigned op : 2;
} __attribute__((packed));

enum bifrost_csel_cond {
        BIFROST_FEQ_F = 0x0,
        BIFROST_FGT_F = 0x1,
        BIFROST_FGE_F = 0x2,
        BIFROST_IEQ_F = 0x3,
        BIFROST_IGT_I = 0x4,
        BIFROST_IGE_I = 0x5,
        BIFROST_UGT_I = 0x6,
        BIFROST_UGE_I = 0x7
};

#define BIFROST_FMA_OP_CSEL4     (0x5c)
#define BIFROST_FMA_OP_CSEL4_V16 (0xdc)

struct bifrost_csel4 {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src2 : 3;
        unsigned src3 : 3;
        enum bifrost_csel_cond cond : 3;
        unsigned op   : 8;
} __attribute__((packed));

#define BIFROST_FMA_OP_RSHIFT_NAND     (0x60000 >> 12)
#define BIFROST_FMA_OP_RSHIFT_AND      (0x61000 >> 12)
#define BIFROST_FMA_OP_LSHIFT_NAND     (0x62000 >> 12)
#define BIFROST_FMA_OP_LSHIFT_AND      (0x63000 >> 12)
#define BIFROST_FMA_OP_RSHIFT_XOR      (0x64000 >> 12)
#define BIFROST_FMA_OP_LSHIFT_ADD_32   (0x65200 >> 6)
#define BIFROST_FMA_OP_LSHIFT_SUB_32   (0x65600 >> 6)
#define BIFROST_FMA_OP_LSHIFT_RSUB_32  (0x65a00 >> 6)
#define BIFROST_FMA_OP_RSHIFT_ADD_32   (0x65e00 >> 6)
#define BIFROST_FMA_OP_RSHIFT_SUB_32   (0x66200 >> 6)
#define BIFROST_FMA_OP_RSHIFT_RSUB_32  (0x66600 >> 6)

struct bifrost_shift_fma {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src2 : 3;
        unsigned half : 3;
        unsigned unk  : 1; /* always set? */
        unsigned invert_1 : 1; /* Inverts sources to combining op */
        /* For XOR, switches RSHIFT to LSHIFT since only one invert needed */
        unsigned invert_2 : 1;
        unsigned op : 8;
} __attribute__((packed));

struct bifrost_shift_add {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src2 : 3;
        unsigned zero : 2;

        unsigned invert_1 : 1;
        unsigned invert_2 : 1;

        unsigned op : 7;
} __attribute__((packed));

enum bifrost_fcmp_cond {
        BIFROST_OEQ = 0,
        BIFROST_OGT = 1,
        BIFROST_OGE = 2,
        BIFROST_UNE = 3,
        BIFROST_OLT = 4,
        BIFROST_OLE = 5,
};

#define BIFROST_FMA_OP_FCMP_GL (0x48000 >> 13)
#define BIFROST_FMA_OP_FCMP_D3D (0x4c000 >> 13)

struct bifrost_fma_fcmp {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned src1_abs : 1;
        unsigned unk1 : 1;
        unsigned src1_neg : 1;
        unsigned src_expand : 3;
        unsigned src0_abs : 1;
        enum bifrost_fcmp_cond cond : 3;
        unsigned op   : 7;
} __attribute__((packed));

struct bifrost_add_fcmp {
        unsigned src0 : 3;
        unsigned src1 : 3;
        enum bifrost_fcmp_cond cond : 3;
        unsigned src_expand : 2;
        unsigned src0_abs : 1;
        unsigned src1_abs : 1;
        unsigned src1_neg : 1;
        unsigned op   : 6;
} __attribute__((packed));

#define BIFROST_FMA_OP_FCMP_GL_16 (0xc8000 >> 13)
#define BIFROST_FMA_OP_FCMP_D3D_16 (0xcc000 >> 13)

struct bifrost_fma_fcmp16 {
        unsigned src0 : 3;
        unsigned src1 : 3;

        /* abs2 inferred */
        unsigned abs1 : 1;
        unsigned unk : 2;

        unsigned src0_swizzle : 2;
        unsigned src1_swizzle : 2;

        enum bifrost_fcmp_cond cond : 3;
        unsigned op   : 7;
} __attribute__((packed));

struct bifrost_add_fcmp16 {
        unsigned src0 : 3;
        unsigned src1 : 3;
        enum bifrost_fcmp_cond cond : 3;

        unsigned src0_swizzle : 2;
        unsigned src1_swizzle : 2;

        /* No abs mods */
        unsigned src0_neg : 1;

        unsigned op   : 6;
} __attribute__((packed));

enum bifrost_icmp_cond {
        BIFROST_ICMP_IGT = 0,
        BIFROST_ICMP_IGE = 1,
        BIFROST_ICMP_UGT = 2,
        BIFROST_ICMP_UGE = 3,
        BIFROST_ICMP_EQ  = 4,
        BIFROST_ICMP_NEQ  = 5,
};

struct bifrost_fma_icmp32 {
        unsigned src0 : 3;
        unsigned src1 : 3;
        enum bifrost_icmp_cond cond : 3;
        unsigned unk1 : 1; /* set */
        unsigned d3d : 1;
        unsigned op : 12;
} __attribute__((packed));

struct bifrost_fma_icmp16 {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned unk : 5; /* 11010 */
        enum bifrost_icmp_cond cond : 3;
        unsigned op : 9;
} __attribute__((packed));

struct bifrost_add_icmp {
        unsigned src0 : 3;
        unsigned src1 : 3;
        enum bifrost_icmp_cond cond : 3;
        unsigned sz : 1; /* 1 for 32, 0 for 8 */
        unsigned d3d : 1;
        unsigned op : 9;
} __attribute__((packed));

/* Two sources for vectorization */
#define BIFROST_FMA_FLOAT32_TO_16 (0xdd000 >> 3)
#define BIFROST_ADD_FLOAT32_TO_16 (0x0EC00 >> 3)

enum bifrost_convert_mode {
        BIFROST_CONV_UNK0 = 0,
        BIFROST_CONV_F32_TO_I32 = 1,
        BIFROST_CONV_F16_TO_I16 = 2,
        BIFROST_CONV_I32_TO_F32 = 3,
        BIFROST_CONV_I16_TO_X32 = 4,
        BIFROST_CONV_F16_TO_F32 = 5,
        BIFROST_CONV_I16_TO_F16 = 6,
        BIFROST_CONV_UNK7 = 7
};

/* i16 to x32 */
#define BIFROST_CONVERT_4(is_unsigned, component, to_float) \
        ((is_unsigned & 1) | ((component & 1) << 1) | ((to_float & 1) << 2) | \
         ((0x3) << 3) | ((4) << 5) | 0x100)

/* f16 to f32 */
#define BIFROST_CONVERT_5(component) \
        ((component & 1) | ((1) << 1) | ((5) << 5) | 0x100)

/* Other conversions */
#define BIFROST_CONVERT(is_unsigned, roundmode, swizzle, mode) \
        ((is_unsigned & 1) | ((roundmode & 3) << 1) | ((swizzle & 3) << 3) | ((mode & 7) << 5))

#define BIFROST_FMA_CONVERT (0xe0000)
#define BIFROST_ADD_CONVERT (0x07800)

enum bifrost_ldst_type {
        BIFROST_LDST_F16 = 0,
        BIFROST_LDST_F32 = 1,
        BIFROST_LDST_I32 = 2,
        BIFROST_LDST_U32 = 3
};

#define BIFROST_ADD_OP_LD_VAR_ADDR (0x18000 >> 10)

struct bifrost_ld_var_addr {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned location : 5;
        enum bifrost_ldst_type type : 2;
        unsigned op : 7;
} __attribute__((packed));

#define BIFROST_ADD_OP_LD_ATTR (0x08000 >> 12)

struct bifrost_ld_attr {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned location : 5;
        unsigned channels : 2; /* MALI_POSITIVE */
        enum bifrost_ldst_type type : 2;
        unsigned op : 5;
} __attribute__((packed));

enum bifrost_interp_mode {
        BIFROST_INTERP_PER_FRAG = 0x0,
        BIFROST_INTERP_CENTROID = 0x1,
        BIFROST_INTERP_DEFAULT  = 0x2,
        BIFROST_INTERP_EXPLICIT = 0x3
};

#define BIFROST_ADD_OP_LD_VAR_16 (0x1a << 1)
#define BIFROST_ADD_OP_LD_VAR_32 (0x0a << 1)

struct bifrost_ld_var {
        unsigned src0 : 3;

        /* If top two bits set, indirect with src in bottom three */
        unsigned addr : 5;

        unsigned channels : 2; /* MALI_POSITIVE */
        enum bifrost_interp_mode interp_mode : 2;
        unsigned reuse : 1;
        unsigned flat : 1;
        unsigned op : 6;
} __attribute__((packed));

struct bifrost_tex_ctrl {
        unsigned sampler_index : 4; // also used to signal indirects
        unsigned tex_index : 7;
        bool no_merge_index : 1; // whether to merge (direct) sampler & texture indices
        bool filter : 1; // use the usual filtering pipeline (0 for texelFetch & textureGather)
        unsigned unk0 : 2;
        bool texel_offset : 1; // *Offset()
        bool is_shadow : 1;
        bool is_array : 1;
        unsigned tex_type : 2; // 2D, 3D, Cube, Buffer
        bool compute_lod : 1; // 0 for *Lod()
        bool not_supply_lod : 1; // 0 for *Lod() or when a bias is applied
        bool calc_gradients : 1; // 0 for *Grad()
        unsigned unk1 : 1;
        unsigned result_type : 4; // integer, unsigned, float TODO: why is this 4 bits?
        unsigned unk2 : 4;
} __attribute__((packed));

struct bifrost_dual_tex_ctrl {
        unsigned sampler_index0 : 2;
        unsigned unk0 : 2;
        unsigned tex_index0 : 2;
        unsigned sampler_index1 : 2;
        unsigned tex_index1 : 2;
        unsigned unk1 : 22;
} __attribute__((packed));

#define BIFROST_ADD_OP_TEX_COMPACT_F32 (0x0b000 >> 10)
#define BIFROST_ADD_OP_TEX_COMPACT_F16 (0x1b000 >> 10)

struct bifrost_tex_compact {
        unsigned src0 : 3;
        unsigned src1 : 3;
        unsigned tex_index : 3;
        unsigned unknown : 1;
        unsigned sampler_index : 3;
        unsigned op   : 7;
} __attribute__((packed));

enum branch_bit_size {
        BR_SIZE_32 = 0,
        BR_SIZE_16XX = 1,
        BR_SIZE_16YY = 2,
        // For the above combinations of bitsize and location, an extra bit is
        // encoded via comparing the sources. The only possible source of ambiguity
        // would be if the sources were the same, but then the branch condition
        // would be always true or always false anyways, so we can ignore it. But
        // this no longer works when comparing the y component to the x component,
        // since it's valid to compare the y component of a source against its own
        // x component. Instead, the extra bit is encoded via an extra bitsize.
        BR_SIZE_16YX0 = 3,
        BR_SIZE_16YX1 = 4,
        BR_SIZE_32_AND_16X = 5,
        BR_SIZE_32_AND_16Y = 6,
        // Used for comparisons with zero and always-true, see below. I think this
        // only works for integer comparisons.
        BR_SIZE_ZERO = 7,
};

enum bifrost_reg_write_unit {
        REG_WRITE_NONE = 0, // don't write
        REG_WRITE_TWO, // write using reg2
        REG_WRITE_THREE, // write using reg3
};

struct bifrost_regs {
        unsigned uniform_const : 8;
        unsigned reg2 : 6;
        unsigned reg3 : 6;
        unsigned reg0 : 5;
        unsigned reg1 : 6;
        unsigned ctrl : 4;
} __attribute__((packed));

enum bifrost_branch_cond {
        BR_COND_LT = 0,
        BR_COND_LE = 1,
        BR_COND_GE = 2,
        BR_COND_GT = 3,
        // Equal vs. not-equal determined by src0/src1 comparison
        BR_COND_EQ = 4,
        // floating-point comparisons
        // Becomes UNE when you flip the arguments
        BR_COND_OEQ = 5,
        // TODO what happens when you flip the arguments?
        BR_COND_OGT = 6,
        BR_COND_OLT = 7,
};

enum bifrost_branch_code {
        BR_ALWAYS = 63,
};

struct bifrost_branch {
        unsigned src0 : 3;

        /* For BR_SIZE_ZERO, upper two bits become ctrl */
        unsigned src1 : 3;

        /* Offset source -- always uniform/const but
         * theoretically could support indirect jumps? */
        unsigned src2 : 3;

        enum bifrost_branch_cond cond : 3;
        enum branch_bit_size size : 3;

        unsigned op : 5;
};

/* Clause packing */

#define BIFROST_FMA_NOP (0x701960 | BIFROST_SRC_STAGE)
#define BIFROST_ADD_NOP (0x3D960 | BIFROST_SRC_STAGE)

struct bifrost_fmt1 {
        unsigned ins_0 : 3;
        unsigned tag : 5;
        uint64_t ins_1 : 64;
        unsigned ins_2 : 11;
        uint64_t header : 45;
} __attribute__((packed));

#define BIFROST_FMT1_INSTRUCTIONS    0b00101
#define BIFROST_FMT1_FINAL           0b01001
#define BIFROST_FMT1_CONSTANTS       0b00001

#define BIFROST_FMTC_CONSTANTS       0b0011
#define BIFROST_FMTC_FINAL           0b0111

struct bifrost_fmt_constant {
        unsigned pos : 4;
        unsigned tag : 4;
        uint64_t imm_1 : 60;
        uint64_t imm_2 : 60;
} __attribute__((packed));

enum bifrost_reg_control {
        BIFROST_WRITE_FMA_P2         = 1,
        BIFROST_WRITE_FMA_P2_READ_P3 = 2,
        BIFROST_FIRST_WRITE_FMA_P2_READ_P3 = 3,
        BIFROST_READ_P3              = 4,
        BIFROST_WRITE_ADD_P2         = 5,
        BIFROST_WRITE_ADD_P2_READ_P3 = 6,
        BIFROST_WRITE_ADD_P2_FMA_P3  = 7,

        BIFROST_FIRST_NONE           = 8,
        BIFROST_FIRST_WRITE_FMA_P2   = 9,
        /* INSTR_INVALID_ENC */
        BIFROST_REG_NONE             = 11,
        BIFROST_FIRST_READ_P3        = 12,
        BIFROST_FIRST_WRITE_ADD_P2   = 13,
        BIFROST_FIRST_WRITE_ADD_P2_READ_P3 = 14,
        BIFROST_FIRST_WRITE_ADD_P2_FMA_P3  = 15
};

#endif
