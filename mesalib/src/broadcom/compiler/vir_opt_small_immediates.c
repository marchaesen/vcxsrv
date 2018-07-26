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
 * @file v3d_opt_small_immediates.c
 *
 * Turns references to small constant uniform values into small immediates
 * fields.
 */

#include "v3d_compiler.h"

static bool debug;

bool
vir_opt_small_immediates(struct v3d_compile *c)
{
        bool progress = false;

        vir_for_each_inst_inorder(inst, c) {
                if (inst->qpu.type != V3D_QPU_INSTR_TYPE_ALU)
                        continue;

                /* The small immediate value sits in the raddr B field, so we
                 * can't have 2 small immediates in one instruction (unless
                 * they're the same value, but that should be optimized away
                 * elsewhere).
                 */
                bool uses_small_imm = false;
                for (int i = 0; i < vir_get_nsrc(inst); i++) {
                        if (inst->src[i].file == QFILE_SMALL_IMM)
                                uses_small_imm = true;
                }
                if (uses_small_imm)
                        continue;

                for (int i = 0; i < vir_get_nsrc(inst); i++) {
                        struct qreg src = vir_follow_movs(c, inst->src[i]);

                        if (src.file != QFILE_UNIF ||
                            c->uniform_contents[src.index] !=
                            QUNIFORM_CONSTANT) {
                                continue;
                        }

                        if (vir_has_implicit_uniform(inst) &&
                            i == vir_get_implicit_uniform_src(inst)) {
                                /* No turning the implicit uniform read into
                                 * an immediate.
                                 */
                                continue;
                        }

                        /* Check if the uniform is suitable as a small
                         * immediate.
                         */
                        uint32_t imm = c->uniform_data[src.index];
                        uint32_t packed;
                        if (!v3d_qpu_small_imm_pack(c->devinfo, imm, &packed))
                                continue;

                        /* Check that we don't have any other signals already
                         * that would be incompatible with small_imm.
                         */
                        struct v3d_qpu_sig new_sig = inst->qpu.sig;
                        uint32_t sig_packed;
                        new_sig.small_imm = true;
                        if (!v3d_qpu_sig_pack(c->devinfo, &new_sig, &sig_packed))
                                continue;

                        if (debug) {
                                fprintf(stderr, "opt_small_immediate() from: ");
                                vir_dump_inst(c, inst);
                                fprintf(stderr, "\n");
                        }
                        inst->qpu.sig.small_imm = true;
                        inst->qpu.raddr_b = packed;

                        inst->src[i].file = QFILE_SMALL_IMM;
                        inst->src[i].index = imm;
                        if (debug) {
                                fprintf(stderr, "to: ");
                                vir_dump_inst(c, inst);
                                fprintf(stderr, "\n");
                        }
                        progress = true;
                        break;
                }
        }

        return progress;
}
