/*
 * Copyright © 2025 Collabora, Ltd.
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

#include "nir.h"
#include "nir_worklist.h"

struct helper_state {
   BITSET_WORD *needs_helpers;
   nir_instr_worklist *worklist;
   nir_instr_worklist *tex_instrs;
   bool no_add_divergence;
};

static inline bool
def_needs_helpers(nir_def *def, void *_data)
{
   struct helper_state *hs = _data;
   return BITSET_TEST(hs->needs_helpers, def->index);
}

static inline bool
set_src_needs_helpers(nir_src *src, void *_data)
{
   struct helper_state *hs = _data;
   if (!BITSET_TEST(hs->needs_helpers, src->ssa->index)) {
      BITSET_SET(hs->needs_helpers, src->ssa->index);
      nir_instr_worklist_push_tail(hs->worklist, src->ssa->parent_instr);
   }
   return true;
}

bool
nir_opt_tex_skip_helpers(nir_shader *shader, bool no_add_divergence)
{
   /* This is only useful on fragment shaders */
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   /* This only works if functions are inlined */
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   struct helper_state hs = {
      .needs_helpers = rzalloc_array(NULL, BITSET_WORD,
                                     BITSET_WORDS(impl->ssa_alloc)),
      .worklist = nir_instr_worklist_create(),
      .tex_instrs = nir_instr_worklist_create(),
      .no_add_divergence = no_add_divergence,
   };

   /* First, add subgroup ops and anything that might cause side effects */
   nir_foreach_block(block, impl) {
      /* Control-flow is hard.  Given that this is only for texture ops, we
       * can afford to be conservative and assume that any control-flow is
       * potentially going to affect helpers.
       */
      nir_if *nif = nir_block_get_following_if(block);
      if (nif != NULL)
         set_src_needs_helpers(&nif->condition, &hs);

      nir_foreach_instr(instr, block) {
         switch (instr->type) {
         case nir_instr_type_tex: {
            nir_tex_instr *tex = nir_instr_as_tex(instr);

            /* Stash texture instructions so we don't have to walk the whole
             * shader again just to set the skip_helpers bit.
             */
            nir_instr_worklist_push_tail(hs.tex_instrs, instr);

            for (uint32_t i = 0; i < tex->num_srcs; i++) {
               switch (tex->src[i].src_type) {
               case nir_tex_src_coord:
               case nir_tex_src_projector:
                  if (nir_tex_instr_has_implicit_derivative(tex))
                     set_src_needs_helpers(&tex->src[i].src, &hs);
                  break;

               case nir_tex_src_texture_deref:
               case nir_tex_src_sampler_deref:
               case nir_tex_src_texture_offset:
               case nir_tex_src_sampler_offset:
               case nir_tex_src_texture_handle:
               case nir_tex_src_sampler_handle:
               case nir_tex_src_sampler_deref_intrinsic:
               case nir_tex_src_texture_deref_intrinsic:
               case nir_tex_src_backend1:
               case nir_tex_src_backend2:
                  /* Anything which affects which descriptor is used by
                   * the texture instruction is considered a possible
                   * side-effect.  If, for instance, the array index or
                   * bindless handle is wrong, that can cause us to use an
                   * invalid descriptor or fault.  This includes back-end
                   * source types because we don't know what they are.
                   */
                  set_src_needs_helpers(&tex->src[i].src, &hs);
                  break;

               default:
                  break;
               }
            }
            break;
         }

         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (nir_intrinsic_has_semantic(intr, NIR_INTRINSIC_SUBGROUP)) {
               nir_foreach_src(instr, set_src_needs_helpers, &hs);
            } else {
               /* All I/O addresses need helpers because getting them wrong
                * may cause a fault.
                */
               nir_src *io_index_src = nir_get_io_index_src(intr);
               if (io_index_src != NULL)
                  set_src_needs_helpers(io_index_src, &hs);
               nir_src *io_offset_src = nir_get_io_offset_src(intr);
               if (io_offset_src != NULL)
                  set_src_needs_helpers(io_offset_src, &hs);
            }
            break;
         }

         default:
            break;
         }
      }
   }

   bool progress = false;

   /* We only need to run the worklist if we have textures */
   if (!nir_instr_worklist_is_empty(hs.tex_instrs)) {
      while (!nir_instr_worklist_is_empty(hs.worklist)) {
         nir_instr *instr = nir_instr_worklist_pop_head(hs.worklist);
         assert(nir_foreach_def(instr, def_needs_helpers, &hs));
         nir_foreach_src(instr, set_src_needs_helpers, &hs);
      }

      while (!nir_instr_worklist_is_empty(hs.tex_instrs)) {
         nir_instr *instr = nir_instr_worklist_pop_head(hs.tex_instrs);
         nir_tex_instr *tex = nir_instr_as_tex(instr);

         /* If a texture uniform, we don't want to set skip_helpers because
          * then it might not be uniform if the helpers don't fetch.  Also,
          * for uniform texture results, we shouldn't be burning any more
          * memory by executing the helper pixels unless the hardware is
          * really dumb.
          *
          * NOTE: Any texture instruction that doesn't have skip_helpers set
          * then relies on correct parameters in those helper invocations.
          * If we're depending on those helpers to keep things uniform, then
          * leaving skip_helpers=false adds dependencies.  However, in order
          * for the texture result to be uniform, all parameters must be
          * uniform so they either have to come from other uniform things or
          * subgroup ops which uniformize values.  Therefore, as long as we
          * always leave skip_helpers=false on all uniform texture ops, we'll
          * have valid helper data in this texture op.
          */
         if (!tex->def.divergent && hs.no_add_divergence)
            continue;

         if (!def_needs_helpers(&tex->def, &hs) && !tex->skip_helpers) {
            tex->skip_helpers = true;
            progress = true;
         }
      }
   }

   nir_instr_worklist_destroy(hs.tex_instrs);
   nir_instr_worklist_destroy(hs.worklist);
   ralloc_free(hs.needs_helpers);

   return nir_progress(progress, impl, nir_metadata_all);
}
