/*
 * Copyright © 2016-2017 Broadcom
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

#include "broadcom/common/v3d_device_info.h"
#include "v3d_compiler.h"

static void
vir_print_reg(struct v3d_compile *c, struct qreg reg)
{
        static const char *files[] = {
                [QFILE_TEMP] = "t",
                [QFILE_UNIF] = "u",
                [QFILE_TLB] = "tlb",
                [QFILE_TLBU] = "tlbu",
        };
        static const char *quniform_names[] = {
                [QUNIFORM_VIEWPORT_X_SCALE] = "vp_x_scale",
                [QUNIFORM_VIEWPORT_Y_SCALE] = "vp_y_scale",
                [QUNIFORM_VIEWPORT_Z_OFFSET] = "vp_z_offset",
                [QUNIFORM_VIEWPORT_Z_SCALE] = "vp_z_scale",
        };

        switch (reg.file) {

        case QFILE_NULL:
                fprintf(stderr, "null");
                break;

        case QFILE_LOAD_IMM:
                fprintf(stderr, "0x%08x (%f)", reg.index, uif(reg.index));
                break;

        case QFILE_REG:
                fprintf(stderr, "rf%d", reg.index);
                break;

        case QFILE_MAGIC:
                fprintf(stderr, "%s", v3d_qpu_magic_waddr_name(reg.index));
                break;

        case QFILE_SMALL_IMM:
                if ((int)reg.index >= -16 && (int)reg.index <= 15)
                        fprintf(stderr, "%d", reg.index);
                else
                        fprintf(stderr, "%f", uif(reg.index));
                break;

        case QFILE_VPM:
                fprintf(stderr, "vpm%d.%d",
                        reg.index / 4, reg.index % 4);
                break;

        case QFILE_TLB:
        case QFILE_TLBU:
                fprintf(stderr, "%s", files[reg.file]);
                break;

        case QFILE_UNIF: {
                enum quniform_contents contents = c->uniform_contents[reg.index];

                fprintf(stderr, "%s%d", files[reg.file], reg.index);

                switch (contents) {
                case QUNIFORM_CONSTANT:
                        fprintf(stderr, " (0x%08x / %f)",
                                c->uniform_data[reg.index],
                                uif(c->uniform_data[reg.index]));
                        break;

                case QUNIFORM_UNIFORM:
                        fprintf(stderr, " (push[%d])",
                                c->uniform_data[reg.index]);
                        break;

                case QUNIFORM_TEXTURE_CONFIG_P1:
                        fprintf(stderr, " (tex[%d].p1)",
                                c->uniform_data[reg.index]);
                        break;

                case QUNIFORM_TEXTURE_WIDTH:
                        fprintf(stderr, " (tex[%d].width)",
                                c->uniform_data[reg.index]);
                        break;
                case QUNIFORM_TEXTURE_HEIGHT:
                        fprintf(stderr, " (tex[%d].height)",
                                c->uniform_data[reg.index]);
                        break;
                case QUNIFORM_TEXTURE_DEPTH:
                        fprintf(stderr, " (tex[%d].depth)",
                                c->uniform_data[reg.index]);
                        break;
                case QUNIFORM_TEXTURE_ARRAY_SIZE:
                        fprintf(stderr, " (tex[%d].array_size)",
                                c->uniform_data[reg.index]);
                        break;
                case QUNIFORM_TEXTURE_LEVELS:
                        fprintf(stderr, " (tex[%d].levels)",
                                c->uniform_data[reg.index]);
                        break;

                case QUNIFORM_UBO_ADDR:
                        fprintf(stderr, " (ubo[%d])",
                                c->uniform_data[reg.index]);
                        break;

                default:
                        if (quniform_contents_is_texture_p0(contents)) {
                                fprintf(stderr, " (tex[%d].p0: 0x%08x)",
                                        contents - QUNIFORM_TEXTURE_CONFIG_P0_0,
                                        c->uniform_data[reg.index]);
                        } else if (contents < ARRAY_SIZE(quniform_names)) {
                                fprintf(stderr, " (%s)",
                                        quniform_names[contents]);
                        } else {
                                fprintf(stderr, " (%d / 0x%08x)", contents,
                                        c->uniform_data[reg.index]);
                        }
                }

                break;
        }

        default:
                fprintf(stderr, "%s%d", files[reg.file], reg.index);
                break;
        }
}

static void
vir_dump_sig_addr(const struct v3d_device_info *devinfo,
                  const struct v3d_qpu_instr *instr)
{
        if (devinfo->ver < 41)
                return;

        if (!instr->sig_magic)
                fprintf(stderr, ".rf%d", instr->sig_addr);
        else {
                const char *name = v3d_qpu_magic_waddr_name(instr->sig_addr);
                if (name)
                        fprintf(stderr, ".%s", name);
                else
                        fprintf(stderr, ".UNKNOWN%d", instr->sig_addr);
        }
}

static void
vir_dump_sig(struct v3d_compile *c, struct qinst *inst)
{
        struct v3d_qpu_sig *sig = &inst->qpu.sig;

        if (sig->thrsw)
                fprintf(stderr, "; thrsw");
        if (sig->ldvary) {
                fprintf(stderr, "; ldvary");
                vir_dump_sig_addr(c->devinfo, &inst->qpu);
        }
        if (sig->ldvpm)
                fprintf(stderr, "; ldvpm");
        if (sig->ldtmu) {
                fprintf(stderr, "; ldtmu");
                vir_dump_sig_addr(c->devinfo, &inst->qpu);
        }
        if (sig->ldtlb) {
                fprintf(stderr, "; ldtlb");
                vir_dump_sig_addr(c->devinfo, &inst->qpu);
        }
        if (sig->ldtlbu) {
                fprintf(stderr, "; ldtlbu");
                vir_dump_sig_addr(c->devinfo, &inst->qpu);
        }
        if (sig->ldunif)
                fprintf(stderr, "; ldunif");
        if (sig->ldunifrf) {
                fprintf(stderr, "; ldunifrf");
                vir_dump_sig_addr(c->devinfo, &inst->qpu);
        }
        if (sig->ldunifa)
                fprintf(stderr, "; ldunifa");
        if (sig->ldunifarf) {
                fprintf(stderr, "; ldunifarf");
                vir_dump_sig_addr(c->devinfo, &inst->qpu);
        }
        if (sig->wrtmuc)
                fprintf(stderr, "; wrtmuc");
}

static void
vir_dump_alu(struct v3d_compile *c, struct qinst *inst)
{
        struct v3d_qpu_instr *instr = &inst->qpu;
        int nsrc = vir_get_non_sideband_nsrc(inst);
        int sideband_nsrc = vir_get_nsrc(inst);
        enum v3d_qpu_input_unpack unpack[2];

        if (inst->qpu.alu.add.op != V3D_QPU_A_NOP) {
                fprintf(stderr, "%s", v3d_qpu_add_op_name(instr->alu.add.op));
                fprintf(stderr, "%s", v3d_qpu_cond_name(instr->flags.ac));
                fprintf(stderr, "%s", v3d_qpu_pf_name(instr->flags.apf));
                fprintf(stderr, "%s", v3d_qpu_uf_name(instr->flags.auf));
                fprintf(stderr, " ");

                vir_print_reg(c, inst->dst);
                fprintf(stderr, "%s", v3d_qpu_pack_name(instr->alu.add.output_pack));

                unpack[0] = instr->alu.add.a_unpack;
                unpack[1] = instr->alu.add.b_unpack;
        } else {
                fprintf(stderr, "%s", v3d_qpu_mul_op_name(instr->alu.mul.op));
                fprintf(stderr, "%s", v3d_qpu_cond_name(instr->flags.mc));
                fprintf(stderr, "%s", v3d_qpu_pf_name(instr->flags.mpf));
                fprintf(stderr, "%s", v3d_qpu_uf_name(instr->flags.muf));
                fprintf(stderr, " ");

                vir_print_reg(c, inst->dst);
                fprintf(stderr, "%s", v3d_qpu_pack_name(instr->alu.mul.output_pack));

                unpack[0] = instr->alu.mul.a_unpack;
                unpack[1] = instr->alu.mul.b_unpack;
        }

        for (int i = 0; i < sideband_nsrc; i++) {
                fprintf(stderr, ", ");
                vir_print_reg(c, inst->src[i]);
                if (i < nsrc)
                        fprintf(stderr, "%s", v3d_qpu_unpack_name(unpack[i]));
        }

        vir_dump_sig(c, inst);
}

void
vir_dump_inst(struct v3d_compile *c, struct qinst *inst)
{
        struct v3d_qpu_instr *instr = &inst->qpu;

        switch (inst->qpu.type) {
        case V3D_QPU_INSTR_TYPE_ALU:
                vir_dump_alu(c, inst);
                break;
        case V3D_QPU_INSTR_TYPE_BRANCH:
                fprintf(stderr, "b");
                if (instr->branch.ub)
                        fprintf(stderr, "u");

                fprintf(stderr, "%s",
                        v3d_qpu_branch_cond_name(instr->branch.cond));
                fprintf(stderr, "%s", v3d_qpu_msfign_name(instr->branch.msfign));

                switch (instr->branch.bdi) {
                case V3D_QPU_BRANCH_DEST_ABS:
                        fprintf(stderr, "  zero_addr+0x%08x", instr->branch.offset);
                        break;

                case V3D_QPU_BRANCH_DEST_REL:
                        fprintf(stderr, "  %d", instr->branch.offset);
                        break;

                case V3D_QPU_BRANCH_DEST_LINK_REG:
                        fprintf(stderr, "  lri");
                        break;

                case V3D_QPU_BRANCH_DEST_REGFILE:
                        fprintf(stderr, "  rf%d", instr->branch.raddr_a);
                        break;
                }

                if (instr->branch.ub) {
                        switch (instr->branch.bdu) {
                        case V3D_QPU_BRANCH_DEST_ABS:
                                fprintf(stderr, ", a:unif");
                                break;

                        case V3D_QPU_BRANCH_DEST_REL:
                                fprintf(stderr, ", r:unif");
                                break;

                        case V3D_QPU_BRANCH_DEST_LINK_REG:
                                fprintf(stderr, ", lri");
                                break;

                        case V3D_QPU_BRANCH_DEST_REGFILE:
                                fprintf(stderr, ", rf%d", instr->branch.raddr_a);
                                break;
                        }
                }

                if (vir_has_implicit_uniform(inst)) {
                        fprintf(stderr, " ");
                        vir_print_reg(c, inst->src[vir_get_implicit_uniform_src(inst)]);
                }

                break;
        }
}

void
vir_dump(struct v3d_compile *c)
{
        int ip = 0;

        vir_for_each_block(block, c) {
                fprintf(stderr, "BLOCK %d:\n", block->index);
                vir_for_each_inst(inst, block) {
                        if (c->live_intervals_valid) {
                                bool first = true;

                                for (int i = 0; i < c->num_temps; i++) {
                                        if (c->temp_start[i] != ip)
                                                continue;

                                        if (first) {
                                                first = false;
                                        } else {
                                                fprintf(stderr, ", ");
                                        }
                                        fprintf(stderr, "S%4d", i);
                                }

                                if (first)
                                        fprintf(stderr, "      ");
                                else
                                        fprintf(stderr, " ");
                        }

                        if (c->live_intervals_valid) {
                                bool first = true;

                                for (int i = 0; i < c->num_temps; i++) {
                                        if (c->temp_end[i] != ip)
                                                continue;

                                        if (first) {
                                                first = false;
                                        } else {
                                                fprintf(stderr, ", ");
                                        }
                                        fprintf(stderr, "E%4d", i);
                                }

                                if (first)
                                        fprintf(stderr, "      ");
                                else
                                        fprintf(stderr, " ");
                        }

                        vir_dump_inst(c, inst);
                        fprintf(stderr, "\n");
                        ip++;
                }
                if (block->successors[1]) {
                        fprintf(stderr, "-> BLOCK %d, %d\n",
                                block->successors[0]->index,
                                block->successors[1]->index);
                } else if (block->successors[0]) {
                        fprintf(stderr, "-> BLOCK %d\n",
                                block->successors[0]->index);
                }
        }
}
