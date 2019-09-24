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

#ifndef __compiler_defines_h__
#define __compiler_defines_h__
#include "bifrost.h"
#include "bifrost_compile.h"
#include "bifrost_ops.h"

struct nir_builder;

typedef struct ssa_args {
        uint32_t dest;
        uint32_t src0, src1, src2, src3;
} ssa_args;

/**
 * @brief Singular unpacked instruction that lives outside of the clause bundle
 */
typedef struct bifrost_instruction {
        // Must be first
        struct list_head link;

        /**
         * @brief Pre-RA arguments
         */
        struct ssa_args ssa_args;
        uint32_t literal_args[4];
        uint32_t src_modifiers;
        unsigned op;


        /**
         * @brief Post-RA arguments
         */
        struct ssa_args args;

        /**
         * @brief The number of components that the destination takes up
         *
         * This allows the RA to understand when it needs to allocate registers from different classes
         */
        unsigned dest_components;

} bifrost_instruction;

typedef struct bifrost_clause {
        struct bifrost_header header;

        /* List of bifrost_instructions emitted for the current clause */
        struct list_head instructions;

} bifrost_clause;

typedef struct bifrost_block {
        /* Link to next block. Must be first for mir_get_block */
        struct list_head link;

        /* List of bifrost_instructions emitted for the current block */
        struct list_head instructions;

        /* List of bifrost clauses to be emitted for the current block*/
        struct util_dynarray clauses;

        /* Maximum number of successors is 2 */
        struct bifrost_block *successors[2];
        uint32_t num_successors;

} bifrost_block;

typedef struct compiler_context {
        nir_shader *nir;
        gl_shader_stage stage;

        /* Current NIR function */
        nir_function *func;
        struct nir_builder *b;

        /* Unordered list of bifrost_blocks */
        uint32_t block_count;
        struct list_head blocks;

        /* The current block we are operating on */
        struct bifrost_block *current_block;

        struct hash_table_u64 *ssa_constants;

        /* Uniform IDs */
        struct hash_table_u64 *uniform_nir_to_bi;
        uint32_t uniform_count;

        struct hash_table_u64 *varying_nir_to_bi;
        uint32_t varying_count;

        struct hash_table_u64 *outputs_nir_to_bi;
        uint32_t outputs_count;

        /* Count of instructions emitted from NIR overall, across all blocks */
        uint32_t instruction_count;

        uint32_t mir_temp;

        struct hash_table_u64 *hash_to_temp;
        uint32_t num_temps;

        uint32_t max_hash;

} compiler_context;

#define mir_foreach_block(ctx, v) list_for_each_entry(struct bifrost_block, v, &ctx->blocks, link)
#define mir_foreach_block_from(ctx, from, v) list_for_each_entry_from(struct bifrost_block, v, from, &ctx->blocks, link)

#define mir_last_block(ctx) list_last_entry(&ctx->blocks, struct bifrost_block, link)

#define mir_foreach_instr(ctx, v) list_for_each_entry(struct bifrost_instruction, v, &ctx->current_block->instructions, link)
#define mir_foreach_instr_in_block(block, v) list_for_each_entry(struct bifrost_instruction, v, &block->instructions, link)
#define mir_foreach_instr_in_block_from(block, v, from) list_for_each_entry_from(struct bifrost_instruction, v, from, &block->instructions, link)
#define mir_foreach_instr_in_block_safe(block, v) list_for_each_entry_safe(struct bifrost_instruction, v, &block->instructions, link)
#define mir_last_instr_in_block(block) list_last_entry(&block->instructions, struct bifrost_instruction, link)
#define mir_foreach_instr_in_block_from_rev(block, v, from) list_for_each_entry_from_rev(struct bifrost_instruction, v, from, &block->instructions, link)

#define mir_next_instr(from) list_first_entry(&(from->link), struct bifrost_instruction, link)
#define mir_remove_instr(instr) list_del(&instr->link)

#define mir_insert_instr_before(before, ins) list_addtail(&(mir_alloc_ins(ins))->link, &before->link)

#define SSA_INVALID_VALUE ~0U
#define SSA_TEMP_SHIFT 24
#define SSA_FIXED_REGISTER_SHIFT 25

#define SSA_FIXED_REGISTER(x) ((1U << SSA_FIXED_REGISTER_SHIFT) + (x))
#define SSA_REG_FROM_FIXED(x) ((x) & ~(1U << SSA_FIXED_REGISTER_SHIFT))

#define SSA_FIXED_MINIMUM SSA_FIXED_REGISTER(0)
#define SSA_FIXED_UREG_MINIMUM SSA_FIXED_REGISTER(64)
#define SSA_FIXED_CONST_0 SSA_FIXED_REGISTER(256 + 64)

#define SSA_FIXED_UREGISTER(x) (SSA_FIXED_REGISTER(x + 64))
#define SSA_UREG_FROM_FIXED(x) (SSA_REG_FROM_FIXED(x) - 64)

#define SSA_TEMP_VALUE(x) ((1U << SSA_TEMP_SHIFT) + (x))
#define SSA_TEMP_FROM_VALUE(x) (((x) & ~(1U << SSA_TEMP_SHIFT)))
#define MIR_TEMP_MINIMUM SSA_TEMP_VALUE(0)

#define SRC_MOD_ABS 1
#define SRC_MOD_NEG 2
#define MOD_SIZE 2
#define SOURCE_MODIFIER(src, mod) (mod << (src * MOD_SIZE))

struct bifrost_instruction *
mir_alloc_ins(struct bifrost_instruction instr);

#endif
