/*
 * Copyright (C) 2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

#ifndef _MDG_COMPILER_H
#define _MDG_COMPILER_H

#include "midgard.h"
#include "helpers.h"
#include "midgard_compile.h"

#include "util/hash_table.h"
#include "util/u_dynarray.h"
#include "util/set.h"
#include "util/list.h"

#include "main/mtypes.h"
#include "compiler/nir_types.h"
#include "compiler/nir/nir.h"

/* Forward declare */
struct midgard_block;

/* Target types. Defaults to TARGET_GOTO (the type corresponding directly to
 * the hardware), hence why that must be zero. TARGET_DISCARD signals this
 * instruction is actually a discard op. */

#define TARGET_GOTO 0
#define TARGET_BREAK 1
#define TARGET_CONTINUE 2
#define TARGET_DISCARD 3

typedef struct midgard_branch {
        /* If conditional, the condition is specified in r31.w */
        bool conditional;

        /* For conditionals, if this is true, we branch on FALSE. If false, we  branch on TRUE. */
        bool invert_conditional;

        /* Branch targets: the start of a block, the start of a loop (continue), the end of a loop (break). Value is one of TARGET_ */
        unsigned target_type;

        /* The actual target */
        union {
                int target_block;
                int target_break;
                int target_continue;
        };
} midgard_branch;

/* Generic in-memory data type repesenting a single logical instruction, rather
 * than a single instruction group. This is the preferred form for code gen.
 * Multiple midgard_insturctions will later be combined during scheduling,
 * though this is not represented in this structure.  Its format bridges
 * the low-level binary representation with the higher level semantic meaning.
 *
 * Notably, it allows registers to be specified as block local SSA, for code
 * emitted before the register allocation pass.
 */

typedef struct midgard_instruction {
        /* Must be first for casting */
        struct list_head link;

        unsigned type; /* ALU, load/store, texture */

        /* Instruction arguments represented as block-local SSA
         * indices, rather than registers. ~0 means unused. */
        unsigned src[3];
        unsigned dest;

        /* Swizzle for the conditional for a csel */
        unsigned csel_swizzle;

        /* Special fields for an ALU instruction */
        midgard_reg_info registers;

        /* I.e. (1 << alu_bit) */
        int unit;

        /* When emitting bundle, should this instruction have a break forced
         * before it? Used for r31 writes which are valid only within a single
         * bundle and *need* to happen as early as possible... this is a hack,
         * TODO remove when we have a scheduler */
        bool precede_break;

        bool has_constants;
        uint32_t constants[4];
        uint16_t inline_constant;
        bool has_blend_constant;
        bool has_inline_constant;

        bool compact_branch;
        bool writeout;
        bool prepacked_branch;

        /* Kind of a hack, but hint against aggressive DCE */
        bool dont_eliminate;

        /* Masks in a saneish format. One bit per channel, not packed fancy.
         * Use this instead of the op specific ones, and switch over at emit
         * time */

        uint16_t mask;

        /* For ALU ops only: set to true to invert (bitwise NOT) the
         * destination of an integer-out op. Not imeplemented in hardware but
         * allows more optimizations */

        bool invert;

        /* Hint for the register allocator not to spill the destination written
         * from this instruction (because it is a spill/unspill node itself) */

        bool no_spill;

        /* Generic hint for intra-pass use */
        bool hint;

        union {
                midgard_load_store_word load_store;
                midgard_vector_alu alu;
                midgard_texture_word texture;
                midgard_branch_extended branch_extended;
                uint16_t br_compact;

                /* General branch, rather than packed br_compact. Higher level
                 * than the other components */
                midgard_branch branch;
        };
} midgard_instruction;

typedef struct midgard_block {
        /* Link to next block. Must be first for mir_get_block */
        struct list_head link;

        /* List of midgard_instructions emitted for the current block */
        struct list_head instructions;

        /* Index of the block in source order */
        unsigned source_id;

        bool is_scheduled;

        /* List of midgard_bundles emitted (after the scheduler has run) */
        struct util_dynarray bundles;

        /* Number of quadwords _actually_ emitted, as determined after scheduling */
        unsigned quadword_count;

        /* Succeeding blocks. The compiler should not necessarily rely on
         * source-order traversal */
        struct midgard_block *successors[2];
        unsigned nr_successors;

        struct set *predecessors;

        /* The successors pointer form a graph, and in the case of
         * complex control flow, this graph has a cycles. To aid
         * traversal during liveness analysis, we have a visited?
         * boolean for passes to use as they see fit, provided they
         * clean up later */
        bool visited;

        /* In liveness analysis, these are live masks (per-component) for
         * indices for the block. Scalar compilers have the luxury of using
         * simple bit fields, but for us, liveness is a vector idea. We use
         * 8-bit to allow finegrained tracking up to vec8. If you're
         * implementing vec16 on Panfrost... I'm sorry. */
        uint8_t *live_in;
        uint8_t *live_out;
} midgard_block;

typedef struct midgard_bundle {
        /* Tag for the overall bundle */
        int tag;

        /* Instructions contained by the bundle */
        int instruction_count;
        midgard_instruction *instructions[5];

        /* Bundle-wide ALU configuration */
        int padding;
        int control;
        bool has_embedded_constants;
        float constants[4];
        bool has_blend_constant;
} midgard_bundle;

typedef struct compiler_context {
        nir_shader *nir;
        gl_shader_stage stage;

        /* The screen we correspond to */
        struct midgard_screen *screen;

        /* Is internally a blend shader? Depends on stage == FRAGMENT */
        bool is_blend;

        /* Tracking for blend constant patching */
        int blend_constant_offset;

        /* Number of bytes used for Thread Local Storage */
        unsigned tls_size;

        /* Count of spills and fills for shaderdb */
        unsigned spills;
        unsigned fills;

        /* Current NIR function */
        nir_function *func;

        /* Allocated compiler temporary counter */
        unsigned temp_alloc;

        /* Unordered list of midgard_blocks */
        int block_count;
        struct list_head blocks;

        /* TODO merge with block_count? */
        unsigned block_source_count;

        /* List of midgard_instructions emitted for the current block */
        midgard_block *current_block;

        /* If there is a preset after block, use this, otherwise emit_block will create one if NULL */
        midgard_block *after_block;

        /* The current "depth" of the loop, for disambiguating breaks/continues
         * when using nested loops */
        int current_loop_depth;

        /* Total number of loops for shader-db */
        unsigned loop_count;

        /* Constants which have been loaded, for later inlining */
        struct hash_table_u64 *ssa_constants;

        /* Mapping of hashes computed from NIR indices to the sequential temp indices ultimately used in MIR */
        struct hash_table_u64 *hash_to_temp;
        int temp_count;
        int max_hash;

        /* Just the count of the max register used. Higher count => higher
         * register pressure */
        int work_registers;

        /* Used for cont/last hinting. Increase when a tex op is added.
         * Decrease when a tex op is removed. */
        int texture_op_count;

        /* The number of uniforms allowable for the fast path */
        int uniform_cutoff;

        /* Count of instructions emitted from NIR overall, across all blocks */
        int instruction_count;

        /* Alpha ref value passed in */
        float alpha_ref;

        unsigned quadword_count;

        /* The mapping of sysvals to uniforms, the count, and the off-by-one inverse */
        unsigned sysvals[MAX_SYSVAL_COUNT];
        unsigned sysval_count;
        struct hash_table_u64 *sysval_to_id;
} compiler_context;

/* Helpers for manipulating the above structures (forming the driver IR) */

/* Append instruction to end of current block */

static inline midgard_instruction *
mir_upload_ins(struct compiler_context *ctx, struct midgard_instruction ins)
{
        midgard_instruction *heap = ralloc(ctx, struct midgard_instruction);
        memcpy(heap, &ins, sizeof(ins));
        return heap;
}

static inline midgard_instruction *
emit_mir_instruction(struct compiler_context *ctx, struct midgard_instruction ins)
{
        midgard_instruction *u = mir_upload_ins(ctx, ins);
        list_addtail(&u->link, &ctx->current_block->instructions);
        return u;
}

static inline struct midgard_instruction *
mir_insert_instruction_before(struct compiler_context *ctx,
                              struct midgard_instruction *tag,
                              struct midgard_instruction ins)
{
        struct midgard_instruction *u = mir_upload_ins(ctx, ins);
        list_addtail(&u->link, &tag->link);
        return u;
}

static inline void
mir_remove_instruction(struct midgard_instruction *ins)
{
        list_del(&ins->link);
}

static inline midgard_instruction*
mir_prev_op(struct midgard_instruction *ins)
{
        return list_last_entry(&(ins->link), midgard_instruction, link);
}

static inline midgard_instruction*
mir_next_op(struct midgard_instruction *ins)
{
        return list_first_entry(&(ins->link), midgard_instruction, link);
}

#define mir_foreach_block(ctx, v) \
        list_for_each_entry(struct midgard_block, v, &ctx->blocks, link)

#define mir_foreach_block_from(ctx, from, v) \
        list_for_each_entry_from(struct midgard_block, v, from, &ctx->blocks, link)

#define mir_foreach_instr(ctx, v) \
        list_for_each_entry(struct midgard_instruction, v, &ctx->current_block->instructions, link)

#define mir_foreach_instr_safe(ctx, v) \
        list_for_each_entry_safe(struct midgard_instruction, v, &ctx->current_block->instructions, link)

#define mir_foreach_instr_in_block(block, v) \
        list_for_each_entry(struct midgard_instruction, v, &block->instructions, link)
#define mir_foreach_instr_in_block_rev(block, v) \
        list_for_each_entry_rev(struct midgard_instruction, v, &block->instructions, link)

#define mir_foreach_instr_in_block_safe(block, v) \
        list_for_each_entry_safe(struct midgard_instruction, v, &block->instructions, link)

#define mir_foreach_instr_in_block_safe_rev(block, v) \
        list_for_each_entry_safe_rev(struct midgard_instruction, v, &block->instructions, link)

#define mir_foreach_instr_in_block_from(block, v, from) \
        list_for_each_entry_from(struct midgard_instruction, v, from, &block->instructions, link)

#define mir_foreach_instr_in_block_from_rev(block, v, from) \
        list_for_each_entry_from_rev(struct midgard_instruction, v, from, &block->instructions, link)

#define mir_foreach_bundle_in_block(block, v) \
        util_dynarray_foreach(&block->bundles, midgard_bundle, v)

#define mir_foreach_bundle_in_block_rev(block, v) \
        util_dynarray_foreach_reverse(&block->bundles, midgard_bundle, v)

#define mir_foreach_instr_in_block_scheduled_rev(block, v) \
        midgard_instruction* v; \
        signed i = 0; \
        mir_foreach_bundle_in_block_rev(block, _bundle) \
                for (i = (_bundle->instruction_count - 1), v = _bundle->instructions[i]; \
                                i >= 0; \
                                --i, v = _bundle->instructions[i]) \

#define mir_foreach_instr_global(ctx, v) \
        mir_foreach_block(ctx, v_block) \
                mir_foreach_instr_in_block(v_block, v)

#define mir_foreach_instr_global_safe(ctx, v) \
        mir_foreach_block(ctx, v_block) \
                mir_foreach_instr_in_block_safe(v_block, v)

#define mir_foreach_successor(blk, v) \
        struct midgard_block *v; \
        struct midgard_block **_v; \
        for (_v = &blk->successors[0], \
                v = *_v; \
                v != NULL && _v < &blk->successors[2]; \
                _v++, v = *_v) \

/* Based on set_foreach, expanded with automatic type casts */

#define mir_foreach_predecessor(blk, v) \
        struct set_entry *_entry_##v; \
        struct midgard_block *v; \
        for (_entry_##v = _mesa_set_next_entry(blk->predecessors, NULL), \
                v = (struct midgard_block *) (_entry_##v ? _entry_##v->key : NULL);  \
                _entry_##v != NULL; \
                _entry_##v = _mesa_set_next_entry(blk->predecessors, _entry_##v), \
                v = (struct midgard_block *) (_entry_##v ? _entry_##v->key : NULL))

#define mir_foreach_src(ins, v) \
        for (unsigned v = 0; v < ARRAY_SIZE(ins->src); ++v)

static inline midgard_instruction *
mir_last_in_block(struct midgard_block *block)
{
        return list_last_entry(&block->instructions, struct midgard_instruction, link);
}

static inline midgard_block *
mir_get_block(compiler_context *ctx, int idx)
{
        struct list_head *lst = &ctx->blocks;

        while ((idx--) + 1)
                lst = lst->next;

        return (struct midgard_block *) lst;
}

static inline midgard_block *
mir_exit_block(struct compiler_context *ctx)
{
        midgard_block *last = list_last_entry(&ctx->blocks,
                        struct midgard_block, link);

        /* The last block must be empty logically but contains branch writeout
         * for fragment shaders */

        assert(last->nr_successors == 0);

        return last;
}

static inline bool
mir_is_alu_bundle(midgard_bundle *bundle)
{
        return IS_ALU(bundle->tag);
}

/* Registers/SSA are distinguish in the backend by the bottom-most bit */

#define IS_REG (1)

static inline unsigned
make_compiler_temp(compiler_context *ctx)
{
        return (ctx->func->impl->ssa_alloc + ctx->temp_alloc++) << 1;
}

static inline unsigned
make_compiler_temp_reg(compiler_context *ctx)
{
        return ((ctx->func->impl->reg_alloc + ctx->temp_alloc++) << 1) | IS_REG;
}

static inline unsigned
nir_src_index(compiler_context *ctx, nir_src *src)
{
        if (src->is_ssa)
                return (src->ssa->index << 1) | 0;
        else {
                assert(!src->reg.indirect);
                return (src->reg.reg->index << 1) | IS_REG;
        }
}

static inline unsigned
nir_alu_src_index(compiler_context *ctx, nir_alu_src *src)
{
        return nir_src_index(ctx, &src->src);
}

static inline unsigned
nir_dest_index(compiler_context *ctx, nir_dest *dst)
{
        if (dst->is_ssa)
                return (dst->ssa.index << 1) | 0;
        else {
                assert(!dst->reg.indirect);
                return (dst->reg.reg->index << 1) | IS_REG;
        }
}



/* MIR manipulation */

unsigned mir_get_swizzle(midgard_instruction *ins, unsigned idx);
void mir_set_swizzle(midgard_instruction *ins, unsigned idx, unsigned new);
void mir_rewrite_index(compiler_context *ctx, unsigned old, unsigned new);
void mir_rewrite_index_src(compiler_context *ctx, unsigned old, unsigned new);
void mir_rewrite_index_dst(compiler_context *ctx, unsigned old, unsigned new);
void mir_rewrite_index_dst_single(midgard_instruction *ins, unsigned old, unsigned new);
void mir_rewrite_index_src_single(midgard_instruction *ins, unsigned old, unsigned new);
void mir_rewrite_index_src_swizzle(compiler_context *ctx, unsigned old, unsigned new, unsigned swizzle);
bool mir_single_use(compiler_context *ctx, unsigned value);
bool mir_special_index(compiler_context *ctx, unsigned idx);
unsigned mir_use_count(compiler_context *ctx, unsigned value);
bool mir_is_written_before(compiler_context *ctx, midgard_instruction *ins, unsigned node);
unsigned mir_mask_of_read_components(midgard_instruction *ins, unsigned node);
unsigned mir_ubo_shift(midgard_load_store_op op);

/* MIR printing */

void mir_print_instruction(midgard_instruction *ins);
void mir_print_bundle(midgard_bundle *ctx);
void mir_print_block(midgard_block *block);
void mir_print_shader(compiler_context *ctx);
bool mir_nontrivial_source2_mod(midgard_instruction *ins);
bool mir_nontrivial_source2_mod_simple(midgard_instruction *ins);
bool mir_nontrivial_mod(midgard_vector_alu_src src, bool is_int, unsigned mask);
bool mir_nontrivial_outmod(midgard_instruction *ins);

void mir_insert_instruction_before_scheduled(compiler_context *ctx, midgard_block *block, midgard_instruction *tag, midgard_instruction ins);
void mir_insert_instruction_after_scheduled(compiler_context *ctx, midgard_block *block, midgard_instruction *tag, midgard_instruction ins);

/* MIR goodies */

static const midgard_vector_alu_src blank_alu_src = {
        .swizzle = SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_Z, COMPONENT_W),
};

static const midgard_vector_alu_src blank_alu_src_xxxx = {
        .swizzle = SWIZZLE(COMPONENT_X, COMPONENT_X, COMPONENT_X, COMPONENT_X),
};

static const midgard_scalar_alu_src blank_scalar_alu_src = {
        .full = true
};

/* Used for encoding the unused source of 1-op instructions */
static const midgard_vector_alu_src zero_alu_src = { 0 };

/* 'Intrinsic' move for aliasing */

static inline midgard_instruction
v_mov(unsigned src, midgard_vector_alu_src mod, unsigned dest)
{
        midgard_instruction ins = {
                .type = TAG_ALU_4,
                .mask = 0xF,
                .src = { SSA_UNUSED, src, SSA_UNUSED },
                .dest = dest,
                .alu = {
                        .op = midgard_alu_op_imov,
                        .reg_mode = midgard_reg_mode_32,
                        .dest_override = midgard_dest_override_none,
                        .outmod = midgard_outmod_int_wrap,
                        .src1 = vector_alu_srco_unsigned(zero_alu_src),
                        .src2 = vector_alu_srco_unsigned(mod)
                },
        };

        return ins;
}

static inline bool
mir_has_arg(midgard_instruction *ins, unsigned arg)
{
        if (!ins)
                return false;

        for (unsigned i = 0; i < ARRAY_SIZE(ins->src); ++i) {
                if (ins->src[i] == arg)
                        return true;
        }

        return false;
}

/* Scheduling */

void schedule_program(compiler_context *ctx);

/* Register allocation */

struct ra_graph;

/* Broad types of register classes so we can handle special
 * registers */

#define NR_REG_CLASSES 6

#define REG_CLASS_WORK          0
#define REG_CLASS_LDST          1
#define REG_CLASS_LDST27        2
#define REG_CLASS_TEXR          3
#define REG_CLASS_TEXW          4
#define REG_CLASS_FRAGC         5

void mir_lower_special_reads(compiler_context *ctx);
struct ra_graph* allocate_registers(compiler_context *ctx, bool *spilled);
void install_registers(compiler_context *ctx, struct ra_graph *g);
bool mir_is_live_after(compiler_context *ctx, midgard_block *block, midgard_instruction *start, int src);
bool mir_has_multiple_writes(compiler_context *ctx, int src);

void mir_create_pipeline_registers(compiler_context *ctx);

void
midgard_promote_uniforms(compiler_context *ctx, unsigned promoted_count);

midgard_instruction *
emit_ubo_read(
        compiler_context *ctx,
        nir_instr *instr,
        unsigned dest,
        unsigned offset,
        nir_src *indirect_offset,
        unsigned index);

void
emit_sysval_read(compiler_context *ctx, nir_instr *instr, signed dest_override, unsigned nr_components);

void
midgard_emit_derivatives(compiler_context *ctx, nir_alu_instr *instr);

void
midgard_lower_derivatives(compiler_context *ctx, midgard_block *block);

bool mir_op_computes_derivatives(unsigned op);

/* Final emission */

void emit_binary_bundle(
        compiler_context *ctx,
        midgard_bundle *bundle,
        struct util_dynarray *emission,
        int next_tag);

bool
nir_undef_to_zero(nir_shader *shader);

/* Optimizations */

bool midgard_opt_copy_prop(compiler_context *ctx, midgard_block *block);
bool midgard_opt_combine_projection(compiler_context *ctx, midgard_block *block);
bool midgard_opt_varying_projection(compiler_context *ctx, midgard_block *block);
bool midgard_opt_dead_code_eliminate(compiler_context *ctx, midgard_block *block);
bool midgard_opt_dead_move_eliminate(compiler_context *ctx, midgard_block *block);

void midgard_lower_invert(compiler_context *ctx, midgard_block *block);
bool midgard_opt_not_propagate(compiler_context *ctx, midgard_block *block);
bool midgard_opt_fuse_src_invert(compiler_context *ctx, midgard_block *block);
bool midgard_opt_fuse_dest_invert(compiler_context *ctx, midgard_block *block);
bool midgard_opt_promote_fmov(compiler_context *ctx, midgard_block *block);

#endif
