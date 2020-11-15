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
#include "cle/v3d_packet_v33_pack.h"

void
v3d33_vir_emit_tex(struct v3d_compile *c, nir_tex_instr *instr)
{
        unsigned unit = instr->texture_index;

        struct V3D33_TEXTURE_UNIFORM_PARAMETER_0_CFG_MODE1 p0_unpacked = {
                V3D33_TEXTURE_UNIFORM_PARAMETER_0_CFG_MODE1_header,

                .fetch_sample_mode = instr->op == nir_texop_txf,
        };

        struct V3D33_TEXTURE_UNIFORM_PARAMETER_1_CFG_MODE1 p1_unpacked = {
        };

        switch (instr->sampler_dim) {
        case GLSL_SAMPLER_DIM_1D:
                if (instr->is_array)
                        p0_unpacked.lookup_type = TEXTURE_1D_ARRAY;
                else
                        p0_unpacked.lookup_type = TEXTURE_1D;
                break;
        case GLSL_SAMPLER_DIM_2D:
        case GLSL_SAMPLER_DIM_RECT:
                if (instr->is_array)
                        p0_unpacked.lookup_type = TEXTURE_2D_ARRAY;
                else
                        p0_unpacked.lookup_type = TEXTURE_2D;
                break;
        case GLSL_SAMPLER_DIM_3D:
                p0_unpacked.lookup_type = TEXTURE_3D;
                break;
        case GLSL_SAMPLER_DIM_CUBE:
                p0_unpacked.lookup_type = TEXTURE_CUBE_MAP;
                break;
        default:
                unreachable("Bad sampler type");
        }

        struct qreg coords[5];
        int next_coord = 0;
        for (unsigned i = 0; i < instr->num_srcs; i++) {
                switch (instr->src[i].src_type) {
                case nir_tex_src_coord:
                        for (int j = 0; j < instr->coord_components; j++) {
                                coords[next_coord++] =
                                        ntq_get_src(c, instr->src[i].src, j);
                        }
                        if (instr->coord_components < 2)
                                coords[next_coord++] = vir_uniform_f(c, 0.5);
                        break;
                case nir_tex_src_bias:
                        coords[next_coord++] =
                                ntq_get_src(c, instr->src[i].src, 0);

                        p0_unpacked.bias_supplied = true;
                        break;
                case nir_tex_src_lod:
                        coords[next_coord++] =
                                vir_FADD(c,
                                         ntq_get_src(c, instr->src[i].src, 0),
                                         vir_uniform(c, QUNIFORM_TEXTURE_FIRST_LEVEL,
                                                     unit));

                        if (instr->op != nir_texop_txf &&
                            instr->op != nir_texop_tg4) {
                                p0_unpacked.disable_autolod_use_bias_only = true;
                        }
                        break;
                case nir_tex_src_comparator:
                        coords[next_coord++] =
                                ntq_get_src(c, instr->src[i].src, 0);

                        p0_unpacked.shadow = true;
                        break;

                case nir_tex_src_offset: {
                        p0_unpacked.texel_offset_for_s_coordinate =
                                nir_src_comp_as_int(instr->src[i].src, 0);

                        if (instr->coord_components >= 2)
                                p0_unpacked.texel_offset_for_t_coordinate =
                                        nir_src_comp_as_int(instr->src[i].src, 1);

                        if (instr->coord_components >= 3)
                                p0_unpacked.texel_offset_for_r_coordinate =
                                        nir_src_comp_as_int(instr->src[i].src, 2);
                        break;
                }

                default:
                        unreachable("unknown texture source");
                }
        }

        /* Limit the number of channels returned to both how many the NIR
         * instruction writes and how many the instruction could produce.
         */
        p1_unpacked.return_words_of_texture_data =
                instr->dest.is_ssa ?
                nir_ssa_def_components_read(&instr->dest.ssa) :
                (1 << instr->dest.reg.reg->num_components) - 1;

        uint32_t p0_packed;
        V3D33_TEXTURE_UNIFORM_PARAMETER_0_CFG_MODE1_pack(NULL,
                                                         (uint8_t *)&p0_packed,
                                                         &p0_unpacked);

        uint32_t p1_packed;
        V3D33_TEXTURE_UNIFORM_PARAMETER_1_CFG_MODE1_pack(NULL,
                                                         (uint8_t *)&p1_packed,
                                                         &p1_unpacked);
        /* Load unit number into the address field, which will be be used by
         * the driver to decide which texture to put in the actual address
         * field.
         */
        p1_packed |= unit << 5;

        /* There is no native support for GL texture rectangle coordinates, so
         * we have to rescale from ([0, width], [0, height]) to ([0, 1], [0,
         * 1]).
         */
        if (instr->sampler_dim == GLSL_SAMPLER_DIM_RECT) {
                coords[0] = vir_FMUL(c, coords[0],
                                     vir_uniform(c, QUNIFORM_TEXRECT_SCALE_X,
                                                 unit));
                coords[1] = vir_FMUL(c, coords[1],
                                     vir_uniform(c, QUNIFORM_TEXRECT_SCALE_Y,
                                                 unit));
        }

        int texture_u[] = {
                vir_get_uniform_index(c, QUNIFORM_TEXTURE_CONFIG_P0_0 + unit, p0_packed),
                vir_get_uniform_index(c, QUNIFORM_TEXTURE_CONFIG_P1, p1_packed),
        };

        for (int i = 0; i < next_coord; i++) {
                struct qreg dst;

                if (i == next_coord - 1)
                        dst = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMUL);
                else
                        dst = vir_reg(QFILE_MAGIC, V3D_QPU_WADDR_TMU);

                struct qinst *tmu = vir_MOV_dest(c, dst, coords[i]);

                if (i < 2)
                        tmu->uniform = texture_u[i];
        }

        vir_emit_thrsw(c);

        for (int i = 0; i < 4; i++) {
                if (p1_unpacked.return_words_of_texture_data & (1 << i))
                        ntq_store_dest(c, &instr->dest, i, vir_LDTMU(c));
        }
}
