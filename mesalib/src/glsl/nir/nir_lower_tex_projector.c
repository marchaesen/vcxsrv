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

/*
 * This lowering pass converts the coordinate division for texture projection
 * to be done in ALU instructions instead of asking the texture operation to
 * do so.
 */

#include "nir.h"
#include "nir_builder.h"

static nir_ssa_def *
channel(nir_builder *b, nir_ssa_def *def, int c)
{
   return nir_swizzle(b, def, (unsigned[4]){c, c, c, c}, 1, false);
}

static bool
nir_lower_tex_projector_block(nir_block *block, void *void_state)
{
   nir_builder *b = void_state;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_tex)
         continue;

      nir_tex_instr *tex = nir_instr_as_tex(instr);
      nir_builder_insert_before_instr(b, &tex->instr);

      /* Find the projector in the srcs list, if present. */
      int proj_index;
      for (proj_index = 0; proj_index < tex->num_srcs; proj_index++) {
         if (tex->src[proj_index].src_type == nir_tex_src_projector)
            break;
      }
      if (proj_index == tex->num_srcs)
         continue;
      nir_ssa_def *inv_proj =
         nir_frcp(b, nir_ssa_for_src(b, tex->src[proj_index].src, 1));

      /* Walk through the sources projecting the arguments. */
      for (int i = 0; i < tex->num_srcs; i++) {
         switch (tex->src[i].src_type) {
         case nir_tex_src_coord:
         case nir_tex_src_comparitor:
            break;
         default:
            continue;
         }
         nir_ssa_def *unprojected =
            nir_ssa_for_src(b, tex->src[i].src, nir_tex_instr_src_size(tex, i));
         nir_ssa_def *projected = nir_fmul(b, unprojected, inv_proj);

         /* Array indices don't get projected, so make an new vector with the
          * coordinate's array index untouched.
          */
         if (tex->is_array && tex->src[i].src_type == nir_tex_src_coord) {
            switch (tex->coord_components) {
            case 4:
               projected = nir_vec4(b,
                                    channel(b, projected, 0),
                                    channel(b, projected, 1),
                                    channel(b, projected, 2),
                                    channel(b, unprojected, 3));
               break;
            case 3:
               projected = nir_vec3(b,
                                    channel(b, projected, 0),
                                    channel(b, projected, 1),
                                    channel(b, unprojected, 2));
               break;
            case 2:
               projected = nir_vec2(b,
                                    channel(b, projected, 0),
                                    channel(b, unprojected, 1));
               break;
            default:
               unreachable("bad texture coord count for array");
               break;
            }
         }

         nir_instr_rewrite_src(&tex->instr,
                               &tex->src[i].src,
                               nir_src_for_ssa(projected));
      }

      /* Now move the later tex sources down the array so that the projector
       * disappears.
       */
      nir_instr_rewrite_src(&tex->instr, &tex->src[proj_index].src,
                            NIR_SRC_INIT);
      for (int i = proj_index + 1; i < tex->num_srcs; i++) {
         tex->src[i-1].src_type = tex->src[i].src_type;
         nir_instr_move_src(&tex->instr, &tex->src[i-1].src, &tex->src[i].src);
      }
      tex->num_srcs--;
   }

   return true;
}

static void
nir_lower_tex_projector_impl(nir_function_impl *impl)
{
   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block(impl, nir_lower_tex_projector_block, &b);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

void
nir_lower_tex_projector(nir_shader *shader)
{
   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         nir_lower_tex_projector_impl(overload->impl);
   }
}
