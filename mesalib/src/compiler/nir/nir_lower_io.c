/*
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Connor Abbott (cwabbott0@gmail.com)
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

/*
 * This lowering pass converts references to input/output variables with
 * loads/stores to actual input/output intrinsics.
 */

#include "nir.h"
#include "nir_builder.h"

struct lower_io_state {
   nir_builder builder;
   int (*type_size)(const struct glsl_type *type);
   nir_variable_mode modes;
   nir_lower_io_options options;
};

void
nir_assign_var_locations(struct exec_list *var_list, unsigned *size,
                         int (*type_size)(const struct glsl_type *))
{
   unsigned location = 0;

   nir_foreach_variable(var, var_list) {
      /*
       * UBO's have their own address spaces, so don't count them towards the
       * number of global uniforms
       */
      if ((var->data.mode == nir_var_uniform || var->data.mode == nir_var_shader_storage) &&
          var->interface_type != NULL)
         continue;

      var->data.driver_location = location;
      location += type_size(var->type);
   }

   *size = location;
}

/**
 * Return true if the given variable is a per-vertex input/output array.
 * (such as geometry shader inputs).
 */
bool
nir_is_per_vertex_io(nir_variable *var, gl_shader_stage stage)
{
   if (var->data.patch || !glsl_type_is_array(var->type))
      return false;

   if (var->data.mode == nir_var_shader_in)
      return stage == MESA_SHADER_GEOMETRY ||
             stage == MESA_SHADER_TESS_CTRL ||
             stage == MESA_SHADER_TESS_EVAL;

   if (var->data.mode == nir_var_shader_out)
      return stage == MESA_SHADER_TESS_CTRL;

   return false;
}

static nir_ssa_def *
get_io_offset(nir_builder *b, nir_deref_var *deref,
              nir_ssa_def **vertex_index,
              int (*type_size)(const struct glsl_type *),
              unsigned *component)
{
   nir_deref *tail = &deref->deref;

   /* For per-vertex input arrays (i.e. geometry shader inputs), keep the
    * outermost array index separate.  Process the rest normally.
    */
   if (vertex_index != NULL) {
      tail = tail->child;
      nir_deref_array *deref_array = nir_deref_as_array(tail);

      nir_ssa_def *vtx = nir_imm_int(b, deref_array->base_offset);
      if (deref_array->deref_array_type == nir_deref_array_type_indirect) {
         vtx = nir_iadd(b, vtx, nir_ssa_for_src(b, deref_array->indirect, 1));
      }
      *vertex_index = vtx;
   }

   if (deref->var->data.compact) {
      assert(tail->child->deref_type == nir_deref_type_array);
      assert(glsl_type_is_scalar(glsl_without_array(deref->var->type)));
      nir_deref_array *deref_array = nir_deref_as_array(tail->child);
      /* We always lower indirect dereferences for "compact" array vars. */
      assert(deref_array->deref_array_type == nir_deref_array_type_direct);

      const unsigned total_offset = *component + deref_array->base_offset;
      const unsigned slot_offset = total_offset / 4;
      *component = total_offset % 4;
      return nir_imm_int(b, type_size(glsl_vec4_type()) * slot_offset);
   }

   /* Just emit code and let constant-folding go to town */
   nir_ssa_def *offset = nir_imm_int(b, 0);

   while (tail->child != NULL) {
      const struct glsl_type *parent_type = tail->type;
      tail = tail->child;

      if (tail->deref_type == nir_deref_type_array) {
         nir_deref_array *deref_array = nir_deref_as_array(tail);
         unsigned size = type_size(tail->type);

         offset = nir_iadd(b, offset,
                           nir_imm_int(b, size * deref_array->base_offset));

         if (deref_array->deref_array_type == nir_deref_array_type_indirect) {
            nir_ssa_def *mul =
               nir_imul(b, nir_imm_int(b, size),
                        nir_ssa_for_src(b, deref_array->indirect, 1));

            offset = nir_iadd(b, offset, mul);
         }
      } else if (tail->deref_type == nir_deref_type_struct) {
         nir_deref_struct *deref_struct = nir_deref_as_struct(tail);

         unsigned field_offset = 0;
         for (unsigned i = 0; i < deref_struct->index; i++) {
            field_offset += type_size(glsl_get_struct_field(parent_type, i));
         }
         offset = nir_iadd(b, offset, nir_imm_int(b, field_offset));
      }
   }

   return offset;
}

static nir_intrinsic_instr *
lower_load(nir_intrinsic_instr *intrin, struct lower_io_state *state,
           nir_ssa_def *vertex_index, nir_ssa_def *offset,
           unsigned component)
{
   const nir_shader *nir = state->builder.shader;
   nir_variable *var = intrin->variables[0]->var;
   nir_variable_mode mode = var->data.mode;
   nir_ssa_def *barycentric = NULL;

   nir_intrinsic_op op;
   switch (mode) {
   case nir_var_shader_in:
      if (nir->stage == MESA_SHADER_FRAGMENT &&
          nir->options->use_interpolated_input_intrinsics &&
          var->data.interpolation != INTERP_MODE_FLAT) {
         assert(vertex_index == NULL);

         nir_intrinsic_op bary_op;
         if (var->data.sample ||
             (state->options & nir_lower_io_force_sample_interpolation))
            bary_op = nir_intrinsic_load_barycentric_sample;
         else if (var->data.centroid)
            bary_op = nir_intrinsic_load_barycentric_centroid;
         else
            bary_op = nir_intrinsic_load_barycentric_pixel;

         barycentric = nir_load_barycentric(&state->builder, bary_op,
                                            var->data.interpolation);
         op = nir_intrinsic_load_interpolated_input;
      } else {
         op = vertex_index ? nir_intrinsic_load_per_vertex_input :
                             nir_intrinsic_load_input;
      }
      break;
   case nir_var_shader_out:
      op = vertex_index ? nir_intrinsic_load_per_vertex_output :
                          nir_intrinsic_load_output;
      break;
   case nir_var_uniform:
      op = nir_intrinsic_load_uniform;
      break;
   case nir_var_shared:
      op = nir_intrinsic_load_shared;
      break;
   default:
      unreachable("Unknown variable mode");
   }

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(state->builder.shader, op);
   load->num_components = intrin->num_components;

   nir_intrinsic_set_base(load, var->data.driver_location);
   if (mode == nir_var_shader_in || mode == nir_var_shader_out)
      nir_intrinsic_set_component(load, component);

   if (load->intrinsic == nir_intrinsic_load_uniform)
      nir_intrinsic_set_range(load, state->type_size(var->type));

   if (vertex_index) {
      load->src[0] = nir_src_for_ssa(vertex_index);
      load->src[1] = nir_src_for_ssa(offset);
   } else if (barycentric) {
      load->src[0] = nir_src_for_ssa(barycentric);
      load->src[1] = nir_src_for_ssa(offset);
   } else {
      load->src[0] = nir_src_for_ssa(offset);
   }

   return load;
}

static nir_intrinsic_instr *
lower_store(nir_intrinsic_instr *intrin, struct lower_io_state *state,
            nir_ssa_def *vertex_index, nir_ssa_def *offset,
            unsigned component)
{
   nir_variable *var = intrin->variables[0]->var;
   nir_variable_mode mode = var->data.mode;

   nir_intrinsic_op op;
   if (mode == nir_var_shared) {
      op = nir_intrinsic_store_shared;
   } else {
      assert(mode == nir_var_shader_out);
      op = vertex_index ? nir_intrinsic_store_per_vertex_output :
                          nir_intrinsic_store_output;
   }

   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(state->builder.shader, op);
   store->num_components = intrin->num_components;

   nir_src_copy(&store->src[0], &intrin->src[0], store);

   nir_intrinsic_set_base(store, var->data.driver_location);

   if (mode == nir_var_shader_out)
      nir_intrinsic_set_component(store, component);

   nir_intrinsic_set_write_mask(store, nir_intrinsic_write_mask(intrin));

   if (vertex_index)
      store->src[1] = nir_src_for_ssa(vertex_index);

   store->src[vertex_index ? 2 : 1] = nir_src_for_ssa(offset);

   return store;
}

static nir_intrinsic_instr *
lower_atomic(nir_intrinsic_instr *intrin, struct lower_io_state *state,
             nir_ssa_def *offset)
{
   nir_variable *var = intrin->variables[0]->var;

   assert(var->data.mode == nir_var_shared);

   nir_intrinsic_op op;
   switch (intrin->intrinsic) {
#define OP(O) case nir_intrinsic_var_##O: op = nir_intrinsic_shared_##O; break;
   OP(atomic_exchange)
   OP(atomic_comp_swap)
   OP(atomic_add)
   OP(atomic_imin)
   OP(atomic_umin)
   OP(atomic_imax)
   OP(atomic_umax)
   OP(atomic_and)
   OP(atomic_or)
   OP(atomic_xor)
#undef OP
   default:
      unreachable("Invalid atomic");
   }

   nir_intrinsic_instr *atomic =
      nir_intrinsic_instr_create(state->builder.shader, op);

   nir_intrinsic_set_base(atomic, var->data.driver_location);

   atomic->src[0] = nir_src_for_ssa(offset);
   for (unsigned i = 0; i < nir_intrinsic_infos[intrin->intrinsic].num_srcs; i++) {
      nir_src_copy(&atomic->src[i+1], &intrin->src[i], atomic);
   }

   return atomic;
}

static nir_intrinsic_instr *
lower_interpolate_at(nir_intrinsic_instr *intrin, struct lower_io_state *state,
                     nir_ssa_def *offset, unsigned component)
{
   nir_variable *var = intrin->variables[0]->var;

   assert(var->data.mode == nir_var_shader_in);

   /* Ignore interpolateAt() for flat variables - flat is flat. */
   if (var->data.interpolation == INTERP_MODE_FLAT)
      return lower_load(intrin, state, NULL, offset, component);

   nir_intrinsic_op bary_op;
   switch (intrin->intrinsic) {
   case nir_intrinsic_interp_var_at_centroid:
      bary_op = (state->options & nir_lower_io_force_sample_interpolation) ?
                nir_intrinsic_load_barycentric_sample :
                nir_intrinsic_load_barycentric_centroid;
      break;
   case nir_intrinsic_interp_var_at_sample:
      bary_op = nir_intrinsic_load_barycentric_at_sample;
      break;
   case nir_intrinsic_interp_var_at_offset:
      bary_op = nir_intrinsic_load_barycentric_at_offset;
      break;
   default:
      unreachable("Bogus interpolateAt() intrinsic.");
   }

   nir_intrinsic_instr *bary_setup =
      nir_intrinsic_instr_create(state->builder.shader, bary_op);

   nir_ssa_dest_init(&bary_setup->instr, &bary_setup->dest, 2, 32, NULL);
   nir_intrinsic_set_interp_mode(bary_setup, var->data.interpolation);

   if (intrin->intrinsic != nir_intrinsic_interp_var_at_centroid)
      nir_src_copy(&bary_setup->src[0], &intrin->src[0], bary_setup);

   nir_builder_instr_insert(&state->builder, &bary_setup->instr);

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(state->builder.shader,
                                 nir_intrinsic_load_interpolated_input);
   load->num_components = intrin->num_components;

   nir_intrinsic_set_base(load, var->data.driver_location);
   nir_intrinsic_set_component(load, component);

   load->src[0] = nir_src_for_ssa(&bary_setup->dest.ssa);
   load->src[1] = nir_src_for_ssa(offset);

   return load;
}

static bool
nir_lower_io_block(nir_block *block,
                   struct lower_io_state *state)
{
   nir_builder *b = &state->builder;
   const nir_shader_compiler_options *options = b->shader->options;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_var:
      case nir_intrinsic_store_var:
      case nir_intrinsic_var_atomic_add:
      case nir_intrinsic_var_atomic_imin:
      case nir_intrinsic_var_atomic_umin:
      case nir_intrinsic_var_atomic_imax:
      case nir_intrinsic_var_atomic_umax:
      case nir_intrinsic_var_atomic_and:
      case nir_intrinsic_var_atomic_or:
      case nir_intrinsic_var_atomic_xor:
      case nir_intrinsic_var_atomic_exchange:
      case nir_intrinsic_var_atomic_comp_swap:
         /* We can lower the io for this nir instrinsic */
         break;
      case nir_intrinsic_interp_var_at_centroid:
      case nir_intrinsic_interp_var_at_sample:
      case nir_intrinsic_interp_var_at_offset:
         /* We can optionally lower these to load_interpolated_input */
         if (options->use_interpolated_input_intrinsics)
            break;
      default:
         /* We can't lower the io for this nir instrinsic, so skip it */
         continue;
      }

      nir_variable *var = intrin->variables[0]->var;
      nir_variable_mode mode = var->data.mode;

      if ((state->modes & mode) == 0)
         continue;

      if (mode != nir_var_shader_in &&
          mode != nir_var_shader_out &&
          mode != nir_var_shared &&
          mode != nir_var_uniform)
         continue;

      b->cursor = nir_before_instr(instr);

      const bool per_vertex = nir_is_per_vertex_io(var, b->shader->stage);

      nir_ssa_def *offset;
      nir_ssa_def *vertex_index = NULL;
      unsigned component_offset = var->data.location_frac;

      offset = get_io_offset(b, intrin->variables[0],
                             per_vertex ? &vertex_index : NULL,
                             state->type_size, &component_offset);

      nir_intrinsic_instr *replacement;

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_var:
         replacement = lower_load(intrin, state, vertex_index, offset,
                                  component_offset);
         break;

      case nir_intrinsic_store_var:
         replacement = lower_store(intrin, state, vertex_index, offset,
                                   component_offset);
         break;

      case nir_intrinsic_var_atomic_add:
      case nir_intrinsic_var_atomic_imin:
      case nir_intrinsic_var_atomic_umin:
      case nir_intrinsic_var_atomic_imax:
      case nir_intrinsic_var_atomic_umax:
      case nir_intrinsic_var_atomic_and:
      case nir_intrinsic_var_atomic_or:
      case nir_intrinsic_var_atomic_xor:
      case nir_intrinsic_var_atomic_exchange:
      case nir_intrinsic_var_atomic_comp_swap:
         assert(vertex_index == NULL);
         replacement = lower_atomic(intrin, state, offset);
         break;

      case nir_intrinsic_interp_var_at_centroid:
      case nir_intrinsic_interp_var_at_sample:
      case nir_intrinsic_interp_var_at_offset:
         assert(vertex_index == NULL);
         replacement = lower_interpolate_at(intrin, state, offset,
                                            component_offset);
         break;

      default:
         continue;
      }

      if (nir_intrinsic_infos[intrin->intrinsic].has_dest) {
         if (intrin->dest.is_ssa) {
            nir_ssa_dest_init(&replacement->instr, &replacement->dest,
                              intrin->dest.ssa.num_components,
                              intrin->dest.ssa.bit_size, NULL);
            nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                     nir_src_for_ssa(&replacement->dest.ssa));
         } else {
            nir_dest_copy(&replacement->dest, &intrin->dest, &intrin->instr);
         }
      }

      nir_instr_insert_before(&intrin->instr, &replacement->instr);
      nir_instr_remove(&intrin->instr);
   }

   return true;
}

static void
nir_lower_io_impl(nir_function_impl *impl,
                  nir_variable_mode modes,
                  int (*type_size)(const struct glsl_type *),
                  nir_lower_io_options options)
{
   struct lower_io_state state;

   nir_builder_init(&state.builder, impl);
   state.modes = modes;
   state.type_size = type_size;
   state.options = options;

   nir_foreach_block(block, impl) {
      nir_lower_io_block(block, &state);
   }

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

void
nir_lower_io(nir_shader *shader, nir_variable_mode modes,
             int (*type_size)(const struct glsl_type *),
             nir_lower_io_options options)
{
   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_lower_io_impl(function->impl, modes, type_size, options);
      }
   }
}

/**
 * Return the offset soruce for a load/store intrinsic.
 */
nir_src *
nir_get_io_offset_src(nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_uniform:
      return &instr->src[0];
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_store_output:
      return &instr->src[1];
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_per_vertex_output:
      return &instr->src[2];
   default:
      return NULL;
   }
}

/**
 * Return the vertex index source for a load/store per_vertex intrinsic.
 */
nir_src *
nir_get_io_vertex_index_src(nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
      return &instr->src[0];
   case nir_intrinsic_store_per_vertex_output:
      return &instr->src[1];
   default:
      return NULL;
   }
}
