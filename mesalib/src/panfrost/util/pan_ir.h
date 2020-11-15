/*
 * Copyright (C) 2020 Collabora, Ltd.
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

#ifndef __PAN_IR_H
#define __PAN_IR_H

#include <stdint.h>
#include "compiler/nir/nir.h"
#include "util/u_dynarray.h"
#include "util/hash_table.h"

/* Define the general compiler entry point */

#define MAX_SYSVAL_COUNT 32

/* Allow 2D of sysval IDs, while allowing nonparametric sysvals to equal
 * their class for equal comparison */

#define PAN_SYSVAL(type, no) (((no) << 16) | PAN_SYSVAL_##type)
#define PAN_SYSVAL_TYPE(sysval) ((sysval) & 0xffff)
#define PAN_SYSVAL_ID(sysval) ((sysval) >> 16)

/* Define some common types. We start at one for easy indexing of hash
 * tables internal to the compiler */

enum {
        PAN_SYSVAL_VIEWPORT_SCALE = 1,
        PAN_SYSVAL_VIEWPORT_OFFSET = 2,
        PAN_SYSVAL_TEXTURE_SIZE = 3,
        PAN_SYSVAL_SSBO = 4,
        PAN_SYSVAL_NUM_WORK_GROUPS = 5,
        PAN_SYSVAL_SAMPLER = 7,
};

#define PAN_TXS_SYSVAL_ID(texidx, dim, is_array)          \
	((texidx) | ((dim) << 7) | ((is_array) ? (1 << 9) : 0))

#define PAN_SYSVAL_ID_TO_TXS_TEX_IDX(id)        ((id) & 0x7f)
#define PAN_SYSVAL_ID_TO_TXS_DIM(id)            (((id) >> 7) & 0x3)
#define PAN_SYSVAL_ID_TO_TXS_IS_ARRAY(id)       !!((id) & (1 << 9))

/* Special attribute slots for vertex builtins. Sort of arbitrary but let's be
 * consistent with the blob so we can compare traces easier. */

enum {
        PAN_VERTEX_ID   = 16,
        PAN_INSTANCE_ID = 17,
        PAN_MAX_ATTRIBUTE
};

struct panfrost_sysvals {
        /* The mapping of sysvals to uniforms, the count, and the off-by-one inverse */
        unsigned sysvals[MAX_SYSVAL_COUNT];
        unsigned sysval_count;
        struct hash_table_u64 *sysval_to_id;
};

void
panfrost_nir_assign_sysvals(struct panfrost_sysvals *ctx, void *memctx, nir_shader *shader);

int
panfrost_sysval_for_instr(nir_instr *instr, nir_dest *dest);

bool
nir_undef_to_zero(nir_shader *shader);

typedef struct {
        int work_register_count;
        int uniform_cutoff;

        /* For Bifrost - output type for each RT */
        nir_alu_type blend_types[8];

        /* For Bifrost - return address for blend instructions */
        uint32_t blend_ret_offsets[8];

        /* Prepended before uniforms, mapping to SYSVAL_ names for the
         * sysval */

        unsigned sysval_count;
        unsigned sysvals[MAX_SYSVAL_COUNT];

        int first_tag;

        struct util_dynarray compiled;

        /* The number of bytes to allocate per-thread for Thread Local Storage
         * (register spilling), or zero if no spilling is used */
        unsigned tls_size;

} panfrost_program;

struct panfrost_compile_inputs {
        unsigned gpu_id;
        bool is_blend;
        struct {
                unsigned rt;
                float constants[4];
                uint64_t bifrost_blend_desc;
        } blend;
        bool shaderdb;

        enum pipe_format rt_formats[8];
};

typedef struct pan_block {
        /* Link to next block. Must be first for mir_get_block */
        struct list_head link;

        /* List of instructions emitted for the current block */
        struct list_head instructions;

        /* Index of the block in source order */
        unsigned name;

        /* Control flow graph */
        struct pan_block *successors[2];
        struct set *predecessors;
        bool unconditional_jumps;

        /* In liveness analysis, these are live masks (per-component) for
         * indices for the block. Scalar compilers have the luxury of using
         * simple bit fields, but for us, liveness is a vector idea. */
        uint16_t *live_in;
        uint16_t *live_out;
} pan_block;

struct pan_instruction {
        struct list_head link;
};

#define pan_foreach_instr_in_block_rev(block, v) \
        list_for_each_entry_rev(struct pan_instruction, v, &block->instructions, link)

#define pan_foreach_successor(blk, v) \
        pan_block *v; \
        pan_block **_v; \
        for (_v = (pan_block **) &blk->successors[0], \
                v = *_v; \
                v != NULL && _v < (pan_block **) &blk->successors[2]; \
                _v++, v = *_v) \

#define pan_foreach_predecessor(blk, v) \
        struct set_entry *_entry_##v; \
        struct pan_block *v; \
        for (_entry_##v = _mesa_set_next_entry(blk->predecessors, NULL), \
                v = (struct pan_block *) (_entry_##v ? _entry_##v->key : NULL);  \
                _entry_##v != NULL; \
                _entry_##v = _mesa_set_next_entry(blk->predecessors, _entry_##v), \
                v = (struct pan_block *) (_entry_##v ? _entry_##v->key : NULL))

static inline pan_block *
pan_exit_block(struct list_head *blocks)
{
        pan_block *last = list_last_entry(blocks, pan_block, link);
        assert(!last->successors[0] && !last->successors[1]);
        return last;
}

typedef void (*pan_liveness_update)(uint16_t *, void *, unsigned max);

void pan_liveness_gen(uint16_t *live, unsigned node, unsigned max, uint16_t mask);
void pan_liveness_kill(uint16_t *live, unsigned node, unsigned max, uint16_t mask);
bool pan_liveness_get(uint16_t *live, unsigned node, uint16_t max);

void pan_compute_liveness(struct list_head *blocks,
                unsigned temp_count,
                pan_liveness_update callback);

void pan_free_liveness(struct list_head *blocks);

uint16_t
pan_to_bytemask(unsigned bytes, unsigned mask);

void pan_block_add_successor(pan_block *block, pan_block *successor);

/* IR indexing */
#define PAN_IS_REG (1)

static inline unsigned
pan_ssa_index(nir_ssa_def *ssa)
{
        /* Off-by-one ensures BIR_NO_ARG is skipped */
        return ((ssa->index + 1) << 1) | 0;
}

static inline unsigned
pan_src_index(nir_src *src)
{
        if (src->is_ssa)
                return pan_ssa_index(src->ssa);
        else {
                assert(!src->reg.indirect);
                return (src->reg.reg->index << 1) | PAN_IS_REG;
        }
}

static inline unsigned
pan_dest_index(nir_dest *dst)
{
        if (dst->is_ssa)
                return pan_ssa_index(&dst->ssa);
        else {
                assert(!dst->reg.indirect);
                return (dst->reg.reg->index << 1) | PAN_IS_REG;
        }
}

/* IR printing helpers */
void pan_print_alu_type(nir_alu_type t, FILE *fp);

/* Until it can be upstreamed.. */
bool pan_has_source_mod(nir_alu_src *src, nir_op op);
bool pan_has_dest_mod(nir_dest **dest, nir_op op);

/* NIR passes to do some backend-specific lowering */

#define PAN_WRITEOUT_C 1
#define PAN_WRITEOUT_Z 2
#define PAN_WRITEOUT_S 4

bool pan_nir_reorder_writeout(nir_shader *nir);
bool pan_nir_lower_zs_store(nir_shader *nir);

#endif
