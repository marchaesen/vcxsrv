/*
 * Copyright © 2010 Intel Corporation
 * Copyright © 2014-2017 Broadcom
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
 * The basic model of the list scheduler is to take a basic block, compute a
 * DAG of the dependencies, and make a list of the DAG heads.  Heuristically
 * pick a DAG head, then put all the children that are now DAG heads into the
 * list of things to schedule.
 *
 * The goal of scheduling here is to pack pairs of operations together in a
 * single QPU instruction.
 */

#include "qpu/qpu_disasm.h"
#include "v3d_compiler.h"
#include "util/ralloc.h"

static bool debug;

struct schedule_node_child;

struct schedule_node {
        struct list_head link;
        struct qinst *inst;
        struct schedule_node_child *children;
        uint32_t child_count;
        uint32_t child_array_size;
        uint32_t parent_count;

        /* Longest cycles + instruction_latency() of any parent of this node. */
        uint32_t unblocked_time;

        /**
         * Minimum number of cycles from scheduling this instruction until the
         * end of the program, based on the slowest dependency chain through
         * the children.
         */
        uint32_t delay;

        /**
         * cycles between this instruction being scheduled and when its result
         * can be consumed.
         */
        uint32_t latency;
};

struct schedule_node_child {
        struct schedule_node *node;
        bool write_after_read;
};

/* When walking the instructions in reverse, we need to swap before/after in
 * add_dep().
 */
enum direction { F, R };

struct schedule_state {
        const struct v3d_device_info *devinfo;
        struct schedule_node *last_r[6];
        struct schedule_node *last_rf[64];
        struct schedule_node *last_sf;
        struct schedule_node *last_vpm_read;
        struct schedule_node *last_tmu_write;
        struct schedule_node *last_tmu_config;
        struct schedule_node *last_tlb;
        struct schedule_node *last_vpm;
        struct schedule_node *last_unif;
        struct schedule_node *last_rtop;
        enum direction dir;
        /* Estimated cycle when the current instruction would start. */
        uint32_t time;
};

static void
add_dep(struct schedule_state *state,
        struct schedule_node *before,
        struct schedule_node *after,
        bool write)
{
        bool write_after_read = !write && state->dir == R;

        if (!before || !after)
                return;

        assert(before != after);

        if (state->dir == R) {
                struct schedule_node *t = before;
                before = after;
                after = t;
        }

        for (int i = 0; i < before->child_count; i++) {
                if (before->children[i].node == after &&
                    (before->children[i].write_after_read == write_after_read)) {
                        return;
                }
        }

        if (before->child_array_size <= before->child_count) {
                before->child_array_size = MAX2(before->child_array_size * 2, 16);
                before->children = reralloc(before, before->children,
                                            struct schedule_node_child,
                                            before->child_array_size);
        }

        before->children[before->child_count].node = after;
        before->children[before->child_count].write_after_read =
                write_after_read;
        before->child_count++;
        after->parent_count++;
}

static void
add_read_dep(struct schedule_state *state,
              struct schedule_node *before,
              struct schedule_node *after)
{
        add_dep(state, before, after, false);
}

static void
add_write_dep(struct schedule_state *state,
              struct schedule_node **before,
              struct schedule_node *after)
{
        add_dep(state, *before, after, true);
        *before = after;
}

static bool
qpu_inst_is_tlb(const struct v3d_qpu_instr *inst)
{
        if (inst->type != V3D_QPU_INSTR_TYPE_ALU)
                return false;

        if (inst->alu.add.magic_write &&
            (inst->alu.add.waddr == V3D_QPU_WADDR_TLB ||
             inst->alu.add.waddr == V3D_QPU_WADDR_TLBU))
                return true;

        if (inst->alu.mul.magic_write &&
            (inst->alu.mul.waddr == V3D_QPU_WADDR_TLB ||
             inst->alu.mul.waddr == V3D_QPU_WADDR_TLBU))
                return true;

        return false;
}

static void
process_mux_deps(struct schedule_state *state, struct schedule_node *n,
                 enum v3d_qpu_mux mux)
{
        switch (mux) {
        case V3D_QPU_MUX_A:
                add_read_dep(state, state->last_rf[n->inst->qpu.raddr_a], n);
                break;
        case V3D_QPU_MUX_B:
                add_read_dep(state, state->last_rf[n->inst->qpu.raddr_b], n);
                break;
        default:
                add_read_dep(state, state->last_r[mux - V3D_QPU_MUX_R0], n);
                break;
        }
}


static void
process_waddr_deps(struct schedule_state *state, struct schedule_node *n,
                   uint32_t waddr, bool magic)
{
        if (!magic) {
                add_write_dep(state, &state->last_rf[waddr], n);
        } else if (v3d_qpu_magic_waddr_is_tmu(waddr)) {
                add_write_dep(state, &state->last_tmu_write, n);
                switch (waddr) {
                case V3D_QPU_WADDR_TMUS:
                case V3D_QPU_WADDR_TMUSCM:
                case V3D_QPU_WADDR_TMUSF:
                case V3D_QPU_WADDR_TMUSLOD:
                        add_write_dep(state, &state->last_tmu_config, n);
                        break;
                default:
                        break;
                }
        } else if (v3d_qpu_magic_waddr_is_sfu(waddr)) {
                /* Handled by v3d_qpu_writes_r4() check. */
        } else {
                switch (waddr) {
                case V3D_QPU_WADDR_R0:
                case V3D_QPU_WADDR_R1:
                case V3D_QPU_WADDR_R2:
                        add_write_dep(state,
                                      &state->last_r[waddr - V3D_QPU_WADDR_R0],
                                      n);
                        break;
                case V3D_QPU_WADDR_R3:
                case V3D_QPU_WADDR_R4:
                case V3D_QPU_WADDR_R5:
                        /* Handled by v3d_qpu_writes_r*() checks below. */
                        break;

                case V3D_QPU_WADDR_VPM:
                case V3D_QPU_WADDR_VPMU:
                        add_write_dep(state, &state->last_vpm, n);
                        break;

                case V3D_QPU_WADDR_TLB:
                case V3D_QPU_WADDR_TLBU:
                        add_write_dep(state, &state->last_tlb, n);
                        break;

                case V3D_QPU_WADDR_NOP:
                        break;

                default:
                        fprintf(stderr, "Unknown waddr %d\n", waddr);
                        abort();
                }
        }
}

static void
process_cond_deps(struct schedule_state *state, struct schedule_node *n,
                  enum v3d_qpu_cond cond)
{
        if (cond != V3D_QPU_COND_NONE)
                add_read_dep(state, state->last_sf, n);
}

static void
process_pf_deps(struct schedule_state *state, struct schedule_node *n,
                enum v3d_qpu_pf pf)
{
        if (pf != V3D_QPU_PF_NONE)
                add_write_dep(state, &state->last_sf, n);
}

static void
process_uf_deps(struct schedule_state *state, struct schedule_node *n,
                enum v3d_qpu_uf uf)
{
        if (uf != V3D_QPU_UF_NONE)
                add_write_dep(state, &state->last_sf, n);
}

/**
 * Common code for dependencies that need to be tracked both forward and
 * backward.
 *
 * This is for things like "all reads of r4 have to happen between the r4
 * writes that surround them".
 */
static void
calculate_deps(struct schedule_state *state, struct schedule_node *n)
{
        const struct v3d_device_info *devinfo = state->devinfo;
        struct qinst *qinst = n->inst;
        struct v3d_qpu_instr *inst = &qinst->qpu;

        if (inst->type == V3D_QPU_INSTR_TYPE_BRANCH) {
                if (inst->branch.cond != V3D_QPU_BRANCH_COND_ALWAYS)
                        add_read_dep(state, state->last_sf, n);

                /* XXX: BDI */
                /* XXX: BDU */
                /* XXX: ub */
                /* XXX: raddr_a */

                add_write_dep(state, &state->last_unif, n);
                return;
        }

        assert(inst->type == V3D_QPU_INSTR_TYPE_ALU);

        /* XXX: LOAD_IMM */

        if (v3d_qpu_add_op_num_src(inst->alu.add.op) > 0)
                process_mux_deps(state, n, inst->alu.add.a);
        if (v3d_qpu_add_op_num_src(inst->alu.add.op) > 1)
                process_mux_deps(state, n, inst->alu.add.b);

        if (v3d_qpu_mul_op_num_src(inst->alu.mul.op) > 0)
                process_mux_deps(state, n, inst->alu.mul.a);
        if (v3d_qpu_mul_op_num_src(inst->alu.mul.op) > 1)
                process_mux_deps(state, n, inst->alu.mul.b);

        switch (inst->alu.add.op) {
        case V3D_QPU_A_VPMSETUP:
                /* Could distinguish read/write by unpacking the uniform. */
                add_write_dep(state, &state->last_vpm, n);
                add_write_dep(state, &state->last_vpm_read, n);
                break;

        case V3D_QPU_A_STVPMV:
        case V3D_QPU_A_STVPMD:
        case V3D_QPU_A_STVPMP:
                add_write_dep(state, &state->last_vpm, n);
                break;

        case V3D_QPU_A_VPMWT:
                add_read_dep(state, state->last_vpm, n);
                break;

        case V3D_QPU_A_MSF:
                add_read_dep(state, state->last_tlb, n);
                break;

        case V3D_QPU_A_SETMSF:
        case V3D_QPU_A_SETREVF:
                add_write_dep(state, &state->last_tlb, n);
                break;

        case V3D_QPU_A_FLAPUSH:
        case V3D_QPU_A_FLBPUSH:
        case V3D_QPU_A_VFLA:
        case V3D_QPU_A_VFLNA:
        case V3D_QPU_A_VFLB:
        case V3D_QPU_A_VFLNB:
                add_read_dep(state, state->last_sf, n);
                break;

        case V3D_QPU_A_FLBPOP:
                add_write_dep(state, &state->last_sf, n);
                break;

        default:
                break;
        }

        switch (inst->alu.mul.op) {
        case V3D_QPU_M_MULTOP:
        case V3D_QPU_M_UMUL24:
                /* MULTOP sets rtop, and UMUL24 implicitly reads rtop and
                 * resets it to 0.  We could possibly reorder umul24s relative
                 * to each other, but for now just keep all the MUL parts in
                 * order.
                 */
                add_write_dep(state, &state->last_rtop, n);
                break;
        default:
                break;
        }

        if (inst->alu.add.op != V3D_QPU_A_NOP) {
                process_waddr_deps(state, n, inst->alu.add.waddr,
                                   inst->alu.add.magic_write);
        }
        if (inst->alu.mul.op != V3D_QPU_M_NOP) {
                process_waddr_deps(state, n, inst->alu.mul.waddr,
                                   inst->alu.mul.magic_write);
        }
        if (v3d_qpu_sig_writes_address(devinfo, &inst->sig)) {
                process_waddr_deps(state, n, inst->sig_addr,
                                   inst->sig_magic);
        }

        if (v3d_qpu_writes_r3(devinfo, inst))
                add_write_dep(state, &state->last_r[3], n);
        if (v3d_qpu_writes_r4(devinfo, inst))
                add_write_dep(state, &state->last_r[4], n);
        if (v3d_qpu_writes_r5(devinfo, inst))
                add_write_dep(state, &state->last_r[5], n);

        if (inst->sig.thrsw) {
                /* All accumulator contents and flags are undefined after the
                 * switch.
                 */
                for (int i = 0; i < ARRAY_SIZE(state->last_r); i++)
                        add_write_dep(state, &state->last_r[i], n);
                add_write_dep(state, &state->last_sf, n);

                /* Scoreboard-locking operations have to stay after the last
                 * thread switch.
                 */
                add_write_dep(state, &state->last_tlb, n);

                add_write_dep(state, &state->last_tmu_write, n);
                add_write_dep(state, &state->last_tmu_config, n);
        }

        if (inst->sig.ldtmu) {
                /* TMU loads are coming from a FIFO, so ordering is important.
                 */
                add_write_dep(state, &state->last_tmu_write, n);
        }

        if (inst->sig.wrtmuc)
                add_write_dep(state, &state->last_tmu_config, n);

        if (inst->sig.ldtlb | inst->sig.ldtlbu)
                add_read_dep(state, state->last_tlb, n);

        if (inst->sig.ldvpm)
                add_write_dep(state, &state->last_vpm_read, n);

        /* inst->sig.ldunif or sideband uniform read */
        if (qinst->uniform != ~0)
                add_write_dep(state, &state->last_unif, n);

        process_cond_deps(state, n, inst->flags.ac);
        process_cond_deps(state, n, inst->flags.mc);
        process_pf_deps(state, n, inst->flags.apf);
        process_pf_deps(state, n, inst->flags.mpf);
        process_uf_deps(state, n, inst->flags.auf);
        process_uf_deps(state, n, inst->flags.muf);
}

static void
calculate_forward_deps(struct v3d_compile *c, struct list_head *schedule_list)
{
        struct schedule_state state;

        memset(&state, 0, sizeof(state));
        state.devinfo = c->devinfo;
        state.dir = F;

        list_for_each_entry(struct schedule_node, node, schedule_list, link)
                calculate_deps(&state, node);
}

static void
calculate_reverse_deps(struct v3d_compile *c, struct list_head *schedule_list)
{
        struct list_head *node;
        struct schedule_state state;

        memset(&state, 0, sizeof(state));
        state.devinfo = c->devinfo;
        state.dir = R;

        for (node = schedule_list->prev; schedule_list != node; node = node->prev) {
                calculate_deps(&state, (struct schedule_node *)node);
        }
}

struct choose_scoreboard {
        int tick;
        int last_sfu_write_tick;
        int last_ldvary_tick;
        int last_uniforms_reset_tick;
        uint32_t last_waddr_add, last_waddr_mul;
        bool tlb_locked;
};

static bool
mux_reads_too_soon(struct choose_scoreboard *scoreboard,
                   const struct v3d_qpu_instr *inst, enum v3d_qpu_mux mux)
{
        switch (mux) {
        case V3D_QPU_MUX_A:
                if (scoreboard->last_waddr_add == inst->raddr_a ||
                    scoreboard->last_waddr_mul == inst->raddr_a) {
                        return true;
                }
                break;

        case V3D_QPU_MUX_B:
                if (scoreboard->last_waddr_add == inst->raddr_b ||
                    scoreboard->last_waddr_mul == inst->raddr_b) {
                        return true;
                }
                break;

        case V3D_QPU_MUX_R4:
                if (scoreboard->tick - scoreboard->last_sfu_write_tick <= 2)
                        return true;
                break;

        case V3D_QPU_MUX_R5:
                if (scoreboard->tick - scoreboard->last_ldvary_tick <= 1)
                        return true;
                break;
        default:
                break;
        }

        return false;
}

static bool
reads_too_soon_after_write(struct choose_scoreboard *scoreboard,
                           struct qinst *qinst)
{
        const struct v3d_qpu_instr *inst = &qinst->qpu;

        /* XXX: Branching off of raddr. */
        if (inst->type == V3D_QPU_INSTR_TYPE_BRANCH)
                return false;

        assert(inst->type == V3D_QPU_INSTR_TYPE_ALU);

        if (inst->alu.add.op != V3D_QPU_A_NOP) {
                if (v3d_qpu_add_op_num_src(inst->alu.add.op) > 0 &&
                    mux_reads_too_soon(scoreboard, inst, inst->alu.add.a)) {
                        return true;
                }
                if (v3d_qpu_add_op_num_src(inst->alu.add.op) > 1 &&
                    mux_reads_too_soon(scoreboard, inst, inst->alu.add.b)) {
                        return true;
                }
        }

        if (inst->alu.mul.op != V3D_QPU_M_NOP) {
                if (v3d_qpu_mul_op_num_src(inst->alu.mul.op) > 0 &&
                    mux_reads_too_soon(scoreboard, inst, inst->alu.mul.a)) {
                        return true;
                }
                if (v3d_qpu_mul_op_num_src(inst->alu.mul.op) > 1 &&
                    mux_reads_too_soon(scoreboard, inst, inst->alu.mul.b)) {
                        return true;
                }
        }

        /* XXX: imm */

        return false;
}

static bool
writes_too_soon_after_write(const struct v3d_device_info *devinfo,
                            struct choose_scoreboard *scoreboard,
                            struct qinst *qinst)
{
        const struct v3d_qpu_instr *inst = &qinst->qpu;

        /* Don't schedule any other r4 write too soon after an SFU write.
         * This would normally be prevented by dependency tracking, but might
         * occur if a dead SFU computation makes it to scheduling.
         */
        if (scoreboard->tick - scoreboard->last_sfu_write_tick < 2 &&
            v3d_qpu_writes_r4(devinfo, inst))
                return true;

        return false;
}

static bool
pixel_scoreboard_too_soon(struct choose_scoreboard *scoreboard,
                          const struct v3d_qpu_instr *inst)
{
        return (scoreboard->tick == 0 && qpu_inst_is_tlb(inst));
}

static int
get_instruction_priority(const struct v3d_qpu_instr *inst)
{
        uint32_t baseline_score;
        uint32_t next_score = 0;

        /* Schedule TLB operations as late as possible, to get more
         * parallelism between shaders.
         */
        if (qpu_inst_is_tlb(inst))
                return next_score;
        next_score++;

        /* Schedule texture read results collection late to hide latency. */
        if (inst->sig.ldtmu)
                return next_score;
        next_score++;

        /* Default score for things that aren't otherwise special. */
        baseline_score = next_score;
        next_score++;

        /* Schedule texture read setup early to hide their latency better. */
        if (inst->type == V3D_QPU_INSTR_TYPE_ALU &&
            ((inst->alu.add.magic_write &&
              v3d_qpu_magic_waddr_is_tmu(inst->alu.add.waddr)) ||
             (inst->alu.mul.magic_write &&
              v3d_qpu_magic_waddr_is_tmu(inst->alu.mul.waddr)))) {
                return next_score;
        }
        next_score++;

        return baseline_score;
}

static bool
qpu_magic_waddr_is_periph(enum v3d_qpu_waddr waddr)
{
        return (v3d_qpu_magic_waddr_is_tmu(waddr) ||
                v3d_qpu_magic_waddr_is_sfu(waddr) ||
                v3d_qpu_magic_waddr_is_tlb(waddr) ||
                v3d_qpu_magic_waddr_is_vpm(waddr) ||
                v3d_qpu_magic_waddr_is_tsy(waddr));
}

static bool
qpu_accesses_peripheral(const struct v3d_qpu_instr *inst)
{
        if (v3d_qpu_uses_vpm(inst))
                return true;

        if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
                if (inst->alu.add.op != V3D_QPU_A_NOP &&
                    inst->alu.add.magic_write &&
                    qpu_magic_waddr_is_periph(inst->alu.add.waddr)) {
                        return true;
                }

                if (inst->alu.mul.op != V3D_QPU_M_NOP &&
                    inst->alu.mul.magic_write &&
                    qpu_magic_waddr_is_periph(inst->alu.mul.waddr)) {
                        return true;
                }
        }

        return (inst->sig.ldvpm ||
                inst->sig.ldtmu ||
                inst->sig.ldtlb ||
                inst->sig.ldtlbu ||
                inst->sig.wrtmuc);
}

static bool
qpu_merge_inst(const struct v3d_device_info *devinfo,
               struct v3d_qpu_instr *result,
               const struct v3d_qpu_instr *a,
               const struct v3d_qpu_instr *b)
{
        if (a->type != V3D_QPU_INSTR_TYPE_ALU ||
            b->type != V3D_QPU_INSTR_TYPE_ALU) {
                return false;
        }

        /* Can't do more than one peripheral access in an instruction.
         *
         * XXX: V3D 4.1 allows TMU read along with a VPM read or write, and
         * WRTMUC with a TMU magic register write (other than tmuc).
         */
        if (qpu_accesses_peripheral(a) && qpu_accesses_peripheral(b))
                return false;

        struct v3d_qpu_instr merge = *a;

        if (b->alu.add.op != V3D_QPU_A_NOP) {
                if (a->alu.add.op != V3D_QPU_A_NOP)
                        return false;
                merge.alu.add = b->alu.add;

                merge.flags.ac = b->flags.ac;
                merge.flags.apf = b->flags.apf;
                merge.flags.auf = b->flags.auf;
        }

        if (b->alu.mul.op != V3D_QPU_M_NOP) {
                if (a->alu.mul.op != V3D_QPU_M_NOP)
                        return false;
                merge.alu.mul = b->alu.mul;

                merge.flags.mc = b->flags.mc;
                merge.flags.mpf = b->flags.mpf;
                merge.flags.muf = b->flags.muf;
        }

        if (v3d_qpu_uses_mux(b, V3D_QPU_MUX_A)) {
                if (v3d_qpu_uses_mux(a, V3D_QPU_MUX_A) &&
                    a->raddr_a != b->raddr_a) {
                        return false;
                }
                merge.raddr_a = b->raddr_a;
        }

        if (v3d_qpu_uses_mux(b, V3D_QPU_MUX_B)) {
                if (v3d_qpu_uses_mux(a, V3D_QPU_MUX_B) &&
                    a->raddr_b != b->raddr_b) {
                        return false;
                }
                merge.raddr_b = b->raddr_b;
        }

        merge.sig.thrsw |= b->sig.thrsw;
        merge.sig.ldunif |= b->sig.ldunif;
        merge.sig.ldunifrf |= b->sig.ldunifrf;
        merge.sig.ldunifa |= b->sig.ldunifa;
        merge.sig.ldunifarf |= b->sig.ldunifarf;
        merge.sig.ldtmu |= b->sig.ldtmu;
        merge.sig.ldvary |= b->sig.ldvary;
        merge.sig.ldvpm |= b->sig.ldvpm;
        merge.sig.small_imm |= b->sig.small_imm;
        merge.sig.ldtlb |= b->sig.ldtlb;
        merge.sig.ldtlbu |= b->sig.ldtlbu;
        merge.sig.ucb |= b->sig.ucb;
        merge.sig.rotate |= b->sig.rotate;
        merge.sig.wrtmuc |= b->sig.wrtmuc;

        if (v3d_qpu_sig_writes_address(devinfo, &a->sig) &&
            v3d_qpu_sig_writes_address(devinfo, &b->sig))
                return false;
        merge.sig_addr |= b->sig_addr;
        merge.sig_magic |= b->sig_magic;

        uint64_t packed;
        bool ok = v3d_qpu_instr_pack(devinfo, &merge, &packed);

        *result = merge;
        /* No modifying the real instructions on failure. */
        assert(ok || (a != result && b != result));

        return ok;
}

static struct schedule_node *
choose_instruction_to_schedule(const struct v3d_device_info *devinfo,
                               struct choose_scoreboard *scoreboard,
                               struct list_head *schedule_list,
                               struct schedule_node *prev_inst)
{
        struct schedule_node *chosen = NULL;
        int chosen_prio = 0;

        /* Don't pair up anything with a thread switch signal -- emit_thrsw()
         * will handle pairing it along with filling the delay slots.
         */
        if (prev_inst) {
                if (prev_inst->inst->qpu.sig.thrsw)
                        return NULL;
        }

        list_for_each_entry(struct schedule_node, n, schedule_list, link) {
                const struct v3d_qpu_instr *inst = &n->inst->qpu;

                /* Don't choose the branch instruction until it's the last one
                 * left.  We'll move it up to fit its delay slots after we
                 * choose it.
                 */
                if (inst->type == V3D_QPU_INSTR_TYPE_BRANCH &&
                    !list_is_singular(schedule_list)) {
                        continue;
                }

                /* "An instruction must not read from a location in physical
                 *  regfile A or B that was written to by the previous
                 *  instruction."
                 */
                if (reads_too_soon_after_write(scoreboard, n->inst))
                        continue;

                if (writes_too_soon_after_write(devinfo, scoreboard, n->inst))
                        continue;

                /* "A scoreboard wait must not occur in the first two
                 *  instructions of a fragment shader. This is either the
                 *  explicit Wait for Scoreboard signal or an implicit wait
                 *  with the first tile-buffer read or write instruction."
                 */
                if (pixel_scoreboard_too_soon(scoreboard, inst))
                        continue;

                /* ldunif and ldvary both write r5, but ldunif does so a tick
                 * sooner.  If the ldvary's r5 wasn't used, then ldunif might
                 * otherwise get scheduled so ldunif and ldvary try to update
                 * r5 in the same tick.
                 */
                if ((inst->sig.ldunif || inst->sig.ldunifa) &&
                    scoreboard->tick == scoreboard->last_ldvary_tick + 1) {
                        continue;
                }

                /* If we're trying to pair with another instruction, check
                 * that they're compatible.
                 */
                if (prev_inst) {
                        /* Don't pair up a thread switch signal -- we'll
                         * handle pairing it when we pick it on its own.
                         */
                        if (inst->sig.thrsw)
                                continue;

                        if (prev_inst->inst->uniform != -1 &&
                            n->inst->uniform != -1)
                                continue;

                        /* Don't merge in something that will lock the TLB.
                         * Hopwefully what we have in inst will release some
                         * other instructions, allowing us to delay the
                         * TLB-locking instruction until later.
                         */
                        if (!scoreboard->tlb_locked && qpu_inst_is_tlb(inst))
                                continue;

                        struct v3d_qpu_instr merged_inst;
                        if (!qpu_merge_inst(devinfo, &merged_inst,
                                            &prev_inst->inst->qpu, inst)) {
                                continue;
                        }
                }

                int prio = get_instruction_priority(inst);

                /* Found a valid instruction.  If nothing better comes along,
                 * this one works.
                 */
                if (!chosen) {
                        chosen = n;
                        chosen_prio = prio;
                        continue;
                }

                if (prio > chosen_prio) {
                        chosen = n;
                        chosen_prio = prio;
                } else if (prio < chosen_prio) {
                        continue;
                }

                if (n->delay > chosen->delay) {
                        chosen = n;
                        chosen_prio = prio;
                } else if (n->delay < chosen->delay) {
                        continue;
                }
        }

        return chosen;
}

static void
update_scoreboard_for_magic_waddr(struct choose_scoreboard *scoreboard,
                                  enum v3d_qpu_waddr waddr)
{
        if (v3d_qpu_magic_waddr_is_sfu(waddr))
                scoreboard->last_sfu_write_tick = scoreboard->tick;
}

static void
update_scoreboard_for_chosen(struct choose_scoreboard *scoreboard,
                             const struct v3d_qpu_instr *inst)
{
        scoreboard->last_waddr_add = ~0;
        scoreboard->last_waddr_mul = ~0;

        if (inst->type == V3D_QPU_INSTR_TYPE_BRANCH)
                return;

        assert(inst->type == V3D_QPU_INSTR_TYPE_ALU);

        if (inst->alu.add.op != V3D_QPU_A_NOP)  {
                if (inst->alu.add.magic_write) {
                        update_scoreboard_for_magic_waddr(scoreboard,
                                                          inst->alu.add.waddr);
                } else {
                        scoreboard->last_waddr_add = inst->alu.add.waddr;
                }
        }

        if (inst->alu.mul.op != V3D_QPU_M_NOP) {
                if (inst->alu.mul.magic_write) {
                        update_scoreboard_for_magic_waddr(scoreboard,
                                                          inst->alu.mul.waddr);
                } else {
                        scoreboard->last_waddr_mul = inst->alu.mul.waddr;
                }
        }

        if (inst->sig.ldvary)
                scoreboard->last_ldvary_tick = scoreboard->tick;

        if (qpu_inst_is_tlb(inst))
                scoreboard->tlb_locked = true;
}

static void
dump_state(const struct v3d_device_info *devinfo,
           struct list_head *schedule_list)
{
        list_for_each_entry(struct schedule_node, n, schedule_list, link) {
                fprintf(stderr, "         t=%4d: ", n->unblocked_time);
                v3d_qpu_dump(devinfo, &n->inst->qpu);
                fprintf(stderr, "\n");

                for (int i = 0; i < n->child_count; i++) {
                        struct schedule_node *child = n->children[i].node;
                        if (!child)
                                continue;

                        fprintf(stderr, "                 - ");
                        v3d_qpu_dump(devinfo, &child->inst->qpu);
                        fprintf(stderr, " (%d parents, %c)\n",
                                child->parent_count,
                                n->children[i].write_after_read ? 'w' : 'r');
                }
        }
}

static uint32_t magic_waddr_latency(enum v3d_qpu_waddr waddr,
                                    const struct v3d_qpu_instr *after)
{
        /* Apply some huge latency between texture fetch requests and getting
         * their results back.
         *
         * FIXME: This is actually pretty bogus.  If we do:
         *
         * mov tmu0_s, a
         * <a bit of math>
         * mov tmu0_s, b
         * load_tmu0
         * <more math>
         * load_tmu0
         *
         * we count that as worse than
         *
         * mov tmu0_s, a
         * mov tmu0_s, b
         * <lots of math>
         * load_tmu0
         * <more math>
         * load_tmu0
         *
         * because we associate the first load_tmu0 with the *second* tmu0_s.
         */
        if (v3d_qpu_magic_waddr_is_tmu(waddr) && after->sig.ldtmu)
                return 100;

        /* Assume that anything depending on us is consuming the SFU result. */
        if (v3d_qpu_magic_waddr_is_sfu(waddr))
                return 3;

        return 1;
}

static uint32_t
instruction_latency(struct schedule_node *before, struct schedule_node *after)
{
        const struct v3d_qpu_instr *before_inst = &before->inst->qpu;
        const struct v3d_qpu_instr *after_inst = &after->inst->qpu;
        uint32_t latency = 1;

        if (before_inst->type != V3D_QPU_INSTR_TYPE_ALU ||
            after_inst->type != V3D_QPU_INSTR_TYPE_ALU)
                return latency;

        if (before_inst->alu.add.magic_write) {
                latency = MAX2(latency,
                               magic_waddr_latency(before_inst->alu.add.waddr,
                                                   after_inst));
        }

        if (before_inst->alu.mul.magic_write) {
                latency = MAX2(latency,
                               magic_waddr_latency(before_inst->alu.mul.waddr,
                                                   after_inst));
        }

        return latency;
}

/** Recursive computation of the delay member of a node. */
static void
compute_delay(struct schedule_node *n)
{
        if (!n->child_count) {
                n->delay = 1;
        } else {
                for (int i = 0; i < n->child_count; i++) {
                        if (!n->children[i].node->delay)
                                compute_delay(n->children[i].node);
                        n->delay = MAX2(n->delay,
                                        n->children[i].node->delay +
                                        instruction_latency(n, n->children[i].node));
                }
        }
}

static void
mark_instruction_scheduled(struct list_head *schedule_list,
                           uint32_t time,
                           struct schedule_node *node,
                           bool war_only)
{
        if (!node)
                return;

        for (int i = node->child_count - 1; i >= 0; i--) {
                struct schedule_node *child =
                        node->children[i].node;

                if (!child)
                        continue;

                if (war_only && !node->children[i].write_after_read)
                        continue;

                /* If the requirement is only that the node not appear before
                 * the last read of its destination, then it can be scheduled
                 * immediately after (or paired with!) the thing reading the
                 * destination.
                 */
                uint32_t latency = 0;
                if (!war_only) {
                        latency = instruction_latency(node,
                                                      node->children[i].node);
                }

                child->unblocked_time = MAX2(child->unblocked_time,
                                             time + latency);
                child->parent_count--;
                if (child->parent_count == 0)
                        list_add(&child->link, schedule_list);

                node->children[i].node = NULL;
        }
}

static void
insert_scheduled_instruction(struct v3d_compile *c,
                             struct qblock *block,
                             struct choose_scoreboard *scoreboard,
                             struct qinst *inst)
{
        list_addtail(&inst->link, &block->instructions);

        update_scoreboard_for_chosen(scoreboard, &inst->qpu);
        c->qpu_inst_count++;
        scoreboard->tick++;
}

static struct qinst *
vir_nop()
{
        struct qreg undef = { QFILE_NULL, 0 };
        struct qinst *qinst = vir_add_inst(V3D_QPU_A_NOP, undef, undef, undef);

        return qinst;
}

static void
emit_nop(struct v3d_compile *c, struct qblock *block,
         struct choose_scoreboard *scoreboard)
{
        insert_scheduled_instruction(c, block, scoreboard, vir_nop());
}

static bool
qpu_instruction_valid_in_thrend_slot(struct v3d_compile *c,
                                     const struct qinst *qinst, int slot)
{
        const struct v3d_qpu_instr *inst = &qinst->qpu;

        /* Only TLB Z writes are prohibited in the last slot, but we don't
         * have those flagged so prohibit all TLB ops for now.
         */
        if (slot == 2 && qpu_inst_is_tlb(inst))
                return false;

        if (slot > 0 && qinst->uniform != ~0)
                return false;

        if (v3d_qpu_uses_vpm(inst))
                return false;

        if (inst->sig.ldvary)
                return false;

        if (inst->type == V3D_QPU_INSTR_TYPE_ALU) {
                /* No writing physical registers at the end. */
                if (!inst->alu.add.magic_write ||
                    !inst->alu.mul.magic_write) {
                        return false;
                }

                if (c->devinfo->ver < 40 && inst->alu.add.op == V3D_QPU_A_SETMSF)
                        return false;

                /* RF0-2 might be overwritten during the delay slots by
                 * fragment shader setup.
                 */
                if (inst->raddr_a < 3 &&
                    (inst->alu.add.a == V3D_QPU_MUX_A ||
                     inst->alu.add.b == V3D_QPU_MUX_A ||
                     inst->alu.mul.a == V3D_QPU_MUX_A ||
                     inst->alu.mul.b == V3D_QPU_MUX_A)) {
                        return false;
                }

                if (inst->raddr_b < 3 &&
                    !inst->sig.small_imm &&
                    (inst->alu.add.a == V3D_QPU_MUX_B ||
                     inst->alu.add.b == V3D_QPU_MUX_B ||
                     inst->alu.mul.a == V3D_QPU_MUX_B ||
                     inst->alu.mul.b == V3D_QPU_MUX_B)) {
                        return false;
                }
        }

        return true;
}

static bool
valid_thrsw_sequence(struct v3d_compile *c,
                     struct qinst *qinst, int instructions_in_sequence,
                     bool is_thrend)
{
        for (int slot = 0; slot < instructions_in_sequence; slot++) {
                /* No scheduling SFU when the result would land in the other
                 * thread.  The simulator complains for safety, though it
                 * would only occur for dead code in our case.
                 */
                if (slot > 0 &&
                    qinst->qpu.type == V3D_QPU_INSTR_TYPE_ALU &&
                    (v3d_qpu_magic_waddr_is_sfu(qinst->qpu.alu.add.waddr) ||
                     v3d_qpu_magic_waddr_is_sfu(qinst->qpu.alu.mul.waddr))) {
                        return false;
                }

                if (slot > 0 && qinst->qpu.sig.ldvary)
                        return false;

                if (is_thrend &&
                    !qpu_instruction_valid_in_thrend_slot(c, qinst, slot)) {
                        return false;
                }

                /* Note that the list is circular, so we can only do this up
                 * to instructions_in_sequence.
                 */
                qinst = (struct qinst *)qinst->link.next;
        }

        return true;
}

/**
 * Emits a THRSW signal in the stream, trying to move it up to pair with
 * another instruction.
 */
static int
emit_thrsw(struct v3d_compile *c,
           struct qblock *block,
           struct choose_scoreboard *scoreboard,
           struct qinst *inst,
           bool is_thrend)
{
        int time = 0;

        /* There should be nothing in a thrsw inst being scheduled other than
         * the signal bits.
         */
        assert(inst->qpu.type == V3D_QPU_INSTR_TYPE_ALU);
        assert(inst->qpu.alu.add.op == V3D_QPU_A_NOP);
        assert(inst->qpu.alu.mul.op == V3D_QPU_M_NOP);

        /* Find how far back into previous instructions we can put the THRSW. */
        int slots_filled = 0;
        struct qinst *merge_inst = NULL;
        vir_for_each_inst_rev(prev_inst, block) {
                struct v3d_qpu_sig sig = prev_inst->qpu.sig;
                sig.thrsw = true;
                uint32_t packed_sig;

                if (!v3d_qpu_sig_pack(c->devinfo, &sig, &packed_sig))
                        break;

                if (!valid_thrsw_sequence(c, prev_inst, slots_filled + 1,
                                          is_thrend)) {
                        break;
                }

                merge_inst = prev_inst;
                if (++slots_filled == 3)
                        break;
        }

        bool needs_free = false;
        if (merge_inst) {
                merge_inst->qpu.sig.thrsw = true;
                needs_free = true;
        } else {
                insert_scheduled_instruction(c, block, scoreboard, inst);
                time++;
                slots_filled++;
                merge_inst = inst;
        }

        /* Insert any extra delay slot NOPs we need. */
        for (int i = 0; i < 3 - slots_filled; i++) {
                emit_nop(c, block, scoreboard);
                time++;
        }

        /* If we're emitting the last THRSW (other than program end), then
         * signal that to the HW by emitting two THRSWs in a row.
         */
        if (inst->is_last_thrsw) {
                struct qinst *second_inst =
                        (struct qinst *)merge_inst->link.next;
                second_inst->qpu.sig.thrsw = true;
        }

        /* If we put our THRSW into another instruction, free up the
         * instruction that didn't end up scheduled into the list.
         */
        if (needs_free)
                free(inst);

        return time;
}

static uint32_t
schedule_instructions(struct v3d_compile *c,
                      struct choose_scoreboard *scoreboard,
                      struct qblock *block,
                      struct list_head *schedule_list,
                      enum quniform_contents *orig_uniform_contents,
                      uint32_t *orig_uniform_data,
                      uint32_t *next_uniform)
{
        const struct v3d_device_info *devinfo = c->devinfo;
        uint32_t time = 0;

        if (debug) {
                fprintf(stderr, "initial deps:\n");
                dump_state(devinfo, schedule_list);
                fprintf(stderr, "\n");
        }

        /* Remove non-DAG heads from the list. */
        list_for_each_entry_safe(struct schedule_node, n, schedule_list, link) {
                if (n->parent_count != 0)
                        list_del(&n->link);
        }

        while (!list_empty(schedule_list)) {
                struct schedule_node *chosen =
                        choose_instruction_to_schedule(devinfo,
                                                       scoreboard,
                                                       schedule_list,
                                                       NULL);
                struct schedule_node *merge = NULL;

                /* If there are no valid instructions to schedule, drop a NOP
                 * in.
                 */
                struct qinst *qinst = chosen ? chosen->inst : vir_nop();
                struct v3d_qpu_instr *inst = &qinst->qpu;

                if (debug) {
                        fprintf(stderr, "t=%4d: current list:\n",
                                time);
                        dump_state(devinfo, schedule_list);
                        fprintf(stderr, "t=%4d: chose:   ", time);
                        v3d_qpu_dump(devinfo, inst);
                        fprintf(stderr, "\n");
                }

                /* We can't mark_instruction_scheduled() the chosen inst until
                 * we're done identifying instructions to merge, so put the
                 * merged instructions on a list for a moment.
                 */
                struct list_head merged_list;
                list_inithead(&merged_list);

                /* Schedule this instruction onto the QPU list. Also try to
                 * find an instruction to pair with it.
                 */
                if (chosen) {
                        time = MAX2(chosen->unblocked_time, time);
                        list_del(&chosen->link);
                        mark_instruction_scheduled(schedule_list, time,
                                                   chosen, true);

                        while ((merge =
                                choose_instruction_to_schedule(devinfo,
                                                               scoreboard,
                                                               schedule_list,
                                                               chosen))) {
                                time = MAX2(merge->unblocked_time, time);
                                list_del(&merge->link);
                                list_addtail(&merge->link, &merged_list);
                                (void)qpu_merge_inst(devinfo, inst,
                                                     inst, &merge->inst->qpu);
                                if (merge->inst->uniform != -1) {
                                        chosen->inst->uniform =
                                                merge->inst->uniform;
                                }

                                if (debug) {
                                        fprintf(stderr, "t=%4d: merging: ",
                                                time);
                                        v3d_qpu_dump(devinfo, &merge->inst->qpu);
                                        fprintf(stderr, "\n");
                                        fprintf(stderr, "         result: ");
                                        v3d_qpu_dump(devinfo, inst);
                                        fprintf(stderr, "\n");
                                }
                        }
                }

                /* Update the uniform index for the rewritten location --
                 * branch target updating will still need to change
                 * c->uniform_data[] using this index.
                 */
                if (qinst->uniform != -1) {
                        if (inst->type == V3D_QPU_INSTR_TYPE_BRANCH)
                                block->branch_uniform = *next_uniform;

                        c->uniform_data[*next_uniform] =
                                orig_uniform_data[qinst->uniform];
                        c->uniform_contents[*next_uniform] =
                                orig_uniform_contents[qinst->uniform];
                        qinst->uniform = *next_uniform;
                        (*next_uniform)++;
                }

                if (debug) {
                        fprintf(stderr, "\n");
                }

                /* Now that we've scheduled a new instruction, some of its
                 * children can be promoted to the list of instructions ready to
                 * be scheduled.  Update the children's unblocked time for this
                 * DAG edge as we do so.
                 */
                mark_instruction_scheduled(schedule_list, time, chosen, false);
                list_for_each_entry(struct schedule_node, merge, &merged_list,
                                    link) {
                        mark_instruction_scheduled(schedule_list, time, merge,
                                                   false);

                        /* The merged VIR instruction doesn't get re-added to the
                         * block, so free it now.
                         */
                        free(merge->inst);
                }

                if (inst->sig.thrsw) {
                        time += emit_thrsw(c, block, scoreboard, qinst, false);
                } else {
                        insert_scheduled_instruction(c, block,
                                                     scoreboard, qinst);

                        if (inst->type == V3D_QPU_INSTR_TYPE_BRANCH) {
                                block->branch_qpu_ip = c->qpu_inst_count - 1;
                                /* Fill the delay slots.
                                 *
                                 * We should fill these with actual instructions,
                                 * instead, but that will probably need to be done
                                 * after this, once we know what the leading
                                 * instructions of the successors are (so we can
                                 * handle A/B register file write latency)
                                 */
                                for (int i = 0; i < 3; i++)
                                        emit_nop(c, block, scoreboard);
                        }
                }
        }

        return time;
}

static uint32_t
qpu_schedule_instructions_block(struct v3d_compile *c,
                                struct choose_scoreboard *scoreboard,
                                struct qblock *block,
                                enum quniform_contents *orig_uniform_contents,
                                uint32_t *orig_uniform_data,
                                uint32_t *next_uniform)
{
        void *mem_ctx = ralloc_context(NULL);
        struct list_head schedule_list;

        list_inithead(&schedule_list);

        /* Wrap each instruction in a scheduler structure. */
        while (!list_empty(&block->instructions)) {
                struct qinst *qinst = (struct qinst *)block->instructions.next;
                struct schedule_node *n =
                        rzalloc(mem_ctx, struct schedule_node);

                n->inst = qinst;

                list_del(&qinst->link);
                list_addtail(&n->link, &schedule_list);
        }

        calculate_forward_deps(c, &schedule_list);
        calculate_reverse_deps(c, &schedule_list);

        list_for_each_entry(struct schedule_node, n, &schedule_list, link) {
                compute_delay(n);
        }

        uint32_t cycles = schedule_instructions(c, scoreboard, block,
                                                &schedule_list,
                                                orig_uniform_contents,
                                                orig_uniform_data,
                                                next_uniform);

        ralloc_free(mem_ctx);

        return cycles;
}

static void
qpu_set_branch_targets(struct v3d_compile *c)
{
        vir_for_each_block(block, c) {
                /* The end block of the program has no branch. */
                if (!block->successors[0])
                        continue;

                /* If there was no branch instruction, then the successor
                 * block must follow immediately after this one.
                 */
                if (block->branch_qpu_ip == ~0) {
                        assert(block->end_qpu_ip + 1 ==
                               block->successors[0]->start_qpu_ip);
                        continue;
                }

                /* Walk back through the delay slots to find the branch
                 * instr.
                 */
                struct list_head *entry = block->instructions.prev;
                for (int i = 0; i < 3; i++)
                        entry = entry->prev;
                struct qinst *branch = container_of(entry, branch, link);
                assert(branch->qpu.type == V3D_QPU_INSTR_TYPE_BRANCH);

                /* Make sure that the if-we-don't-jump
                 * successor was scheduled just after the
                 * delay slots.
                 */
                assert(!block->successors[1] ||
                       block->successors[1]->start_qpu_ip ==
                       block->branch_qpu_ip + 4);

                branch->qpu.branch.offset =
                        ((block->successors[0]->start_qpu_ip -
                          (block->branch_qpu_ip + 4)) *
                         sizeof(uint64_t));

                /* Set up the relative offset to jump in the
                 * uniform stream.
                 *
                 * Use a temporary here, because
                 * uniform_data[inst->uniform] may be shared
                 * between multiple instructions.
                 */
                assert(c->uniform_contents[branch->uniform] == QUNIFORM_CONSTANT);
                c->uniform_data[branch->uniform] =
                        (block->successors[0]->start_uniform -
                         (block->branch_uniform + 1)) * 4;
        }
}

uint32_t
v3d_qpu_schedule_instructions(struct v3d_compile *c)
{
        const struct v3d_device_info *devinfo = c->devinfo;
        struct qblock *end_block = list_last_entry(&c->blocks,
                                                   struct qblock, link);

        /* We reorder the uniforms as we schedule instructions, so save the
         * old data off and replace it.
         */
        uint32_t *uniform_data = c->uniform_data;
        enum quniform_contents *uniform_contents = c->uniform_contents;
        c->uniform_contents = ralloc_array(c, enum quniform_contents,
                                           c->num_uniforms);
        c->uniform_data = ralloc_array(c, uint32_t, c->num_uniforms);
        c->uniform_array_size = c->num_uniforms;
        uint32_t next_uniform = 0;

        struct choose_scoreboard scoreboard;
        memset(&scoreboard, 0, sizeof(scoreboard));
        scoreboard.last_waddr_add = ~0;
        scoreboard.last_waddr_mul = ~0;
        scoreboard.last_ldvary_tick = -10;
        scoreboard.last_sfu_write_tick = -10;
        scoreboard.last_uniforms_reset_tick = -10;

        if (debug) {
                fprintf(stderr, "Pre-schedule instructions\n");
                vir_for_each_block(block, c) {
                        fprintf(stderr, "BLOCK %d\n", block->index);
                        list_for_each_entry(struct qinst, qinst,
                                            &block->instructions, link) {
                                v3d_qpu_dump(devinfo, &qinst->qpu);
                                fprintf(stderr, "\n");
                        }
                }
                fprintf(stderr, "\n");
        }

        uint32_t cycles = 0;
        vir_for_each_block(block, c) {
                block->start_qpu_ip = c->qpu_inst_count;
                block->branch_qpu_ip = ~0;
                block->start_uniform = next_uniform;

                cycles += qpu_schedule_instructions_block(c,
                                                          &scoreboard,
                                                          block,
                                                          uniform_contents,
                                                          uniform_data,
                                                          &next_uniform);

                block->end_qpu_ip = c->qpu_inst_count - 1;
        }

        /* Emit the program-end THRSW instruction. */;
        struct qinst *thrsw = vir_nop();
        thrsw->qpu.sig.thrsw = true;
        emit_thrsw(c, end_block, &scoreboard, thrsw, true);

        qpu_set_branch_targets(c);

        assert(next_uniform == c->num_uniforms);

        return cycles;
}
