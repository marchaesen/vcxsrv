/*
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
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

#include "util/register_allocate.h"
#include "compiler_defines.h"
#include "bifrost_sched.h"
#include "bifrost_compile.h"
#include "bifrost_print.h"

#define BI_DEBUG
const unsigned max_primary_reg = 64; /* Overestimate because of special regs */
const unsigned max_vec2_reg = 32;
const unsigned max_vec3_reg = 16; // XXX: Do we need to align vec3 to vec4 boundary?
const unsigned max_vec4_reg = 16;
const unsigned max_registers = 128; /* Sum of classes */
const unsigned primary_base = 0;
const unsigned vec2_base = 64;
const unsigned vec3_base = 96; /* above base + max_class_reg */
const unsigned vec4_base = 112;
const unsigned vec4_end = 128;

static unsigned
find_or_allocate_temp(compiler_context *ctx, unsigned hash)
{
        if (hash >= SSA_FIXED_MINIMUM)
                return hash;

        unsigned temp = (uintptr_t) _mesa_hash_table_u64_search(ctx->hash_to_temp, hash + 1);

        if (temp)
                return temp - 1;

        /* If no temp is find, allocate one */
        temp = ctx->num_temps++;
        ctx->max_hash = MAX2(ctx->max_hash, hash);

        _mesa_hash_table_u64_insert(ctx->hash_to_temp, hash + 1, (void *) ((uintptr_t) temp + 1));

        return temp;
}

static bool
is_live_in_instr(bifrost_instruction *instr, unsigned temp)
{
        if (instr->ssa_args.src0 == temp) return true;
        if (instr->ssa_args.src1 == temp) return true;
        if (instr->ssa_args.src2 == temp) return true;
        if (instr->ssa_args.src3 == temp) return true;

        return false;
}

static bool
is_live_after_instr(compiler_context *ctx, bifrost_block *blk, bifrost_instruction *instr, unsigned temp)
{
        // Scan forward in the block from this location to see if we are still live.

        mir_foreach_instr_in_block_from(blk, ins, mir_next_instr(instr)) {
                if (is_live_in_instr(ins, temp))
                        return true;
        }

        // XXX: Walk all successor blocks and ensure the value isn't used there

        return false;
}

static uint32_t
ra_select_callback(struct ra_graph *g, BITSET_WORD *regs, void *data)
{
        for (int i = primary_base; i < vec4_end; ++i) {
                if (BITSET_TEST(regs, i)) {
                        return i;
                }
        }

        assert(0);
        return 0;
}

static uint32_t
ra_get_phys_reg(compiler_context *ctx, struct ra_graph *g, unsigned temp, unsigned max_reg)
{
        if (temp == SSA_INVALID_VALUE ||
            temp >= SSA_FIXED_UREG_MINIMUM ||
            temp == SSA_FIXED_CONST_0)
                return temp;

        if (temp >= SSA_FIXED_MINIMUM)
                return SSA_REG_FROM_FIXED(temp);

        assert(temp < max_reg);
        uint32_t r = ra_get_node_reg(g, temp);
        if (r >= vec4_base)
                return (r - vec4_base) * 4;
        else if (r >= vec3_base)
                return (r - vec3_base) * 4;
        else if (r >= vec2_base)
                return (r - vec2_base) * 2;

        return r;
}

static void
allocate_registers(compiler_context *ctx)
{
        struct ra_regs *regs = ra_alloc_reg_set(NULL, max_registers, true);

        int primary_class = ra_alloc_reg_class(regs);
        int vec2_class = ra_alloc_reg_class(regs);
        int vec3_class = ra_alloc_reg_class(regs);
        int vec4_class = ra_alloc_reg_class(regs);

        // Allocate our register classes and conflicts
        {
                unsigned reg = 0;
                unsigned primary_base = 0;

                // Add all of our primary scalar registers
                for (unsigned i = 0; i < max_primary_reg; ++i) {
                        ra_class_add_reg(regs, primary_class, reg);
                        reg++;
                }

                // Add all of our vec2 class registers
                // These alias with the scalar registers
                for (unsigned i = 0; i < max_vec2_reg; ++i) {
                        ra_class_add_reg(regs, vec2_class, reg);

                        // Tell RA that this conflicts with primary class registers
                        // Make sure to tell the RA utility all conflict slots
                        ra_add_reg_conflict(regs, reg, primary_base + i*2 + 0);
                        ra_add_reg_conflict(regs, reg, primary_base + i*2 + 1);

                        reg++;
                }

                // Add all of our vec3 class registers
                // These alias with the scalar registers
                for (unsigned i = 0; i < max_vec3_reg; ++i) {
                        ra_class_add_reg(regs, vec3_class, reg);

                        // Tell RA that this conflicts with primary class registers
                        // Make sure to tell the RA utility all conflict slots
                        // These are aligned to vec4 even though they only conflict with a vec3 wide slot
                        ra_add_reg_conflict(regs, reg, primary_base + i*4 + 0);
                        ra_add_reg_conflict(regs, reg, primary_base + i*4 + 1);
                        ra_add_reg_conflict(regs, reg, primary_base + i*4 + 2);

                        // State that this class conflicts with the vec2 class
                        ra_add_reg_conflict(regs, reg, vec2_base + i*2 + 0);
                        ra_add_reg_conflict(regs, reg, vec2_base + i*2 + 1);

                        reg++;
                }

                // Add all of our vec4 class registers
                // These alias with the scalar registers
                for (unsigned i = 0; i < max_vec4_reg; ++i) {
                        ra_class_add_reg(regs, vec4_class, reg);

                        // Tell RA that this conflicts with primary class registers
                        // Make sure to tell the RA utility all conflict slots
                        // These are aligned to vec4 even though they only conflict with a vec3 wide slot
                        ra_add_reg_conflict(regs, reg, primary_base + i*4 + 0);
                        ra_add_reg_conflict(regs, reg, primary_base + i*4 + 1);
                        ra_add_reg_conflict(regs, reg, primary_base + i*4 + 2);
                        ra_add_reg_conflict(regs, reg, primary_base + i*4 + 3);

                        // State that this class conflicts with the vec2 class
                        ra_add_reg_conflict(regs, reg, vec2_base + i*2 + 0);
                        ra_add_reg_conflict(regs, reg, vec2_base + i*2 + 1);

                        // State that this class conflicts with the vec3 class
                        // They conflict on the exact same location due to alignments
                        ra_add_reg_conflict(regs, reg, vec3_base + i);

                        reg++;
                }
        }

        ra_set_finalize(regs, NULL);
        mir_foreach_block(ctx, block) {
                mir_foreach_instr_in_block(block, instr) {
                        instr->ssa_args.src0 = find_or_allocate_temp(ctx, instr->ssa_args.src0);
                        instr->ssa_args.src1 = find_or_allocate_temp(ctx, instr->ssa_args.src1);
                        instr->ssa_args.src2 = find_or_allocate_temp(ctx, instr->ssa_args.src2);
                        instr->ssa_args.src3 = find_or_allocate_temp(ctx, instr->ssa_args.src3);
                        instr->ssa_args.dest = find_or_allocate_temp(ctx, instr->ssa_args.dest);
                }
        }

        uint32_t nodes = ctx->num_temps;
        struct ra_graph *g = ra_alloc_interference_graph(regs, nodes);

        mir_foreach_block(ctx, block) {
                mir_foreach_instr_in_block(block, instr) {
                        if (instr->ssa_args.dest >= SSA_FIXED_MINIMUM) continue;
                        if (instr->dest_components == 4)
                                ra_set_node_class(g, instr->ssa_args.dest, vec4_class);
                        else if (instr->dest_components == 3)
                                ra_set_node_class(g, instr->ssa_args.dest, vec3_class);
                        else if (instr->dest_components == 2)
                                ra_set_node_class(g, instr->ssa_args.dest, vec2_class);
                        else
                                ra_set_node_class(g, instr->ssa_args.dest, primary_class);
                }
        }

        uint32_t *live_start = malloc(nodes * sizeof(uint32_t));
        uint32_t *live_end = malloc(nodes * sizeof(uint32_t));

        memset(live_start, 0xFF, nodes * sizeof(uint32_t));
        memset(live_end, 0xFF, nodes * sizeof(uint32_t));

        uint32_t location = 0;
        mir_foreach_block(ctx, block) {
                mir_foreach_instr_in_block(block, instr) {
                        if (instr->ssa_args.dest < SSA_FIXED_MINIMUM) {
                                // If the destination isn't yet live before this point
                                // then this is the point it becomes live since we wrote to it
                                if (live_start[instr->ssa_args.dest] == ~0U) {
                                        live_start[instr->ssa_args.dest] = location;
                                }
                        }

                        uint32_t sources[4] = {
                                instr->ssa_args.src0,
                                instr->ssa_args.src1,
                                instr->ssa_args.src2,
                                instr->ssa_args.src3,
                        };

                        for (unsigned i = 0; i < 4; ++i) {
                                if (sources[i] >= SSA_FIXED_MINIMUM)
                                        continue;

                                // If the source is no longer live after this instruction then we can end its liveness
                                if (!is_live_after_instr(ctx, block, instr, sources[i])) {
                                        live_end[sources[i]] = location;
                                }
                        }
                        ++location;
                }
        }

        // Spin through the nodes quick and ensure they are all killed by the end of the program
        for (unsigned i = 0; i < nodes; ++i) {
                if (live_end[i] == ~0U)
                        live_end[i] = location;
        }

        for (int i = 0; i < nodes; ++i) {
                for (int j = i + 1; j < nodes; ++j) {
                        if (!(live_start[i] >= live_end[j] || live_start[j] >= live_end[i])) {
                                ra_add_node_interference(g, i, j);
                        }
                }
        }

        ra_set_select_reg_callback(g, ra_select_callback, NULL);

        if (!ra_allocate(g)) {
                assert(0);
        }

        free(live_start);
        free(live_end);

        mir_foreach_block(ctx, block) {
                mir_foreach_instr_in_block(block, instr) {
                        instr->args.src0 = ra_get_phys_reg(ctx, g, instr->ssa_args.src0, nodes);
                        instr->args.src1 = ra_get_phys_reg(ctx, g, instr->ssa_args.src1, nodes);
                        instr->args.src2 = ra_get_phys_reg(ctx, g, instr->ssa_args.src2, nodes);
                        instr->args.src3 = ra_get_phys_reg(ctx, g, instr->ssa_args.src3, nodes);
                        instr->args.dest = ra_get_phys_reg(ctx, g, instr->ssa_args.dest, nodes);
                }
        }
}

static void
bundle_block(compiler_context *ctx, bifrost_block *block)
{
}

static void
remove_create_vectors(compiler_context *ctx, bifrost_block *block)
{
        mir_foreach_instr_in_block_safe(block, instr) {
                if (instr->op != op_create_vector) continue;

                uint32_t vector_ssa_sources[4] = {
                        instr->ssa_args.src0,
                        instr->ssa_args.src1,
                        instr->ssa_args.src2,
                        instr->ssa_args.src3,
                };

                mir_foreach_instr_in_block_from_rev(block, next_instr, instr) {
                        // Walk our block backwards and find the creators of this vector creation instruction
                        for (unsigned i = 0; i < instr->dest_components; ++i) {
                                // If this instruction is ther one that writes this register then forward it to the real register
                                if (vector_ssa_sources[i] == next_instr->ssa_args.dest) {
                                        next_instr->ssa_args.dest = vector_ssa_sources[i];
                                        // Source instruction destination is a vector register of size dest_components
                                        // So dest + i gets the components of it
                                        next_instr->args.dest = instr->args.dest + i;
                                }
                        }
                }

                // Remove the instruction now that we have copied over all the sources
                mir_remove_instr(instr);
        }
}

static void
remove_extract_elements(compiler_context *ctx, bifrost_block *block)
{
        mir_foreach_instr_in_block_safe(block, instr) {
                if (instr->op != op_extract_element) continue;

                mir_foreach_instr_in_block_from(block, next_instr, instr) {
                        // Walk our block forward to replace uses of this register with a real register
                        // src0 = vector
                        // src1 = index in to vector
                        uint32_t vector_ssa_sources[4] = {
                                next_instr->ssa_args.src0,
                                next_instr->ssa_args.src1,
                                next_instr->ssa_args.src2,
                                next_instr->ssa_args.src3,
                        };
                        uint32_t *vector_sources[4] = {
                                &next_instr->args.src0,
                                &next_instr->args.src1,
                                &next_instr->args.src2,
                                &next_instr->args.src3,
                        };

                        for (unsigned i = 0; i < 4; ++i) {
                                if (vector_ssa_sources[i] == instr->ssa_args.dest) {
                                        // This source uses this vector extraction
                                        // Replace its usage with the real register
                                        // src0 is a vector register and src1 is the constant element of the vector
                                        *vector_sources[i] = instr->args.src0 + instr->literal_args[0];
                                }
                        }

                }

                // Remove the instruction now that we have copied over all the sources
                mir_remove_instr(instr);
        }
}


void schedule_program(compiler_context *ctx)
{
        // XXX: we should move instructions together before RA that can feed in to each other and be scheduled in the same clause
        allocate_registers(ctx);

        mir_foreach_block(ctx, block) {
                remove_create_vectors(ctx, block);
                remove_extract_elements(ctx, block);
        }

        mir_foreach_block(ctx, block) {
#ifdef BI_DEBUG
                print_mir_block(block, true);
#endif

                bundle_block(ctx, block);
        }
}

