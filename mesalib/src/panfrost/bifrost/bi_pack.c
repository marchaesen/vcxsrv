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
#include "bi_print.h"
#include "bi_generated_pack.h"

#define RETURN_PACKED(str) { \
        uint64_t temp = 0; \
        memcpy(&temp, &str, sizeof(str)); \
        return temp; \
}

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
                .next_clause_prefetch = clause->next_clause_prefetch,
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
bi_lookup_constant(bi_clause *clause, uint64_t cons, bool *hi, bool b64)
{
        uint64_t want = (cons >> 4);

        for (unsigned i = 0; i < clause->constant_count; ++i) {
                /* Only check top 60-bits since that's what's actually embedded
                 * in the clause, the bottom 4-bits are bundle-inline */

                uint64_t candidates[2] = {
                        clause->constants[i] >> 4,
                        clause->constants[i] >> 36
                };

                /* For <64-bit mode, we treat lo/hi separately */

                if (!b64)
                        candidates[0] &= (0xFFFFFFFF >> 4);

                if (candidates[0] == want)
                        return i;

                if (candidates[1] == want && !b64) {
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
                         bi_instruction *ins,
                         bool assigned,
                         bool fast_zero)
{
        if (!ins)
                return assigned;

        if (ins->type == BI_BRANCH && clause->branch_constant) {
                /* By convention branch constant is last */
                unsigned idx = clause->constant_count - 1;

                /* We can only jump to clauses which are qword aligned so the
                 * bottom 4-bits of the offset are necessarily 0 */
                unsigned lo = 0;

                /* Build the constant */
                unsigned C = bi_constant_field(idx) | lo;

                if (assigned && regs->fau_idx != C)
                        unreachable("Mismatched fau_idx: branch");

                regs->fau_idx = C;
                return true;
        }

        bi_foreach_src(ins, s) {
                if (s == 0 && (ins->type == BI_LOAD_VAR_ADDRESS || ins->type == BI_LOAD_ATTR)) continue;
                if (s == 1 && (ins->type == BI_BRANCH)) continue;

                if (ins->src[s] & BIR_INDEX_CONSTANT) {
                        /* Let direct addresses through */
                        if (ins->type == BI_LOAD_VAR)
                                continue;

                        bool hi = false;
                        bool b64 = nir_alu_type_get_type_size(ins->src_types[s]) > 32;
                        uint64_t cons = bi_get_immediate(ins, s);
                        unsigned idx = bi_lookup_constant(clause, cons, &hi, b64);
                        unsigned lo = clause->constants[idx] & 0xF;
                        unsigned f = bi_constant_field(idx) | lo;

                        if (assigned && regs->fau_idx != f)
                                unreachable("Mismatched uniform/const field: imm");

                        regs->fau_idx = f;
                        ins->src[s] = BIR_INDEX_PASS | (hi ? BIFROST_SRC_FAU_HI : BIFROST_SRC_FAU_LO);
                        assigned = true;
                } else if (ins->src[s] & BIR_INDEX_ZERO && (ins->type == BI_LOAD_UNIFORM || ins->type == BI_LOAD_VAR)) {
                        /* XXX: HACK UNTIL WE HAVE HI MATCHING DUE TO OVERFLOW XXX */
                        ins->src[s] = BIR_INDEX_PASS | BIFROST_SRC_FAU_HI;
                } else if (ins->src[s] & BIR_INDEX_ZERO && !fast_zero) {
                        /* FMAs have a fast zero slot, ADD needs to use the
                         * uniform/const slot's special 0 mode handled here */
                        unsigned f = 0;

                        if (assigned && regs->fau_idx != f)
                                unreachable("Mismatched uniform/const field: 0");

                        regs->fau_idx = f;
                        ins->src[s] = BIR_INDEX_PASS | BIFROST_SRC_FAU_LO;
                        assigned = true;
                } else if (ins->src[s] & BIR_INDEX_ZERO && fast_zero) {
                        ins->src[s] = BIR_INDEX_PASS | BIFROST_SRC_STAGE;
                } else if (ins->src[s] & BIR_INDEX_BLEND) {
                        unsigned rt = ins->blend_location;

                        assert(rt <= 7);
                        assert((ins->src[s] & ~BIR_SPECIAL) == BIFROST_SRC_FAU_HI ||
                               (ins->src[s] & ~BIR_SPECIAL) == BIFROST_SRC_FAU_LO);
                        ins->src[s] = BIR_INDEX_PASS | (ins->src[s] & ~BIR_SPECIAL);
                        if (assigned && regs->fau_idx != (8 | rt))
                                unreachable("Mismatched FAU index");

                        regs->fau_idx = 8 | rt;
                        assigned = true;
                } else if (s & BIR_INDEX_UNIFORM) {
                        unreachable("Push uniforms not implemented yet");
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
bi_assign_slot_read(bi_registers *regs, unsigned src)
{
        /* We only assign for registers */
        if (!(src & BIR_INDEX_REGISTER))
                return;

        unsigned reg = src & ~BIR_INDEX_REGISTER;

        /* Check if we already assigned the slot */
        for (unsigned i = 0; i <= 1; ++i) {
                if (regs->slot[i] == reg && regs->enabled[i])
                        return;
        }

        if (regs->slot[2] == reg && regs->slot23.slot2 == BIFROST_OP_READ)
                return;

        /* Assign it now */

        for (unsigned i = 0; i <= 1; ++i) {
                if (!regs->enabled[i]) {
                        regs->slot[i] = reg;
                        regs->enabled[i] = true;
                        return;
                }
        }

        if (!regs->slot23.slot3) {
                regs->slot[2] = reg;
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

        unsigned read_dreg = now->add &&
                bi_class_props[now->add->type] & BI_DATA_REG_SRC;

        unsigned write_dreg = prev->add &&
                bi_class_props[prev->add->type] & BI_DATA_REG_DEST;

        /* First, assign reads */

        if (now->fma)
                bi_foreach_src(now->fma, src)
                        bi_assign_slot_read(&now->regs, now->fma->src[src]);

        if (now->add) {
                bi_foreach_src(now->add, src) {
                        if (!(src == 0 && read_dreg))
                                bi_assign_slot_read(&now->regs, now->add->src[src]);
                }
        }

        /* Next, assign writes */

        if (prev->add && prev->add->dest & BIR_INDEX_REGISTER && !write_dreg) {
                now->regs.slot[3] = prev->add->dest & ~BIR_INDEX_REGISTER;
                now->regs.slot23.slot3 = BIFROST_OP_WRITE;
        }

        if (prev->fma && prev->fma->dest & BIR_INDEX_REGISTER) {
                unsigned r = prev->fma->dest & ~BIR_INDEX_REGISTER;

                if (now->regs.slot23.slot3) {
                        /* Scheduler constraint: cannot read 3 and write 2 */
                        assert(!now->regs.slot23.slot2);
                        now->regs.slot[2] = r;
                        now->regs.slot23.slot2 = BIFROST_OP_WRITE;
                } else {
                        now->regs.slot[3] = r;
                        now->regs.slot23.slot3 = BIFROST_OP_WRITE;
                        now->regs.slot23.slot3_fma = true;
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

static unsigned
bi_pack_fma(bi_clause *clause, bi_bundle bundle, bi_registers *regs)
{
        if (!bundle.fma)
                return pan_pack_fma_nop_i32(clause, NULL, regs);

        bool f16 = bundle.fma->dest_type == nir_type_float16;
        bool f32 = bundle.fma->dest_type == nir_type_float32;
        bool u32 = bundle.fma->dest_type == nir_type_uint32;
        bool u16 = bundle.fma->dest_type == nir_type_uint16;
        ASSERTED bool u8 = bundle.fma->dest_type == nir_type_uint8;
        bool s32 = bundle.fma->dest_type == nir_type_int32;
        bool s16 = bundle.fma->dest_type == nir_type_int16;
        ASSERTED bool s8 = bundle.fma->dest_type == nir_type_int8;

        bool src0_f16 = bundle.fma->src_types[0] == nir_type_float16;
        bool src0_f32 = bundle.fma->src_types[0] == nir_type_float32;
        bool src0_u16 = bundle.fma->src_types[0] == nir_type_uint16;
        bool src0_s16 = bundle.fma->src_types[0] == nir_type_int16;
        bool src0_s8 = bundle.fma->src_types[0] == nir_type_int8;
        bool src0_u8 = bundle.fma->src_types[0] == nir_type_uint8;

        enum bi_cond cond = bundle.fma->cond;
        bool typeless_cond = (cond == BI_COND_EQ) || (cond == BI_COND_NE);

        switch (bundle.fma->type) {
        case BI_ADD:
                if (bundle.fma->dest_type == nir_type_float32)
                        return pan_pack_fma_fadd_f32(clause, bundle.fma, regs);
                else if (bundle.fma->dest_type == nir_type_float16)
                        return pan_pack_fma_fadd_v2f16(clause, bundle.fma, regs);

                unreachable("TODO");
        case BI_CMP:
                assert (src0_f16 || src0_f32);

                if (src0_f32)
                        return pan_pack_fma_fcmp_f32(clause, bundle.fma, regs);
                else
                        return pan_pack_fma_fcmp_v2f16(clause, bundle.fma, regs);
        case BI_BITWISE:
                if (bundle.fma->op.bitwise == BI_BITWISE_AND) {
                        if (u32 || s32) {
                                return bundle.fma->bitwise.rshift ?
                                        pan_pack_fma_rshift_and_i32(clause, bundle.fma, regs) :
                                        pan_pack_fma_lshift_and_i32(clause, bundle.fma, regs);
                        } else if (u16 || s16) {
                                return bundle.fma->bitwise.rshift ?
                                        pan_pack_fma_rshift_and_v2i16(clause, bundle.fma, regs) :
                                        pan_pack_fma_lshift_and_v2i16(clause, bundle.fma, regs);
                        } else {
                                assert(u8 || s8);
                                return bundle.fma->bitwise.rshift ?
                                        pan_pack_fma_rshift_and_v4i8(clause, bundle.fma, regs) :
                                        pan_pack_fma_lshift_and_v4i8(clause, bundle.fma, regs);
                        }

                } else if (bundle.fma->op.bitwise == BI_BITWISE_OR) {
                        if (u32 || s32) {
                                return bundle.fma->bitwise.rshift ?
                                        pan_pack_fma_rshift_or_i32(clause, bundle.fma, regs) :
                                        pan_pack_fma_lshift_or_i32(clause, bundle.fma, regs);
                        } else if (u16 || s16) {
                                return bundle.fma->bitwise.rshift ?
                                        pan_pack_fma_rshift_or_v2i16(clause, bundle.fma, regs) :
                                        pan_pack_fma_lshift_or_v2i16(clause, bundle.fma, regs);
                        } else {
                                assert(u8 || s8);
                                return bundle.fma->bitwise.rshift ?
                                        pan_pack_fma_rshift_or_v4i8(clause, bundle.fma, regs) :
                                        pan_pack_fma_lshift_or_v4i8(clause, bundle.fma, regs);
                        }
                } else {
                        assert(bundle.fma->op.bitwise == BI_BITWISE_XOR);

                        if (u32 || s32) {
                                return bundle.fma->bitwise.rshift ?
                                        pan_pack_fma_rshift_xor_i32(clause, bundle.fma, regs) :
                                        pan_pack_fma_lshift_xor_i32(clause, bundle.fma, regs);
                        } else if (u16 || s16) {
                                return bundle.fma->bitwise.rshift ?
                                        pan_pack_fma_rshift_xor_v2i16(clause, bundle.fma, regs) :
                                        pan_pack_fma_lshift_xor_v2i16(clause, bundle.fma, regs);
                        } else {
                                assert(u8 || s8);
                                return bundle.fma->bitwise.rshift ?
                                        pan_pack_fma_rshift_xor_v4i8(clause, bundle.fma, regs) :
                                        pan_pack_fma_lshift_xor_v4i8(clause, bundle.fma, regs);
                        }
                }
        case BI_CONVERT:
                if (src0_s8) {
                        assert(s32);
                        return pan_pack_fma_s8_to_s32(clause, bundle.fma, regs);
                } else if (src0_u8) {
                        assert(u32);
                        return pan_pack_fma_u8_to_u32(clause, bundle.fma, regs);
                } else if (src0_s16) {
                        assert(s32);
                        return pan_pack_fma_s16_to_s32(clause, bundle.fma, regs);
                } else if (src0_u16) {
                        assert(u32);
                        return pan_pack_fma_u16_to_u32(clause, bundle.fma, regs);
                } else if (src0_f16) {
                        assert(f32);
                        return pan_pack_fma_f16_to_f32(clause, bundle.fma, regs);
                } else if (src0_f32) {
                        assert(f16);
                        return pan_pack_fma_v2f32_to_v2f16(clause, bundle.fma, regs);
                }

                unreachable("Invalid FMA convert");
        case BI_CSEL:
                if (f32)
                        return pan_pack_fma_csel_f32(clause, bundle.fma, regs);
                else if (f16)
                        return pan_pack_fma_csel_v2f16(clause, bundle.fma, regs);
                else if ((u32 || s32) && typeless_cond)
                        return pan_pack_fma_csel_i32(clause, bundle.fma, regs);
                else if ((u16 || s16) && typeless_cond)
                        return pan_pack_fma_csel_v2i16(clause, bundle.fma, regs);
                else if (u32)
                        return pan_pack_fma_csel_u32(clause, bundle.fma, regs);
                else if (u16)
                        return pan_pack_fma_csel_v2u16(clause, bundle.fma, regs);
                else if (s32)
                        return pan_pack_fma_csel_s32(clause, bundle.fma, regs);
                else if (s16)
                        return pan_pack_fma_csel_v2s16(clause, bundle.fma, regs);
                else
                        unreachable("Invalid csel type");
        case BI_FMA:
                if (bundle.fma->dest_type == nir_type_float32) {
                        if (bundle.fma->op.mscale)
                                return pan_pack_fma_fma_rscale_f32(clause, bundle.fma, regs);
                        else
                                return pan_pack_fma_fma_f32(clause, bundle.fma, regs);
                } else {
                        assert(bundle.fma->dest_type == nir_type_float16);

                        if (bundle.fma->op.mscale)
                                return pan_pack_fma_fma_rscale_v2f16(clause, bundle.fma, regs);
                        else
                                return pan_pack_fma_fma_v2f16(clause, bundle.fma, regs);
                }
        case BI_FREXP:
                assert(src0_f32 || src0_f16);

                if (src0_f32)
                        return pan_pack_fma_frexpe_f32(clause, bundle.fma, regs);
                else
                        return pan_pack_fma_frexpe_v2f16(clause, bundle.fma, regs);
        case BI_IMATH:
                /* XXX: Only 32-bit, with carries/borrows forced */
                assert(s32 || u32);

                if (bundle.fma->op.imath == BI_IMATH_ADD)
                        return pan_pack_fma_iaddc_i32(clause, bundle.fma, regs);
                else
                        return pan_pack_fma_isubb_i32(clause, bundle.fma, regs);
        case BI_MOV:
                return pan_pack_fma_mov_i32(clause, bundle.fma, regs);
        case BI_SELECT:
                if (nir_alu_type_get_type_size(bundle.fma->src_types[0]) == 16) {
                        return pan_pack_fma_mkvec_v2i16(clause, bundle.fma, regs);
                } else {
                        assert(nir_alu_type_get_type_size(bundle.fma->src_types[0]) == 8);
                        return pan_pack_fma_mkvec_v4i8(clause, bundle.fma, regs);
                }
        case BI_ROUND:
                assert(f16 || f32);

                if (f16)
                        return pan_pack_fma_fround_v2f16(clause, bundle.fma, regs);
                else
                        return pan_pack_fma_fround_f32(clause, bundle.fma, regs);
        case BI_REDUCE_FMA:
                assert(src0_f32 && f32);
                return pan_pack_fma_fadd_lscale_f32(clause, bundle.fma, regs);
        case BI_IMUL:
                return pan_pack_fma_imul_i32(clause, bundle.fma, regs);
        default:
                unreachable("Cannot encode class as FMA");
        }
}

static unsigned
bi_pack_add_branch_cond(bi_instruction *ins, bi_registers *regs)
{
        assert(ins->cond == BI_COND_EQ);
        assert(ins->src[1] == BIR_INDEX_ZERO);

        unsigned zero_ctrl = 0;
        unsigned size = nir_alu_type_get_type_size(ins->src_types[0]);

        if (size == 16) {
                /* See BR_SIZE_ZERO swizzle disassembly */
                zero_ctrl = ins->swizzle[0][0] ? 1 : 2;
        } else {
                assert(size == 32);
        }

        /* EQ swap to NE */
        bool slot_swapped = false;

        struct bifrost_branch pack = {
                .src0 = bi_get_src(ins, regs, 0),
                .src1 = (zero_ctrl << 1) | !slot_swapped,
                .cond = BR_COND_EQ,
                .size = BR_SIZE_ZERO,
                .op = BIFROST_ADD_OP_BRANCH
        };

        if (ins->branch_target) {
                /* We assigned the constant slot to fetch the branch offset so
                 * we can just passthrough here. We put in the HI slot to match
                 * the blob since that's where the magic flags end up
                 */
                assert(!ins->src[2]);
                pack.src2 = BIFROST_SRC_FAU_HI;
        } else {
                pack.src2 = bi_get_src(ins, regs, 2);
        }

        RETURN_PACKED(pack);
}

static unsigned
bi_pack_add_branch_uncond(bi_instruction *ins, bi_registers *regs)
{
        struct bifrost_branch pack = {
                /* It's unclear what these bits actually mean */
                .src0 = BIFROST_SRC_FAU_LO,
                .src1 = BIFROST_SRC_PASS_FMA,

                /* All ones in fact */
                .cond = (BR_ALWAYS & 0x7),
                .size = (BR_ALWAYS >> 3),
                .op = BIFROST_ADD_OP_BRANCH
        };

        if (ins->branch_target) {
                /* Offset is passed as a PC-relative offset through an
                 * embedded constant.
                 */
                assert(!ins->src[2]);
                pack.src2 = BIFROST_SRC_FAU_HI;
        } else {
                pack.src2 = bi_get_src(ins, regs, 2);
        }

        RETURN_PACKED(pack);
}

static unsigned
bi_pack_add_branch(bi_instruction *ins, bi_registers *regs)
{
        if (ins->cond == BI_COND_ALWAYS)
                return bi_pack_add_branch_uncond(ins, regs);
        else
                return bi_pack_add_branch_cond(ins, regs);
}

static unsigned
bi_pack_add(bi_clause *clause, bi_bundle bundle, bi_registers *regs, gl_shader_stage stage)
{
        if (!bundle.add)
                return pan_pack_add_nop_i32(clause, NULL, regs);

        bool f16 = bundle.add->dest_type == nir_type_float16;
        bool f32 = bundle.add->dest_type == nir_type_float32;
        bool u32 = bundle.add->dest_type == nir_type_uint32;
        bool u16 = bundle.add->dest_type == nir_type_uint16;
        bool s32 = bundle.add->dest_type == nir_type_int32;
        bool s16 = bundle.add->dest_type == nir_type_int16;

        bool src0_f16 = bundle.add->src_types[0] == nir_type_float16;
        bool src0_f32 = bundle.add->src_types[0] == nir_type_float32;
        bool src0_u32 = bundle.add->src_types[0] == nir_type_uint32;
        bool src0_u16 = bundle.add->src_types[0] == nir_type_uint16;
        bool src0_u8 = bundle.add->src_types[0] == nir_type_uint8;
        bool src0_s32 = bundle.add->src_types[0] == nir_type_int32;
        bool src0_s16 = bundle.add->src_types[0] == nir_type_int16;
        bool src0_s8 = bundle.add->src_types[0] == nir_type_int8;

        unsigned sz = nir_alu_type_get_type_size(bundle.add->dest_type);
        enum bi_cond cond = bundle.add->cond;
        bool typeless_cond = (cond == BI_COND_EQ) || (cond == BI_COND_NE);

        switch (bundle.add->type) {
        case BI_ADD:
                if (bundle.add->dest_type == nir_type_float32)
                        return pan_pack_add_fadd_f32(clause, bundle.add, regs);
                else if (bundle.add->dest_type == nir_type_float16)
                        return pan_pack_add_fadd_v2f16(clause, bundle.add, regs);

                unreachable("TODO");
        case BI_ATEST:
                return pan_pack_add_atest(clause, bundle.add, regs);
        case BI_BRANCH:
                return bi_pack_add_branch(bundle.add, regs);
        case BI_CMP:
                if (src0_f32)
                        return pan_pack_add_fcmp_f32(clause, bundle.add, regs);
                else if (src0_f16)
                        return pan_pack_add_fcmp_v2f16(clause, bundle.add, regs);
                else if ((src0_u32 || src0_s32) && typeless_cond)
                        return pan_pack_add_icmp_i32(clause, bundle.add, regs);
                else if ((src0_u16 || src0_s16) && typeless_cond)
                        return pan_pack_add_icmp_v2i16(clause, bundle.add, regs);
                else if ((src0_u8 || src0_s8) && typeless_cond)
                        return pan_pack_add_icmp_v4i8(clause, bundle.add, regs);
                else if (src0_u32)
                        return pan_pack_add_icmp_u32(clause, bundle.add, regs);
                else if (src0_u16)
                        return pan_pack_add_icmp_v2u16(clause, bundle.add, regs);
                else if (src0_u8)
                        return pan_pack_add_icmp_v4u8(clause, bundle.add, regs);
                else if (src0_s32)
                        return pan_pack_add_icmp_s32(clause, bundle.add, regs);
                else if (src0_s16)
                        return pan_pack_add_icmp_v2s16(clause, bundle.add, regs);
                else if (src0_s8)
                        return pan_pack_add_icmp_v4s8(clause, bundle.add, regs);
                else
                        unreachable("Invalid cmp type");
        case BI_BLEND:
                return pan_pack_add_blend(clause, bundle.add, regs);
        case BI_BITWISE:
                unreachable("Packing todo");
        case BI_CONVERT:
                if (src0_f16 && s16)
                        return pan_pack_add_v2f16_to_v2s16(clause, bundle.add, regs);
                else if (src0_f16 && u16)
                        return pan_pack_add_v2f16_to_v2u16(clause, bundle.add, regs);
                else if (src0_f16 && s32)
                        return pan_pack_add_f16_to_s32(clause, bundle.add, regs);
                else if (src0_f16 && u32)
                        return pan_pack_add_f16_to_u32(clause, bundle.add, regs);
                else if (src0_s16 && f16)
                        return pan_pack_add_v2s16_to_v2f16(clause, bundle.add, regs);
                else if (src0_u16 && f16)
                        return pan_pack_add_v2u16_to_v2f16(clause, bundle.add, regs);
                else if (src0_s8  && s16)
                        return pan_pack_add_v2s8_to_v2s16(clause, bundle.add, regs);
                else if (src0_u8  && u16)
                        return pan_pack_add_v2u8_to_v2u16(clause, bundle.add, regs);
                else if (src0_s8  && f16)
                        return pan_pack_add_v2s8_to_v2f16(clause, bundle.add, regs);
                else if (src0_u8  && f16)
                        return pan_pack_add_v2u8_to_v2f16(clause, bundle.add, regs);
                else if (src0_f32 && s32)
                        return pan_pack_add_f32_to_s32(clause, bundle.add, regs);
                else if (src0_f32 && u32)
                        return pan_pack_add_f32_to_u32(clause, bundle.add, regs);
                else if (src0_s8  && s32)
                        return pan_pack_add_s8_to_s32(clause, bundle.add, regs);
                else if (src0_u8  && u32)
                        return pan_pack_add_u8_to_u32(clause, bundle.add, regs);
                else if (src0_s8  && f32)
                        return pan_pack_add_s8_to_f32(clause, bundle.add, regs);
                else if (src0_u8  && f32)
                        return pan_pack_add_u8_to_f32(clause, bundle.add, regs);
                else if (src0_s32 && f32)
                        return pan_pack_add_s32_to_f32(clause, bundle.add, regs);
                else if (src0_u32 && f32)
                        return pan_pack_add_u32_to_f32(clause, bundle.add, regs);
                else if (src0_s16 && s32)
                        return pan_pack_add_s16_to_s32(clause, bundle.add, regs);
                else if (src0_u16 && u32)
                        return pan_pack_add_u16_to_u32(clause, bundle.add, regs);
                else if (src0_s16 && f32)
                        return pan_pack_add_s16_to_f32(clause, bundle.add, regs);
                else if (src0_u16 && f32)
                        return pan_pack_add_u16_to_f32(clause, bundle.add, regs);
                else if (src0_f16 && f32)
                        return pan_pack_add_f16_to_f32(clause, bundle.add, regs);
                else if (src0_f32 && f16)
                        return pan_pack_add_v2f32_to_v2f16(clause, bundle.add, regs);
                else
                        unreachable("Invalid ADD convert");
        case BI_DISCARD:
                return pan_pack_add_discard_f32(clause, bundle.add, regs);
        case BI_FREXP:
                unreachable("Packing todo");
        case BI_IMATH:
                assert(sz == 8 || sz == 16 || sz == 32);

                if (bundle.add->op.imath == BI_IMATH_ADD) {
                        return (sz == 8) ? pan_pack_add_iadd_v4s8(clause, bundle.add, regs) :
                                (sz == 16) ? pan_pack_add_iadd_v2s16(clause, bundle.add, regs) :
                                pan_pack_add_iadd_s32(clause, bundle.add, regs);
                } else {
                        return (sz == 8) ? pan_pack_add_isub_v4s8(clause, bundle.add, regs) :
                                (sz == 16) ? pan_pack_add_isub_v2s16(clause, bundle.add, regs) :
                                pan_pack_add_isub_s32(clause, bundle.add, regs);
                }
        case BI_LOAD:
                unreachable("Packing todo");
        case BI_LOAD_ATTR:
                return pan_pack_add_ld_attr_imm(clause, bundle.add, regs);
        case BI_LOAD_UNIFORM:
                switch (bundle.add->vector_channels) {
                case 1: return pan_pack_add_load_i32(clause, bundle.add, regs);
                case 2: return pan_pack_add_load_i64(clause, bundle.add, regs);
                case 3: return pan_pack_add_load_i96(clause, bundle.add, regs);
                case 4: return pan_pack_add_load_i128(clause, bundle.add, regs);
                default: unreachable("Invalid channel count");
                }
        case BI_LOAD_VAR:
                if (bundle.add->src[0] & BIR_INDEX_CONSTANT) {
                        if (bi_get_immediate(bundle.add, 0) >= 20)
                                return pan_pack_add_ld_var_special(clause, bundle.add, regs);
                        else if (bundle.add->load_vary.flat)
                                return pan_pack_add_ld_var_flat_imm(clause, bundle.add, regs);
                        else
                                return pan_pack_add_ld_var_imm(clause, bundle.add, regs);
                } else {
                        if (bundle.add->load_vary.flat)
                                return pan_pack_add_ld_var_flat(clause, bundle.add, regs);
                        else
                                return pan_pack_add_ld_var(clause, bundle.add, regs);
                }
        case BI_LOAD_VAR_ADDRESS:
                return pan_pack_add_lea_attr_imm(clause, bundle.add, regs);
        case BI_LOAD_TILE:
                return pan_pack_add_ld_tile(clause, bundle.add, regs);
        case BI_MINMAX:
                if (bundle.add->op.minmax == BI_MINMAX_MIN) {
                        if (bundle.add->dest_type == nir_type_float32)
                                return pan_pack_add_fmin_f32(clause, bundle.add, regs);
                        else if (bundle.add->dest_type == nir_type_float16)
                                return pan_pack_add_fmin_v2f16(clause, bundle.add, regs);
                        unreachable("TODO");
                } else {
                        if (bundle.add->dest_type == nir_type_float32)
                                return pan_pack_add_fmax_f32(clause, bundle.add, regs);
                        else if (bundle.add->dest_type == nir_type_float16)
                                return pan_pack_add_fmax_v2f16(clause, bundle.add, regs);
                        unreachable("TODO");
                }
        case BI_MOV:
        case BI_STORE:
                unreachable("Packing todo");
        case BI_STORE_VAR:
                return pan_pack_add_st_cvt(clause, bundle.add, regs);
        case BI_SPECIAL:
                if (bundle.add->op.special == BI_SPECIAL_FRCP) {
                        return f16 ? pan_pack_add_frcp_f16(clause, bundle.add, regs) :
                                pan_pack_add_frcp_f32(clause, bundle.add, regs);
                } else if (bundle.add->op.special == BI_SPECIAL_FRSQ) {
                        return f16 ? pan_pack_add_frsq_f16(clause, bundle.add, regs) :
                                pan_pack_add_frsq_f32(clause, bundle.add, regs);
                } else if (bundle.add->op.special == BI_SPECIAL_EXP2_LOW) {
                        assert(!f16);
                        return pan_pack_add_fexp_f32(clause, bundle.add, regs);
                } else if (bundle.add->op.special == BI_SPECIAL_IABS) {
                        assert(bundle.add->src_types[0] == nir_type_int32);
                        return pan_pack_add_iabs_s32(clause, bundle.add, regs);
                }

                unreachable("Unknown special op");
        case BI_TABLE:
                assert(bundle.add->dest_type == nir_type_float32);
                return pan_pack_add_flogd_f32(clause, bundle.add, regs);
        case BI_SELECT:
                assert(nir_alu_type_get_type_size(bundle.add->src_types[0]) == 16);
                return pan_pack_add_mkvec_v2i16(clause, bundle.add, regs);
        case BI_TEXC:
                return pan_pack_add_texc(clause, bundle.add, regs);
        case BI_TEXC_DUAL:
                unreachable("Packing todo");
        case BI_TEXS:
                assert(f16 || f32);

                if (f16)
                        return pan_pack_add_texs_2d_f16(clause, bundle.add, regs);
                else
                        return pan_pack_add_texs_2d_f32(clause, bundle.add, regs);
case BI_ROUND:
                unreachable("Packing todo");
        default:
                unreachable("Cannot encode class as ADD");
        }
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

static struct bi_packed_bundle
bi_pack_bundle(bi_clause *clause, bi_bundle bundle, bi_bundle prev, bool first_bundle, gl_shader_stage stage)
{
        bi_assign_slots(&bundle, &prev);
        bi_assign_fau_idx(clause, &bundle);
        bundle.regs.first_instruction = first_bundle;

        bi_flip_slots(&bundle.regs);

        uint64_t reg = bi_pack_registers(bundle.regs);
        uint64_t fma = bi_pack_fma(clause, bundle, &bundle.regs);
        uint64_t add = bi_pack_add(clause, bundle, &bundle.regs, stage);

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
                bi_instruction *br = clause->bundles[clause->bundle_count - 1].add;
                assert(br && br->type == BI_BRANCH && br->branch_target);

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
                return !ctx->nir->info.fs.needs_helper_invocations;
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
        const bi_instruction *ins = bundle->add;

        if (!ins || ins->type != BI_BLEND)
                return;

        /* We don't support non-terminal blend instructions yet.
         * That would requires fixing blend shaders to restore the registers
         * they use before jumping back to the fragment shader, which is
         * currently not supported.
         */
        assert(0);

        assert(ins->blend_location < ARRAY_SIZE(ctx->blend_ret_offsets));
        assert(!ctx->blend_ret_offsets[ins->blend_location]);
        ctx->blend_ret_offsets[ins->blend_location] =
                util_dynarray_num_elements(emission, uint8_t);
        assert(!(ctx->blend_ret_offsets[ins->blend_location] & 0x7));
}

void
bi_pack(bi_context *ctx, struct util_dynarray *emission)
{
        bool tdd = bi_terminate_discarded_threads(ctx);

        util_dynarray_init(emission, NULL);

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
