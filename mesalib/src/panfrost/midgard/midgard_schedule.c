/*
 * Copyright (C) 2018-2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
#include "midgard_ops.h"
#include "util/u_memory.h"
#include "util/register_allocate.h"

/* Scheduling for Midgard is complicated, to say the least. ALU instructions
 * must be grouped into VLIW bundles according to following model:
 *
 * [VMUL] [SADD]
 * [VADD] [SMUL] [VLUT]
 *
 * A given instruction can execute on some subset of the units (or a few can
 * execute on all). Instructions can be either vector or scalar; only scalar
 * instructions can execute on SADD/SMUL units. Units on a given line execute
 * in parallel. Subsequent lines execute separately and can pass results
 * directly via pipeline registers r24/r25, bypassing the register file.
 *
 * A bundle can optionally have 128-bits of embedded constants, shared across
 * all of the instructions within a bundle.
 *
 * Instructions consuming conditionals (branches and conditional selects)
 * require their condition to be written into the conditional register (r31)
 * within the same bundle they are consumed.
 *
 * Fragment writeout requires its argument to be written in full within the
 * same bundle as the branch, with no hanging dependencies.
 *
 * Load/store instructions are also in bundles of simply two instructions, and
 * texture instructions have no bundling.
 *
 * -------------------------------------------------------------------------
 *
 */

/* Create a mask of accessed components from a swizzle to figure out vector
 * dependencies */

static unsigned
swizzle_to_access_mask(unsigned swizzle)
{
        unsigned component_mask = 0;

        for (int i = 0; i < 4; ++i) {
                unsigned c = (swizzle >> (2 * i)) & 3;
                component_mask |= (1 << c);
        }

        return component_mask;
}

/* Does the mask cover more than a scalar? */

static bool
is_single_component_mask(unsigned mask)
{
        int components = 0;

        for (int c = 0; c < 8; ++c) {
                if (mask & (1 << c))
                        components++;
        }

        return components == 1;
}

/* Checks for an SSA data hazard between two adjacent instructions, keeping in
 * mind that we are a vector architecture and we can write to different
 * components simultaneously */

static bool
can_run_concurrent_ssa(midgard_instruction *first, midgard_instruction *second)
{
        /* Writeout has its own rules anyway */
        if (first->compact_branch || second->compact_branch)
                return true;

        /* Each instruction reads some registers and writes to a register. See
         * where the first writes */

        int source = first->dest;
        int source_mask = first->mask;

        /* As long as the second doesn't read from the first, we're okay */
        for (unsigned i = 0; i < ARRAY_SIZE(second->src); ++i) {
                if (second->src[i] != source)
                        continue;

                if (first->type != TAG_ALU_4)
                        return false;

                /* Figure out which components we just read from */

                int q = (i == 0) ? second->alu.src1 : second->alu.src2;
                midgard_vector_alu_src *m = (midgard_vector_alu_src *) &q;

                /* Check if there are components in common, and fail if so */
                if (swizzle_to_access_mask(m->swizzle) & source_mask)
                        return false;
        }

        /* Otherwise, it's safe in that regard. Another data hazard is both
         * writing to the same place, of course */

        if (second->dest == source) {
                /* ...but only if the components overlap */

                if (second->mask & source_mask)
                        return false;
        }

        /* ...That's it */
        return true;
}

static bool
midgard_has_hazard(
        midgard_instruction **segment, unsigned segment_size,
        midgard_instruction *ains)
{
        for (int s = 0; s < segment_size; ++s)
                if (!can_run_concurrent_ssa(segment[s], ains))
                        return true;

        return false;


}

/* Fragment writeout (of r0) is allowed when:
 *
 *  - All components of r0 are written in the bundle
 *  - No components of r0 are written in VLUT
 *  - Non-pipelined dependencies of r0 are not written in the bundle
 *
 * This function checks if these requirements are satisfied given the content
 * of a scheduled bundle.
 */

static bool
can_writeout_fragment(compiler_context *ctx, midgard_instruction **bundle, unsigned count, unsigned node_count, unsigned r0)
{
        /* First scan for which components of r0 are written out. Initially
         * none are written */

        uint8_t r0_written_mask = 0x0;

        /* Simultaneously we scan for the set of dependencies */

        size_t sz = sizeof(BITSET_WORD) * BITSET_WORDS(node_count);
        BITSET_WORD *dependencies = calloc(1, sz);
        memset(dependencies, 0, sz);

        bool success = false;

        for (unsigned i = 0; i < count; ++i) {
                midgard_instruction *ins = bundle[i];

                if (ins->dest != r0)
                        continue;

                /* Record written out mask */
                r0_written_mask |= ins->mask;

                /* Record dependencies, but only if they won't become pipeline
                 * registers. We know we can't be live after this, because
                 * we're writeout at the very end of the shader. So check if
                 * they were written before us. */

                unsigned src0 = ins->src[0];
                unsigned src1 = ins->src[1];

                if (!mir_is_written_before(ctx, bundle[0], src0))
                        src0 = ~0;

                if (!mir_is_written_before(ctx, bundle[0], src1))
                        src1 = ~0;

                if (src0 < node_count)
                        BITSET_SET(dependencies, src0);

                if (src1 < node_count)
                        BITSET_SET(dependencies, src1);

                /* Requirement 2 */
                if (ins->unit == UNIT_VLUT)
                        goto done;
        }

        /* Requirement 1 */
        if ((r0_written_mask & 0xF) != 0xF)
                goto done;

        /* Requirement 3 */

        for (unsigned i = 0; i < count; ++i) {
                unsigned dest = bundle[i]->dest;

                if (dest < node_count && BITSET_TEST(dependencies, dest))
                        goto done;
        }

        /* Otherwise, we're good to go */
        success = true;

done:
        free(dependencies);
        return success;
}

/* Helpers for scheudling */

static bool
mir_is_scalar(midgard_instruction *ains)
{
        /* Does the op support scalar units? */
        if (!(alu_opcode_props[ains->alu.op].props & UNITS_SCALAR))
                return false;

        /* Do we try to use it as a vector op? */
        if (!is_single_component_mask(ains->mask))
                return false;

        /* Otherwise, check mode hazards */
        bool could_scalar = true;

        /* Only 16/32-bit can run on a scalar unit */
        could_scalar &= ains->alu.reg_mode != midgard_reg_mode_8;
        could_scalar &= ains->alu.reg_mode != midgard_reg_mode_64;
        could_scalar &= ains->alu.dest_override == midgard_dest_override_none;

        if (ains->alu.reg_mode == midgard_reg_mode_16) {
                /* If we're running in 16-bit mode, we
                 * can't have any 8-bit sources on the
                 * scalar unit (since the scalar unit
                 * doesn't understand 8-bit) */

                midgard_vector_alu_src s1 =
                        vector_alu_from_unsigned(ains->alu.src1);

                could_scalar &= !s1.half;

                midgard_vector_alu_src s2 =
                        vector_alu_from_unsigned(ains->alu.src2);

                could_scalar &= !s2.half;
        }

        return could_scalar;
}

/* How many bytes does this ALU instruction add to the bundle? */

static unsigned
bytes_for_instruction(midgard_instruction *ains)
{
        if (ains->unit & UNITS_ANY_VECTOR)
                return sizeof(midgard_reg_info) + sizeof(midgard_vector_alu);
        else if (ains->unit == ALU_ENAB_BRANCH)
                return sizeof(midgard_branch_extended);
        else if (ains->compact_branch)
                return sizeof(ains->br_compact);
        else
                return sizeof(midgard_reg_info) + sizeof(midgard_scalar_alu);
}

/* Schedules, but does not emit, a single basic block. After scheduling, the
 * final tag and size of the block are known, which are necessary for branching
 * */

static midgard_bundle
schedule_bundle(compiler_context *ctx, midgard_block *block, midgard_instruction *ins, int *skip)
{
        int instructions_emitted = 0, packed_idx = 0;
        midgard_bundle bundle = { 0 };

        midgard_instruction *scheduled[5] = { NULL };

        uint8_t tag = ins->type;

        /* Default to the instruction's tag */
        bundle.tag = tag;

        switch (ins->type) {
        case TAG_ALU_4: {
                uint32_t control = 0;
                size_t bytes_emitted = sizeof(control);

                /* TODO: Constant combining */
                int index = 0, last_unit = 0;

                /* Previous instructions, for the purpose of parallelism */
                midgard_instruction *segment[4] = {0};
                int segment_size = 0;

                instructions_emitted = -1;
                midgard_instruction *pins = ins;

                unsigned constant_count = 0;

                for (;;) {
                        midgard_instruction *ains = pins;

                        /* Advance instruction pointer */
                        if (index) {
                                ains = mir_next_op(pins);
                                pins = ains;
                        }

                        /* Out-of-work condition */
                        if ((struct list_head *) ains == &block->instructions)
                                break;

                        /* Ensure that the chain can continue */
                        if (ains->type != TAG_ALU_4) break;

                        /* If there's already something in the bundle and we
                         * have weird scheduler constraints, break now */
                        if (ains->precede_break && index) break;

                        /* According to the presentation "The ARM
                         * Mali-T880 Mobile GPU" from HotChips 27,
                         * there are two pipeline stages. Branching
                         * position determined experimentally. Lines
                         * are executed in parallel:
                         *
                         * [ VMUL ] [ SADD ]
                         * [ VADD ] [ SMUL ] [ LUT ] [ BRANCH ]
                         *
                         * Verify that there are no ordering dependencies here.
                         *
                         * TODO: Allow for parallelism!!!
                         */

                        /* Pick a unit for it if it doesn't force a particular unit */

                        int unit = ains->unit;

                        if (!unit) {
                                int op = ains->alu.op;
                                int units = alu_opcode_props[op].props;
                                bool scalar = mir_is_scalar(ains);

                                if (!scalar) {
                                        if (last_unit >= UNIT_VADD) {
                                                if (units & UNIT_VLUT)
                                                        unit = UNIT_VLUT;
                                                else
                                                        break;
                                        } else {
                                                if ((units & UNIT_VMUL) && last_unit < UNIT_VMUL)
                                                        unit = UNIT_VMUL;
                                                else if ((units & UNIT_VADD) && !(control & UNIT_VADD))
                                                        unit = UNIT_VADD;
                                                else if (units & UNIT_VLUT)
                                                        unit = UNIT_VLUT;
                                                else
                                                        break;
                                        }
                                } else {
                                        if (last_unit >= UNIT_VADD) {
                                                if ((units & UNIT_SMUL) && !(control & UNIT_SMUL))
                                                        unit = UNIT_SMUL;
                                                else if (units & UNIT_VLUT)
                                                        unit = UNIT_VLUT;
                                                else
                                                        break;
                                        } else {
                                                if ((units & UNIT_VMUL) && (last_unit < UNIT_VMUL))
                                                        unit = UNIT_VMUL;
                                                else if ((units & UNIT_SADD) && !(control & UNIT_SADD) && !midgard_has_hazard(segment, segment_size, ains))
                                                        unit = UNIT_SADD;
                                                else if (units & UNIT_VADD)
                                                        unit = UNIT_VADD;
                                                else if (units & UNIT_SMUL)
                                                        unit = UNIT_SMUL;
                                                else if (units & UNIT_VLUT)
                                                        unit = UNIT_VLUT;
                                                else
                                                        break;
                                        }
                                }

                                assert(unit & units);
                        }

                        /* Late unit check, this time for encoding (not parallelism) */
                        if (unit <= last_unit) break;

                        /* Clear the segment */
                        if (last_unit < UNIT_VADD && unit >= UNIT_VADD)
                                segment_size = 0;

                        if (midgard_has_hazard(segment, segment_size, ains))
                                break;

                        /* We're good to go -- emit the instruction */
                        ains->unit = unit;

                        segment[segment_size++] = ains;

                        /* We try to reuse constants if possible, by adjusting
                         * the swizzle */

                        if (ains->has_blend_constant) {
                                /* Everything conflicts with the blend constant */
                                if (bundle.has_embedded_constants)
                                        break;

                                bundle.has_blend_constant = 1;
                                bundle.has_embedded_constants = 1;
                        } else if (ains->has_constants && ains->alu.reg_mode == midgard_reg_mode_16) {
                                /* TODO: DRY with the analysis pass */

                                if (bundle.has_blend_constant)
                                        break;

                                if (constant_count)
                                        break;

                                /* TODO: Fix packing XXX */
                                uint16_t *bundles = (uint16_t *) bundle.constants;
                                uint32_t *constants = (uint32_t *) ains->constants;

                                /* Copy them wholesale */
                                for (unsigned i = 0; i < 4; ++i)
                                        bundles[i] = constants[i];

                                bundle.has_embedded_constants = true;
                                constant_count = 4;
                        } else if (ains->has_constants) {
                                /* By definition, blend constants conflict with
                                 * everything, so if there are already
                                 * constants we break the bundle *now* */

                                if (bundle.has_blend_constant)
                                        break;

                                /* For anything but blend constants, we can do
                                 * proper analysis, however */

                                /* TODO: Mask by which are used */
                                uint32_t *constants = (uint32_t *) ains->constants;
                                uint32_t *bundles = (uint32_t *) bundle.constants;

                                uint32_t indices[4] = { 0 };
                                bool break_bundle = false;

                                for (unsigned i = 0; i < 4; ++i) {
                                        uint32_t cons = constants[i];
                                        bool constant_found = false;

                                        /* Search for the constant */
                                        for (unsigned j = 0; j < constant_count; ++j) {
                                                if (bundles[j] != cons)
                                                        continue;

                                                /* We found it, reuse */
                                                indices[i] = j;
                                                constant_found = true;
                                                break;
                                        }

                                        if (constant_found)
                                                continue;

                                        /* We didn't find it, so allocate it */
                                        unsigned idx = constant_count++;

                                        if (idx >= 4) {
                                                /* Uh-oh, out of space */
                                                break_bundle = true;
                                                break;
                                        }

                                        /* We have space, copy it in! */
                                        bundles[idx] = cons;
                                        indices[i] = idx;
                                }

                                if (break_bundle)
                                        break;

                                /* Cool, we have it in. So use indices as a
                                 * swizzle */

                                unsigned swizzle = SWIZZLE_FROM_ARRAY(indices);
                                unsigned r_constant = SSA_FIXED_REGISTER(REGISTER_CONSTANT);

                                if (ains->src[0] == r_constant)
                                        ains->alu.src1 = vector_alu_apply_swizzle(ains->alu.src1, swizzle);

                                if (ains->src[1] == r_constant)
                                        ains->alu.src2 = vector_alu_apply_swizzle(ains->alu.src2, swizzle);

                                bundle.has_embedded_constants = true;
                        }

                        if (ains->compact_branch) {
                                /* All of r0 has to be written out along with
                                 * the branch writeout */

                                if (ains->writeout && !can_writeout_fragment(ctx, scheduled, index, ctx->temp_count, ains->src[0])) {
                                        /* We only work on full moves
                                         * at the beginning. We could
                                         * probably do better */
                                        if (index != 0)
                                                break;

                                        /* Inject a move */
                                        midgard_instruction ins = v_mov(0, blank_alu_src, SSA_FIXED_REGISTER(0));
                                        ins.unit = UNIT_VMUL;
                                        control |= ins.unit;

                                        /* TODO don't leak */
                                        midgard_instruction *move =
                                                mem_dup(&ins, sizeof(midgard_instruction));
                                        bytes_emitted += bytes_for_instruction(move);
                                        bundle.instructions[packed_idx++] = move;
                                }
                        }

                        bytes_emitted += bytes_for_instruction(ains);

                        /* Defer marking until after writing to allow for break */
                        scheduled[index] = ains;
                        control |= ains->unit;
                        last_unit = ains->unit;
                        ++instructions_emitted;
                        ++index;
                }

                int padding = 0;

                /* Pad ALU op to nearest word */

                if (bytes_emitted & 15) {
                        padding = 16 - (bytes_emitted & 15);
                        bytes_emitted += padding;
                }

                /* Constants must always be quadwords */
                if (bundle.has_embedded_constants)
                        bytes_emitted += 16;

                /* Size ALU instruction for tag */
                bundle.tag = (TAG_ALU_4) + (bytes_emitted / 16) - 1;
                bundle.padding = padding;
                bundle.control = bundle.tag | control;

                break;
        }

        case TAG_LOAD_STORE_4: {
                /* Load store instructions have two words at once. If
                 * we only have one queued up, we need to NOP pad.
                 * Otherwise, we store both in succession to save space
                 * and cycles -- letting them go in parallel -- skip
                 * the next. The usefulness of this optimisation is
                 * greatly dependent on the quality of the instruction
                 * scheduler.
                 */

                midgard_instruction *next_op = mir_next_op(ins);

                if ((struct list_head *) next_op != &block->instructions && next_op->type == TAG_LOAD_STORE_4) {
                        /* TODO: Concurrency check */
                        instructions_emitted++;
                }

                break;
        }

        case TAG_TEXTURE_4: {
                /* Which tag we use depends on the shader stage */
                bool in_frag = ctx->stage == MESA_SHADER_FRAGMENT;
                bundle.tag = in_frag ? TAG_TEXTURE_4 : TAG_TEXTURE_4_VTX;
                break;
        }

        default:
                unreachable("Unknown tag");
                break;
        }

        /* Copy the instructions into the bundle */
        bundle.instruction_count = instructions_emitted + 1 + packed_idx;

        midgard_instruction *uins = ins;
        for (; packed_idx < bundle.instruction_count; ++packed_idx) {
                assert(&uins->link != &block->instructions);
                bundle.instructions[packed_idx] = uins;
                uins = mir_next_op(uins);
        }

        *skip = instructions_emitted;

        return bundle;
}

/* Schedule a single block by iterating its instruction to create bundles.
 * While we go, tally about the bundle sizes to compute the block size. */

static void
schedule_block(compiler_context *ctx, midgard_block *block)
{
        util_dynarray_init(&block->bundles, NULL);

        block->quadword_count = 0;

        int skip = 0;
        mir_foreach_instr_in_block(block, ins) {
                if (skip) {
                        skip--;
                        continue;
                }

                midgard_bundle bundle = schedule_bundle(ctx, block, ins, &skip);
                util_dynarray_append(&block->bundles, midgard_bundle, bundle);

                if (bundle.has_blend_constant) {
                        unsigned offset = ctx->quadword_count + block->quadword_count + quadword_size(bundle.tag) - 1;
                        ctx->blend_constant_offset = offset * 0x10;
                }

                block->quadword_count += quadword_size(bundle.tag);
        }

        block->is_scheduled = true;
        ctx->quadword_count += block->quadword_count;
}

/* The following passes reorder MIR instructions to enable better scheduling */

static void
midgard_pair_load_store(compiler_context *ctx, midgard_block *block)
{
        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_LOAD_STORE_4) continue;

                /* We've found a load/store op. Check if next is also load/store. */
                midgard_instruction *next_op = mir_next_op(ins);
                if (&next_op->link != &block->instructions) {
                        if (next_op->type == TAG_LOAD_STORE_4) {
                                /* If so, we're done since we're a pair */
                                ins = mir_next_op(ins);
                                continue;
                        }

                        /* Maximum search distance to pair, to avoid register pressure disasters */
                        int search_distance = 8;

                        /* Otherwise, we have an orphaned load/store -- search for another load */
                        mir_foreach_instr_in_block_from(block, c, mir_next_op(ins)) {
                                /* Terminate search if necessary */
                                if (!(search_distance--)) break;

                                if (c->type != TAG_LOAD_STORE_4) continue;

                                /* We can only reorder if there are no sources */

                                bool deps = false;

                                for (unsigned s = 0; s < ARRAY_SIZE(ins->src); ++s)
                                        deps |= (c->src[s] != ~0);

                                if (deps)
                                        continue;

                                /* We found one! Move it up to pair and remove it from the old location */

                                mir_insert_instruction_before(ctx, ins, *c);
                                mir_remove_instruction(c);

                                break;
                        }
                }
        }
}

/* When we're 'squeezing down' the values in the IR, we maintain a hash
 * as such */

static unsigned
find_or_allocate_temp(compiler_context *ctx, unsigned hash)
{
        if (hash >= SSA_FIXED_MINIMUM)
                return hash;

        unsigned temp = (uintptr_t) _mesa_hash_table_u64_search(
                                ctx->hash_to_temp, hash + 1);

        if (temp)
                return temp - 1;

        /* If no temp is find, allocate one */
        temp = ctx->temp_count++;
        ctx->max_hash = MAX2(ctx->max_hash, hash);

        _mesa_hash_table_u64_insert(ctx->hash_to_temp,
                                    hash + 1, (void *) ((uintptr_t) temp + 1));

        return temp;
}

/* Reassigns numbering to get rid of gaps in the indices */

static void
mir_squeeze_index(compiler_context *ctx)
{
        /* Reset */
        ctx->temp_count = 0;
        /* TODO don't leak old hash_to_temp */
        ctx->hash_to_temp = _mesa_hash_table_u64_create(NULL);

        mir_foreach_instr_global(ctx, ins) {
                ins->dest = find_or_allocate_temp(ctx, ins->dest);

                for (unsigned i = 0; i < ARRAY_SIZE(ins->src); ++i)
                        ins->src[i] = find_or_allocate_temp(ctx, ins->src[i]);
        }
}

static midgard_instruction
v_load_store_scratch(
                unsigned srcdest,
                unsigned index,
                bool is_store,
                unsigned mask)
{
        /* We index by 32-bit vec4s */
        unsigned byte = (index * 4 * 4);

        midgard_instruction ins = {
                .type = TAG_LOAD_STORE_4,
                .mask = mask,
                .dest = ~0,
                .src = { ~0, ~0, ~0 },
                .load_store = {
                        .op = is_store ? midgard_op_st_int4 : midgard_op_ld_int4,
                        .swizzle = SWIZZLE_XYZW,

                        /* For register spilling - to thread local storage */
                        .arg_1 = 0xEA,
                        .arg_2 = 0x1E,

                        /* Splattered across, TODO combine logically */
                        .varying_parameters = (byte & 0x1FF) << 1,
                        .address = (byte >> 9)
                },

                /* If we spill an unspill, RA goes into an infinite loop */
                .no_spill = true
        };

       if (is_store) {
                /* r0 = r26, r1 = r27 */
                assert(srcdest == SSA_FIXED_REGISTER(26) || srcdest == SSA_FIXED_REGISTER(27));
                ins.src[0] = srcdest;
        } else {
                ins.dest = srcdest;
        }

        return ins;
}

/* If register allocation fails, find the best spill node and spill it to fix
 * whatever the issue was. This spill node could be a work register (spilling
 * to thread local storage), but it could also simply be a special register
 * that needs to spill to become a work register. */

static void mir_spill_register(
                compiler_context *ctx,
                struct ra_graph *g,
                unsigned *spill_count)
{
        unsigned spill_index = ctx->temp_count;

        /* Our first step is to calculate spill cost to figure out the best
         * spill node. All nodes are equal in spill cost, but we can't spill
         * nodes written to from an unspill */

        for (unsigned i = 0; i < ctx->temp_count; ++i) {
                ra_set_node_spill_cost(g, i, 1.0);
        }

        /* We can't spill any bundles that contain unspills. This could be
         * optimized to allow use of r27 to spill twice per bundle, but if
         * you're at the point of optimizing spilling, it's too late. */

        mir_foreach_block(ctx, block) {
                mir_foreach_bundle_in_block(block, bun) {
                        bool no_spill = false;

                        for (unsigned i = 0; i < bun->instruction_count; ++i)
                                no_spill |= bun->instructions[i]->no_spill;

                        if (!no_spill)
                                continue;

                        for (unsigned i = 0; i < bun->instruction_count; ++i) {
                                unsigned dest = bun->instructions[i]->dest;
                                if (dest < ctx->temp_count)
                                        ra_set_node_spill_cost(g, dest, -1.0);
                        }
                }
        }

        int spill_node = ra_get_best_spill_node(g);

        if (spill_node < 0) {
                mir_print_shader(ctx);
                assert(0);
        }

        /* We have a spill node, so check the class. Work registers
         * legitimately spill to TLS, but special registers just spill to work
         * registers */

        unsigned class = ra_get_node_class(g, spill_node);
        bool is_special = (class >> 2) != REG_CLASS_WORK;
        bool is_special_w = (class >> 2) == REG_CLASS_TEXW;

        /* Allocate TLS slot (maybe) */
        unsigned spill_slot = !is_special ? (*spill_count)++ : 0;

        /* For TLS, replace all stores to the spilled node. For
         * special reads, just keep as-is; the class will be demoted
         * implicitly. For special writes, spill to a work register */

        if (!is_special || is_special_w) {
                if (is_special_w)
                        spill_slot = spill_index++;

                mir_foreach_block(ctx, block) {
                mir_foreach_instr_in_block_safe(block, ins) {
                        if (ins->dest != spill_node) continue;

                        midgard_instruction st;

                        if (is_special_w) {
                                st = v_mov(spill_node, blank_alu_src, spill_slot);
                                st.no_spill = true;
                        } else {
                                ins->dest = SSA_FIXED_REGISTER(26);
                                ins->no_spill = true;
                                st = v_load_store_scratch(ins->dest, spill_slot, true, ins->mask);
                        }

                        /* Hint: don't rewrite this node */
                        st.hint = true;

                        mir_insert_instruction_after_scheduled(ctx, block, ins, st);

                        if (!is_special)
                                ctx->spills++;
                }
                }
        }

        /* For special reads, figure out how many components we need */
        unsigned read_mask = 0;

        mir_foreach_instr_global_safe(ctx, ins) {
                read_mask |= mir_mask_of_read_components(ins, spill_node);
        }

        /* Insert a load from TLS before the first consecutive
         * use of the node, rewriting to use spilled indices to
         * break up the live range. Or, for special, insert a
         * move. Ironically the latter *increases* register
         * pressure, but the two uses of the spilling mechanism
         * are somewhat orthogonal. (special spilling is to use
         * work registers to back special registers; TLS
         * spilling is to use memory to back work registers) */

        mir_foreach_block(ctx, block) {
                bool consecutive_skip = false;
                unsigned consecutive_index = 0;

                mir_foreach_instr_in_block(block, ins) {
                        /* We can't rewrite the moves used to spill in the
                         * first place. These moves are hinted. */
                        if (ins->hint) continue;

                        if (!mir_has_arg(ins, spill_node)) {
                                consecutive_skip = false;
                                continue;
                        }

                        if (consecutive_skip) {
                                /* Rewrite */
                                mir_rewrite_index_src_single(ins, spill_node, consecutive_index);
                                continue;
                        }

                        if (!is_special_w) {
                                consecutive_index = ++spill_index;

                                midgard_instruction *before = ins;

                                /* For a csel, go back one more not to break up the bundle */
                                if (ins->type == TAG_ALU_4 && OP_IS_CSEL(ins->alu.op))
                                        before = mir_prev_op(before);

                                midgard_instruction st;

                                if (is_special) {
                                        /* Move */
                                        st = v_mov(spill_node, blank_alu_src, consecutive_index);
                                        st.no_spill = true;
                                } else {
                                        /* TLS load */
                                        st = v_load_store_scratch(consecutive_index, spill_slot, false, 0xF);
                                }

                                /* Mask the load based on the component count
                                 * actually needed to prvent RA loops */

                                st.mask = read_mask;

                                mir_insert_instruction_before_scheduled(ctx, block, before, st);
                               // consecutive_skip = true;
                        } else {
                                /* Special writes already have their move spilled in */
                                consecutive_index = spill_slot;
                        }


                        /* Rewrite to use */
                        mir_rewrite_index_src_single(ins, spill_node, consecutive_index);

                        if (!is_special)
                                ctx->fills++;
                }
        }

        /* Reset hints */

        mir_foreach_instr_global(ctx, ins) {
                ins->hint = false;
        }
}

void
schedule_program(compiler_context *ctx)
{
        struct ra_graph *g = NULL;
        bool spilled = false;
        int iter_count = 1000; /* max iterations */

        /* Number of 128-bit slots in memory we've spilled into */
        unsigned spill_count = 0;

        midgard_promote_uniforms(ctx, 16);

        mir_foreach_block(ctx, block) {
                midgard_pair_load_store(ctx, block);
        }

        /* Must be lowered right before RA */
        mir_squeeze_index(ctx);
        mir_lower_special_reads(ctx);

        /* Lowering can introduce some dead moves */

        mir_foreach_block(ctx, block) {
                midgard_opt_dead_move_eliminate(ctx, block);
                schedule_block(ctx, block);
        }

        mir_create_pipeline_registers(ctx);

        do {
                if (spilled) 
                        mir_spill_register(ctx, g, &spill_count);

                mir_squeeze_index(ctx);

                g = NULL;
                g = allocate_registers(ctx, &spilled);
        } while(spilled && ((iter_count--) > 0));

        if (iter_count <= 0) {
                fprintf(stderr, "panfrost: Gave up allocating registers, rendering will be incomplete\n");
                assert(0);
        }

        /* Report spilling information. spill_count is in 128-bit slots (vec4 x
         * fp32), but tls_size is in bytes, so multiply by 16 */

        ctx->tls_size = spill_count * 16;

        install_registers(ctx, g);
}
