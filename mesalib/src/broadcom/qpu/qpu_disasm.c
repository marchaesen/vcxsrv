/*
 * Copyright © 2016 Broadcom
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

#include <string.h>
#include <stdio.h>
#include "util/ralloc.h"

#include "broadcom/common/v3d_device_info.h"
#include "qpu_instr.h"
#include "qpu_disasm.h"

struct disasm_state {
        const struct v3d_device_info *devinfo;
        char *string;
        size_t offset;
};

static void
append(struct disasm_state *disasm, const char *fmt, ...)
{
        va_list args;
        va_start(args, fmt);
        ralloc_vasprintf_rewrite_tail(&disasm->string,
                                      &disasm->offset,
                                      fmt, args);
        va_end(args);
}

static void
pad_to(struct disasm_state *disasm, int n)
{
        /* FIXME: Do a single append somehow. */
        while (disasm->offset < n)
                append(disasm, " ");
}


static void
v3d_qpu_disasm_raddr(struct disasm_state *disasm,
                     const struct v3d_qpu_instr *instr, uint8_t mux)
{
        if (mux == V3D_QPU_MUX_A) {
                append(disasm, "rf%d", instr->raddr_a);
        } else if (mux == V3D_QPU_MUX_B) {
                append(disasm, "rf%d", instr->raddr_b);
        } else {
                append(disasm, "r%d", mux);
        }
}

static void
v3d_qpu_disasm_waddr(struct disasm_state *disasm, uint32_t waddr, bool magic)
{
        if (!magic) {
                append(disasm, "rf%d", waddr);
                return;
        }

        const char *name = v3d_qpu_magic_waddr_name(waddr);
        if (name)
                append(disasm, "%s", name);
        else
                append(disasm, "waddr UNKNOWN %d", waddr);
}

static void
v3d_qpu_disasm_add(struct disasm_state *disasm,
                   const struct v3d_qpu_instr *instr)
{
        bool has_dst = v3d_qpu_add_op_has_dst(instr->alu.add.op);
        int num_src = v3d_qpu_add_op_num_src(instr->alu.add.op);

        append(disasm, "%s", v3d_qpu_add_op_name(instr->alu.add.op));
        append(disasm, "%s", v3d_qpu_cond_name(instr->flags.ac));
        append(disasm, "%s", v3d_qpu_pf_name(instr->flags.apf));
        append(disasm, "%s", v3d_qpu_uf_name(instr->flags.auf));

        append(disasm, "  ");

        if (has_dst) {
                v3d_qpu_disasm_waddr(disasm, instr->alu.add.waddr,
                                     instr->alu.add.magic_write);
                append(disasm, v3d_qpu_pack_name(instr->alu.add.output_pack));
        }

        if (num_src >= 1) {
                if (has_dst)
                        append(disasm, ", ");
                v3d_qpu_disasm_raddr(disasm, instr, instr->alu.add.a);
                append(disasm, "%s",
                       v3d_qpu_unpack_name(instr->alu.add.a_unpack));
        }

        if (num_src >= 2) {
                append(disasm, ", ");
                v3d_qpu_disasm_raddr(disasm, instr, instr->alu.add.b);
                append(disasm, "%s",
                       v3d_qpu_unpack_name(instr->alu.add.b_unpack));
        }
}

static void
v3d_qpu_disasm_mul(struct disasm_state *disasm,
                   const struct v3d_qpu_instr *instr)
{
        bool has_dst = v3d_qpu_mul_op_has_dst(instr->alu.mul.op);
        int num_src = v3d_qpu_mul_op_num_src(instr->alu.mul.op);

        pad_to(disasm, 21);
        append(disasm, "; ");

        append(disasm, "%s", v3d_qpu_mul_op_name(instr->alu.mul.op));
        append(disasm, "%s", v3d_qpu_cond_name(instr->flags.mc));
        append(disasm, "%s", v3d_qpu_pf_name(instr->flags.mpf));
        append(disasm, "%s", v3d_qpu_uf_name(instr->flags.muf));

        if (instr->alu.mul.op == V3D_QPU_M_NOP)
                return;

        append(disasm, "  ");

        if (has_dst) {
                v3d_qpu_disasm_waddr(disasm, instr->alu.mul.waddr,
                                     instr->alu.mul.magic_write);
                append(disasm, v3d_qpu_pack_name(instr->alu.mul.output_pack));
        }

        if (num_src >= 1) {
                if (has_dst)
                        append(disasm, ", ");
                v3d_qpu_disasm_raddr(disasm, instr, instr->alu.mul.a);
                append(disasm, "%s",
                       v3d_qpu_unpack_name(instr->alu.mul.a_unpack));
        }

        if (num_src >= 2) {
                append(disasm, ", ");
                v3d_qpu_disasm_raddr(disasm, instr, instr->alu.mul.b);
                append(disasm, "%s",
                       v3d_qpu_unpack_name(instr->alu.mul.b_unpack));
        }
}

static void
v3d_qpu_disasm_sig(struct disasm_state *disasm,
                   const struct v3d_qpu_instr *instr)
{
        const struct v3d_qpu_sig *sig = &instr->sig;

        if (!sig->thrsw &&
            !sig->ldvary &&
            !sig->ldvpm &&
            !sig->ldtmu &&
            !sig->ldunif &&
            !sig->wrtmuc) {
                return;
        }

        pad_to(disasm, 41);

        if (sig->thrsw)
                append(disasm, "; thrsw");
        if (sig->ldvary)
                append(disasm, "; ldvary");
        if (sig->ldvpm)
                append(disasm, "; ldvpm");
        if (sig->ldtmu)
                append(disasm, "; ldtmu");
        if (sig->ldunif)
                append(disasm, "; ldunif");
        if (sig->wrtmuc)
                append(disasm, "; wrtmuc");
}

static void
v3d_qpu_disasm_alu(struct disasm_state *disasm,
                   const struct v3d_qpu_instr *instr)
{
        v3d_qpu_disasm_add(disasm, instr);
        v3d_qpu_disasm_mul(disasm, instr);
        v3d_qpu_disasm_sig(disasm, instr);
}

static void
v3d_qpu_disasm_branch(struct disasm_state *disasm,
                      const struct v3d_qpu_instr *instr)
{
        append(disasm, "b");
        if (instr->branch.ub)
                append(disasm, "u");
        append(disasm, "%s", v3d_qpu_branch_cond_name(instr->branch.cond));
        append(disasm, "%s", v3d_qpu_msfign_name(instr->branch.msfign));

        switch (instr->branch.bdi) {
        case V3D_QPU_BRANCH_DEST_ABS:
                append(disasm, "  zero_addr+0x%08x", instr->branch.offset);
                break;

        case V3D_QPU_BRANCH_DEST_REL:
                append(disasm, "  %d", instr->branch.offset);
                break;

        case V3D_QPU_BRANCH_DEST_LINK_REG:
                append(disasm, "  lri");
                break;

        case V3D_QPU_BRANCH_DEST_REGFILE:
                append(disasm, "  rf%d", instr->branch.raddr_a);
                break;
        }

        if (instr->branch.ub) {
                switch (instr->branch.bdu) {
                case V3D_QPU_BRANCH_DEST_ABS:
                        append(disasm, ", a:unif");
                        break;

                case V3D_QPU_BRANCH_DEST_REL:
                        append(disasm, ", r:unif");
                        break;

                case V3D_QPU_BRANCH_DEST_LINK_REG:
                        append(disasm, ", lri");
                        break;

                case V3D_QPU_BRANCH_DEST_REGFILE:
                        append(disasm, ", rf%d", instr->branch.raddr_a);
                        break;
                }
        }
}

const char *
v3d_qpu_decode(const struct v3d_device_info *devinfo,
               const struct v3d_qpu_instr *instr)
{
        struct disasm_state disasm = {
                .string = rzalloc_size(NULL, 1),
                .offset = 0,
                .devinfo = devinfo,
        };

        switch (instr->type) {
        case V3D_QPU_INSTR_TYPE_ALU:
                v3d_qpu_disasm_alu(&disasm, instr);
                break;

        case V3D_QPU_INSTR_TYPE_BRANCH:
                v3d_qpu_disasm_branch(&disasm, instr);
                break;
        }

        return disasm.string;
}

/**
 * Returns a string containing the disassembled representation of the QPU
 * instruction.  It is the caller's responsibility to free the return value
 * with ralloc_free().
 */
const char *
v3d_qpu_disasm(const struct v3d_device_info *devinfo, uint64_t inst)
{
        struct v3d_qpu_instr instr;
        bool ok = v3d_qpu_instr_unpack(devinfo, inst, &instr);
        assert(ok); (void)ok;

        return v3d_qpu_decode(devinfo, &instr);
}

void
v3d_qpu_dump(const struct v3d_device_info *devinfo,
             const struct v3d_qpu_instr *instr)
{
        const char *decoded = v3d_qpu_decode(devinfo, instr);
        fprintf(stderr, "%s", decoded);
        ralloc_free((char *)decoded);
}
