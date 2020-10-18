/*
 * Copyright © 2020 Advanced Micro Devices, Inc.
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

/* These passes enable converting uniforms to literals when it's profitable,
 * effectively inlining uniform values in the IR. The main benefit is register
 * usage decrease leading to better SMT (hyperthreading). It's accomplished
 * by targetting uniforms that determine whether a conditional branch is
 * taken.
 *
 * Only uniforms used in if conditions are analyzed.
 *
 * nir_find_inlinable_uniforms finds uniforms that can be inlined and stores
 * that information in shader_info.
 *
 * nir_inline_uniforms inlines uniform values.
 *
 * (uniforms must be lowered to load_ubo before calling this)
 */

#include "compiler/nir/nir_builder.h"

/* Maximum value in shader_info::inlinable_uniform_dw_offsets[] */
#define MAX_OFFSET (UINT16_MAX * 4)

static bool
src_only_uses_uniforms(const nir_src *src, struct set **uni_offsets)
{
   if (!src->is_ssa)
      return false;

   nir_instr *instr = src->ssa->parent_instr;

   switch (instr->type) {
   case nir_instr_type_alu: {
      /* Return true if all sources return true. */
      /* TODO: Swizzles are ignored, so vectors can prevent inlining. */
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
         if (!src_only_uses_uniforms(&alu->src[i].src, uni_offsets))
             return false;
      }
      return true;
   }

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      /* Return true if the intrinsic loads from UBO 0 with a constant
       * offset.
       */
      if (intr->intrinsic == nir_intrinsic_load_ubo &&
          nir_src_is_const(intr->src[0]) &&
          nir_src_as_uint(intr->src[0]) == 0 &&
          nir_src_is_const(intr->src[1]) &&
          nir_src_as_uint(intr->src[1]) <= MAX_OFFSET &&
          /* TODO: Can't handle vectors and other bit sizes for now. */
          /* UBO loads should be scalarized. */
          intr->dest.ssa.num_components == 1 &&
          intr->dest.ssa.bit_size == 32) {
         /* Record the uniform offset. */
         if (!*uni_offsets)
            *uni_offsets = _mesa_set_create_u32_keys(NULL);

         /* Add 1 because the set doesn't allow NULL keys. */
         _mesa_set_add(*uni_offsets,
                       (void*)(uintptr_t)(nir_src_as_uint(intr->src[1]) + 1));
         return true;
      }
      return false;
   }

   case nir_instr_type_load_const:
      /* Always return true for constants. */
      return true;

   default:
      return false;
   }
}

void
nir_find_inlinable_uniforms(nir_shader *shader)
{
   struct set *uni_offsets = NULL;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         foreach_list_typed(nir_cf_node, node, node, &function->impl->body) {
            switch (node->type) {
            case nir_cf_node_if: {
               const nir_src *cond = &nir_cf_node_as_if(node)->condition;
               struct set *found_offsets = NULL;

               if (src_only_uses_uniforms(cond, &found_offsets) &&
                   found_offsets) {
                  /* All uniforms are lowerable. Save uniform offsets. */
                  set_foreach(found_offsets, entry) {
                     if (!uni_offsets)
                        uni_offsets = _mesa_set_create_u32_keys(NULL);

                     _mesa_set_add(uni_offsets, entry->key);
                  }
               }
               if (found_offsets)
                  _mesa_set_destroy(found_offsets, NULL);
               break;
            }

            case nir_cf_node_loop:
               /* TODO: handle loops if we want to unroll them at draw time */
               break;

            default:
               break;
            }
         }
      }
   }

   if (uni_offsets) {
      unsigned num = 0;

      set_foreach(uni_offsets, entry) {
         /* Subtract 1 because all keys are + 1. */
         uint32_t offset = (uintptr_t)entry->key - 1;
         assert(offset < MAX_OFFSET);

         if (num < MAX_INLINABLE_UNIFORMS)
            shader->info.inlinable_uniform_dw_offsets[num++] = offset / 4;
      }
      shader->info.num_inlinable_uniforms = num;
      _mesa_set_destroy(uni_offsets, NULL);
   }
}

void
nir_inline_uniforms(nir_shader *shader, unsigned num_uniforms,
                    const uint32_t *uniform_values,
                    const uint16_t *uniform_dw_offsets)
{
   if (!num_uniforms)
      return;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;

               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

               /* Only replace UBO 0 with constant offsets. */
               if (intr->intrinsic == nir_intrinsic_load_ubo &&
                   nir_src_is_const(intr->src[0]) &&
                   nir_src_as_uint(intr->src[0]) == 0 &&
                   nir_src_is_const(intr->src[1]) &&
                   /* TODO: Can't handle vectors and other bit sizes for now. */
                   /* UBO loads should be scalarized. */
                   intr->dest.ssa.num_components == 1 &&
                   intr->dest.ssa.bit_size == 32) {
                  uint64_t offset = nir_src_as_uint(intr->src[1]);

                  for (unsigned i = 0; i < num_uniforms; i++) {
                     if (offset == uniform_dw_offsets[i] * 4) {
                        b.cursor = nir_before_instr(&intr->instr);
                        nir_ssa_def *def = nir_imm_int(&b, uniform_values[i]);
                        nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_src_for_ssa(def));
                        nir_instr_remove(&intr->instr);
                        break;
                     }
                  }
               }
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }
}
