/*
 * Copyright (C) 2005-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2008  VMware, Inc.   All Rights Reserved.
 * Copyright © 2014 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "gl_nir.h"
#include "ir_uniform.h"

#include "main/compiler.h"
#include "main/mtypes.h"

/* Calculate the sampler index based on array indicies and also
 * calculate the base uniform location for struct members.
 */
static void
calc_sampler_offsets(nir_builder *b, nir_ssa_def *ptr,
                     const struct gl_shader_program *shader_program,
                     unsigned *base_index, nir_ssa_def **index,
                     unsigned *array_elements)
{
   *base_index = 0;
   *index = NULL;
   *array_elements = 1;
   unsigned location = 0;

   nir_deref_instr *deref = nir_instr_as_deref(ptr->parent_instr);
   while (deref->deref_type != nir_deref_type_var) {
      assert(deref->parent.is_ssa);
      nir_deref_instr *parent =
         nir_instr_as_deref(deref->parent.ssa->parent_instr);

      switch (deref->deref_type) {
      case nir_deref_type_struct:
         location += glsl_get_record_location_offset(parent->type,
                                                     deref->strct.index);
         break;

      case nir_deref_type_array: {
         nir_const_value *const_deref_index =
            nir_src_as_const_value(deref->arr.index);

         if (const_deref_index && *index == NULL) {
            /* We're still building a direct index */
            *base_index += const_deref_index->u32[0] * *array_elements;
         } else {
            if (*index == NULL) {
               /* We used to be direct but not anymore */
               *index = nir_imm_int(b, *base_index);
               *base_index = 0;
            }

            *index = nir_iadd(b, *index,
                     nir_imul(b, nir_imm_int(b, *array_elements),
                              nir_ssa_for_src(b, deref->arr.index, 1)));
         }

         *array_elements *= glsl_get_length(parent->type);
         break;
      }

      default:
         unreachable("Invalid sampler deref type");
      }

      deref = parent;
   }

   if (*index)
      *index = nir_umin(b, *index, nir_imm_int(b, *array_elements - 1));

   /* We hit the deref_var.  This is the end of the line */
   assert(deref->deref_type == nir_deref_type_var);

   location += deref->var->data.location;

   gl_shader_stage stage = b->shader->info.stage;
   assert(location < shader_program->data->NumUniformStorage &&
          shader_program->data->UniformStorage[location].opaque[stage].active);

   *base_index +=
      shader_program->data->UniformStorage[location].opaque[stage].index;
}

static bool
lower_sampler(nir_builder *b, nir_tex_instr *instr,
              const struct gl_shader_program *shader_program)
{
   int texture_idx =
      nir_tex_instr_src_index(instr, nir_tex_src_texture_deref);
   int sampler_idx =
      nir_tex_instr_src_index(instr, nir_tex_src_sampler_deref);

   if (texture_idx < 0)
      return false;

   assert(texture_idx >= 0 && sampler_idx >= 0);
   assert(instr->src[texture_idx].src.is_ssa);
   assert(instr->src[sampler_idx].src.is_ssa);
   assert(instr->src[texture_idx].src.ssa == instr->src[sampler_idx].src.ssa);

   b->cursor = nir_before_instr(&instr->instr);

   unsigned base_offset, array_elements;
   nir_ssa_def *indirect;
   calc_sampler_offsets(b, instr->src[texture_idx].src.ssa, shader_program,
                        &base_offset, &indirect, &array_elements);

   instr->texture_index = base_offset;
   instr->sampler_index = base_offset;
   if (indirect) {
      nir_instr_rewrite_src(&instr->instr, &instr->src[texture_idx].src,
                            nir_src_for_ssa(indirect));
      instr->src[texture_idx].src_type = nir_tex_src_texture_offset;
      nir_instr_rewrite_src(&instr->instr, &instr->src[sampler_idx].src,
                            nir_src_for_ssa(indirect));
      instr->src[sampler_idx].src_type = nir_tex_src_sampler_offset;

      instr->texture_array_size = array_elements;
   } else {
      nir_tex_instr_remove_src(instr, texture_idx);
      /* The sampler index may have changed */
      sampler_idx = nir_tex_instr_src_index(instr, nir_tex_src_sampler_deref);
      nir_tex_instr_remove_src(instr, sampler_idx);
   }

   return true;
}

static bool
lower_impl(nir_function_impl *impl,
           const struct gl_shader_program *shader_program)
{
   nir_builder b;
   nir_builder_init(&b, impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_tex)
            progress |= lower_sampler(&b, nir_instr_as_tex(instr),
                                      shader_program);
      }
   }

   return progress;
}

bool
gl_nir_lower_samplers(nir_shader *shader,
                      const struct gl_shader_program *shader_program)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_impl(function->impl, shader_program);
   }

   return progress;
}
