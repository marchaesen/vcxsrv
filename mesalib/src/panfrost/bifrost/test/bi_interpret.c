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

#include <math.h>
#include "bit.h"
#include "util/half_float.h"

typedef union {
        uint64_t u64;
        uint32_t u32;
        uint16_t u16[2];
        uint8_t u8[4];
        int64_t i64;
        int32_t i32;
        int16_t i16[2];
        int8_t i8[4];
        double f64;
        float f32;
        uint16_t f16[2];
} bit_t;

/* Interprets a subset of Bifrost IR required for automated testing */

static uint64_t
bit_read(struct bit_state *s, bi_instruction *ins, unsigned index, nir_alu_type T, bool FMA)
{
        if (index & BIR_INDEX_REGISTER) {
                uint32_t reg = index & ~BIR_INDEX_REGISTER;
                assert(reg < 64);
                return s->r[reg];
        } else if (index & BIR_INDEX_UNIFORM) {
                unreachable("Uniform registers to be implemented");
        } else if (index & BIR_INDEX_CONSTANT) {
                return ins->constant.u64 >> (index & ~BIR_INDEX_CONSTANT);
        } else if (index & BIR_INDEX_ZERO) {
                return 0;
        } else if (index & (BIR_INDEX_PASS | BIFROST_SRC_STAGE)) {
                return FMA ? 0 : s->T;
        } else if (index & (BIR_INDEX_PASS | BIFROST_SRC_PASS_FMA)) {
                return s->T0;
        } else if (index & (BIR_INDEX_PASS | BIFROST_SRC_PASS_ADD)) {
                return s->T1;
        } else if (!index) {
                /* Placeholder */
                return 0;
        } else {
                unreachable("Invalid source");
        }
}

static void
bit_write(struct bit_state *s, unsigned index, nir_alu_type T, bit_t value, bool FMA)
{
        /* Always write stage passthrough */
        if (FMA)
                s->T = value.u32;

        if (index & BIR_INDEX_REGISTER) {
                uint32_t reg = index & ~BIR_INDEX_REGISTER;
                assert(reg < 64);
                s->r[reg] = value.u32;
        } else if (!index) {
                /* Nothing to do */
        } else {
                unreachable("Invalid destination");
        }
}

#define bh _mesa_float_to_half
#define bf _mesa_half_to_float

#define bv2f16(fxn) \
        for (unsigned c = 0; c < 2; ++c) { \
                dest.f16[c] = bh(fxn(bf(srcs[0].f16[ins->swizzle[0][c]]), \
                                        bf(srcs[1].f16[ins->swizzle[1][c]]), \
                                        bf(srcs[2].f16[ins->swizzle[2][c]]), \
                                        bf(srcs[3].f16[ins->swizzle[3][c]]))); \
        }

#define bv2i16(fxn) \
        for (unsigned c = 0; c < 2; ++c) { \
                dest.f16[c] = fxn(srcs[0].u16[ins->swizzle[0][c]], \
                                        srcs[1].u16[ins->swizzle[1][c]], \
                                        srcs[2].u16[ins->swizzle[2][c]], \
                                        srcs[3].u16[ins->swizzle[3][c]]); \
        }

#define bf32(fxn) dest.f32 = fxn(srcs[0].f32, srcs[1].f32, srcs[2].f32, srcs[3].f32)
#define bi32(fxn) dest.i32 = fxn(srcs[0].u32, srcs[1].u32, srcs[2].u32, srcs[3].i32)

#define bfloat(fxn64, fxn32) \
        if (ins->dest_type == nir_type_float64) { \
                unreachable("TODO: 64-bit"); \
        } else if (ins->dest_type == nir_type_float32) { \
                bf32(fxn64); \
                break; \
        } else if (ins->dest_type == nir_type_float16) { \
                bv2f16(fxn32); \
                break; \
        }

#define bint(fxn64, fxn32, fxn16, fxn8) \
        if (ins->dest_type == nir_type_int64 || ins->dest_type == nir_type_uint64) { \
                unreachable("TODO: 64-bit"); \
        } else if (ins->dest_type == nir_type_int32 || ins->dest_type == nir_type_uint32) { \
                bi32(fxn32); \
                break; \
        } else if (ins->dest_type == nir_type_int16 || ins->dest_type == nir_type_uint16) { \
                bv2i16(fxn16); \
                break; \
        } else if (ins->dest_type == nir_type_int8 || ins->dest_type == nir_type_uint8) { \
                unreachable("TODO: 8-bit"); \
        }

#define bpoly(name) \
        bfloat(bit_f64 ## name, bit_f32 ## name); \
        bint(bit_i64 ## name, bit_i32 ## name, bit_i16 ## name, bit_i8 ## name); \
        unreachable("Invalid type");

#define bit_make_float(name, expr) \
        static inline double \
        bit_f64 ## name(double a, double b, double c, double d) \
        { \
                return expr; \
        } \
        static inline float \
        bit_f32 ## name(float a, float b, float c, float d) \
        { \
                return expr; \
        } \

#define bit_make_int(name, expr) \
        static inline int64_t \
        bit_i64 ## name (int64_t a, int64_t b, int64_t c, int64_t d) \
        { \
                return expr; \
        } \
        \
        static inline int32_t \
        bit_i32 ## name (int32_t a, int32_t b, int32_t c, int32_t d) \
        { \
                return expr; \
        } \
        \
        static inline int16_t \
        bit_i16 ## name (int16_t a, int16_t b, int16_t c, int16_t d) \
        { \
                return expr; \
        } \
        \
        static inline int8_t \
        bit_i8 ## name (int8_t a, int8_t b, int8_t c, int8_t d) \
        { \
                return expr; \
        } \

#define bit_make_poly(name, expr) \
        bit_make_float(name, expr) \
        bit_make_int(name, expr) \
        
bit_make_poly(add, a + b);
bit_make_float(fma, (a * b) + c);
bit_make_poly(mov, a);
bit_make_poly(min, MIN2(a, b));
bit_make_poly(max, MAX2(a, b));

/* Modifiers */

static float
bit_outmod(float raw, enum bifrost_outmod mod)
{
        switch (mod) {
        case BIFROST_POS:
                return MAX2(raw, 0.0);
        case BIFROST_SAT_SIGNED:
                return CLAMP(raw, -1.0, 1.0);
        case BIFROST_SAT:
                return CLAMP(raw, 0.0, 1.0);
        default:
                return raw;
        }
}

static float
bit_srcmod(float raw, bool abs, bool neg)
{
        if (abs)
                raw = fabs(raw);

        if (neg)
                raw = -raw;

        return raw;
}

#define BIT_COND(cond, left, right) \
        if (cond == BI_COND_LT) return left < right; \
        else if (cond == BI_COND_LE) return left <= right; \
        else if (cond == BI_COND_GE) return left >= right; \
        else if (cond == BI_COND_GT) return left > right; \
        else if (cond == BI_COND_EQ) return left == right; \
        else if (cond == BI_COND_NE) return left != right; \
        else { return true; }

static bool
bit_eval_cond(enum bi_cond cond, bit_t l, bit_t r, nir_alu_type T, unsigned c)
{
        if (T == nir_type_float32) {
                BIT_COND(cond, l.f32, r.f32);
        } else if (T == nir_type_float16) {
                float left = bf(l.f16[c]);
                float right = bf(r.f16[c]);
                BIT_COND(cond, left, right);
        } else if (T == nir_type_int32) {
                int32_t left = (int32_t) l.u32;
                int32_t right = (int32_t) r.u32;
                BIT_COND(cond, left, right);
        } else if (T == nir_type_int16) {
                int16_t left = (int16_t) l.u32;
                int16_t right = (int16_t) r.u32;
                BIT_COND(cond, left, right);
        } else if (T == nir_type_uint32) {
                BIT_COND(cond, l.u32, r.u32);
        } else if (T == nir_type_uint16) {
                BIT_COND(cond, l.u16[c], r.u16[c]);
        } else {
                unreachable("Unknown type evaluated");
        }
}

static float
biti_special(float Q, enum bi_special_op op)
{
        switch (op) {
        case BI_SPECIAL_FRCP: return 1.0 / Q;
        case BI_SPECIAL_FRSQ: {
              double Qf = 1.0 / sqrt(Q);
              return Qf;
        }
        default: unreachable("Invalid special");
        }
}

/* For BI_CONVERT. */

#define _AS_ROUNDMODE(mode) \
        ((mode == BIFROST_RTZ) ? FP_INT_TOWARDZERO : \
        (mode == BIFROST_RTE) ? FP_INT_TONEAREST : \
        (mode == BIFROST_RTN) ? FP_INT_DOWNWARD : \
        FP_INT_UPWARD)

static float
bit_as_float32(nir_alu_type T, bit_t src, unsigned C)
{
        switch (T) {
        case nir_type_int32:   return src.i32;
        case nir_type_uint32:  return src.u32;
        case nir_type_float16: return bf(src.u16[C]);
        default: unreachable("Invalid");
        }
}

static uint32_t
bit_as_uint32(nir_alu_type T, bit_t src, unsigned C, enum bifrost_roundmode rm)
{
        switch (T) {
        case nir_type_float16: return bf(src.u16[C]);
        case nir_type_float32: return ufromfpf(src.f32, _AS_ROUNDMODE(rm), 32);
        default: unreachable("Invalid");
        }
}

static int32_t
bit_as_int32(nir_alu_type T, bit_t src, unsigned C, enum bifrost_roundmode rm)
{
        switch (T) {
        case nir_type_float16: return bf(src.u16[C]);
        case nir_type_float32: return fromfpf(src.f32, _AS_ROUNDMODE(rm), 32);
        default: unreachable("Invalid");
        }
}

static uint16_t
bit_as_float16(nir_alu_type T, bit_t src, unsigned C)
{
        switch (T) {
        case nir_type_int32:   return bh(src.i32);
        case nir_type_uint32:  return bh(src.u32);
        case nir_type_float32: return bh(src.f32);
        case nir_type_int16:   return bh(src.i16[C]);
        case nir_type_uint16:  return bh(src.u16[C]);
        default: unreachable("Invalid");
        }
}

static uint16_t
bit_as_uint16(nir_alu_type T, bit_t src, unsigned C, enum bifrost_roundmode rm)
{
        switch (T) {
        case nir_type_int32:   return src.i32;
        case nir_type_uint32:  return src.u32;
        case nir_type_float16: return ufromfpf(bf(src.u16[C]), _AS_ROUNDMODE(rm), 16);
        case nir_type_float32: return src.f32;
        default: unreachable("Invalid");
        }
}

static int16_t
bit_as_int16(nir_alu_type T, bit_t src, unsigned C, enum bifrost_roundmode rm)
{
        switch (T) {
        case nir_type_int32:   return src.i32;
        case nir_type_uint32:  return src.u32;
        case nir_type_float16: return fromfpf(bf(src.u16[C]), _AS_ROUNDMODE(rm), 16);
        case nir_type_float32: return src.f32;
        default: unreachable("Invalid");
        }
}

void
bit_step(struct bit_state *s, bi_instruction *ins, bool FMA)
{
        /* First, load sources */
        bit_t srcs[BIR_SRC_COUNT] = { 0 };

        bi_foreach_src(ins, src)
                srcs[src].u64 = bit_read(s, ins, ins->src[src], ins->src_types[src], FMA);

        /* Apply source modifiers if we need to */
        if (bi_has_source_mods(ins)) {
                bi_foreach_src(ins, src) {
                        if (ins->src_types[src] == nir_type_float16) {
                                for (unsigned c = 0; c < 2; ++c) {
                                        srcs[src].f16[c] = bh(bit_srcmod(bf(srcs[src].f16[c]),
                                                        ins->src_abs[src],
                                                        ins->src_neg[src]));
                                }
                        } else if (ins->src_types[src] == nir_type_float32) {
                                srcs[src].f32 = bit_srcmod(srcs[src].f32,
                                                        ins->src_abs[src],
                                                        ins->src_neg[src]);
                        }
                }
        }

        /* Next, do the action of the instruction */
        bit_t dest = { 0 };

        switch (ins->type) {
        case BI_ADD:
                bpoly(add);

        case BI_BRANCH:
        case BI_CMP:
        case BI_BITWISE:
                unreachable("Unsupported op");

        case BI_CONVERT: {
                /* If it exists */
                unsigned comp = ins->swizzle[0][1];

                if (ins->dest_type == nir_type_float32)
                        dest.f32 = bit_as_float32(ins->src_types[0], srcs[0], comp);
                else if (ins->dest_type == nir_type_uint32)
                        dest.u32 = bit_as_uint32(ins->src_types[0], srcs[0], comp, ins->roundmode);
                else if (ins->dest_type == nir_type_int32)
                        dest.i32 = bit_as_int32(ins->src_types[0], srcs[0], comp, ins->roundmode);
                else if (ins->dest_type == nir_type_float16) {
                        dest.u16[0] = bit_as_float16(ins->src_types[0], srcs[0], ins->swizzle[0][0]);

                        if (ins->src_types[0] == nir_type_float32) {
                                /* TODO: Second argument */
                                dest.u16[1] = 0;
                        } else {
                                dest.u16[1] = bit_as_float16(ins->src_types[0], srcs[0], ins->swizzle[0][1]);
                        }
                } else if (ins->dest_type == nir_type_uint16) {
                        dest.u16[0] = bit_as_uint16(ins->src_types[0], srcs[0], ins->swizzle[0][0], ins->roundmode);
                        dest.u16[1] = bit_as_uint16(ins->src_types[0], srcs[0], ins->swizzle[0][1], ins->roundmode);
                } else if (ins->dest_type == nir_type_int16) {
                        dest.i16[0] = bit_as_int16(ins->src_types[0], srcs[0], ins->swizzle[0][0], ins->roundmode);
                        dest.i16[1] = bit_as_int16(ins->src_types[0], srcs[0], ins->swizzle[0][1], ins->roundmode);
                } else {
                        unreachable("Unknown convert type");
                }

                break;
        }

        case BI_CSEL: {
                bool direct = ins->csel_cond == BI_COND_ALWAYS;
                bool cond = direct ? srcs[0].u32 :
                        bit_eval_cond(ins->csel_cond, srcs[0], srcs[1], ins->src_types[0], 0);

                dest = cond ? srcs[2] : srcs[3];
                break;
        }

        case BI_FMA: {
                bfloat(bit_f64fma, bit_f32fma);
                unreachable("Unknown type");
        }

        case BI_FREXP:
        case BI_ISUB:
                unreachable("Unsupported op");

        case BI_MINMAX: {
                if (ins->op.minmax == BI_MINMAX_MIN) {
                        bpoly(min);
                } else {
                        bpoly(max);
                }
        }

        case BI_MOV:
                bpoly(mov);

        case BI_SPECIAL: {
                assert(nir_alu_type_get_base_type(ins->dest_type) == nir_type_float);
                assert(nir_alu_type_get_base_type(ins->dest_type) != nir_type_float64);
                float Q = (ins->dest_type == nir_type_float16) ?
                        bf(srcs[0].u16[ins->swizzle[0][0]]) :
                        srcs[0].f32;

                float R = biti_special(Q, ins->op.special);

                if (ins->dest_type == nir_type_float16) {
                        dest.f16[0] = bh(R);

                        if (!ins->swizzle[0][0] && ins->op.special == BI_SPECIAL_FRSQ) {
                                /* Sorry. */
                                dest.f16[0]++;
                        }
                } else {
                        dest.f32 = R;
                }
                break;
        }

        case BI_SHIFT:
        case BI_SWIZZLE:
        case BI_ROUND:
                unreachable("Unsupported op");
        
        /* We only interpret vertex shaders */
        case BI_DISCARD:
        case BI_LOAD_VAR:
        case BI_ATEST:
        case BI_BLEND:
                unreachable("Fragment op used in interpreter");

        /* Modeling main memory is more than I bargained for */
        case BI_LOAD_UNIFORM:
        case BI_LOAD_ATTR:
        case BI_LOAD_VAR_ADDRESS:
        case BI_LOAD:
        case BI_STORE:
        case BI_STORE_VAR:
        case BI_TEX:
                unreachable("Unsupported I/O in interpreter");

        default:
                unreachable("Unsupported op");
        }

        /* Apply outmod */
        if (bi_has_outmod(ins) && ins->outmod != BIFROST_NONE) {
                if (ins->dest_type == nir_type_float16) {
                        for (unsigned c = 0; c < 2; ++c)
                                dest.f16[c] = bh(bit_outmod(bf(dest.f16[c]), ins->outmod));
                } else {
                        dest.f32 = bit_outmod(dest.f32, ins->outmod);
                }
        }

        /* Finally, store the result */
        bit_write(s, ins->dest, ins->dest_type, dest, FMA);

        /* For ADD - change out the passthrough */
        if (!FMA) {
                s->T0 = s->T;
                s->T1 = dest.u32;
        }
}

#undef bh
#undef bf
