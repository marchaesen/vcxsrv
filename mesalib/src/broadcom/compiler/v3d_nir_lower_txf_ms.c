/*
 * Copyright © 2015 Broadcom
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
#include "compiler/nir/nir_builder.h"

/** @file v3d_nir_lower_txf_ms.c
 *
 * V3D's MSAA surfaces are laid out in UIF textures where each pixel is a 2x2
 * quad in the texture.  This pass lowers the txf_ms with a ms_index source to
 * a plain txf with the sample_index pulling out the correct texel from the
 * 2x2 quad.
 */

#define V3D_MAX_SAMPLES 4

static void
vc4_nir_lower_txf_ms_instr(struct v3d_compile *c, nir_builder *b,
                           nir_tex_instr *instr)
{
        if (instr->op != nir_texop_txf_ms)
                return;

        b->cursor = nir_before_instr(&instr->instr);

        int coord_index = nir_tex_instr_src_index(instr, nir_tex_src_coord);
        int sample_index = nir_tex_instr_src_index(instr, nir_tex_src_ms_index);
        nir_ssa_def *coord = instr->src[coord_index].src.ssa;
        nir_ssa_def *sample = instr->src[sample_index].src.ssa;

        nir_ssa_def *one = nir_imm_int(b, 1);
        coord = nir_ishl(b, coord, nir_imm_int(b, 1));
        coord = nir_vec2(b,
                         nir_iadd(b,
                                  nir_channel(b, coord, 0),
                                  nir_iand(b, sample, one)),
                         nir_iadd(b,
                                  nir_channel(b, coord, 1),
                                  nir_iand(b, nir_ushr(b, sample, one), one)));

        nir_instr_rewrite_src(&instr->instr,
                              &instr->src[nir_tex_src_coord].src,
                              nir_src_for_ssa(coord));
        nir_tex_instr_remove_src(instr, sample_index);
        instr->op = nir_texop_txf;
        instr->sampler_dim = GLSL_SAMPLER_DIM_2D;
}

void
v3d_nir_lower_txf_ms(nir_shader *s, struct v3d_compile *c)
{
        nir_foreach_function(function, s) {
                if (!function->impl)
                        continue;

                nir_builder b;
                nir_builder_init(&b, function->impl);

                nir_foreach_block(block, function->impl) {
                        nir_foreach_instr_safe(instr, block) {
                                if (instr->type != nir_instr_type_tex)
                                        continue;

                                vc4_nir_lower_txf_ms_instr(c, &b,
                                                           nir_instr_as_tex(instr));
                        }
                }

                nir_metadata_preserve(function->impl,
                                      nir_metadata_block_index |
                                      nir_metadata_dominance);
        }
}
