/*
 * Copyright © 2016-2018 Broadcom
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

#include "v3d_compiler.h"
#include "compiler/nir/nir_deref.h"

/* We don't do any address packing. */
#define __gen_user_data void
#define __gen_address_type uint32_t
#define __gen_address_offset(reloc) (*reloc)
#define __gen_emit_reloc(cl, reloc)
#include "cle/v3d_packet_v41_pack.h"

static void
vir_TMU_WRITE(struct v3d_compile *c, enum v3d_qpu_waddr waddr, struct qreg val,
              int *tmu_writes)
{
        /* XXX perf: We should figure out how to merge ALU operations
         * producing the val with this MOV, when possible.
         */
        vir_MOV_dest(c, vir_reg(QFILE_MAGIC, waddr), val);

        (*tmu_writes)++;
}

static void
vir_WRTMUC(struct v3d_compile *c, enum quniform_contents contents, uint32_t data)
{
        struct qinst *inst = vir_NOP(c);
        inst->qpu.sig.wrtmuc = true;
        inst->uniform = vir_get_uniform_index(c, contents, data);
}

static const struct V3D41_TMU_CONFIG_PARAMETER_1 p1_unpacked_default = {
        .per_pixel_mask_enable = true,
};

static const struct V3D41_TMU_CONFIG_PARAMETER_2 p2_unpacked_default = {
        .op = V3D_TMU_OP_REGULAR,
};

void
v3d40_vir_emit_tex(struct v3d_compile *c, nir_tex_instr *instr)
{
        unsigned unit = instr->texture_index;
        int tmu_writes = 0;

        struct V3D41_TMU_CONFIG_PARAMETER_0 p0_unpacked = {
        };

        struct V3D41_TMU_CONFIG_PARAMETER_1 p1_unpacked = {
                .output_type_32_bit = (c->key->tex[unit].return_size == 32 &&
                                       !instr->is_shadow),

                .unnormalized_coordinates = (instr->sampler_dim ==
                                             GLSL_SAMPLER_DIM_RECT),
        };

        struct V3D41_TMU_CONFIG_PARAMETER_2 p2_unpacked = {
                .op = V3D_TMU_OP_REGULAR,

                .gather_mode = instr->op == nir_texop_tg4,
                .gather_component = instr->component,

                .coefficient_mode = instr->op == nir_texop_txd,

                .disable_autolod = instr->op == nir_texop_tg4
        };

        int non_array_components = instr->coord_components - instr->is_array;
        struct qreg s;

        for (unsigned i = 0; i < instr->num_srcs; i++) {
                switch (instr->src[i].src_type) {
                case nir_tex_src_coord:
                        /* S triggers the lookup, so save it for the end. */
                        s = ntq_get_src(c, instr->src[i].src, 0);

                        if (non_array_components > 1) {
                                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUT,
                                              ntq_get_src(c, instr->src[i].src,
                                                          1), &tmu_writes);
                        }
                        if (non_array_components > 2) {
                                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUR,
                                              ntq_get_src(c, instr->src[i].src,
                                                          2), &tmu_writes);
                        }

                        if (instr->is_array) {
                                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUI,
                                              ntq_get_src(c, instr->src[i].src,
                                                          instr->coord_components - 1),
                                              &tmu_writes);
                        }
                        break;

                case nir_tex_src_bias:
                        vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUB,
                                      ntq_get_src(c, instr->src[i].src, 0),
                                      &tmu_writes);
                        break;

                case nir_tex_src_lod:
                        vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUB,
                                      ntq_get_src(c, instr->src[i].src, 0),
                                      &tmu_writes);

                        if (instr->op != nir_texop_txf)
                                p2_unpacked.disable_autolod = true;
                        break;

                case nir_tex_src_comparator:
                        vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUDREF,
                                      ntq_get_src(c, instr->src[i].src, 0),
                                      &tmu_writes);
                        break;

                case nir_tex_src_offset: {
                        if (nir_src_is_const(instr->src[i].src)) {
                                p2_unpacked.offset_s = nir_src_comp_as_int(instr->src[i].src, 0);
                                if (instr->coord_components >= 2)
                                        p2_unpacked.offset_t =
                                                nir_src_comp_as_int(instr->src[i].src, 1);
                                if (non_array_components >= 3)
                                        p2_unpacked.offset_r =
                                                nir_src_comp_as_int(instr->src[i].src, 2);
                        } else {
                                struct qreg mask = vir_uniform_ui(c, 0xf);
                                struct qreg x, y, offset;

                                x = vir_AND(c, ntq_get_src(c, instr->src[i].src,
                                                           0), mask);
                                y = vir_AND(c, ntq_get_src(c, instr->src[i].src,
                                                           1), mask);
                                offset = vir_OR(c, x,
                                                vir_SHL(c, y,
                                                        vir_uniform_ui(c, 4)));

                                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUOFF,
                                              offset, &tmu_writes);
                        }
                        break;
                }

                default:
                        unreachable("unknown texture source");
                }
        }

        /* Limit the number of channels returned to both how many the NIR
         * instruction writes and how many the instruction could produce.
         */
        assert(instr->dest.is_ssa);
        p0_unpacked.return_words_of_texture_data =
                nir_ssa_def_components_read(&instr->dest.ssa);

        /* Word enables can't ask for more channels than the output type could
         * provide (2 for f16, 4 for 32-bit).
         */
        assert(!p1_unpacked.output_type_32_bit ||
               p0_unpacked.return_words_of_texture_data < (1 << 4));
        assert(p1_unpacked.output_type_32_bit ||
               p0_unpacked.return_words_of_texture_data < (1 << 2));

        assert(p0_unpacked.return_words_of_texture_data != 0);

        uint32_t p0_packed;
        V3D41_TMU_CONFIG_PARAMETER_0_pack(NULL,
                                          (uint8_t *)&p0_packed,
                                          &p0_unpacked);

        uint32_t p1_packed;
        V3D41_TMU_CONFIG_PARAMETER_1_pack(NULL,
                                          (uint8_t *)&p1_packed,
                                          &p1_unpacked);

        uint32_t p2_packed;
        V3D41_TMU_CONFIG_PARAMETER_2_pack(NULL,
                                          (uint8_t *)&p2_packed,
                                          &p2_unpacked);

        /* Load unit number into the high bits of the texture or sampler
         * address field, which will be be used by the driver to decide which
         * texture to put in the actual address field.
         */
        p0_packed |= unit << 24;
        p1_packed |= unit << 24;

        vir_WRTMUC(c, QUNIFORM_TMU_CONFIG_P0, p0_packed);
        /* XXX perf: Can we skip p1 setup for txf ops? */
        vir_WRTMUC(c, QUNIFORM_TMU_CONFIG_P1, p1_packed);
        if (memcmp(&p2_unpacked, &p2_unpacked_default, sizeof(p2_unpacked)) != 0)
                vir_WRTMUC(c, QUNIFORM_CONSTANT, p2_packed);

        if (instr->op == nir_texop_txf) {
                assert(instr->sampler_dim != GLSL_SAMPLER_DIM_CUBE);
                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUSF, s, &tmu_writes);
        } else if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUSCM, s, &tmu_writes);
        } else {
                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUS, s, &tmu_writes);
        }

        vir_emit_thrsw(c);

        /* The input FIFO has 16 slots across all threads, so make sure we
         * don't overfill our allocation.
         */
        while (tmu_writes > 16 / c->threads)
                c->threads /= 2;

        for (int i = 0; i < 4; i++) {
                if (p0_unpacked.return_words_of_texture_data & (1 << i))
                        ntq_store_dest(c, &instr->dest, i, vir_LDTMU(c));
        }
}

static void
type_size_align_1(const struct glsl_type *type, unsigned *size, unsigned *align)
{
        *size = 1;
        *align = 1;
}

static uint32_t
v3d40_image_load_store_tmu_op(nir_intrinsic_instr *instr)
{
        switch (instr->intrinsic) {
        case nir_intrinsic_image_deref_load:
        case nir_intrinsic_image_deref_store:
                return V3D_TMU_OP_REGULAR;
        case nir_intrinsic_image_deref_atomic_add:
                return v3d_get_op_for_atomic_add(instr, 3);
        case nir_intrinsic_image_deref_atomic_imin:
                return V3D_TMU_OP_WRITE_SMIN;
        case nir_intrinsic_image_deref_atomic_umin:
                return V3D_TMU_OP_WRITE_UMIN_FULL_L1_CLEAR;
        case nir_intrinsic_image_deref_atomic_imax:
                return V3D_TMU_OP_WRITE_SMAX;
        case nir_intrinsic_image_deref_atomic_umax:
                return V3D_TMU_OP_WRITE_UMAX;
        case nir_intrinsic_image_deref_atomic_and:
                return V3D_TMU_OP_WRITE_AND_READ_INC;
        case nir_intrinsic_image_deref_atomic_or:
                return V3D_TMU_OP_WRITE_OR_READ_DEC;
        case nir_intrinsic_image_deref_atomic_xor:
                return V3D_TMU_OP_WRITE_XOR_READ_NOT;
        case nir_intrinsic_image_deref_atomic_exchange:
                return V3D_TMU_OP_WRITE_XCHG_READ_FLUSH;
        case nir_intrinsic_image_deref_atomic_comp_swap:
                return V3D_TMU_OP_WRITE_CMPXCHG_READ_FLUSH;
        default:
                unreachable("unknown image intrinsic");
        };
}

void
v3d40_vir_emit_image_load_store(struct v3d_compile *c,
                                nir_intrinsic_instr *instr)
{
        nir_variable *var = nir_intrinsic_get_var(instr, 0);
        const struct glsl_type *sampler_type = glsl_without_array(var->type);
        unsigned unit = (var->data.driver_location +
                         nir_deref_instr_get_const_offset(nir_src_as_deref(instr->src[0]),
                                                          type_size_align_1));
        int tmu_writes = 0;

        struct V3D41_TMU_CONFIG_PARAMETER_0 p0_unpacked = {
        };

        struct V3D41_TMU_CONFIG_PARAMETER_1 p1_unpacked = {
                .per_pixel_mask_enable = true,
                .output_type_32_bit = v3d_gl_format_is_return_32(var->data.image.format),
        };

        struct V3D41_TMU_CONFIG_PARAMETER_2 p2_unpacked = { 0 };

        p2_unpacked.op = v3d40_image_load_store_tmu_op(instr);

        /* If we were able to replace atomic_add for an inc/dec, then we
         * need/can to do things slightly different, like not loading the
         * amount to add/sub, as that is implicit.
         */
        bool atomic_add_replaced = (instr->intrinsic == nir_intrinsic_image_deref_atomic_add &&
                                    (p2_unpacked.op == V3D_TMU_OP_WRITE_AND_READ_INC ||
                                     p2_unpacked.op == V3D_TMU_OP_WRITE_OR_READ_DEC));

        bool is_1d = false;
        switch (glsl_get_sampler_dim(sampler_type)) {
        case GLSL_SAMPLER_DIM_1D:
                is_1d = true;
                break;
        case GLSL_SAMPLER_DIM_BUF:
                break;
        case GLSL_SAMPLER_DIM_2D:
        case GLSL_SAMPLER_DIM_RECT:
                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUT,
                              ntq_get_src(c, instr->src[1], 1), &tmu_writes);
                break;
        case GLSL_SAMPLER_DIM_3D:
        case GLSL_SAMPLER_DIM_CUBE:
                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUT,
                              ntq_get_src(c, instr->src[1], 1), &tmu_writes);
                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUR,
                              ntq_get_src(c, instr->src[1], 2), &tmu_writes);
                break;
        default:
                unreachable("bad image sampler dim");
        }

        if (glsl_sampler_type_is_array(sampler_type)) {
                vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUI,
                              ntq_get_src(c, instr->src[1],
                                          is_1d ? 1 : 2), &tmu_writes);
        }

        /* Limit the number of channels returned to both how many the NIR
         * instruction writes and how many the instruction could produce.
         */
        uint32_t instr_return_channels = nir_intrinsic_dest_components(instr);
        if (!p1_unpacked.output_type_32_bit)
                instr_return_channels = (instr_return_channels + 1) / 2;

        p0_unpacked.return_words_of_texture_data =
                (1 << instr_return_channels) - 1;

        uint32_t p0_packed;
        V3D41_TMU_CONFIG_PARAMETER_0_pack(NULL,
                                          (uint8_t *)&p0_packed,
                                          &p0_unpacked);

        uint32_t p1_packed;
        V3D41_TMU_CONFIG_PARAMETER_1_pack(NULL,
                                          (uint8_t *)&p1_packed,
                                          &p1_unpacked);

        uint32_t p2_packed;
        V3D41_TMU_CONFIG_PARAMETER_2_pack(NULL,
                                          (uint8_t *)&p2_packed,
                                          &p2_unpacked);

        /* Load unit number into the high bits of the texture or sampler
         * address field, which will be be used by the driver to decide which
         * texture to put in the actual address field.
         */
        p0_packed |= unit << 24;

        vir_WRTMUC(c, QUNIFORM_IMAGE_TMU_CONFIG_P0, p0_packed);
        if (memcmp(&p1_unpacked, &p1_unpacked_default, sizeof(p1_unpacked)) != 0)
                vir_WRTMUC(c, QUNIFORM_CONSTANT, p1_packed);
        if (memcmp(&p2_unpacked, &p2_unpacked_default, sizeof(p2_unpacked)) != 0)
                vir_WRTMUC(c, QUNIFORM_CONSTANT, p2_packed);

        /* Emit the data writes for atomics or image store. */
        if (instr->intrinsic != nir_intrinsic_image_deref_load &&
            !atomic_add_replaced) {
                /* Vector for stores, or first atomic argument */
                struct qreg src[4];
                for (int i = 0; i < nir_intrinsic_src_components(instr, 3); i++) {
                        src[i] = ntq_get_src(c, instr->src[3], i);
                        vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUD, src[i],
                                      &tmu_writes);
                }

                /* Second atomic argument */
                if (instr->intrinsic ==
                    nir_intrinsic_image_deref_atomic_comp_swap) {
                        vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUD,
                                      ntq_get_src(c, instr->src[4], 0),
                                      &tmu_writes);
                }
        }

        if (vir_in_nonuniform_control_flow(c) &&
            instr->intrinsic != nir_intrinsic_image_deref_load) {
           vir_set_pf(vir_MOV_dest(c, vir_nop_reg(), c->execute),
                      V3D_QPU_PF_PUSHZ);
        }

        vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUSF, ntq_get_src(c, instr->src[1], 0),
                      &tmu_writes);

        if (vir_in_nonuniform_control_flow(c) &&
            instr->intrinsic != nir_intrinsic_image_deref_load) {
           struct qinst *last_inst= (struct  qinst *)c->cur_block->instructions.prev;
           vir_set_cond(last_inst, V3D_QPU_COND_IFA);
        }

        vir_emit_thrsw(c);

        /* The input FIFO has 16 slots across all threads, so make sure we
         * don't overfill our allocation.
         */
        while (tmu_writes > 16 / c->threads)
                c->threads /= 2;

        for (int i = 0; i < 4; i++) {
                if (p0_unpacked.return_words_of_texture_data & (1 << i))
                        ntq_store_dest(c, &instr->dest, i, vir_LDTMU(c));
        }

        if (nir_intrinsic_dest_components(instr) == 0)
                vir_TMUWT(c);

        if (instr->intrinsic != nir_intrinsic_image_deref_load)
                c->tmu_dirty_rcl = true;
}
