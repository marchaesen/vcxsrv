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

#include "util/ralloc.h"
#include "util/register_allocate.h"
#include "common/v3d_device_info.h"
#include "v3d_compiler.h"

#define QPU_R(i) { .magic = false, .index = i }

#define ACC_INDEX     0
#define ACC_COUNT     6
#define PHYS_INDEX    (ACC_INDEX + ACC_COUNT)
#define PHYS_COUNT    64

static inline bool
qinst_writes_tmu(struct qinst *inst)
{
        return (inst->dst.file == QFILE_MAGIC &&
                v3d_qpu_magic_waddr_is_tmu(inst->dst.index)) ||
                inst->qpu.sig.wrtmuc;
}

static bool
is_end_of_tmu_sequence(struct qinst *inst, struct qblock *block)
{
        if (inst->qpu.type == V3D_QPU_INSTR_TYPE_ALU &&
            inst->qpu.alu.add.op == V3D_QPU_A_TMUWT)
                return true;

        if (!inst->qpu.sig.ldtmu)
                return false;

        list_for_each_entry_from(struct qinst, scan_inst, inst->link.next,
                                 &block->instructions, link) {
                if (scan_inst->qpu.sig.ldtmu)
                        return false;
                if (qinst_writes_tmu(scan_inst))
                        return true;
        }

        return true;
}

static bool
vir_is_mov_uniform(struct v3d_compile *c, int temp)
{
        struct qinst *def = c->defs[temp];

        return def && def->qpu.sig.ldunif;
}

static int
v3d_choose_spill_node(struct v3d_compile *c, struct ra_graph *g,
                      uint32_t *temp_to_node)
{
        const float tmu_scale = 5;
        float block_scale = 1.0;
        float spill_costs[c->num_temps];
        bool in_tmu_operation = false;
        bool started_last_seg = false;

        for (unsigned i = 0; i < c->num_temps; i++)
                spill_costs[i] = 0.0;

        /* XXX: Scale the cost up when inside of a loop. */
        vir_for_each_block(block, c) {
                vir_for_each_inst(inst, block) {
                        /* We can't insert new thread switches after
                         * starting output writes.
                         */
                        bool no_spilling =
                                c->threads > 1 && started_last_seg;

                        /* Discourage spilling of TMU operations */
                        for (int i = 0; i < vir_get_nsrc(inst); i++) {
                                if (inst->src[i].file != QFILE_TEMP)
                                        continue;

                                int temp = inst->src[i].index;
                                if (vir_is_mov_uniform(c, temp)) {
                                        spill_costs[temp] += block_scale;
                                } else if (!no_spilling) {
                                        float tmu_op_scale = in_tmu_operation ?
                                                3.0 : 1.0;
                                        spill_costs[temp] += (block_scale *
                                                              tmu_scale *
                                                              tmu_op_scale);
                                } else {
                                        BITSET_CLEAR(c->spillable, temp);
                                }
                        }

                        if (inst->dst.file == QFILE_TEMP) {
                                int temp = inst->dst.index;

                                if (vir_is_mov_uniform(c, temp)) {
                                        /* We just rematerialize the unform
                                         * later.
                                         */
                                } else if (!no_spilling) {
                                        spill_costs[temp] += (block_scale *
                                                              tmu_scale);
                                } else {
                                        BITSET_CLEAR(c->spillable, temp);
                                }
                        }

                        /* Refuse to spill a ldvary's dst, because that means
                         * that ldvary's r5 would end up being used across a
                         * thrsw.
                         */
                        if (inst->qpu.sig.ldvary) {
                                assert(inst->dst.file == QFILE_TEMP);
                                BITSET_CLEAR(c->spillable, inst->dst.index);
                        }

                        if (inst->is_last_thrsw)
                                started_last_seg = true;

                        if (v3d_qpu_writes_vpm(&inst->qpu) ||
                            v3d_qpu_uses_tlb(&inst->qpu))
                                started_last_seg = true;

                        /* Track when we're in between a TMU setup and the
                         * final LDTMU or TMUWT from that TMU setup.  We
                         * penalize spills during that time.
                         */
                        if (is_end_of_tmu_sequence(inst, block))
                                in_tmu_operation = false;

                        if (qinst_writes_tmu(inst))
                                in_tmu_operation = true;
                }
        }

        for (unsigned i = 0; i < c->num_temps; i++) {
                int node = temp_to_node[i];

                if (BITSET_TEST(c->spillable, i))
                        ra_set_node_spill_cost(g, node, spill_costs[i]);
        }

        return ra_get_best_spill_node(g);
}

/* The spill offset for this thread takes a bit of setup, so do it once at
 * program start.
 */
void
v3d_setup_spill_base(struct v3d_compile *c)
{
        c->cursor = vir_before_block(vir_entry_block(c));

        int start_num_temps = c->num_temps;

        /* Each thread wants to be in a separate region of the scratch space
         * so that the QPUs aren't fighting over cache lines.  We have the
         * driver keep a single global spill BO rather than
         * per-spilling-program BOs, so we need a uniform from the driver for
         * what the per-thread scale is.
         */
        struct qreg thread_offset =
                vir_UMUL(c,
                         vir_TIDX(c),
                         vir_uniform(c, QUNIFORM_SPILL_SIZE_PER_THREAD, 0));

        /* Each channel in a reg is 4 bytes, so scale them up by that. */
        struct qreg element_offset = vir_SHL(c, vir_EIDX(c),
                                             vir_uniform_ui(c, 2));

        c->spill_base = vir_ADD(c,
                                vir_ADD(c, thread_offset, element_offset),
                                vir_uniform(c, QUNIFORM_SPILL_OFFSET, 0));

        /* Make sure that we don't spill the spilling setup instructions. */
        for (int i = start_num_temps; i < c->num_temps; i++)
                BITSET_CLEAR(c->spillable, i);

        c->cursor = vir_after_block(c->cur_block);
}

static void
v3d_emit_spill_tmua(struct v3d_compile *c, uint32_t spill_offset)
{
        vir_ADD_dest(c, vir_reg(QFILE_MAGIC,
                                V3D_QPU_WADDR_TMUA),
                     c->spill_base,
                     vir_uniform_ui(c, spill_offset));
}


static void
v3d_emit_tmu_spill(struct v3d_compile *c, struct qinst *inst,
                   struct qinst *position, uint32_t spill_offset)
{
        c->cursor = vir_after_inst(position);
        inst->dst.index = c->num_temps++;
        vir_MOV_dest(c, vir_reg(QFILE_MAGIC,
                                V3D_QPU_WADDR_TMUD),
                     inst->dst);
        v3d_emit_spill_tmua(c, spill_offset);
        vir_emit_thrsw(c);
        vir_TMUWT(c);
        c->spills++;
        c->tmu_dirty_rcl = true;
}

static void
v3d_spill_reg(struct v3d_compile *c, int spill_temp)
{
        c->spill_count++;

        bool is_uniform = vir_is_mov_uniform(c, spill_temp);

        uint32_t spill_offset = 0;

        if (!is_uniform) {
                spill_offset = c->spill_size;
                c->spill_size += V3D_CHANNELS * sizeof(uint32_t);

                if (spill_offset == 0)
                        v3d_setup_spill_base(c);
        }

        struct qinst *last_thrsw = c->last_thrsw;
        assert(!last_thrsw || last_thrsw->is_last_thrsw);

        int start_num_temps = c->num_temps;

        int uniform_index = ~0;
        if (is_uniform) {
                struct qinst *orig_unif = c->defs[spill_temp];
                uniform_index = orig_unif->uniform;
        }

        struct qinst *start_of_tmu_sequence = NULL;
        struct qinst *postponed_spill = NULL;
        vir_for_each_block(block, c) {
                vir_for_each_inst_safe(inst, block) {
                        /* Track when we're in between a TMU setup and the final
                         * LDTMU or TMUWT from that TMU setup. We can't spill/fill any
                         * temps during that time, because that involves inserting a
                         * new TMU setup/LDTMU sequence, so we postpone the spill or
                         * move the fill up to not intrude in the middle of the TMU
                         * sequence.
                         */
                        if (is_end_of_tmu_sequence(inst, block)) {
                                if (postponed_spill) {
                                        v3d_emit_tmu_spill(c, postponed_spill,
                                                           inst, spill_offset);
                                }

                                start_of_tmu_sequence = NULL;
                                postponed_spill = NULL;
                        }

                        if (!start_of_tmu_sequence && qinst_writes_tmu(inst))
                                start_of_tmu_sequence = inst;

                        /* fills */
                        for (int i = 0; i < vir_get_nsrc(inst); i++) {
                                if (inst->src[i].file != QFILE_TEMP ||
                                    inst->src[i].index != spill_temp) {
                                        continue;
                                }

                                c->cursor = vir_before_inst(inst);

                                if (is_uniform) {
                                        struct qreg unif =
                                                vir_uniform(c,
                                                            c->uniform_contents[uniform_index],
                                                            c->uniform_data[uniform_index]);
                                        inst->src[i] = unif;
                                } else {
                                        /* If we have a postponed spill, we don't need
                                         * a fill as the temp would not have been
                                         * spilled yet.
                                         */
                                        if (postponed_spill)
                                                continue;
                                        if (start_of_tmu_sequence)
                                                c->cursor = vir_before_inst(start_of_tmu_sequence);

                                        v3d_emit_spill_tmua(c, spill_offset);
                                        vir_emit_thrsw(c);
                                        inst->src[i] = vir_LDTMU(c);
                                        c->fills++;
                                }
                        }

                        /* spills */
                        if (inst->dst.file == QFILE_TEMP &&
                            inst->dst.index == spill_temp) {
                                if (is_uniform) {
                                        c->cursor.link = NULL;
                                        vir_remove_instruction(c, inst);
                                } else {
                                        if (start_of_tmu_sequence)
                                                postponed_spill = inst;
                                        else
                                                v3d_emit_tmu_spill(c, inst, inst,
                                                                   spill_offset);
                                }
                        }

                        /* If we didn't have a last-thrsw inserted by nir_to_vir and
                         * we've been inserting thrsws, then insert a new last_thrsw
                         * right before we start the vpm/tlb sequence for the last
                         * thread segment.
                         */
                        if (!is_uniform && !last_thrsw && c->last_thrsw &&
                            (v3d_qpu_writes_vpm(&inst->qpu) ||
                             v3d_qpu_uses_tlb(&inst->qpu))) {
                                c->cursor = vir_before_inst(inst);
                                vir_emit_thrsw(c);

                                last_thrsw = c->last_thrsw;
                                last_thrsw->is_last_thrsw = true;
                        }
                }
        }

        /* Make sure c->last_thrsw is the actual last thrsw, not just one we
         * inserted in our most recent unspill.
         */
        if (last_thrsw)
                c->last_thrsw = last_thrsw;

        /* Don't allow spilling of our spilling instructions.  There's no way
         * they can help get things colored.
         */
        for (int i = start_num_temps; i < c->num_temps; i++)
                BITSET_CLEAR(c->spillable, i);
}

struct v3d_ra_select_callback_data {
        uint32_t next_acc;
        uint32_t next_phys;
};

static unsigned int
v3d_ra_select_callback(unsigned int n, BITSET_WORD *regs, void *data)
{
        struct v3d_ra_select_callback_data *v3d_ra = data;
        int r5 = ACC_INDEX + 5;

        /* Choose r5 for our ldunifs if possible (nobody else can load to that
         * reg, and it keeps the QPU cond field free from being occupied by
         * ldunifrf).
         */
        if (BITSET_TEST(regs, r5))
                return r5;

        /* Choose an accumulator if possible (I think it's lower power than
         * phys regs), but round-robin through them to give post-RA
         * instruction selection more options.
         */
        for (int i = 0; i < ACC_COUNT; i++) {
                int acc_off = (v3d_ra->next_acc + i) % ACC_COUNT;
                int acc = ACC_INDEX + acc_off;

                if (BITSET_TEST(regs, acc)) {
                        v3d_ra->next_acc = acc_off + 1;
                        return acc;
                }
        }

        for (int i = 0; i < PHYS_COUNT; i++) {
                int phys_off = (v3d_ra->next_phys + i) % PHYS_COUNT;
                int phys = PHYS_INDEX + phys_off;

                if (BITSET_TEST(regs, phys)) {
                        v3d_ra->next_phys = phys_off + 1;
                        return phys;
                }
        }

        unreachable("RA must pass us at least one possible reg.");
}

bool
vir_init_reg_sets(struct v3d_compiler *compiler)
{
        /* Allocate up to 3 regfile classes, for the ways the physical
         * register file can be divided up for fragment shader threading.
         */
        int max_thread_index = (compiler->devinfo->ver >= 40 ? 2 : 3);

        compiler->regs = ra_alloc_reg_set(compiler, PHYS_INDEX + PHYS_COUNT,
                                          true);
        if (!compiler->regs)
                return false;

        for (int threads = 0; threads < max_thread_index; threads++) {
                compiler->reg_class_any[threads] =
                        ra_alloc_reg_class(compiler->regs);
                compiler->reg_class_r5[threads] =
                        ra_alloc_reg_class(compiler->regs);
                compiler->reg_class_phys_or_acc[threads] =
                        ra_alloc_reg_class(compiler->regs);
                compiler->reg_class_phys[threads] =
                        ra_alloc_reg_class(compiler->regs);

                for (int i = PHYS_INDEX;
                     i < PHYS_INDEX + (PHYS_COUNT >> threads); i++) {
                        ra_class_add_reg(compiler->regs,
                                         compiler->reg_class_phys_or_acc[threads], i);
                        ra_class_add_reg(compiler->regs,
                                         compiler->reg_class_phys[threads], i);
                        ra_class_add_reg(compiler->regs,
                                         compiler->reg_class_any[threads], i);
                }

                for (int i = ACC_INDEX + 0; i < ACC_INDEX + ACC_COUNT - 1; i++) {
                        ra_class_add_reg(compiler->regs,
                                         compiler->reg_class_phys_or_acc[threads], i);
                        ra_class_add_reg(compiler->regs,
                                         compiler->reg_class_any[threads], i);
                }
                /* r5 can only store a single 32-bit value, so not much can
                 * use it.
                 */
                ra_class_add_reg(compiler->regs,
                                 compiler->reg_class_r5[threads],
                                 ACC_INDEX + 5);
                ra_class_add_reg(compiler->regs,
                                 compiler->reg_class_any[threads],
                                 ACC_INDEX + 5);
        }

        ra_set_finalize(compiler->regs, NULL);

        return true;
}

struct node_to_temp_map {
        uint32_t temp;
        uint32_t priority;
};

static int
node_to_temp_priority(const void *in_a, const void *in_b)
{
        const struct node_to_temp_map *a = in_a;
        const struct node_to_temp_map *b = in_b;

        return a->priority - b->priority;
}

/**
 * Computes the number of registers to spill in a batch after a register
 * allocation failure.
 */
static uint32_t
get_spill_batch_size(struct v3d_compile *c)
{
   /* Allow up to 10 spills in batches of 1 in any case to avoid any chance of
    * over-spilling if the program requires few spills to compile.
    */
   if (c->spill_count < 10)
           return 1;

   /* If we have to spill more than that we assume performance is not going to
    * be great and we shift focus to batching spills to cut down compile
    * time at the expense of over-spilling.
    */
   return 20;
}

#define CLASS_BIT_PHYS			(1 << 0)
#define CLASS_BIT_ACC			(1 << 1)
#define CLASS_BIT_R5			(1 << 4)
#define CLASS_BITS_ANY			(CLASS_BIT_PHYS | \
                                         CLASS_BIT_ACC | \
                                         CLASS_BIT_R5)

/**
 * Returns a mapping from QFILE_TEMP indices to struct qpu_regs.
 *
 * The return value should be freed by the caller.
 */
struct qpu_reg *
v3d_register_allocate(struct v3d_compile *c, bool *spilled)
{
        struct node_to_temp_map map[c->num_temps];
        uint32_t temp_to_node[c->num_temps];
        uint8_t class_bits[c->num_temps];
        int acc_nodes[ACC_COUNT];
        struct v3d_ra_select_callback_data callback_data = {
                .next_acc = 0,
                /* Start at RF3, to try to keep the TLB writes from using
                 * RF0-2.
                 */
                .next_phys = 3,
        };

        *spilled = false;

        vir_calculate_live_intervals(c);

        /* Convert 1, 2, 4 threads to 0, 1, 2 index.
         *
         * V3D 4.x has double the physical register space, so 64 physical regs
         * are available at both 1x and 2x threading, and 4x has 32.
         */
        int thread_index = ffs(c->threads) - 1;
        if (c->devinfo->ver >= 40) {
                if (thread_index >= 1)
                        thread_index--;
        }

        struct ra_graph *g = ra_alloc_interference_graph(c->compiler->regs,
                                                         c->num_temps +
                                                         ARRAY_SIZE(acc_nodes));
        ra_set_select_reg_callback(g, v3d_ra_select_callback, &callback_data);

        /* Make some fixed nodes for the accumulators, which we will need to
         * interfere with when ops have implied r3/r4 writes or for the thread
         * switches.  We could represent these as classes for the nodes to
         * live in, but the classes take up a lot of memory to set up, so we
         * don't want to make too many.
         */
        for (int i = 0; i < ARRAY_SIZE(acc_nodes); i++) {
                acc_nodes[i] = c->num_temps + i;
                ra_set_node_reg(g, acc_nodes[i], ACC_INDEX + i);
        }

        for (uint32_t i = 0; i < c->num_temps; i++) {
                map[i].temp = i;
                map[i].priority = c->temp_end[i] - c->temp_start[i];
        }
        qsort(map, c->num_temps, sizeof(map[0]), node_to_temp_priority);
        for (uint32_t i = 0; i < c->num_temps; i++) {
                temp_to_node[map[i].temp] = i;
        }

        /* Figure out our register classes and preallocated registers.  We
         * start with any temp being able to be in any file, then instructions
         * incrementally remove bits that the temp definitely can't be in.
         */
        memset(class_bits, CLASS_BITS_ANY, sizeof(class_bits));

        int ip = 0;
        vir_for_each_inst_inorder(inst, c) {
                /* If the instruction writes r3/r4 (and optionally moves its
                 * result to a temp), nothing else can be stored in r3/r4 across
                 * it.
                 */
                if (vir_writes_r3(c->devinfo, inst)) {
                        for (int i = 0; i < c->num_temps; i++) {
                                if (c->temp_start[i] < ip &&
                                    c->temp_end[i] > ip) {
                                        ra_add_node_interference(g,
                                                                 temp_to_node[i],
                                                                 acc_nodes[3]);
                                }
                        }
                }
                if (vir_writes_r4(c->devinfo, inst)) {
                        for (int i = 0; i < c->num_temps; i++) {
                                if (c->temp_start[i] < ip &&
                                    c->temp_end[i] > ip) {
                                        ra_add_node_interference(g,
                                                                 temp_to_node[i],
                                                                 acc_nodes[4]);
                                }
                        }
                }

                if (inst->qpu.type == V3D_QPU_INSTR_TYPE_ALU) {
                        switch (inst->qpu.alu.add.op) {
                        case V3D_QPU_A_LDVPMV_IN:
                        case V3D_QPU_A_LDVPMV_OUT:
                        case V3D_QPU_A_LDVPMD_IN:
                        case V3D_QPU_A_LDVPMD_OUT:
                        case V3D_QPU_A_LDVPMP:
                        case V3D_QPU_A_LDVPMG_IN:
                        case V3D_QPU_A_LDVPMG_OUT:
                                /* LDVPMs only store to temps (the MA flag
                                 * decides whether the LDVPM is in or out)
                                 */
                                assert(inst->dst.file == QFILE_TEMP);
                                class_bits[inst->dst.index] &= CLASS_BIT_PHYS;
                                break;

                        case V3D_QPU_A_RECIP:
                        case V3D_QPU_A_RSQRT:
                        case V3D_QPU_A_EXP:
                        case V3D_QPU_A_LOG:
                        case V3D_QPU_A_SIN:
                        case V3D_QPU_A_RSQRT2:
                                /* The SFU instructions write directly to the
                                 * phys regfile.
                                 */
                                assert(inst->dst.file == QFILE_TEMP);
                                class_bits[inst->dst.index] &= CLASS_BIT_PHYS;
                                break;

                        default:
                                break;
                        }
                }

                if (inst->src[0].file == QFILE_REG) {
                        switch (inst->src[0].index) {
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                                /* Payload setup instructions: Force allocate
                                 * the dst to the given register (so the MOV
                                 * will disappear).
                                 */
                                assert(inst->qpu.alu.mul.op == V3D_QPU_M_MOV);
                                assert(inst->dst.file == QFILE_TEMP);
                                ra_set_node_reg(g,
                                                temp_to_node[inst->dst.index],
                                                PHYS_INDEX +
                                                inst->src[0].index);
                                break;
                        }
                }

                if (inst->dst.file == QFILE_TEMP) {
                        /* Only a ldunif gets to write to R5, which only has a
                         * single 32-bit channel of storage.
                         */
                        if (!inst->qpu.sig.ldunif) {
                                class_bits[inst->dst.index] &= ~CLASS_BIT_R5;
                        } else {
                                /* Until V3D 4.x, we could only load a uniform
                                 * to r5, so we'll need to spill if uniform
                                 * loads interfere with each other.
                                 */
                                if (c->devinfo->ver < 40) {
                                        class_bits[inst->dst.index] &=
                                                CLASS_BIT_R5;
                                }
                        }
                }

                if (inst->qpu.sig.thrsw) {
                        /* All accumulators are invalidated across a thread
                         * switch.
                         */
                        for (int i = 0; i < c->num_temps; i++) {
                                if (c->temp_start[i] < ip && c->temp_end[i] > ip)
                                        class_bits[i] &= CLASS_BIT_PHYS;
                        }
                }

                ip++;
        }

        for (uint32_t i = 0; i < c->num_temps; i++) {
                if (class_bits[i] == CLASS_BIT_PHYS) {
                        ra_set_node_class(g, temp_to_node[i],
                                          c->compiler->reg_class_phys[thread_index]);
                } else if (class_bits[i] == (CLASS_BIT_R5)) {
                        ra_set_node_class(g, temp_to_node[i],
                                          c->compiler->reg_class_r5[thread_index]);
                } else if (class_bits[i] == (CLASS_BIT_PHYS | CLASS_BIT_ACC)) {
                        ra_set_node_class(g, temp_to_node[i],
                                          c->compiler->reg_class_phys_or_acc[thread_index]);
                } else {
                        assert(class_bits[i] == CLASS_BITS_ANY);
                        ra_set_node_class(g, temp_to_node[i],
                                          c->compiler->reg_class_any[thread_index]);
                }
        }

        for (uint32_t i = 0; i < c->num_temps; i++) {
                for (uint32_t j = i + 1; j < c->num_temps; j++) {
                        if (!(c->temp_start[i] >= c->temp_end[j] ||
                              c->temp_start[j] >= c->temp_end[i])) {
                                ra_add_node_interference(g,
                                                         temp_to_node[i],
                                                         temp_to_node[j]);
                        }
                }
        }

        /* Debug code to force a bit of register spilling, for running across
         * conformance tests to make sure that spilling works.
         */
        int force_register_spills = 0;
        if (c->spill_size <
            V3D_CHANNELS * sizeof(uint32_t) * force_register_spills) {
                int node = v3d_choose_spill_node(c, g, temp_to_node);
                if (node != -1) {
                        v3d_spill_reg(c, map[node].temp);
                        ralloc_free(g);
                        *spilled = true;
                        return NULL;
                }
        }

        bool ok = ra_allocate(g);
        if (!ok) {
                const uint32_t spill_batch_size = get_spill_batch_size(c);

                for (uint32_t i = 0; i < spill_batch_size; i++) {
                        int node = v3d_choose_spill_node(c, g, temp_to_node);
                        if (node == -1)
                           break;

                        /* TMU spills inject thrsw signals that invalidate
                         * accumulators, so we can't batch them.
                         */
                        bool is_uniform = vir_is_mov_uniform(c, map[node].temp);
                        if (i > 0 && !is_uniform)
                                break;

                        /* Don't emit spills using the TMU until we've dropped
                         * thread count first.
                         */
                        if (is_uniform || thread_index == 0) {
                                v3d_spill_reg(c, map[node].temp);

                                /* Ask the outer loop to call back in. */
                                *spilled = true;

                                /* See comment above about batching TMU spills.
                                 */
                                if (!is_uniform) {
                                        assert(i == 0);
                                        break;
                                }
                        } else {
                                break;
                        }
                }

                ralloc_free(g);
                return NULL;
        }

        struct qpu_reg *temp_registers = calloc(c->num_temps,
                                                sizeof(*temp_registers));

        for (uint32_t i = 0; i < c->num_temps; i++) {
                int ra_reg = ra_get_node_reg(g, temp_to_node[i]);
                if (ra_reg < PHYS_INDEX) {
                        temp_registers[i].magic = true;
                        temp_registers[i].index = (V3D_QPU_WADDR_R0 +
                                                   ra_reg - ACC_INDEX);
                } else {
                        temp_registers[i].magic = false;
                        temp_registers[i].index = ra_reg - PHYS_INDEX;
                }
        }

        ralloc_free(g);

        return temp_registers;
}
