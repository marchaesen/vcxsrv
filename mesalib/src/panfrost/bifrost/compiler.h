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
#include "compiler/nir/nir.h"
#include "panfrost/util/pan_ir.h"

/* Bifrost opcodes are tricky -- the same op may exist on both FMA and
 * ADD with two completely different opcodes, and opcodes can be varying
 * length in some cases. Then we have different opcodes for int vs float
 * and then sometimes even for different typesizes. Further, virtually
 * every op has a number of flags which depend on the op. In constrast
 * to Midgard where you have a strict ALU/LDST/TEX division and within
 * ALU you have strict int/float and that's it... here it's a *lot* more
 * involved. As such, we use something much higher level for our IR,
 * encoding "classes" of operations, letting the opcode details get
 * sorted out at emit time.
 *
 * Please keep this list alphabetized. Please use a dictionary if you
 * don't know how to do that.
 */

enum bi_class {
        BI_ADD,
        BI_ATEST,
        BI_BRANCH,
        BI_CMP,
        BI_BLEND,
        BI_BITWISE,
        BI_COMBINE,
        BI_CONVERT,
        BI_CSEL,
        BI_DISCARD,
        BI_FMA,
        BI_FMOV,
        BI_FREXP,
        BI_IMATH,
        BI_LOAD,
        BI_LOAD_UNIFORM,
        BI_LOAD_ATTR,
        BI_LOAD_VAR,
        BI_LOAD_VAR_ADDRESS,
        BI_MINMAX,
        BI_MOV,
        BI_REDUCE_FMA,
        BI_SELECT,
        BI_SHIFT,
        BI_STORE,
        BI_STORE_VAR,
        BI_SPECIAL, /* _FAST on supported GPUs */
        BI_TABLE,
        BI_TEX,
        BI_ROUND,
        BI_NUM_CLASSES
};

/* Properties of a class... */
extern unsigned bi_class_props[BI_NUM_CLASSES];

/* abs/neg/outmod valid for a float op */
#define BI_MODS (1 << 0)

/* Accepts a bi_cond */
#define BI_CONDITIONAL (1 << 1)

/* Accepts a bifrost_roundmode */
#define BI_ROUNDMODE (1 << 2)

/* Can be scheduled to FMA */
#define BI_SCHED_FMA (1 << 3)

/* Can be scheduled to ADD */
#define BI_SCHED_ADD (1 << 4)

/* Most ALU ops can do either, actually */
#define BI_SCHED_ALL (BI_SCHED_FMA | BI_SCHED_ADD)

/* Along with setting BI_SCHED_ADD, eats up the entire cycle, so FMA must be
 * nopped out. Used for _FAST operations. */
#define BI_SCHED_SLOW (1 << 5)

/* Swizzling allowed for the 8/16-bit source */
#define BI_SWIZZLABLE (1 << 6)

/* For scheduling purposes this is a high latency instruction and must be at
 * the end of a clause. Implies ADD */
#define BI_SCHED_HI_LATENCY (1 << 7)

/* Intrinsic is vectorized and acts with `vector_channels` components */
#define BI_VECTOR (1 << 8)

/* Use a data register for src0/dest respectively, bypassing the usual
 * register accessor. Mutually exclusive. */
#define BI_DATA_REG_SRC (1 << 9)
#define BI_DATA_REG_DEST (1 << 10)

/* Quirk: cannot encode multiple abs on FMA in fp16 mode */
#define BI_NO_ABS_ABS_FP16_FMA (1 << 11)

/* It can't get any worse than csel4... can it? */
#define BIR_SRC_COUNT 4

/* BI_LD_VARY */
struct bi_load_vary {
        enum bifrost_interp_mode interp_mode;
        bool reuse;
        bool flat;
};

/* BI_BRANCH encoding the details of the branch itself as well as a pointer to
 * the target. We forward declare bi_block since this is mildly circular (not
 * strictly, but this order of the file makes more sense I think)
 *
 * We define our own enum of conditions since the conditions in the hardware
 * packed in crazy ways that would make manipulation unweildly (meaning changes
 * based on port swapping, etc), so we defer dealing with that until emit time.
 * Likewise, we expose NIR types instead of the crazy branch types, although
 * the restrictions do eventually apply of course. */

struct bi_block;

enum bi_cond {
        BI_COND_ALWAYS,
        BI_COND_LT,
        BI_COND_LE,
        BI_COND_GE,
        BI_COND_GT,
        BI_COND_EQ,
        BI_COND_NE,
};

/* Opcodes within a class */
enum bi_minmax_op {
        BI_MINMAX_MIN,
        BI_MINMAX_MAX
};

enum bi_bitwise_op {
        BI_BITWISE_AND,
        BI_BITWISE_OR,
        BI_BITWISE_XOR
};

enum bi_imath_op {
        BI_IMATH_ADD,
        BI_IMATH_SUB,
};

enum bi_table_op {
        /* fp32 log2() with low precision, suitable for GL or half_log2() in
         * CL. In the first argument, takes x. Letting u be such that x =
         * 2^{-m} u with m integer and 0.75 <= u < 1.5, returns
         * log2(u) / (u - 1). */

        BI_TABLE_LOG2_U_OVER_U_1_LOW,
};

enum bi_reduce_op {
        /* Takes two fp32 arguments and returns x + frexp(y). Used in
         * low-precision log2 argument reduction on newer models. */

        BI_REDUCE_ADD_FREXPM,
};

enum bi_frexp_op {
        BI_FREXPE_LOG,
};

enum bi_special_op {
        BI_SPECIAL_FRCP,
        BI_SPECIAL_FRSQ,

        /* fp32 exp2() with low precision, suitable for half_exp2() in CL or
         * exp2() in GL. In the first argument, it takes f2i_rte(x * 2^24). In
         * the second, it takes x itself. */
        BI_SPECIAL_EXP2_LOW,
};

enum bi_tex_op {
        BI_TEX_NORMAL,
        BI_TEX_COMPACT,
        BI_TEX_DUAL
};

struct bi_bitwise {
        bool src_invert[2];
        bool rshift; /* false for lshift */
};

struct bi_texture {
        /* Constant indices. Indirect would need to be in src[..] like normal,
         * we can reserve some sentinels there for that for future. */
        unsigned texture_index, sampler_index;
};

typedef struct {
        struct list_head link; /* Must be first */
        enum bi_class type;

        /* Indices, see pan_ssa_index etc. Note zero is special cased
         * to "no argument" */
        unsigned dest;
        unsigned src[BIR_SRC_COUNT];

        /* 32-bit word offset for destination, added to the register number in
         * RA when lowering combines */
        unsigned dest_offset;

        /* If one of the sources has BIR_INDEX_CONSTANT */
        union {
                uint64_t u64;
                uint32_t u32;
                uint16_t u16[2];
                uint8_t u8[4];
        } constant;

        /* Floating-point modifiers, type/class permitting. If not
         * allowed for the type/class, these are ignored. */
        enum bifrost_outmod outmod;
        bool src_abs[BIR_SRC_COUNT];
        bool src_neg[BIR_SRC_COUNT];

        /* Round mode (requires BI_ROUNDMODE) */
        enum bifrost_roundmode roundmode;

        /* Destination type. Usually the type of the instruction
         * itself, but if sources and destination have different
         * types, the type of the destination wins (so f2i would be
         * int). Zero if there is no destination. Bitsize included */
        nir_alu_type dest_type;

        /* Source types if required by the class */
        nir_alu_type src_types[BIR_SRC_COUNT];

        /* If the source type is 8-bit or 16-bit such that SIMD is possible,
         * and the class has BI_SWIZZLABLE, this is a swizzle in the usual
         * sense. On non-SIMD instructions, it can be used for component
         * selection, so we don't have to special case extraction. */
        uint8_t swizzle[BIR_SRC_COUNT][NIR_MAX_VEC_COMPONENTS];

        /* For VECTOR ops, how many channels are written? */
        unsigned vector_channels;

        /* The comparison op. BI_COND_ALWAYS may not be valid. */
        enum bi_cond cond;

        /* A class-specific op from which the actual opcode can be derived
         * (along with the above information) */

        union {
                enum bi_minmax_op minmax;
                enum bi_bitwise_op bitwise;
                enum bi_special_op special;
                enum bi_reduce_op reduce;
                enum bi_table_op table;
                enum bi_frexp_op frexp;
                enum bi_tex_op texture;
                enum bi_imath_op imath;

                /* For FMA/ADD, should we add a biased exponent? */
                bool mscale;
        } op;

        /* Union for class-specific information */
        union {
                enum bifrost_minmax_mode minmax;
                struct bi_load_vary load_vary;
                struct bi_block *branch_target;

                /* For BLEND -- the location 0-7 */
                unsigned blend_location;

                struct bi_bitwise bitwise;
                struct bi_texture texture;
        };
} bi_instruction;

/* Represents the assignment of ports for a given bi_bundle */

typedef struct {
        /* Register to assign to each port */
        unsigned port[4];

        /* Read ports can be disabled */
        bool enabled[2];

        /* Should we write FMA? what about ADD? If only a single port is
         * enabled it is in port 2, else ADD/FMA is 2/3 respectively */
        bool write_fma, write_add;

        /* Should we read with port 3? */
        bool read_port3;

        /* Packed uniform/constant */
        uint8_t uniform_constant;

        /* Whether writes are actually for the last instruction */
        bool first_instruction;
} bi_registers;

/* A bi_bundle contains two paired instruction pointers. If a slot is unfilled,
 * leave it NULL; the emitter will fill in a nop. Instructions reference
 * registers via ports which are assigned per bundle.
 */

typedef struct {
        bi_registers regs;
        bi_instruction *fma;
        bi_instruction *add;
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

        /* Back-to-back corresponds directly to the back-to-back bit. Branch
         * conditional corresponds to the branch conditional bit except that in
         * the emitted code it's always set if back-to-bit is, whereas we use
         * the actual value (without back-to-back so to speak) internally */
        bool back_to_back;
        bool branch_conditional;

        /* Assigned data register */
        unsigned data_register;

        /* Corresponds to the usual bit but shifted by a clause */
        bool data_register_write_barrier;

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
        unsigned clause_type;
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
} bi_context;

static inline bi_instruction *
bi_emit(bi_context *ctx, bi_instruction ins)
{
        bi_instruction *u = rzalloc(ctx, bi_instruction);
        memcpy(u, &ins, sizeof(ins));
        list_addtail(&u->link, &ctx->current_block->base.instructions);
        return u;
}

static inline bi_instruction *
bi_emit_before(bi_context *ctx, bi_instruction *tag, bi_instruction ins)
{
        bi_instruction *u = rzalloc(ctx, bi_instruction);
        memcpy(u, &ins, sizeof(ins));
        list_addtail(&u->link, &tag->link);
        return u;
}

static inline void
bi_remove_instruction(bi_instruction *ins)
{
        list_del(&ins->link);
}

/* If high bits are set, instead of SSA/registers, we have specials indexed by
 * the low bits if necessary.
 *
 *  Fixed register: do not allocate register, do not collect $200.
 *  Uniform: access a uniform register given by low bits.
 *  Constant: access the specified constant (specifies a bit offset / shift)
 *  Zero: special cased to avoid wasting a constant
 *  Passthrough: a bifrost_packed_src to passthrough T/T0/T1
 */

#define BIR_INDEX_REGISTER (1 << 31)
#define BIR_INDEX_UNIFORM  (1 << 30)
#define BIR_INDEX_CONSTANT (1 << 29)
#define BIR_INDEX_ZERO     (1 << 28)
#define BIR_INDEX_PASS     (1 << 27)

/* Keep me synced please so we can check src & BIR_SPECIAL */

#define BIR_SPECIAL        ((BIR_INDEX_REGISTER | BIR_INDEX_UNIFORM) | \
        (BIR_INDEX_CONSTANT | BIR_INDEX_ZERO | BIR_INDEX_PASS))

static inline unsigned
bi_max_temp(bi_context *ctx)
{
        unsigned alloc = MAX2(ctx->impl->reg_alloc, ctx->impl->ssa_alloc);
        return ((alloc + 2 + ctx->temp_alloc) << 1);
}

static inline unsigned
bi_make_temp(bi_context *ctx)
{
        return (ctx->impl->ssa_alloc + 1 + ctx->temp_alloc++) << 1;
}

static inline unsigned
bi_make_temp_reg(bi_context *ctx)
{
        return ((ctx->impl->reg_alloc + ctx->temp_alloc++) << 1) | PAN_IS_REG;
}

/* Iterators for Bifrost IR */

#define bi_foreach_block(ctx, v) \
        list_for_each_entry(pan_block, v, &ctx->blocks, link)

#define bi_foreach_block_from(ctx, from, v) \
        list_for_each_entry_from(pan_block, v, from, &ctx->blocks, link)

#define bi_foreach_block_from_rev(ctx, from, v) \
        list_for_each_entry_from_rev(pan_block, v, from, &ctx->blocks, link)

#define bi_foreach_instr_in_block(block, v) \
        list_for_each_entry(bi_instruction, v, &(block)->base.instructions, link)

#define bi_foreach_instr_in_block_rev(block, v) \
        list_for_each_entry_rev(bi_instruction, v, &(block)->base.instructions, link)

#define bi_foreach_instr_in_block_safe(block, v) \
        list_for_each_entry_safe(bi_instruction, v, &(block)->base.instructions, link)

#define bi_foreach_instr_in_block_safe_rev(block, v) \
        list_for_each_entry_safe_rev(bi_instruction, v, &(block)->base.instructions, link)

#define bi_foreach_instr_in_block_from(block, v, from) \
        list_for_each_entry_from(bi_instruction, v, from, &(block)->base.instructions, link)

#define bi_foreach_instr_in_block_from_rev(block, v, from) \
        list_for_each_entry_from_rev(bi_instruction, v, from, &(block)->base.instructions, link)

#define bi_foreach_clause_in_block(block, v) \
        list_for_each_entry(bi_clause, v, &(block)->clauses, link)

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

static inline bi_instruction *
bi_prev_op(bi_instruction *ins)
{
        return list_last_entry(&(ins->link), bi_instruction, link);
}

static inline bi_instruction *
bi_next_op(bi_instruction *ins)
{
        return list_first_entry(&(ins->link), bi_instruction, link);
}

static inline pan_block *
pan_next_block(pan_block *block)
{
        return list_first_entry(&(block->link), pan_block, link);
}

/* Special functions */

void bi_emit_fexp2(bi_context *ctx, nir_alu_instr *instr);
void bi_emit_flog2(bi_context *ctx, nir_alu_instr *instr);

/* BIR manipulation */

bool bi_has_outmod(bi_instruction *ins);
bool bi_has_source_mods(bi_instruction *ins);
bool bi_is_src_swizzled(bi_instruction *ins, unsigned s);
bool bi_has_arg(bi_instruction *ins, unsigned arg);
uint16_t bi_from_bytemask(uint16_t bytemask, unsigned bytes);
unsigned bi_get_component_count(bi_instruction *ins, signed s);
uint16_t bi_bytemask_of_read_components(bi_instruction *ins, unsigned node);
uint64_t bi_get_immediate(bi_instruction *ins, unsigned index);
bool bi_writes_component(bi_instruction *ins, unsigned comp);
unsigned bi_writemask(bi_instruction *ins);

/* BIR passes */

void bi_lower_combine(bi_context *ctx, bi_block *block);
bool bi_opt_dead_code_eliminate(bi_context *ctx, bi_block *block);
void bi_schedule(bi_context *ctx);
void bi_register_allocate(bi_context *ctx);

/* Liveness */

void bi_compute_liveness(bi_context *ctx);
void bi_liveness_ins_update(uint16_t *live, bi_instruction *ins, unsigned max);
void bi_invalidate_liveness(bi_context *ctx);
bool bi_is_live_after(bi_context *ctx, bi_block *block, bi_instruction *start, int src);

/* Layout */

bool bi_can_insert_bundle(bi_clause *clause, bool constant);
unsigned bi_clause_quadwords(bi_clause *clause);
signed bi_block_offset(bi_context *ctx, bi_clause *start, bi_block *target);

/* Code emit */

void bi_pack(bi_context *ctx, struct util_dynarray *emission);

#endif
