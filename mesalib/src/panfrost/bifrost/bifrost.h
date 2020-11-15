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

#define BIFROST_DBG_MSGS        0x0001
#define BIFROST_DBG_SHADERS     0x0002

extern int bifrost_debug;

enum bifrost_message_type {
        BIFROST_MESSAGE_NONE       = 0,
        BIFROST_MESSAGE_VARYING    = 1,
        BIFROST_MESSAGE_ATTRIBUTE  = 2,
        BIFROST_MESSAGE_TEX        = 3,
        BIFROST_MESSAGE_VARTEX     = 4,
        BIFROST_MESSAGE_LOAD       = 5,
        BIFROST_MESSAGE_STORE      = 6,
        BIFROST_MESSAGE_ATOMIC     = 7,
        BIFROST_MESSAGE_BARRIER    = 8,
        BIFROST_MESSAGE_BLEND      = 9,
        BIFROST_MESSAGE_TILE       = 10,
        /* type 11 reserved */
        BIFROST_MESSAGE_Z_STENCIL  = 12,
        BIFROST_MESSAGE_ATEST      = 13,
        BIFROST_MESSAGE_JOB        = 14,
        BIFROST_MESSAGE_64BIT      = 15
};

enum bifrost_ftz {
        BIFROST_FTZ_DISABLE = 0,
        BIFROST_FTZ_DX11 = 1,
        BIFROST_FTZ_ALWAYS = 2,
        BIFROST_FTZ_ABRUPT = 3
};

enum bifrost_exceptions {
        BIFROST_EXCEPTIONS_ENABLED = 0,
        BIFROST_EXCEPTIONS_DISABLED = 1,
        BIFROST_EXCEPTIONS_PRECISE_DIVISION = 2,
        BIFROST_EXCEPTIONS_PRECISE_SQRT = 3,
};

/* Describes clause flow control, with respect to control flow and branch
 * reconvergence.
 *
 * Control flow may be considered back-to-back (execute clauses back-to-back),
 * non-back-to-back (switch warps after clause before the next clause), write
 * elision (back-to-back and elide register slot #3 write from the clause), or
 * end of shader.
 *
 * Branch reconvergence may be disabled, enabled unconditionally, or enabled
 * based on the program counter. A clause requires reconvergence if it has a
 * successor that can be executed without first executing the clause itself.
 * Separate iterations of a loop are treated separately here, so it is also the
 * case for a loop exit where the iteration count is not warp-invariant.
 *
 */

enum bifrost_flow {
        /* End-of-shader */
        BIFROST_FLOW_END = 0,

        /* Non back-to-back, PC-encoded reconvergence */
        BIFROST_FLOW_NBTB_PC = 1,

        /* Non back-to-back, unconditional reconvergence */
        BIFROST_FLOW_NBTB_UNCONDITIONAL = 2,

        /* Non back-to-back, no reconvergence */
        BIFROST_FLOW_NBTB = 3,

        /* Back-to-back, unconditional reconvergence */
        BIFROST_FLOW_BTB_UNCONDITIONAL = 4,

        /* Back-to-back, no reconvergence */
        BIFROST_FLOW_BTB_NONE = 5,

        /* Write elision, unconditional reconvergence */
        BIFROST_FLOW_WE_UNCONDITIONAL = 6,

        /* Write elision, no reconvergence */
        BIFROST_FLOW_WE = 7,
};

struct bifrost_header {
        /* Reserved */
        unsigned zero1 : 5;

        /* Flush-to-zero mode, leave zero for GL */
        enum bifrost_ftz flush_to_zero : 2;

        /* Convert any infinite result of any floating-point operation to the
         * biggest representable number */
        unsigned suppress_inf: 1;

        /* Convert NaN to +0.0 */
        unsigned suppress_nan : 1;

        /* Floating-point excception handling mode */
        enum bifrost_exceptions float_exceptions : 2;

        /* Enum describing the flow control, which matters for handling
         * divergence and reconvergence efficiently */
        enum bifrost_flow flow_control : 3;

        /* Reserved */
        unsigned zero2 : 1;

        /* Terminate discarded threads, rather than continuing execution. Set
         * for fragment shaders for standard GL behaviour of DISCARD. Also in a
         * fragment shader, this disables helper invocations, so cannot be used
         * in a shader that requires derivatives or texture LOD computation */
        unsigned terminate_discarded_threads : 1;

        /* If set, the hardware may prefetch the next clause. If false, the
         * hardware may not. Clear for unconditional branches. */
        unsigned next_clause_prefetch : 1;

        /* If set, a barrier will be inserted after the clause waiting for all
         * message passing instructions to read their staging registers, such
         * that it is safe for the next clause to write them. */
        unsigned staging_barrier: 1;
        unsigned staging_register : 6;

        /* Slots to wait on and slot to be used for message passing
         * instructions respectively */
        unsigned dependency_wait : 8;
        unsigned dependency_slot : 3;

        enum bifrost_message_type message_type : 5;
        enum bifrost_message_type next_message_type : 5;
} __attribute__((packed));

enum bifrost_packed_src {
        BIFROST_SRC_PORT0    = 0,
        BIFROST_SRC_PORT1    = 1,
        BIFROST_SRC_PORT2    = 2,
        BIFROST_SRC_STAGE    = 3,
        BIFROST_SRC_FAU_LO   = 4,
        BIFROST_SRC_FAU_HI   = 5,
        BIFROST_SRC_PASS_FMA = 6,
        BIFROST_SRC_PASS_ADD = 7,
};

struct bifrost_fma_inst {
        unsigned src0 : 3;
        unsigned op   : 20;
} __attribute__((packed));

struct bifrost_add_inst {
        unsigned src0 : 3;
        unsigned op   : 17;
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

enum bifrost_interp_mode {
        BIFROST_INTERP_CENTER = 0x0,
        BIFROST_INTERP_CENTROID = 0x1,
        BIFROST_INTERP_SAMPLE  = 0x2,
        BIFROST_INTERP_EXPLICIT = 0x3,
        BIFROST_INTERP_NONE = 0x4,
};

/* Fixed location for gl_FragCoord.zw */
#define BIFROST_FRAGZ (23)
#define BIFROST_FRAGW (22)

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

struct bifrost_regs {
        unsigned fau_idx : 8;
        unsigned reg3 : 6;
        unsigned reg2 : 6;
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

#define BIFROST_ADD_OP_BRANCH (0x0d000 >> 12)

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

/* 32-bit modes for slots 2/3, as encoded in the register block. Other values
 * are reserved. First part specifies behaviour of slot 2 (Idle, Read, Write
 * Full, Write Low, Write High), second part behaviour of slot 3, and the last
 * part specifies the source for the write (FMA, ADD, or MIX for FMA/ADD).
 *
 * IDLE is a special mode disabling both slots, except for the first
 * instruction in the clause which uses IDLE_1 for the same purpose.
 *
 * All fields 0 used as sentinel for reserved encoding, so IDLE(_1) have FMA
 * set (and ignored) as a placeholder to differentiate from reserved.
 */
enum bifrost_reg_mode {
        BIFROST_R_WL_FMA  = 1,
        BIFROST_R_WH_FMA  = 2,
        BIFROST_R_W_FMA   = 3,
        BIFROST_R_WL_ADD  = 4,
        BIFROST_R_WH_ADD  = 5,
        BIFROST_R_W_ADD   = 6,
        BIFROST_WL_WL_ADD = 7,
        BIFROST_WL_WH_ADD = 8,
        BIFROST_WL_W_ADD  = 9,
        BIFROST_WH_WL_ADD = 10,
        BIFROST_WH_WH_ADD = 11,
        BIFROST_WH_W_ADD  = 12,
        BIFROST_W_WL_ADD  = 13,
        BIFROST_W_WH_ADD  = 14,
        BIFROST_W_W_ADD   = 15,
        BIFROST_IDLE_1    = 16,
        BIFROST_I_W_FMA   = 17,
        BIFROST_I_WL_FMA  = 18,
        BIFROST_I_WH_FMA  = 19,
        BIFROST_R_I       = 20,
        BIFROST_I_W_ADD   = 21,
        BIFROST_I_WL_ADD  = 22,
        BIFROST_I_WH_ADD  = 23,
        BIFROST_WL_WH_MIX = 24,
        BIFROST_WH_WL_MIX = 26,
        BIFROST_IDLE      = 27,
};

enum bifrost_reg_op {
        BIFROST_OP_IDLE = 0,
        BIFROST_OP_READ = 1,
        BIFROST_OP_WRITE = 2,
        BIFROST_OP_WRITE_LO = 3,
        BIFROST_OP_WRITE_HI = 4,
};

struct bifrost_reg_ctrl_23 {
        enum bifrost_reg_op slot2;
        enum bifrost_reg_op slot3;
        bool slot3_fma;
};

static const struct bifrost_reg_ctrl_23 bifrost_reg_ctrl_lut[32] = {
        [BIFROST_R_WL_FMA]  = { BIFROST_OP_READ,     BIFROST_OP_WRITE_LO, true },
        [BIFROST_R_WH_FMA]  = { BIFROST_OP_READ,     BIFROST_OP_WRITE_HI, true },
        [BIFROST_R_W_FMA]   = { BIFROST_OP_READ,     BIFROST_OP_WRITE,    true },
        [BIFROST_R_WL_ADD]  = { BIFROST_OP_READ,     BIFROST_OP_WRITE_LO, false },
        [BIFROST_R_WH_ADD]  = { BIFROST_OP_READ,     BIFROST_OP_WRITE_HI, false },
        [BIFROST_R_W_ADD]   = { BIFROST_OP_READ,     BIFROST_OP_WRITE,    false },
        [BIFROST_WL_WL_ADD] = { BIFROST_OP_WRITE_LO, BIFROST_OP_WRITE_LO, false },
        [BIFROST_WL_WH_ADD] = { BIFROST_OP_WRITE_LO, BIFROST_OP_WRITE_HI, false },
        [BIFROST_WL_W_ADD]  = { BIFROST_OP_WRITE_LO, BIFROST_OP_WRITE,    false },
        [BIFROST_WH_WL_ADD] = { BIFROST_OP_WRITE_HI, BIFROST_OP_WRITE_LO, false },
        [BIFROST_WH_WH_ADD] = { BIFROST_OP_WRITE_HI, BIFROST_OP_WRITE_HI, false },
        [BIFROST_WH_W_ADD]  = { BIFROST_OP_WRITE_HI, BIFROST_OP_WRITE,    false },
        [BIFROST_W_WL_ADD]  = { BIFROST_OP_WRITE,    BIFROST_OP_WRITE_LO, false },
        [BIFROST_W_WH_ADD]  = { BIFROST_OP_WRITE,    BIFROST_OP_WRITE_HI, false },
        [BIFROST_W_W_ADD]   = { BIFROST_OP_WRITE,    BIFROST_OP_WRITE,    false },
        [BIFROST_IDLE_1]    = { BIFROST_OP_IDLE,     BIFROST_OP_IDLE,     true },
        [BIFROST_I_W_FMA]   = { BIFROST_OP_IDLE,     BIFROST_OP_WRITE,    true },
        [BIFROST_I_WL_FMA]  = { BIFROST_OP_IDLE,     BIFROST_OP_WRITE_LO, true },
        [BIFROST_I_WH_FMA]  = { BIFROST_OP_IDLE,     BIFROST_OP_WRITE_HI, true },
        [BIFROST_R_I]       = { BIFROST_OP_READ,     BIFROST_OP_IDLE,     false },
        [BIFROST_I_W_ADD]   = { BIFROST_OP_IDLE,     BIFROST_OP_WRITE,    false },
        [BIFROST_I_WL_ADD]  = { BIFROST_OP_IDLE,     BIFROST_OP_WRITE_LO, false },
        [BIFROST_I_WH_ADD]  = { BIFROST_OP_IDLE,     BIFROST_OP_WRITE_HI, false },
        [BIFROST_WL_WH_MIX] = { BIFROST_OP_WRITE_LO, BIFROST_OP_WRITE_HI, false },
        [BIFROST_WH_WL_MIX] = { BIFROST_OP_WRITE_HI, BIFROST_OP_WRITE_LO, false },
        [BIFROST_IDLE]      = { BIFROST_OP_IDLE,     BIFROST_OP_IDLE,     true },
};

/* Texture operator descriptors in various states. Usually packed in the
 * compiler and stored as a constant */

enum bifrost_index {
        /* Both texture/sampler index immediate */
        BIFROST_INDEX_IMMEDIATE_SHARED = 0,

        /* Sampler index immediate, texture index from staging */
        BIFROST_INDEX_IMMEDIATE_SAMPLER = 1,

        /* Texture index immediate, sampler index from staging */
        BIFROST_INDEX_IMMEDIATE_TEXTURE = 2,

        /* Both indices from (separate) staging registers */
        BIFROST_INDEX_REGISTER = 3,
};

enum bifrost_tex_op {
        /* Given explicit derivatives, compute a gradient descriptor */
        BIFROST_TEX_OP_GRDESC_DER = 4,

        /* Given implicit derivatives (texture coordinates in a fragment
         * shader), compute a gradient descriptor */
        BIFROST_TEX_OP_GRDESC = 5,

        /* Fetch a texel. Takes a staging register with LOD level / face index
         * packed 16:16 */
        BIFROST_TEX_OP_FETCH = 6,

        /* Filtered texture */
        BIFROST_TEX_OP_TEX = 7,
};

enum bifrost_lod_mode {
        /* Takes two staging registers forming a 64-bit gradient descriptor
         * (computed by a previous GRDESC or GRDESC_DER operation) */
        BIFROST_LOD_MODE_GRDESC = 3,

        /* Take a staging register with 8:8 fixed-point in bottom 16-bits
         * specifying an explicit LOD */
        BIFROST_LOD_MODE_EXPLICIT = 4,

        /* Takes a staging register with bottom 16-bits as 8:8 fixed-point LOD
         * bias and top 16-bit as 8:8 fixed-point lower bound (generally left
         * zero), added and clamped to a computed LOD */
        BIFROST_LOD_MODE_BIAS = 5,

        /* Set LOD to zero */
        BIFROST_LOD_MODE_ZERO = 6,

        /* Compute LOD */
        BIFROST_LOD_MODE_COMPUTE = 7,
};

enum bifrost_texture_format {
        /* 16-bit floating point, with optional clamping */
        BIFROST_TEXTURE_FORMAT_F16 = 0,
        BIFROST_TEXTURE_FORMAT_F16_POS = 1,
        BIFROST_TEXTURE_FORMAT_F16_PM1 = 2,
        BIFROST_TEXTURE_FORMAT_F16_1 = 3,

        /* 32-bit floating point, with optional clamping */
        BIFROST_TEXTURE_FORMAT_F32 = 4,
        BIFROST_TEXTURE_FORMAT_F32_POS = 5,
        BIFROST_TEXTURE_FORMAT_F32_PM1 = 6,
        BIFROST_TEXTURE_FORMAT_F32_1 = 7,
};

enum bifrost_texture_format_full {
        /* Transclude bifrost_texture_format from above */

        /* Integers, unclamped */
        BIFROST_TEXTURE_FORMAT_U16 = 12,
        BIFROST_TEXTURE_FORMAT_S16 = 13,
        BIFROST_TEXTURE_FORMAT_U32 = 14,
        BIFROST_TEXTURE_FORMAT_S32 = 15,
};

enum bifrost_texture_fetch {
        /* Default texelFetch */
        BIFROST_TEXTURE_FETCH_TEXEL = 1,

        /* Deprecated, fetches 4x U32 of a U8 x 4 texture. Do not use. */
        BIFROST_TEXTURE_FETCH_GATHER4_RGBA = 3,

        /* Gathers */
        BIFROST_TEXTURE_FETCH_GATHER4_R = 4,
        BIFROST_TEXTURE_FETCH_GATHER4_G = 5,
        BIFROST_TEXTURE_FETCH_GATHER4_B = 6,
        BIFROST_TEXTURE_FETCH_GATHER4_A = 7
};

struct bifrost_texture_operation {
        /* If immediate_indices is set:
         *     - immediate sampler index
         *     - index used as texture index
         * Otherwise:
         *      - bifrost_single_index in lower 2 bits
         *      - 0x3 in upper 2 bits (single-texturing)
         */
        unsigned sampler_index_or_mode : 4;
        unsigned index : 7;
        bool immediate_indices : 1;
        enum bifrost_tex_op op : 3;

        /* If set for TEX/FETCH, loads texel offsets and multisample index from
         * a staging register containing offset_x:offset_y:offset_z:ms_index
         * packed 8:8:8:8. Offsets must be in [-31, +31]. If set for
         * GRDESC(_DER), disable LOD bias. */
        bool offset_or_bias_disable : 1;

        /* If set for TEX/FETCH, loads fp32 shadow comparison value from a
         * staging register. Implies fetch_component = gather4_r. If set for
         * GRDESC(_DER), disables LOD clamping. */
        bool shadow_or_clamp_disable : 1;

        /* If set, loads an uint32 array index from a staging register. */
        bool array : 1;

        /* Texture dimension, or 0 for a cubemap */
        unsigned dimension : 2;

        /* Method to compute LOD value or for a FETCH, the
         * bifrost_texture_fetch component specification */
        enum bifrost_lod_mode lod_or_fetch : 3;

        /* Reserved */
        unsigned zero : 1;

        /* Register format for the result */
        enum bifrost_texture_format_full format : 4;

        /* Write mask for the result */
        unsigned mask : 4;
} __attribute__((packed));

#define BIFROST_MEGA_SAMPLE 128
#define BIFROST_ALL_SAMPLES 255
#define BIFROST_CURRENT_PIXEL 255

struct bifrost_pixel_indices {
        unsigned sample : 8;
        unsigned rt : 8;
        unsigned x : 8;
        unsigned y : 8;
} __attribute__((packed));

#endif
