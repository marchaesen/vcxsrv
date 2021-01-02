/*
 * Copyright (C) 2020 Collabora, Ltd.
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

#include "compiler.h"

/* This file contains the final passes of the compiler. Running after
 * scheduling and RA, the IR is now finalized, so we need to emit it to actual
 * bits on the wire (as well as fixup branches) */

static uint64_t
bi_pack_header(bi_clause *clause, bi_clause *next_1, bi_clause *next_2, bool tdd)
{
        /* next_dependencies are the union of the dependencies of successors'
         * dependencies */

        unsigned dependency_wait = next_1 ? next_1->dependencies : 0;
        dependency_wait |= next_2 ? next_2->dependencies : 0;

        struct bifrost_header header = {
                .flow_control =
                        (next_1 == NULL) ? BIFROST_FLOW_END :
                        clause->flow_control,
                .terminate_discarded_threads = tdd,
                .next_clause_prefetch = clause->next_clause_prefetch && next_1,
                .staging_barrier = clause->staging_barrier,
                .staging_register = clause->staging_register,
                .dependency_wait = dependency_wait,
                .dependency_slot = clause->scoreboard_id,
                .message_type = clause->message_type,
                .next_message_type = next_1 ? next_1->message_type : 0,
                .suppress_inf = true,
                .suppress_nan = true,
        };

        uint64_t u = 0;
        memcpy(&u, &header, sizeof(header));
        return u;
}

/* The uniform/constant slot allows loading a contiguous 64-bit immediate or
 * pushed uniform per bundle. Figure out which one we need in the bundle (the
 * scheduler needs to ensure we only have one type per bundle), validate
 * everything, and rewrite away the register/uniform indices to use 3-bit
 * sources directly. */

static unsigned
bi_lookup_constant(bi_clause *clause, uint32_t cons, bool *hi)
{
        for (unsigned i = 0; i < clause->constant_count; ++i) {
                /* Try to apply to top or to bottom */
                uint64_t top = clause->constants[i];

                if (cons == ((uint32_t) top | (cons & 0xF)))
                        return i;

                if (cons == (top >> 32ul)) {
                        *hi = true;
                        return i;
                }
        }

        unreachable("Invalid constant accessed");
}

static unsigned
bi_constant_field(unsigned idx)
{
        assert(idx <= 5);

        const unsigned values[] = {
                4, 5, 6, 7, 2, 3
        };

        return values[idx] << 4;
}

static bool
bi_assign_fau_idx_single(bi_registers *regs,
                         bi_clause *clause,
                         bi_instr *ins,
                         bool assigned,
                         bool fast_zero)
{
        if (!ins)
                return assigned;

        if (ins->op == BI_OPCODE_ATEST) {
                /* ATEST FAU index must point to the ATEST parameter datum slot */
                assert(!assigned && !clause->branch_constant);
                regs->fau_idx = BIR_FAU_ATEST_PARAM;
                return true;
        }

        if (ins->branch_target && clause->branch_constant) {
                /* By convention branch constant is last XXX: this whole thing
                 * is a hack, FIXME */
                unsigned idx = clause->constant_count - 1;

                /* We can only jump to clauses which are qword aligned so the
                 * bottom 4-bits of the offset are necessarily 0 */
                unsigned lo = 0;

                /* Build the constant */
                unsigned C = bi_constant_field(idx) | lo;

                if (assigned && regs->fau_idx != C)
                        unreachable("Mismatched fau_idx: branch");

                bi_foreach_src(ins, s) {
                        if (ins->src[s].type == BI_INDEX_CONSTANT)
                                ins->src[s] = bi_passthrough(BIFROST_SRC_FAU_HI);
                }

                regs->fau_idx = C;
                return true;
        }

        bi_foreach_src(ins, s) {
                if (ins->src[s].type == BI_INDEX_CONSTANT) {
                        bool hi = false;
                        uint32_t cons = ins->src[s].value;
                        unsigned swizzle = ins->src[s].swizzle;

                        /* FMA can encode zero for free */
                        if (cons == 0 && fast_zero) {
                                assert(!ins->src[s].abs && !ins->src[s].neg);
                                ins->src[s] = bi_passthrough(BIFROST_SRC_STAGE);
                                ins->src[s].swizzle = swizzle;
                                continue;
                        }

                        unsigned idx = bi_lookup_constant(clause, cons, &hi);
                        unsigned lo = clause->constants[idx] & 0xF;
                        unsigned f = bi_constant_field(idx) | lo;

                        if (assigned && regs->fau_idx != f)
                                unreachable("Mismatched uniform/const field: imm");

                        regs->fau_idx = f;
                        ins->src[s] = bi_passthrough(hi ? BIFROST_SRC_FAU_HI : BIFROST_SRC_FAU_LO);
                        ins->src[s].swizzle = swizzle;
                        assigned = true;
                } else if (ins->src[s].type == BI_INDEX_FAU) {
                        bool hi = ins->src[s].offset > 0;

                        assert(!assigned || regs->fau_idx == ins->src[s].value);
                        assert(ins->src[s].swizzle == BI_SWIZZLE_H01);
                        regs->fau_idx = ins->src[s].value;
                        ins->src[s] = bi_passthrough(hi ? BIFROST_SRC_FAU_HI :
                                        BIFROST_SRC_FAU_LO);
                        assigned = true;
                }
        }

        return assigned;
}

static void
bi_assign_fau_idx(bi_clause *clause,
                  bi_bundle *bundle)
{
        bool assigned =
                bi_assign_fau_idx_single(&bundle->regs, clause, bundle->fma, false, true);

        bi_assign_fau_idx_single(&bundle->regs, clause, bundle->add, assigned, false);
}

/* Assigns a slot for reading, before anything is written */

static void
bi_assign_slot_read(bi_registers *regs, bi_index src)
{
        /* We only assign for registers */
        if (src.type != BI_INDEX_REGISTER)
                return;

        /* Check if we already assigned the slot */
        for (unsigned i = 0; i <= 1; ++i) {
                if (regs->slot[i] == src.value && regs->enabled[i])
                        return;
        }

        if (regs->slot[2] == src.value && regs->slot23.slot2 == BIFROST_OP_READ)
                return;

        /* Assign it now */

        for (unsigned i = 0; i <= 1; ++i) {
                if (!regs->enabled[i]) {
                        regs->slot[i] = src.value;
                        regs->enabled[i] = true;
                        return;
                }
        }

        if (!regs->slot23.slot3) {
                regs->slot[2] = src.value;
                regs->slot23.slot2 = BIFROST_OP_READ;
                return;
        }

        bi_print_slots(regs, stderr);
        unreachable("Failed to find a free slot for src");
}

static bi_registers
bi_assign_slots(bi_bundle *now, bi_bundle *prev)
{
        /* We assign slots for the main register mechanism. Special ops
         * use the data registers, which has its own mechanism entirely
         * and thus gets skipped over here. */

        bool read_dreg = now->add &&
                bi_opcode_props[(now->add)->op].sr_read;

        bool write_dreg = now->add &&
                bi_opcode_props[(now->add)->op].sr_write;

        /* First, assign reads */

        if (now->fma)
                bi_foreach_src(now->fma, src)
                        bi_assign_slot_read(&now->regs, (now->fma)->src[src]);

        if (now->add) {
                bi_foreach_src(now->add, src) {
                        if (!(src == 0 && read_dreg))
                                bi_assign_slot_read(&now->regs, (now->add)->src[src]);
                }
        }

        /* Next, assign writes. Staging writes are assigned separately, but
         * +ATEST wants its destination written to both a staging register
         * _and_ a regular write, because it may not generate a message */

        if (prev->add && (!write_dreg || prev->add->op == BI_OPCODE_ATEST)) {
                bi_index idx = prev->add->dest[0];

                if (idx.type == BI_INDEX_REGISTER) {
                        now->regs.slot[3] = idx.value;
                        now->regs.slot23.slot3 = BIFROST_OP_WRITE;
                }
        }

        if (prev->fma) {
                bi_index idx = (prev->fma)->dest[0];

                if (idx.type == BI_INDEX_REGISTER) {
                        if (now->regs.slot23.slot3) {
                                /* Scheduler constraint: cannot read 3 and write 2 */
                                assert(!now->regs.slot23.slot2);
                                now->regs.slot[2] = idx.value;
                                now->regs.slot23.slot2 = BIFROST_OP_WRITE;
                        } else {
                                now->regs.slot[3] = idx.value;
                                now->regs.slot23.slot3 = BIFROST_OP_WRITE;
                                now->regs.slot23.slot3_fma = true;
                        }
                }
        }

        return now->regs;
}

static enum bifrost_reg_mode
bi_pack_register_mode(bi_registers r)
{
        /* Handle idle special case for first instructions */
        if (r.first_instruction && !(r.slot23.slot2 | r.slot23.slot3))
                return BIFROST_IDLE_1;

        /* Otherwise, use the LUT */
        for (unsigned i = 0; i < ARRAY_SIZE(bifrost_reg_ctrl_lut); ++i) {
                if (memcmp(bifrost_reg_ctrl_lut + i, &r.slot23, sizeof(r.slot23)) == 0)
                        return i;
        }

        bi_print_slots(&r, stderr);
        unreachable("Invalid slot assignment");
}

static uint64_t
bi_pack_registers(bi_registers regs)
{
        enum bifrost_reg_mode mode = bi_pack_register_mode(regs);
        struct bifrost_regs s = { 0 };
        uint64_t packed = 0;

        /* Need to pack 5-bit mode as a 4-bit field. The decoder moves bit 3 to bit 4 for
         * first instruction and adds 16 when reg 2 == reg 3 */

        unsigned ctrl;
        bool r2_equals_r3 = false;

        if (regs.first_instruction) {
                /* Bit 3 implicitly must be clear for first instructions.
                 * The affected patterns all write both ADD/FMA, but that
                 * is forbidden for the first instruction, so this does
                 * not add additional encoding constraints */
                assert(!(mode & 0x8));

                /* Move bit 4 to bit 3, since bit 3 is clear */
                ctrl = (mode & 0x7) | ((mode & 0x10) >> 1);

                /* If we can let r2 equal r3, we have to or the hardware raises
                 * INSTR_INVALID_ENC (it's unclear why). */
                if (!(regs.slot23.slot2 && regs.slot23.slot3))
                        r2_equals_r3 = true;
        } else {
                /* We force r2=r3 or not for the upper bit */
                ctrl = (mode & 0xF);
                r2_equals_r3 = (mode & 0x10);
        }

        if (regs.enabled[1]) {
                /* Gotta save that bit!~ Required by the 63-x trick */
                assert(regs.slot[1] > regs.slot[0]);
                assert(regs.enabled[0]);

                /* Do the 63-x trick, see docs/disasm */
                if (regs.slot[0] > 31) {
                        regs.slot[0] = 63 - regs.slot[0];
                        regs.slot[1] = 63 - regs.slot[1];
                }

                assert(regs.slot[0] <= 31);
                assert(regs.slot[1] <= 63);

                s.ctrl = ctrl;
                s.reg1 = regs.slot[1];
                s.reg0 = regs.slot[0];
        } else {
                /* slot 1 disabled, so set to zero and use slot 1 for ctrl */
                s.ctrl = 0;
                s.reg1 = ctrl << 2;

                if (regs.enabled[0]) {
                        /* Bit 0 upper bit of slot 0 */
                        s.reg1 |= (regs.slot[0] >> 5);

                        /* Rest of slot 0 in usual spot */
                        s.reg0 = (regs.slot[0] & 0b11111);
                } else {
                        /* Bit 1 set if slot 0 also disabled */
                        s.reg1 |= (1 << 1);
                }
        }

        /* Force r2 =/!= r3 as needed */
        if (r2_equals_r3) {
                assert(regs.slot[3] == regs.slot[2] || !(regs.slot23.slot2 && regs.slot23.slot3));

                if (regs.slot23.slot2)
                        regs.slot[3] = regs.slot[2];
                else
                        regs.slot[2] = regs.slot[3];
        } else if (!regs.first_instruction) {
                /* Enforced by the encoding anyway */
                assert(regs.slot[2] != regs.slot[3]);
        }

        s.reg2 = regs.slot[2];
        s.reg3 = regs.slot[3];
        s.fau_idx = regs.fau_idx;

        memcpy(&packed, &s, sizeof(s));
        return packed;
}

struct bi_packed_bundle {
        uint64_t lo;
        uint64_t hi;
};

/* We must ensure slot 1 > slot 0 for the 63-x trick to function, so we fix
 * this up at pack time. (Scheduling doesn't care.) */

static void
bi_flip_slots(bi_registers *regs)
{
        if (regs->enabled[0] && regs->enabled[1] && regs->slot[1] < regs->slot[0]) {
                unsigned temp = regs->slot[0];
                regs->slot[0] = regs->slot[1];
                regs->slot[1] = temp;
        }

}

/* Lower CUBEFACE2 to a CUBEFACE1/CUBEFACE2. This is a hack so the scheduler
 * doesn't have to worry about this while we're just packing singletons */

static void
bi_lower_cubeface2(bi_context *ctx, bi_bundle *bundle)
{
        bi_instr *old = bundle->add;

        /* Filter for +CUBEFACE2 */
        if (!old || old->op != BI_OPCODE_CUBEFACE2)
                return;

        /* This won't be used once we emit non-singletons, for now this is just
         * a fact of our scheduler and allows us to clobber FMA */
        assert(!bundle->fma);

        /* Construct an FMA op */
        bi_instr *new = rzalloc(ctx, bi_instr);
        new->op = BI_OPCODE_CUBEFACE1;
        /* no dest, just a temporary */
        new->src[0] = old->src[0];
        new->src[1] = old->src[1];
        new->src[2] = old->src[2];

        /* Emit the instruction */
        list_addtail(&new->link, &old->link);
        bundle->fma = new;

        /* Now replace the sources of the CUBEFACE2 with a single passthrough
         * from the CUBEFACE1 (and a side-channel) */
        old->src[0] = bi_passthrough(BIFROST_SRC_STAGE);
        old->src[1] = old->src[2] = bi_null();
}

static inline enum bifrost_packed_src
bi_get_src_slot(bi_registers *regs, unsigned reg)
{
        if (regs->slot[0] == reg && regs->enabled[0])
                return BIFROST_SRC_PORT0;
        else if (regs->slot[1] == reg && regs->enabled[1])
                return BIFROST_SRC_PORT1;
        else if (regs->slot[2] == reg && regs->slot23.slot2 == BIFROST_OP_READ)
                return BIFROST_SRC_PORT2;
        else
                unreachable("Tried to access register with no port");
}

static inline enum bifrost_packed_src
bi_get_src_new(bi_instr *ins, bi_registers *regs, unsigned s)
{
        if (!ins)
                return 0;

        bi_index src = ins->src[s];

        if (src.type == BI_INDEX_REGISTER)
                return bi_get_src_slot(regs, src.value);
        else if (src.type == BI_INDEX_PASS)
                return src.value;
        else if (bi_is_null(src) && ins->op == BI_OPCODE_ZS_EMIT && s < 2)
                return BIFROST_SRC_STAGE;
        else {
                /* TODO make safer */
                return BIFROST_SRC_STAGE;
        }
}

static struct bi_packed_bundle
bi_pack_bundle(bi_clause *clause, bi_bundle bundle, bi_bundle prev, bool first_bundle, gl_shader_stage stage)
{
        bi_assign_slots(&bundle, &prev);
        bi_assign_fau_idx(clause, &bundle);
        bundle.regs.first_instruction = first_bundle;

        bi_flip_slots(&bundle.regs);

        bool sr_read = bundle.add &&
                bi_opcode_props[(bundle.add)->op].sr_read;

        uint64_t reg = bi_pack_registers(bundle.regs);
        uint64_t fma = bi_pack_fma(bundle.fma,
                        bi_get_src_new(bundle.fma, &bundle.regs, 0),
                        bi_get_src_new(bundle.fma, &bundle.regs, 1),
                        bi_get_src_new(bundle.fma, &bundle.regs, 2),
                        bi_get_src_new(bundle.fma, &bundle.regs, 3));

        uint64_t add = bi_pack_add(bundle.add,
                        bi_get_src_new(bundle.add, &bundle.regs, sr_read + 0),
                        bi_get_src_new(bundle.add, &bundle.regs, sr_read + 1),
                        bi_get_src_new(bundle.add, &bundle.regs, sr_read + 2),
                        0);

        if (bundle.add) {
                bi_instr *add = bundle.add;

                bool sr_write = bi_opcode_props[add->op].sr_write;

                if (sr_read) {
                        assert(add->src[0].type == BI_INDEX_REGISTER);
                        clause->staging_register = add->src[0].value;

                        if (sr_write)
                                assert(bi_is_equiv(add->src[0], add->dest[0]));
                } else if (sr_write) {
                        assert(add->dest[0].type == BI_INDEX_REGISTER);
                        clause->staging_register = add->dest[0].value;
                }
        }

        struct bi_packed_bundle packed = {
                .lo = reg | (fma << 35) | ((add & 0b111111) << 58),
                .hi = add >> 6
        };

        return packed;
}

/* Packs the next two constants as a dedicated constant quadword at the end of
 * the clause, returning the number packed. There are two cases to consider:
 *
 * Case #1: Branching is not used. For a single constant copy the upper nibble
 * over, easy.
 *
 * Case #2: Branching is used. For a single constant, it suffices to set the
 * upper nibble to 4 and leave the latter constant 0, which matches what the
 * blob does.
 *
 * Extending to multiple constants is considerably more tricky and left for
 * future work.
 */

static unsigned
bi_pack_constants(bi_context *ctx, bi_clause *clause,
                unsigned index,
                struct util_dynarray *emission)
{
        /* After these two, are we done? Determines tag */
        bool done = clause->constant_count <= (index + 2);
        ASSERTED bool only = clause->constant_count <= (index + 1);

        /* Is the constant we're packing for a branch? */
        bool branches = clause->branch_constant && done;

        /* TODO: Pos */
        assert(index == 0 && clause->bundle_count == 1);
        assert(only);

        /* Compute branch offset instead of a dummy 0 */
        if (branches) {
                bi_instr *br = clause->bundles[clause->bundle_count - 1].add;
                assert(br && br->branch_target);

                /* Put it in the high place */
                int32_t qwords = bi_block_offset(ctx, clause, br->branch_target);
                int32_t bytes = qwords * 16;

                /* Copy so we get proper sign behaviour */
                uint32_t raw = 0;
                memcpy(&raw, &bytes, sizeof(raw));

                /* Clear off top bits for the magic bits */
                raw &= ~0xF0000000;

                /* Put in top 32-bits */
                clause->constants[index + 0] = ((uint64_t) raw) << 32ull;
        }

        uint64_t hi = clause->constants[index + 0] >> 60ull;

        struct bifrost_fmt_constant quad = {
                .pos = 0, /* TODO */
                .tag = done ? BIFROST_FMTC_FINAL : BIFROST_FMTC_CONSTANTS,
                .imm_1 = clause->constants[index + 0] >> 4,
                .imm_2 = ((hi < 8) ? (hi << 60ull) : 0) >> 4,
        };

        if (branches) {
                /* Branch offsets are less than 60-bits so this should work at
                 * least for now */
                quad.imm_1 |= (4ull << 60ull) >> 4;
                assert (hi == 0);
        }

        /* XXX: On G71, Connor observed that the difference of the top 4 bits
         * of the second constant with the first must be less than 8, otherwise
         * we have to swap them. On G52, I'm able to reproduce a similar issue
         * but with a different workaround (modeled above with a single
         * constant, unclear how to workaround for multiple constants.) Further
         * investigation needed. Possibly an errata. XXX */

        util_dynarray_append(emission, struct bifrost_fmt_constant, quad);

        return 2;
}

static void
bi_pack_clause(bi_context *ctx, bi_clause *clause,
                bi_clause *next_1, bi_clause *next_2,
                struct util_dynarray *emission, gl_shader_stage stage,
                bool tdd)
{
        /* TODO After the deadline lowering */
        bi_lower_cubeface2(ctx, &clause->bundles[0]);

        struct bi_packed_bundle ins_1 = bi_pack_bundle(clause, clause->bundles[0], clause->bundles[0], true, stage);
        assert(clause->bundle_count == 1);

        /* State for packing constants throughout */
        unsigned constant_index = 0;

        struct bifrost_fmt1 quad_1 = {
                .tag = clause->constant_count ? BIFROST_FMT1_CONSTANTS : BIFROST_FMT1_FINAL,
                .header = bi_pack_header(clause, next_1, next_2, tdd),
                .ins_1 = ins_1.lo,
                .ins_2 = ins_1.hi & ((1 << 11) - 1),
                .ins_0 = (ins_1.hi >> 11) & 0b111,
        };

        util_dynarray_append(emission, struct bifrost_fmt1, quad_1);

        /* Pack the remaining constants */

        while (constant_index < clause->constant_count) {
                constant_index += bi_pack_constants(ctx, clause,
                                constant_index, emission);
        }
}

static bi_clause *
bi_next_clause(bi_context *ctx, pan_block *block, bi_clause *clause)
{
        /* Try the first clause in this block if we're starting from scratch */
        if (!clause && !list_is_empty(&((bi_block *) block)->clauses))
                return list_first_entry(&((bi_block *) block)->clauses, bi_clause, link);

        /* Try the next clause in this block */
        if (clause && clause->link.next != &((bi_block *) block)->clauses)
                return list_first_entry(&(clause->link), bi_clause, link);

        /* Try the next block, or the one after that if it's empty, etc .*/
        pan_block *next_block = pan_next_block(block);

        bi_foreach_block_from(ctx, next_block, block) {
                bi_block *blk = (bi_block *) block;

                if (!list_is_empty(&blk->clauses))
                        return list_first_entry(&(blk->clauses), bi_clause, link);
        }

        return NULL;
}

/* We should terminate discarded threads if there may be discarded threads (a
 * fragment shader) and helper invocations are not used. Further logic may be
 * required for future discard/demote differentiation
 */

static bool
bi_terminate_discarded_threads(bi_context *ctx)
{
        if (ctx->stage == MESA_SHADER_FRAGMENT)
                return !ctx->nir->info.fs.needs_quad_helper_invocations;
        else
                return false;
}

static void
bi_collect_blend_ret_addr(bi_context *ctx, struct util_dynarray *emission,
                          const bi_clause *clause)
{
        /* No need to collect return addresses when we're in a blend shader. */
        if (ctx->is_blend)
                return;

        const bi_bundle *bundle = &clause->bundles[clause->bundle_count - 1];
        const bi_instr *ins = bundle->add;

        if (!ins || ins->op != BI_OPCODE_BLEND)
                return;

        /* We don't support non-terminal blend instructions yet.
         * That would requires fixing blend shaders to restore the registers
         * they use before jumping back to the fragment shader, which is
         * currently not supported.
         */
        assert(0);

#if 0
        assert(ins->blend_location < ARRAY_SIZE(ctx->blend_ret_offsets));
        assert(!ctx->blend_ret_offsets[ins->blend_location]);
        ctx->blend_ret_offsets[ins->blend_location] =
                util_dynarray_num_elements(emission, uint8_t);
        assert(!(ctx->blend_ret_offsets[ins->blend_location] & 0x7));
#endif
}

void
bi_pack(bi_context *ctx, struct util_dynarray *emission)
{
        bool tdd = bi_terminate_discarded_threads(ctx);

        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;

                /* Passthrough the first clause of where we're branching to for
                 * the last clause of the block (the clause with the branch) */

                bi_clause *succ_clause = block->base.successors[1] ?
                        bi_next_clause(ctx, block->base.successors[0], NULL) : NULL;

                bi_foreach_clause_in_block(block, clause) {
                        bool is_last = clause->link.next == &block->clauses;

                        bi_clause *next = bi_next_clause(ctx, _block, clause);
                        bi_clause *next_2 = is_last ? succ_clause : NULL;

                        bi_pack_clause(ctx, clause, next, next_2, emission, ctx->stage, tdd);

                        if (!is_last)
                                bi_collect_blend_ret_addr(ctx, emission, clause);
                }
        }
}
