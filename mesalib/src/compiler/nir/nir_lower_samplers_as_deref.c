/*
 * Copyright (C) 2005-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2008  VMware, Inc.   All Rights Reserved.
 * Copyright © 2014 Intel Corporation
 * Copyright © 2017 Advanced Micro Devices, Inc.
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

/**
 * \file
 *
 * Lower sampler and image references of (non-bindless) uniforms by removing
 * struct dereferences, and synthesizing new uniform variables without structs
 * if required.
 *
 * This will allow backends to have a simple, uniform treatment of bindless and
 * non-bindless samplers and images.
 *
 * Example:
 *
 *   struct S {
 *      sampler2D tex[2];
 *      sampler2D other;
 *   };
 *   uniform S s[2];
 *
 *   tmp = texture(s[n].tex[m], coord);
 *
 * Becomes:
 *
 *   decl_var uniform INTERP_MODE_NONE sampler2D[2][2] lower@s.tex (...)
 *
 *   vec1 32 ssa_idx = $(2 * n + m)
 *   vec4 32 ssa_out = tex ssa_coord (coord), lower@s.tex[n][m] (texture), lower@s.tex[n][m] (sampler)
 *
 * and lower@s.tex has var->data.binding set to the base index as defined by
 * the opaque uniform mapping.
 */

#include "nir.h"
#include "nir_builder.h"
#include "compiler/glsl/ir_uniform.h"

#include "main/compiler.h"
#include "main/mtypes.h"
#include "program/prog_parameter.h"
#include "program/program.h"

struct lower_samplers_as_deref_state {
   nir_shader *shader;
   const struct gl_shader_program *shader_program;
   struct hash_table *remap_table;
};

static void
remove_struct_derefs(nir_deref *tail,
                     struct lower_samplers_as_deref_state *state,
                     nir_builder *b, char **path, unsigned *location)
{
   if (!tail->child)
      return;

   switch (tail->child->deref_type) {
   case nir_deref_type_array: {
      unsigned length = glsl_get_length(tail->type);

      remove_struct_derefs(tail->child, state, b, path, location);

      tail->type = glsl_get_array_instance(tail->child->type, length);
      break;
   }

   case nir_deref_type_struct: {
      nir_deref_struct *deref_struct = nir_deref_as_struct(tail->child);

      *location += glsl_get_record_location_offset(tail->type, deref_struct->index);
      ralloc_asprintf_append(path, ".%s",
                             glsl_get_struct_elem_name(tail->type, deref_struct->index));

      remove_struct_derefs(tail->child, state, b, path, location);

      /* Drop the struct deref and re-parent. */
      ralloc_steal(tail, tail->child->child);
      tail->type = tail->child->type;
      tail->child = tail->child->child;
      break;
   }

   default:
      unreachable("Invalid deref type");
      break;
   }
}

static void
lower_deref(nir_deref_var *deref,
            struct lower_samplers_as_deref_state *state,
            nir_builder *b)
{
   nir_variable *var = deref->var;
   gl_shader_stage stage = state->shader->info.stage;
   unsigned location = var->data.location;
   unsigned binding;
   const struct glsl_type *orig_type = deref->deref.type;
   char *path;

   assert(var->data.mode == nir_var_uniform);

   path = ralloc_asprintf(state->remap_table, "lower@%s", var->name);
   remove_struct_derefs(&deref->deref, state, b, &path, &location);

   assert(location < state->shader_program->data->NumUniformStorage &&
          state->shader_program->data->UniformStorage[location].opaque[stage].active);

   binding = state->shader_program->data->UniformStorage[location].opaque[stage].index;

   if (orig_type == deref->deref.type) {
      /* Fast path: We did not encounter any struct derefs. */
      var->data.binding = binding;
      return;
   }

   uint32_t hash = _mesa_key_hash_string(path);
   struct hash_entry *h =
      _mesa_hash_table_search_pre_hashed(state->remap_table, hash, path);

   if (h) {
      var = (nir_variable *)h->data;
   } else {
      var = nir_variable_create(state->shader, nir_var_uniform, deref->deref.type, path);
      var->data.binding = binding;
      _mesa_hash_table_insert_pre_hashed(state->remap_table, hash, path, var);
   }

   deref->var = var;
}

static bool
lower_sampler(nir_tex_instr *instr, struct lower_samplers_as_deref_state *state,
              nir_builder *b)
{
   if (!instr->texture)
      return false;

   /* In GLSL, we only fill out the texture field.  The sampler is inferred */
   assert(instr->sampler == NULL);

   b->cursor = nir_before_instr(&instr->instr);
   lower_deref(instr->texture, state, b);

   if (instr->op != nir_texop_txf_ms &&
       instr->op != nir_texop_txf_ms_mcs &&
       instr->op != nir_texop_samples_identical) {
      nir_instr_rewrite_deref(&instr->instr, &instr->sampler,
                              nir_deref_var_clone(instr->texture, instr));
   } else {
      assert(!instr->sampler);
   }

   return true;
}

static bool
lower_intrinsic(nir_intrinsic_instr *instr,
                struct lower_samplers_as_deref_state *state,
                nir_builder *b)
{
   if (instr->intrinsic == nir_intrinsic_image_var_load ||
       instr->intrinsic == nir_intrinsic_image_var_store ||
       instr->intrinsic == nir_intrinsic_image_var_atomic_add ||
       instr->intrinsic == nir_intrinsic_image_var_atomic_min ||
       instr->intrinsic == nir_intrinsic_image_var_atomic_max ||
       instr->intrinsic == nir_intrinsic_image_var_atomic_and ||
       instr->intrinsic == nir_intrinsic_image_var_atomic_or ||
       instr->intrinsic == nir_intrinsic_image_var_atomic_xor ||
       instr->intrinsic == nir_intrinsic_image_var_atomic_exchange ||
       instr->intrinsic == nir_intrinsic_image_var_atomic_comp_swap ||
       instr->intrinsic == nir_intrinsic_image_var_size) {
      b->cursor = nir_before_instr(&instr->instr);
      lower_deref(instr->variables[0], state, b);
      return true;
   }

   return false;
}

static bool
lower_impl(nir_function_impl *impl, struct lower_samplers_as_deref_state *state)
{
   nir_builder b;
   nir_builder_init(&b, impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_tex)
            progress |= lower_sampler(nir_instr_as_tex(instr), state, &b);
         else if (instr->type == nir_instr_type_intrinsic)
            progress |= lower_intrinsic(nir_instr_as_intrinsic(instr), state, &b);
      }
   }

   return progress;
}

bool
nir_lower_samplers_as_deref(nir_shader *shader,
                            const struct gl_shader_program *shader_program)
{
   bool progress = false;
   struct lower_samplers_as_deref_state state;

   state.shader = shader;
   state.shader_program = shader_program;
   state.remap_table = _mesa_hash_table_create(NULL, _mesa_key_hash_string,
                                               _mesa_key_string_equal);

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_impl(function->impl, &state);
   }

   /* keys are freed automatically by ralloc */
   _mesa_hash_table_destroy(state.remap_table, NULL);

   return progress;
}
