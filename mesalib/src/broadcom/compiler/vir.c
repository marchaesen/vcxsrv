/*
 * Copyright © 2016-2017 Broadcom
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

#include "broadcom/common/v3d_device_info.h"
#include "v3d_compiler.h"

int
vir_get_nsrc(struct qinst *inst)
{
        switch (inst->qpu.type) {
        case V3D_QPU_INSTR_TYPE_BRANCH:
                return 0;
        case V3D_QPU_INSTR_TYPE_ALU:
                if (inst->qpu.alu.add.op != V3D_QPU_A_NOP)
                        return v3d_qpu_add_op_num_src(inst->qpu.alu.add.op);
                else
                        return v3d_qpu_mul_op_num_src(inst->qpu.alu.mul.op);
        }

        return 0;
}

/**
 * Returns whether the instruction has any side effects that must be
 * preserved.
 */
bool
vir_has_side_effects(struct v3d_compile *c, struct qinst *inst)
{
        switch (inst->qpu.type) {
        case V3D_QPU_INSTR_TYPE_BRANCH:
                return true;
        case V3D_QPU_INSTR_TYPE_ALU:
                switch (inst->qpu.alu.add.op) {
                case V3D_QPU_A_SETREVF:
                case V3D_QPU_A_SETMSF:
                case V3D_QPU_A_VPMSETUP:
                case V3D_QPU_A_STVPMV:
                case V3D_QPU_A_STVPMD:
                case V3D_QPU_A_STVPMP:
                case V3D_QPU_A_VPMWT:
                case V3D_QPU_A_TMUWT:
                        return true;
                default:
                        break;
                }

                switch (inst->qpu.alu.mul.op) {
                case V3D_QPU_M_MULTOP:
                        return true;
                default:
                        break;
                }
        }

        if (inst->qpu.sig.ldtmu ||
            inst->qpu.sig.ldvary ||
            inst->qpu.sig.wrtmuc ||
            inst->qpu.sig.thrsw) {
                return true;
        }

        return false;
}

bool
vir_is_raw_mov(struct qinst *inst)
{
        if (inst->qpu.type != V3D_QPU_INSTR_TYPE_ALU ||
            (inst->qpu.alu.mul.op != V3D_QPU_M_FMOV &&
             inst->qpu.alu.mul.op != V3D_QPU_M_MOV)) {
                return false;
        }

        if (inst->qpu.alu.add.output_pack != V3D_QPU_PACK_NONE ||
            inst->qpu.alu.mul.output_pack != V3D_QPU_PACK_NONE) {
                return false;
        }

        if (inst->qpu.alu.add.a_unpack != V3D_QPU_UNPACK_NONE ||
            inst->qpu.alu.add.b_unpack != V3D_QPU_UNPACK_NONE ||
            inst->qpu.alu.mul.a_unpack != V3D_QPU_UNPACK_NONE ||
            inst->qpu.alu.mul.b_unpack != V3D_QPU_UNPACK_NONE) {
                return false;
        }

        if (inst->qpu.flags.ac != V3D_QPU_COND_NONE ||
            inst->qpu.flags.mc != V3D_QPU_COND_NONE)
                return false;

        return true;
}

bool
vir_is_add(struct qinst *inst)
{
        return (inst->qpu.type == V3D_QPU_INSTR_TYPE_ALU &&
                inst->qpu.alu.add.op != V3D_QPU_A_NOP);
}

bool
vir_is_mul(struct qinst *inst)
{
        return (inst->qpu.type == V3D_QPU_INSTR_TYPE_ALU &&
                inst->qpu.alu.mul.op != V3D_QPU_M_NOP);
}

bool
vir_is_tex(struct qinst *inst)
{
        if (inst->dst.file == QFILE_MAGIC)
                return v3d_qpu_magic_waddr_is_tmu(inst->dst.index);

        if (inst->qpu.type == V3D_QPU_INSTR_TYPE_ALU &&
            inst->qpu.alu.add.op == V3D_QPU_A_TMUWT) {
                return true;
        }

        return false;
}

bool
vir_writes_r3(const struct v3d_device_info *devinfo, struct qinst *inst)
{
        for (int i = 0; i < vir_get_nsrc(inst); i++) {
                switch (inst->src[i].file) {
                case QFILE_VPM:
                        return true;
                default:
                        break;
                }
        }

        if (devinfo->ver < 41 && (inst->qpu.sig.ldvary ||
                                  inst->qpu.sig.ldtlb ||
                                  inst->qpu.sig.ldtlbu ||
                                  inst->qpu.sig.ldvpm)) {
                return true;
        }

        return false;
}

bool
vir_writes_r4(const struct v3d_device_info *devinfo, struct qinst *inst)
{
        switch (inst->dst.file) {
        case QFILE_MAGIC:
                switch (inst->dst.index) {
                case V3D_QPU_WADDR_RECIP:
                case V3D_QPU_WADDR_RSQRT:
                case V3D_QPU_WADDR_EXP:
                case V3D_QPU_WADDR_LOG:
                case V3D_QPU_WADDR_SIN:
                        return true;
                }
                break;
        default:
                break;
        }

        if (devinfo->ver < 41 && inst->qpu.sig.ldtmu)
                return true;

        return false;
}

void
vir_set_unpack(struct qinst *inst, int src,
               enum v3d_qpu_input_unpack unpack)
{
        assert(src == 0 || src == 1);

        if (vir_is_add(inst)) {
                if (src == 0)
                        inst->qpu.alu.add.a_unpack = unpack;
                else
                        inst->qpu.alu.add.b_unpack = unpack;
        } else {
                assert(vir_is_mul(inst));
                if (src == 0)
                        inst->qpu.alu.mul.a_unpack = unpack;
                else
                        inst->qpu.alu.mul.b_unpack = unpack;
        }
}

void
vir_set_cond(struct qinst *inst, enum v3d_qpu_cond cond)
{
        if (vir_is_add(inst)) {
                inst->qpu.flags.ac = cond;
        } else {
                assert(vir_is_mul(inst));
                inst->qpu.flags.mc = cond;
        }
}

void
vir_set_pf(struct qinst *inst, enum v3d_qpu_pf pf)
{
        if (vir_is_add(inst)) {
                inst->qpu.flags.apf = pf;
        } else {
                assert(vir_is_mul(inst));
                inst->qpu.flags.mpf = pf;
        }
}

void
vir_set_uf(struct qinst *inst, enum v3d_qpu_uf uf)
{
        if (vir_is_add(inst)) {
                inst->qpu.flags.auf = uf;
        } else {
                assert(vir_is_mul(inst));
                inst->qpu.flags.muf = uf;
        }
}

#if 0
uint8_t
vir_channels_written(struct qinst *inst)
{
        if (vir_is_mul(inst)) {
                switch (inst->dst.pack) {
                case QPU_PACK_MUL_NOP:
                case QPU_PACK_MUL_8888:
                        return 0xf;
                case QPU_PACK_MUL_8A:
                        return 0x1;
                case QPU_PACK_MUL_8B:
                        return 0x2;
                case QPU_PACK_MUL_8C:
                        return 0x4;
                case QPU_PACK_MUL_8D:
                        return 0x8;
                }
        } else {
                switch (inst->dst.pack) {
                case QPU_PACK_A_NOP:
                case QPU_PACK_A_8888:
                case QPU_PACK_A_8888_SAT:
                case QPU_PACK_A_32_SAT:
                        return 0xf;
                case QPU_PACK_A_8A:
                case QPU_PACK_A_8A_SAT:
                        return 0x1;
                case QPU_PACK_A_8B:
                case QPU_PACK_A_8B_SAT:
                        return 0x2;
                case QPU_PACK_A_8C:
                case QPU_PACK_A_8C_SAT:
                        return 0x4;
                case QPU_PACK_A_8D:
                case QPU_PACK_A_8D_SAT:
                        return 0x8;
                case QPU_PACK_A_16A:
                case QPU_PACK_A_16A_SAT:
                        return 0x3;
                case QPU_PACK_A_16B:
                case QPU_PACK_A_16B_SAT:
                        return 0xc;
                }
        }
        unreachable("Bad pack field");
}
#endif

struct qreg
vir_get_temp(struct v3d_compile *c)
{
        struct qreg reg;

        reg.file = QFILE_TEMP;
        reg.index = c->num_temps++;

        if (c->num_temps > c->defs_array_size) {
                uint32_t old_size = c->defs_array_size;
                c->defs_array_size = MAX2(old_size * 2, 16);

                c->defs = reralloc(c, c->defs, struct qinst *,
                                   c->defs_array_size);
                memset(&c->defs[old_size], 0,
                       sizeof(c->defs[0]) * (c->defs_array_size - old_size));

                c->spillable = reralloc(c, c->spillable,
                                        BITSET_WORD,
                                        BITSET_WORDS(c->defs_array_size));
                for (int i = old_size; i < c->defs_array_size; i++)
                        BITSET_SET(c->spillable, i);
        }

        return reg;
}

struct qinst *
vir_add_inst(enum v3d_qpu_add_op op, struct qreg dst, struct qreg src0, struct qreg src1)
{
        struct qinst *inst = calloc(1, sizeof(*inst));

        inst->qpu = v3d_qpu_nop();
        inst->qpu.alu.add.op = op;

        inst->dst = dst;
        inst->src[0] = src0;
        inst->src[1] = src1;
        inst->uniform = ~0;

        return inst;
}

struct qinst *
vir_mul_inst(enum v3d_qpu_mul_op op, struct qreg dst, struct qreg src0, struct qreg src1)
{
        struct qinst *inst = calloc(1, sizeof(*inst));

        inst->qpu = v3d_qpu_nop();
        inst->qpu.alu.mul.op = op;

        inst->dst = dst;
        inst->src[0] = src0;
        inst->src[1] = src1;
        inst->uniform = ~0;

        return inst;
}

struct qinst *
vir_branch_inst(struct v3d_compile *c, enum v3d_qpu_branch_cond cond)
{
        struct qinst *inst = calloc(1, sizeof(*inst));

        inst->qpu = v3d_qpu_nop();
        inst->qpu.type = V3D_QPU_INSTR_TYPE_BRANCH;
        inst->qpu.branch.cond = cond;
        inst->qpu.branch.msfign = V3D_QPU_MSFIGN_NONE;
        inst->qpu.branch.bdi = V3D_QPU_BRANCH_DEST_REL;
        inst->qpu.branch.ub = true;
        inst->qpu.branch.bdu = V3D_QPU_BRANCH_DEST_REL;

        inst->dst = vir_nop_reg();
        inst->uniform = vir_get_uniform_index(c, QUNIFORM_CONSTANT, 0);

        return inst;
}

static void
vir_emit(struct v3d_compile *c, struct qinst *inst)
{
        switch (c->cursor.mode) {
        case vir_cursor_add:
                list_add(&inst->link, c->cursor.link);
                break;
        case vir_cursor_addtail:
                list_addtail(&inst->link, c->cursor.link);
                break;
        }

        c->cursor = vir_after_inst(inst);
        c->live_intervals_valid = false;
}

/* Updates inst to write to a new temporary, emits it, and notes the def. */
struct qreg
vir_emit_def(struct v3d_compile *c, struct qinst *inst)
{
        assert(inst->dst.file == QFILE_NULL);

        /* If we're emitting an instruction that's a def, it had better be
         * writing a register.
         */
        if (inst->qpu.type == V3D_QPU_INSTR_TYPE_ALU) {
                assert(inst->qpu.alu.add.op == V3D_QPU_A_NOP ||
                       v3d_qpu_add_op_has_dst(inst->qpu.alu.add.op));
                assert(inst->qpu.alu.mul.op == V3D_QPU_M_NOP ||
                       v3d_qpu_mul_op_has_dst(inst->qpu.alu.mul.op));
        }

        inst->dst = vir_get_temp(c);

        if (inst->dst.file == QFILE_TEMP)
                c->defs[inst->dst.index] = inst;

        vir_emit(c, inst);

        return inst->dst;
}

struct qinst *
vir_emit_nondef(struct v3d_compile *c, struct qinst *inst)
{
        if (inst->dst.file == QFILE_TEMP)
                c->defs[inst->dst.index] = NULL;

        vir_emit(c, inst);

        return inst;
}

struct qblock *
vir_new_block(struct v3d_compile *c)
{
        struct qblock *block = rzalloc(c, struct qblock);

        list_inithead(&block->instructions);

        block->predecessors = _mesa_set_create(block,
                                               _mesa_hash_pointer,
                                               _mesa_key_pointer_equal);

        block->index = c->next_block_index++;

        return block;
}

void
vir_set_emit_block(struct v3d_compile *c, struct qblock *block)
{
        c->cur_block = block;
        c->cursor = vir_after_block(block);
        list_addtail(&block->link, &c->blocks);
}

struct qblock *
vir_entry_block(struct v3d_compile *c)
{
        return list_first_entry(&c->blocks, struct qblock, link);
}

struct qblock *
vir_exit_block(struct v3d_compile *c)
{
        return list_last_entry(&c->blocks, struct qblock, link);
}

void
vir_link_blocks(struct qblock *predecessor, struct qblock *successor)
{
        _mesa_set_add(successor->predecessors, predecessor);
        if (predecessor->successors[0]) {
                assert(!predecessor->successors[1]);
                predecessor->successors[1] = successor;
        } else {
                predecessor->successors[0] = successor;
        }
}

const struct v3d_compiler *
v3d_compiler_init(const struct v3d_device_info *devinfo)
{
        struct v3d_compiler *compiler = rzalloc(NULL, struct v3d_compiler);
        if (!compiler)
                return NULL;

        compiler->devinfo = devinfo;

        if (!vir_init_reg_sets(compiler)) {
                ralloc_free(compiler);
                return NULL;
        }

        return compiler;
}

void
v3d_compiler_free(const struct v3d_compiler *compiler)
{
        ralloc_free((void *)compiler);
}

static struct v3d_compile *
vir_compile_init(const struct v3d_compiler *compiler,
                 struct v3d_key *key,
                 nir_shader *s,
                 void (*debug_output)(const char *msg,
                                      void *debug_output_data),
                 void *debug_output_data,
                 int program_id, int variant_id)
{
        struct v3d_compile *c = rzalloc(NULL, struct v3d_compile);

        c->compiler = compiler;
        c->devinfo = compiler->devinfo;
        c->key = key;
        c->program_id = program_id;
        c->variant_id = variant_id;
        c->threads = 4;
        c->debug_output = debug_output;
        c->debug_output_data = debug_output_data;

        s = nir_shader_clone(c, s);
        c->s = s;

        list_inithead(&c->blocks);
        vir_set_emit_block(c, vir_new_block(c));

        c->output_position_index = -1;
        c->output_sample_mask_index = -1;

        c->def_ht = _mesa_hash_table_create(c, _mesa_hash_pointer,
                                            _mesa_key_pointer_equal);

        return c;
}

static int
type_size_vec4(const struct glsl_type *type, bool bindless)
{
        return glsl_count_attribute_slots(type, false);
}

static void
v3d_lower_nir(struct v3d_compile *c)
{
        struct nir_lower_tex_options tex_options = {
                .lower_txd = true,
                .lower_tg4_broadcom_swizzle = true,

                .lower_rect = false, /* XXX: Use this on V3D 3.x */
                .lower_txp = ~0,
                /* Apply swizzles to all samplers. */
                .swizzle_result = ~0,
        };

        /* Lower the format swizzle and (for 32-bit returns)
         * ARB_texture_swizzle-style swizzle.
         */
        for (int i = 0; i < ARRAY_SIZE(c->key->tex); i++) {
                for (int j = 0; j < 4; j++)
                        tex_options.swizzles[i][j] = c->key->tex[i].swizzle[j];

                if (c->key->tex[i].clamp_s)
                        tex_options.saturate_s |= 1 << i;
                if (c->key->tex[i].clamp_t)
                        tex_options.saturate_t |= 1 << i;
                if (c->key->tex[i].clamp_r)
                        tex_options.saturate_r |= 1 << i;
                if (c->key->tex[i].return_size == 16) {
                        tex_options.lower_tex_packing[i] =
                                nir_lower_tex_packing_16;
                }
        }

        /* CS textures may not have return_size reflecting the shadow state. */
        nir_foreach_variable(var, &c->s->uniforms) {
                const struct glsl_type *type = glsl_without_array(var->type);
                unsigned array_len = MAX2(glsl_get_length(var->type), 1);

                if (!glsl_type_is_sampler(type) ||
                    !glsl_sampler_type_is_shadow(type))
                        continue;

                for (int i = 0; i < array_len; i++) {
                        tex_options.lower_tex_packing[var->data.binding + i] =
                                nir_lower_tex_packing_16;
                }
        }

        NIR_PASS_V(c->s, nir_lower_tex, &tex_options);
        NIR_PASS_V(c->s, nir_lower_system_values);

        NIR_PASS_V(c->s, nir_lower_vars_to_scratch,
                   nir_var_function_temp,
                   0,
                   glsl_get_natural_size_align_bytes);
        NIR_PASS_V(c->s, v3d_nir_lower_scratch);
}

static void
v3d_set_prog_data_uniforms(struct v3d_compile *c,
                           struct v3d_prog_data *prog_data)
{
        int count = c->num_uniforms;
        struct v3d_uniform_list *ulist = &prog_data->uniforms;

        ulist->count = count;
        ulist->data = ralloc_array(prog_data, uint32_t, count);
        memcpy(ulist->data, c->uniform_data,
               count * sizeof(*ulist->data));
        ulist->contents = ralloc_array(prog_data, enum quniform_contents, count);
        memcpy(ulist->contents, c->uniform_contents,
               count * sizeof(*ulist->contents));
}

static void
v3d_vs_set_prog_data(struct v3d_compile *c,
                     struct v3d_vs_prog_data *prog_data)
{
        /* The vertex data gets format converted by the VPM so that
         * each attribute channel takes up a VPM column.  Precompute
         * the sizes for the shader record.
         */
        for (int i = 0; i < ARRAY_SIZE(prog_data->vattr_sizes); i++) {
                prog_data->vattr_sizes[i] = c->vattr_sizes[i];
                prog_data->vpm_input_size += c->vattr_sizes[i];
        }

        prog_data->uses_vid = (c->s->info.system_values_read &
                               (1ull << SYSTEM_VALUE_VERTEX_ID));
        prog_data->uses_iid = (c->s->info.system_values_read &
                               (1ull << SYSTEM_VALUE_INSTANCE_ID));

        if (prog_data->uses_vid)
                prog_data->vpm_input_size++;
        if (prog_data->uses_iid)
                prog_data->vpm_input_size++;

        /* Input/output segment size are in sectors (8 rows of 32 bits per
         * channel).
         */
        prog_data->vpm_input_size = align(prog_data->vpm_input_size, 8) / 8;
        prog_data->vpm_output_size = align(c->vpm_output_size, 8) / 8;

        /* Set us up for shared input/output segments.  This is apparently
         * necessary for our VCM setup to avoid varying corruption.
         */
        prog_data->separate_segments = false;
        prog_data->vpm_output_size = MAX2(prog_data->vpm_output_size,
                                          prog_data->vpm_input_size);
        prog_data->vpm_input_size = 0;

        /* Compute VCM cache size.  We set up our program to take up less than
         * half of the VPM, so that any set of bin and render programs won't
         * run out of space.  We need space for at least one input segment,
         * and then allocate the rest to output segments (one for the current
         * program, the rest to VCM).  The valid range of the VCM cache size
         * field is 1-4 16-vertex batches, but GFXH-1744 limits us to 2-4
         * batches.
         */
        assert(c->devinfo->vpm_size);
        int sector_size = V3D_CHANNELS * sizeof(uint32_t) * 8;
        int vpm_size_in_sectors = c->devinfo->vpm_size / sector_size;
        int half_vpm = vpm_size_in_sectors / 2;
        int vpm_output_sectors = half_vpm - prog_data->vpm_input_size;
        int vpm_output_batches = vpm_output_sectors / prog_data->vpm_output_size;
        assert(vpm_output_batches >= 2);
        prog_data->vcm_cache_size = CLAMP(vpm_output_batches - 1, 2, 4);
}

static void
v3d_set_fs_prog_data_inputs(struct v3d_compile *c,
                            struct v3d_fs_prog_data *prog_data)
{
        prog_data->num_inputs = c->num_inputs;
        memcpy(prog_data->input_slots, c->input_slots,
               c->num_inputs * sizeof(*c->input_slots));

        STATIC_ASSERT(ARRAY_SIZE(prog_data->flat_shade_flags) >
                      (V3D_MAX_FS_INPUTS - 1) / 24);
        for (int i = 0; i < V3D_MAX_FS_INPUTS; i++) {
                if (BITSET_TEST(c->flat_shade_flags, i))
                        prog_data->flat_shade_flags[i / 24] |= 1 << (i % 24);

                if (BITSET_TEST(c->noperspective_flags, i))
                        prog_data->noperspective_flags[i / 24] |= 1 << (i % 24);

                if (BITSET_TEST(c->centroid_flags, i))
                        prog_data->centroid_flags[i / 24] |= 1 << (i % 24);
        }
}

static void
v3d_fs_set_prog_data(struct v3d_compile *c,
                     struct v3d_fs_prog_data *prog_data)
{
        v3d_set_fs_prog_data_inputs(c, prog_data);
        prog_data->writes_z = c->writes_z;
        prog_data->disable_ez = !c->s->info.fs.early_fragment_tests;
        prog_data->uses_center_w = c->uses_center_w;
}

static void
v3d_cs_set_prog_data(struct v3d_compile *c,
                     struct v3d_compute_prog_data *prog_data)
{
        prog_data->shared_size = c->s->info.cs.shared_size;
}

static void
v3d_set_prog_data(struct v3d_compile *c,
                  struct v3d_prog_data *prog_data)
{
        prog_data->threads = c->threads;
        prog_data->single_seg = !c->last_thrsw;
        prog_data->spill_size = c->spill_size;

        v3d_set_prog_data_uniforms(c, prog_data);

        if (c->s->info.stage == MESA_SHADER_COMPUTE) {
                v3d_cs_set_prog_data(c, (struct v3d_compute_prog_data *)prog_data);
        } else if (c->s->info.stage == MESA_SHADER_VERTEX) {
                v3d_vs_set_prog_data(c, (struct v3d_vs_prog_data *)prog_data);
        } else {
                assert(c->s->info.stage == MESA_SHADER_FRAGMENT);
                v3d_fs_set_prog_data(c, (struct v3d_fs_prog_data *)prog_data);
        }
}

static uint64_t *
v3d_return_qpu_insts(struct v3d_compile *c, uint32_t *final_assembly_size)
{
        *final_assembly_size = c->qpu_inst_count * sizeof(uint64_t);

        uint64_t *qpu_insts = malloc(*final_assembly_size);
        if (!qpu_insts)
                return NULL;

        memcpy(qpu_insts, c->qpu_insts, *final_assembly_size);

        vir_compile_destroy(c);

        return qpu_insts;
}

static void
v3d_nir_lower_vs_early(struct v3d_compile *c)
{
        /* Split our I/O vars and dead code eliminate the unused
         * components.
         */
        NIR_PASS_V(c->s, nir_lower_io_to_scalar_early,
                   nir_var_shader_in | nir_var_shader_out);
        uint64_t used_outputs[4] = {0};
        for (int i = 0; i < c->vs_key->num_fs_inputs; i++) {
                int slot = v3d_slot_get_slot(c->vs_key->fs_inputs[i]);
                int comp = v3d_slot_get_component(c->vs_key->fs_inputs[i]);
                used_outputs[comp] |= 1ull << slot;
        }
        NIR_PASS_V(c->s, nir_remove_unused_io_vars,
                   &c->s->outputs, used_outputs, NULL); /* demotes to globals */
        NIR_PASS_V(c->s, nir_lower_global_vars_to_local);
        v3d_optimize_nir(c->s);
        NIR_PASS_V(c->s, nir_remove_dead_variables, nir_var_shader_in);
        NIR_PASS_V(c->s, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
                   type_size_vec4,
                   (nir_lower_io_options)0);
}

static void
v3d_fixup_fs_output_types(struct v3d_compile *c)
{
        nir_foreach_variable(var, &c->s->outputs) {
                uint32_t mask = 0;

                switch (var->data.location) {
                case FRAG_RESULT_COLOR:
                        mask = ~0;
                        break;
                case FRAG_RESULT_DATA0:
                case FRAG_RESULT_DATA1:
                case FRAG_RESULT_DATA2:
                case FRAG_RESULT_DATA3:
                        mask = 1 << (var->data.location - FRAG_RESULT_DATA0);
                        break;
                }

                if (c->fs_key->int_color_rb & mask) {
                        var->type =
                                glsl_vector_type(GLSL_TYPE_INT,
                                                 glsl_get_components(var->type));
                } else if (c->fs_key->uint_color_rb & mask) {
                        var->type =
                                glsl_vector_type(GLSL_TYPE_UINT,
                                                 glsl_get_components(var->type));
                }
        }
}

static void
v3d_nir_lower_fs_early(struct v3d_compile *c)
{
        if (c->fs_key->int_color_rb || c->fs_key->uint_color_rb)
                v3d_fixup_fs_output_types(c);

        /* If the shader has no non-TLB side effects, we can promote it to
         * enabling early_fragment_tests even if the user didn't.
         */
        if (!(c->s->info.num_images ||
              c->s->info.num_ssbos ||
              c->s->info.num_abos)) {
                c->s->info.fs.early_fragment_tests = true;
        }
}

static void
v3d_nir_lower_vs_late(struct v3d_compile *c)
{
        if (c->vs_key->clamp_color)
                NIR_PASS_V(c->s, nir_lower_clamp_color_outputs);

        if (c->key->ucp_enables) {
                NIR_PASS_V(c->s, nir_lower_clip_vs, c->key->ucp_enables,
                           false);
                NIR_PASS_V(c->s, nir_lower_io_to_scalar,
                           nir_var_shader_out);
        }

        /* Note: VS output scalarizing must happen after nir_lower_clip_vs. */
        NIR_PASS_V(c->s, nir_lower_io_to_scalar, nir_var_shader_out);
}

static void
v3d_nir_lower_fs_late(struct v3d_compile *c)
{
        if (c->fs_key->light_twoside)
                NIR_PASS_V(c->s, nir_lower_two_sided_color);

        if (c->fs_key->clamp_color)
                NIR_PASS_V(c->s, nir_lower_clamp_color_outputs);

        if (c->fs_key->alpha_test) {
                NIR_PASS_V(c->s, nir_lower_alpha_test,
                           c->fs_key->alpha_test_func,
                           false);
        }

        if (c->key->ucp_enables)
                NIR_PASS_V(c->s, nir_lower_clip_fs, c->key->ucp_enables);

        /* Note: FS input scalarizing must happen after
         * nir_lower_two_sided_color, which only handles a vec4 at a time.
         */
        NIR_PASS_V(c->s, nir_lower_io_to_scalar, nir_var_shader_in);
}

static uint32_t
vir_get_max_temps(struct v3d_compile *c)
{
        int max_ip = 0;
        vir_for_each_inst_inorder(inst, c)
                max_ip++;

        uint32_t *pressure = rzalloc_array(NULL, uint32_t, max_ip);

        for (int t = 0; t < c->num_temps; t++) {
                for (int i = c->temp_start[t]; (i < c->temp_end[t] &&
                                                i < max_ip); i++) {
                        if (i > max_ip)
                                break;
                        pressure[i]++;
                }
        }

        uint32_t max_temps = 0;
        for (int i = 0; i < max_ip; i++)
                max_temps = MAX2(max_temps, pressure[i]);

        ralloc_free(pressure);

        return max_temps;
}

uint64_t *v3d_compile(const struct v3d_compiler *compiler,
                      struct v3d_key *key,
                      struct v3d_prog_data **out_prog_data,
                      nir_shader *s,
                      void (*debug_output)(const char *msg,
                                           void *debug_output_data),
                      void *debug_output_data,
                      int program_id, int variant_id,
                      uint32_t *final_assembly_size)
{
        struct v3d_prog_data *prog_data;
        struct v3d_compile *c = vir_compile_init(compiler, key, s,
                                                 debug_output, debug_output_data,
                                                 program_id, variant_id);

        switch (c->s->info.stage) {
        case MESA_SHADER_VERTEX:
                c->vs_key = (struct v3d_vs_key *)key;
                prog_data = rzalloc_size(NULL, sizeof(struct v3d_vs_prog_data));
                break;
        case MESA_SHADER_FRAGMENT:
                c->fs_key = (struct v3d_fs_key *)key;
                prog_data = rzalloc_size(NULL, sizeof(struct v3d_fs_prog_data));
                break;
        case MESA_SHADER_COMPUTE:
                prog_data = rzalloc_size(NULL,
                                         sizeof(struct v3d_compute_prog_data));
                break;
        default:
                unreachable("unsupported shader stage");
        }

        if (c->s->info.stage == MESA_SHADER_VERTEX) {
                v3d_nir_lower_vs_early(c);
        } else if (c->s->info.stage != MESA_SHADER_COMPUTE) {
                assert(c->s->info.stage == MESA_SHADER_FRAGMENT);
                v3d_nir_lower_fs_early(c);
        }

        v3d_lower_nir(c);

        if (c->s->info.stage == MESA_SHADER_VERTEX) {
                v3d_nir_lower_vs_late(c);
        } else if (c->s->info.stage != MESA_SHADER_COMPUTE)  {
                assert(c->s->info.stage == MESA_SHADER_FRAGMENT);
                v3d_nir_lower_fs_late(c);
        }

        NIR_PASS_V(c->s, v3d_nir_lower_io, c);
        NIR_PASS_V(c->s, v3d_nir_lower_txf_ms, c);
        NIR_PASS_V(c->s, v3d_nir_lower_image_load_store);
        NIR_PASS_V(c->s, nir_lower_idiv);

        v3d_optimize_nir(c->s);
        NIR_PASS_V(c->s, nir_lower_bool_to_int32);
        NIR_PASS_V(c->s, nir_convert_from_ssa, true);

        v3d_nir_to_vir(c);

        v3d_set_prog_data(c, prog_data);

        *out_prog_data = prog_data;

        char *shaderdb;
        int ret = asprintf(&shaderdb,
                           "%s shader: %d inst, %d threads, %d loops, "
                           "%d uniforms, %d max-temps, %d:%d spills:fills",
                           vir_get_stage_name(c),
                           c->qpu_inst_count,
                           c->threads,
                           c->loops,
                           c->num_uniforms,
                           vir_get_max_temps(c),
                           c->spills,
                           c->fills);
        if (ret >= 0) {
                if (V3D_DEBUG & V3D_DEBUG_SHADERDB)
                        fprintf(stderr, "SHADER-DB: %s\n", shaderdb);

                c->debug_output(shaderdb, c->debug_output_data);
                free(shaderdb);
        }

       return v3d_return_qpu_insts(c, final_assembly_size);
}

void
vir_remove_instruction(struct v3d_compile *c, struct qinst *qinst)
{
        if (qinst->dst.file == QFILE_TEMP)
                c->defs[qinst->dst.index] = NULL;

        assert(&qinst->link != c->cursor.link);

        list_del(&qinst->link);
        free(qinst);

        c->live_intervals_valid = false;
}

struct qreg
vir_follow_movs(struct v3d_compile *c, struct qreg reg)
{
        /* XXX
        int pack = reg.pack;

        while (reg.file == QFILE_TEMP &&
               c->defs[reg.index] &&
               (c->defs[reg.index]->op == QOP_MOV ||
                c->defs[reg.index]->op == QOP_FMOV) &&
               !c->defs[reg.index]->dst.pack &&
               !c->defs[reg.index]->src[0].pack) {
                reg = c->defs[reg.index]->src[0];
        }

        reg.pack = pack;
        */
        return reg;
}

void
vir_compile_destroy(struct v3d_compile *c)
{
        /* Defuse the assert that we aren't removing the cursor's instruction.
         */
        c->cursor.link = NULL;

        vir_for_each_block(block, c) {
                while (!list_empty(&block->instructions)) {
                        struct qinst *qinst =
                                list_first_entry(&block->instructions,
                                                 struct qinst, link);
                        vir_remove_instruction(c, qinst);
                }
        }

        ralloc_free(c);
}

uint32_t
vir_get_uniform_index(struct v3d_compile *c,
                      enum quniform_contents contents,
                      uint32_t data)
{
        for (int i = 0; i < c->num_uniforms; i++) {
                if (c->uniform_contents[i] == contents &&
                    c->uniform_data[i] == data) {
                        return i;
                }
        }

        uint32_t uniform = c->num_uniforms++;

        if (uniform >= c->uniform_array_size) {
                c->uniform_array_size = MAX2(MAX2(16, uniform + 1),
                                             c->uniform_array_size * 2);

                c->uniform_data = reralloc(c, c->uniform_data,
                                           uint32_t,
                                           c->uniform_array_size);
                c->uniform_contents = reralloc(c, c->uniform_contents,
                                               enum quniform_contents,
                                               c->uniform_array_size);
        }

        c->uniform_contents[uniform] = contents;
        c->uniform_data[uniform] = data;

        return uniform;
}

struct qreg
vir_uniform(struct v3d_compile *c,
            enum quniform_contents contents,
            uint32_t data)
{
        struct qinst *inst = vir_NOP(c);
        inst->qpu.sig.ldunif = true;
        inst->uniform = vir_get_uniform_index(c, contents, data);
        inst->dst = vir_get_temp(c);
        c->defs[inst->dst.index] = inst;
        return inst->dst;
}

#define OPTPASS(func)                                                   \
        do {                                                            \
                bool stage_progress = func(c);                          \
                if (stage_progress) {                                   \
                        progress = true;                                \
                        if (print_opt_debug) {                          \
                                fprintf(stderr,                         \
                                        "VIR opt pass %2d: %s progress\n", \
                                        pass, #func);                   \
                        }                                               \
                        /*XXX vir_validate(c);*/                        \
                }                                                       \
        } while (0)

void
vir_optimize(struct v3d_compile *c)
{
        bool print_opt_debug = false;
        int pass = 1;

        while (true) {
                bool progress = false;

                OPTPASS(vir_opt_copy_propagate);
                OPTPASS(vir_opt_redundant_flags);
                OPTPASS(vir_opt_dead_code);
                OPTPASS(vir_opt_small_immediates);

                if (!progress)
                        break;

                pass++;
        }
}

const char *
vir_get_stage_name(struct v3d_compile *c)
{
        if (c->vs_key && c->vs_key->is_coord)
                return "MESA_SHADER_COORD";
        else
                return gl_shader_stage_name(c->s->info.stage);
}
