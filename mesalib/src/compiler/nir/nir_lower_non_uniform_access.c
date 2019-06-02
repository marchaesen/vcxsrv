/*
 * Copyright © 2019 Intel Corporation
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
#include "nir_builder.h"

static nir_ssa_def *
read_first_invocation(nir_builder *b, nir_ssa_def *x)
{
   nir_intrinsic_instr *first =
      nir_intrinsic_instr_create(b->shader,
                                 nir_intrinsic_read_first_invocation);
   first->num_components = x->num_components;
   first->src[0] = nir_src_for_ssa(x);
   nir_ssa_dest_init(&first->instr, &first->dest,
                     x->num_components, x->bit_size, NULL);
   nir_builder_instr_insert(b, &first->instr);
   return &first->dest.ssa;
}

static bool
lower_non_uniform_tex_access(nir_builder *b, nir_tex_instr *tex)
{
   if (!tex->texture_non_uniform && !tex->sampler_non_uniform)
      return false;

   /* We can have at most one texture and one sampler handle */
   nir_ssa_def *handles[2];
   unsigned handle_count = 0;
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      switch (tex->src[i].src_type) {
      case nir_tex_src_texture_offset:
      case nir_tex_src_texture_handle:
         if (!tex->texture_non_uniform)
            continue;
         break;

      case nir_tex_src_sampler_offset:
      case nir_tex_src_sampler_handle:
         if (!tex->sampler_non_uniform)
            continue;
         break;

      default:
         continue;
      }

      assert(tex->src[i].src.is_ssa);
      assert(tex->src[i].src.ssa->num_components == 1);
      assert(handle_count < 2);
      handles[handle_count++] = tex->src[i].src.ssa;
   }

   if (handle_count == 0)
      return false;

   b->cursor = nir_instr_remove(&tex->instr);

   nir_push_loop(b);

   nir_ssa_def *all_equal_first = nir_imm_true(b);
   for (unsigned i = 0; i < handle_count; i++) {
      nir_ssa_def *equal_first =
         nir_ieq(b, read_first_invocation(b, handles[i]), handles[i]);
      all_equal_first = nir_iand(b, all_equal_first, equal_first);
   }

   nir_push_if(b, all_equal_first);

   nir_builder_instr_insert(b, &tex->instr);
   nir_jump(b, nir_jump_break);

   return true;
}

static bool
lower_non_uniform_access_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                                unsigned handle_src)
{
   if (!(nir_intrinsic_access(intrin) & ACCESS_NON_UNIFORM))
      return false;

   /* If it's constant, it's automatically uniform; don't bother. */
   if (nir_src_is_const(intrin->src[handle_src]))
      return false;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_push_loop(b);

   assert(intrin->src[handle_src].is_ssa);
   assert(intrin->src[handle_src].ssa->num_components == 1);
   nir_ssa_def *handle = intrin->src[handle_src].ssa;

   nir_push_if(b, nir_ieq(b, read_first_invocation(b, handle), handle));

   nir_builder_instr_insert(b, &intrin->instr);
   nir_jump(b, nir_jump_break);

   return true;
}

static bool
nir_lower_non_uniform_access_impl(nir_function_impl *impl,
                                  enum nir_lower_non_uniform_access_type types)
{
   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block_safe(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         switch (instr->type) {
         case nir_instr_type_tex: {
            nir_tex_instr *tex = nir_instr_as_tex(instr);
            if ((types & nir_lower_non_uniform_texture_access) &&
                lower_non_uniform_tex_access(&b, tex))
               progress = true;
            break;
         }

         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_load_ubo:
               if ((types & nir_lower_non_uniform_ubo_access) &&
                   lower_non_uniform_access_intrin(&b, intrin, 0))
                  progress = true;
               break;

            case nir_intrinsic_load_ssbo:
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
            case nir_intrinsic_ssbo_atomic_fadd:
            case nir_intrinsic_ssbo_atomic_fmin:
            case nir_intrinsic_ssbo_atomic_fmax:
            case nir_intrinsic_ssbo_atomic_fcomp_swap:
               if ((types & nir_lower_non_uniform_ssbo_access) &&
                   lower_non_uniform_access_intrin(&b, intrin, 0))
                  progress = true;
               break;

            case nir_intrinsic_store_ssbo:
               /* SSBO Stores put the index in the second source */
               if ((types & nir_lower_non_uniform_ssbo_access) &&
                   lower_non_uniform_access_intrin(&b, intrin, 1))
                  progress = true;
               break;

            case nir_intrinsic_image_load:
            case nir_intrinsic_image_store:
            case nir_intrinsic_image_atomic_add:
            case nir_intrinsic_image_atomic_min:
            case nir_intrinsic_image_atomic_max:
            case nir_intrinsic_image_atomic_and:
            case nir_intrinsic_image_atomic_or:
            case nir_intrinsic_image_atomic_xor:
            case nir_intrinsic_image_atomic_exchange:
            case nir_intrinsic_image_atomic_comp_swap:
            case nir_intrinsic_image_atomic_fadd:
            case nir_intrinsic_image_size:
            case nir_intrinsic_image_samples:
            case nir_intrinsic_bindless_image_load:
            case nir_intrinsic_bindless_image_store:
            case nir_intrinsic_bindless_image_atomic_add:
            case nir_intrinsic_bindless_image_atomic_min:
            case nir_intrinsic_bindless_image_atomic_max:
            case nir_intrinsic_bindless_image_atomic_and:
            case nir_intrinsic_bindless_image_atomic_or:
            case nir_intrinsic_bindless_image_atomic_xor:
            case nir_intrinsic_bindless_image_atomic_exchange:
            case nir_intrinsic_bindless_image_atomic_comp_swap:
            case nir_intrinsic_bindless_image_atomic_fadd:
            case nir_intrinsic_bindless_image_size:
            case nir_intrinsic_bindless_image_samples:
               if ((types & nir_lower_non_uniform_image_access) &&
                   lower_non_uniform_access_intrin(&b, intrin, 0))
                  progress = true;
               break;

            default:
               /* Nothing to do */
               break;
            }
            break;
         }

         default:
            /* Nothing to do */
            break;
         }
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_none);

   return progress;
}

/**
 * Lowers non-uniform resource access by using a loop
 *
 * This pass lowers non-uniform resource access by using subgroup operations
 * and a loop.  Most hardware requires things like textures and UBO access
 * operations to happen on a dynamically uniform (or at least subgroup
 * uniform) resource.  This pass allows for non-uniform access by placing the
 * texture instruction in a loop that looks something like this:
 *
 * loop {
 *    bool tex_eq_first = readFirstInvocationARB(texture) == texture;
 *    bool smp_eq_first = readFirstInvocationARB(sampler) == sampler;
 *    if (tex_eq_first && smp_eq_first) {
 *       res = texture(texture, sampler, ...);
 *       break;
 *    }
 * }
 *
 * Fortunately, because the instruction is immediately followed by the only
 * break in the loop, the block containing the instruction dominates the end
 * of the loop.  Therefore, it's safe to move the instruction into the loop
 * without fixing up SSA in any way.
 */
bool
nir_lower_non_uniform_access(nir_shader *shader,
                             enum nir_lower_non_uniform_access_type types)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl &&
          nir_lower_non_uniform_access_impl(function->impl, types))
         progress = true;
   }

   return progress;
}
