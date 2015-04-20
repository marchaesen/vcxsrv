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

#include "nir.h"
#include "../program.h"
#include "program/hash_table.h"
#include "ir_uniform.h"

extern "C" {
#include "main/compiler.h"
#include "main/mtypes.h"
#include "program/prog_parameter.h"
#include "program/program.h"
}

static unsigned
get_sampler_index(const struct gl_shader_program *shader_program,
                  gl_shader_stage stage, const char *name)
{
   unsigned location;
   if (!shader_program->UniformHash->get(location, name)) {
      assert(!"failed to find sampler");
      return 0;
   }

   if (!shader_program->UniformStorage[location].sampler[stage].active) {
      assert(!"cannot return a sampler");
      return 0;
   }

   return shader_program->UniformStorage[location].sampler[stage].index;
}

static void
lower_sampler(nir_tex_instr *instr, const struct gl_shader_program *shader_program,
              gl_shader_stage stage, void *mem_ctx)
{
   if (instr->sampler == NULL)
      return;

   /* Get the name and the offset */
   instr->sampler_index = 0;
   char *name = ralloc_strdup(mem_ctx, instr->sampler->var->name);

   for (nir_deref *deref = &instr->sampler->deref;
        deref->child; deref = deref->child) {
      switch (deref->child->deref_type) {
      case nir_deref_type_array: {
         nir_deref_array *deref_array = nir_deref_as_array(deref->child);

         /* XXX: We're assuming here that the indirect is the last array
          * thing we have.  This should be ok for now as we don't support
          * arrays_of_arrays yet.
          */

         instr->sampler_index *= glsl_get_length(deref->type);
         switch (deref_array->deref_array_type) {
         case nir_deref_array_type_direct:
            instr->sampler_index += deref_array->base_offset;
            if (deref_array->deref.child)
               ralloc_asprintf_append(&name, "[%u]", deref_array->base_offset);
            break;
         case nir_deref_array_type_indirect: {
            instr->src = reralloc(instr, instr->src, nir_tex_src,
                                  instr->num_srcs + 1);
            memset(&instr->src[instr->num_srcs], 0, sizeof *instr->src);
            instr->src[instr->num_srcs].src_type = nir_tex_src_sampler_offset;
            instr->num_srcs++;

            nir_instr_rewrite_src(&instr->instr,
                                  &instr->src[instr->num_srcs - 1].src,
                                  deref_array->indirect);

            instr->sampler_array_size = glsl_get_length(deref->type);

            nir_src empty;
            memset(&empty, 0, sizeof empty);
            nir_instr_rewrite_src(&instr->instr, &deref_array->indirect, empty);

            if (deref_array->deref.child)
               ralloc_strcat(&name, "[0]");
            break;
         }

         case nir_deref_array_type_wildcard:
            unreachable("Cannot copy samplers");
         default:
            unreachable("Invalid deref array type");
         }
         break;
      }

      case nir_deref_type_struct: {
         nir_deref_struct *deref_struct = nir_deref_as_struct(deref->child);
         const char *field = glsl_get_struct_elem_name(deref->type,
                                                       deref_struct->index);
         ralloc_asprintf_append(&name, ".%s", field);
         break;
      }

      default:
         unreachable("Invalid deref type");
         break;
      }
   }

   instr->sampler_index += get_sampler_index(shader_program, stage, name);

   instr->sampler = NULL;
}

typedef struct {
   void *mem_ctx;
   const struct gl_shader_program *shader_program;
   gl_shader_stage stage;
} lower_state;

static bool
lower_block_cb(nir_block *block, void *_state)
{
   lower_state *state = (lower_state *) _state;

   nir_foreach_instr(block, instr) {
      if (instr->type == nir_instr_type_tex) {
         nir_tex_instr *tex_instr = nir_instr_as_tex(instr);
         lower_sampler(tex_instr, state->shader_program, state->stage,
                       state->mem_ctx);
      }
   }

   return true;
}

static void
lower_impl(nir_function_impl *impl, const struct gl_shader_program *shader_program,
           gl_shader_stage stage)
{
   lower_state state;

   state.mem_ctx = ralloc_parent(impl);
   state.shader_program = shader_program;
   state.stage = stage;

   nir_foreach_block(impl, lower_block_cb, &state);
}

extern "C" void
nir_lower_samplers(nir_shader *shader, const struct gl_shader_program *shader_program,
                   gl_shader_stage stage)
{
   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         lower_impl(overload->impl, shader_program, stage);
   }
}
