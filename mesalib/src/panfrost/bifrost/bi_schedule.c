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
#include "bi_builder.h"

/* Arguments common to worklist, passed by value for convenience */

struct bi_worklist {
        /* # of instructions in the block */
        unsigned count;

        /* Instructions in the block */
        bi_instr **instructions;

        /* Bitset of instructions in the block ready for scheduling */
        BITSET_WORD *worklist;
};

/* State of a single tuple and clause under construction */

struct bi_reg_state {
        /* Number of register writes */
        unsigned nr_writes;

        /* Register reads, expressed as (equivalence classes of)
         * sources. Only 3 reads are allowed, but up to 2 may spill as
         * "forced" for the next scheduled tuple, provided such a tuple
         * can be constructed */
        bi_index reads[5];
        unsigned nr_reads;

        /* The previous tuple scheduled (= the next tuple executed in the
         * program) may require certain writes, in order to bypass the register
         * file and use a temporary passthrough for the value. Up to 2 such
         * constraints are architecturally satisfiable */
        unsigned forced_count;
        bi_index forceds[2];
};

struct bi_tuple_state {
        /* Is this the last tuple in the clause */
        bool last;

        /* Scheduled ADD instruction, or null if none */
        bi_instr *add;

        /* Reads for previous (succeeding) tuple */
        bi_index prev_reads[5];
        unsigned nr_prev_reads;
        bi_tuple *prev;

        /* Register slot state for current tuple */
        struct bi_reg_state reg;

        /* Constants are shared in the tuple. If constant_count is nonzero, it
         * is a size for constant count. Otherwise, fau is the slot read from
         * FAU, or zero if none is assigned. Ordinarily FAU slot 0 reads zero,
         * but within a tuple, that should be encoded as constant_count != 0
         * and constants[0] = constants[1] = 0 */
        unsigned constant_count;

        union {
                uint32_t constants[2];
                enum bir_fau fau;
        };

        unsigned pcrel_idx;
};

struct bi_const_state {
        unsigned constant_count;
        bool pcrel; /* applies to first const */
        uint32_t constants[2];

        /* Index of the constant into the clause */
        unsigned word_idx;
};

struct bi_clause_state {
        /* Has a message-passing instruction already been assigned? */
        bool message;

        /* Indices already read, this needs to be tracked to avoid hazards
         * around message-passing instructions */
        unsigned read_count;
        bi_index reads[BI_MAX_SRCS * 16];

        unsigned tuple_count;
        struct bi_const_state consts[8];
};

/* Determines messsage type by checking the table and a few special cases. Only
 * case missing is tilebuffer instructions that access depth/stencil, which
 * require a Z_STENCIL message (to implement
 * ARM_shader_framebuffer_fetch_depth_stencil) */

static enum bifrost_message_type
bi_message_type_for_instr(bi_instr *ins)
{
        enum bifrost_message_type msg = bi_opcode_props[ins->op].message;
        bool ld_var_special = (ins->op == BI_OPCODE_LD_VAR_SPECIAL);

        if (ld_var_special && ins->varying_name == BI_VARYING_NAME_FRAG_Z)
                return BIFROST_MESSAGE_Z_STENCIL;

        if (msg == BIFROST_MESSAGE_LOAD && ins->seg == BI_SEG_UBO)
                return BIFROST_MESSAGE_ATTRIBUTE;

        return msg;
}

/* Attribute, texture, and UBO load (attribute message) instructions support
 * bindless, so just check the message type */

ASSERTED static bool
bi_supports_dtsel(bi_instr *ins)
{
        switch (bi_message_type_for_instr(ins)) {
        case BIFROST_MESSAGE_ATTRIBUTE:
                return ins->op != BI_OPCODE_LD_GCLK_U64;
        case BIFROST_MESSAGE_TEX:
                return true;
        default:
                return false;
        }
}

/* Scheduler pseudoinstruction lowerings to enable instruction pairings.
 * Currently only support CUBEFACE -> *CUBEFACE1/+CUBEFACE2
 */

static bi_instr *
bi_lower_cubeface(bi_context *ctx,
                struct bi_clause_state *clause, struct bi_tuple_state *tuple)
{
        bi_instr *pinstr = tuple->add;
        bi_builder b = bi_init_builder(ctx, bi_before_instr(pinstr));
        bi_instr *cubeface1 = bi_cubeface1_to(&b, pinstr->dest[0],
                        pinstr->src[0], pinstr->src[1], pinstr->src[2]);

        pinstr->op = BI_OPCODE_CUBEFACE2;
        pinstr->dest[0] = pinstr->dest[1];
        pinstr->dest[1] = bi_null();
        pinstr->src[0] = cubeface1->dest[0];
        pinstr->src[1] = bi_null();
        pinstr->src[2] = bi_null();

        return cubeface1;
}

/* Psuedo arguments are (rbase, address lo, address hi). We need *ATOM_C.i32 to
 * have the arguments (address lo, address hi, rbase), and +ATOM_CX to have the
 * arguments (rbase, address lo, address hi, rbase) */

static bi_instr *
bi_lower_atom_c(bi_context *ctx, struct bi_clause_state *clause, struct
                bi_tuple_state *tuple)
{
        bi_instr *pinstr = tuple->add;
        bi_builder b = bi_init_builder(ctx, bi_before_instr(pinstr));
        bi_instr *atom_c = bi_atom_c_return_i32(&b, 
                        pinstr->src[1], pinstr->src[2], pinstr->src[0],
                        pinstr->atom_opc);

        if (bi_is_null(pinstr->dest[0]))
                atom_c->op = BI_OPCODE_ATOM_C_I32;

        pinstr->op = BI_OPCODE_ATOM_CX;
        pinstr->src[3] = atom_c->src[2];

        return atom_c;
}

static bi_instr *
bi_lower_atom_c1(bi_context *ctx, struct bi_clause_state *clause, struct
                bi_tuple_state *tuple)
{
        bi_instr *pinstr = tuple->add;
        bi_builder b = bi_init_builder(ctx, bi_before_instr(pinstr));
        bi_instr *atom_c = bi_atom_c1_return_i32(&b,
                        pinstr->src[0], pinstr->src[1], pinstr->atom_opc);

        if (bi_is_null(pinstr->dest[0]))
                atom_c->op = BI_OPCODE_ATOM_C1_I32;

        pinstr->op = BI_OPCODE_ATOM_CX;
        pinstr->src[2] = pinstr->src[1];
        pinstr->src[1] = pinstr->src[0];
        pinstr->src[3] = bi_dontcare();
        pinstr->src[0] = bi_null();

        return atom_c;
}

static bi_instr *
bi_lower_seg_add(bi_context *ctx,
                struct bi_clause_state *clause, struct bi_tuple_state *tuple)
{
        bi_instr *pinstr = tuple->add;
        bi_builder b = bi_init_builder(ctx, bi_before_instr(pinstr));

        bi_instr *fma = bi_seg_add_to(&b, bi_word(pinstr->dest[0], 0),
                        pinstr->src[0], pinstr->preserve_null, pinstr->seg);

        pinstr->op = BI_OPCODE_SEG_ADD;
        pinstr->dest[0] = bi_word(pinstr->dest[0], 1);
        pinstr->src[0] = pinstr->src[1];
        pinstr->src[1] = bi_null();

        return fma;
}

static bi_instr *
bi_lower_dtsel(bi_context *ctx,
                struct bi_clause_state *clause, struct bi_tuple_state *tuple)
{
        bi_instr *add = tuple->add;
        bi_builder b = bi_init_builder(ctx, bi_before_instr(add));

        bi_instr *dtsel = bi_dtsel_imm_to(&b, bi_temp(b.shader),
                        add->src[0], add->table);
        add->src[0] = dtsel->dest[0];

        assert(bi_supports_dtsel(add));
        return dtsel;
}

/* Flatten linked list to array for O(1) indexing */

static bi_instr **
bi_flatten_block(bi_block *block, unsigned *len)
{
        if (list_is_empty(&block->base.instructions))
                return NULL;

        *len = list_length(&block->base.instructions);
        bi_instr **instructions = malloc(sizeof(bi_instr *) * (*len));

        unsigned i = 0;

        bi_foreach_instr_in_block(block, ins)
                instructions[i++] = ins;

        return instructions;
}

/* The worklist would track instructions without outstanding dependencies. For
 * debug, force in-order scheduling (no dependency graph is constructed).
 */

static struct bi_worklist
bi_initialize_worklist(bi_block *block)
{
        struct bi_worklist st = { };
        st.instructions = bi_flatten_block(block, &st.count);

        if (st.count) {
                st.worklist = calloc(BITSET_WORDS(st.count), sizeof(BITSET_WORD));
                BITSET_SET(st.worklist, st.count - 1);
        }

        return st;
}

static void
bi_free_worklist(struct bi_worklist st)
{
        free(st.instructions);
        free(st.worklist);
}

static void
bi_update_worklist(struct bi_worklist st, unsigned idx)
{
        if (idx >= 1)
                BITSET_SET(st.worklist, idx - 1);
}

/* To work out the back-to-back flag, we need to detect branches and
 * "fallthrough" branches, implied in the last clause of a block that falls
 * through to another block with *multiple predecessors*. */

static bool
bi_back_to_back(bi_block *block)
{
        /* Last block of a program */
        if (!block->base.successors[0]) {
                assert(!block->base.successors[1]);
                return false;
        }

        /* Multiple successors? We're branching */
        if (block->base.successors[1])
                return false;

        struct pan_block *succ = block->base.successors[0];
        assert(succ->predecessors);
        unsigned count = succ->predecessors->entries;

        /* Back to back only if the successor has only a single predecessor */
        return (count == 1);
}

/* Insert a clause wrapping a single instruction */

bi_clause *
bi_singleton(void *memctx, bi_instr *ins,
                bi_block *block,
                unsigned scoreboard_id,
                unsigned dependencies,
                uint64_t combined_constant,
                bool osrb)
{
        bi_clause *u = rzalloc(memctx, bi_clause);
        u->tuple_count = 1;

        ASSERTED bool can_fma = bi_opcode_props[ins->op].fma;
        bool can_add = bi_opcode_props[ins->op].add;
        assert(can_fma || can_add);

        if (can_add)
                u->tuples[0].add = ins;
        else
                u->tuples[0].fma = ins;

        u->scoreboard_id = scoreboard_id;
        u->staging_barrier = osrb;
        u->dependencies = dependencies;

        if (ins->op == BI_OPCODE_ATEST)
                u->dependencies |= (1 << 6);

        if (ins->op == BI_OPCODE_BLEND)
                u->dependencies |= (1 << 6) | (1 << 7);

        /* Let's be optimistic, we'll fix up later */
        u->flow_control = BIFROST_FLOW_NBTB;

        assert(!ins->branch_target);

        if (combined_constant) {
                /* Clause in 64-bit, above in 32-bit */
                u->constant_count = 1;
                u->constants[0] = combined_constant;
                u->tuples[0].fau_idx = bi_constant_field(0) |
                        (combined_constant & 0xF);
        }

        u->next_clause_prefetch = (ins->op != BI_OPCODE_JUMP);
        u->message_type = bi_message_type_for_instr(ins);
        u->message = u->message_type ? ins : NULL;
        u->block = block;

        return u;
}

/* Scheduler predicates */

ASSERTED static bool
bi_can_fma(bi_instr *ins)
{
        /* Errata: *V2F32_TO_V2F16 with distinct sources raises
         * INSTR_INVALID_ENC under certain conditions */
        if (ins->op == BI_OPCODE_V2F32_TO_V2F16 &&
                        !bi_is_word_equiv(ins->src[0], ins->src[1]))
                return false;

        /* TODO: some additional fp16 constraints */
        return bi_opcode_props[ins->op].fma;
}

ASSERTED static bool
bi_can_add(bi_instr *ins)
{
        /* +FADD.v2f16 lacks clamp modifier, use *FADD.v2f16 instead */
        if (ins->op == BI_OPCODE_FADD_V2F16 && ins->clamp)
                return false;

        /* TODO: some additional fp16 constraints */
        return bi_opcode_props[ins->op].add;
}

ASSERTED static bool
bi_must_last(bi_instr *ins)
{
        return bi_opcode_props[ins->op].last;
}

/* Architecturally, no single instruction has a "not last" constraint. However,
 * pseudoinstructions writing multiple destinations (expanding to multiple
 * paired instructions) can run afoul of the "no two writes on the last clause"
 * constraint, so we check for that here.
 */

static bool
bi_must_not_last(bi_instr *ins)
{
        return !bi_is_null(ins->dest[0]) && !bi_is_null(ins->dest[1]);
}

/* Check for a message-passing instruction. +DISCARD.f32 is special-cased; we
 * treat it as a message-passing instruction for the purpose of scheduling
 * despite no passing no logical message. Otherwise invalid encoding faults may
 * be raised for unknown reasons (possibly an errata).
 */

ASSERTED static bool
bi_must_message(bi_instr *ins)
{
        return (bi_opcode_props[ins->op].message != BIFROST_MESSAGE_NONE) ||
                (ins->op == BI_OPCODE_DISCARD_F32);
}

static bool
bi_fma_atomic(enum bi_opcode op)
{
        switch (op) {
        case BI_OPCODE_ATOM_C_I32:
        case BI_OPCODE_ATOM_C_I64:
        case BI_OPCODE_ATOM_C1_I32:
        case BI_OPCODE_ATOM_C1_I64:
        case BI_OPCODE_ATOM_C1_RETURN_I32:
        case BI_OPCODE_ATOM_C1_RETURN_I64:
        case BI_OPCODE_ATOM_C_RETURN_I32:
        case BI_OPCODE_ATOM_C_RETURN_I64:
        case BI_OPCODE_ATOM_POST_I32:
        case BI_OPCODE_ATOM_POST_I64:
        case BI_OPCODE_ATOM_PRE_I64:
                return true;
        default:
                return false;
        }
}

ASSERTED static bool
bi_reads_zero(bi_instr *ins)
{
        return !(bi_fma_atomic(ins->op) || ins->op == BI_OPCODE_IMULD);
}

static bool
bi_reads_temps(bi_instr *ins, unsigned src)
{
        switch (ins->op) {
        /* Cannot permute a temporary */
        case BI_OPCODE_CLPER_V6_I32:
        case BI_OPCODE_CLPER_V7_I32:
                return src != 0;
        case BI_OPCODE_IMULD:
                return false;
        default:
                return true;
        }
}

ASSERTED static bool
bi_reads_t(bi_instr *ins, unsigned src)
{
        /* Branch offset cannot come from passthrough */
        if (bi_opcode_props[ins->op].branch)
                return src != 2;

        /* Table can never read passthrough */
        if (bi_opcode_props[ins->op].table)
                return false;

        /* Staging register reads may happen before the succeeding register
         * block encodes a write, so effectively there is no passthrough */
        if (src == 0 && bi_opcode_props[ins->op].sr_read)
                return false;

        /* Descriptor must not come from a passthrough */
        switch (ins->op) {
        case BI_OPCODE_LD_CVT:
        case BI_OPCODE_LD_TILE:
        case BI_OPCODE_ST_CVT:
        case BI_OPCODE_ST_TILE:
        case BI_OPCODE_TEXC:
                return src != 2;
        case BI_OPCODE_BLEND:
                return src != 2 && src != 3;

        /* Else, just check if we can read any temps */
        default:
                return bi_reads_temps(ins, src);
        }
}

/* Counts the number of 64-bit constants required by a clause. TODO: We
 * might want to account for merging, right now we overestimate, but
 * that's probably fine most of the time */

static unsigned
bi_nconstants(struct bi_clause_state *clause)
{
        unsigned count_32 = 0;

        for (unsigned i = 0; i < ARRAY_SIZE(clause->consts); ++i)
                count_32 += clause->consts[i].constant_count;

        return DIV_ROUND_UP(count_32, 2);
}

/* Would there be space for constants if we added one tuple? */

static bool
bi_space_for_more_constants(struct bi_clause_state *clause)
{
        return (bi_nconstants(clause) < 13 - (clause->tuple_count + 1));
}

/* Updates the FAU assignment for a tuple. A valid FAU assignment must be
 * possible (as a precondition), though not necessarily on the selected unit;
 * this is gauranteed per-instruction by bi_lower_fau and per-tuple by
 * bi_instr_schedulable */

static bool
bi_update_fau(struct bi_clause_state *clause,
                struct bi_tuple_state *tuple,
                bi_instr *instr, bool fma, bool destructive)
{
        /* Maintain our own constants, for nondestructive mode */
        uint32_t copied_constants[2], copied_count;
        unsigned *constant_count = &tuple->constant_count;
        uint32_t *constants = tuple->constants;
        enum bir_fau fau = tuple->fau;

        if (!destructive) {
                memcpy(copied_constants, tuple->constants,
                                (*constant_count) * sizeof(constants[0]));
                copied_count = tuple->constant_count;

                constant_count = &copied_count;
                constants = copied_constants;
        }

        bi_foreach_src(instr, s) {
                bi_index src = instr->src[s];

                if (src.type == BI_INDEX_FAU) {
                        bool no_constants = *constant_count == 0;
                        bool no_other_fau = (fau == src.value) || !fau;
                        bool mergable = no_constants && no_other_fau;

                        if (destructive) {
                                assert(mergable);
                                tuple->fau = src.value;
                        } else if (!mergable) {
                                return false;
                        }

                        fau = src.value;
                } else if (src.type == BI_INDEX_CONSTANT) {
                        /* No need to reserve space if we have a fast 0 */
                        if (src.value == 0 && fma && bi_reads_zero(instr))
                                continue;

                        /* If there is a branch target, #0 by convention is the
                         * PC-relative offset to the target */
                        bool pcrel = instr->branch_target && src.value == 0;
                        bool found = false;

                        for (unsigned i = 0; i < *constant_count; ++i) {
                                found |= (constants[i] == src.value) &&
                                        (i != tuple->pcrel_idx);
                        }

                        /* pcrel constants are unique, so don't match */
                        if (found && !pcrel)
                                continue;

                        bool no_fau = (*constant_count > 0) || !fau;
                        bool mergable = no_fau && ((*constant_count) < 2);

                        if (destructive) {
                                assert(mergable);

                                if (pcrel)
                                        tuple->pcrel_idx = *constant_count;
                        } else if (!mergable)
                                return false;

                        constants[(*constant_count)++] = src.value;
                }
        }

        /* Constants per clause may be limited by tuple count */
        bool room_for_constants = (*constant_count == 0) ||
                bi_space_for_more_constants(clause);

        if (destructive)
                assert(room_for_constants);
        else if (!room_for_constants)
                return false;

        return true;
}

/* Given an in-progress tuple, a candidate new instruction to add to the tuple,
 * and a source (index) from that candidate, determine whether this source is
 * "new", in the sense of requiring an additional read slot. That is, checks
 * whether the specified source reads from the register file via a read slot
 * (determined by its type and placement) and whether the source was already
 * specified by a prior read slot (to avoid double counting) */

static bool
bi_tuple_is_new_src(bi_instr *instr, struct bi_reg_state *reg, unsigned src_idx)
{
        bi_index src = instr->src[src_idx];

        /* Only consider sources which come from the register file */
        if (!(src.type == BI_INDEX_NORMAL || src.type == BI_INDEX_REGISTER))
                return false;

        /* Staging register reads bypass the usual register file mechanism */
        if (src_idx == 0 && bi_opcode_props[instr->op].sr_read)
                return false;

        /* If a source is already read in the tuple, it is already counted */
        for (unsigned t = 0; t < reg->nr_reads; ++t)
                if (bi_is_word_equiv(src, reg->reads[t]))
                        return false;

        /* If a source is read in _this instruction_, it is already counted */
        for (unsigned t = 0; t < src_idx; ++t)
                if (bi_is_word_equiv(src, instr->src[t]))
                        return false;

        return true;
}

/* Given two tuples in source order, count the number of register reads of the
 * successor, determined as the number of unique words accessed that aren't
 * written by the predecessor (since those are tempable).
 */

static unsigned
bi_count_succ_reads(bi_index t0, bi_index t1,
                bi_index *succ_reads, unsigned nr_succ_reads)
{
        unsigned reads = 0;

        for (unsigned i = 0; i < nr_succ_reads; ++i) {
                bool unique = true;

                for (unsigned j = 0; j < i; ++j)
                        if (bi_is_word_equiv(succ_reads[i], succ_reads[j]))
                                unique = false;

                if (!unique)
                        continue;

                if (bi_is_word_equiv(succ_reads[i], t0))
                        continue;

                if (bi_is_word_equiv(succ_reads[i], t1))
                        continue;

                reads++;
        }

        return reads;
}

/* Not all instructions can read from the staging passthrough (as determined by
 * reads_t), check if a given pair of instructions has such a restriction. Note
 * we also use this mechanism to prevent data races around staging register
 * reads, so we allow the input source to potentially be vector-valued */

static bool
bi_has_staging_passthrough_hazard(bi_index fma, bi_instr *add)
{
        bi_foreach_src(add, s) {
                bi_index src = add->src[s];
                unsigned count = bi_count_read_registers(add, s);

                if (!bi_is_equiv(fma, src))
                        continue;

                /* fma \in [src, src + src_count) */
                if (!(fma.offset >= src.offset && fma.offset < src.offset + count))
                        continue;

                if (!bi_reads_t(add, s))
                        return true;
        }

        return false;
}

/* Likewise for cross-tuple passthrough (reads_temps) */

static bool
bi_has_cross_passthrough_hazard(bi_tuple *succ, bi_instr *ins)
{
        bi_foreach_instr_in_tuple(succ, pins) {
                bi_foreach_src(pins, s) {
                        if (bi_is_word_equiv(ins->dest[0], pins->src[s]) &&
                                        !bi_reads_temps(pins, s))
                                return true;
                }
        }

        return false;
}

/* Is a register written other than the staging mechanism? ATEST is special,
 * writing to both a staging register and a regular register (fixed packing)*/

static bool
bi_writes_reg(bi_instr *instr)
{
        return (instr->op == BI_OPCODE_ATEST) ||
                (!bi_is_null(instr->dest[0]) &&
                 !bi_opcode_props[instr->op].sr_write);
}

/* Instruction placement entails two questions: what subset of instructions in
 * the block can legally be scheduled? and of those which is the best? That is,
 * we seek to maximize a cost function on a subset of the worklist satisfying a
 * particular predicate. The necessary predicate is determined entirely by
 * Bifrost's architectural limitations and is described in the accompanying
 * whitepaper. The cost function is a heuristic. */

static bool
bi_instr_schedulable(bi_instr *instr,
                struct bi_clause_state *clause,
                struct bi_tuple_state *tuple,
                bool fma)
{
        /* The units must match */
        if ((fma && !bi_can_fma(instr)) || (!fma && !bi_can_add(instr)))
                return false;

        /* There can only be one message-passing instruction per clause */
        if (bi_must_message(instr) && clause->message)
                return false;

        /* Some instructions have placement requirements */
        if (bi_must_last(instr) && !tuple->last)
                return false;

        if (bi_must_not_last(instr) && tuple->last)
                return false;

        /* Message-passing instructions are not guaranteed write within the
         * same clause (most likely they will not), so if a later instruction
         * in the clause reads from the destination, the message-passing
         * instruction can't be scheduled */
        if (bi_opcode_props[instr->op].sr_write) {
                for (unsigned i = 0; i < clause->read_count; ++i) {
                        if (bi_is_equiv(instr->dest[0], clause->reads[i]))
                                return false;
                }
        }

        /* If FAU is already assigned, we may not disrupt that. Do a
         * non-disruptive test update */
        if (!bi_update_fau(clause, tuple, instr, fma, false))
                return false;

        /* If this choice of FMA would force a staging passthrough, the ADD
         * instruction must support such a passthrough */
        if (tuple->add && bi_has_staging_passthrough_hazard(instr->dest[0], tuple->add))
                return false;

        /* If this choice of destination would force a cross-tuple passthrough, the next tuple must support that */
        if (tuple->prev && bi_has_cross_passthrough_hazard(tuple->prev, instr))
                return false;

        /* Register file writes are limited, TODO don't count tempable things */
        unsigned total_writes = tuple->reg.nr_writes;

        if (bi_writes_reg(instr))
                total_writes++;

        /* Last tuple in a clause can only write a single value */
        if (tuple->last && total_writes > 1)
                return false;

        /* Register file reads are limited, so count unique */

        unsigned unique_new_srcs = 0;

        bi_foreach_src(instr, s) {
                if (bi_tuple_is_new_src(instr, &tuple->reg, s))
                        unique_new_srcs++;
        }

        unsigned total_srcs = tuple->reg.nr_reads + unique_new_srcs;

        /* TODO: spill to moves */
        if (total_srcs > 3)
                return false;

        /* Count effective reads for the successor */
        unsigned succ_reads = bi_count_succ_reads(instr->dest[0],
                        tuple->add ? tuple->add->dest[0] : bi_null(),
                        tuple->prev_reads, tuple->nr_prev_reads);

        /* Successor must satisfy R+W <= 4, so we require W <= 4-R */
        if (total_writes > MAX2(4 - (signed) succ_reads, 0))
                return false;

        return true;
}

static signed
bi_instr_cost(bi_instr *instr)
{
        /* TODO: stub */
        return 0;
}

static unsigned
bi_choose_index(struct bi_worklist st,
                struct bi_clause_state *clause,
                struct bi_tuple_state *tuple,
                bool fma)
{
        unsigned i, best_idx = ~0;
        signed best_cost = INT_MAX;

        BITSET_FOREACH_SET(i, st.worklist, st.count) {
                bi_instr *instr = st.instructions[i];

                if (!bi_instr_schedulable(instr, clause, tuple, fma))
                        continue;

                signed cost = bi_instr_cost(instr);

                if (cost <= best_cost) {
                        best_idx = i;
                        best_cost = cost;
                }
        }

        return best_idx;
}

static void
bi_pop_instr(struct bi_clause_state *clause, struct bi_tuple_state *tuple,
                bi_instr *instr, bool fma)
{
        bi_update_fau(clause, tuple, instr, fma, true);

        /* TODO: maybe opt a bit? or maybe doesn't matter */
        assert(clause->read_count + BI_MAX_SRCS <= ARRAY_SIZE(clause->reads));
        memcpy(clause->reads + clause->read_count, instr->src, sizeof(instr->src));
        clause->read_count += BI_MAX_SRCS;

        if (bi_writes_reg(instr))
                tuple->reg.nr_writes++;

        bi_foreach_src(instr, s) {
                if (bi_tuple_is_new_src(instr, &tuple->reg, s))
                        tuple->reg.reads[tuple->reg.nr_reads++] = instr->src[s];
        }
}

/* Choose the best instruction and pop it off the worklist. Returns NULL if no
 * instruction is available. This function is destructive. */

static bi_instr *
bi_take_instr(bi_context *ctx, struct bi_worklist st,
                struct bi_clause_state *clause,
                struct bi_tuple_state *tuple,
                bool fma)
{
        if (tuple->add && tuple->add->op == BI_OPCODE_CUBEFACE)
                return bi_lower_cubeface(ctx, clause, tuple);
        else if (tuple->add && tuple->add->op == BI_OPCODE_PATOM_C_I32)
                return bi_lower_atom_c(ctx, clause, tuple);
        else if (tuple->add && tuple->add->op == BI_OPCODE_PATOM_C1_I32)
                return bi_lower_atom_c1(ctx, clause, tuple);
        else if (tuple->add && tuple->add->op == BI_OPCODE_SEG_ADD_I64)
                return bi_lower_seg_add(ctx, clause, tuple);
        else if (tuple->add && tuple->add->table)
                return bi_lower_dtsel(ctx, clause, tuple);

#ifndef NDEBUG
        /* Don't pair instructions if debugging */
        if ((bifrost_debug & BIFROST_DBG_NOSCHED) && tuple->add)
                return NULL;
#endif

        unsigned idx = bi_choose_index(st, clause, tuple, fma);

        if (idx >= st.count)
                return NULL;

        /* Update state to reflect taking the instruction */
        bi_instr *instr = st.instructions[idx];
        BITSET_CLEAR(st.worklist, idx);
        bi_update_worklist(st, idx);
        bi_pop_instr(clause, tuple, instr, fma);

        return instr;
}

/* Variant of bi_rewrite_index_src_single that uses word-equivalence, rewriting
 * to a passthrough register. If except_zero is true, the zeroth (first) source
 * is skipped, so staging register reads are not accidentally encoded as
 * passthrough (which is impossible) */

static void
bi_use_passthrough(bi_instr *ins, bi_index old,
                enum bifrost_packed_src new,
                bool except_zero)
{
        /* Optional for convenience */
        if (!ins || bi_is_null(old))
                return;

        bi_foreach_src(ins, i) {
                if (i == 0 && except_zero)
                        continue;

                if (bi_is_word_equiv(ins->src[i], old)) {
                        ins->src[i].type = BI_INDEX_PASS;
                        ins->src[i].value = new;
                        ins->src[i].reg = false;
                        ins->src[i].offset = 0;
                }
        }
}

/* Rewrites an adjacent pair of tuples _prec_eding and _succ_eding to use
 * intertuple passthroughs where necessary. Passthroughs are allowed as a
 * post-condition of scheduling. */

static void
bi_rewrite_passthrough(bi_tuple prec, bi_tuple succ)
{
        bool sr_read = succ.add ? bi_opcode_props[succ.add->op].sr_read : false;

        if (prec.fma) {
                bi_use_passthrough(succ.fma, prec.fma->dest[0], BIFROST_SRC_PASS_FMA, false);
                bi_use_passthrough(succ.add, prec.fma->dest[0], BIFROST_SRC_PASS_FMA, sr_read);
        }

        if (prec.add) {
                bi_use_passthrough(succ.fma, prec.add->dest[0], BIFROST_SRC_PASS_ADD, false);
                bi_use_passthrough(succ.add, prec.add->dest[0], BIFROST_SRC_PASS_ADD, sr_read);
        }
}

static void
bi_rewrite_fau_to_pass(bi_tuple *tuple)
{
        bi_foreach_instr_and_src_in_tuple(tuple, ins, s) {
                if (ins->src[s].type != BI_INDEX_FAU) continue;

                bi_index pass = bi_passthrough(ins->src[s].offset ?
                                BIFROST_SRC_FAU_HI : BIFROST_SRC_FAU_LO);

                ins->src[s] = bi_replace_index(ins->src[s], pass);
        }
}

static void
bi_rewrite_zero(bi_instr *ins, bool fma)
{
        bi_index zero = bi_passthrough(fma ? BIFROST_SRC_STAGE : BIFROST_SRC_FAU_LO);

        bi_foreach_src(ins, s) {
                bi_index src = ins->src[s];

                if (src.type == BI_INDEX_CONSTANT && src.value == 0)
                        ins->src[s] = bi_replace_index(src, zero);
        }
}

/* Assumes #0 to {T, FAU} rewrite has already occurred */

static void
bi_rewrite_constants_to_pass(bi_tuple *tuple, uint64_t constant, bool pcrel)
{
        bi_foreach_instr_and_src_in_tuple(tuple, ins, s) {
                if (ins->src[s].type != BI_INDEX_CONSTANT) continue;

                uint32_t cons = ins->src[s].value;

                ASSERTED bool lo = (cons == (constant & 0xffffffff));
                bool hi = (cons == (constant >> 32ull));

                /* PC offsets always live in the upper half, set to zero by
                 * convention before pack time. (This is safe, since if you
                 * wanted to compare against zero, you would use a BRANCHZ
                 * instruction instead.) */
                if (cons == 0 && ins->branch_target != NULL) {
                        assert(pcrel);
                        hi = true;
                        lo = false;
                } else if (pcrel) {
                        hi = false;
                }

                assert(lo || hi);

                ins->src[s] = bi_replace_index(ins->src[s],
                                bi_passthrough(hi ?  BIFROST_SRC_FAU_HI :
                                        BIFROST_SRC_FAU_LO));
        }
}

/* Constructs a constant state given a tuple state. This has the
 * postcondition that pcrel applies to the first constant by convention,
 * and PC-relative constants will be #0 by convention here, so swap to
 * match if needed */

static struct bi_const_state
bi_get_const_state(struct bi_tuple_state *tuple)
{
        struct bi_const_state consts = {
                .constant_count = tuple->constant_count,
                .constants[0] = tuple->constants[0],
                .constants[1] = tuple->constants[1],
                .pcrel = tuple->add && tuple->add->branch_target,
        };

        /* pcrel applies to the first constant by convention, and
         * PC-relative constants will be #0 by convention here, so swap
         * to match if needed */
        if (consts.pcrel && consts.constants[0]) {
                assert(consts.constant_count == 2);
                assert(consts.constants[1] == 0);

                consts.constants[1] = consts.constants[0];
                consts.constants[0] = 0;
        }

        return consts;
}

/* Merges constants in a clause, satisfying the following rules, assuming no
 * more than one tuple has pcrel:
 *
 * 1. If a tuple has two constants, they must be packed together. If one is
 * pcrel, it must be the high constant to use the M1=4 modification [sx64(E0) +
 * (PC << 32)]. Otherwise choose an arbitrary order.
 *
 * 4. If a tuple has one constant, it may be shared with an existing
 * pair that already contains that constant, or it may be combined with another
 * (distinct) tuple of a single constant.
 *
 * This gaurantees a packing is possible. The next routine handles modification
 * related swapping, to satisfy format 12 and the lack of modification for
 * tuple count 5/8 in EC0.
 */

static uint64_t
bi_merge_u32(uint32_t c0, uint32_t c1, bool pcrel)
{
        /* At this point in the constant merge algorithm, pcrel constants are
         * treated as zero, so pcrel implies at least one constants is zero */ 
        assert(!pcrel || (c0 == 0 || c1 == 0));

        /* Order: pcrel, maximum non-pcrel, minimum non-pcrel */
        uint32_t hi = pcrel ? 0 : MAX2(c0, c1);
        uint32_t lo = (c0 == hi) ? c1 : c0;

        /* Merge in the selected order */
        return lo | (((uint64_t) hi) << 32ull);
}

static unsigned
bi_merge_pairs(struct bi_const_state *consts, unsigned tuple_count,
                uint64_t *merged, unsigned *pcrel_pair)
{
        unsigned merge_count = 0;

        for (unsigned t = 0; t < tuple_count; ++t) {
                if (consts[t].constant_count != 2) continue;

                unsigned idx = ~0;
                uint64_t val = bi_merge_u32(consts[t].constants[0],
                                consts[t].constants[1], consts[t].pcrel);

                /* Skip the pcrel pair if assigned, because if one is assigned,
                 * this one is not pcrel by uniqueness so it's a mismatch */
                for (unsigned s = 0; s < merge_count; ++s) {
                        if (merged[s] == val && (*pcrel_pair) != s) {
                                idx = s;
                                break;
                        }
                }

                if (idx == ~0) {
                        idx = merge_count++;
                        merged[idx] = val;

                        if (consts[t].pcrel)
                                (*pcrel_pair) = idx;
                }

                consts[t].word_idx = idx;
        }

        return merge_count;
}

static unsigned
bi_merge_singles(struct bi_const_state *consts, unsigned tuple_count,
                uint64_t *pairs, unsigned pair_count, unsigned *pcrel_pair)
{
        bool pending = false, pending_pcrel = false;
        uint32_t pending_single = 0;

        for (unsigned t = 0; t < tuple_count; ++t) {
                if (consts[t].constant_count != 1) continue;

                uint32_t val = consts[t].constants[0];
                unsigned idx = ~0;

                /* Try to match, but don't match pcrel with non-pcrel, even
                 * though we can merge a pcrel with a non-pcrel single */
                for (unsigned i = 0; i < pair_count; ++i) {
                        bool lo = ((pairs[i] & 0xffffffff) == val);
                        bool hi = ((pairs[i] >> 32) == val);
                        bool match = (lo || hi);
                        match &= ((*pcrel_pair) != i);
                        if (match && !consts[t].pcrel) {
                                idx = i;
                                break;
                        }
                }

                if (idx == ~0) {
                        idx = pair_count;

                        if (pending && pending_single != val) {
                                assert(!(pending_pcrel && consts[t].pcrel));
                                bool pcrel = pending_pcrel || consts[t].pcrel;

                                if (pcrel)
                                        *pcrel_pair = idx;

                                pairs[pair_count++] = bi_merge_u32(pending_single, val, pcrel);

                                pending = pending_pcrel = false;
                        } else {
                                pending = true;
                                pending_pcrel = consts[t].pcrel;
                                pending_single = val;
                        }
                }

                consts[t].word_idx = idx;
        }

        /* Shift so it works whether pending_pcrel is set or not */
        if (pending) {
                if (pending_pcrel)
                        *pcrel_pair = pair_count;

                pairs[pair_count++] = ((uint64_t) pending_single) << 32ull;
        }

        return pair_count;
}

static unsigned
bi_merge_constants(struct bi_const_state *consts, uint64_t *pairs, unsigned *pcrel_idx)
{
        unsigned pair_count = bi_merge_pairs(consts, 8, pairs, pcrel_idx);
        return bi_merge_singles(consts, 8, pairs, pair_count, pcrel_idx);
}

/* Swap two constants at word i and i+1 by swapping their actual positions and
 * swapping all references so the meaning of the clause is preserved */

static void
bi_swap_constants(struct bi_const_state *consts, uint64_t *pairs, unsigned i)
{
        uint64_t tmp_pair = pairs[i + 0];
        pairs[i + 0] = pairs[i + 1];
        pairs[i + 1] = tmp_pair;

        for (unsigned t = 0; t < 8; ++t) {
                if (consts[t].word_idx == i)
                        consts[t].word_idx = (i + 1);
                else if (consts[t].word_idx == (i + 1))
                        consts[t].word_idx = i;
        }
}

/* Given merged constants, one of which might be PC-relative, fix up the M
 * values so the PC-relative constant (if it exists) has the M1=4 modification
 * and other constants are used as-is (which might require swapping) */

static unsigned
bi_apply_constant_modifiers(struct bi_const_state *consts,
                uint64_t *pairs, unsigned *pcrel_idx,
                unsigned tuple_count, unsigned constant_count)
{
        unsigned start = bi_ec0_packed(tuple_count) ? 1 : 0;

        /* Clauses with these tuple counts lack an M field for the packed EC0,
         * so EC0 cannot be PC-relative, which might require swapping (and
         * possibly adding an unused constant) to fit */

        if (*pcrel_idx == 0 && (tuple_count == 5 || tuple_count == 8)) {
                constant_count = MAX2(constant_count, 2);
                *pcrel_idx = 1;
                bi_swap_constants(consts, pairs, 0);
        }

        /* EC0 might be packed free, after that constants are packed in pairs
         * (with clause format 12), with M1 values computed from the pair */

        for (unsigned i = start; i < constant_count; i += 2) {
                bool swap = false;
                bool last = (i + 1) == constant_count;

                unsigned A1 = (pairs[i] >> 60);
                unsigned B1 = (pairs[i + 1] >> 60);

                if (*pcrel_idx == i || *pcrel_idx == (i + 1)) {
                        /* PC-relative constant must be E0, not E1 */
                        swap = (*pcrel_idx == (i + 1));

                        /* Set M1 = 4 by noting (A - B) mod 16 = 4 is
                         * equivalent to A = (B + 4) mod 16 and that we can
                         * control A */
                        unsigned B = swap ? A1 : B1;
                        unsigned A = (B + 4) & 0xF;
                        pairs[*pcrel_idx] |= ((uint64_t) A) << 60;

                        /* Swapped if swap set, identity if swap not set */
                        *pcrel_idx = i;
                } else {
                        /* Compute M1 value if we don't swap */
                        unsigned M1 = (16 + A1 - B1) & 0xF;

                        /* For M1 = 0 or M1 >= 8, the constants are unchanged,
                         * we have 0 < (A1 - B1) % 16 < 8, which implies (B1 -
                         * A1) % 16 >= 8, so swapping will let them be used
                         * unchanged */
                        swap = (M1 != 0) && (M1 < 8);

                        /* However, we can't swap the last constant, so we
                         * force M1 = 0 instead for this case */
                        if (last && swap) {
                                pairs[i + 1] |= pairs[i] & (0xfull << 60);
                                swap = false;
                        }
                }

                if (swap) {
                        assert(!last);
                        bi_swap_constants(consts, pairs, i);
                }
        }

        return constant_count;
}

/* Schedule a single clause. If no instructions remain, return NULL. */

static bi_clause *
bi_schedule_clause(bi_context *ctx, bi_block *block, struct bi_worklist st)
{
        struct bi_clause_state clause_state = { 0 };
        bi_clause *clause = rzalloc(ctx, bi_clause);
        bi_tuple *tuple = NULL;

        const unsigned max_tuples = ARRAY_SIZE(clause->tuples);

        /* TODO: Decide flow control better */
        clause->flow_control = BIFROST_FLOW_NBTB;

        /* The last clause can only write one instruction, so initialize that */
        struct bi_reg_state reg_state = {};
        bi_index prev_reads[5] = { bi_null() };
        unsigned nr_prev_reads = 0;

        do {
                struct bi_tuple_state tuple_state = {
                        .last = (clause->tuple_count == 0),
                        .reg = reg_state,
                        .nr_prev_reads = nr_prev_reads,
                        .prev = tuple,
                        .pcrel_idx = ~0,
                };

                assert(nr_prev_reads < ARRAY_SIZE(prev_reads));
                memcpy(tuple_state.prev_reads, prev_reads, sizeof(prev_reads));

                unsigned idx = max_tuples - clause->tuple_count - 1;

                tuple = &clause->tuples[idx];

                /* Since we schedule backwards, we schedule ADD first */
                tuple_state.add = bi_take_instr(ctx, st, &clause_state, &tuple_state, false);
                tuple->fma = bi_take_instr(ctx, st, &clause_state, &tuple_state, true);
                tuple->add = tuple_state.add;

                /* We may have a message, but only one per clause */
                if (tuple->add && bi_must_message(tuple->add)) {
                        assert(!clause_state.message);
                        clause_state.message = true;

                        clause->message_type =
                                bi_message_type_for_instr(tuple->add);
                        clause->message = tuple->add;

                        switch (tuple->add->op) {
                        case BI_OPCODE_ATEST:
                                clause->dependencies |= (1 << BIFROST_SLOT_ELDEST_DEPTH);
                                break;
                        case BI_OPCODE_LD_TILE:
                                if (!ctx->inputs->is_blend)
                                        clause->dependencies |= (1 << BIFROST_SLOT_ELDEST_COLOUR);
                                break;
                        case BI_OPCODE_BLEND:
                                clause->dependencies |= (1 << BIFROST_SLOT_ELDEST_DEPTH);
                                clause->dependencies |= (1 << BIFROST_SLOT_ELDEST_COLOUR);
                                break;
                        default:
                                break;
                        }
                }

                clause_state.consts[idx] = bi_get_const_state(&tuple_state);

                /* Before merging constants, eliminate zeroes, otherwise the
                 * merging will fight over the #0 that never gets read (and is
                 * never marked as read by update_fau) */
                if (tuple->fma && bi_reads_zero(tuple->fma))
                        bi_rewrite_zero(tuple->fma, true);

                /* Rewrite away FAU, constant write is deferred */
                if (!tuple_state.constant_count) {
                        tuple->fau_idx = tuple_state.fau;
                        bi_rewrite_fau_to_pass(tuple);
                }

                /* Use passthrough register for cross-stage accesses. Since
                 * there are just FMA and ADD stages, that means we rewrite to
                 * passthrough the sources of the ADD that read from the
                 * destination of the FMA */

                if (tuple->fma) {
                        bi_use_passthrough(tuple->add, tuple->fma->dest[0],
                                        BIFROST_SRC_STAGE, false);
                }

                /* Don't add an empty tuple, unless the worklist has nothing
                 * but a (pseudo)instruction failing to schedule due to a "not
                 * last instruction" constraint */

                int some_instruction = __bitset_ffs(st.worklist, BITSET_WORDS(st.count));
                bool not_last = (some_instruction > 0) &&
                        bi_must_not_last(st.instructions[some_instruction - 1]);

                if (!(tuple->fma || tuple->add || (tuple_state.last && not_last)))
                        break;

                clause->tuple_count++;

                /* Adding enough tuple might overflow constants */
                if (!bi_space_for_more_constants(&clause_state))
                        break;

#ifndef NDEBUG
                /* Don't schedule more than 1 tuple if debugging */
                if (bifrost_debug & BIFROST_DBG_NOSCHED)
                        break;
#endif

                /* Link through the register state */
                STATIC_ASSERT(sizeof(prev_reads) == sizeof(tuple_state.reg.reads));
                memcpy(prev_reads, tuple_state.reg.reads, sizeof(prev_reads));
                nr_prev_reads = tuple_state.reg.nr_reads;
                clause_state.tuple_count++;
        } while(clause->tuple_count < 8);

        /* Don't schedule an empty clause */
        if (!clause->tuple_count)
                return NULL;

        /* Before merging, rewrite away any tuples that read only zero */
        for (unsigned i = max_tuples - clause->tuple_count; i < max_tuples; ++i) {
                bi_tuple *tuple = &clause->tuples[i];
                struct bi_const_state *st = &clause_state.consts[i];

                if (st->constant_count == 0 || st->constants[0] || st->constants[1] || st->pcrel)
                        continue;

                bi_foreach_instr_in_tuple(tuple, ins)
                        bi_rewrite_zero(ins, false);

                /* Constant has been demoted to FAU, so don't pack it separately */
                st->constant_count = 0;

                /* Default */
                assert(tuple->fau_idx == BIR_FAU_ZERO);
        }

        uint64_t constant_pairs[8] = { 0 };
        unsigned pcrel_idx = ~0;
        unsigned constant_words =
                bi_merge_constants(clause_state.consts, constant_pairs, &pcrel_idx);

        constant_words = bi_apply_constant_modifiers(clause_state.consts,
                        constant_pairs, &pcrel_idx, clause->tuple_count,
                        constant_words);

        clause->pcrel_idx = pcrel_idx;

        for (unsigned i = max_tuples - clause->tuple_count; i < max_tuples; ++i) {
                bi_tuple *tuple = &clause->tuples[i];

                /* If no constants, leave FAU as it is, possibly defaulting to 0 */
                if (clause_state.consts[i].constant_count == 0)
                        continue;

                /* FAU is already handled */
                assert(!tuple->fau_idx);

                unsigned word_idx = clause_state.consts[i].word_idx;
                assert(word_idx <= 8);

                /* We could try to merge regardless of bottom bits as well, but
                 * that's probably diminishing returns */
                uint64_t pair = constant_pairs[word_idx];
                unsigned lo = pair & 0xF;

                tuple->fau_idx = bi_constant_field(word_idx) | lo;
                bi_rewrite_constants_to_pass(tuple, pair, word_idx == pcrel_idx);
        }

        clause->constant_count = constant_words;
        memcpy(clause->constants, constant_pairs, sizeof(constant_pairs));

        /* Branches must be last, so this can be factored out */
        bi_instr *last = clause->tuples[max_tuples - 1].add;
        clause->next_clause_prefetch = !last || (last->op != BI_OPCODE_JUMP);
        clause->block = block;

        /* TODO: scoreboard assignment post-sched */
        clause->dependencies |= (1 << 0);

        /* We emit in reverse and emitted to the back of the tuples array, so
         * move it up front for easy indexing */
        memmove(clause->tuples,
                       clause->tuples + (max_tuples - clause->tuple_count),
                       clause->tuple_count * sizeof(clause->tuples[0]));

        /* Use passthrough register for cross-tuple accesses. Note this is
         * after the memmove, so this is forwards. Skip the first tuple since
         * there is nothing before it to passthrough */

        for (unsigned t = 1; t < clause->tuple_count; ++t)
                bi_rewrite_passthrough(clause->tuples[t - 1], clause->tuples[t]);

        return clause;
}

static void
bi_schedule_block(bi_context *ctx, bi_block *block)
{
        list_inithead(&block->clauses);

        /* Copy list to dynamic array */
        struct bi_worklist st = bi_initialize_worklist(block);

        if (!st.count) {
                bi_free_worklist(st);
                return;
        }

        /* Schedule as many clauses as needed to fill the block */
        bi_clause *u = NULL;
        while((u = bi_schedule_clause(ctx, block, st)))
                list_add(&u->link, &block->clauses);

        /* Back-to-back bit affects only the last clause of a block,
         * the rest are implicitly true */
        if (!list_is_empty(&block->clauses)) {
                bi_clause *last_clause = list_last_entry(&block->clauses, bi_clause, link);
                if (!bi_back_to_back(block))
                        last_clause->flow_control = BIFROST_FLOW_NBTB_UNCONDITIONAL;
        }

        block->scheduled = true;

#ifndef NDEBUG
        unsigned i;
        bool incomplete = false;

        BITSET_FOREACH_SET(i, st.worklist, st.count) {
                bi_print_instr(st.instructions[i], stderr);
                incomplete = true;
        }

        if (incomplete)
                unreachable("The above instructions failed to schedule.");
#endif

        bi_free_worklist(st);
}

static bool
bi_check_fau_src(bi_instr *ins, unsigned s, uint32_t *constants, unsigned *cwords, bi_index *fau)
{
        bi_index src = ins->src[s];

        /* Staging registers can't have FAU accesses */
        if (s == 0 && bi_opcode_props[ins->op].sr_read)
                return (src.type != BI_INDEX_CONSTANT) && (src.type != BI_INDEX_FAU);

        if (src.type == BI_INDEX_CONSTANT) {
                /* Allow fast zero */
                if (src.value == 0 && bi_opcode_props[ins->op].fma && bi_reads_zero(ins))
                        return true;

                if (!bi_is_null(*fau))
                        return false;

                /* Else, try to inline a constant */
                for (unsigned i = 0; i < *cwords; ++i) {
                        if (src.value == constants[i])
                                return true;
                }

                if (*cwords >= 2)
                        return false;

                constants[(*cwords)++] = src.value;
        } else if (src.type == BI_INDEX_FAU) {
                if (*cwords != 0)
                        return false;

                /* Can only read from one pair of FAU words */
                if (!bi_is_null(*fau) && (src.value != fau->value))
                        return false;

                /* If there is a target, we'll need a PC-relative constant */
                if (ins->branch_target)
                        return false;

                *fau = src;
        }

        return true;
}

static void
bi_lower_fau(bi_context *ctx, bi_block *block)
{
        bi_builder b = bi_init_builder(ctx, bi_after_block(ctx->current_block));

        bi_foreach_instr_in_block_safe(block, _ins) {
                bi_instr *ins = (bi_instr *) _ins;

                uint32_t constants[2];
                unsigned cwords = 0;
                bi_index fau = bi_null();

                /* ATEST must have the ATEST datum encoded, not any other
                 * uniform. See to it this is the case. */
                if (ins->op == BI_OPCODE_ATEST)
                        fau = ins->src[2];

                bi_foreach_src(ins, s) {
                        if (bi_check_fau_src(ins, s, constants, &cwords, &fau)) continue;

                        b.cursor = bi_before_instr(ins);
                        bi_index copy = bi_mov_i32(&b, ins->src[s]);
                        ins->src[s] = bi_replace_index(ins->src[s], copy);
                }
        }
}

void
bi_schedule(bi_context *ctx)
{
        bi_foreach_block(ctx, block) {
                bi_block *bblock = (bi_block *) block;
                bi_lower_fau(ctx, bblock);
                bi_schedule_block(ctx, bblock);
        }

        bi_opt_dead_code_eliminate(ctx, true);
}

#ifndef NDEBUG

static bi_builder *
bit_builder(void *memctx)
{
        bi_context *ctx = rzalloc(memctx, bi_context);
        list_inithead(&ctx->blocks);

        bi_block *blk = rzalloc(ctx, bi_block);

        blk->base.predecessors = _mesa_set_create(blk,
                        _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        list_addtail(&blk->base.link, &ctx->blocks);
        list_inithead(&blk->base.instructions);

        bi_builder *b = rzalloc(memctx, bi_builder);
        b->shader = ctx;
        b->cursor = bi_after_block(blk);
        return b;
}

#define TMP() bi_temp(b->shader)

static void
bi_test_units(bi_builder *b)
{
        bi_instr *mov = bi_mov_i32_to(b, TMP(), TMP());
        assert(bi_can_fma(mov));
        assert(bi_can_add(mov));
        assert(!bi_must_last(mov));
        assert(!bi_must_message(mov));
        assert(bi_reads_zero(mov));
        assert(bi_reads_temps(mov, 0));
        assert(bi_reads_t(mov, 0));

        bi_instr *fma = bi_fma_f32_to(b, TMP(), TMP(), TMP(), bi_zero(), BI_ROUND_NONE);
        assert(bi_can_fma(fma));
        assert(!bi_can_add(fma));
        assert(!bi_must_last(fma));
        assert(!bi_must_message(fma));
        assert(bi_reads_zero(fma));
        for (unsigned i = 0; i < 3; ++i) {
                assert(bi_reads_temps(fma, i));
                assert(bi_reads_t(fma, i));
        }

        bi_instr *load = bi_load_i128_to(b, TMP(), TMP(), TMP(), BI_SEG_UBO);
        assert(!bi_can_fma(load));
        assert(bi_can_add(load));
        assert(!bi_must_last(load));
        assert(bi_must_message(load));
        for (unsigned i = 0; i < 2; ++i) {
                assert(bi_reads_temps(load, i));
                assert(bi_reads_t(load, i));
        }

        bi_instr *blend = bi_blend_to(b, TMP(), TMP(), TMP(), TMP(), TMP(), 4);
        assert(!bi_can_fma(load));
        assert(bi_can_add(load));
        assert(bi_must_last(blend));
        assert(bi_must_message(blend));
        for (unsigned i = 0; i < 4; ++i)
                assert(bi_reads_temps(blend, i));
        assert(!bi_reads_t(blend, 0));
        assert(bi_reads_t(blend, 1));
        assert(!bi_reads_t(blend, 2));
        assert(!bi_reads_t(blend, 3));
}

int bi_test_scheduler(void)
{
        void *memctx = NULL;

        bi_test_units(bit_builder(memctx));

        return 0;
}
#endif
