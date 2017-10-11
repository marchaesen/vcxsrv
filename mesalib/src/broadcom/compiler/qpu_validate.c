/*
 * Copyright © 2014 Broadcom
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * @file
 *
 * Validates the QPU instruction sequence after register allocation and
 * scheduling.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "v3d_compiler.h"
#include "qpu/qpu_disasm.h"

struct v3d_qpu_validate_state {
        struct v3d_compile *c;
        const struct v3d_qpu_instr *last;
        int ip;
        int last_sfu_write;
};

static void
fail_instr(struct v3d_qpu_validate_state *state, const char *msg)
{
        struct v3d_compile *c = state->c;

        fprintf(stderr, "v3d_qpu_validate at ip %d: %s:\n", state->ip, msg);

        int dump_ip = 0;
        vir_for_each_inst_inorder(inst, c) {
                v3d_qpu_dump(c->devinfo, &inst->qpu);

                if (dump_ip++ == state->ip)
                        fprintf(stderr, " *** ERROR ***");

                fprintf(stderr, "\n");
        }

        fprintf(stderr, "\n");
        abort();
}

static bool
qpu_magic_waddr_matches(const struct v3d_qpu_instr *inst,
                        bool (*predicate)(enum v3d_qpu_waddr waddr))
{
        if (inst->type == V3D_QPU_INSTR_TYPE_ALU)
                return false;

        if (inst->alu.add.op != V3D_QPU_A_NOP &&
            inst->alu.add.magic_write &&
            predicate(inst->alu.add.waddr))
                return true;

        if (inst->alu.mul.op != V3D_QPU_M_NOP &&
            inst->alu.mul.magic_write &&
            predicate(inst->alu.mul.waddr))
                return true;

        return false;
}

static void
qpu_validate_inst(struct v3d_qpu_validate_state *state, struct qinst *qinst)
{
        const struct v3d_qpu_instr *inst = &qinst->qpu;

        if (inst->type != V3D_QPU_INSTR_TYPE_ALU)
                return;

        /* LDVARY writes r5 two instructions later and LDUNIF writes
         * r5 one instruction later, which is illegal to have
         * together.
         */
        if (state->last && state->last->sig.ldvary && inst->sig.ldunif) {
                fail_instr(state, "LDUNIF after a LDVARY");
        }

        int tmu_writes = 0;
        int sfu_writes = 0;
        int vpm_writes = 0;
        int tlb_writes = 0;
        int tsy_writes = 0;

        if (inst->alu.add.op != V3D_QPU_A_NOP) {
                if (inst->alu.add.magic_write) {
                        if (v3d_qpu_magic_waddr_is_tmu(inst->alu.add.waddr))
                                tmu_writes++;
                        if (v3d_qpu_magic_waddr_is_sfu(inst->alu.add.waddr))
                                sfu_writes++;
                        if (v3d_qpu_magic_waddr_is_vpm(inst->alu.add.waddr))
                                vpm_writes++;
                        if (v3d_qpu_magic_waddr_is_tlb(inst->alu.add.waddr))
                                tlb_writes++;
                        if (v3d_qpu_magic_waddr_is_tsy(inst->alu.add.waddr))
                                tsy_writes++;
                }
        }

        if (inst->alu.mul.op != V3D_QPU_M_NOP) {
                if (inst->alu.mul.magic_write) {
                        if (v3d_qpu_magic_waddr_is_tmu(inst->alu.mul.waddr))
                                tmu_writes++;
                        if (v3d_qpu_magic_waddr_is_sfu(inst->alu.mul.waddr))
                                sfu_writes++;
                        if (v3d_qpu_magic_waddr_is_vpm(inst->alu.mul.waddr))
                                vpm_writes++;
                        if (v3d_qpu_magic_waddr_is_tlb(inst->alu.mul.waddr))
                                tlb_writes++;
                        if (v3d_qpu_magic_waddr_is_tsy(inst->alu.mul.waddr))
                                tsy_writes++;
                }
        }

        (void)qpu_magic_waddr_matches; /* XXX */

        /* SFU r4 results come back two instructions later.  No doing
         * r4 read/writes or other SFU lookups until it's done.
         */
        if (state->ip - state->last_sfu_write < 2) {
                if (v3d_qpu_uses_mux(inst, V3D_QPU_MUX_R4))
                        fail_instr(state, "R4 read too soon after SFU");

                if (v3d_qpu_writes_r4(inst))
                        fail_instr(state, "R4 write too soon after SFU");

                if (sfu_writes)
                        fail_instr(state, "SFU write too soon after SFU");
        }

        /* XXX: The docs say VPM can happen with the others, but the simulator
         * disagrees.
         */
        if (tmu_writes +
            sfu_writes +
            vpm_writes +
            tlb_writes +
            tsy_writes +
            inst->sig.ldtmu +
            inst->sig.ldtlb +
            inst->sig.ldvpm +
            inst->sig.ldtlbu > 1) {
                fail_instr(state,
                           "Only one of [TMU, SFU, TSY, TLB read, VPM] allowed");
        }

        if (sfu_writes)
                state->last_sfu_write = state->ip;
}

static void
qpu_validate_block(struct v3d_qpu_validate_state *state, struct qblock *block)
{
        vir_for_each_inst(qinst, block) {
                qpu_validate_inst(state, qinst);

                state->last = &qinst->qpu;
                state->ip++;
        }
}

/**
 * Checks for the instruction restrictions from page 37 ("Summary of
 * Instruction Restrictions").
 */
void
qpu_validate(struct v3d_compile *c)
{
        /* We don't want to do validation in release builds, but we want to
         * keep compiling the validation code to make sure it doesn't get
         * broken.
         */
#ifndef DEBUG
        return;
#endif

        struct v3d_qpu_validate_state state = {
                .c = c,
                .last_sfu_write = -10,
                .ip = 0,
        };

        vir_for_each_block(block, c) {
                qpu_validate_block(&state, block);
        }
}
