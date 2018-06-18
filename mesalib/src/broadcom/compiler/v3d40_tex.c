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
        vir_MOV_dest(c, vir_reg(QFILE_MAGIC, waddr), val);

        (*tmu_writes)++;
}

static void
vir_WRTMUC(struct v3d_compile *c, enum quniform_contents contents, uint32_t data)
{
        struct qinst *inst = vir_NOP(c);
        inst->qpu.sig.wrtmuc = true;
        inst->has_implicit_uniform = true;
        inst->src[0] = vir_uniform(c, contents, data);
}

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

                        if (instr->op != nir_texop_txf &&
                            instr->op != nir_texop_tg4) {
                                p2_unpacked.disable_autolod = true;
                        }
                        break;

                case nir_tex_src_comparator:
                        vir_TMU_WRITE(c, V3D_QPU_WADDR_TMUDREF,
                                      ntq_get_src(c, instr->src[i].src, 0),
                                      &tmu_writes);
                        break;

                case nir_tex_src_offset: {
                        nir_const_value *offset =
                                nir_src_as_const_value(instr->src[i].src);

                        p2_unpacked.offset_s = offset->i32[0];
                        if (instr->coord_components >= 2)
                                p2_unpacked.offset_t = offset->i32[1];
                        if (instr->coord_components >= 3)
                                p2_unpacked.offset_r = offset->i32[2];
                        break;
                }

                default:
                        unreachable("unknown texture source");
                }
        }

        /* Limit the number of channels returned to both how many the NIR
         * instruction writes and how many the instruction could produce.
         */
        uint32_t instr_return_channels = nir_tex_instr_dest_size(instr);
        if (!p1_unpacked.output_type_32_bit)
                instr_return_channels = (instr_return_channels + 1) / 2;

        p0_unpacked.return_words_of_texture_data =
                (1 << MIN2(instr_return_channels,
                           c->key->tex[unit].return_channels)) - 1;

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
        vir_WRTMUC(c, QUNIFORM_TMU_CONFIG_P1, p1_packed);
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

        struct qreg return_values[4];
        for (int i = 0; i < 4; i++) {
                /* Swizzling .zw of an RG texture should give undefined
                 * results, not crash the compiler.
                 */
                if (p0_unpacked.return_words_of_texture_data & (1 << i))
                        return_values[i] = vir_LDTMU(c);
                else
                        return_values[i] = c->undef;
        }

        for (int i = 0; i < nir_tex_instr_dest_size(instr); i++) {
                struct qreg chan;

                if (!p1_unpacked.output_type_32_bit) {
                        STATIC_ASSERT(PIPE_SWIZZLE_X == 0);
                        chan = return_values[i / 2];

                        if (nir_alu_type_get_base_type(instr->dest_type) ==
                            nir_type_float) {
                                enum v3d_qpu_input_unpack unpack;
                                if (i & 1)
                                        unpack = V3D_QPU_UNPACK_H;
                                else
                                        unpack = V3D_QPU_UNPACK_L;

                                chan = vir_FMOV(c, chan);
                                vir_set_unpack(c->defs[chan.index], 0, unpack);
                        } else {
                                /* If we're unpacking the low field, shift it
                                 * up to the top first.
                                 */
                                if ((i & 1) == 0) {
                                        chan = vir_SHL(c, chan,
                                                       vir_uniform_ui(c, 16));
                                }

                                /* Do proper sign extension to a 32-bit int. */
                                if (nir_alu_type_get_base_type(instr->dest_type) ==
                                    nir_type_int) {
                                        chan = vir_ASR(c, chan,
                                                       vir_uniform_ui(c, 16));
                                } else {
                                        chan = vir_SHR(c, chan,
                                                       vir_uniform_ui(c, 16));
                                }
                        }
                } else {
                        chan = vir_MOV(c, return_values[i]);
                }
                ntq_store_dest(c, &instr->dest, i, chan);
        }
}
