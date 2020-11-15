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

#define bv4i8(fxn) \
        for (unsigned c = 0; c < 4; ++c) { \
                dest.u8[c] = fxn(srcs[0].u8[ins->swizzle[0][c]], \
                                        srcs[1].u8[ins->swizzle[1][c]], \
                                        srcs[2].u8[ins->swizzle[2][c]], \
                                        srcs[3].u8[ins->swizzle[3][c]]); \
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
                bv4i8(fxn8); \
                break; \
        }

#define bpoly(name) \
        bfloat(bit_f64 ## name, bit_f32 ## name); \
        bint(bit_i64 ## name, bit_i32 ## name, bit_i16 ## name, bit_i8 ## name); \
        unreachable("Invalid type");

#define bit_make_float_2(name, expr32, expr64) \
        static inline double \
        bit_f64 ## name(double a, double b, double c, double d) \
        { \
                return expr64; \
        } \
        static inline float \
        bit_f32 ## name(float a, float b, float c, float d) \
        { \
                return expr32; \
        } \

#define bit_make_float(name, expr) \
        bit_make_float_2(name, expr, expr)

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
bit_make_int(sub, a - b);
bit_make_float(fma, (a * b) + c);
bit_make_poly(mov, a);
bit_make_poly(min, MIN2(a, b));
bit_make_poly(max, MAX2(a, b));
bit_make_float_2(floor, floorf(a), floor(a));
bit_make_float_2(ceil,  ceilf(a), ceil(a));
bit_make_float_2(trunc, truncf(a), trunc(a));
bit_make_float_2(nearbyint, nearbyintf(a), nearbyint(a));

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
                return SATURATE(raw);
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
bit_eval_cond(enum bi_cond cond, bit_t l, bit_t r, nir_alu_type T, unsigned cl, unsigned cr)
{
        if (T == nir_type_float32) {
                BIT_COND(cond, l.f32, r.f32);
        } else if (T == nir_type_float16) {
                float left = bf(l.f16[cl]);
                float right = bf(r.f16[cr]);
                BIT_COND(cond, left, right);
        } else if (T == nir_type_int32) {
                int32_t left = l.u32;
                int32_t right = r.u32;
                BIT_COND(cond, left, right);
        } else if (T == nir_type_int16) {
                int16_t left = l.i16[cl];
                int16_t right = r.i16[cr];
                BIT_COND(cond, left, right);
        } else if (T == nir_type_uint32) {
                BIT_COND(cond, l.u32, r.u32);
        } else if (T == nir_type_uint16) {
                BIT_COND(cond, l.u16[cl], r.u16[cr]);
        } else {
                unreachable("Unknown type evaluated");
        }
}

static unsigned
bit_cmp(enum bi_cond cond, bit_t l, bit_t r, nir_alu_type T, unsigned cl, unsigned cr, bool d3d)
{
        bool v = bit_eval_cond(cond, l, r, T, cl, cr);

        /* Fill for D3D but only up to 32-bit... 64-bit is only partial
         * (although we probably need a cleverer representation for 64-bit) */

        unsigned sz = MIN2(nir_alu_type_get_type_size(T), 32);
        unsigned max = (sz == 32) ? (~0) : ((1 << sz) - 1);

        return v ? (d3d ? max : 1) : 0;
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

static float
frexp_log(float x, int *e)
{
        /* Ignore sign until end */
        float xa = fabs(x);

        /* frexp reduces to [0.5, 1) */
        float f = frexpf(xa, e);

        /* reduce to [0.75, 1.5) */
        if (f < 0.75) {
                f *= 2.0;
                (*e)--;
        }

        /* Reattach sign */
        if (xa < 0.0)
                f = -f;

        return f;
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
                unreachable("Unsupported op");

        case BI_CMP: {
                nir_alu_type T = ins->src_types[0];
                unsigned sz = nir_alu_type_get_type_size(T);

                if (sz == 32 || sz == 64) {
                        dest.u32 = bit_cmp(ins->cond, srcs[0], srcs[1], T, 0, 0, true);
                } else if (sz == 16) {
                        for (unsigned c = 0; c < 2; ++c) {
                                dest.u16[c] = bit_cmp(ins->cond, srcs[0], srcs[1],
                                                T, ins->swizzle[0][c], ins->swizzle[1][c],
                                                true);
                        }
                } else if (sz == 8) {
                        for (unsigned c = 0; c < 4; ++c) {
                                dest.u8[c] = bit_cmp(ins->cond, srcs[0], srcs[1],
                                                T, ins->swizzle[0][c], ins->swizzle[1][c],
                                                true);
                        }
                } else {
                        unreachable("Invalid");
                }

                break;
        }

        case BI_BITWISE: {
                /* Apply inverts first */
                if (ins->bitwise.src1_invert)
                        srcs[1].u64 = ~srcs[1].u64;

                /* TODO: Shifting */
                assert(srcs[2].u32 == 0);

                if (ins->op.bitwise == BI_BITWISE_AND)
                        dest.u64 = srcs[0].u64 & srcs[1].u64;
                else if (ins->op.bitwise == BI_BITWISE_OR)
                        dest.u64 = srcs[0].u64 | srcs[1].u64;
                else if (ins->op.bitwise == BI_BITWISE_XOR)
                        dest.u64 = srcs[0].u64 ^ srcs[1].u64;
                else
                        unreachable("Unsupported op");

                if (ins->bitwise.dest_invert)
                        dest.u64 = ~dest.u64;

                break;
         }

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
                        dest.u16[1] = bit_as_float16(ins->src_types[0], srcs[0], ins->swizzle[0][1]);
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
                bool direct = ins->cond == BI_COND_ALWAYS;
                unsigned sz = nir_alu_type_get_type_size(ins->src_types[0]);

                if (sz == 32) {
                        bool cond = direct ? srcs[0].u32 :
                                bit_eval_cond(ins->cond, srcs[0], srcs[1], ins->src_types[0], 0, 0);

                        dest = cond ? srcs[2] : srcs[3];
                } else if (sz == 16) {
                        for (unsigned c = 0; c < 2; ++c) {
                                bool cond = direct ? srcs[0].u16[c] :
                                        bit_eval_cond(ins->cond, srcs[0], srcs[1], ins->src_types[0], c, c);

                                dest.u16[c] = cond ? srcs[2].u16[c] : srcs[3].u16[c];
                        }
                } else {
                        unreachable("Remaining types todo");
                }

                break;
        }

        case BI_FMA: {
                bfloat(bit_f64fma, bit_f32fma);
                unreachable("Unknown type");
        }

        case BI_FREXP: {
                if (ins->src_types[0] != nir_type_float32)
                        unreachable("Unknown frexp type");


                if (ins->op.frexp == BI_FREXPE_LOG)
                        frexp_log(srcs[0].f32, &dest.i32);
                else
                        unreachable("Unknown frexp");

                break;
        }

        case BI_IMATH: {
                if (ins->op.imath == BI_IMATH_ADD) {
                        bint(bit_i64add, bit_i32add, bit_i16add, bit_i8add);
                } else if (ins->op.imath == BI_IMATH_SUB) {
                        bint(bit_i64sub, bit_i32sub, bit_i16sub, bit_i8sub);
                } else {
                        unreachable("Unsupported op");
                }

                break;
        }

        case BI_MINMAX: {
                if (ins->op.minmax == BI_MINMAX_MIN) {
                        bpoly(min);
                } else {
                        bpoly(max);
                }
        }

        case BI_MOV:
                bpoly(mov);

        case BI_REDUCE_FMA: {
                if (ins->src_types[0] != nir_type_float32)
                        unreachable("Unknown reduce type");

                if (ins->op.reduce == BI_REDUCE_ADD_FREXPM) {
                        int _nop = 0;
                        float f = frexp_log(srcs[1].f32, &_nop);
                        dest.f32 = srcs[0].f32 + f;
                } else {
                        unreachable("Unknown reduce");
                }

                break;
        }

        case BI_SPECIAL_FMA:
        case BI_SPECIAL_ADD: {
                assert(nir_alu_type_get_base_type(ins->dest_type) == nir_type_float);
                assert(ins->dest_type != nir_type_float64);

                if (ins->op.special == BI_SPECIAL_EXP2_LOW) {
                        assert(ins->dest_type == nir_type_float32);
                        dest.f32 = exp2f(srcs[1].f32);
                        break;
                }

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

        case BI_TABLE: {
                if (ins->op.table == BI_TABLE_LOG2_U_OVER_U_1_LOW) {
                        assert(ins->dest_type == nir_type_float32);
                        int _nop = 0;
                        float f = frexp_log(srcs[0].f32, &_nop);
                        dest.f32 = log2f(f) / (f - 1.0);
                        dest.u32++; /* Sorry. */
                } else {
                        unreachable("Unknown table op");
                }
                break;
       }

        case BI_SELECT: {
                if (ins->src_types[0] == nir_type_uint16) {
                        for (unsigned c = 0; c < 2; ++c)
                                dest.u16[c] = srcs[c].u16[ins->swizzle[c][0]];
                } else if (ins->src_types[0] == nir_type_uint8) {
                        for (unsigned c = 0; c < 4; ++c)
                                dest.u8[c] = srcs[c].u8[ins->swizzle[c][0]];
                } else {
                        unreachable("Unknown type");
                }
                break;
        }

        case BI_ROUND: {
                if (ins->roundmode == BIFROST_RTP) {
                        bfloat(bit_f64ceil, bit_f32ceil);
                } else if (ins->roundmode == BIFROST_RTN) {
                        bfloat(bit_f64floor, bit_f32floor);
                } else if (ins->roundmode == BIFROST_RTE) {
                        bfloat(bit_f64nearbyint, bit_f32nearbyint);
                } else if (ins->roundmode == BIFROST_RTZ) {
                        bfloat(bit_f64trunc, bit_f32trunc);
                } else
                        unreachable("Invalid");

                break;
        }
        
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
        case BI_TEXS:
        case BI_TEXC:
        case BI_TEXC_DUAL:
                unreachable("Unsupported I/O in interpreter");

        default:
                unreachable("Unsupported op");
        }

        /* Apply _MSCALE */
        if ((ins->type == BI_FMA || ins->type == BI_ADD) && ins->op.mscale) {
                unsigned idx = (ins->type == BI_FMA) ? 3 : 2;

                assert(ins->src_types[idx] == nir_type_int32);
                assert(ins->dest_type == nir_type_float32);

                int32_t scale = srcs[idx].i32;
                dest.f32 *= exp2f(scale);
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
