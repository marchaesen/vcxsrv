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

#include <inttypes.h>
#include "util/u_format.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/ralloc.h"
#include "util/hash_table.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "common/v3d_device_info.h"
#include "v3d_compiler.h"

#define GENERAL_TMU_LOOKUP_PER_QUAD                 (0 << 7)
#define GENERAL_TMU_LOOKUP_PER_PIXEL                (1 << 7)
#define GENERAL_TMU_READ_OP_PREFETCH                (0 << 3)
#define GENERAL_TMU_READ_OP_CACHE_CLEAR             (1 << 3)
#define GENERAL_TMU_READ_OP_CACHE_FLUSH             (3 << 3)
#define GENERAL_TMU_READ_OP_CACHE_CLEAN             (3 << 3)
#define GENERAL_TMU_READ_OP_CACHE_L1T_CLEAR         (4 << 3)
#define GENERAL_TMU_READ_OP_CACHE_L1T_FLUSH_AGGREGATION (5 << 3)
#define GENERAL_TMU_READ_OP_ATOMIC_INC              (8 << 3)
#define GENERAL_TMU_READ_OP_ATOMIC_DEC              (9 << 3)
#define GENERAL_TMU_READ_OP_ATOMIC_NOT              (10 << 3)
#define GENERAL_TMU_READ_OP_READ                    (15 << 3)
#define GENERAL_TMU_LOOKUP_TYPE_8BIT_I              (0 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_16BIT_I             (1 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_VEC2                (2 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_VEC3                (3 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_VEC4                (4 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_8BIT_UI             (5 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_16BIT_UI            (6 << 0)
#define GENERAL_TMU_LOOKUP_TYPE_32BIT_UI            (7 << 0)

#define GENERAL_TMU_WRITE_OP_ATOMIC_ADD_WRAP         (0 << 3)
#define GENERAL_TMU_WRITE_OP_ATOMIC_SUB_WRAP         (1 << 3)
#define GENERAL_TMU_WRITE_OP_ATOMIC_XCHG             (2 << 3)
#define GENERAL_TMU_WRITE_OP_ATOMIC_CMPXCHG          (3 << 3)
#define GENERAL_TMU_WRITE_OP_ATOMIC_UMIN             (4 << 3)
#define GENERAL_TMU_WRITE_OP_ATOMIC_UMAX             (5 << 3)
#define GENERAL_TMU_WRITE_OP_ATOMIC_SMIN             (6 << 3)
#define GENERAL_TMU_WRITE_OP_ATOMIC_SMAX             (7 << 3)
#define GENERAL_TMU_WRITE_OP_ATOMIC_AND              (8 << 3)
#define GENERAL_TMU_WRITE_OP_ATOMIC_OR               (9 << 3)
#define GENERAL_TMU_WRITE_OP_ATOMIC_XOR              (10 << 3)
#define GENERAL_TMU_WRITE_OP_WRITE                   (15 << 3)

#define V3D_TSY_SET_QUORUM          0
#define V3D_TSY_INC_WAITERS         1
#define V3D_TSY_DEC_WAITERS         2
#define V3D_TSY_INC_QUORUM          3
#define V3D_TSY_DEC_QUORUM          4
#define V3D_TSY_FREE_ALL            5
#define V3D_TSY_RELEASE             6
#define V3D_TSY_ACQUIRE             7
#define V3D_TSY_WAIT                8
#define V3D_TSY_WAIT_INC            9
#define V3D_TSY_WAIT_CHECK          10
#define V3D_TSY_WAIT_INC_CHECK      11
#define V3D_TSY_WAIT_CV             12
#define V3D_TSY_INC_SEMAPHORE       13
#define V3D_TSY_DEC_SEMAPHORE       14
#define V3D_TSY_SET_QUORUM_FREE_ALL 15

static void
ntq_emit_cf_list(struct v3d_compile *c, struct exec_list *list);

static void
resize_qreg_array(struct v3d_compile *c,
                  struct qreg **regs,
                  uint32_t *size,
                  uint32_t decl_size)
{
        if (*size >= decl_size)
                return;

        uint32_t old_size = *size;
        *size = MAX2(*size * 2, decl_size);
        *regs = reralloc(c, *regs, struct qreg, *size);
        if (!*regs) {
                fprintf(stderr, "Malloc failure\n");
                abort();
        }

        for (uint32_t i = old_size; i < *size; i++)
                (*regs)[i] = c->undef;
}

void
vir_emit_thrsw(struct v3d_compile *c)
{
        if (c->threads == 1)
                return;

        /* Always thread switch after each texture operation for now.
         *
         * We could do better by batching a bunch of texture fetches up and
         * then doing one thread switch and collecting all their results
         * afterward.
         */
        c->last_thrsw = vir_NOP(c);
        c->last_thrsw->qpu.sig.thrsw = true;
        c->last_thrsw_at_top_level = (c->execute.file == QFILE_NULL);
}

static uint32_t
v3d_general_tmu_op(nir_intrinsic_instr *instr)
{
        switch (instr->intrinsic) {
        case nir_intrinsic_load_ssbo:
        case nir_intrinsic_load_ubo:
        case nir_intrinsic_load_uniform:
        case nir_intrinsic_load_shared:
                return GENERAL_TMU_READ_OP_READ;
        case nir_intrinsic_store_ssbo:
        case nir_intrinsic_store_shared:
                return GENERAL_TMU_WRITE_OP_WRITE;
        case nir_intrinsic_ssbo_atomic_add:
        case nir_intrinsic_shared_atomic_add:
                return GENERAL_TMU_WRITE_OP_ATOMIC_ADD_WRAP;
        case nir_intrinsic_ssbo_atomic_imin:
        case nir_intrinsic_shared_atomic_imin:
                return GENERAL_TMU_WRITE_OP_ATOMIC_SMIN;
        case nir_intrinsic_ssbo_atomic_umin:
        case nir_intrinsic_shared_atomic_umin:
                return GENERAL_TMU_WRITE_OP_ATOMIC_UMIN;
        case nir_intrinsic_ssbo_atomic_imax:
        case nir_intrinsic_shared_atomic_imax:
                return GENERAL_TMU_WRITE_OP_ATOMIC_SMAX;
        case nir_intrinsic_ssbo_atomic_umax:
        case nir_intrinsic_shared_atomic_umax:
                return GENERAL_TMU_WRITE_OP_ATOMIC_UMAX;
        case nir_intrinsic_ssbo_atomic_and:
        case nir_intrinsic_shared_atomic_and:
                return GENERAL_TMU_WRITE_OP_ATOMIC_AND;
        case nir_intrinsic_ssbo_atomic_or:
        case nir_intrinsic_shared_atomic_or:
                return GENERAL_TMU_WRITE_OP_ATOMIC_OR;
        case nir_intrinsic_ssbo_atomic_xor:
        case nir_intrinsic_shared_atomic_xor:
                return GENERAL_TMU_WRITE_OP_ATOMIC_XOR;
        case nir_intrinsic_ssbo_atomic_exchange:
        case nir_intrinsic_shared_atomic_exchange:
                return GENERAL_TMU_WRITE_OP_ATOMIC_XCHG;
        case nir_intrinsic_ssbo_atomic_comp_swap:
        case nir_intrinsic_shared_atomic_comp_swap:
                return GENERAL_TMU_WRITE_OP_ATOMIC_CMPXCHG;
        default:
                unreachable("unknown intrinsic op");
        }
}

/**
 * Implements indirect uniform loads and SSBO accesses through the TMU general
 * memory access interface.
 */
static void
ntq_emit_tmu_general(struct v3d_compile *c, nir_intrinsic_instr *instr,
                     bool is_shared)
{
        /* XXX perf: We should turn add/sub of 1 to inc/dec.  Perhaps NIR
         * wants to have support for inc/dec?
         */

        uint32_t tmu_op = v3d_general_tmu_op(instr);
        bool is_store = (instr->intrinsic == nir_intrinsic_store_ssbo ||
                         instr->intrinsic == nir_intrinsic_store_shared);
        bool has_index = !is_shared;

        int offset_src;
        int tmu_writes = 1; /* address */
        if (instr->intrinsic == nir_intrinsic_load_uniform) {
                offset_src = 0;
        } else if (instr->intrinsic == nir_intrinsic_load_ssbo ||
                   instr->intrinsic == nir_intrinsic_load_ubo ||
                   instr->intrinsic == nir_intrinsic_load_shared) {
                offset_src = 0 + has_index;
        } else if (is_store) {
                offset_src = 1 + has_index;
                for (int i = 0; i < instr->num_components; i++) {
                        vir_MOV_dest(c,
                                     vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMUD),
                                     ntq_get_src(c, instr->src[0], i));
                        tmu_writes++;
                }
        } else {
                offset_src = 0 + has_index;
                vir_MOV_dest(c,
                             vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMUD),
                             ntq_get_src(c, instr->src[1 + has_index], 0));
                tmu_writes++;
                if (tmu_op == GENERAL_TMU_WRITE_OP_ATOMIC_CMPXCHG) {
                        vir_MOV_dest(c,
                                     vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMUD),
                                     ntq_get_src(c, instr->src[2 + has_index],
                                                 0));
                        tmu_writes++;
                }
        }

        /* Make sure we won't exceed the 16-entry TMU fifo if each thread is
         * storing at the same time.
         */
        while (tmu_writes > 16 / c->threads)
                c->threads /= 2;

        struct qreg offset;
        if (instr->intrinsic == nir_intrinsic_load_uniform) {
                offset = vir_uniform(c, QUNIFORM_UBO_ADDR, 0);

                /* Find what variable in the default uniform block this
                 * uniform load is coming from.
                 */
                uint32_t base = nir_intrinsic_base(instr);
                int i;
                struct v3d_ubo_range *range = NULL;
                for (i = 0; i < c->num_ubo_ranges; i++) {
                        range = &c->ubo_ranges[i];
                        if (base >= range->src_offset &&
                            base < range->src_offset + range->size) {
                                break;
                        }
                }
                /* The driver-location-based offset always has to be within a
                 * declared uniform range.
                 */
                assert(i != c->num_ubo_ranges);
                if (!c->ubo_range_used[i]) {
                        c->ubo_range_used[i] = true;
                        range->dst_offset = c->next_ubo_dst_offset;
                        c->next_ubo_dst_offset += range->size;
                }

                base = base - range->src_offset + range->dst_offset;

                if (base != 0)
                        offset = vir_ADD(c, offset, vir_uniform_ui(c, base));
        } else if (instr->intrinsic == nir_intrinsic_load_ubo) {
                /* Note that QUNIFORM_UBO_ADDR takes a UBO index shifted up by
                 * 1 (0 is gallium's constant buffer 0).
                 */
                offset = vir_uniform(c, QUNIFORM_UBO_ADDR,
                                     nir_src_as_uint(instr->src[0]) + 1);
        } else if (is_shared) {
                /* Shared variables have no buffer index, and all start from a
                 * common base that we set up at the start of dispatch
                 */
                offset = c->cs_shared_offset;
        } else {
                offset = vir_uniform(c, QUNIFORM_SSBO_OFFSET,
                                     nir_src_as_uint(instr->src[is_store ?
                                                                1 : 0]));
        }

        uint32_t config = (0xffffff00 |
                           tmu_op |
                           GENERAL_TMU_LOOKUP_PER_PIXEL);
        if (instr->num_components == 1) {
                config |= GENERAL_TMU_LOOKUP_TYPE_32BIT_UI;
        } else {
                config |= (GENERAL_TMU_LOOKUP_TYPE_VEC2 +
                           instr->num_components - 2);
        }

        if (c->execute.file != QFILE_NULL)
                vir_PF(c, c->execute, V3D_QPU_PF_PUSHZ);

        struct qreg dest;
        if (config == ~0)
                dest = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMUA);
        else
                dest = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMUAU);

        struct qinst *tmu;
        if (nir_src_is_const(instr->src[offset_src]) &&
            nir_src_as_uint(instr->src[offset_src]) == 0) {
                tmu = vir_MOV_dest(c, dest, offset);
        } else {
                tmu = vir_ADD_dest(c, dest,
                                   offset,
                                   ntq_get_src(c, instr->src[offset_src], 0));
        }

        if (config != ~0) {
                tmu->src[vir_get_implicit_uniform_src(tmu)] =
                        vir_uniform_ui(c, config);
        }

        if (c->execute.file != QFILE_NULL)
                vir_set_cond(tmu, V3D_QPU_COND_IFA);

        vir_emit_thrsw(c);

        /* Read the result, or wait for the TMU op to complete. */
        for (int i = 0; i < nir_intrinsic_dest_components(instr); i++)
                ntq_store_dest(c, &instr->dest, i, vir_MOV(c, vir_LDTMU(c)));

        if (nir_intrinsic_dest_components(instr) == 0)
                vir_TMUWT(c);
}

static struct qreg *
ntq_init_ssa_def(struct v3d_compile *c, nir_ssa_def *def)
{
        struct qreg *qregs = ralloc_array(c->def_ht, struct qreg,
                                          def->num_components);
        _mesa_hash_table_insert(c->def_ht, def, qregs);
        return qregs;
}

/**
 * This function is responsible for getting VIR results into the associated
 * storage for a NIR instruction.
 *
 * If it's a NIR SSA def, then we just set the associated hash table entry to
 * the new result.
 *
 * If it's a NIR reg, then we need to update the existing qreg assigned to the
 * NIR destination with the incoming value.  To do that without introducing
 * new MOVs, we require that the incoming qreg either be a uniform, or be
 * SSA-defined by the previous VIR instruction in the block and rewritable by
 * this function.  That lets us sneak ahead and insert the SF flag beforehand
 * (knowing that the previous instruction doesn't depend on flags) and rewrite
 * its destination to be the NIR reg's destination
 */
void
ntq_store_dest(struct v3d_compile *c, nir_dest *dest, int chan,
               struct qreg result)
{
        struct qinst *last_inst = NULL;
        if (!list_empty(&c->cur_block->instructions))
                last_inst = (struct qinst *)c->cur_block->instructions.prev;

        assert(result.file == QFILE_UNIF ||
               (result.file == QFILE_TEMP &&
                last_inst && last_inst == c->defs[result.index]));

        if (dest->is_ssa) {
                assert(chan < dest->ssa.num_components);

                struct qreg *qregs;
                struct hash_entry *entry =
                        _mesa_hash_table_search(c->def_ht, &dest->ssa);

                if (entry)
                        qregs = entry->data;
                else
                        qregs = ntq_init_ssa_def(c, &dest->ssa);

                qregs[chan] = result;
        } else {
                nir_register *reg = dest->reg.reg;
                assert(dest->reg.base_offset == 0);
                assert(reg->num_array_elems == 0);
                struct hash_entry *entry =
                        _mesa_hash_table_search(c->def_ht, reg);
                struct qreg *qregs = entry->data;

                /* Insert a MOV if the source wasn't an SSA def in the
                 * previous instruction.
                 */
                if (result.file == QFILE_UNIF) {
                        result = vir_MOV(c, result);
                        last_inst = c->defs[result.index];
                }

                /* We know they're both temps, so just rewrite index. */
                c->defs[last_inst->dst.index] = NULL;
                last_inst->dst.index = qregs[chan].index;

                /* If we're in control flow, then make this update of the reg
                 * conditional on the execution mask.
                 */
                if (c->execute.file != QFILE_NULL) {
                        last_inst->dst.index = qregs[chan].index;

                        /* Set the flags to the current exec mask.
                         */
                        c->cursor = vir_before_inst(last_inst);
                        vir_PF(c, c->execute, V3D_QPU_PF_PUSHZ);
                        c->cursor = vir_after_inst(last_inst);

                        vir_set_cond(last_inst, V3D_QPU_COND_IFA);
                        last_inst->cond_is_exec_mask = true;
                }
        }
}

struct qreg
ntq_get_src(struct v3d_compile *c, nir_src src, int i)
{
        struct hash_entry *entry;
        if (src.is_ssa) {
                entry = _mesa_hash_table_search(c->def_ht, src.ssa);
                assert(i < src.ssa->num_components);
        } else {
                nir_register *reg = src.reg.reg;
                entry = _mesa_hash_table_search(c->def_ht, reg);
                assert(reg->num_array_elems == 0);
                assert(src.reg.base_offset == 0);
                assert(i < reg->num_components);
        }

        struct qreg *qregs = entry->data;
        return qregs[i];
}

static struct qreg
ntq_get_alu_src(struct v3d_compile *c, nir_alu_instr *instr,
                unsigned src)
{
        assert(util_is_power_of_two_or_zero(instr->dest.write_mask));
        unsigned chan = ffs(instr->dest.write_mask) - 1;
        struct qreg r = ntq_get_src(c, instr->src[src].src,
                                    instr->src[src].swizzle[chan]);

        assert(!instr->src[src].abs);
        assert(!instr->src[src].negate);

        return r;
};

static struct qreg
ntq_minify(struct v3d_compile *c, struct qreg size, struct qreg level)
{
        return vir_MAX(c, vir_SHR(c, size, level), vir_uniform_ui(c, 1));
}

static void
ntq_emit_txs(struct v3d_compile *c, nir_tex_instr *instr)
{
        unsigned unit = instr->texture_index;
        int lod_index = nir_tex_instr_src_index(instr, nir_tex_src_lod);
        int dest_size = nir_tex_instr_dest_size(instr);

        struct qreg lod = c->undef;
        if (lod_index != -1)
                lod = ntq_get_src(c, instr->src[lod_index].src, 0);

        for (int i = 0; i < dest_size; i++) {
                assert(i < 3);
                enum quniform_contents contents;

                if (instr->is_array && i == dest_size - 1)
                        contents = QUNIFORM_TEXTURE_ARRAY_SIZE;
                else
                        contents = QUNIFORM_TEXTURE_WIDTH + i;

                struct qreg size = vir_uniform(c, contents, unit);

                switch (instr->sampler_dim) {
                case GLSL_SAMPLER_DIM_1D:
                case GLSL_SAMPLER_DIM_2D:
                case GLSL_SAMPLER_DIM_MS:
                case GLSL_SAMPLER_DIM_3D:
                case GLSL_SAMPLER_DIM_CUBE:
                        /* Don't minify the array size. */
                        if (!(instr->is_array && i == dest_size - 1)) {
                                size = ntq_minify(c, size, lod);
                        }
                        break;

                case GLSL_SAMPLER_DIM_RECT:
                        /* There's no LOD field for rects */
                        break;

                default:
                        unreachable("Bad sampler type");
                }

                ntq_store_dest(c, &instr->dest, i, size);
        }
}

static void
ntq_emit_tex(struct v3d_compile *c, nir_tex_instr *instr)
{
        unsigned unit = instr->texture_index;

        /* Since each texture sampling op requires uploading uniforms to
         * reference the texture, there's no HW support for texture size and
         * you just upload uniforms containing the size.
         */
        switch (instr->op) {
        case nir_texop_query_levels:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_TEXTURE_LEVELS, unit));
                return;
        case nir_texop_txs:
                ntq_emit_txs(c, instr);
                return;
        default:
                break;
        }

        if (c->devinfo->ver >= 40)
                v3d40_vir_emit_tex(c, instr);
        else
                v3d33_vir_emit_tex(c, instr);
}

static struct qreg
ntq_fsincos(struct v3d_compile *c, struct qreg src, bool is_cos)
{
        struct qreg input = vir_FMUL(c, src, vir_uniform_f(c, 1.0f / M_PI));
        if (is_cos)
                input = vir_FADD(c, input, vir_uniform_f(c, 0.5));

        struct qreg periods = vir_FROUND(c, input);
        struct qreg sin_output = vir_SIN(c, vir_FSUB(c, input, periods));
        return vir_XOR(c, sin_output, vir_SHL(c,
                                              vir_FTOIN(c, periods),
                                              vir_uniform_ui(c, -1)));
}

static struct qreg
ntq_fsign(struct v3d_compile *c, struct qreg src)
{
        struct qreg t = vir_get_temp(c);

        vir_MOV_dest(c, t, vir_uniform_f(c, 0.0));
        vir_PF(c, vir_FMOV(c, src), V3D_QPU_PF_PUSHZ);
        vir_MOV_cond(c, V3D_QPU_COND_IFNA, t, vir_uniform_f(c, 1.0));
        vir_PF(c, vir_FMOV(c, src), V3D_QPU_PF_PUSHN);
        vir_MOV_cond(c, V3D_QPU_COND_IFA, t, vir_uniform_f(c, -1.0));
        return vir_MOV(c, t);
}

static struct qreg
ntq_isign(struct v3d_compile *c, struct qreg src)
{
        struct qreg t = vir_get_temp(c);

        vir_MOV_dest(c, t, vir_uniform_ui(c, 0));
        vir_PF(c, vir_MOV(c, src), V3D_QPU_PF_PUSHZ);
        vir_MOV_cond(c, V3D_QPU_COND_IFNA, t, vir_uniform_ui(c, 1));
        vir_PF(c, vir_MOV(c, src), V3D_QPU_PF_PUSHN);
        vir_MOV_cond(c, V3D_QPU_COND_IFA, t, vir_uniform_ui(c, -1));
        return vir_MOV(c, t);
}

static void
emit_fragcoord_input(struct v3d_compile *c, int attr)
{
        c->inputs[attr * 4 + 0] = vir_FXCD(c);
        c->inputs[attr * 4 + 1] = vir_FYCD(c);
        c->inputs[attr * 4 + 2] = c->payload_z;
        c->inputs[attr * 4 + 3] = vir_RECIP(c, c->payload_w);
}

static struct qreg
emit_fragment_varying(struct v3d_compile *c, nir_variable *var,
                      uint8_t swizzle, int array_index)
{
        struct qreg r3 = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_R3);
        struct qreg r5 = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_R5);

        struct qreg vary;
        if (c->devinfo->ver >= 41) {
                struct qinst *ldvary = vir_add_inst(V3D_QPU_A_NOP, c->undef,
                                                    c->undef, c->undef);
                ldvary->qpu.sig.ldvary = true;
                vary = vir_emit_def(c, ldvary);
        } else {
                vir_NOP(c)->qpu.sig.ldvary = true;
                vary = r3;
        }

        /* For gl_PointCoord input or distance along a line, we'll be called
         * with no nir_variable, and we don't count toward VPM size so we
         * don't track an input slot.
         */
        if (!var) {
                return vir_FADD(c, vir_FMUL(c, vary, c->payload_w), r5);
        }

        int i = c->num_inputs++;
        c->input_slots[i] =
                v3d_slot_from_slot_and_component(var->data.location +
                                                 array_index, swizzle);

        switch (var->data.interpolation) {
        case INTERP_MODE_NONE:
                /* If a gl_FrontColor or gl_BackColor input has no interp
                 * qualifier, then if we're using glShadeModel(GL_FLAT) it
                 * needs to be flat shaded.
                 */
                switch (var->data.location + array_index) {
                case VARYING_SLOT_COL0:
                case VARYING_SLOT_COL1:
                case VARYING_SLOT_BFC0:
                case VARYING_SLOT_BFC1:
                        if (c->fs_key->shade_model_flat) {
                                BITSET_SET(c->flat_shade_flags, i);
                                vir_MOV_dest(c, c->undef, vary);
                                return vir_MOV(c, r5);
                        } else {
                                return vir_FADD(c, vir_FMUL(c, vary,
                                                            c->payload_w), r5);
                        }
                default:
                        break;
                }
                /* FALLTHROUGH */
        case INTERP_MODE_SMOOTH:
                if (var->data.centroid) {
                        BITSET_SET(c->centroid_flags, i);
                        return vir_FADD(c, vir_FMUL(c, vary,
                                                    c->payload_w_centroid), r5);
                } else {
                        return vir_FADD(c, vir_FMUL(c, vary, c->payload_w), r5);
                }
        case INTERP_MODE_NOPERSPECTIVE:
                BITSET_SET(c->noperspective_flags, i);
                return vir_FADD(c, vir_MOV(c, vary), r5);
        case INTERP_MODE_FLAT:
                BITSET_SET(c->flat_shade_flags, i);
                vir_MOV_dest(c, c->undef, vary);
                return vir_MOV(c, r5);
        default:
                unreachable("Bad interp mode");
        }
}

static void
emit_fragment_input(struct v3d_compile *c, int attr, nir_variable *var,
                    int array_index)
{
        for (int i = 0; i < glsl_get_vector_elements(var->type); i++) {
                int chan = var->data.location_frac + i;
                c->inputs[attr * 4 + chan] =
                        emit_fragment_varying(c, var, chan, array_index);
        }
}

static void
add_output(struct v3d_compile *c,
           uint32_t decl_offset,
           uint8_t slot,
           uint8_t swizzle)
{
        uint32_t old_array_size = c->outputs_array_size;
        resize_qreg_array(c, &c->outputs, &c->outputs_array_size,
                          decl_offset + 1);

        if (old_array_size != c->outputs_array_size) {
                c->output_slots = reralloc(c,
                                           c->output_slots,
                                           struct v3d_varying_slot,
                                           c->outputs_array_size);
        }

        c->output_slots[decl_offset] =
                v3d_slot_from_slot_and_component(slot, swizzle);
}

static void
declare_uniform_range(struct v3d_compile *c, uint32_t start, uint32_t size)
{
        unsigned array_id = c->num_ubo_ranges++;
        if (array_id >= c->ubo_ranges_array_size) {
                c->ubo_ranges_array_size = MAX2(c->ubo_ranges_array_size * 2,
                                                array_id + 1);
                c->ubo_ranges = reralloc(c, c->ubo_ranges,
                                         struct v3d_ubo_range,
                                         c->ubo_ranges_array_size);
                c->ubo_range_used = reralloc(c, c->ubo_range_used,
                                             bool,
                                             c->ubo_ranges_array_size);
        }

        c->ubo_ranges[array_id].dst_offset = 0;
        c->ubo_ranges[array_id].src_offset = start;
        c->ubo_ranges[array_id].size = size;
        c->ubo_range_used[array_id] = false;
}

/**
 * If compare_instr is a valid comparison instruction, emits the
 * compare_instr's comparison and returns the sel_instr's return value based
 * on the compare_instr's result.
 */
static bool
ntq_emit_comparison(struct v3d_compile *c,
                    nir_alu_instr *compare_instr,
                    enum v3d_qpu_cond *out_cond)
{
        struct qreg src0 = ntq_get_alu_src(c, compare_instr, 0);
        struct qreg src1;
        if (nir_op_infos[compare_instr->op].num_inputs > 1)
                src1 = ntq_get_alu_src(c, compare_instr, 1);
        bool cond_invert = false;
        struct qreg nop = vir_reg(QFILE_NULL, 0);

        switch (compare_instr->op) {
        case nir_op_feq32:
        case nir_op_seq:
                vir_set_pf(vir_FCMP_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHZ);
                break;
        case nir_op_ieq32:
                vir_set_pf(vir_XOR_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHZ);
                break;

        case nir_op_fne32:
        case nir_op_sne:
                vir_set_pf(vir_FCMP_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHZ);
                cond_invert = true;
                break;
        case nir_op_ine32:
                vir_set_pf(vir_XOR_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHZ);
                cond_invert = true;
                break;

        case nir_op_fge32:
        case nir_op_sge:
                vir_set_pf(vir_FCMP_dest(c, nop, src1, src0), V3D_QPU_PF_PUSHC);
                break;
        case nir_op_ige32:
                vir_set_pf(vir_MIN_dest(c, nop, src1, src0), V3D_QPU_PF_PUSHC);
                cond_invert = true;
                break;
        case nir_op_uge32:
                vir_set_pf(vir_SUB_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHC);
                cond_invert = true;
                break;

        case nir_op_slt:
        case nir_op_flt32:
                vir_set_pf(vir_FCMP_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHN);
                break;
        case nir_op_ilt32:
                vir_set_pf(vir_MIN_dest(c, nop, src1, src0), V3D_QPU_PF_PUSHC);
                break;
        case nir_op_ult32:
                vir_set_pf(vir_SUB_dest(c, nop, src0, src1), V3D_QPU_PF_PUSHC);
                break;

        default:
                return false;
        }

        *out_cond = cond_invert ? V3D_QPU_COND_IFNA : V3D_QPU_COND_IFA;

        return true;
}

/* Finds an ALU instruction that generates our src value that could
 * (potentially) be greedily emitted in the consuming instruction.
 */
static struct nir_alu_instr *
ntq_get_alu_parent(nir_src src)
{
        if (!src.is_ssa || src.ssa->parent_instr->type != nir_instr_type_alu)
                return NULL;
        nir_alu_instr *instr = nir_instr_as_alu(src.ssa->parent_instr);
        if (!instr)
                return NULL;

        /* If the ALU instr's srcs are non-SSA, then we would have to avoid
         * moving emission of the ALU instr down past another write of the
         * src.
         */
        for (int i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
                if (!instr->src[i].src.is_ssa)
                        return NULL;
        }

        return instr;
}

/**
 * Attempts to fold a comparison generating a boolean result into the
 * condition code for selecting between two values, instead of comparing the
 * boolean result against 0 to generate the condition code.
 */
static struct qreg ntq_emit_bcsel(struct v3d_compile *c, nir_alu_instr *instr,
                                  struct qreg *src)
{
        nir_alu_instr *compare = ntq_get_alu_parent(instr->src[0].src);
        if (!compare)
                goto out;

        enum v3d_qpu_cond cond;
        if (ntq_emit_comparison(c, compare, &cond))
                return vir_MOV(c, vir_SEL(c, cond, src[1], src[2]));

out:
        vir_PF(c, src[0], V3D_QPU_PF_PUSHZ);
        return vir_MOV(c, vir_SEL(c, V3D_QPU_COND_IFNA, src[1], src[2]));
}


static void
ntq_emit_alu(struct v3d_compile *c, nir_alu_instr *instr)
{
        /* This should always be lowered to ALU operations for V3D. */
        assert(!instr->dest.saturate);

        /* Vectors are special in that they have non-scalarized writemasks,
         * and just take the first swizzle channel for each argument in order
         * into each writemask channel.
         */
        if (instr->op == nir_op_vec2 ||
            instr->op == nir_op_vec3 ||
            instr->op == nir_op_vec4) {
                struct qreg srcs[4];
                for (int i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
                        srcs[i] = ntq_get_src(c, instr->src[i].src,
                                              instr->src[i].swizzle[0]);
                for (int i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
                        ntq_store_dest(c, &instr->dest.dest, i,
                                       vir_MOV(c, srcs[i]));
                return;
        }

        /* General case: We can just grab the one used channel per src. */
        struct qreg src[nir_op_infos[instr->op].num_inputs];
        for (int i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
                src[i] = ntq_get_alu_src(c, instr, i);
        }

        struct qreg result;

        switch (instr->op) {
        case nir_op_fmov:
        case nir_op_imov:
                result = vir_MOV(c, src[0]);
                break;

        case nir_op_fneg:
                result = vir_XOR(c, src[0], vir_uniform_ui(c, 1 << 31));
                break;
        case nir_op_ineg:
                result = vir_NEG(c, src[0]);
                break;

        case nir_op_fmul:
                result = vir_FMUL(c, src[0], src[1]);
                break;
        case nir_op_fadd:
                result = vir_FADD(c, src[0], src[1]);
                break;
        case nir_op_fsub:
                result = vir_FSUB(c, src[0], src[1]);
                break;
        case nir_op_fmin:
                result = vir_FMIN(c, src[0], src[1]);
                break;
        case nir_op_fmax:
                result = vir_FMAX(c, src[0], src[1]);
                break;

        case nir_op_f2i32:
                result = vir_FTOIZ(c, src[0]);
                break;
        case nir_op_f2u32:
                result = vir_FTOUZ(c, src[0]);
                break;
        case nir_op_i2f32:
                result = vir_ITOF(c, src[0]);
                break;
        case nir_op_u2f32:
                result = vir_UTOF(c, src[0]);
                break;
        case nir_op_b2f32:
                result = vir_AND(c, src[0], vir_uniform_f(c, 1.0));
                break;
        case nir_op_b2i32:
                result = vir_AND(c, src[0], vir_uniform_ui(c, 1));
                break;
        case nir_op_i2b32:
        case nir_op_f2b32:
                vir_PF(c, src[0], V3D_QPU_PF_PUSHZ);
                result = vir_MOV(c, vir_SEL(c, V3D_QPU_COND_IFNA,
                                            vir_uniform_ui(c, ~0),
                                            vir_uniform_ui(c, 0)));
                break;

        case nir_op_iadd:
                result = vir_ADD(c, src[0], src[1]);
                break;
        case nir_op_ushr:
                result = vir_SHR(c, src[0], src[1]);
                break;
        case nir_op_isub:
                result = vir_SUB(c, src[0], src[1]);
                break;
        case nir_op_ishr:
                result = vir_ASR(c, src[0], src[1]);
                break;
        case nir_op_ishl:
                result = vir_SHL(c, src[0], src[1]);
                break;
        case nir_op_imin:
                result = vir_MIN(c, src[0], src[1]);
                break;
        case nir_op_umin:
                result = vir_UMIN(c, src[0], src[1]);
                break;
        case nir_op_imax:
                result = vir_MAX(c, src[0], src[1]);
                break;
        case nir_op_umax:
                result = vir_UMAX(c, src[0], src[1]);
                break;
        case nir_op_iand:
                result = vir_AND(c, src[0], src[1]);
                break;
        case nir_op_ior:
                result = vir_OR(c, src[0], src[1]);
                break;
        case nir_op_ixor:
                result = vir_XOR(c, src[0], src[1]);
                break;
        case nir_op_inot:
                result = vir_NOT(c, src[0]);
                break;

        case nir_op_ufind_msb:
                result = vir_SUB(c, vir_uniform_ui(c, 31), vir_CLZ(c, src[0]));
                break;

        case nir_op_imul:
                result = vir_UMUL(c, src[0], src[1]);
                break;

        case nir_op_seq:
        case nir_op_sne:
        case nir_op_sge:
        case nir_op_slt: {
                enum v3d_qpu_cond cond;
                MAYBE_UNUSED bool ok = ntq_emit_comparison(c, instr, &cond);
                assert(ok);
                result = vir_MOV(c, vir_SEL(c, cond,
                                            vir_uniform_f(c, 1.0),
                                            vir_uniform_f(c, 0.0)));
                break;
        }

        case nir_op_feq32:
        case nir_op_fne32:
        case nir_op_fge32:
        case nir_op_flt32:
        case nir_op_ieq32:
        case nir_op_ine32:
        case nir_op_ige32:
        case nir_op_uge32:
        case nir_op_ilt32:
        case nir_op_ult32: {
                enum v3d_qpu_cond cond;
                MAYBE_UNUSED bool ok = ntq_emit_comparison(c, instr, &cond);
                assert(ok);
                result = vir_MOV(c, vir_SEL(c, cond,
                                            vir_uniform_ui(c, ~0),
                                            vir_uniform_ui(c, 0)));
                break;
        }

        case nir_op_b32csel:
                result = ntq_emit_bcsel(c, instr, src);
                break;
        case nir_op_fcsel:
                vir_PF(c, src[0], V3D_QPU_PF_PUSHZ);
                result = vir_MOV(c, vir_SEL(c, V3D_QPU_COND_IFNA,
                                            src[1], src[2]));
                break;

        case nir_op_frcp:
                result = vir_RECIP(c, src[0]);
                break;
        case nir_op_frsq:
                result = vir_RSQRT(c, src[0]);
                break;
        case nir_op_fexp2:
                result = vir_EXP(c, src[0]);
                break;
        case nir_op_flog2:
                result = vir_LOG(c, src[0]);
                break;

        case nir_op_fceil:
                result = vir_FCEIL(c, src[0]);
                break;
        case nir_op_ffloor:
                result = vir_FFLOOR(c, src[0]);
                break;
        case nir_op_fround_even:
                result = vir_FROUND(c, src[0]);
                break;
        case nir_op_ftrunc:
                result = vir_FTRUNC(c, src[0]);
                break;
        case nir_op_ffract:
                result = vir_FSUB(c, src[0], vir_FFLOOR(c, src[0]));
                break;

        case nir_op_fsin:
                result = ntq_fsincos(c, src[0], false);
                break;
        case nir_op_fcos:
                result = ntq_fsincos(c, src[0], true);
                break;

        case nir_op_fsign:
                result = ntq_fsign(c, src[0]);
                break;
        case nir_op_isign:
                result = ntq_isign(c, src[0]);
                break;

        case nir_op_fabs: {
                result = vir_FMOV(c, src[0]);
                vir_set_unpack(c->defs[result.index], 0, V3D_QPU_UNPACK_ABS);
                break;
        }

        case nir_op_iabs:
                result = vir_MAX(c, src[0],
                                vir_SUB(c, vir_uniform_ui(c, 0), src[0]));
                break;

        case nir_op_fddx:
        case nir_op_fddx_coarse:
        case nir_op_fddx_fine:
                result = vir_FDX(c, src[0]);
                break;

        case nir_op_fddy:
        case nir_op_fddy_coarse:
        case nir_op_fddy_fine:
                result = vir_FDY(c, src[0]);
                break;

        case nir_op_uadd_carry:
                vir_PF(c, vir_ADD(c, src[0], src[1]), V3D_QPU_PF_PUSHC);
                result = vir_MOV(c, vir_SEL(c, V3D_QPU_COND_IFA,
                                            vir_uniform_ui(c, ~0),
                                            vir_uniform_ui(c, 0)));
                break;

        case nir_op_pack_half_2x16_split:
                result = vir_VFPACK(c, src[0], src[1]);
                break;

        case nir_op_unpack_half_2x16_split_x:
                /* XXX perf: It would be good to be able to merge this unpack
                 * with whatever uses our result.
                 */
                result = vir_FMOV(c, src[0]);
                vir_set_unpack(c->defs[result.index], 0, V3D_QPU_UNPACK_L);
                break;

        case nir_op_unpack_half_2x16_split_y:
                result = vir_FMOV(c, src[0]);
                vir_set_unpack(c->defs[result.index], 0, V3D_QPU_UNPACK_H);
                break;

        default:
                fprintf(stderr, "unknown NIR ALU inst: ");
                nir_print_instr(&instr->instr, stderr);
                fprintf(stderr, "\n");
                abort();
        }

        /* We have a scalar result, so the instruction should only have a
         * single channel written to.
         */
        assert(util_is_power_of_two_or_zero(instr->dest.write_mask));
        ntq_store_dest(c, &instr->dest.dest,
                       ffs(instr->dest.write_mask) - 1, result);
}

/* Each TLB read/write setup (a render target or depth buffer) takes an 8-bit
 * specifier.  They come from a register that's preloaded with 0xffffffff
 * (0xff gets you normal vec4 f16 RT0 writes), and when one is neaded the low
 * 8 bits are shifted off the bottom and 0xff shifted in from the top.
 */
#define TLB_TYPE_F16_COLOR         (3 << 6)
#define TLB_TYPE_I32_COLOR         (1 << 6)
#define TLB_TYPE_F32_COLOR         (0 << 6)
#define TLB_RENDER_TARGET_SHIFT    3 /* Reversed!  7 = RT 0, 0 = RT 7. */
#define TLB_SAMPLE_MODE_PER_SAMPLE (0 << 2)
#define TLB_SAMPLE_MODE_PER_PIXEL  (1 << 2)
#define TLB_F16_SWAP_HI_LO         (1 << 1)
#define TLB_VEC_SIZE_4_F16         (1 << 0)
#define TLB_VEC_SIZE_2_F16         (0 << 0)
#define TLB_VEC_SIZE_MINUS_1_SHIFT 0

/* Triggers Z/Stencil testing, used when the shader state's "FS modifies Z"
 * flag is set.
 */
#define TLB_TYPE_DEPTH             ((2 << 6) | (0 << 4))
#define TLB_DEPTH_TYPE_INVARIANT   (0 << 2) /* Unmodified sideband input used */
#define TLB_DEPTH_TYPE_PER_PIXEL   (1 << 2) /* QPU result used */
#define TLB_V42_DEPTH_TYPE_INVARIANT   (0 << 3) /* Unmodified sideband input used */
#define TLB_V42_DEPTH_TYPE_PER_PIXEL   (1 << 3) /* QPU result used */

/* Stencil is a single 32-bit write. */
#define TLB_TYPE_STENCIL_ALPHA     ((2 << 6) | (1 << 4))

static void
emit_frag_end(struct v3d_compile *c)
{
        /* XXX
        if (c->output_sample_mask_index != -1) {
                vir_MS_MASK(c, c->outputs[c->output_sample_mask_index]);
        }
        */

        bool has_any_tlb_color_write = false;
        for (int rt = 0; rt < c->fs_key->nr_cbufs; rt++) {
                if (c->output_color_var[rt])
                        has_any_tlb_color_write = true;
        }

        if (c->fs_key->sample_alpha_to_coverage && c->output_color_var[0]) {
                struct nir_variable *var = c->output_color_var[0];
                struct qreg *color = &c->outputs[var->data.driver_location * 4];

                vir_SETMSF_dest(c, vir_reg(QFILE_NULL, 0),
                                vir_AND(c,
                                        vir_MSF(c),
                                        vir_FTOC(c, color[3])));
        }

        if (c->output_position_index != -1) {
                struct qinst *inst = vir_MOV_dest(c,
                                                  vir_reg(QFILE_TLBU, 0),
                                                  c->outputs[c->output_position_index]);
                uint8_t tlb_specifier = TLB_TYPE_DEPTH;

                if (c->devinfo->ver >= 42) {
                        tlb_specifier |= (TLB_V42_DEPTH_TYPE_PER_PIXEL |
                                          TLB_SAMPLE_MODE_PER_PIXEL);
                } else
                        tlb_specifier |= TLB_DEPTH_TYPE_PER_PIXEL;

                inst->src[vir_get_implicit_uniform_src(inst)] =
                        vir_uniform_ui(c, tlb_specifier | 0xffffff00);
        } else if (c->s->info.fs.uses_discard ||
                   c->fs_key->sample_alpha_to_coverage ||
                   !has_any_tlb_color_write) {
                /* Emit passthrough Z if it needed to be delayed until shader
                 * end due to potential discards.
                 *
                 * Since (single-threaded) fragment shaders always need a TLB
                 * write, emit passthrouh Z if we didn't have any color
                 * buffers and flag us as potentially discarding, so that we
                 * can use Z as the TLB write.
                 */
                c->s->info.fs.uses_discard = true;

                struct qinst *inst = vir_MOV_dest(c,
                                                  vir_reg(QFILE_TLBU, 0),
                                                  vir_reg(QFILE_NULL, 0));
                uint8_t tlb_specifier = TLB_TYPE_DEPTH;

                if (c->devinfo->ver >= 42) {
                        /* The spec says the PER_PIXEL flag is ignored for
                         * invariant writes, but the simulator demands it.
                         */
                        tlb_specifier |= (TLB_V42_DEPTH_TYPE_INVARIANT |
                                          TLB_SAMPLE_MODE_PER_PIXEL);
                } else {
                        tlb_specifier |= TLB_DEPTH_TYPE_INVARIANT;
                }

                inst->src[vir_get_implicit_uniform_src(inst)] =
                        vir_uniform_ui(c, tlb_specifier | 0xffffff00);
        }

        /* XXX: Performance improvement: Merge Z write and color writes TLB
         * uniform setup
         */

        for (int rt = 0; rt < c->fs_key->nr_cbufs; rt++) {
                if (!c->output_color_var[rt])
                        continue;

                nir_variable *var = c->output_color_var[rt];
                struct qreg *color = &c->outputs[var->data.driver_location * 4];
                int num_components = glsl_get_vector_elements(var->type);
                uint32_t conf = 0xffffff00;
                struct qinst *inst;

                conf |= TLB_SAMPLE_MODE_PER_PIXEL;
                conf |= (7 - rt) << TLB_RENDER_TARGET_SHIFT;

                if (c->fs_key->swap_color_rb & (1 << rt))
                        num_components = MAX2(num_components, 3);

                assert(num_components != 0);
                switch (glsl_get_base_type(var->type)) {
                case GLSL_TYPE_UINT:
                case GLSL_TYPE_INT:
                        /* The F32 vs I32 distinction was dropped in 4.2. */
                        if (c->devinfo->ver < 42)
                                conf |= TLB_TYPE_I32_COLOR;
                        else
                                conf |= TLB_TYPE_F32_COLOR;
                        conf |= ((num_components - 1) <<
                                 TLB_VEC_SIZE_MINUS_1_SHIFT);

                        inst = vir_MOV_dest(c, vir_reg(QFILE_TLBU, 0), color[0]);
                        inst->src[vir_get_implicit_uniform_src(inst)] =
                                vir_uniform_ui(c, conf);

                        for (int i = 1; i < num_components; i++) {
                                inst = vir_MOV_dest(c, vir_reg(QFILE_TLB, 0),
                                                    color[i]);
                        }
                        break;

                default: {
                        struct qreg r = color[0];
                        struct qreg g = color[1];
                        struct qreg b = color[2];
                        struct qreg a = color[3];

                        if (c->fs_key->f32_color_rb & (1 << rt)) {
                                conf |= TLB_TYPE_F32_COLOR;
                                conf |= ((num_components - 1) <<
                                         TLB_VEC_SIZE_MINUS_1_SHIFT);
                        } else {
                                conf |= TLB_TYPE_F16_COLOR;
                                conf |= TLB_F16_SWAP_HI_LO;
                                if (num_components >= 3)
                                        conf |= TLB_VEC_SIZE_4_F16;
                                else
                                        conf |= TLB_VEC_SIZE_2_F16;
                        }

                        if (c->fs_key->swap_color_rb & (1 << rt))  {
                                r = color[2];
                                b = color[0];
                        }

                        if (c->fs_key->sample_alpha_to_one)
                                a = vir_uniform_f(c, 1.0);

                        if (c->fs_key->f32_color_rb & (1 << rt)) {
                                inst = vir_MOV_dest(c, vir_reg(QFILE_TLBU, 0), r);
                                inst->src[vir_get_implicit_uniform_src(inst)] =
                                        vir_uniform_ui(c, conf);

                                if (num_components >= 2)
                                        vir_MOV_dest(c, vir_reg(QFILE_TLB, 0), g);
                                if (num_components >= 3)
                                        vir_MOV_dest(c, vir_reg(QFILE_TLB, 0), b);
                                if (num_components >= 4)
                                        vir_MOV_dest(c, vir_reg(QFILE_TLB, 0), a);
                        } else {
                                inst = vir_VFPACK_dest(c, vir_reg(QFILE_TLB, 0), r, g);
                                if (conf != ~0) {
                                        inst->dst.file = QFILE_TLBU;
                                        inst->src[vir_get_implicit_uniform_src(inst)] =
                                                vir_uniform_ui(c, conf);
                                }

                                if (num_components >= 3)
                                        inst = vir_VFPACK_dest(c, vir_reg(QFILE_TLB, 0), b, a);
                        }
                        break;
                }
                }
        }
}

static void
vir_VPM_WRITE(struct v3d_compile *c, struct qreg val, uint32_t *vpm_index)
{
        if (c->devinfo->ver >= 40) {
                vir_STVPMV(c, vir_uniform_ui(c, *vpm_index), val);
                *vpm_index = *vpm_index + 1;
        } else {
                vir_MOV_dest(c, vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_VPM), val);
        }

        c->num_vpm_writes++;
}

static void
emit_scaled_viewport_write(struct v3d_compile *c, struct qreg rcp_w,
                           uint32_t *vpm_index)
{
        for (int i = 0; i < 2; i++) {
                struct qreg coord = c->outputs[c->output_position_index + i];
                coord = vir_FMUL(c, coord,
                                 vir_uniform(c, QUNIFORM_VIEWPORT_X_SCALE + i,
                                             0));
                coord = vir_FMUL(c, coord, rcp_w);
                vir_VPM_WRITE(c, vir_FTOIN(c, coord), vpm_index);
        }

}

static void
emit_zs_write(struct v3d_compile *c, struct qreg rcp_w, uint32_t *vpm_index)
{
        struct qreg zscale = vir_uniform(c, QUNIFORM_VIEWPORT_Z_SCALE, 0);
        struct qreg zoffset = vir_uniform(c, QUNIFORM_VIEWPORT_Z_OFFSET, 0);

        struct qreg z = c->outputs[c->output_position_index + 2];
        z = vir_FMUL(c, z, zscale);
        z = vir_FMUL(c, z, rcp_w);
        z = vir_FADD(c, z, zoffset);
        vir_VPM_WRITE(c, z, vpm_index);
}

static void
emit_rcp_wc_write(struct v3d_compile *c, struct qreg rcp_w, uint32_t *vpm_index)
{
        vir_VPM_WRITE(c, rcp_w, vpm_index);
}

static void
emit_point_size_write(struct v3d_compile *c, uint32_t *vpm_index)
{
        struct qreg point_size;

        if (c->output_point_size_index != -1)
                point_size = c->outputs[c->output_point_size_index];
        else
                point_size = vir_uniform_f(c, 1.0);

        /* Workaround: HW-2726 PTB does not handle zero-size points (BCM2835,
         * BCM21553).
         */
        point_size = vir_FMAX(c, point_size, vir_uniform_f(c, .125));

        vir_VPM_WRITE(c, point_size, vpm_index);
}

static void
emit_vpm_write_setup(struct v3d_compile *c)
{
        if (c->devinfo->ver >= 40)
                return;

        v3d33_vir_vpm_write_setup(c);
}

/**
 * Sets up c->outputs[c->output_position_index] for the vertex shader
 * epilogue, if an output vertex position wasn't specified in the user's
 * shader.  This may be the case for transform feedback with rasterizer
 * discard enabled.
 */
static void
setup_default_position(struct v3d_compile *c)
{
        if (c->output_position_index != -1)
                return;

        c->output_position_index = c->outputs_array_size;
        for (int i = 0; i < 4; i++) {
                add_output(c,
                           c->output_position_index + i,
                           VARYING_SLOT_POS, i);
        }
}

static void
emit_vert_end(struct v3d_compile *c)
{
        setup_default_position(c);

        uint32_t vpm_index = 0;
        struct qreg rcp_w = vir_RECIP(c,
                                      c->outputs[c->output_position_index + 3]);

        emit_vpm_write_setup(c);

        if (c->vs_key->is_coord) {
                for (int i = 0; i < 4; i++)
                        vir_VPM_WRITE(c, c->outputs[c->output_position_index + i],
                                      &vpm_index);
                emit_scaled_viewport_write(c, rcp_w, &vpm_index);
                if (c->vs_key->per_vertex_point_size) {
                        emit_point_size_write(c, &vpm_index);
                        /* emit_rcp_wc_write(c, rcp_w); */
                }
                /* XXX: Z-only rendering */
                if (0)
                        emit_zs_write(c, rcp_w, &vpm_index);
        } else {
                emit_scaled_viewport_write(c, rcp_w, &vpm_index);
                emit_zs_write(c, rcp_w, &vpm_index);
                emit_rcp_wc_write(c, rcp_w, &vpm_index);
                if (c->vs_key->per_vertex_point_size)
                        emit_point_size_write(c, &vpm_index);
        }

        for (int i = 0; i < c->vs_key->num_fs_inputs; i++) {
                struct v3d_varying_slot input = c->vs_key->fs_inputs[i];
                int j;

                for (j = 0; j < c->num_outputs; j++) {
                        struct v3d_varying_slot output = c->output_slots[j];

                        if (!memcmp(&input, &output, sizeof(input))) {
                                vir_VPM_WRITE(c, c->outputs[j],
                                              &vpm_index);
                                break;
                        }
                }
                /* Emit padding if we didn't find a declared VS output for
                 * this FS input.
                 */
                if (j == c->num_outputs)
                        vir_VPM_WRITE(c, vir_uniform_f(c, 0.0),
                                      &vpm_index);
        }

        /* GFXH-1684: VPM writes need to be complete by the end of the shader.
         */
        if (c->devinfo->ver >= 40 && c->devinfo->ver <= 42)
                vir_VPMWT(c);
}

void
v3d_optimize_nir(struct nir_shader *s)
{
        bool progress;

        do {
                progress = false;

                NIR_PASS_V(s, nir_lower_vars_to_ssa);
                NIR_PASS(progress, s, nir_lower_alu_to_scalar);
                NIR_PASS(progress, s, nir_lower_phis_to_scalar);
                NIR_PASS(progress, s, nir_copy_prop);
                NIR_PASS(progress, s, nir_opt_remove_phis);
                NIR_PASS(progress, s, nir_opt_dce);
                NIR_PASS(progress, s, nir_opt_dead_cf);
                NIR_PASS(progress, s, nir_opt_cse);
                NIR_PASS(progress, s, nir_opt_peephole_select, 8, true, true);
                NIR_PASS(progress, s, nir_opt_algebraic);
                NIR_PASS(progress, s, nir_opt_constant_folding);
                NIR_PASS(progress, s, nir_opt_undef);
        } while (progress);

        NIR_PASS(progress, s, nir_opt_move_load_ubo);
}

static int
driver_location_compare(const void *in_a, const void *in_b)
{
        const nir_variable *const *a = in_a;
        const nir_variable *const *b = in_b;

        return (*a)->data.driver_location - (*b)->data.driver_location;
}

static struct qreg
ntq_emit_vpm_read(struct v3d_compile *c,
                  uint32_t *num_components_queued,
                  uint32_t *remaining,
                  uint32_t vpm_index)
{
        struct qreg vpm = vir_reg(QFILE_VPM, vpm_index);

        if (c->devinfo->ver >= 40 ) {
                return vir_LDVPMV_IN(c,
                                     vir_uniform_ui(c,
                                                    (*num_components_queued)++));
        }

        if (*num_components_queued != 0) {
                (*num_components_queued)--;
                c->num_inputs++;
                return vir_MOV(c, vpm);
        }

        uint32_t num_components = MIN2(*remaining, 32);

        v3d33_vir_vpm_read_setup(c, num_components);

        *num_components_queued = num_components - 1;
        *remaining -= num_components;
        c->num_inputs++;

        return vir_MOV(c, vpm);
}

static void
ntq_setup_vpm_inputs(struct v3d_compile *c)
{
        /* Figure out how many components of each vertex attribute the shader
         * uses.  Each variable should have been split to individual
         * components and unused ones DCEed.  The vertex fetcher will load
         * from the start of the attribute to the number of components we
         * declare we need in c->vattr_sizes[].
         */
        nir_foreach_variable(var, &c->s->inputs) {
                /* No VS attribute array support. */
                assert(MAX2(glsl_get_length(var->type), 1) == 1);

                unsigned loc = var->data.driver_location;
                int start_component = var->data.location_frac;
                int num_components = glsl_get_components(var->type);

                c->vattr_sizes[loc] = MAX2(c->vattr_sizes[loc],
                                           start_component + num_components);
        }

        unsigned num_components = 0;
        uint32_t vpm_components_queued = 0;
        bool uses_iid = c->s->info.system_values_read &
                (1ull << SYSTEM_VALUE_INSTANCE_ID);
        bool uses_vid = c->s->info.system_values_read &
                (1ull << SYSTEM_VALUE_VERTEX_ID);
        num_components += uses_iid;
        num_components += uses_vid;

        for (int i = 0; i < ARRAY_SIZE(c->vattr_sizes); i++)
                num_components += c->vattr_sizes[i];

        if (uses_iid) {
                c->iid = ntq_emit_vpm_read(c, &vpm_components_queued,
                                           &num_components, ~0);
        }

        if (uses_vid) {
                c->vid = ntq_emit_vpm_read(c, &vpm_components_queued,
                                           &num_components, ~0);
        }

        for (int loc = 0; loc < ARRAY_SIZE(c->vattr_sizes); loc++) {
                resize_qreg_array(c, &c->inputs, &c->inputs_array_size,
                                  (loc + 1) * 4);

                for (int i = 0; i < c->vattr_sizes[loc]; i++) {
                        c->inputs[loc * 4 + i] =
                                ntq_emit_vpm_read(c,
                                                  &vpm_components_queued,
                                                  &num_components,
                                                  loc * 4 + i);

                }
        }

        if (c->devinfo->ver >= 40) {
                assert(vpm_components_queued == num_components);
        } else {
                assert(vpm_components_queued == 0);
                assert(num_components == 0);
        }
}

static void
ntq_setup_fs_inputs(struct v3d_compile *c)
{
        unsigned num_entries = 0;
        unsigned num_components = 0;
        nir_foreach_variable(var, &c->s->inputs) {
                num_entries++;
                num_components += glsl_get_components(var->type);
        }

        nir_variable *vars[num_entries];

        unsigned i = 0;
        nir_foreach_variable(var, &c->s->inputs)
                vars[i++] = var;

        /* Sort the variables so that we emit the input setup in
         * driver_location order.  This is required for VPM reads, whose data
         * is fetched into the VPM in driver_location (TGSI register index)
         * order.
         */
        qsort(&vars, num_entries, sizeof(*vars), driver_location_compare);

        for (unsigned i = 0; i < num_entries; i++) {
                nir_variable *var = vars[i];
                unsigned array_len = MAX2(glsl_get_length(var->type), 1);
                unsigned loc = var->data.driver_location;

                resize_qreg_array(c, &c->inputs, &c->inputs_array_size,
                                  (loc + array_len) * 4);

                if (var->data.location == VARYING_SLOT_POS) {
                        emit_fragcoord_input(c, loc);
                } else if (var->data.location == VARYING_SLOT_PNTC ||
                           (var->data.location >= VARYING_SLOT_VAR0 &&
                            (c->fs_key->point_sprite_mask &
                             (1 << (var->data.location -
                                    VARYING_SLOT_VAR0))))) {
                        c->inputs[loc * 4 + 0] = c->point_x;
                        c->inputs[loc * 4 + 1] = c->point_y;
                } else {
                        for (int j = 0; j < array_len; j++)
                                emit_fragment_input(c, loc + j, var, j);
                }
        }
}

static void
ntq_setup_outputs(struct v3d_compile *c)
{
        nir_foreach_variable(var, &c->s->outputs) {
                unsigned array_len = MAX2(glsl_get_length(var->type), 1);
                unsigned loc = var->data.driver_location * 4;

                assert(array_len == 1);
                (void)array_len;

                for (int i = 0; i < 4 - var->data.location_frac; i++) {
                        add_output(c, loc + var->data.location_frac + i,
                                   var->data.location,
                                   var->data.location_frac + i);
                }

                if (c->s->info.stage == MESA_SHADER_FRAGMENT) {
                        switch (var->data.location) {
                        case FRAG_RESULT_COLOR:
                                c->output_color_var[0] = var;
                                c->output_color_var[1] = var;
                                c->output_color_var[2] = var;
                                c->output_color_var[3] = var;
                                break;
                        case FRAG_RESULT_DATA0:
                        case FRAG_RESULT_DATA1:
                        case FRAG_RESULT_DATA2:
                        case FRAG_RESULT_DATA3:
                                c->output_color_var[var->data.location -
                                                    FRAG_RESULT_DATA0] = var;
                                break;
                        case FRAG_RESULT_DEPTH:
                                c->output_position_index = loc;
                                break;
                        case FRAG_RESULT_SAMPLE_MASK:
                                c->output_sample_mask_index = loc;
                                break;
                        }
                } else {
                        switch (var->data.location) {
                        case VARYING_SLOT_POS:
                                c->output_position_index = loc;
                                break;
                        case VARYING_SLOT_PSIZ:
                                c->output_point_size_index = loc;
                                break;
                        }
                }
        }
}

static void
ntq_setup_uniforms(struct v3d_compile *c)
{
        nir_foreach_variable(var, &c->s->uniforms) {
                uint32_t vec4_count = glsl_count_attribute_slots(var->type,
                                                                 false);
                unsigned vec4_size = 4 * sizeof(float);

                if (var->data.mode != nir_var_uniform)
                        continue;

                declare_uniform_range(c, var->data.driver_location * vec4_size,
                                      vec4_count * vec4_size);

        }
}

/**
 * Sets up the mapping from nir_register to struct qreg *.
 *
 * Each nir_register gets a struct qreg per 32-bit component being stored.
 */
static void
ntq_setup_registers(struct v3d_compile *c, struct exec_list *list)
{
        foreach_list_typed(nir_register, nir_reg, node, list) {
                unsigned array_len = MAX2(nir_reg->num_array_elems, 1);
                struct qreg *qregs = ralloc_array(c->def_ht, struct qreg,
                                                  array_len *
                                                  nir_reg->num_components);

                _mesa_hash_table_insert(c->def_ht, nir_reg, qregs);

                for (int i = 0; i < array_len * nir_reg->num_components; i++)
                        qregs[i] = vir_get_temp(c);
        }
}

static void
ntq_emit_load_const(struct v3d_compile *c, nir_load_const_instr *instr)
{
        /* XXX perf: Experiment with using immediate loads to avoid having
         * these end up in the uniform stream.  Watch out for breaking the
         * small immediates optimization in the process!
         */
        struct qreg *qregs = ntq_init_ssa_def(c, &instr->def);
        for (int i = 0; i < instr->def.num_components; i++)
                qregs[i] = vir_uniform_ui(c, instr->value.u32[i]);

        _mesa_hash_table_insert(c->def_ht, &instr->def, qregs);
}

static void
ntq_emit_ssa_undef(struct v3d_compile *c, nir_ssa_undef_instr *instr)
{
        struct qreg *qregs = ntq_init_ssa_def(c, &instr->def);

        /* VIR needs there to be *some* value, so pick 0 (same as for
         * ntq_setup_registers().
         */
        for (int i = 0; i < instr->def.num_components; i++)
                qregs[i] = vir_uniform_ui(c, 0);
}

static void
ntq_emit_image_size(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        assert(instr->intrinsic == nir_intrinsic_image_deref_size);
        nir_variable *var = nir_intrinsic_get_var(instr, 0);
        unsigned image_index = var->data.driver_location;
        const struct glsl_type *sampler_type = glsl_without_array(var->type);
        bool is_array = glsl_sampler_type_is_array(sampler_type);

        ntq_store_dest(c, &instr->dest, 0,
                       vir_uniform(c, QUNIFORM_IMAGE_WIDTH, image_index));
        if (instr->num_components > 1) {
                ntq_store_dest(c, &instr->dest, 1,
                               vir_uniform(c, QUNIFORM_IMAGE_HEIGHT,
                                           image_index));
        }
        if (instr->num_components > 2) {
                ntq_store_dest(c, &instr->dest, 2,
                               vir_uniform(c,
                                           is_array ?
                                           QUNIFORM_IMAGE_ARRAY_SIZE :
                                           QUNIFORM_IMAGE_DEPTH,
                                           image_index));
        }
}

static void
ntq_emit_intrinsic(struct v3d_compile *c, nir_intrinsic_instr *instr)
{
        unsigned offset;

        switch (instr->intrinsic) {
        case nir_intrinsic_load_uniform:
                if (nir_src_is_const(instr->src[0])) {
                        int offset = (nir_intrinsic_base(instr) +
                                      nir_src_as_uint(instr->src[0]));
                        assert(offset % 4 == 0);
                        /* We need dwords */
                        offset = offset / 4;
                        for (int i = 0; i < instr->num_components; i++) {
                                ntq_store_dest(c, &instr->dest, i,
                                               vir_uniform(c, QUNIFORM_UNIFORM,
                                                           offset + i));
                        }
                } else {
                        ntq_emit_tmu_general(c, instr, false);
                }
                break;

        case nir_intrinsic_load_ubo:
                ntq_emit_tmu_general(c, instr, false);
                break;

        case nir_intrinsic_ssbo_atomic_add:
        case nir_intrinsic_ssbo_atomic_imin:
        case nir_intrinsic_ssbo_atomic_umin:
        case nir_intrinsic_ssbo_atomic_imax:
        case nir_intrinsic_ssbo_atomic_umax:
        case nir_intrinsic_ssbo_atomic_and:
        case nir_intrinsic_ssbo_atomic_or:
        case nir_intrinsic_ssbo_atomic_xor:
        case nir_intrinsic_ssbo_atomic_exchange:
        case nir_intrinsic_ssbo_atomic_comp_swap:
        case nir_intrinsic_load_ssbo:
        case nir_intrinsic_store_ssbo:
                ntq_emit_tmu_general(c, instr, false);
                break;

        case nir_intrinsic_shared_atomic_add:
        case nir_intrinsic_shared_atomic_imin:
        case nir_intrinsic_shared_atomic_umin:
        case nir_intrinsic_shared_atomic_imax:
        case nir_intrinsic_shared_atomic_umax:
        case nir_intrinsic_shared_atomic_and:
        case nir_intrinsic_shared_atomic_or:
        case nir_intrinsic_shared_atomic_xor:
        case nir_intrinsic_shared_atomic_exchange:
        case nir_intrinsic_shared_atomic_comp_swap:
        case nir_intrinsic_load_shared:
        case nir_intrinsic_store_shared:
                ntq_emit_tmu_general(c, instr, true);
                break;

        case nir_intrinsic_image_deref_load:
        case nir_intrinsic_image_deref_store:
        case nir_intrinsic_image_deref_atomic_add:
        case nir_intrinsic_image_deref_atomic_min:
        case nir_intrinsic_image_deref_atomic_max:
        case nir_intrinsic_image_deref_atomic_and:
        case nir_intrinsic_image_deref_atomic_or:
        case nir_intrinsic_image_deref_atomic_xor:
        case nir_intrinsic_image_deref_atomic_exchange:
        case nir_intrinsic_image_deref_atomic_comp_swap:
                v3d40_vir_emit_image_load_store(c, instr);
                break;

        case nir_intrinsic_get_buffer_size:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_GET_BUFFER_SIZE,
                                           nir_src_as_uint(instr->src[0])));
                break;

        case nir_intrinsic_load_user_clip_plane:
                for (int i = 0; i < instr->num_components; i++) {
                        ntq_store_dest(c, &instr->dest, i,
                                       vir_uniform(c, QUNIFORM_USER_CLIP_PLANE,
                                                   nir_intrinsic_ucp_id(instr) *
                                                   4 + i));
                }
                break;

        case nir_intrinsic_load_alpha_ref_float:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_uniform(c, QUNIFORM_ALPHA_REF, 0));
                break;

        case nir_intrinsic_load_sample_mask_in:
                ntq_store_dest(c, &instr->dest, 0, vir_MSF(c));
                break;

        case nir_intrinsic_load_helper_invocation:
                vir_PF(c, vir_MSF(c), V3D_QPU_PF_PUSHZ);
                ntq_store_dest(c, &instr->dest, 0,
                               vir_MOV(c, vir_SEL(c, V3D_QPU_COND_IFA,
                                                  vir_uniform_ui(c, ~0),
                                                  vir_uniform_ui(c, 0))));
                break;

        case nir_intrinsic_load_front_face:
                /* The register contains 0 (front) or 1 (back), and we need to
                 * turn it into a NIR bool where true means front.
                 */
                ntq_store_dest(c, &instr->dest, 0,
                               vir_ADD(c,
                                       vir_uniform_ui(c, -1),
                                       vir_REVF(c)));
                break;

        case nir_intrinsic_load_instance_id:
                ntq_store_dest(c, &instr->dest, 0, vir_MOV(c, c->iid));
                break;

        case nir_intrinsic_load_vertex_id:
                ntq_store_dest(c, &instr->dest, 0, vir_MOV(c, c->vid));
                break;

        case nir_intrinsic_load_input:
                for (int i = 0; i < instr->num_components; i++) {
                        offset = (nir_intrinsic_base(instr) +
                                  nir_src_as_uint(instr->src[0]));
                        int comp = nir_intrinsic_component(instr) + i;
                        ntq_store_dest(c, &instr->dest, i,
                                       vir_MOV(c, c->inputs[offset * 4 + comp]));
                }
                break;

        case nir_intrinsic_store_output:
                offset = ((nir_intrinsic_base(instr) +
                           nir_src_as_uint(instr->src[1])) * 4 +
                          nir_intrinsic_component(instr));

                for (int i = 0; i < instr->num_components; i++) {
                        c->outputs[offset + i] =
                                vir_MOV(c, ntq_get_src(c, instr->src[0], i));
                }
                c->num_outputs = MAX2(c->num_outputs,
                                      offset + instr->num_components);
                break;

        case nir_intrinsic_image_deref_size:
                ntq_emit_image_size(c, instr);
                break;

        case nir_intrinsic_discard:
                if (c->execute.file != QFILE_NULL) {
                        vir_PF(c, c->execute, V3D_QPU_PF_PUSHZ);
                        vir_set_cond(vir_SETMSF_dest(c, vir_reg(QFILE_NULL, 0),
                                                     vir_uniform_ui(c, 0)),
                                V3D_QPU_COND_IFA);
                } else {
                        vir_SETMSF_dest(c, vir_reg(QFILE_NULL, 0),
                                        vir_uniform_ui(c, 0));
                }
                break;

        case nir_intrinsic_discard_if: {
                /* true (~0) if we're discarding */
                struct qreg cond = ntq_get_src(c, instr->src[0], 0);

                if (c->execute.file != QFILE_NULL) {
                        /* execute == 0 means the channel is active.  Invert
                         * the condition so that we can use zero as "executing
                         * and discarding."
                         */
                        vir_PF(c, vir_OR(c, c->execute, vir_NOT(c, cond)),
                               V3D_QPU_PF_PUSHZ);
                        vir_set_cond(vir_SETMSF_dest(c, vir_reg(QFILE_NULL, 0),
                                                     vir_uniform_ui(c, 0)),
                                     V3D_QPU_COND_IFA);
                } else {
                        vir_PF(c, cond, V3D_QPU_PF_PUSHZ);
                        vir_set_cond(vir_SETMSF_dest(c, vir_reg(QFILE_NULL, 0),
                                                     vir_uniform_ui(c, 0)),
                                     V3D_QPU_COND_IFNA);
                }

                break;
        }

        case nir_intrinsic_memory_barrier:
        case nir_intrinsic_memory_barrier_atomic_counter:
        case nir_intrinsic_memory_barrier_buffer:
        case nir_intrinsic_memory_barrier_image:
        case nir_intrinsic_memory_barrier_shared:
                /* We don't do any instruction scheduling of these NIR
                 * instructions between each other, so we just need to make
                 * sure that the TMU operations before the barrier are flushed
                 * before the ones after the barrier.  That is currently
                 * handled by having a THRSW in each of them and a LDTMU
                 * series or a TMUWT after.
                 */
                break;

        case nir_intrinsic_barrier:
                /* Emit a TSY op to get all invocations in the workgroup
                 * (actually supergroup) to block until the last invocation
                 * reaches the TSY op.
                 */
                if (c->devinfo->ver >= 42) {
                        vir_BARRIERID_dest(c, vir_reg(QFILE_MAGIC,
                                                      V3D_QPU_WADDR_SYNCB));
                } else {
                        struct qinst *sync =
                                vir_BARRIERID_dest(c,
                                                   vir_reg(QFILE_MAGIC,
                                                           V3D_QPU_WADDR_SYNCU));
                        sync->src[vir_get_implicit_uniform_src(sync)] =
                                vir_uniform_ui(c,
                                               0xffffff00 |
                                               V3D_TSY_WAIT_INC_CHECK);

                }

                /* The blocking of a TSY op only happens at the next thread
                 * switch.  No texturing may be outstanding at the time of a
                 * TSY blocking operation.
                 */
                vir_emit_thrsw(c);
                break;

        case nir_intrinsic_load_num_work_groups:
                for (int i = 0; i < 3; i++) {
                        ntq_store_dest(c, &instr->dest, i,
                                       vir_uniform(c, QUNIFORM_NUM_WORK_GROUPS,
                                                   i));
                }
                break;

        case nir_intrinsic_load_local_invocation_index:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_SHR(c, c->cs_payload[1],
                                       vir_uniform_ui(c, 32 - c->local_invocation_index_bits)));
                break;

        case nir_intrinsic_load_work_group_id:
                ntq_store_dest(c, &instr->dest, 0,
                               vir_AND(c, c->cs_payload[0],
                                       vir_uniform_ui(c, 0xffff)));
                ntq_store_dest(c, &instr->dest, 1,
                               vir_SHR(c, c->cs_payload[0],
                                       vir_uniform_ui(c, 16)));
                ntq_store_dest(c, &instr->dest, 2,
                               vir_AND(c, c->cs_payload[1],
                                       vir_uniform_ui(c, 0xffff)));
                break;

        default:
                fprintf(stderr, "Unknown intrinsic: ");
                nir_print_instr(&instr->instr, stderr);
                fprintf(stderr, "\n");
                break;
        }
}

/* Clears (activates) the execute flags for any channels whose jump target
 * matches this block.
 *
 * XXX perf: Could we be using flpush/flpop somehow for our execution channel
 * enabling?
 *
 * XXX perf: For uniform control flow, we should be able to skip c->execute
 * handling entirely.
 */
static void
ntq_activate_execute_for_block(struct v3d_compile *c)
{
        vir_set_pf(vir_XOR_dest(c, vir_reg(QFILE_NULL, 0),
                                c->execute, vir_uniform_ui(c, c->cur_block->index)),
                   V3D_QPU_PF_PUSHZ);

        vir_MOV_cond(c, V3D_QPU_COND_IFA, c->execute, vir_uniform_ui(c, 0));
}

static void
ntq_emit_uniform_if(struct v3d_compile *c, nir_if *if_stmt)
{
        nir_block *nir_else_block = nir_if_first_else_block(if_stmt);
        bool empty_else_block =
                (nir_else_block == nir_if_last_else_block(if_stmt) &&
                 exec_list_is_empty(&nir_else_block->instr_list));

        struct qblock *then_block = vir_new_block(c);
        struct qblock *after_block = vir_new_block(c);
        struct qblock *else_block;
        if (empty_else_block)
                else_block = after_block;
        else
                else_block = vir_new_block(c);

        /* Set up the flags for the IF condition (taking the THEN branch). */
        nir_alu_instr *if_condition_alu = ntq_get_alu_parent(if_stmt->condition);
        enum v3d_qpu_cond cond;
        if (!if_condition_alu ||
            !ntq_emit_comparison(c, if_condition_alu, &cond)) {
                vir_PF(c, ntq_get_src(c, if_stmt->condition, 0),
                       V3D_QPU_PF_PUSHZ);
                cond = V3D_QPU_COND_IFNA;
        }

        /* Jump to ELSE. */
        vir_BRANCH(c, cond == V3D_QPU_COND_IFA ?
                   V3D_QPU_BRANCH_COND_ALLNA :
                   V3D_QPU_BRANCH_COND_ALLA);
        vir_link_blocks(c->cur_block, else_block);
        vir_link_blocks(c->cur_block, then_block);

        /* Process the THEN block. */
        vir_set_emit_block(c, then_block);
        ntq_emit_cf_list(c, &if_stmt->then_list);

        if (!empty_else_block) {
                /* At the end of the THEN block, jump to ENDIF */
                vir_BRANCH(c, V3D_QPU_BRANCH_COND_ALWAYS);
                vir_link_blocks(c->cur_block, after_block);

                /* Emit the else block. */
                vir_set_emit_block(c, else_block);
                ntq_activate_execute_for_block(c);
                ntq_emit_cf_list(c, &if_stmt->else_list);
        }

        vir_link_blocks(c->cur_block, after_block);

        vir_set_emit_block(c, after_block);
}

static void
ntq_emit_nonuniform_if(struct v3d_compile *c, nir_if *if_stmt)
{
        nir_block *nir_else_block = nir_if_first_else_block(if_stmt);
        bool empty_else_block =
                (nir_else_block == nir_if_last_else_block(if_stmt) &&
                 exec_list_is_empty(&nir_else_block->instr_list));

        struct qblock *then_block = vir_new_block(c);
        struct qblock *after_block = vir_new_block(c);
        struct qblock *else_block;
        if (empty_else_block)
                else_block = after_block;
        else
                else_block = vir_new_block(c);

        bool was_top_level = false;
        if (c->execute.file == QFILE_NULL) {
                c->execute = vir_MOV(c, vir_uniform_ui(c, 0));
                was_top_level = true;
        }

        /* Set up the flags for the IF condition (taking the THEN branch). */
        nir_alu_instr *if_condition_alu = ntq_get_alu_parent(if_stmt->condition);
        enum v3d_qpu_cond cond;
        if (!if_condition_alu ||
            !ntq_emit_comparison(c, if_condition_alu, &cond)) {
                vir_PF(c, ntq_get_src(c, if_stmt->condition, 0),
                       V3D_QPU_PF_PUSHZ);
                cond = V3D_QPU_COND_IFNA;
        }

        /* Update the flags+cond to mean "Taking the ELSE branch (!cond) and
         * was previously active (execute Z) for updating the exec flags.
         */
        if (was_top_level) {
                cond = v3d_qpu_cond_invert(cond);
        } else {
                struct qinst *inst = vir_MOV_dest(c, vir_reg(QFILE_NULL, 0),
                                                  c->execute);
                if (cond == V3D_QPU_COND_IFA) {
                        vir_set_uf(inst, V3D_QPU_UF_NORNZ);
                } else {
                        vir_set_uf(inst, V3D_QPU_UF_ANDZ);
                        cond = V3D_QPU_COND_IFA;
                }
        }

        vir_MOV_cond(c, cond,
                     c->execute,
                     vir_uniform_ui(c, else_block->index));

        /* Jump to ELSE if nothing is active for THEN, otherwise fall
         * through.
         */
        vir_PF(c, c->execute, V3D_QPU_PF_PUSHZ);
        vir_BRANCH(c, V3D_QPU_BRANCH_COND_ALLNA);
        vir_link_blocks(c->cur_block, else_block);
        vir_link_blocks(c->cur_block, then_block);

        /* Process the THEN block. */
        vir_set_emit_block(c, then_block);
        ntq_emit_cf_list(c, &if_stmt->then_list);

        if (!empty_else_block) {
                /* Handle the end of the THEN block.  First, all currently
                 * active channels update their execute flags to point to
                 * ENDIF
                 */
                vir_PF(c, c->execute, V3D_QPU_PF_PUSHZ);
                vir_MOV_cond(c, V3D_QPU_COND_IFA, c->execute,
                             vir_uniform_ui(c, after_block->index));

                /* If everything points at ENDIF, then jump there immediately. */
                vir_PF(c, vir_XOR(c, c->execute,
                                  vir_uniform_ui(c, after_block->index)),
                       V3D_QPU_PF_PUSHZ);
                vir_BRANCH(c, V3D_QPU_BRANCH_COND_ALLA);
                vir_link_blocks(c->cur_block, after_block);
                vir_link_blocks(c->cur_block, else_block);

                vir_set_emit_block(c, else_block);
                ntq_activate_execute_for_block(c);
                ntq_emit_cf_list(c, &if_stmt->else_list);
        }

        vir_link_blocks(c->cur_block, after_block);

        vir_set_emit_block(c, after_block);
        if (was_top_level)
                c->execute = c->undef;
        else
                ntq_activate_execute_for_block(c);
}

static void
ntq_emit_if(struct v3d_compile *c, nir_if *nif)
{
        if (c->execute.file == QFILE_NULL &&
            nir_src_is_dynamically_uniform(nif->condition)) {
                ntq_emit_uniform_if(c, nif);
        } else {
                ntq_emit_nonuniform_if(c, nif);
        }
}

static void
ntq_emit_jump(struct v3d_compile *c, nir_jump_instr *jump)
{
        switch (jump->type) {
        case nir_jump_break:
                vir_PF(c, c->execute, V3D_QPU_PF_PUSHZ);
                vir_MOV_cond(c, V3D_QPU_COND_IFA, c->execute,
                             vir_uniform_ui(c, c->loop_break_block->index));
                break;

        case nir_jump_continue:
                vir_PF(c, c->execute, V3D_QPU_PF_PUSHZ);
                vir_MOV_cond(c, V3D_QPU_COND_IFA, c->execute,
                             vir_uniform_ui(c, c->loop_cont_block->index));
                break;

        case nir_jump_return:
                unreachable("All returns shouold be lowered\n");
        }
}

static void
ntq_emit_instr(struct v3d_compile *c, nir_instr *instr)
{
        switch (instr->type) {
        case nir_instr_type_deref:
                /* ignored, will be walked by the intrinsic using it. */
                break;

        case nir_instr_type_alu:
                ntq_emit_alu(c, nir_instr_as_alu(instr));
                break;

        case nir_instr_type_intrinsic:
                ntq_emit_intrinsic(c, nir_instr_as_intrinsic(instr));
                break;

        case nir_instr_type_load_const:
                ntq_emit_load_const(c, nir_instr_as_load_const(instr));
                break;

        case nir_instr_type_ssa_undef:
                ntq_emit_ssa_undef(c, nir_instr_as_ssa_undef(instr));
                break;

        case nir_instr_type_tex:
                ntq_emit_tex(c, nir_instr_as_tex(instr));
                break;

        case nir_instr_type_jump:
                ntq_emit_jump(c, nir_instr_as_jump(instr));
                break;

        default:
                fprintf(stderr, "Unknown NIR instr type: ");
                nir_print_instr(instr, stderr);
                fprintf(stderr, "\n");
                abort();
        }
}

static void
ntq_emit_block(struct v3d_compile *c, nir_block *block)
{
        nir_foreach_instr(instr, block) {
                ntq_emit_instr(c, instr);
        }
}

static void ntq_emit_cf_list(struct v3d_compile *c, struct exec_list *list);

static void
ntq_emit_loop(struct v3d_compile *c, nir_loop *loop)
{
        bool was_top_level = false;
        if (c->execute.file == QFILE_NULL) {
                c->execute = vir_MOV(c, vir_uniform_ui(c, 0));
                was_top_level = true;
        }

        struct qblock *save_loop_cont_block = c->loop_cont_block;
        struct qblock *save_loop_break_block = c->loop_break_block;

        c->loop_cont_block = vir_new_block(c);
        c->loop_break_block = vir_new_block(c);

        vir_link_blocks(c->cur_block, c->loop_cont_block);
        vir_set_emit_block(c, c->loop_cont_block);
        ntq_activate_execute_for_block(c);

        ntq_emit_cf_list(c, &loop->body);

        /* Re-enable any previous continues now, so our ANYA check below
         * works.
         *
         * XXX: Use the .ORZ flags update, instead.
         */
        vir_PF(c, vir_XOR(c,
                          c->execute,
                          vir_uniform_ui(c, c->loop_cont_block->index)),
               V3D_QPU_PF_PUSHZ);
        vir_MOV_cond(c, V3D_QPU_COND_IFA, c->execute, vir_uniform_ui(c, 0));

        vir_PF(c, c->execute, V3D_QPU_PF_PUSHZ);

        struct qinst *branch = vir_BRANCH(c, V3D_QPU_BRANCH_COND_ANYA);
        /* Pixels that were not dispatched or have been discarded should not
         * contribute to looping again.
         */
        branch->qpu.branch.msfign = V3D_QPU_MSFIGN_P;
        vir_link_blocks(c->cur_block, c->loop_cont_block);
        vir_link_blocks(c->cur_block, c->loop_break_block);

        vir_set_emit_block(c, c->loop_break_block);
        if (was_top_level)
                c->execute = c->undef;
        else
                ntq_activate_execute_for_block(c);

        c->loop_break_block = save_loop_break_block;
        c->loop_cont_block = save_loop_cont_block;

        c->loops++;
}

static void
ntq_emit_function(struct v3d_compile *c, nir_function_impl *func)
{
        fprintf(stderr, "FUNCTIONS not handled.\n");
        abort();
}

static void
ntq_emit_cf_list(struct v3d_compile *c, struct exec_list *list)
{
        foreach_list_typed(nir_cf_node, node, node, list) {
                switch (node->type) {
                case nir_cf_node_block:
                        ntq_emit_block(c, nir_cf_node_as_block(node));
                        break;

                case nir_cf_node_if:
                        ntq_emit_if(c, nir_cf_node_as_if(node));
                        break;

                case nir_cf_node_loop:
                        ntq_emit_loop(c, nir_cf_node_as_loop(node));
                        break;

                case nir_cf_node_function:
                        ntq_emit_function(c, nir_cf_node_as_function(node));
                        break;

                default:
                        fprintf(stderr, "Unknown NIR node type\n");
                        abort();
                }
        }
}

static void
ntq_emit_impl(struct v3d_compile *c, nir_function_impl *impl)
{
        ntq_setup_registers(c, &impl->registers);
        ntq_emit_cf_list(c, &impl->body);
}

static void
nir_to_vir(struct v3d_compile *c)
{
        switch (c->s->info.stage) {
        case MESA_SHADER_FRAGMENT:
                c->payload_w = vir_MOV(c, vir_reg(QFILE_REG, 0));
                c->payload_w_centroid = vir_MOV(c, vir_reg(QFILE_REG, 1));
                c->payload_z = vir_MOV(c, vir_reg(QFILE_REG, 2));

                /* XXX perf: We could set the "disable implicit point/line
                 * varyings" field in the shader record and not emit these, if
                 * they're not going to be used.
                 */
                if (c->fs_key->is_points) {
                        c->point_x = emit_fragment_varying(c, NULL, 0, 0);
                        c->point_y = emit_fragment_varying(c, NULL, 0, 0);
                } else if (c->fs_key->is_lines) {
                        c->line_x = emit_fragment_varying(c, NULL, 0, 0);
                }
                break;
        case MESA_SHADER_COMPUTE:
                /* Set up the TSO for barriers, assuming we do some. */
                if (c->devinfo->ver < 42) {
                        vir_BARRIERID_dest(c, vir_reg(QFILE_MAGIC,
                                                      V3D_QPU_WADDR_SYNC));
                }

                if (c->s->info.system_values_read &
                    ((1ull << SYSTEM_VALUE_LOCAL_INVOCATION_INDEX) |
                     (1ull << SYSTEM_VALUE_WORK_GROUP_ID))) {
                        c->cs_payload[0] = vir_MOV(c, vir_reg(QFILE_REG, 0));
                }
                if ((c->s->info.system_values_read &
                     ((1ull << SYSTEM_VALUE_WORK_GROUP_ID))) ||
                    c->s->info.cs.shared_size) {
                        c->cs_payload[1] = vir_MOV(c, vir_reg(QFILE_REG, 2));
                }

                /* Set up the division between gl_LocalInvocationIndex and
                 * wg_in_mem in the payload reg.
                 */
                int wg_size = (c->s->info.cs.local_size[0] *
                               c->s->info.cs.local_size[1] *
                               c->s->info.cs.local_size[2]);
                c->local_invocation_index_bits =
                        ffs(util_next_power_of_two(MAX2(wg_size, 64))) - 1;
                assert(c->local_invocation_index_bits <= 8);

                if (c->s->info.cs.shared_size) {
                        struct qreg wg_in_mem = vir_SHR(c, c->cs_payload[1],
                                                        vir_uniform_ui(c, 16));
                        if (c->s->info.cs.local_size[0] != 1 ||
                            c->s->info.cs.local_size[1] != 1 ||
                            c->s->info.cs.local_size[2] != 1) {
                                int wg_bits = (16 -
                                               c->local_invocation_index_bits);
                                int wg_mask = (1 << wg_bits) - 1;
                                wg_in_mem = vir_AND(c, wg_in_mem,
                                                    vir_uniform_ui(c, wg_mask));
                        }
                        struct qreg shared_per_wg =
                                vir_uniform_ui(c, c->s->info.cs.shared_size);

                        c->cs_shared_offset =
                                vir_ADD(c,
                                        vir_uniform(c, QUNIFORM_SHARED_OFFSET,0),
                                        vir_UMUL(c, wg_in_mem, shared_per_wg));
                }
                break;
        default:
                break;
        }

        if (c->s->info.stage == MESA_SHADER_FRAGMENT)
                ntq_setup_fs_inputs(c);
        else
                ntq_setup_vpm_inputs(c);

        ntq_setup_outputs(c);
        ntq_setup_uniforms(c);
        ntq_setup_registers(c, &c->s->registers);

        /* Find the main function and emit the body. */
        nir_foreach_function(function, c->s) {
                assert(strcmp(function->name, "main") == 0);
                assert(function->impl);
                ntq_emit_impl(c, function->impl);
        }
}

const nir_shader_compiler_options v3d_nir_options = {
        .lower_all_io_to_temps = true,
        .lower_extract_byte = true,
        .lower_extract_word = true,
        .lower_bfm = true,
        .lower_bitfield_insert_to_shifts = true,
        .lower_bitfield_extract_to_shifts = true,
        .lower_bitfield_reverse = true,
        .lower_bit_count = true,
        .lower_cs_local_id_from_index = true,
        .lower_pack_unorm_2x16 = true,
        .lower_pack_snorm_2x16 = true,
        .lower_pack_unorm_4x8 = true,
        .lower_pack_snorm_4x8 = true,
        .lower_unpack_unorm_4x8 = true,
        .lower_unpack_snorm_4x8 = true,
        .lower_pack_half_2x16 = true,
        .lower_unpack_half_2x16 = true,
        .lower_fdiv = true,
        .lower_find_lsb = true,
        .lower_ffma = true,
        .lower_flrp32 = true,
        .lower_fpow = true,
        .lower_fsat = true,
        .lower_fsqrt = true,
        .lower_ifind_msb = true,
        .lower_ldexp = true,
        .lower_mul_high = true,
        .lower_wpos_pntc = true,
        .native_integers = true,
};

/**
 * When demoting a shader down to single-threaded, removes the THRSW
 * instructions (one will still be inserted at v3d_vir_to_qpu() for the
 * program end).
 */
static void
vir_remove_thrsw(struct v3d_compile *c)
{
        vir_for_each_block(block, c) {
                vir_for_each_inst_safe(inst, block) {
                        if (inst->qpu.sig.thrsw)
                                vir_remove_instruction(c, inst);
                }
        }

        c->last_thrsw = NULL;
}

void
vir_emit_last_thrsw(struct v3d_compile *c)
{
        /* On V3D before 4.1, we need a TMU op to be outstanding when thread
         * switching, so disable threads if we didn't do any TMU ops (each of
         * which would have emitted a THRSW).
         */
        if (!c->last_thrsw_at_top_level && c->devinfo->ver < 41) {
                c->threads = 1;
                if (c->last_thrsw)
                        vir_remove_thrsw(c);
                return;
        }

        /* If we're threaded and the last THRSW was in conditional code, then
         * we need to emit another one so that we can flag it as the last
         * thrsw.
         */
        if (c->last_thrsw && !c->last_thrsw_at_top_level) {
                assert(c->devinfo->ver >= 41);
                vir_emit_thrsw(c);
        }

        /* If we're threaded, then we need to mark the last THRSW instruction
         * so we can emit a pair of them at QPU emit time.
         *
         * For V3D 4.x, we can spawn the non-fragment shaders already in the
         * post-last-THRSW state, so we can skip this.
         */
        if (!c->last_thrsw && c->s->info.stage == MESA_SHADER_FRAGMENT) {
                assert(c->devinfo->ver >= 41);
                vir_emit_thrsw(c);
        }

        if (c->last_thrsw)
                c->last_thrsw->is_last_thrsw = true;
}

/* There's a flag in the shader for "center W is needed for reasons other than
 * non-centroid varyings", so we just walk the program after VIR optimization
 * to see if it's used.  It should be harmless to set even if we only use
 * center W for varyings.
 */
static void
vir_check_payload_w(struct v3d_compile *c)
{
        if (c->s->info.stage != MESA_SHADER_FRAGMENT)
                return;

        vir_for_each_inst_inorder(inst, c) {
                for (int i = 0; i < vir_get_nsrc(inst); i++) {
                        if (inst->src[i].file == QFILE_REG &&
                            inst->src[i].index == 0) {
                                c->uses_center_w = true;
                                return;
                        }
                }
        }

}

void
v3d_nir_to_vir(struct v3d_compile *c)
{
        if (V3D_DEBUG & (V3D_DEBUG_NIR |
                         v3d_debug_flag_for_shader_stage(c->s->info.stage))) {
                fprintf(stderr, "%s prog %d/%d NIR:\n",
                        vir_get_stage_name(c),
                        c->program_id, c->variant_id);
                nir_print_shader(c->s, stderr);
        }

        nir_to_vir(c);

        /* Emit the last THRSW before STVPM and TLB writes. */
        vir_emit_last_thrsw(c);

        switch (c->s->info.stage) {
        case MESA_SHADER_FRAGMENT:
                emit_frag_end(c);
                break;
        case MESA_SHADER_VERTEX:
                emit_vert_end(c);
                break;
        default:
                unreachable("bad stage");
        }

        if (V3D_DEBUG & (V3D_DEBUG_VIR |
                         v3d_debug_flag_for_shader_stage(c->s->info.stage))) {
                fprintf(stderr, "%s prog %d/%d pre-opt VIR:\n",
                        vir_get_stage_name(c),
                        c->program_id, c->variant_id);
                vir_dump(c);
                fprintf(stderr, "\n");
        }

        vir_optimize(c);
        vir_lower_uniforms(c);

        vir_check_payload_w(c);

        /* XXX perf: On VC4, we do a VIR-level instruction scheduling here.
         * We used that on that platform to pipeline TMU writes and reduce the
         * number of thread switches, as well as try (mostly successfully) to
         * reduce maximum register pressure to allow more threads.  We should
         * do something of that sort for V3D -- either instruction scheduling
         * here, or delay the the THRSW and LDTMUs from our texture
         * instructions until the results are needed.
         */

        if (V3D_DEBUG & (V3D_DEBUG_VIR |
                         v3d_debug_flag_for_shader_stage(c->s->info.stage))) {
                fprintf(stderr, "%s prog %d/%d VIR:\n",
                        vir_get_stage_name(c),
                        c->program_id, c->variant_id);
                vir_dump(c);
                fprintf(stderr, "\n");
        }

        /* Attempt to allocate registers for the temporaries.  If we fail,
         * reduce thread count and try again.
         */
        int min_threads = (c->devinfo->ver >= 41) ? 2 : 1;
        struct qpu_reg *temp_registers;
        while (true) {
                bool spilled;
                temp_registers = v3d_register_allocate(c, &spilled);
                if (spilled)
                        continue;

                if (temp_registers)
                        break;

                if (c->threads == min_threads) {
                        fprintf(stderr, "Failed to register allocate at %d threads:\n",
                                c->threads);
                        vir_dump(c);
                        c->failed = true;
                        return;
                }

                c->threads /= 2;

                if (c->threads == 1)
                        vir_remove_thrsw(c);
        }

        v3d_vir_to_qpu(c, temp_registers);
}
