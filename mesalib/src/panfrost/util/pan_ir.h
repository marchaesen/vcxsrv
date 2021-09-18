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
        PAN_SYSVAL_LOCAL_GROUP_SIZE = 8,
        PAN_SYSVAL_WORK_DIM = 9,
        PAN_SYSVAL_IMAGE_SIZE = 10,
        PAN_SYSVAL_SAMPLE_POSITIONS = 11,
        PAN_SYSVAL_MULTISAMPLED = 12,
        PAN_SYSVAL_RT_CONVERSION = 13,
        PAN_SYSVAL_VERTEX_INSTANCE_OFFSETS = 14,
        PAN_SYSVAL_DRAWID = 15,
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
};

/* Technically Midgard could go up to 92 in a pathological case but we don't
 * take advantage of that. Likewise Bifrost's FAU encoding can address 128
 * words but actual implementations (G72, G76) are capped at 64 */

#define PAN_MAX_PUSH 64

/* Architectural invariants (Midgard and Bifrost): UBO must be <= 2^16 bytes so
 * an offset to a word must be < 2^16. There are less than 2^8 UBOs */

struct panfrost_ubo_word {
        uint16_t ubo;
        uint16_t offset;
};

struct panfrost_ubo_push {
        unsigned count;
        struct panfrost_ubo_word words[PAN_MAX_PUSH];
};

/* Helper for searching the above. Note this is O(N) to the number of pushed
 * constants, do not run in the draw call hot path */

unsigned
pan_lookup_pushed_ubo(struct panfrost_ubo_push *push, unsigned ubo, unsigned offs);

struct hash_table_u64 *
panfrost_init_sysvals(struct panfrost_sysvals *sysvals, void *memctx);

unsigned
pan_lookup_sysval(struct hash_table_u64 *sysval_to_id,
                  struct panfrost_sysvals *sysvals,
                  int sysval);

int
panfrost_sysval_for_instr(nir_instr *instr, nir_dest *dest);

struct panfrost_compile_inputs {
        unsigned gpu_id;
        bool is_blend, is_blit;
        struct {
                unsigned rt;
                unsigned nr_samples;
                uint64_t bifrost_blend_desc;
        } blend;
        unsigned sysval_ubo;
        bool shaderdb;
        bool no_ubo_to_push;

        enum pipe_format rt_formats[8];
        unsigned nr_cbufs;
};

struct pan_shader_varying {
        gl_varying_slot location;
        enum pipe_format format;
};

struct bifrost_shader_blend_info {
        nir_alu_type type;
        uint32_t return_offset;

        /* mali_bifrost_register_file_format corresponding to nir_alu_type */
        unsigned format;
};

struct bifrost_shader_info {
        struct bifrost_shader_blend_info blend[8];
        nir_alu_type blend_src1_type;
        bool wait_6, wait_7;

        /* Packed, preloaded message descriptors */
        uint16_t messages[2];
};

struct midgard_shader_info {
        unsigned first_tag;
};

struct pan_shader_info {
        gl_shader_stage stage;
        unsigned work_reg_count;
        unsigned tls_size;
        unsigned wls_size;

        union {
                struct {
                        bool reads_frag_coord;
                        bool reads_point_coord;
                        bool reads_face;
                        bool helper_invocations;
                        bool can_discard;
                        bool writes_depth;
                        bool writes_stencil;
                        bool writes_coverage;
                        bool sidefx;
                        bool reads_sample_id;
                        bool reads_sample_pos;
                        bool reads_sample_mask_in;
                        bool reads_helper_invocation;
                        bool sample_shading;
                        bool early_fragment_tests;
                        bool can_early_z, can_fpk;
                        BITSET_WORD outputs_read;
                        BITSET_WORD outputs_written;
                } fs;

                struct {
                        bool writes_point_size;
                } vs;
        };

        bool separable;
        bool contains_barrier;
        bool writes_global;
        uint64_t outputs_written;

        unsigned sampler_count;
        unsigned texture_count;
        unsigned ubo_count;
        unsigned attribute_count;

        struct {
                unsigned input_count;
                struct pan_shader_varying input[MAX_VARYING];
                unsigned output_count;
                struct pan_shader_varying output[MAX_VARYING];
        } varyings;

        struct panfrost_sysvals sysvals;

        /* UBOs to push to Register Mapped Uniforms (Midgard) or Fast Access
         * Uniforms (Bifrost) */
        struct panfrost_ubo_push push;

        uint32_t ubo_mask;

        union {
                struct bifrost_shader_info bifrost;
                struct midgard_shader_info midgard;
        };
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

bool pan_nir_lower_64bit_intrin(nir_shader *shader);

bool pan_lower_helper_invocation(nir_shader *shader);
bool pan_lower_sample_pos(nir_shader *shader);

#endif
