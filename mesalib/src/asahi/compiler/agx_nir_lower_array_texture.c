/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2020 Collabora Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "agx_compiler.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_builtin_builder.h"

/*
 * NIR indexes into array textures with unclamped floats (integer for txf). AGX
 * requires the index to be a clamped integer. Lower tex_src_coord into
 * tex_src_backend1 for array textures by type-converting and clamping.
 */
static bool
lower_array_texture(nir_builder *b, nir_instr *instr, UNUSED void *data)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   b->cursor = nir_before_instr(instr);

   if (!tex->is_array || nir_tex_instr_is_query(tex))
      return false;

   /* Get the coordinates */
   int coord_idx = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   nir_ssa_def *coord = tex->src[coord_idx].src.ssa;
   unsigned nr = nir_src_num_components(tex->src[coord_idx].src);

   /* The layer is always the last component of the NIR coordinate */
   unsigned lidx = nr - 1;
   nir_ssa_def *layer = nir_channel(b, coord, lidx);

   /* Round layer to nearest even */
   if (tex->op != nir_texop_txf)
      layer = nir_f2u32(b, nir_fround_even(b, layer));

   /* Clamp to max layer = (# of layers - 1) for out-of-bounds handling */
   nir_ssa_def *txs = nir_get_texture_size(b, tex);
   nir_ssa_def *nr_layers = nir_channel(b, txs, lidx);
   layer = nir_umin(b, layer, nir_iadd_imm(b, nr_layers, -1));

   nir_tex_instr_remove_src(tex, coord_idx);
   nir_tex_instr_add_src(tex, nir_tex_src_backend1,
                         nir_src_for_ssa(nir_vector_insert_imm(b, coord, layer,
                                                                  lidx)));
   return true;
}

bool
agx_nir_lower_array_texture(nir_shader *s)
{
   return nir_shader_instructions_pass(s, lower_array_texture,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance, NULL);
}
