/*
 * Copyright (C) 2022 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

#include "agx_compiler.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

#define AGX_TEXTURE_DESC_STRIDE 24

static nir_ssa_def *
texture_descriptor_ptr(nir_builder *b, nir_tex_instr *tex)
{
   /* For bindless, we store the descriptor pointer in the texture handle */
   int handle_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
   if (handle_idx >= 0)
      return tex->src[handle_idx].src.ssa;

   /* For non-bindless, compute from the texture index, offset, and table */
   unsigned base_B = tex->texture_index * AGX_TEXTURE_DESC_STRIDE;
   nir_ssa_def *offs = nir_imm_int(b, base_B);

   int offs_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_offset);
   if (offs_idx >= 0) {
      nir_ssa_def *offset_src = tex->src[offs_idx].src.ssa;
      offs = nir_iadd(b, offs,
                      nir_imul_imm(b, offset_src, AGX_TEXTURE_DESC_STRIDE));
   }

   return nir_iadd(b, nir_load_texture_base_agx(b), nir_u2u64(b, offs));
}

static nir_ssa_def *
agx_txs(nir_builder *b, nir_tex_instr *tex)
{
   nir_ssa_def *ptr = texture_descriptor_ptr(b, tex);
   nir_ssa_def *comp[4] = { NULL };

   nir_ssa_def *desc = nir_load_global_constant(b, ptr, 8, 4, 32);
   nir_ssa_def *w0 = nir_channel(b, desc, 0);
   nir_ssa_def *w1 = nir_channel(b, desc, 1);
   nir_ssa_def *w3 = nir_channel(b, desc, 3);

   /* Width minus 1: bits [28, 42) */
   nir_ssa_def *width_m1 = nir_ior(b, nir_ushr_imm(b, w0, 28),
                                   nir_ishl_imm(b, nir_iand_imm(b, w1,
                                         BITFIELD_MASK(14 - 4)), 4));
   /* Height minus 1: bits [42, 56) */
   nir_ssa_def *height_m1 = nir_iand_imm(b, nir_ushr_imm(b, w1, 42 - 32),
                                            BITFIELD_MASK(14));

   /* Depth minus 1: bits [110, 124) */
   nir_ssa_def *depth_m1 = nir_iand_imm(b, nir_ushr_imm(b, w3, 110 - 96),
                                            BITFIELD_MASK(14));

   /* First level: bits [56, 60) */
   nir_ssa_def *lod = nir_iand_imm(b, nir_ushr_imm(b, w1, 56 - 32),
                                      BITFIELD_MASK(4));

   /* Add LOD offset to first level to get the interesting LOD */
   int lod_idx = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (lod_idx >= 0)
      lod = nir_iadd(b, lod, nir_ssa_for_src(b, tex->src[lod_idx].src, 1));

   /* Add 1 to width-1, height-1 to get base dimensions */
   nir_ssa_def *width = nir_iadd_imm(b, width_m1, 1);
   nir_ssa_def *height = nir_iadd_imm(b, height_m1, 1);
   nir_ssa_def *depth = nir_iadd_imm(b, depth_m1, 1);

   /* How we finish depends on the size of the result */
   unsigned nr_comps = nir_dest_num_components(tex->dest);
   assert(nr_comps <= 3);

   /* Adjust for LOD, do not adjust array size */
   assert(!(nr_comps <= 1 && tex->is_array));
   width = nir_imax(b, nir_ushr(b, width, lod), nir_imm_int(b, 1));

   if (!(nr_comps == 2 && tex->is_array))
      height = nir_imax(b, nir_ushr(b, height, lod), nir_imm_int(b, 1));

   if (!(nr_comps == 3 && tex->is_array))
      depth = nir_imax(b, nir_ushr(b, depth, lod), nir_imm_int(b, 1));

   comp[0] = width;
   comp[1] = height;
   comp[2] = depth;

   return nir_vec(b, comp, nr_comps);
}

static bool
lower_txs(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   b->cursor = nir_before_instr(instr);

   if (tex->op != nir_texop_txs)
      return false;

   nir_ssa_def *res = agx_txs(b, tex);
   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, res, instr);
   nir_instr_remove(instr);
   return true;
}

/*
 * Lower txs.
 */
bool
agx_lower_resinfo(nir_shader *s)
{
   return nir_shader_instructions_pass(s, lower_txs,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance, NULL);
}
