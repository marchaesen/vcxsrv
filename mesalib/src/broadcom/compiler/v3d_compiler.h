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

#ifndef V3D_COMPILER_H
#define V3D_COMPILER_H

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "util/macros.h"
#include "common/v3d_debug.h"
#include "common/v3d_device_info.h"
#include "compiler/nir/nir.h"
#include "util/list.h"
#include "util/u_math.h"

#include "qpu/qpu_instr.h"
#include "pipe/p_state.h"

#define V3D_MAX_TEXTURE_SAMPLERS 32
#define V3D_MAX_SAMPLES 4
#define V3D_MAX_FS_INPUTS 64
#define V3D_MAX_VS_INPUTS 64

struct nir_builder;

struct v3d_fs_inputs {
        /**
         * Array of the meanings of the VPM inputs this shader needs.
         *
         * It doesn't include those that aren't part of the VPM, like
         * point/line coordinates.
         */
        struct v3d_varying_slot *input_slots;
        uint32_t num_inputs;
};

enum qfile {
        /** An unused source or destination register. */
        QFILE_NULL,

        /** A physical register, such as the W coordinate payload. */
        QFILE_REG,
        /** One of the regsiters for fixed function interactions. */
        QFILE_MAGIC,

        /**
         *  A virtual register, that will be allocated to actual accumulator
         * or physical registers later.
         */
        QFILE_TEMP,
        QFILE_UNIF,
        QFILE_TLB,
        QFILE_TLBU,

        /**
         * VPM reads use this with an index value to say what part of the VPM
         * is being read.
         */
        QFILE_VPM,

        /**
         * Stores an immediate value in the index field that will be used
         * directly by qpu_load_imm().
         */
        QFILE_LOAD_IMM,

        /**
         * Stores an immediate value in the index field that can be turned
         * into a small immediate field by qpu_encode_small_immediate().
         */
        QFILE_SMALL_IMM,
};

/**
 * A reference to a QPU register or a virtual temp register.
 */
struct qreg {
        enum qfile file;
        uint32_t index;
};

static inline struct qreg vir_reg(enum qfile file, uint32_t index)
{
        return (struct qreg){file, index};
}

/**
 * A reference to an actual register at the QPU level, for register
 * allocation.
 */
struct qpu_reg {
        bool magic;
        int index;
};

struct qinst {
        /** Entry in qblock->instructions */
        struct list_head link;

        /**
         * The instruction being wrapped.  Its condition codes, pack flags,
         * signals, etc. will all be used, with just the register references
         * being replaced by the contents of qinst->dst and qinst->src[].
         */
        struct v3d_qpu_instr qpu;

        /* Pre-register-allocation references to src/dst registers */
        struct qreg dst;
        struct qreg src[3];
        bool cond_is_exec_mask;
        bool has_implicit_uniform;
        bool is_last_thrsw;

        /* After vir_to_qpu.c: If instr reads a uniform, which uniform from
         * the uncompiled stream it is.
         */
        int uniform;
};

enum quniform_contents {
        /**
         * Indicates that a constant 32-bit value is copied from the program's
         * uniform contents.
         */
        QUNIFORM_CONSTANT,
        /**
         * Indicates that the program's uniform contents are used as an index
         * into the GL uniform storage.
         */
        QUNIFORM_UNIFORM,

        /** @{
         * Scaling factors from clip coordinates to relative to the viewport
         * center.
         *
         * This is used by the coordinate and vertex shaders to produce the
         * 32-bit entry consisting of 2 16-bit fields with 12.4 signed fixed
         * point offsets from the viewport ccenter.
         */
        QUNIFORM_VIEWPORT_X_SCALE,
        QUNIFORM_VIEWPORT_Y_SCALE,
        /** @} */

        QUNIFORM_VIEWPORT_Z_OFFSET,
        QUNIFORM_VIEWPORT_Z_SCALE,

        QUNIFORM_USER_CLIP_PLANE,

        /**
         * A reference to a V3D 3.x texture config parameter 0 uniform.
         *
         * This is a uniform implicitly loaded with a QPU_W_TMU* write, which
         * defines texture type, miplevels, and such.  It will be found as a
         * parameter to the first QOP_TEX_[STRB] instruction in a sequence.
         */
        QUNIFORM_TEXTURE_CONFIG_P0_0,
        QUNIFORM_TEXTURE_CONFIG_P0_1,
        QUNIFORM_TEXTURE_CONFIG_P0_2,
        QUNIFORM_TEXTURE_CONFIG_P0_3,
        QUNIFORM_TEXTURE_CONFIG_P0_4,
        QUNIFORM_TEXTURE_CONFIG_P0_5,
        QUNIFORM_TEXTURE_CONFIG_P0_6,
        QUNIFORM_TEXTURE_CONFIG_P0_7,
        QUNIFORM_TEXTURE_CONFIG_P0_8,
        QUNIFORM_TEXTURE_CONFIG_P0_9,
        QUNIFORM_TEXTURE_CONFIG_P0_10,
        QUNIFORM_TEXTURE_CONFIG_P0_11,
        QUNIFORM_TEXTURE_CONFIG_P0_12,
        QUNIFORM_TEXTURE_CONFIG_P0_13,
        QUNIFORM_TEXTURE_CONFIG_P0_14,
        QUNIFORM_TEXTURE_CONFIG_P0_15,
        QUNIFORM_TEXTURE_CONFIG_P0_16,
        QUNIFORM_TEXTURE_CONFIG_P0_17,
        QUNIFORM_TEXTURE_CONFIG_P0_18,
        QUNIFORM_TEXTURE_CONFIG_P0_19,
        QUNIFORM_TEXTURE_CONFIG_P0_20,
        QUNIFORM_TEXTURE_CONFIG_P0_21,
        QUNIFORM_TEXTURE_CONFIG_P0_22,
        QUNIFORM_TEXTURE_CONFIG_P0_23,
        QUNIFORM_TEXTURE_CONFIG_P0_24,
        QUNIFORM_TEXTURE_CONFIG_P0_25,
        QUNIFORM_TEXTURE_CONFIG_P0_26,
        QUNIFORM_TEXTURE_CONFIG_P0_27,
        QUNIFORM_TEXTURE_CONFIG_P0_28,
        QUNIFORM_TEXTURE_CONFIG_P0_29,
        QUNIFORM_TEXTURE_CONFIG_P0_30,
        QUNIFORM_TEXTURE_CONFIG_P0_31,
        QUNIFORM_TEXTURE_CONFIG_P0_32,

        /**
         * A reference to a V3D 3.x texture config parameter 1 uniform.
         *
         * This is a uniform implicitly loaded with a QPU_W_TMU* write, which
         * has the pointer to the indirect texture state.  Our data[] field
         * will have a packed p1 value, but the address field will be just
         * which texture unit's texture should be referenced.
         */
        QUNIFORM_TEXTURE_CONFIG_P1,

        /* A a V3D 4.x texture config parameter.  The high 8 bits will be
         * which texture or sampler is being sampled, and the driver must
         * replace the address field with the appropriate address.
         */
        QUNIFORM_TMU_CONFIG_P0,
        QUNIFORM_TMU_CONFIG_P1,

        QUNIFORM_TEXTURE_FIRST_LEVEL,

        QUNIFORM_TEXTURE_WIDTH,
        QUNIFORM_TEXTURE_HEIGHT,
        QUNIFORM_TEXTURE_DEPTH,
        QUNIFORM_TEXTURE_ARRAY_SIZE,
        QUNIFORM_TEXTURE_LEVELS,

        QUNIFORM_UBO_ADDR,

        QUNIFORM_TEXRECT_SCALE_X,
        QUNIFORM_TEXRECT_SCALE_Y,

        QUNIFORM_TEXTURE_BORDER_COLOR,

        QUNIFORM_ALPHA_REF,
        QUNIFORM_SAMPLE_MASK,

        /**
         * Returns the the offset of the scratch buffer for register spilling.
         */
        QUNIFORM_SPILL_OFFSET,
        QUNIFORM_SPILL_SIZE_PER_THREAD,
};

struct v3d_varying_slot {
        uint8_t slot_and_component;
};

static inline struct v3d_varying_slot
v3d_slot_from_slot_and_component(uint8_t slot, uint8_t component)
{
        assert(slot < 255 / 4);
        return (struct v3d_varying_slot){ (slot << 2) + component };
}

static inline uint8_t v3d_slot_get_slot(struct v3d_varying_slot slot)
{
        return slot.slot_and_component >> 2;
}

static inline uint8_t v3d_slot_get_component(struct v3d_varying_slot slot)
{
        return slot.slot_and_component & 3;
}

struct v3d_ubo_range {
        /**
         * offset in bytes from the start of the ubo where this range is
         * uploaded.
         *
         * Only set once used is set.
         */
        uint32_t dst_offset;

        /**
         * offset in bytes from the start of the gallium uniforms where the
         * data comes from.
         */
        uint32_t src_offset;

        /** size in bytes of this ubo range */
        uint32_t size;
};

struct v3d_key {
        void *shader_state;
        struct {
                uint8_t swizzle[4];
                uint8_t return_size;
                uint8_t return_channels;
                unsigned compare_mode:1;
                unsigned compare_func:3;
                bool clamp_s:1;
                bool clamp_t:1;
                bool clamp_r:1;
        } tex[V3D_MAX_TEXTURE_SAMPLERS];
        uint8_t ucp_enables;
};

struct v3d_fs_key {
        struct v3d_key base;
        bool depth_enabled;
        bool is_points;
        bool is_lines;
        bool alpha_test;
        bool point_coord_upper_left;
        bool light_twoside;
        bool msaa;
        bool sample_coverage;
        bool sample_alpha_to_coverage;
        bool sample_alpha_to_one;
        bool clamp_color;
        bool shade_model_flat;
        uint8_t nr_cbufs;
        uint8_t swap_color_rb;
        /* Mask of which render targets need to be written as 32-bit floats */
        uint8_t f32_color_rb;
        /* Masks of which render targets need to be written as ints/uints.
         * Used by gallium to work around lost information in TGSI.
         */
        uint8_t int_color_rb;
        uint8_t uint_color_rb;
        uint8_t alpha_test_func;
        uint8_t logicop_func;
        uint32_t point_sprite_mask;

        struct pipe_rt_blend_state blend;
};

struct v3d_vs_key {
        struct v3d_key base;

        struct v3d_varying_slot fs_inputs[V3D_MAX_FS_INPUTS];
        uint8_t num_fs_inputs;

        bool is_coord;
        bool per_vertex_point_size;
        bool clamp_color;
};

/** A basic block of VIR intructions. */
struct qblock {
        struct list_head link;

        struct list_head instructions;

        struct set *predecessors;
        struct qblock *successors[2];

        int index;

        /* Instruction IPs for the first and last instruction of the block.
         * Set by qpu_schedule.c.
         */
        uint32_t start_qpu_ip;
        uint32_t end_qpu_ip;

        /* Instruction IP for the branch instruction of the block.  Set by
         * qpu_schedule.c.
         */
        uint32_t branch_qpu_ip;

        /** Offset within the uniform stream at the start of the block. */
        uint32_t start_uniform;
        /** Offset within the uniform stream of the branch instruction */
        uint32_t branch_uniform;

        /** @{ used by v3d_vir_live_variables.c */
        BITSET_WORD *def;
        BITSET_WORD *use;
        BITSET_WORD *live_in;
        BITSET_WORD *live_out;
        int start_ip, end_ip;
        /** @} */
};

/** Which util/list.h add mode we should use when inserting an instruction. */
enum vir_cursor_mode {
        vir_cursor_add,
        vir_cursor_addtail,
};

/**
 * Tracking structure for where new instructions should be inserted.  Create
 * with one of the vir_after_inst()-style helper functions.
 *
 * This does not protect against removal of the block or instruction, so we
 * have an assert in instruction removal to try to catch it.
 */
struct vir_cursor {
        enum vir_cursor_mode mode;
        struct list_head *link;
};

static inline struct vir_cursor
vir_before_inst(struct qinst *inst)
{
        return (struct vir_cursor){ vir_cursor_addtail, &inst->link };
}

static inline struct vir_cursor
vir_after_inst(struct qinst *inst)
{
        return (struct vir_cursor){ vir_cursor_add, &inst->link };
}

static inline struct vir_cursor
vir_before_block(struct qblock *block)
{
        return (struct vir_cursor){ vir_cursor_add, &block->instructions };
}

static inline struct vir_cursor
vir_after_block(struct qblock *block)
{
        return (struct vir_cursor){ vir_cursor_addtail, &block->instructions };
}

/**
 * Compiler state saved across compiler invocations, for any expensive global
 * setup.
 */
struct v3d_compiler {
        const struct v3d_device_info *devinfo;
        struct ra_regs *regs;
        unsigned int reg_class_phys[3];
        unsigned int reg_class_phys_or_acc[3];
};

struct v3d_compile {
        const struct v3d_device_info *devinfo;
        nir_shader *s;
        nir_function_impl *impl;
        struct exec_list *cf_node_list;
        const struct v3d_compiler *compiler;

        /**
         * Mapping from nir_register * or nir_ssa_def * to array of struct
         * qreg for the values.
         */
        struct hash_table *def_ht;

        /* For each temp, the instruction generating its value. */
        struct qinst **defs;
        uint32_t defs_array_size;

        /**
         * Inputs to the shader, arranged by TGSI declaration order.
         *
         * Not all fragment shader QFILE_VARY reads are present in this array.
         */
        struct qreg *inputs;
        struct qreg *outputs;
        bool msaa_per_sample_output;
        struct qreg color_reads[V3D_MAX_SAMPLES];
        struct qreg sample_colors[V3D_MAX_SAMPLES];
        uint32_t inputs_array_size;
        uint32_t outputs_array_size;
        uint32_t uniforms_array_size;

        /* Booleans for whether the corresponding QFILE_VARY[i] is
         * flat-shaded.  This includes gl_FragColor flat-shading, which is
         * customized based on the shademodel_flat shader key.
         */
        uint32_t flat_shade_flags[BITSET_WORDS(V3D_MAX_FS_INPUTS)];

        uint32_t centroid_flags[BITSET_WORDS(V3D_MAX_FS_INPUTS)];

        bool uses_center_w;

        struct v3d_ubo_range *ubo_ranges;
        bool *ubo_range_used;
        uint32_t ubo_ranges_array_size;
        /** Number of uniform areas tracked in ubo_ranges. */
        uint32_t num_ubo_ranges;
        uint32_t next_ubo_dst_offset;

        /* State for whether we're executing on each channel currently.  0 if
         * yes, otherwise a block number + 1 that the channel jumped to.
         */
        struct qreg execute;

        struct qreg line_x, point_x, point_y;

        /**
         * Instance ID, which comes in before the vertex attribute payload if
         * the shader record requests it.
         */
        struct qreg iid;

        /**
         * Vertex ID, which comes in before the vertex attribute payload
         * (after Instance ID) if the shader record requests it.
         */
        struct qreg vid;

        /* Fragment shader payload regs. */
        struct qreg payload_w, payload_w_centroid, payload_z;

        uint8_t vattr_sizes[V3D_MAX_VS_INPUTS];
        uint32_t num_vpm_writes;

        /* Size in bytes of registers that have been spilled. This is how much
         * space needs to be available in the spill BO per thread per QPU.
         */
        uint32_t spill_size;
        /* Shader-db stats for register spilling. */
        uint32_t spills, fills;
        /**
         * Register spilling's per-thread base address, shared between each
         * spill/fill's addressing calculations.
         */
        struct qreg spill_base;
        /* Bit vector of which temps may be spilled */
        BITSET_WORD *spillable;

        /**
         * Array of the VARYING_SLOT_* of all FS QFILE_VARY reads.
         *
         * This includes those that aren't part of the VPM varyings, like
         * point/line coordinates.
         */
        struct v3d_varying_slot input_slots[V3D_MAX_FS_INPUTS];

        /**
         * An entry per outputs[] in the VS indicating what the VARYING_SLOT_*
         * of the output is.  Used to emit from the VS in the order that the
         * FS needs.
         */
        struct v3d_varying_slot *output_slots;

        struct pipe_shader_state *shader_state;
        struct v3d_key *key;
        struct v3d_fs_key *fs_key;
        struct v3d_vs_key *vs_key;

        /* Live ranges of temps. */
        int *temp_start, *temp_end;
        bool live_intervals_valid;

        uint32_t *uniform_data;
        enum quniform_contents *uniform_contents;
        uint32_t uniform_array_size;
        uint32_t num_uniforms;
        uint32_t num_outputs;
        uint32_t output_position_index;
        nir_variable *output_color_var[4];
        uint32_t output_point_size_index;
        uint32_t output_sample_mask_index;

        struct qreg undef;
        uint32_t num_temps;

        struct vir_cursor cursor;
        struct list_head blocks;
        int next_block_index;
        struct qblock *cur_block;
        struct qblock *loop_cont_block;
        struct qblock *loop_break_block;

        uint64_t *qpu_insts;
        uint32_t qpu_inst_count;
        uint32_t qpu_inst_size;

        /* For the FS, the number of varying inputs not counting the
         * point/line varyings payload
         */
        uint32_t num_inputs;

        /**
         * Number of inputs from num_inputs remaining to be queued to the read
         * FIFO in the VS/CS.
         */
        uint32_t num_inputs_remaining;

        /* Number of inputs currently in the read FIFO for the VS/CS */
        uint32_t num_inputs_in_fifo;

        /** Next offset in the VPM to read from in the VS/CS */
        uint32_t vpm_read_offset;

        uint32_t program_id;
        uint32_t variant_id;

        /* Set to compile program in in 1x, 2x, or 4x threaded mode, where
         * SIG_THREAD_SWITCH is used to hide texturing latency at the cost of
         * limiting ourselves to the part of the physical reg space.
         *
         * On V3D 3.x, 2x or 4x divide the physical reg space by 2x or 4x.  On
         * V3D 4.x, all shaders are 2x threaded, and 4x only divides the
         * physical reg space in half.
         */
        uint8_t threads;
        struct qinst *last_thrsw;
        bool last_thrsw_at_top_level;

        bool failed;
};

struct v3d_uniform_list {
        enum quniform_contents *contents;
        uint32_t *data;
        uint32_t count;
};

struct v3d_prog_data {
        struct v3d_uniform_list uniforms;

        struct v3d_ubo_range *ubo_ranges;
        uint32_t num_ubo_ranges;
        uint32_t ubo_size;
        uint32_t spill_size;

        uint8_t num_inputs;
        uint8_t threads;

        /* For threads > 1, whether the program should be dispatched in the
         * after-final-THRSW state.
         */
        bool single_seg;
};

struct v3d_vs_prog_data {
        struct v3d_prog_data base;

        bool uses_iid, uses_vid;

        /* Number of components read from each vertex attribute. */
        uint8_t vattr_sizes[32];

        /* Total number of components read, for the shader state record. */
        uint32_t vpm_input_size;

        /* Total number of components written, for the shader state record. */
        uint32_t vpm_output_size;
};

struct v3d_fs_prog_data {
        struct v3d_prog_data base;

        struct v3d_varying_slot input_slots[V3D_MAX_FS_INPUTS];

        /* Array of flat shade flags.
         *
         * Each entry is only 24 bits (high 8 bits 0), to match the hardware
         * packet layout.
         */
        uint32_t flat_shade_flags[((V3D_MAX_FS_INPUTS - 1) / 24) + 1];

        uint32_t centroid_flags[((V3D_MAX_FS_INPUTS - 1) / 24) + 1];

        bool writes_z;
        bool discard;
        bool uses_center_w;
};

/* Special nir_load_input intrinsic index for loading the current TLB
 * destination color.
 */
#define V3D_NIR_TLB_COLOR_READ_INPUT		2000000000

#define V3D_NIR_MS_MASK_OUTPUT			2000000000

extern const nir_shader_compiler_options v3d_nir_options;

const struct v3d_compiler *v3d_compiler_init(const struct v3d_device_info *devinfo);
void v3d_compiler_free(const struct v3d_compiler *compiler);
void v3d_optimize_nir(struct nir_shader *s);

uint64_t *v3d_compile_vs(const struct v3d_compiler *compiler,
                         struct v3d_vs_key *key,
                         struct v3d_vs_prog_data *prog_data,
                         nir_shader *s,
                         int program_id, int variant_id,
                         uint32_t *final_assembly_size);

uint64_t *v3d_compile_fs(const struct v3d_compiler *compiler,
                         struct v3d_fs_key *key,
                         struct v3d_fs_prog_data *prog_data,
                         nir_shader *s,
                         int program_id, int variant_id,
                         uint32_t *final_assembly_size);

void v3d_nir_to_vir(struct v3d_compile *c);

void vir_compile_destroy(struct v3d_compile *c);
const char *vir_get_stage_name(struct v3d_compile *c);
struct qblock *vir_new_block(struct v3d_compile *c);
void vir_set_emit_block(struct v3d_compile *c, struct qblock *block);
void vir_link_blocks(struct qblock *predecessor, struct qblock *successor);
struct qblock *vir_entry_block(struct v3d_compile *c);
struct qblock *vir_exit_block(struct v3d_compile *c);
struct qinst *vir_add_inst(enum v3d_qpu_add_op op, struct qreg dst,
                           struct qreg src0, struct qreg src1);
struct qinst *vir_mul_inst(enum v3d_qpu_mul_op op, struct qreg dst,
                           struct qreg src0, struct qreg src1);
struct qinst *vir_branch_inst(enum v3d_qpu_branch_cond cond, struct qreg src0);
void vir_remove_instruction(struct v3d_compile *c, struct qinst *qinst);
struct qreg vir_uniform(struct v3d_compile *c,
                        enum quniform_contents contents,
                        uint32_t data);
void vir_schedule_instructions(struct v3d_compile *c);
struct v3d_qpu_instr v3d_qpu_nop(void);

struct qreg vir_emit_def(struct v3d_compile *c, struct qinst *inst);
struct qinst *vir_emit_nondef(struct v3d_compile *c, struct qinst *inst);
void vir_set_cond(struct qinst *inst, enum v3d_qpu_cond cond);
void vir_set_pf(struct qinst *inst, enum v3d_qpu_pf pf);
void vir_set_unpack(struct qinst *inst, int src,
                    enum v3d_qpu_input_unpack unpack);

struct qreg vir_get_temp(struct v3d_compile *c);
void vir_emit_last_thrsw(struct v3d_compile *c);
void vir_calculate_live_intervals(struct v3d_compile *c);
bool vir_has_implicit_uniform(struct qinst *inst);
int vir_get_implicit_uniform_src(struct qinst *inst);
int vir_get_non_sideband_nsrc(struct qinst *inst);
int vir_get_nsrc(struct qinst *inst);
bool vir_has_side_effects(struct v3d_compile *c, struct qinst *inst);
bool vir_get_add_op(struct qinst *inst, enum v3d_qpu_add_op *op);
bool vir_get_mul_op(struct qinst *inst, enum v3d_qpu_mul_op *op);
bool vir_is_raw_mov(struct qinst *inst);
bool vir_is_tex(struct qinst *inst);
bool vir_is_add(struct qinst *inst);
bool vir_is_mul(struct qinst *inst);
bool vir_is_float_input(struct qinst *inst);
bool vir_depends_on_flags(struct qinst *inst);
bool vir_writes_r3(const struct v3d_device_info *devinfo, struct qinst *inst);
bool vir_writes_r4(const struct v3d_device_info *devinfo, struct qinst *inst);
struct qreg vir_follow_movs(struct v3d_compile *c, struct qreg reg);
uint8_t vir_channels_written(struct qinst *inst);
struct qreg ntq_get_src(struct v3d_compile *c, nir_src src, int i);
void ntq_store_dest(struct v3d_compile *c, nir_dest *dest, int chan,
                    struct qreg result);
void vir_emit_thrsw(struct v3d_compile *c);

void vir_dump(struct v3d_compile *c);
void vir_dump_inst(struct v3d_compile *c, struct qinst *inst);

void vir_validate(struct v3d_compile *c);

void vir_optimize(struct v3d_compile *c);
bool vir_opt_algebraic(struct v3d_compile *c);
bool vir_opt_constant_folding(struct v3d_compile *c);
bool vir_opt_copy_propagate(struct v3d_compile *c);
bool vir_opt_dead_code(struct v3d_compile *c);
bool vir_opt_peephole_sf(struct v3d_compile *c);
bool vir_opt_small_immediates(struct v3d_compile *c);
bool vir_opt_vpm(struct v3d_compile *c);
void v3d_nir_lower_blend(nir_shader *s, struct v3d_compile *c);
void v3d_nir_lower_io(nir_shader *s, struct v3d_compile *c);
void v3d_nir_lower_txf_ms(nir_shader *s, struct v3d_compile *c);
void vir_lower_uniforms(struct v3d_compile *c);

void v3d33_vir_vpm_read_setup(struct v3d_compile *c, int num_components);
void v3d33_vir_vpm_write_setup(struct v3d_compile *c);
void v3d33_vir_emit_tex(struct v3d_compile *c, nir_tex_instr *instr);
void v3d40_vir_emit_tex(struct v3d_compile *c, nir_tex_instr *instr);

void v3d_vir_to_qpu(struct v3d_compile *c, struct qpu_reg *temp_registers);
uint32_t v3d_qpu_schedule_instructions(struct v3d_compile *c);
void qpu_validate(struct v3d_compile *c);
struct qpu_reg *v3d_register_allocate(struct v3d_compile *c, bool *spilled);
bool vir_init_reg_sets(struct v3d_compiler *compiler);

void vir_PF(struct v3d_compile *c, struct qreg src, enum v3d_qpu_pf pf);

static inline bool
quniform_contents_is_texture_p0(enum quniform_contents contents)
{
        return (contents >= QUNIFORM_TEXTURE_CONFIG_P0_0 &&
                contents < (QUNIFORM_TEXTURE_CONFIG_P0_0 +
                            V3D_MAX_TEXTURE_SAMPLERS));
}

static inline struct qreg
vir_uniform_ui(struct v3d_compile *c, uint32_t ui)
{
        return vir_uniform(c, QUNIFORM_CONSTANT, ui);
}

static inline struct qreg
vir_uniform_f(struct v3d_compile *c, float f)
{
        return vir_uniform(c, QUNIFORM_CONSTANT, fui(f));
}

#define VIR_ALU0(name, vir_inst, op)                                     \
static inline struct qreg                                                \
vir_##name(struct v3d_compile *c)                                        \
{                                                                        \
        return vir_emit_def(c, vir_inst(op, c->undef,                    \
                                        c->undef, c->undef));            \
}                                                                        \
static inline struct qinst *                                             \
vir_##name##_dest(struct v3d_compile *c, struct qreg dest)               \
{                                                                        \
        return vir_emit_nondef(c, vir_inst(op, dest,                     \
                                           c->undef, c->undef));         \
}

#define VIR_ALU1(name, vir_inst, op)                                     \
static inline struct qreg                                                \
vir_##name(struct v3d_compile *c, struct qreg a)                         \
{                                                                        \
        return vir_emit_def(c, vir_inst(op, c->undef,                    \
                                        a, c->undef));                   \
}                                                                        \
static inline struct qinst *                                             \
vir_##name##_dest(struct v3d_compile *c, struct qreg dest,               \
                  struct qreg a)                                         \
{                                                                        \
        return vir_emit_nondef(c, vir_inst(op, dest, a,          \
                                           c->undef));                   \
}

#define VIR_ALU2(name, vir_inst, op)                                       \
static inline struct qreg                                                \
vir_##name(struct v3d_compile *c, struct qreg a, struct qreg b)          \
{                                                                        \
        return vir_emit_def(c, vir_inst(op, c->undef, a, b));    \
}                                                                        \
static inline struct qinst *                                             \
vir_##name##_dest(struct v3d_compile *c, struct qreg dest,               \
                  struct qreg a, struct qreg b)                          \
{                                                                        \
        return vir_emit_nondef(c, vir_inst(op, dest, a, b));     \
}

#define VIR_NODST_0(name, vir_inst, op)                                 \
static inline struct qinst *                                            \
vir_##name(struct v3d_compile *c)                                       \
{                                                                       \
        return vir_emit_nondef(c, vir_inst(op, c->undef,                \
                                           c->undef, c->undef));        \
}

#define VIR_NODST_1(name, vir_inst, op)                                               \
static inline struct qinst *                                            \
vir_##name(struct v3d_compile *c, struct qreg a)                        \
{                                                                       \
        return vir_emit_nondef(c, vir_inst(op, c->undef,        \
                                           a, c->undef));               \
}

#define VIR_NODST_2(name, vir_inst, op)                                               \
static inline struct qinst *                                            \
vir_##name(struct v3d_compile *c, struct qreg a, struct qreg b)         \
{                                                                       \
        return vir_emit_nondef(c, vir_inst(op, c->undef,                \
                                           a, b));                      \
}

#define VIR_A_ALU2(name) VIR_ALU2(name, vir_add_inst, V3D_QPU_A_##name)
#define VIR_M_ALU2(name) VIR_ALU2(name, vir_mul_inst, V3D_QPU_M_##name)
#define VIR_A_ALU1(name) VIR_ALU1(name, vir_add_inst, V3D_QPU_A_##name)
#define VIR_M_ALU1(name) VIR_ALU1(name, vir_mul_inst, V3D_QPU_M_##name)
#define VIR_A_ALU0(name) VIR_ALU0(name, vir_add_inst, V3D_QPU_A_##name)
#define VIR_M_ALU0(name) VIR_ALU0(name, vir_mul_inst, V3D_QPU_M_##name)
#define VIR_A_NODST_2(name) VIR_NODST_2(name, vir_add_inst, V3D_QPU_A_##name)
#define VIR_M_NODST_2(name) VIR_NODST_2(name, vir_mul_inst, V3D_QPU_M_##name)
#define VIR_A_NODST_1(name) VIR_NODST_1(name, vir_add_inst, V3D_QPU_A_##name)
#define VIR_M_NODST_1(name) VIR_NODST_1(name, vir_mul_inst, V3D_QPU_M_##name)
#define VIR_A_NODST_0(name) VIR_NODST_0(name, vir_add_inst, V3D_QPU_A_##name)

VIR_A_ALU2(FADD)
VIR_A_ALU2(VFPACK)
VIR_A_ALU2(FSUB)
VIR_A_ALU2(FMIN)
VIR_A_ALU2(FMAX)

VIR_A_ALU2(ADD)
VIR_A_ALU2(SUB)
VIR_A_ALU2(SHL)
VIR_A_ALU2(SHR)
VIR_A_ALU2(ASR)
VIR_A_ALU2(ROR)
VIR_A_ALU2(MIN)
VIR_A_ALU2(MAX)
VIR_A_ALU2(UMIN)
VIR_A_ALU2(UMAX)
VIR_A_ALU2(AND)
VIR_A_ALU2(OR)
VIR_A_ALU2(XOR)
VIR_A_ALU2(VADD)
VIR_A_ALU2(VSUB)
VIR_A_ALU2(STVPMV)
VIR_A_ALU1(NOT)
VIR_A_ALU1(NEG)
VIR_A_ALU1(FLAPUSH)
VIR_A_ALU1(FLBPUSH)
VIR_A_ALU1(FLBPOP)
VIR_A_ALU1(SETMSF)
VIR_A_ALU1(SETREVF)
VIR_A_ALU0(TIDX)
VIR_A_ALU0(EIDX)
VIR_A_ALU1(LDVPMV_IN)
VIR_A_ALU1(LDVPMV_OUT)

VIR_A_ALU0(FXCD)
VIR_A_ALU0(XCD)
VIR_A_ALU0(FYCD)
VIR_A_ALU0(YCD)
VIR_A_ALU0(MSF)
VIR_A_ALU0(REVF)
VIR_A_NODST_1(VPMSETUP)
VIR_A_NODST_0(VPMWT)
VIR_A_ALU2(FCMP)
VIR_A_ALU2(VFMAX)

VIR_A_ALU1(FROUND)
VIR_A_ALU1(FTOIN)
VIR_A_ALU1(FTRUNC)
VIR_A_ALU1(FTOIZ)
VIR_A_ALU1(FFLOOR)
VIR_A_ALU1(FTOUZ)
VIR_A_ALU1(FCEIL)
VIR_A_ALU1(FTOC)

VIR_A_ALU1(FDX)
VIR_A_ALU1(FDY)

VIR_A_ALU1(ITOF)
VIR_A_ALU1(CLZ)
VIR_A_ALU1(UTOF)

VIR_M_ALU2(UMUL24)
VIR_M_ALU2(FMUL)
VIR_M_ALU2(SMUL24)
VIR_M_NODST_2(MULTOP)

VIR_M_ALU1(MOV)
VIR_M_ALU1(FMOV)

static inline struct qinst *
vir_MOV_cond(struct v3d_compile *c, enum v3d_qpu_cond cond,
             struct qreg dest, struct qreg src)
{
        struct qinst *mov = vir_MOV_dest(c, dest, src);
        vir_set_cond(mov, cond);
        return mov;
}

static inline struct qreg
vir_SEL(struct v3d_compile *c, enum v3d_qpu_cond cond,
        struct qreg src0, struct qreg src1)
{
        struct qreg t = vir_get_temp(c);
        vir_MOV_dest(c, t, src1);
        vir_MOV_cond(c, cond, t, src0);
        return t;
}

static inline struct qinst *
vir_NOP(struct v3d_compile *c)
{
        return vir_emit_nondef(c, vir_add_inst(V3D_QPU_A_NOP,
                                               c->undef, c->undef, c->undef));
}

static inline struct qreg
vir_LDTMU(struct v3d_compile *c)
{
        if (c->devinfo->ver >= 41) {
                struct qinst *ldtmu = vir_add_inst(V3D_QPU_A_NOP, c->undef,
                                                   c->undef, c->undef);
                ldtmu->qpu.sig.ldtmu = true;

                return vir_emit_def(c, ldtmu);
        } else {
                vir_NOP(c)->qpu.sig.ldtmu = true;
                return vir_MOV(c, vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_R4));
        }
}

static inline struct qreg
vir_UMUL(struct v3d_compile *c, struct qreg src0, struct qreg src1)
{
        vir_MULTOP(c, src0, src1);
        return vir_UMUL24(c, src0, src1);
}

/*
static inline struct qreg
vir_LOAD_IMM(struct v3d_compile *c, uint32_t val)
{
        return vir_emit_def(c, vir_inst(QOP_LOAD_IMM, c->undef,
                                        vir_reg(QFILE_LOAD_IMM, val), c->undef));
}

static inline struct qreg
vir_LOAD_IMM_U2(struct v3d_compile *c, uint32_t val)
{
        return vir_emit_def(c, vir_inst(QOP_LOAD_IMM_U2, c->undef,
                                        vir_reg(QFILE_LOAD_IMM, val),
                                        c->undef));
}
static inline struct qreg
vir_LOAD_IMM_I2(struct v3d_compile *c, uint32_t val)
{
        return vir_emit_def(c, vir_inst(QOP_LOAD_IMM_I2, c->undef,
                                        vir_reg(QFILE_LOAD_IMM, val),
                                        c->undef));
}
*/

static inline struct qinst *
vir_BRANCH(struct v3d_compile *c, enum v3d_qpu_cond cond)
{
        /* The actual uniform_data value will be set at scheduling time */
        return vir_emit_nondef(c, vir_branch_inst(cond, vir_uniform_ui(c, 0)));
}

#define vir_for_each_block(block, c)                                    \
        list_for_each_entry(struct qblock, block, &c->blocks, link)

#define vir_for_each_block_rev(block, c)                                \
        list_for_each_entry_rev(struct qblock, block, &c->blocks, link)

/* Loop over the non-NULL members of the successors array. */
#define vir_for_each_successor(succ, block)                             \
        for (struct qblock *succ = block->successors[0];                \
             succ != NULL;                                              \
             succ = (succ == block->successors[1] ? NULL :              \
                     block->successors[1]))

#define vir_for_each_inst(inst, block)                                  \
        list_for_each_entry(struct qinst, inst, &block->instructions, link)

#define vir_for_each_inst_rev(inst, block)                                  \
        list_for_each_entry_rev(struct qinst, inst, &block->instructions, link)

#define vir_for_each_inst_safe(inst, block)                             \
        list_for_each_entry_safe(struct qinst, inst, &block->instructions, link)

#define vir_for_each_inst_inorder(inst, c)                              \
        vir_for_each_block(_block, c)                                   \
                vir_for_each_inst(inst, _block)

#endif /* V3D_COMPILER_H */
