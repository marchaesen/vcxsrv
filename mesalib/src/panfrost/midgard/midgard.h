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

#ifndef __midgard_h__
#define __midgard_h__

#include <stdint.h>
#include <stdbool.h>

#define MIDGARD_DBG_MSGS		0x0001
#define MIDGARD_DBG_SHADERS		0x0002
#define MIDGARD_DBG_SHADERDB            0x0004

extern int midgard_debug;

typedef enum {
        midgard_word_type_alu,
        midgard_word_type_load_store,
        midgard_word_type_texture,
        midgard_word_type_unknown
} midgard_word_type;

typedef enum {
        midgard_alu_vmul,
        midgard_alu_sadd,
        midgard_alu_smul,
        midgard_alu_vadd,
        midgard_alu_lut
} midgard_alu;

enum {
        TAG_INVALID = 0x0,
        TAG_BREAK = 0x1,
        TAG_TEXTURE_4_VTX = 0x2,
        TAG_TEXTURE_4 = 0x3,
        TAG_TEXTURE_4_BARRIER = 0x4,
        TAG_LOAD_STORE_4 = 0x5,
        TAG_UNKNOWN_1 = 0x6,
        TAG_UNKNOWN_2 = 0x7,
        TAG_ALU_4 = 0x8,
        TAG_ALU_8 = 0x9,
        TAG_ALU_12 = 0xA,
        TAG_ALU_16 = 0xB,
        TAG_ALU_4_WRITEOUT = 0xC,
        TAG_ALU_8_WRITEOUT = 0xD,
        TAG_ALU_12_WRITEOUT = 0xE,
        TAG_ALU_16_WRITEOUT = 0xF
};

/*
 * ALU words
 */

typedef enum {
        midgard_alu_op_fadd       = 0x10,
        midgard_alu_op_fmul       = 0x14,

        midgard_alu_op_fmin       = 0x28,
        midgard_alu_op_fmax       = 0x2C,

        midgard_alu_op_fmov       = 0x30, /* fmov_rte */
        midgard_alu_op_fmov_rtz   = 0x31,
        midgard_alu_op_fmov_rtn   = 0x32,
        midgard_alu_op_fmov_rtp   = 0x33,
        midgard_alu_op_froundeven = 0x34,
        midgard_alu_op_ftrunc     = 0x35,
        midgard_alu_op_ffloor     = 0x36,
        midgard_alu_op_fceil      = 0x37,
        midgard_alu_op_ffma       = 0x38,
        midgard_alu_op_fdot3      = 0x3C,
        midgard_alu_op_fdot3r     = 0x3D,
        midgard_alu_op_fdot4      = 0x3E,
        midgard_alu_op_freduce    = 0x3F,

        midgard_alu_op_iadd       = 0x40,
        midgard_alu_op_ishladd    = 0x41, /* a + (b<<1) */
        midgard_alu_op_isub       = 0x46,
        midgard_alu_op_iaddsat    = 0x48,
        midgard_alu_op_uaddsat    = 0x49,
        midgard_alu_op_isubsat    = 0x4E,
        midgard_alu_op_usubsat    = 0x4F,

        midgard_alu_op_imul       = 0x58,

        midgard_alu_op_imin       = 0x60,
        midgard_alu_op_umin       = 0x61,
        midgard_alu_op_imax       = 0x62,
        midgard_alu_op_umax       = 0x63,
        midgard_alu_op_ihadd      = 0x64,
        midgard_alu_op_uhadd      = 0x65,
        midgard_alu_op_irhadd     = 0x66,
        midgard_alu_op_urhadd     = 0x67,
        midgard_alu_op_iasr       = 0x68,
        midgard_alu_op_ilsr       = 0x69,
        midgard_alu_op_ishl       = 0x6E,

        midgard_alu_op_iand       = 0x70,
        midgard_alu_op_ior        = 0x71,
        midgard_alu_op_inand      = 0x72, /* ~(a & b), for inot let a = b */
        midgard_alu_op_inor       = 0x73, /* ~(a | b) */
        midgard_alu_op_iandnot    = 0x74, /* (a & ~b), used for not/b2f */
        midgard_alu_op_iornot     = 0x75, /* (a | ~b) */
        midgard_alu_op_ixor       = 0x76,
        midgard_alu_op_inxor      = 0x77, /* ~(a & b) */
        midgard_alu_op_iclz       = 0x78, /* Number of zeroes on left */
        midgard_alu_op_ibitcount8 = 0x7A, /* Counts bits in 8-bit increments */
        midgard_alu_op_imov       = 0x7B,
        midgard_alu_op_iabsdiff   = 0x7C,
        midgard_alu_op_uabsdiff   = 0x7D,
        midgard_alu_op_ichoose    = 0x7E, /* vector, component number - dupe for shuffle() */

        midgard_alu_op_feq        = 0x80,
        midgard_alu_op_fne        = 0x81,
        midgard_alu_op_flt        = 0x82,
        midgard_alu_op_fle        = 0x83,
        midgard_alu_op_fball_eq   = 0x88,
        midgard_alu_op_fball_neq  = 0x89,
        midgard_alu_op_fball_lt   = 0x8A, /* all(lessThan(.., ..)) */
        midgard_alu_op_fball_lte  = 0x8B, /* all(lessThanEqual(.., ..)) */

        midgard_alu_op_fbany_eq   = 0x90,
        midgard_alu_op_fbany_neq  = 0x91,
        midgard_alu_op_fbany_lt   = 0x92, /* any(lessThan(.., ..)) */
        midgard_alu_op_fbany_lte  = 0x93, /* any(lessThanEqual(.., ..)) */

        midgard_alu_op_f2i_rte    = 0x98,
        midgard_alu_op_f2i_rtz    = 0x99,
        midgard_alu_op_f2i_rtn    = 0x9A,
        midgard_alu_op_f2i_rtp    = 0x9B,
        midgard_alu_op_f2u_rte    = 0x9C,
        midgard_alu_op_f2u_rtz    = 0x9D,
        midgard_alu_op_f2u_rtn    = 0x9E,
        midgard_alu_op_f2u_rtp    = 0x9F,

        midgard_alu_op_ieq        = 0xA0,
        midgard_alu_op_ine        = 0xA1,
        midgard_alu_op_ult        = 0xA2,
        midgard_alu_op_ule        = 0xA3,
        midgard_alu_op_ilt        = 0xA4,
        midgard_alu_op_ile        = 0xA5,
        midgard_alu_op_iball_eq   = 0xA8,
        midgard_alu_op_iball_neq  = 0xA9,
        midgard_alu_op_uball_lt   = 0xAA,
        midgard_alu_op_uball_lte  = 0xAB,
        midgard_alu_op_iball_lt   = 0xAC,
        midgard_alu_op_iball_lte  = 0xAD,

        midgard_alu_op_ibany_eq   = 0xB0,
        midgard_alu_op_ibany_neq  = 0xB1,
        midgard_alu_op_ubany_lt   = 0xB2,
        midgard_alu_op_ubany_lte  = 0xB3,
        midgard_alu_op_ibany_lt   = 0xB4, /* any(lessThan(.., ..)) */
        midgard_alu_op_ibany_lte  = 0xB5, /* any(lessThanEqual(.., ..)) */
        midgard_alu_op_i2f_rte    = 0xB8,
        midgard_alu_op_i2f_rtz    = 0xB9,
        midgard_alu_op_i2f_rtn    = 0xBA,
        midgard_alu_op_i2f_rtp    = 0xBB,
        midgard_alu_op_u2f_rte    = 0xBC,
        midgard_alu_op_u2f_rtz    = 0xBD,
        midgard_alu_op_u2f_rtn    = 0xBE,
        midgard_alu_op_u2f_rtp    = 0xBF,

        midgard_alu_op_icsel_v    = 0xC0, /* condition code r31 */
        midgard_alu_op_icsel      = 0xC1, /* condition code r31.w */
        midgard_alu_op_fcsel_v    = 0xC4,
        midgard_alu_op_fcsel      = 0xC5,
        midgard_alu_op_fround     = 0xC6,

        midgard_alu_op_fatan_pt2  = 0xE8,
        midgard_alu_op_fpow_pt1   = 0xEC,
        midgard_alu_op_fpown_pt1  = 0xED,
        midgard_alu_op_fpowr_pt1  = 0xEE,

        midgard_alu_op_frcp       = 0xF0,
        midgard_alu_op_frsqrt     = 0xF2,
        midgard_alu_op_fsqrt      = 0xF3,
        midgard_alu_op_fexp2      = 0xF4,
        midgard_alu_op_flog2      = 0xF5,
        midgard_alu_op_fsin       = 0xF6,
        midgard_alu_op_fcos       = 0xF7,
        midgard_alu_op_fatan2_pt1 = 0xF9,
} midgard_alu_op;

typedef enum {
        midgard_outmod_none = 0,
        midgard_outmod_pos  = 1, /* max(x, 0.0) */
        midgard_outmod_sat_signed  = 2, /* clamp(x, -1.0, 1.0) */
        midgard_outmod_sat  = 3 /* clamp(x, 0.0, 1.0) */
} midgard_outmod_float;

typedef enum {
        midgard_outmod_int_saturate = 0,
        midgard_outmod_uint_saturate = 1,
        midgard_outmod_int_wrap = 2,
        midgard_outmod_int_high = 3, /* Overflowed portion */
} midgard_outmod_int;

typedef enum {
        midgard_reg_mode_8 = 0,
        midgard_reg_mode_16 = 1,
        midgard_reg_mode_32 = 2,
        midgard_reg_mode_64 = 3
} midgard_reg_mode;

typedef enum {
        midgard_dest_override_lower = 0,
        midgard_dest_override_upper = 1,
        midgard_dest_override_none = 2
} midgard_dest_override;

typedef enum {
        midgard_int_sign_extend = 0,
        midgard_int_zero_extend = 1,
        midgard_int_normal = 2,
        midgard_int_shift = 3
} midgard_int_mod;

#define MIDGARD_FLOAT_MOD_ABS (1 << 0)
#define MIDGARD_FLOAT_MOD_NEG (1 << 1)

typedef struct
__attribute__((__packed__))
{
        /* Either midgard_int_mod or from midgard_float_mod_*, depending on the
         * type of op */
        unsigned mod : 2;

        /* replicate lower half if dest = half, or low/high half selection if
         * dest = full
         */
        bool rep_low     : 1;
        bool rep_high    : 1; /* unused if dest = full */
        bool half        : 1; /* only matters if dest = full */
        unsigned swizzle : 8;
}
midgard_vector_alu_src;

typedef struct
__attribute__((__packed__))
{
        midgard_alu_op op               :  8;
        midgard_reg_mode reg_mode   :  2;
        unsigned src1 : 13;
        unsigned src2 : 13;
        midgard_dest_override dest_override : 2;
        unsigned outmod               : 2;
        unsigned mask                           : 8;
}
midgard_vector_alu;

typedef struct
__attribute__((__packed__))
{
        unsigned mod       : 2;
        bool full          : 1; /* 0 = half, 1 = full */
        unsigned component : 3;
}
midgard_scalar_alu_src;

typedef struct
__attribute__((__packed__))
{
        midgard_alu_op op         :  8;
        unsigned src1             :  6;
        unsigned src2             : 11;
        unsigned unknown          :  1;
        unsigned outmod :  2;
        bool output_full          :  1;
        unsigned output_component :  3;
}
midgard_scalar_alu;

typedef struct
__attribute__((__packed__))
{
        unsigned src1_reg : 5;
        unsigned src2_reg : 5;
        unsigned out_reg  : 5;
        bool src2_imm     : 1;
}
midgard_reg_info;

/* In addition to conditional branches and jumps (unconditional branches),
 * Midgard implements a bit of fixed function functionality used in fragment
 * shaders via specially crafted branches. These have special branch opcodes,
 * which perform a fixed-function operation and/or use the results of a
 * fixed-function operation as the branch condition.  */

typedef enum {
        /* Regular branches */
        midgard_jmp_writeout_op_branch_uncond = 1,
        midgard_jmp_writeout_op_branch_cond = 2,

        /* In a fragment shader, execute a discard_if instruction, with the
         * corresponding condition code. Terminates the shader, so generally
         * set the branch target to out of the shader */
        midgard_jmp_writeout_op_discard = 4,

        /* Branch if the tilebuffer is not yet ready. At the beginning of a
         * fragment shader that reads from the tile buffer, for instance via
         * ARM_shader_framebuffer_fetch or EXT_pixel_local_storage, this branch
         * operation should be used as a loop. An instruction like
         * "br.tilebuffer.always -1" does the trick, corresponding to
         * "while(!is_tilebuffer_ready) */
        midgard_jmp_writeout_op_tilebuffer_pending = 6,

        /* In a fragment shader, try to write out the value pushed to r0 to the
         * tilebuffer, subject to unknown state in r1.z and r1.w. If this
         * succeeds, the shader terminates. If it fails, it branches to the
         * specified branch target. Generally, this should be used in a loop to
         * itself, acting as "do { write(r0); } while(!write_successful);" */
        midgard_jmp_writeout_op_writeout = 7,
} midgard_jmp_writeout_op;

typedef enum {
        midgard_condition_write0 = 0,

        /* These condition codes denote a conditional branch on FALSE and on
         * TRUE respectively */
        midgard_condition_false = 1,
        midgard_condition_true = 2,

        /* This condition code always branches. For a pure branch, the
         * unconditional branch coding should be used instead, but for
         * fixed-function branch opcodes, this is still useful */
        midgard_condition_always = 3,
} midgard_condition;

typedef struct
__attribute__((__packed__))
{
        midgard_jmp_writeout_op op : 3; /* == branch_uncond */
        unsigned dest_tag : 4; /* tag of branch destination */
        unsigned unknown : 2;
        int offset : 7;
}
midgard_branch_uncond;

typedef struct
__attribute__((__packed__))
{
        midgard_jmp_writeout_op op : 3; /* == branch_cond */
        unsigned dest_tag : 4; /* tag of branch destination */
        int offset : 7;
        midgard_condition cond : 2;
}
midgard_branch_cond;

typedef struct
__attribute__((__packed__))
{
        midgard_jmp_writeout_op op : 3; /* == branch_cond */
        unsigned dest_tag : 4; /* tag of branch destination */
        unsigned unknown : 2;
        signed offset : 23;

        /* Extended branches permit inputting up to 4 conditions loaded into
         * r31 (two in r31.w and two in r31.x). In the most general case, we
         * specify a function f(A, B, C, D) mapping 4 1-bit conditions to a
         * single 1-bit branch criteria. Note that the domain of f has 2^(2^4)
         * elements, each mapping to 1-bit of output, so we can trivially
         * construct a Godel numbering of f as a (2^4)=16-bit integer. This
         * 16-bit integer serves as a lookup table to compute f, subject to
         * some swaps for ordering.
         *
         * Interesting, the standard 2-bit condition codes are also a LUT with
         * the same format (2^1-bit), but it's usually easier to use enums. */

        unsigned cond : 16;
}
midgard_branch_extended;

typedef struct
__attribute__((__packed__))
{
        midgard_jmp_writeout_op op : 3; /* == writeout */
        unsigned unknown : 13;
}
midgard_writeout;

/*
 * Load/store words
 */

typedef enum {
        midgard_op_ld_st_noop   = 0x03,

        /* Unpack a colour from a native format to fp16 */
        midgard_op_unpack_colour = 0x05,

        /* Packs a colour from fp16 to a native format */
        midgard_op_pack_colour   = 0x09,

        /* Likewise packs from fp32 */
        midgard_op_pack_colour_32 = 0x0A,

        /* Unclear why this is on the L/S unit, but moves fp32 cube map
         * coordinates in r27 to its cube map texture coordinate destination
         * (e.g r29). */

        midgard_op_ld_cubemap_coords = 0x0E,

        /* Loads a global/local/group ID, depending on arguments */
        midgard_op_ld_compute_id = 0x10,

        /* The L/S unit can do perspective division a clock faster than the ALU
         * if you're lucky. Put the vec4 in r27, and call with 0x24 as the
         * unknown state; the output will be <x/w, y/w, z/w, 1>. Replace w with
         * z for the z version */
        midgard_op_ldst_perspective_division_z = 0x12,
        midgard_op_ldst_perspective_division_w = 0x13,

        /* val in r27.y, address embedded, outputs result to argument. Invert val for sub. Let val = +-1 for inc/dec. */
        midgard_op_atomic_add = 0x40,
        midgard_op_atomic_add64 = 0x41,

        midgard_op_atomic_and = 0x44,
        midgard_op_atomic_and64 = 0x45,
        midgard_op_atomic_or = 0x48,
        midgard_op_atomic_or64 = 0x49,
        midgard_op_atomic_xor = 0x4C,
        midgard_op_atomic_xor64 = 0x4D,

        midgard_op_atomic_imin = 0x50,
        midgard_op_atomic_imin64 = 0x51,
        midgard_op_atomic_umin = 0x54,
        midgard_op_atomic_umin64 = 0x55,
        midgard_op_atomic_imax = 0x58,
        midgard_op_atomic_imax64 = 0x59,
        midgard_op_atomic_umax = 0x5C,
        midgard_op_atomic_umax64 = 0x5D,

        midgard_op_atomic_xchg = 0x60,
        midgard_op_atomic_xchg64 = 0x61,

        midgard_op_atomic_cmpxchg = 0x64,
        midgard_op_atomic_cmpxchg64 = 0x65,

        /* Used for compute shader's __global arguments, __local variables (or
         * for register spilling) */

        midgard_op_ld_uchar = 0x80, /* zero extends */
        midgard_op_ld_char = 0x81, /* sign extends */
        midgard_op_ld_ushort = 0x84, /* zero extends */
        midgard_op_ld_short = 0x85, /* sign extends */
        midgard_op_ld_char4 = 0x88, /* short2, int, float */
        midgard_op_ld_short4 = 0x8C, /* int2, float2, long */
        midgard_op_ld_int4 = 0x90, /* float4, long2 */

        midgard_op_ld_attr_32 = 0x94,
        midgard_op_ld_attr_16 = 0x95,
        midgard_op_ld_attr_32u = 0x96,
        midgard_op_ld_attr_32i = 0x97,
        midgard_op_ld_vary_32 = 0x98,
        midgard_op_ld_vary_16 = 0x99,
        midgard_op_ld_vary_32u = 0x9A,
        midgard_op_ld_vary_32i = 0x9B,

        /* Old version of midgard_op_ld_color_buffer_as_fp16, for T720 */
        midgard_op_ld_color_buffer_as_fp32_old = 0x9C,
        midgard_op_ld_color_buffer_as_fp16_old = 0x9D,
        midgard_op_ld_color_buffer_32u_old = 0x9E,

        /* The distinction between these ops is the alignment requirement /
         * accompanying shift. Thus, the offset to ld_ubo_int4 is in 16-byte
         * units and can load 128-bit. The offset to ld_ubo_short4 is in 8-byte
         * units; ld_ubo_char4 in 4-byte units. ld_ubo_char/ld_ubo_char2 are
         * purely theoretical (never seen in the wild) since int8/int16/fp16
         * UBOs don't really exist. The ops are still listed to maintain
         * symmetry with generic I/O ops. */

        midgard_op_ld_ubo_char   = 0xA0, /* theoretical */
        midgard_op_ld_ubo_char2  = 0xA4, /* theoretical */
        midgard_op_ld_ubo_char4  = 0xA8,
        midgard_op_ld_ubo_short4 = 0xAC,
        midgard_op_ld_ubo_int4   = 0xB0,

        /* New-style blending ops. Works on T760/T860 */
        midgard_op_ld_color_buffer_as_fp32 = 0xB8,
        midgard_op_ld_color_buffer_as_fp16 = 0xB9,
        midgard_op_ld_color_buffer_32u = 0xBA,

        midgard_op_st_char = 0xC0,
        midgard_op_st_char2 = 0xC4, /* short */
        midgard_op_st_char4 = 0xC8, /* short2, int, float */
        midgard_op_st_short4 = 0xCC, /* int2, float2, long */
        midgard_op_st_int4 = 0xD0, /* float4, long2 */

        midgard_op_st_vary_32 = 0xD4,
        midgard_op_st_vary_16 = 0xD5,
        midgard_op_st_vary_32u = 0xD6,
        midgard_op_st_vary_32i = 0xD7,

        /* Value to st in r27, location r26.w as short2 */
        midgard_op_st_image_f = 0xD8,
        midgard_op_st_image_ui = 0xDA,
        midgard_op_st_image_i = 0xDB,
} midgard_load_store_op;

typedef enum {
        midgard_interp_sample = 0,
        midgard_interp_centroid = 1,
        midgard_interp_default = 2
} midgard_interpolation;

typedef enum {
        midgard_varying_mod_none = 0,

        /* Other values unknown */

        /* Take the would-be result and divide all components by its z/w
         * (perspective division baked in with the load)  */
        midgard_varying_mod_perspective_z = 2,
        midgard_varying_mod_perspective_w = 3,
} midgard_varying_modifier;

typedef struct
__attribute__((__packed__))
{
        unsigned zero0 : 1; /* Always zero */

        midgard_varying_modifier modifier : 2;

        unsigned zero1: 1; /* Always zero */

        /* Varying qualifiers, zero if not a varying */
        unsigned flat    : 1;
        unsigned is_varying : 1; /* Always one for varying, but maybe something else? */
        midgard_interpolation interpolation : 2;

        unsigned zero2 : 2; /* Always zero */
}
midgard_varying_parameter;

/* 8-bit register/etc selector for load/store ops */
typedef struct
__attribute__((__packed__))
{
        /* Indexes into the register */
        unsigned component : 2;

        /* Register select between r26/r27 */
        unsigned select : 1;

        unsigned unknown : 2;

        /* Like any good Arm instruction set, load/store arguments can be
         * implicitly left-shifted... but only the second argument. Zero for no
         * shifting, up to <<7 possible though. This is useful for indexing.
         *
         * For the first argument, it's unknown what these bits mean */
        unsigned shift : 3;
}
midgard_ldst_register_select;

typedef struct
__attribute__((__packed__))
{
        midgard_load_store_op op : 8;
        unsigned reg     : 5;
        unsigned mask    : 4;
        unsigned swizzle : 8;

        /* Load/store ops can take two additional registers as arguments, but
         * these are limited to load/store registers with only a few supported
         * mask/swizzle combinations. The tradeoff is these are much more
         * compact, requiring 8-bits each rather than 17-bits for a full
         * reg/mask/swizzle. Usually (?) encoded as
         * midgard_ldst_register_select. */
        unsigned arg_1   : 8;
        unsigned arg_2   : 8;

        unsigned varying_parameters : 10;

        unsigned address : 9;
}
midgard_load_store_word;

typedef struct
__attribute__((__packed__))
{
        unsigned type      : 4;
        unsigned next_type : 4;
        uint64_t word1     : 60;
        uint64_t word2     : 60;
}
midgard_load_store;

/* 8-bit register selector used in texture ops to select a bias/LOD/gradient
 * register, shoved into the `bias` field */

typedef struct
__attribute__((__packed__))
{
        /* 32-bit register, clear for half-register */
        unsigned full : 1;

        /* Register select between r28/r29 */
        unsigned select : 1;

        /* For a half-register, selects the upper half */
        unsigned upper : 1;

        /* Indexes into the register */
        unsigned component : 2;

        /* Padding to make this 8-bit */
        unsigned zero : 3;
}
midgard_tex_register_select;

/* Texture pipeline results are in r28-r29 */
#define REG_TEX_BASE 28

enum mali_texture_op {
        TEXTURE_OP_NORMAL = 1,  /* texture */
        TEXTURE_OP_LOD = 2,     /* textureLod */
        TEXTURE_OP_TEXEL_FETCH = 4,
        TEXTURE_OP_BARRIER = 11,
        TEXTURE_OP_DERIVATIVE = 13
};

enum mali_sampler_type {
        MALI_SAMPLER_UNK        = 0x0,
        MALI_SAMPLER_FLOAT      = 0x1, /* sampler */
        MALI_SAMPLER_UNSIGNED   = 0x2, /* usampler */
        MALI_SAMPLER_SIGNED     = 0x3, /* isampler */
};

/* Texture modes */
enum mali_texture_mode {
        TEXTURE_NORMAL = 1,
        TEXTURE_SHADOW = 5,
        TEXTURE_GATHER_SHADOW = 6,
        TEXTURE_GATHER_X = 8,
        TEXTURE_GATHER_Y = 9,
        TEXTURE_GATHER_Z = 10,
        TEXTURE_GATHER_W = 11,
};

enum mali_derivative_mode {
        TEXTURE_DFDX = 0,
        TEXTURE_DFDY = 1,
};

typedef struct
__attribute__((__packed__))
{
        unsigned type      : 4;
        unsigned next_type : 4;

        enum mali_texture_op op  : 4;
        unsigned mode : 4;

        /* A little obscure, but last is set for the last texture operation in
         * a shader. cont appears to just be last's opposite (?). Yeah, I know,
         * kind of funky.. BiOpen thinks it could do with memory hinting, or
         * tile locking? */

        unsigned cont  : 1;
        unsigned last  : 1;

        unsigned format : 2;

        /* Are sampler_handle/texture_handler respectively set by registers? If
         * true, the lower 8-bits of the respective field is a register word.
         * If false, they are an immediate */

        unsigned sampler_register : 1;
        unsigned texture_register : 1;

        /* Is a register used to specify the
         * LOD/bias/offset? If set, use the `bias` field as
         * a register index. If clear, use the `bias` field
         * as an immediate. */
        unsigned lod_register : 1;

        /* Is a register used to specify an offset? If set, use the
         * offset_reg_* fields to encode this, duplicated for each of the
         * components. If clear, there is implcitly always an immediate offst
         * specificed in offset_imm_* */
        unsigned offset_register : 1;

        unsigned in_reg_full  : 1;
        unsigned in_reg_select : 1;
        unsigned in_reg_upper  : 1;
        unsigned in_reg_swizzle : 8;

        unsigned unknown8  : 2;

        unsigned out_full  : 1;

        enum mali_sampler_type sampler_type : 2;

        unsigned out_reg_select : 1;
        unsigned out_upper : 1;

        unsigned mask : 4;

        /* Intriguingly, textures can take an outmod just like alu ops. Int
         * outmods are not supported as far as I can tell, so this is only
         * meaningful for float samplers */
        midgard_outmod_float outmod  : 2;

        unsigned swizzle  : 8;

         /* These indicate how many bundles after this texture op may be
          * executed in parallel with this op. We may execute only ALU and
         * ld/st in parallel (not other textures), and obviously there cannot
         * be any dependency (the blob appears to forbid even accessing other
         * channels of a given texture register). */

        unsigned out_of_order   : 2;
        unsigned unknown4  : 10;

        /* In immediate mode, each offset field is an immediate range [0, 7].
         *
         * In register mode, offset_x becomes a register (full, select, upper)
         * triplet followed by a vec3 swizzle is splattered across
         * offset_y/offset_z in a genuinely bizarre way.
         *
         * For texel fetches in immediate mode, the range is the full [-8, 7],
         * but for normal texturing the top bit must be zero and a register
         * used instead. It's not clear where this limitation is from.
         *
         * union {
         *      struct {
         *              signed offset_x  : 4;
         *              signed offset_y  : 4;
         *              signed offset_z  : 4;
         *      } immediate;
         *      struct {
         *              bool full        : 1;
         *              bool select      : 1;
         *              bool upper       : 1;
         *              unsigned swizzle : 8;
         *              unsigned zero    : 1;
         *      } register;
         * }
         */

        unsigned offset : 12;

        /* In immediate bias mode, for a normal texture op, this is
         * texture bias, computed as int(2^8 * frac(biasf)), with
         * bias_int = floor(bias). For a textureLod, it's that, but
         * s/bias/lod. For a texel fetch, this is the LOD as-is.
         *
         * In register mode, this is a midgard_tex_register_select
         * structure and bias_int is zero */

        unsigned bias : 8;
        signed bias_int  : 8;

        /* If sampler/texture_register is set, the bottom 8-bits are
         * midgard_tex_register_select and the top 8-bits are zero. If they are
         * clear, they are immediate texture indices */

        unsigned sampler_handle : 16;
        unsigned texture_handle : 16;
}
midgard_texture_word;

/* Technically barriers are texture instructions but it's less work to add them
 * as an explicitly zeroed special case, since most fields are forced to go to
 * zero */

typedef struct
__attribute__((__packed__))
{
        unsigned type      : 4;
        unsigned next_type : 4;

        /* op = TEXTURE_OP_BARRIER */
        unsigned op  : 6;
        unsigned zero1    : 2;

        /* Since helper invocations don't make any sense, these are forced to one */
        unsigned cont  : 1;
        unsigned last  : 1;
        unsigned zero2 : 14;

        unsigned zero3 : 24;
        unsigned out_of_order : 4;
        unsigned zero4 : 4;

        uint64_t zero5;
} midgard_texture_barrier_word;

typedef union midgard_constants {
        double f64[2];
        uint64_t u64[2];
        int64_t i64[2];
        float f32[4];
        uint32_t u32[4];
        int32_t i32[4];
        uint16_t f16[8];
        uint16_t u16[8];
        int16_t i16[8];
        uint8_t u8[16];
        int8_t i8[16];
}
midgard_constants;

enum midgard_roundmode {
        MIDGARD_RTE = 0x0, /* round to even */
        MIDGARD_RTZ = 0x1, /* round to zero */
        MIDGARD_RTN = 0x2, /* round to negative */
        MIDGARD_RTP = 0x3, /* round to positive */
};

#endif
