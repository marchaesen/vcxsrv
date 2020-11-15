/* Copyright (c) 2018-2019 Alyssa Rosenzweig (alyssa@rosenzweig.io)
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

#include "midgard.h"

/* Include the definitions of the macros and such */

#define MIDGARD_OPS_TABLE
#include "helpers.h"
#undef MIDGARD_OPS_TABLE

/* Table of mapping opcodes to accompanying properties. This is used for both
 * the disassembler and the compiler. It is placed in a .c file like this to
 * avoid duplications in the binary */

struct mir_op_props alu_opcode_props[256] = {
        [midgard_alu_op_fadd]		 = {"fadd", UNITS_ADD | OP_COMMUTES},
        [midgard_alu_op_fmul]		 = {"fmul", UNITS_MUL | UNIT_VLUT | OP_COMMUTES},
        [midgard_alu_op_fmin]		 = {"fmin", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_fmax]		 = {"fmax", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_imin]		 = {"imin", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_imax]		 = {"imax", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_umin]		 = {"umin", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_umax]		 = {"umax", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_ihadd]		 = {"ihadd", UNITS_ADD | OP_COMMUTES},
        [midgard_alu_op_uhadd]		 = {"uhadd", UNITS_ADD | OP_COMMUTES},
        [midgard_alu_op_irhadd]		 = {"irhadd", UNITS_ADD | OP_COMMUTES},
        [midgard_alu_op_urhadd]		 = {"urhadd", UNITS_ADD | OP_COMMUTES},

        [midgard_alu_op_fmov]		 = {"fmov", UNITS_ALL | QUIRK_FLIPPED_R24},
        [midgard_alu_op_fmov_rtz]	 = {"fmov_rtz", UNITS_ALL | QUIRK_FLIPPED_R24},
        [midgard_alu_op_fmov_rtn]	 = {"fmov_rtn", UNITS_ALL | QUIRK_FLIPPED_R24},
        [midgard_alu_op_fmov_rtp]	 = {"fmov_rtp", UNITS_ALL | QUIRK_FLIPPED_R24},
        [midgard_alu_op_fround]          = {"fround", UNITS_ADD},
        [midgard_alu_op_froundeven]      = {"froundeven", UNITS_ADD},
        [midgard_alu_op_ftrunc]          = {"ftrunc", UNITS_ADD},
        [midgard_alu_op_ffloor]		 = {"ffloor", UNITS_ADD},
        [midgard_alu_op_fceil]		 = {"fceil", UNITS_ADD},

        /* Multiplies the X/Y components of the first arg and adds the second
         * arg. Like other LUTs, it must be scalarized. */
        [midgard_alu_op_ffma]		 = {"ffma", UNIT_VLUT},

        /* Though they output a scalar, they need to run on a vector unit
         * since they process vectors */
        [midgard_alu_op_fdot3]		 = {"fdot3", UNIT_VMUL | OP_CHANNEL_COUNT(3) | OP_COMMUTES},
        [midgard_alu_op_fdot3r]		 = {"fdot3r", UNIT_VMUL | OP_CHANNEL_COUNT(3) | OP_COMMUTES},
        [midgard_alu_op_fdot4]		 = {"fdot4", UNIT_VMUL | OP_CHANNEL_COUNT(4) | OP_COMMUTES},

        /* Incredibly, iadd can run on vmul, etc */
        [midgard_alu_op_iadd]		 = {"iadd", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_ishladd]         = {"ishladd", UNITS_MUL},
        [midgard_alu_op_iaddsat]	 = {"iaddsat", UNITS_ADD | OP_COMMUTES},
        [midgard_alu_op_uaddsat]	 = {"uaddsat", UNITS_ADD | OP_COMMUTES},
        [midgard_alu_op_iabsdiff]	 = {"iabsdiff", UNITS_ADD},
        [midgard_alu_op_uabsdiff]	 = {"uabsdiff", UNITS_ADD},
        [midgard_alu_op_ichoose]	 = {"ichoose", UNITS_ADD},
        [midgard_alu_op_isub]		 = {"isub", UNITS_MOST},
        [midgard_alu_op_isubsat]	 = {"isubsat", UNITS_MOST},
        [midgard_alu_op_usubsat]	 = {"usubsat", UNITS_MOST},
        [midgard_alu_op_imul]		 = {"imul", UNITS_MUL | OP_COMMUTES},
        [midgard_alu_op_imov]		 = {"imov", UNITS_ALL | QUIRK_FLIPPED_R24},

        /* For vector comparisons, use ball etc */
        [midgard_alu_op_feq]		 = {"feq", UNITS_MOST | OP_TYPE_CONVERT | OP_COMMUTES},
        [midgard_alu_op_fne]		 = {"fne", UNITS_MOST | OP_TYPE_CONVERT | OP_COMMUTES},
        [midgard_alu_op_fle]		 = {"fle", UNITS_MOST | OP_TYPE_CONVERT},
        [midgard_alu_op_flt]		 = {"flt", UNITS_MOST | OP_TYPE_CONVERT},
        [midgard_alu_op_ieq]		 = {"ieq", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_ine]		 = {"ine", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_ilt]		 = {"ilt", UNITS_MOST},
        [midgard_alu_op_ile]		 = {"ile", UNITS_MOST},
        [midgard_alu_op_ult]		 = {"ult", UNITS_MOST},
        [midgard_alu_op_ule]		 = {"ule", UNITS_MOST},

        /* csel must run in the second pipeline stage (r31 written in first) */
        [midgard_alu_op_icsel]		 = {"icsel", UNIT_VADD | UNIT_SMUL},
        [midgard_alu_op_icsel_v]         = {"icsel_v", UNIT_VADD | UNIT_SMUL}, /* Acts as bitselect() */
        [midgard_alu_op_fcsel_v]	 = {"fcsel_v", UNIT_VADD | UNIT_SMUL},
        [midgard_alu_op_fcsel]		 = {"fcsel", UNIT_VADD | UNIT_SMUL},

        [midgard_alu_op_frcp]		 = {"frcp", UNIT_VLUT},
        [midgard_alu_op_frsqrt]		 = {"frsqrt", UNIT_VLUT},
        [midgard_alu_op_fsqrt]		 = {"fsqrt", UNIT_VLUT},
        [midgard_alu_op_fpow_pt1]	 = {"fpow_pt1", UNIT_VLUT},
        [midgard_alu_op_fpown_pt1]	 = {"fpown_pt1", UNIT_VLUT},
        [midgard_alu_op_fpowr_pt1]	 = {"fpowr_pt1", UNIT_VLUT},
        [midgard_alu_op_fexp2]		 = {"fexp2", UNIT_VLUT},
        [midgard_alu_op_flog2]		 = {"flog2", UNIT_VLUT},

        [midgard_alu_op_f2i_rte]	 = {"f2i_rte", UNITS_ADD | OP_TYPE_CONVERT | MIDGARD_ROUNDS},
        [midgard_alu_op_f2i_rtz]	 = {"f2i_rtz", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_f2i_rtn]	 = {"f2i_rtn", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_f2i_rtp]	 = {"f2i_rtp", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_f2u_rte]	 = {"f2u_rte", UNITS_ADD | OP_TYPE_CONVERT | MIDGARD_ROUNDS},
        [midgard_alu_op_f2u_rtz]	 = {"f2u_rtz", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_f2u_rtn]	 = {"f2u_rtn", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_f2u_rtp]	 = {"f2u_rtp", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_i2f_rte]	 = {"i2f_rte", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_i2f_rtz]	 = {"i2f_rtz", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_i2f_rtn]	 = {"i2f_rtn", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_i2f_rtp]	 = {"i2f_rtp", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_u2f_rte]	 = {"u2f_rte", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_u2f_rtz]	 = {"u2f_rtz", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_u2f_rtn]	 = {"u2f_rtn", UNITS_ADD | OP_TYPE_CONVERT},
        [midgard_alu_op_u2f_rtp]	 = {"u2f_rtp", UNITS_ADD | OP_TYPE_CONVERT},

        [midgard_alu_op_fsin]		 = {"fsin", UNIT_VLUT},
        [midgard_alu_op_fcos]		 = {"fcos", UNIT_VLUT},

        [midgard_alu_op_iand]		 = {"iand", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_iandnot]         = {"iandnot", UNITS_MOST},

        [midgard_alu_op_ior]		 = {"ior", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_iornot]		 = {"iornot", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_inor]		 = {"inor", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_ixor]		 = {"ixor", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_inxor]		 = {"inxor", UNITS_MOST | OP_COMMUTES},
        [midgard_alu_op_iclz]		 = {"iclz", UNITS_ADD},
        [midgard_alu_op_ibitcount8]	 = {"ibitcount8", UNITS_ADD},
        [midgard_alu_op_inand]		 = {"inand", UNITS_MOST},
        [midgard_alu_op_ishl]		 = {"ishl", UNITS_ADD},
        [midgard_alu_op_iasr]		 = {"iasr", UNITS_ADD},
        [midgard_alu_op_ilsr]		 = {"ilsr", UNITS_ADD},

        [midgard_alu_op_fball_eq]	 = {"fball_eq",  UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES | OP_TYPE_CONVERT},
        [midgard_alu_op_fball_neq]	 = {"fball_neq", UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES | OP_TYPE_CONVERT},
        [midgard_alu_op_fball_lt]	 = {"fball_lt",  UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES | OP_TYPE_CONVERT},
        [midgard_alu_op_fball_lte]	 = {"fball_lte", UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES | OP_TYPE_CONVERT},

        [midgard_alu_op_fbany_eq]	 = {"fbany_eq",  UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES | OP_TYPE_CONVERT},
        [midgard_alu_op_fbany_neq]	 = {"fbany_neq", UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES | OP_TYPE_CONVERT},
        [midgard_alu_op_fbany_lt]	 = {"fbany_lt",  UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES | OP_TYPE_CONVERT},
        [midgard_alu_op_fbany_lte]	 = {"fbany_lte", UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES | OP_TYPE_CONVERT},

        [midgard_alu_op_iball_eq]	 = {"iball_eq",  UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},
        [midgard_alu_op_iball_neq]	 = {"iball_neq", UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},
        [midgard_alu_op_iball_lt]	 = {"iball_lt",  UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},
        [midgard_alu_op_iball_lte]	 = {"iball_lte", UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},
        [midgard_alu_op_uball_lt]	 = {"uball_lt",  UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},
        [midgard_alu_op_uball_lte]	 = {"uball_lte", UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},

        [midgard_alu_op_ibany_eq]	 = {"ibany_eq",  UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},
        [midgard_alu_op_ibany_neq]	 = {"ibany_neq", UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},
        [midgard_alu_op_ibany_lt]	 = {"ibany_lt",  UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},
        [midgard_alu_op_ibany_lte]	 = {"ibany_lte", UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},
        [midgard_alu_op_ubany_lt]	 = {"ubany_lt",  UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},
        [midgard_alu_op_ubany_lte]	 = {"ubany_lte", UNITS_VECTOR | OP_CHANNEL_COUNT(4) | OP_COMMUTES},

        [midgard_alu_op_fatan2_pt1]     = {"fatan2_pt1", UNIT_VLUT},
        [midgard_alu_op_fatan_pt2]      = {"fatan_pt2", UNIT_VLUT},

        /* Haven't seen in a while */
        [midgard_alu_op_freduce]        = {"freduce", 0},
};

/* Define shorthands */

#define M8  midgard_reg_mode_8
#define M16 midgard_reg_mode_16
#define M32 midgard_reg_mode_32
#define M64 midgard_reg_mode_64

struct mir_ldst_op_props load_store_opcode_props[256] = {
        [midgard_op_unpack_colour] = {"unpack_colour", M32},
        [midgard_op_pack_colour] = {"pack_colour", M32},
        [midgard_op_pack_colour_32] = {"pack_colour_32", M32},
        [midgard_op_ld_cubemap_coords] = {"ld_cubemap_coords", M32},
        [midgard_op_ld_compute_id] = {"ld_compute_id", M32},
        [midgard_op_ldst_perspective_division_z] = {"ldst_perspective_division_z", M32},
        [midgard_op_ldst_perspective_division_w] = {"ldst_perspective_division_w", M32},

        [midgard_op_atomic_add]     = {"atomic_add",     M32 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_and]     = {"atomic_and",     M32 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_or]      = {"atomic_or",      M32 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_xor]     = {"atomic_xor",     M32 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_imin]    = {"atomic_imin",    M32 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_umin]    = {"atomic_umin",    M32 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_imax]    = {"atomic_imax",    M32 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_umax]    = {"atomic_umax",    M32 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_xchg]    = {"atomic_xchg",    M32 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_cmpxchg] = {"atomic_cmpxchg", M32 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},

        [midgard_op_atomic_add64]     = {"atomic_add64",     M64 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_and64]     = {"atomic_and64",     M64 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_or64]      = {"atomic_or64",      M64 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_xor64]     = {"atomic_xor64",     M64 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_imin64]    = {"atomic_imin64",    M64 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_umin64]    = {"atomic_umin64",    M64 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_imax64]    = {"atomic_imax64",    M64 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_umax64]    = {"atomic_umax64",    M64 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_xchg64]    = {"atomic_xchg64",    M64 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},
        [midgard_op_atomic_cmpxchg64] = {"atomic_cmpxchg64", M64 | LDST_SIDE_FX | LDST_ADDRESS | LDST_ATOMIC},

        [midgard_op_ld_uchar]  = {"ld_uchar", M32 | LDST_ADDRESS},
        [midgard_op_ld_char]   = {"ld_char",   M32 | LDST_ADDRESS},
        [midgard_op_ld_ushort] = {"ld_ushort", M32 | LDST_ADDRESS},
        [midgard_op_ld_short]  = {"ld_short",  M32 | LDST_ADDRESS},
        [midgard_op_ld_char4]  = {"ld_char4",  M32 | LDST_ADDRESS},
        [midgard_op_ld_short4] = {"ld_short4", M32 | LDST_ADDRESS},
        [midgard_op_ld_int4]   = {"ld_int4",   M32 | LDST_ADDRESS},

        [midgard_op_ld_attr_32]  = {"ld_attr_32",  M32},
        [midgard_op_ld_attr_32i] = {"ld_attr_32i", M32},
        [midgard_op_ld_attr_32u] = {"ld_attr_32u", M32},
        [midgard_op_ld_attr_16]  = {"ld_attr_16",  M32},

        [midgard_op_ld_vary_32]  = {"ld_vary_32",  M32},
        [midgard_op_ld_vary_16]  = {"ld_vary_16",  M32},
        [midgard_op_ld_vary_32i] = {"ld_vary_32i", M32},
        [midgard_op_ld_vary_32u] = {"ld_vary_32u", M32},

        [midgard_op_ld_color_buffer_32u]  = {"ld_color_buffer_32u",  M32},
        [midgard_op_ld_color_buffer_32u_old]  = {"ld_color_buffer_32u_old",  M32},
        [midgard_op_ld_color_buffer_as_fp16] = {"ld_color_buffer_as_fp16", M16},
        [midgard_op_ld_color_buffer_as_fp32] = {"ld_color_buffer_as_fp32", M32},
        [midgard_op_ld_color_buffer_as_fp16_old] = {"ld_color_buffer_as_fp16_old", M16 | LDST_SPECIAL_MASK},
        [midgard_op_ld_color_buffer_as_fp32_old] = {"ld_color_buffer_as_fp32_old", M32 | LDST_SPECIAL_MASK},

        [midgard_op_ld_ubo_char]   = {"ld_ubo_char",   M32},
        [midgard_op_ld_ubo_char2]  = {"ld_ubo_char2",  M16},
        [midgard_op_ld_ubo_char4]  = {"ld_ubo_char4",  M32},
        [midgard_op_ld_ubo_short4] = {"ld_ubo_short4", M32},
        [midgard_op_ld_ubo_int4]   = {"ld_ubo_int4",   M32},

        [midgard_op_st_char]   = {"st_char",   M32 | LDST_STORE | LDST_ADDRESS},
        [midgard_op_st_char2]  = {"st_char2",  M16 | LDST_STORE | LDST_ADDRESS},
        [midgard_op_st_char4]  = {"st_char4",  M32 | LDST_STORE | LDST_ADDRESS},
        [midgard_op_st_short4] = {"st_short4", M32 | LDST_STORE | LDST_ADDRESS},
        [midgard_op_st_int4]   = {"st_int4",   M32 | LDST_STORE | LDST_ADDRESS},

        [midgard_op_st_vary_32]  = {"st_vary_32",  M32 | LDST_STORE},
        [midgard_op_st_vary_32i] = {"st_vary_32i", M32 | LDST_STORE},
        [midgard_op_st_vary_32u] = {"st_vary_32u", M32 | LDST_STORE},
        [midgard_op_st_vary_16]  = {"st_vary_16",  M16 | LDST_STORE},

        [midgard_op_st_image_f]  = {"st_image_f",  M32 | LDST_STORE},
        [midgard_op_st_image_ui] = {"st_image_ui", M32 | LDST_STORE},
        [midgard_op_st_image_i]  = {"st_image_i",  M32 | LDST_STORE},
};

#undef M8
#undef M16
#undef M32
#undef M64

struct mir_tag_props midgard_tag_props[16] = {
        [TAG_INVALID]           = {"invalid", 0},
        [TAG_BREAK]             = {"break", 0},
        [TAG_TEXTURE_4_VTX]     = {"tex/vt", 1},
        [TAG_TEXTURE_4]         = {"tex", 1},
        [TAG_TEXTURE_4_BARRIER] = {"tex/bar", 1},
        [TAG_LOAD_STORE_4]      = {"ldst", 1},
        [TAG_UNKNOWN_1]         = {"unk1", 1},
        [TAG_UNKNOWN_2]         = {"unk2", 1},
        [TAG_ALU_4]             = {"alu/4", 1},
        [TAG_ALU_8]             = {"alu/8", 2},
        [TAG_ALU_12]            = {"alu/12", 3},
        [TAG_ALU_16]            = {"alu/16", 4},
        [TAG_ALU_4_WRITEOUT]    = {"aluw/4", 1},
        [TAG_ALU_8_WRITEOUT]    = {"aluw/8", 2},
        [TAG_ALU_12_WRITEOUT]   = {"aluw/12", 3},
        [TAG_ALU_16_WRITEOUT]   = {"aluw/16", 4}
};
