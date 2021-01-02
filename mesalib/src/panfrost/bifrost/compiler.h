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

#ifndef __BIFROST_COMPILER_H
#define __BIFROST_COMPILER_H

#include "bifrost.h"
#include "bi_opcodes.h"
#include "compiler/nir/nir.h"
#include "panfrost/util/pan_ir.h"
#include "util/u_math.h"

/* Swizzles across bytes in a 32-bit word. Expresses swz in the XML directly.
 * To express widen, use the correpsonding replicated form, i.e. H01 = identity
 * for widen = none, H00 for widen = h0, B1111 for widen = b1. For lane, also
 * use the replicated form (interpretation is governed by the opcode). For
 * 8-bit lanes with two channels, use replicated forms for replicated forms
 * (TODO: what about others?). For 8-bit lanes with four channels using
 * matching form (TODO: what about others?).
 */

enum bi_swizzle {
        /* 16-bit swizzle ordering deliberate for fast compute */
        BI_SWIZZLE_H00 = 0, /* = B0101 */
        BI_SWIZZLE_H01 = 1, /* = B0123 = W0 */
        BI_SWIZZLE_H10 = 2, /* = B2301 */
        BI_SWIZZLE_H11 = 3, /* = B2323 */

        /* replication order should be maintained for fast compute */
        BI_SWIZZLE_B0000 = 4, /* single channel (replicate) */
        BI_SWIZZLE_B1111 = 5,
        BI_SWIZZLE_B2222 = 6,
        BI_SWIZZLE_B3333 = 7,

        /* totally special for explicit pattern matching */
        BI_SWIZZLE_B0011 = 8, /* +SWZ.v4i8 */
        BI_SWIZZLE_B2233 = 9, /* +SWZ.v4i8 */
        BI_SWIZZLE_B1032 = 10, /* +SWZ.v4i8 */
        BI_SWIZZLE_B3210 = 11, /* +SWZ.v4i8 */

        BI_SWIZZLE_B0022 = 12, /* for b02 lanes */
};

enum bi_index_type {
        BI_INDEX_NULL = 0,
        BI_INDEX_NORMAL = 1,
        BI_INDEX_REGISTER = 2,
        BI_INDEX_CONSTANT = 3,
        BI_INDEX_PASS = 4,
        BI_INDEX_FAU = 5
};

typedef struct {
        uint32_t value;

        /* modifiers, should only be set if applicable for a given instruction.
         * For *IDP.v4i8, abs plays the role of sign. For bitwise ops where
         * applicable, neg plays the role of not */
        bool abs : 1;
        bool neg : 1;

        enum bi_swizzle swizzle : 4;
        uint32_t offset : 2;
        bool reg : 1;
        enum bi_index_type type : 3;
} bi_index;

static inline bi_index
bi_get_index(unsigned value, bool is_reg, unsigned offset)
{
        return (bi_index) {
                .type = BI_INDEX_NORMAL,
                .value = value,
                .swizzle = BI_SWIZZLE_H01,
                .offset = offset,
                .reg = is_reg,
        };
}

static inline bi_index
bi_register(unsigned reg)
{
        assert(reg < 64);

        return (bi_index) {
                .type = BI_INDEX_REGISTER,
                .swizzle = BI_SWIZZLE_H01,
                .value = reg
        };
}

static inline bi_index
bi_imm_u32(uint32_t imm)
{
        return (bi_index) {
                .type = BI_INDEX_CONSTANT,
                .swizzle = BI_SWIZZLE_H01,
                .value = imm
        };
}

static inline bi_index
bi_imm_f32(float imm)
{
        return bi_imm_u32(fui(imm));
}

static inline bi_index
bi_null()
{
        return (bi_index) { .type = BI_INDEX_NULL };
}

static inline bi_index
bi_zero()
{
        return bi_imm_u32(0);
}

static inline bi_index
bi_passthrough(enum bifrost_packed_src value)
{
        return (bi_index) {
                .type = BI_INDEX_PASS,
                .swizzle = BI_SWIZZLE_H01,
                .value = value
        };
}

/* Extracts a word from a vectored index */
static inline bi_index
bi_word(bi_index idx, unsigned component)
{
        idx.offset += component;
        return idx;
}

/* Helps construct swizzles */
static inline bi_index
bi_half(bi_index idx, bool upper)
{
        assert(idx.swizzle == BI_SWIZZLE_H01);
        idx.swizzle = upper ? BI_SWIZZLE_H11 : BI_SWIZZLE_H00;
        return idx;
}

static inline bi_index
bi_byte(bi_index idx, unsigned lane)
{
        assert(idx.swizzle == BI_SWIZZLE_H01);
        assert(lane < 4);
        idx.swizzle = BI_SWIZZLE_B0000 + lane;
        return idx;
}

static inline bi_index
bi_abs(bi_index idx)
{
        assert(idx.type != BI_INDEX_CONSTANT);
        idx.abs = true;
        return idx;
}

static inline bi_index
bi_neg(bi_index idx)
{
        assert(idx.type != BI_INDEX_CONSTANT);
        idx.neg ^= true;
        return idx;
}

/* For bitwise instructions */
#define bi_not(x) bi_neg(x)

static inline bi_index
bi_imm_u8(uint8_t imm)
{
        return bi_byte(bi_imm_u32(imm), 0);
}

static inline bi_index
bi_imm_u16(uint16_t imm)
{
        return bi_half(bi_imm_u32(imm), false);
}

static inline bool
bi_is_null(bi_index idx)
{
        return idx.type == BI_INDEX_NULL;
}

/* Compares equivalence as references. Does not compare offsets, swizzles, or
 * modifiers. In other words, this forms bi_index equivalence classes by
 * partitioning memory. E.g. -abs(foo[1].yx) == foo.xy but foo != bar */

static inline bool
bi_is_equiv(bi_index left, bi_index right)
{
        return (left.type == right.type) &&
                (left.reg == right.reg) &&
                (left.value == right.value);
}

#define BI_MAX_DESTS 2
#define BI_MAX_SRCS 4

typedef struct {
        /* Must be first */
        struct list_head link;

        /* Link for the use chain */
        struct list_head use;

        enum bi_opcode op;

        /* Data flow */
        bi_index dest[BI_MAX_DESTS];
        bi_index src[BI_MAX_SRCS];

        /* For a branch */
        struct bi_block *branch_target;

        /* These don't fit neatly with anything else.. */
        enum bi_register_format register_format;
        enum bi_vecsize vecsize;

        /* Can we spill the value written here? Used to prevent
         * useless double fills */
        bool no_spill;

        /* Everything after this MUST NOT be accessed directly, since
         * interpretation depends on opcodes */

        /* Destination modifiers */
        union {
                enum bi_clamp clamp;
                bool saturate;
                bool not_result;
        };

        /* Immediates. All seen alone in an instruction, except for varying/texture
         * which are specified jointly for VARTEX */
        union {
                uint32_t shift;
                uint32_t fill;
                uint32_t index;
                uint32_t table;
                uint32_t attribute_index;

                struct {
                        uint32_t varying_index;
                        uint32_t sampler_index;
                        uint32_t texture_index;
                };

                /* TEXC, ATOM_CX: # of staging registers used */
                uint32_t sr_count;
        };

        /* Modifiers specific to particular instructions are thrown in a union */
        union {
                enum bi_adj adj; /* FEXP_TABLE.u4 */
                enum bi_atom_opc atom_opc; /* atomics */
                enum bi_func func; /* FPOW_SC_DET */
                enum bi_function function; /* LD_VAR_FLAT */
                enum bi_mode mode; /* FLOG_TABLE */
                enum bi_mux mux; /* MUX */
                enum bi_precision precision; /* FLOG_TABLE */
                enum bi_sem sem; /* FMAX, FMIN */
                enum bi_source source; /* LD_GCLK */
                bool scale; /* VN_ASST2, FSINCOS_OFFSET */
                bool offset; /* FSIN_TABLE, FOCS_TABLE */
                bool divzero; /* FRSQ_APPROX, FRSQ */
                bool mask; /* CLZ */
                bool threads; /* IMULD, IMOV_FMA */
                bool combine; /* BRANCHC */
                bool format; /* LEA_TEX */

                struct {
                        bool skip; /* VAR_TEX, TEXS, TEXC */
                        bool lod_mode; /* TEXS */
                };

                struct {
                        enum bi_special special; /* FADD_RSCALE, FMA_RSCALE */
                        enum bi_round round; /* FMA, converts, FADD, _RSCALE, etc */
                };

                struct {
                        enum bi_result_type result_type; /* FCMP, ICMP */
                        enum bi_cmpf cmpf; /* CSEL, FCMP, ICMP, BRANCH */
                };

                struct {
                        enum bi_stack_mode stack_mode; /* JUMP_EX */
                        bool test_mode;
                };

                struct {
                        enum bi_seg seg; /* LOAD, STORE, SEG_ADD, SEG_SUB */
                        bool preserve_null; /* SEG_ADD, SEG_SUB */
                        enum bi_extend extend; /* LOAD, IMUL */
                };

                struct {
                        enum bi_sample sample; /* LD_VAR */
                        enum bi_update update; /* LD_VAR */
                        enum bi_varying_name varying_name; /* LD_VAR_SPECIAL */
                };

                struct {
                        enum bi_subgroup subgroup; /* WMASK, CLPER */
                        enum bi_inactive_result inactive_result; /* CLPER */
                        enum bi_lane_op lane_op; /* CLPER */
                };

                struct {
                        bool z; /* ZS_EMIT */
                        bool stencil; /* ZS_EMIT */
                };

                struct {
                        bool h; /* VN_ASST1.f16 */
                        bool l; /* VN_ASST1.f16 */
                };

                struct {
                        bool bytes2; /* RROT_DOUBLE, FRSHIFT_DOUBLE */
                        bool result_word;
                };

                struct {
                        bool sqrt; /* FREXPM */
                        bool log; /* FREXPM */
                };
        };
} bi_instr;

/* Represents the assignment of slots for a given bi_bundle */

typedef struct {
        /* Register to assign to each slot */
        unsigned slot[4];

        /* Read slots can be disabled */
        bool enabled[2];

        /* Configuration for slots 2/3 */
        struct bifrost_reg_ctrl_23 slot23;

        /* Fast-Access-Uniform RAM index */
        uint8_t fau_idx;

        /* Whether writes are actually for the last instruction */
        bool first_instruction;
} bi_registers;

/* A bi_bundle contains two paired instruction pointers. If a slot is unfilled,
 * leave it NULL; the emitter will fill in a nop. Instructions reference
 * registers via slots which are assigned per bundle.
 */

typedef struct {
        uint8_t fau_idx;
        bi_registers regs;
        bi_instr *fma;
        bi_instr *add;
} bi_bundle;

struct bi_block;

typedef struct {
        struct list_head link;

        /* Link back up for branch calculations */
        struct bi_block *block;

        /* A clause can have 8 instructions in bundled FMA/ADD sense, so there
         * can be 8 bundles. */

        unsigned bundle_count;
        bi_bundle bundles[8];

        /* For scoreboarding -- the clause ID (this is not globally unique!)
         * and its dependencies in terms of other clauses, computed during
         * scheduling and used when emitting code. Dependencies expressed as a
         * bitfield matching the hardware, except shifted by a clause (the
         * shift back to the ISA's off-by-one encoding is worked out when
         * emitting clauses) */
        unsigned scoreboard_id;
        uint8_t dependencies;

        /* See ISA header for description */
        enum bifrost_flow flow_control;

        /* Can we prefetch the next clause? Usually it makes sense, except for
         * clauses ending in unconditional branches */
        bool next_clause_prefetch;

        /* Assigned data register */
        unsigned staging_register;

        /* Corresponds to the usual bit but shifted by a clause */
        bool staging_barrier;

        /* Constants read by this clause. ISA limit. Must satisfy:
         *
         *      constant_count + bundle_count <= 13
         *
         * Also implicitly constant_count <= bundle_count since a bundle only
         * reads a single constant.
         */
        uint64_t constants[8];
        unsigned constant_count;

        /* Branches encode a constant offset relative to the program counter
         * with some magic flags. By convention, if there is a branch, its
         * constant will be last. Set this flag to indicate this is required.
         */
        bool branch_constant;

        /* What type of high latency instruction is here, basically */
        unsigned message_type;
} bi_clause;

typedef struct bi_block {
        pan_block base; /* must be first */

        /* If true, uses clauses; if false, uses instructions */
        bool scheduled;
        struct list_head clauses; /* list of bi_clause */
} bi_block;

typedef struct {
       nir_shader *nir;
       gl_shader_stage stage;
       struct list_head blocks; /* list of bi_block */
       struct panfrost_sysvals sysvals;
       uint32_t quirks;
       unsigned arch;
       unsigned tls_size;

       /* Is internally a blend shader? Depends on stage == FRAGMENT */
       bool is_blend;

       /* Blend constants */
       float blend_constants[4];

       /* Blend return offsets */
       uint32_t blend_ret_offsets[8];

       /* Blend tile buffer conversion desc */
       uint64_t blend_desc;

       /* During NIR->BIR */
       nir_function_impl *impl;
       bi_block *current_block;
       bi_block *after_block;
       bi_block *break_block;
       bi_block *continue_block;
       bool emitted_atest;
       nir_alu_type *blend_types;

       /* For creating temporaries */
       unsigned temp_alloc;

       /* Analysis results */
       bool has_liveness;

       /* Stats for shader-db */
       unsigned instruction_count;
       unsigned loop_count;
       unsigned spills;
       unsigned fills;
} bi_context;

static inline void
bi_remove_instruction(bi_instr *ins)
{
        list_del(&ins->link);
}

enum bir_fau {
        BIR_FAU_ZERO = 0,
        BIR_FAU_LANE_ID = 1,
        BIR_FAU_WRAP_ID = 2,
        BIR_FAU_CORE_ID = 3,
        BIR_FAU_FB_EXTENT = 4,
        BIR_FAU_ATEST_PARAM = 5,
        BIR_FAU_SAMPLE_POS_ARRAY = 6,
        BIR_FAU_BLEND_0 = 8,
        /* blend descs 1 - 7 */
        BIR_FAU_TYPE_MASK = 15,
        BIR_FAU_UNIFORM = (1 << 7),
        BIR_FAU_HI = (1 << 8),
};

static inline bi_index
bi_fau(enum bir_fau value, bool hi)
{
        return (bi_index) {
                .type = BI_INDEX_FAU,
                .value = value,
                .swizzle = BI_SWIZZLE_H01,
                .offset = hi ? 1 : 0
        };
}

static inline unsigned
bi_max_temp(bi_context *ctx)
{
        unsigned alloc = MAX2(ctx->impl->reg_alloc, ctx->impl->ssa_alloc);
        return ((alloc + 2 + ctx->temp_alloc) << 1);
}

static inline bi_index
bi_temp(bi_context *ctx)
{
        unsigned alloc = (ctx->impl->ssa_alloc + ctx->temp_alloc++);
        return bi_get_index(alloc, false, 0);
}

static inline bi_index
bi_temp_reg(bi_context *ctx)
{
        unsigned alloc = (ctx->impl->reg_alloc + ctx->temp_alloc++);
        return bi_get_index(alloc, true, 0);
}


/* Inline constants automatically, will be lowered out by bi_lower_fau where a
 * constant is not allowed. load_const_to_scalar gaurantees that this makes
 * sense */

static inline bi_index
bi_src_index(nir_src *src)
{
        if (nir_src_is_const(*src))
                return bi_imm_u32(nir_src_as_uint(*src));
        else if (src->is_ssa)
                return bi_get_index(src->ssa->index, false, 0);
        else {
                assert(!src->reg.indirect);
                return bi_get_index(src->reg.reg->index, true, 0);
        }
}

static inline bi_index
bi_dest_index(nir_dest *dst)
{
        if (dst->is_ssa)
                return bi_get_index(dst->ssa.index, false, 0);
        else {
                assert(!dst->reg.indirect);
                return bi_get_index(dst->reg.reg->index, true, 0);
        }
}

static inline unsigned
bi_get_node(bi_index index)
{
        if (bi_is_null(index) || index.type != BI_INDEX_NORMAL)
                return ~0;
        else
                return (index.value << 1) | index.reg;
}

static inline bi_index
bi_node_to_index(unsigned node, unsigned node_count)
{
        assert(node < node_count);
        assert(node_count < ~0);

        return bi_get_index(node >> 1, node & PAN_IS_REG, 0);
}

/* Iterators for Bifrost IR */

#define bi_foreach_block(ctx, v) \
        list_for_each_entry(pan_block, v, &ctx->blocks, link)

#define bi_foreach_block_from(ctx, from, v) \
        list_for_each_entry_from(pan_block, v, from, &ctx->blocks, link)

#define bi_foreach_block_from_rev(ctx, from, v) \
        list_for_each_entry_from_rev(pan_block, v, from, &ctx->blocks, link)

#define bi_foreach_instr_in_block(block, v) \
        list_for_each_entry(bi_instr, v, &(block)->base.instructions, link)

#define bi_foreach_instr_in_block_rev(block, v) \
        list_for_each_entry_rev(bi_instr, v, &(block)->base.instructions, link)

#define bi_foreach_instr_in_block_safe(block, v) \
        list_for_each_entry_safe(bi_instr, v, &(block)->base.instructions, link)

#define bi_foreach_instr_in_block_safe_rev(block, v) \
        list_for_each_entry_safe_rev(bi_instr, v, &(block)->base.instructions, link)

#define bi_foreach_instr_in_block_from(block, v, from) \
        list_for_each_entry_from(bi_instr, v, from, &(block)->base.instructions, link)

#define bi_foreach_instr_in_block_from_rev(block, v, from) \
        list_for_each_entry_from_rev(bi_instr, v, from, &(block)->base.instructions, link)

#define bi_foreach_clause_in_block(block, v) \
        list_for_each_entry(bi_clause, v, &(block)->clauses, link)

#define bi_foreach_clause_in_block_safe(block, v) \
        list_for_each_entry_safe(bi_clause, v, &(block)->clauses, link)

#define bi_foreach_clause_in_block_from(block, v, from) \
        list_for_each_entry_from(bi_clause, v, from, &(block)->clauses, link)

#define bi_foreach_clause_in_block_from_rev(block, v, from) \
        list_for_each_entry_from_rev(bi_clause, v, from, &(block)->clauses, link)

#define bi_foreach_instr_global(ctx, v) \
        bi_foreach_block(ctx, v_block) \
                bi_foreach_instr_in_block((bi_block *) v_block, v)

#define bi_foreach_instr_global_safe(ctx, v) \
        bi_foreach_block(ctx, v_block) \
                bi_foreach_instr_in_block_safe((bi_block *) v_block, v)

/* Based on set_foreach, expanded with automatic type casts */

#define bi_foreach_predecessor(blk, v) \
        struct set_entry *_entry_##v; \
        bi_block *v; \
        for (_entry_##v = _mesa_set_next_entry(blk->base.predecessors, NULL), \
                v = (bi_block *) (_entry_##v ? _entry_##v->key : NULL);  \
                _entry_##v != NULL; \
                _entry_##v = _mesa_set_next_entry(blk->base.predecessors, _entry_##v), \
                v = (bi_block *) (_entry_##v ? _entry_##v->key : NULL))

#define bi_foreach_src(ins, v) \
        for (unsigned v = 0; v < ARRAY_SIZE(ins->src); ++v)

static inline bi_instr *
bi_prev_op(bi_instr *ins)
{
        return list_last_entry(&(ins->link), bi_instr, link);
}

static inline bi_instr *
bi_next_op(bi_instr *ins)
{
        return list_first_entry(&(ins->link), bi_instr, link);
}

static inline pan_block *
pan_next_block(pan_block *block)
{
        return list_first_entry(&(block->link), pan_block, link);
}

/* BIR manipulation */

bool bi_has_arg(bi_instr *ins, bi_index arg);
uint16_t bi_bytemask_of_read_components(bi_instr *ins, bi_index node);
unsigned bi_writemask(bi_instr *ins);

void bi_print_instr(bi_instr *I, FILE *fp);
void bi_print_slots(bi_registers *regs, FILE *fp);
void bi_print_bundle(bi_bundle *bundle, FILE *fp);
void bi_print_clause(bi_clause *clause, FILE *fp);
void bi_print_block(bi_block *block, FILE *fp);
void bi_print_shader(bi_context *ctx, FILE *fp);

/* BIR passes */

bool bi_opt_dead_code_eliminate(bi_context *ctx, bi_block *block);
void bi_schedule(bi_context *ctx);
void bi_register_allocate(bi_context *ctx);

bi_clause *
bi_singleton(void *memctx, bi_instr *ins,
                bi_block *block,
                unsigned scoreboard_id,
                unsigned dependencies,
                bool osrb);

/* Liveness */

void bi_compute_liveness(bi_context *ctx);
void bi_liveness_ins_update(uint16_t *live, bi_instr *ins, unsigned max);
void bi_invalidate_liveness(bi_context *ctx);

/* Layout */

bool bi_can_insert_bundle(bi_clause *clause, bool constant);
unsigned bi_clause_quadwords(bi_clause *clause);
signed bi_block_offset(bi_context *ctx, bi_clause *start, bi_block *target);

/* Code emit */

void bi_pack(bi_context *ctx, struct util_dynarray *emission);

unsigned bi_pack_fma(bi_instr *I,
                enum bifrost_packed_src src0,
                enum bifrost_packed_src src1,
                enum bifrost_packed_src src2,
                enum bifrost_packed_src src3);
unsigned bi_pack_add(bi_instr *I,
                enum bifrost_packed_src src0,
                enum bifrost_packed_src src1,
                enum bifrost_packed_src src2,
                enum bifrost_packed_src src3);

/* Like in NIR, for use with the builder */

enum bi_cursor_option {
    bi_cursor_after_block,
    bi_cursor_before_instr,
    bi_cursor_after_instr
};

typedef struct {
    enum bi_cursor_option option;

    union {
        bi_block *block;
        bi_instr *instr;
    };
} bi_cursor;

static inline bi_cursor
bi_after_block(bi_block *block)
{
    return (bi_cursor) {
        .option = bi_cursor_after_block,
        .block = block
    };
}

static inline bi_cursor
bi_before_instr(bi_instr *instr)
{
    return (bi_cursor) {
        .option = bi_cursor_before_instr,
        .instr = instr
    };
}

static inline bi_cursor
bi_after_instr(bi_instr *instr)
{
    return (bi_cursor) {
        .option = bi_cursor_after_instr,
        .instr = instr
    };
}

/* IR builder in terms of cursor infrastructure */

typedef struct {
    bi_context *shader;
    bi_cursor cursor;
} bi_builder;

/* Insert an instruction at the cursor and move the cursor */

static inline void
bi_builder_insert(bi_cursor *cursor, bi_instr *I)
{
    switch (cursor->option) {
    case bi_cursor_after_instr:
        list_add(&I->link, &cursor->instr->link);
        cursor->instr = I;
        return;

    case bi_cursor_after_block:
        list_addtail(&I->link, &cursor->block->base.instructions);
        cursor->option = bi_cursor_after_instr;
        cursor->instr = I;
        return;

    case bi_cursor_before_instr:
        list_addtail(&I->link, &cursor->instr->link);
        cursor->option = bi_cursor_after_instr;
        cursor->instr = I;
        return;
    }

    unreachable("Invalid cursor option");
}

#endif
