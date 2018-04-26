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
vir_get_non_sideband_nsrc(struct qinst *inst)
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

int
vir_get_nsrc(struct qinst *inst)
{
        int nsrc = vir_get_non_sideband_nsrc(inst);

        if (vir_has_implicit_uniform(inst))
                nsrc++;

        return nsrc;
}

bool
vir_has_implicit_uniform(struct qinst *inst)
{
        switch (inst->qpu.type) {
        case V3D_QPU_INSTR_TYPE_BRANCH:
                return true;
        case V3D_QPU_INSTR_TYPE_ALU:
                switch (inst->dst.file) {
                case QFILE_TLBU:
                        return true;
                default:
                        return inst->has_implicit_uniform;
                }
        }
        return false;
}

/* The sideband uniform for textures gets stored after the normal ALU
 * arguments.
 */
int
vir_get_implicit_uniform_src(struct qinst *inst)
{
        return vir_get_nsrc(inst) - 1;
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
vir_is_float_input(struct qinst *inst)
{
        /* XXX: More instrs */
        switch (inst->qpu.type) {
        case V3D_QPU_INSTR_TYPE_BRANCH:
                return false;
        case V3D_QPU_INSTR_TYPE_ALU:
                switch (inst->qpu.alu.add.op) {
                case V3D_QPU_A_FADD:
                case V3D_QPU_A_FSUB:
                case V3D_QPU_A_FMIN:
                case V3D_QPU_A_FMAX:
                case V3D_QPU_A_FTOIN:
                        return true;
                default:
                        break;
                }

                switch (inst->qpu.alu.mul.op) {
                case V3D_QPU_M_FMOV:
                case V3D_QPU_M_VFMUL:
                case V3D_QPU_M_FMUL:
                        return true;
                default:
                        break;
                }
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

        return false;
}

bool
vir_depends_on_flags(struct qinst *inst)
{
        if (inst->qpu.type == V3D_QPU_INSTR_TYPE_BRANCH) {
                return (inst->qpu.branch.cond != V3D_QPU_BRANCH_COND_ALWAYS);
        } else {
                return (inst->qpu.flags.ac != V3D_QPU_COND_NONE &&
                        inst->qpu.flags.mc != V3D_QPU_COND_NONE);
        }
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
vir_branch_inst(enum v3d_qpu_branch_cond cond, struct qreg src)
{
        struct qinst *inst = calloc(1, sizeof(*inst));

        inst->qpu = v3d_qpu_nop();
        inst->qpu.type = V3D_QPU_INSTR_TYPE_BRANCH;
        inst->qpu.branch.cond = cond;
        inst->qpu.branch.msfign = V3D_QPU_MSFIGN_NONE;
        inst->qpu.branch.bdi = V3D_QPU_BRANCH_DEST_REL;
        inst->qpu.branch.ub = true;
        inst->qpu.branch.bdu = V3D_QPU_BRANCH_DEST_REL;

        inst->dst = vir_reg(QFILE_NULL, 0);
        inst->src[0] = src;
        inst->uniform = ~0;

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
                 int program_id, int variant_id)
{
        struct v3d_compile *c = rzalloc(NULL, struct v3d_compile);

        c->compiler = compiler;
        c->devinfo = compiler->devinfo;
        c->key = key;
        c->program_id = program_id;
        c->variant_id = variant_id;
        c->threads = 4;

        s = nir_shader_clone(c, s);
        c->s = s;

        list_inithead(&c->blocks);
        vir_set_emit_block(c, vir_new_block(c));

        c->output_position_index = -1;
        c->output_point_size_index = -1;
        c->output_sample_mask_index = -1;

        c->def_ht = _mesa_hash_table_create(c, _mesa_hash_pointer,
                                            _mesa_key_pointer_equal);

        return c;
}

static void
v3d_lower_nir(struct v3d_compile *c)
{
        struct nir_lower_tex_options tex_options = {
                .lower_txd = true,
                .lower_rect = false, /* XXX */
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
        }

        NIR_PASS_V(c->s, nir_lower_tex, &tex_options);
}

static void
v3d_lower_nir_late(struct v3d_compile *c)
{
        NIR_PASS_V(c->s, v3d_nir_lower_io, c);
        NIR_PASS_V(c->s, v3d_nir_lower_txf_ms, c);
        NIR_PASS_V(c->s, nir_lower_idiv);
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

/* Copy the compiler UBO range state to the compiled shader, dropping out
 * arrays that were never referenced by an indirect load.
 *
 * (Note that QIR dead code elimination of an array access still leaves that
 * array alive, though)
 */
static void
v3d_set_prog_data_ubo(struct v3d_compile *c,
                      struct v3d_prog_data *prog_data)
{
        if (!c->num_ubo_ranges)
                return;

        prog_data->num_ubo_ranges = 0;
        prog_data->ubo_ranges = ralloc_array(prog_data, struct v3d_ubo_range,
                                             c->num_ubo_ranges);
        for (int i = 0; i < c->num_ubo_ranges; i++) {
                if (!c->ubo_range_used[i])
                        continue;

                struct v3d_ubo_range *range = &c->ubo_ranges[i];
                prog_data->ubo_ranges[prog_data->num_ubo_ranges++] = *range;
                prog_data->ubo_size += range->size;
        }

        if (prog_data->ubo_size) {
                if (V3D_DEBUG & V3D_DEBUG_SHADERDB) {
                        fprintf(stderr, "SHADER-DB: %s prog %d/%d: %d UBO uniforms\n",
                                vir_get_stage_name(c),
                                c->program_id, c->variant_id,
                                prog_data->ubo_size / 4);
                }
        }
}

static void
v3d_set_prog_data(struct v3d_compile *c,
                  struct v3d_prog_data *prog_data)
{
        prog_data->threads = c->threads;
        prog_data->single_seg = !c->last_thrsw;
        prog_data->spill_size = c->spill_size;

        v3d_set_prog_data_uniforms(c, prog_data);
        v3d_set_prog_data_ubo(c, prog_data);
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

uint64_t *v3d_compile_vs(const struct v3d_compiler *compiler,
                         struct v3d_vs_key *key,
                         struct v3d_vs_prog_data *prog_data,
                         nir_shader *s,
                         int program_id, int variant_id,
                         uint32_t *final_assembly_size)
{
        struct v3d_compile *c = vir_compile_init(compiler, &key->base, s,
                                                 program_id, variant_id);

        c->vs_key = key;

        v3d_lower_nir(c);

        if (key->clamp_color)
                NIR_PASS_V(c->s, nir_lower_clamp_color_outputs);

        if (key->base.ucp_enables) {
                NIR_PASS_V(c->s, nir_lower_clip_vs, key->base.ucp_enables);
                NIR_PASS_V(c->s, nir_lower_io_to_scalar,
                           nir_var_shader_out);
        }

        /* Note: VS output scalarizing must happen after nir_lower_clip_vs. */
        NIR_PASS_V(c->s, nir_lower_io_to_scalar, nir_var_shader_out);

        v3d_lower_nir_late(c);
        v3d_optimize_nir(c->s);
        NIR_PASS_V(c->s, nir_convert_from_ssa, true);

        v3d_nir_to_vir(c);

        v3d_set_prog_data(c, &prog_data->base);

        prog_data->base.num_inputs = c->num_inputs;

        /* The vertex data gets format converted by the VPM so that
         * each attribute channel takes up a VPM column.  Precompute
         * the sizes for the shader record.
         */
        for (int i = 0; i < ARRAY_SIZE(prog_data->vattr_sizes); i++) {
                prog_data->vattr_sizes[i] = c->vattr_sizes[i];
                prog_data->vpm_input_size += c->vattr_sizes[i];
        }

        prog_data->uses_vid = (s->info.system_values_read &
                               (1ull << SYSTEM_VALUE_VERTEX_ID));
        prog_data->uses_iid = (s->info.system_values_read &
                               (1ull << SYSTEM_VALUE_INSTANCE_ID));

        if (prog_data->uses_vid)
                prog_data->vpm_input_size++;
        if (prog_data->uses_iid)
                prog_data->vpm_input_size++;

        /* Input/output segment size are in 8x32-bit multiples. */
        prog_data->vpm_input_size = align(prog_data->vpm_input_size, 8) / 8;
        prog_data->vpm_output_size = align(c->num_vpm_writes, 8) / 8;

        return v3d_return_qpu_insts(c, final_assembly_size);
}

static void
v3d_set_fs_prog_data_inputs(struct v3d_compile *c,
                            struct v3d_fs_prog_data *prog_data)
{
        prog_data->base.num_inputs = c->num_inputs;
        memcpy(prog_data->input_slots, c->input_slots,
               c->num_inputs * sizeof(*c->input_slots));

        STATIC_ASSERT(ARRAY_SIZE(prog_data->flat_shade_flags) >
                      (V3D_MAX_FS_INPUTS - 1) / 24);
        for (int i = 0; i < V3D_MAX_FS_INPUTS; i++) {
                if (BITSET_TEST(c->flat_shade_flags, i))
                        prog_data->flat_shade_flags[i / 24] |= 1 << (i % 24);

                if (BITSET_TEST(c->centroid_flags, i))
                        prog_data->centroid_flags[i / 24] |= 1 << (i % 24);
        }
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

uint64_t *v3d_compile_fs(const struct v3d_compiler *compiler,
                         struct v3d_fs_key *key,
                         struct v3d_fs_prog_data *prog_data,
                         nir_shader *s,
                         int program_id, int variant_id,
                         uint32_t *final_assembly_size)
{
        struct v3d_compile *c = vir_compile_init(compiler, &key->base, s,
                                                 program_id, variant_id);

        c->fs_key = key;

        if (key->int_color_rb || key->uint_color_rb)
                v3d_fixup_fs_output_types(c);

        v3d_lower_nir(c);

        if (key->light_twoside)
                NIR_PASS_V(c->s, nir_lower_two_sided_color);

        if (key->clamp_color)
                NIR_PASS_V(c->s, nir_lower_clamp_color_outputs);

        if (key->alpha_test) {
                NIR_PASS_V(c->s, nir_lower_alpha_test, key->alpha_test_func,
                           false);
        }

        if (key->base.ucp_enables)
                NIR_PASS_V(c->s, nir_lower_clip_fs, key->base.ucp_enables);

        /* Note: FS input scalarizing must happen after
         * nir_lower_two_sided_color, which only handles a vec4 at a time.
         */
        NIR_PASS_V(c->s, nir_lower_io_to_scalar, nir_var_shader_in);

        v3d_lower_nir_late(c);
        v3d_optimize_nir(c->s);
        NIR_PASS_V(c->s, nir_convert_from_ssa, true);

        v3d_nir_to_vir(c);

        v3d_set_prog_data(c, &prog_data->base);
        v3d_set_fs_prog_data_inputs(c, prog_data);
        prog_data->writes_z = (c->s->info.outputs_written &
                               (1 << FRAG_RESULT_DEPTH));
        prog_data->discard = c->s->info.fs.uses_discard;
        prog_data->uses_centroid_and_center_w = c->uses_centroid_and_center_w;

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

struct qreg
vir_uniform(struct v3d_compile *c,
            enum quniform_contents contents,
            uint32_t data)
{
        for (int i = 0; i < c->num_uniforms; i++) {
                if (c->uniform_contents[i] == contents &&
                    c->uniform_data[i] == data) {
                        return vir_reg(QFILE_UNIF, i);
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

        return vir_reg(QFILE_UNIF, uniform);
}

void
vir_PF(struct v3d_compile *c, struct qreg src, enum v3d_qpu_pf pf)
{
        struct qinst *last_inst = NULL;

        if (!list_empty(&c->cur_block->instructions)) {
                last_inst = (struct qinst *)c->cur_block->instructions.prev;

                /* Can't stuff the PF into the last last inst if our cursor
                 * isn't pointing after it.
                 */
                struct vir_cursor after_inst = vir_after_inst(last_inst);
                if (c->cursor.mode != after_inst.mode ||
                    c->cursor.link != after_inst.link)
                        last_inst = NULL;
        }

        if (src.file != QFILE_TEMP ||
            !c->defs[src.index] ||
            last_inst != c->defs[src.index]) {
                /* XXX: Make the MOV be the appropriate type */
                last_inst = vir_MOV_dest(c, vir_reg(QFILE_NULL, 0), src);
        }

        vir_set_pf(last_inst, pf);
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
                OPTPASS(vir_opt_dead_code);

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
