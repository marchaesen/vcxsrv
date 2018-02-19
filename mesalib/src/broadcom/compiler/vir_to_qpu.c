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

#include "compiler/v3d_compiler.h"
#include "qpu/qpu_instr.h"
#include "qpu/qpu_disasm.h"

static inline struct qpu_reg
qpu_reg(int index)
{
        struct qpu_reg reg = {
                .magic = false,
                .index = index,
        };
        return reg;
}

static inline struct qpu_reg
qpu_magic(enum v3d_qpu_waddr waddr)
{
        struct qpu_reg reg = {
                .magic = true,
                .index = waddr,
        };
        return reg;
}

static inline struct qpu_reg
qpu_acc(int acc)
{
        return qpu_magic(V3D_QPU_WADDR_R0 + acc);
}

struct v3d_qpu_instr
v3d_qpu_nop(void)
{
        struct v3d_qpu_instr instr = {
                .type = V3D_QPU_INSTR_TYPE_ALU,
                .alu = {
                        .add = {
                                .op = V3D_QPU_A_NOP,
                                .waddr = V3D_QPU_WADDR_NOP,
                                .magic_write = true,
                        },
                        .mul = {
                                .op = V3D_QPU_M_NOP,
                                .waddr = V3D_QPU_WADDR_NOP,
                                .magic_write = true,
                        },
                }
        };

        return instr;
}

static struct qinst *
vir_nop(void)
{
        struct qreg undef = { QFILE_NULL, 0 };
        struct qinst *qinst = vir_add_inst(V3D_QPU_A_NOP, undef, undef, undef);

        return qinst;
}

static struct qinst *
new_qpu_nop_before(struct qinst *inst)
{
        struct qinst *q = vir_nop();

        list_addtail(&q->link, &inst->link);

        return q;
}

static void
new_ldunif_instr(struct qinst *inst, int i)
{
        struct qinst *ldunif = new_qpu_nop_before(inst);

        ldunif->qpu.sig.ldunif = true;
        assert(inst->src[i].file == QFILE_UNIF);
        ldunif->uniform = inst->src[i].index;
}

/**
 * Allocates the src register (accumulator or register file) into the RADDR
 * fields of the instruction.
 */
static void
set_src(struct v3d_qpu_instr *instr, enum v3d_qpu_mux *mux, struct qpu_reg src)
{
        if (src.magic) {
                assert(src.index >= V3D_QPU_WADDR_R0 &&
                       src.index <= V3D_QPU_WADDR_R5);
                *mux = src.index - V3D_QPU_WADDR_R0 + V3D_QPU_MUX_R0;
                return;
        }

        if (instr->alu.add.a != V3D_QPU_MUX_A &&
            instr->alu.add.b != V3D_QPU_MUX_A &&
            instr->alu.mul.a != V3D_QPU_MUX_A &&
            instr->alu.mul.b != V3D_QPU_MUX_A) {
                instr->raddr_a = src.index;
                *mux = V3D_QPU_MUX_A;
        } else {
                if (instr->raddr_a == src.index) {
                        *mux = V3D_QPU_MUX_A;
                } else {
                        assert(!(instr->alu.add.a == V3D_QPU_MUX_B &&
                                 instr->alu.add.b == V3D_QPU_MUX_B &&
                                 instr->alu.mul.a == V3D_QPU_MUX_B &&
                                 instr->alu.mul.b == V3D_QPU_MUX_B) ||
                               src.index == instr->raddr_b);

                        instr->raddr_b = src.index;
                        *mux = V3D_QPU_MUX_B;
                }
        }
}

static bool
is_no_op_mov(struct qinst *qinst)
{
        static const struct v3d_qpu_sig no_sig = {0};

        /* Make sure it's just a lone MOV. */
        if (qinst->qpu.type != V3D_QPU_INSTR_TYPE_ALU ||
            qinst->qpu.alu.mul.op != V3D_QPU_M_MOV ||
            qinst->qpu.alu.add.op != V3D_QPU_A_NOP ||
            memcmp(&qinst->qpu.sig, &no_sig, sizeof(no_sig)) != 0) {
                return false;
        }

        /* Check if it's a MOV from a register to itself. */
        enum v3d_qpu_waddr waddr = qinst->qpu.alu.mul.waddr;
        if (qinst->qpu.alu.mul.magic_write) {
                if (waddr < V3D_QPU_WADDR_R0 || waddr > V3D_QPU_WADDR_R4)
                        return false;

                if (qinst->qpu.alu.mul.a !=
                    V3D_QPU_MUX_R0 + (waddr - V3D_QPU_WADDR_R0)) {
                        return false;
                }
        } else {
                int raddr;

                switch (qinst->qpu.alu.mul.a) {
                case V3D_QPU_MUX_A:
                        raddr = qinst->qpu.raddr_a;
                        break;
                case V3D_QPU_MUX_B:
                        raddr = qinst->qpu.raddr_b;
                        break;
                default:
                        return false;
                }
                if (raddr != waddr)
                        return false;
        }

        /* No packing or flags updates, or we need to execute the
         * instruction.
         */
        if (qinst->qpu.alu.mul.a_unpack != V3D_QPU_UNPACK_NONE ||
            qinst->qpu.alu.mul.output_pack != V3D_QPU_PACK_NONE ||
            qinst->qpu.flags.mc != V3D_QPU_COND_NONE ||
            qinst->qpu.flags.mpf != V3D_QPU_PF_NONE ||
            qinst->qpu.flags.muf != V3D_QPU_UF_NONE) {
                return false;
        }

        return true;
}

static void
v3d_generate_code_block(struct v3d_compile *c,
                        struct qblock *block,
                        struct qpu_reg *temp_registers)
{
        int last_vpm_read_index = -1;

        vir_for_each_inst_safe(qinst, block) {
#if 0
                fprintf(stderr, "translating qinst to qpu: ");
                vir_dump_inst(c, qinst);
                fprintf(stderr, "\n");
#endif

                struct qinst *temp;

                if (vir_has_implicit_uniform(qinst)) {
                        int src = vir_get_implicit_uniform_src(qinst);
                        assert(qinst->src[src].file == QFILE_UNIF);
                        qinst->uniform = qinst->src[src].index;
                        c->num_uniforms++;
                }

                int nsrc = vir_get_non_sideband_nsrc(qinst);
                struct qpu_reg src[ARRAY_SIZE(qinst->src)];
                bool emitted_ldunif = false;
                for (int i = 0; i < nsrc; i++) {
                        int index = qinst->src[i].index;
                        switch (qinst->src[i].file) {
                        case QFILE_REG:
                                src[i] = qpu_reg(qinst->src[i].index);
                                break;
                        case QFILE_MAGIC:
                                src[i] = qpu_magic(qinst->src[i].index);
                                break;
                        case QFILE_NULL:
                        case QFILE_LOAD_IMM:
                                src[i] = qpu_acc(0);
                                break;
                        case QFILE_TEMP:
                                src[i] = temp_registers[index];
                                break;
                        case QFILE_UNIF:
                                if (!emitted_ldunif) {
                                        new_ldunif_instr(qinst, i);
                                        c->num_uniforms++;
                                        emitted_ldunif = true;
                                }

                                src[i] = qpu_acc(5);
                                break;
                        case QFILE_SMALL_IMM:
                                abort(); /* XXX */
#if 0
                                src[i].mux = QPU_MUX_SMALL_IMM;
                                src[i].addr = qpu_encode_small_immediate(qinst->src[i].index);
                                /* This should only have returned a valid
                                 * small immediate field, not ~0 for failure.
                                 */
                                assert(src[i].addr <= 47);
#endif
                                break;

                        case QFILE_VPM:
                                assert((int)qinst->src[i].index >=
                                       last_vpm_read_index);
                                (void)last_vpm_read_index;
                                last_vpm_read_index = qinst->src[i].index;

                                temp = new_qpu_nop_before(qinst);
                                temp->qpu.sig.ldvpm = true;

                                src[i] = qpu_acc(3);
                                break;

                        case QFILE_TLB:
                        case QFILE_TLBU:
                                unreachable("bad vir src file");
                        }
                }

                struct qpu_reg dst;
                switch (qinst->dst.file) {
                case QFILE_NULL:
                        dst = qpu_magic(V3D_QPU_WADDR_NOP);
                        break;

                case QFILE_REG:
                        dst = qpu_reg(qinst->dst.index);
                        break;

                case QFILE_MAGIC:
                        dst = qpu_magic(qinst->dst.index);
                        break;

                case QFILE_TEMP:
                        dst = temp_registers[qinst->dst.index];
                        break;

                case QFILE_VPM:
                        dst = qpu_magic(V3D_QPU_WADDR_VPM);
                        break;

                case QFILE_TLB:
                        dst = qpu_magic(V3D_QPU_WADDR_TLB);
                        break;

                case QFILE_TLBU:
                        dst = qpu_magic(V3D_QPU_WADDR_TLBU);
                        break;

                case QFILE_UNIF:
                case QFILE_SMALL_IMM:
                case QFILE_LOAD_IMM:
                        assert(!"not reached");
                        break;
                }

                if (qinst->qpu.type == V3D_QPU_INSTR_TYPE_ALU) {
                        if (v3d_qpu_sig_writes_address(c->devinfo,
                                                       &qinst->qpu.sig)) {
                                assert(qinst->qpu.alu.add.op == V3D_QPU_A_NOP);
                                assert(qinst->qpu.alu.mul.op == V3D_QPU_M_NOP);

                                qinst->qpu.sig_addr = dst.index;
                                qinst->qpu.sig_magic = dst.magic;
                        } else if (qinst->qpu.alu.add.op != V3D_QPU_A_NOP) {
                                assert(qinst->qpu.alu.mul.op == V3D_QPU_M_NOP);
                                if (nsrc >= 1) {
                                        set_src(&qinst->qpu,
                                                &qinst->qpu.alu.add.a, src[0]);
                                }
                                if (nsrc >= 2) {
                                        set_src(&qinst->qpu,
                                                &qinst->qpu.alu.add.b, src[1]);
                                }

                                qinst->qpu.alu.add.waddr = dst.index;
                                qinst->qpu.alu.add.magic_write = dst.magic;
                        } else {
                                if (nsrc >= 1) {
                                        set_src(&qinst->qpu,
                                                &qinst->qpu.alu.mul.a, src[0]);
                                }
                                if (nsrc >= 2) {
                                        set_src(&qinst->qpu,
                                                &qinst->qpu.alu.mul.b, src[1]);
                                }

                                qinst->qpu.alu.mul.waddr = dst.index;
                                qinst->qpu.alu.mul.magic_write = dst.magic;

                                if (is_no_op_mov(qinst)) {
                                        vir_remove_instruction(c, qinst);
                                        continue;
                                }
                        }
                } else {
                        assert(qinst->qpu.type == V3D_QPU_INSTR_TYPE_BRANCH);
                }
        }
}


static void
v3d_dump_qpu(struct v3d_compile *c)
{
        fprintf(stderr, "%s prog %d/%d QPU:\n",
                vir_get_stage_name(c),
                c->program_id, c->variant_id);

        for (int i = 0; i < c->qpu_inst_count; i++) {
                const char *str = v3d_qpu_disasm(c->devinfo, c->qpu_insts[i]);
                fprintf(stderr, "0x%016"PRIx64" %s\n", c->qpu_insts[i], str);
        }
        fprintf(stderr, "\n");
}

void
v3d_vir_to_qpu(struct v3d_compile *c, struct qpu_reg *temp_registers)
{
        /* Reset the uniform count to how many will be actually loaded by the
         * generated QPU code.
         */
        c->num_uniforms = 0;

        vir_for_each_block(block, c)
                v3d_generate_code_block(c, block, temp_registers);

        uint32_t cycles = v3d_qpu_schedule_instructions(c);

        c->qpu_insts = rzalloc_array(c, uint64_t, c->qpu_inst_count);
        int i = 0;
        vir_for_each_inst_inorder(inst, c) {
                bool ok = v3d_qpu_instr_pack(c->devinfo, &inst->qpu,
                                             &c->qpu_insts[i++]);
                assert(ok); (void) ok;
        }
        assert(i == c->qpu_inst_count);

        if (V3D_DEBUG & V3D_DEBUG_SHADERDB) {
                fprintf(stderr, "SHADER-DB: %s prog %d/%d: %d instructions\n",
                        vir_get_stage_name(c),
                        c->program_id, c->variant_id,
                        c->qpu_inst_count);
        }

        if (V3D_DEBUG & V3D_DEBUG_SHADERDB) {
                fprintf(stderr, "SHADER-DB: %s prog %d/%d: %d estimated cycles\n",
                        vir_get_stage_name(c),
                        c->program_id, c->variant_id,
                        cycles);
        }

        if (V3D_DEBUG & (V3D_DEBUG_QPU |
                         v3d_debug_flag_for_shader_stage(c->s->info.stage))) {
                v3d_dump_qpu(c);
        }

        qpu_validate(c);

        free(temp_registers);
}
