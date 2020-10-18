/*
 * Copyright (C) 2020 Collabora Ltd.
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
 *
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "compiler.h"

/* Bifrost requires special functions to be lowered in various machine specific
 * ways. The routines in this file are used in codegen for this. */

/* New Bifrost has a FEXP2_FAST instruction but requires an auxiliary
 * parameter. */

static void
bi_emit_fexp2_new(bi_context *ctx, nir_alu_instr *instr)
{
        /* FMA_MSCALE T, X, 1.0, 0, 0x18 */

        bi_instruction mscale = {
                .type = BI_FMA,
                .op = { .mscale = true },
                .dest = bi_make_temp(ctx),
                .dest_type = nir_type_float32,
                .src = {
                        pan_src_index(&instr->src[0].src),
                        BIR_INDEX_CONSTANT | 0,
                        BIR_INDEX_ZERO,
                        BIR_INDEX_CONSTANT | 32,
                },
                .src_types = {
                        nir_type_float32,
                        nir_type_float32,
                        nir_type_float32,
                        nir_type_int32,
                },
                .constant = {
                        /* 0x3f80000000 = 1.0f as fp32
                         * 24 = shift to multiply by 2^24 */
                        .u64 = (0x3f800000) | (24ull << 32)
                },
                .swizzle = { { instr->src[0].swizzle[0] } }
        };

        /* F2I_RTE T, T */

        bi_instruction f2i = {
                .type = BI_CONVERT,
                .dest = bi_make_temp(ctx),
                .dest_type = nir_type_int32,
                .src = { mscale.dest },
                .src_types = { nir_type_float32 },
                .roundmode = BIFROST_RTE
        };

        /* FEXP2_FAST T, T, X */

        bi_instruction fexp = {
                .type = BI_SPECIAL,
                .op = { .special = BI_SPECIAL_EXP2_LOW },
                .dest = pan_dest_index(&instr->dest.dest),
                .dest_type = nir_type_float32,
                .src = { f2i.dest, mscale.src[0] },
                .src_types = { nir_type_int32, nir_type_float32 },
                .swizzle = { {}, { instr->src[0].swizzle[0] } }
        };

        bi_emit(ctx, mscale);
        bi_emit(ctx, f2i);
        bi_emit(ctx, fexp);
}

/* Even on new Bifrost, there are a bunch of reductions to do */

static void
bi_emit_flog2_new(bi_context *ctx, nir_alu_instr *instr)
{
        /* LOG_FREXPE X */
        bi_instruction frexpe = {
                .type = BI_FREXP,
                .op = { .frexp = BI_FREXPE_LOG },
                .dest = bi_make_temp(ctx),
                .dest_type = nir_type_int32,
                .src = { pan_src_index(&instr->src[0].src) },
                .src_types = { nir_type_float32 },
                .swizzle = { { instr->src[0].swizzle[0] } }
        };

        /* I32_TO_F32 m */
        bi_instruction i2f = {
                .type = BI_CONVERT,
                .dest = bi_make_temp(ctx),
                .dest_type = nir_type_float32,
                .src = { frexpe.dest },
                .src_types = { nir_type_int32 },
                .roundmode = BIFROST_RTZ
        };

        /* ADD_FREXPM (x-1), -1.0, X */
        bi_instruction x_minus_1 = {
                .type = BI_REDUCE_FMA,
                .op = { .reduce = BI_REDUCE_ADD_FREXPM },
                .dest = bi_make_temp(ctx),
                .dest_type = nir_type_float32,
                .src = {
                        BIR_INDEX_CONSTANT,
                        pan_src_index(&instr->src[0].src),
                },
                .src_types = { nir_type_float32, nir_type_float32 },
                .constant = {
                        .u64 = 0xBF800000 /* -1.0 */
                },
                .swizzle = { {}, { instr->src[0].swizzle[0] } }
        };

        /* FLOG2_HELP log2(x)/(x-1), x */
        bi_instruction help = {
                .type = BI_TABLE,
                .op = { .table = BI_TABLE_LOG2_U_OVER_U_1_LOW },
                .dest = bi_make_temp(ctx),
                .dest_type = nir_type_float32,
                .src = { pan_src_index(&instr->src[0].src) },
                .src_types = { nir_type_float32 },
                .swizzle = { { instr->src[0].swizzle[0] } }
        };

        /* FMA log2(x)/(x - 1), (x - 1), M */
        bi_instruction fma = {
                .type = BI_FMA,
                .dest = pan_dest_index(&instr->dest.dest),
                .dest_type = nir_type_float32,
                .src = {
                        help.dest,
                        x_minus_1.dest,
                        i2f.dest
                },
                .src_types = {
                        nir_type_float32,
                        nir_type_float32,
                        nir_type_float32
                }
        };

        bi_emit(ctx, frexpe);
        bi_emit(ctx, i2f);
        bi_emit(ctx, x_minus_1);
        bi_emit(ctx, help);
        bi_emit(ctx, fma);
}

void
bi_emit_fexp2(bi_context *ctx, nir_alu_instr *instr)
{
        /* TODO: G71 */
        bi_emit_fexp2_new(ctx, instr);
}

void
bi_emit_flog2(bi_context *ctx, nir_alu_instr *instr)
{
        /* TODO: G71 */
        bi_emit_flog2_new(ctx, instr);
}
