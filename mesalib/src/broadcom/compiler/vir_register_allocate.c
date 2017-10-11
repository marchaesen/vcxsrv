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
#include "v3d_compiler.h"

#define QPU_R(i) { .magic = false, .index = i }

#define ACC_INDEX     0
#define ACC_COUNT     5
#define PHYS_INDEX    (ACC_INDEX + ACC_COUNT)
#define PHYS_COUNT    64

bool
vir_init_reg_sets(struct v3d_compiler *compiler)
{
        compiler->regs = ra_alloc_reg_set(compiler, PHYS_INDEX + PHYS_COUNT,
                                          true);
        if (!compiler->regs)
                return false;

        /* Allocate 3 regfile classes, for the ways the physical register file
         * can be divided up for fragment shader threading.
         */
        for (int threads = 0; threads < 3; threads++) {
                compiler->reg_class[threads] =
                        ra_alloc_reg_class(compiler->regs);

                for (int i = PHYS_INDEX;
                     i < PHYS_INDEX + (PHYS_COUNT >> threads); i++) {
                        ra_class_add_reg(compiler->regs,
                                         compiler->reg_class[threads], i);
                }

                for (int i = ACC_INDEX + 0; i < ACC_INDEX + ACC_COUNT; i++) {
                        ra_class_add_reg(compiler->regs,
                                         compiler->reg_class[threads], i);
                }
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

#define CLASS_BIT_PHYS			(1 << 0)
#define CLASS_BIT_R0_R2			(1 << 1)
#define CLASS_BIT_R3			(1 << 2)
#define CLASS_BIT_R4			(1 << 3)

/**
 * Returns a mapping from QFILE_TEMP indices to struct qpu_regs.
 *
 * The return value should be freed by the caller.
 */
struct qpu_reg *
v3d_register_allocate(struct v3d_compile *c)
{
        struct node_to_temp_map map[c->num_temps];
        uint32_t temp_to_node[c->num_temps];
        uint8_t class_bits[c->num_temps];
        struct qpu_reg *temp_registers = calloc(c->num_temps,
                                                sizeof(*temp_registers));
        int acc_nodes[ACC_COUNT];

        struct ra_graph *g = ra_alloc_interference_graph(c->compiler->regs,
                                                         c->num_temps +
                                                         ARRAY_SIZE(acc_nodes));

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

        /* Compute the live ranges so we can figure out interference. */
        vir_calculate_live_intervals(c);

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
        memset(class_bits,
               CLASS_BIT_PHYS | CLASS_BIT_R0_R2 | CLASS_BIT_R3 | CLASS_BIT_R4,
               sizeof(class_bits));

        int ip = 0;
        vir_for_each_inst_inorder(inst, c) {
                /* If the instruction writes r3/r4 (and optionally moves its
                 * result to a temp), nothing else can be stored in r3/r4 across
                 * it.
                 */
                if (vir_writes_r3(inst)) {
                        for (int i = 0; i < c->num_temps; i++) {
                                if (c->temp_start[i] < ip &&
                                    c->temp_end[i] > ip) {
                                        ra_add_node_interference(g,
                                                                 temp_to_node[i],
                                                                 acc_nodes[3]);
                                }
                        }
                }
                if (vir_writes_r4(inst)) {
                        for (int i = 0; i < c->num_temps; i++) {
                                if (c->temp_start[i] < ip &&
                                    c->temp_end[i] > ip) {
                                        ra_add_node_interference(g,
                                                                 temp_to_node[i],
                                                                 acc_nodes[4]);
                                }
                        }
                }

                if (inst->src[0].file == QFILE_REG) {
                        switch (inst->src[0].index) {
                        case 0:
                        case 1:
                        case 2:
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

#if 0
                switch (inst->op) {
                case QOP_THRSW:
                        /* All accumulators are invalidated across a thread
                         * switch.
                         */
                        for (int i = 0; i < c->num_temps; i++) {
                                if (c->temp_start[i] < ip && c->temp_end[i] > ip)
                                        class_bits[i] &= ~(CLASS_BIT_R0_R3 |
                                                           CLASS_BIT_R4);
                        }
                        break;

                default:
                        break;
                }
#endif

                ip++;
        }

        for (uint32_t i = 0; i < c->num_temps; i++) {
                ra_set_node_class(g, temp_to_node[i],
                                  c->compiler->reg_class[c->fs_threaded]);
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

        bool ok = ra_allocate(g);
        if (!ok) {
                if (!c->fs_threaded) {
                        fprintf(stderr, "Failed to register allocate:\n");
                        vir_dump(c);
                }

                c->failed = true;
                free(temp_registers);
                return NULL;
        }

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

                /* If the value's never used, just write to the NOP register
                 * for clarity in debug output.
                 */
                if (c->temp_start[i] == c->temp_end[i]) {
                        temp_registers[i].magic = true;
                        temp_registers[i].index = V3D_QPU_WADDR_NOP;
                }
        }

        ralloc_free(g);

        return temp_registers;
}
