/*
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

#ifndef __bifrost_ops_h__
#define __bifrost_ops_h__

enum bifrost_ir_ops {
        op_fma_f32 = 0x0,
        op_fmul_f32,
        op_fadd_f32,
        op_frcp_fast_f32,
        op_max_f32,
        op_min_f32,
        op_add_i32,
        op_sub_i32,
        op_imad,
        op_mul_i32,
        op_or_i32,
        op_and_i32,
        op_lshift_i32,
        op_xor_i32,
        op_rshift_i32,
        op_arshift_i32,
        op_csel_i32,
        op_imin3_i32,
        op_umin3_i32,
        op_imax3_i32,
        op_umax3_i32,

        op_branch,

        // unary
        op_trunc,
        op_ceil,
        op_floor,
        op_round,
        op_roundeven,

        op_mov,
        op_movi,

        op_ld_ubo_v1,
        op_ld_ubo_v2,
        op_ld_ubo_v3,
        op_ld_ubo_v4,

        op_ld_attr_v1,
        op_ld_attr_v2,
        op_ld_attr_v3,
        op_ld_attr_v4,

        op_ld_var_addr,
        op_st_vary_v1,
        op_st_vary_v2,
        op_st_vary_v3,
        op_st_vary_v4,

        op_store_v1,
        op_store_v2,
        op_store_v3,
        op_store_v4,

        op_create_vector,
        op_extract_element,
        op_last,
};


enum branch_cond {
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

enum branch_code {
        BR_ALWAYS = 63,
};

enum csel_cond {
        CSEL_NEQ_0 = 0,
        CSEL_FEQ,
        CSEL_FGTR,
        CSEL_FGE,
        CSEL_IEQ,
        CSEL_IGT,
        CSEL_IGE,
        CSEL_UGT,
        CSEL_UGE,
};

#endif
