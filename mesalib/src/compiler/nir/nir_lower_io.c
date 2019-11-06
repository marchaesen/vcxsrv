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
#include "nir_deref.h"

#include "util/u_math.h"

struct lower_io_state {
   void *dead_ctx;
   nir_builder builder;
   int (*type_size)(const struct glsl_type *type, bool);
   nir_variable_mode modes;
   nir_lower_io_options options;
};

static nir_intrinsic_op
ssbo_atomic_for_deref(nir_intrinsic_op deref_op)
{
   switch (deref_op) {
#define OP(O) case nir_intrinsic_deref_##O: return nir_intrinsic_ssbo_##O;
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
   OP(atomic_fadd)
   OP(atomic_fmin)
   OP(atomic_fmax)
   OP(atomic_fcomp_swap)
#undef OP
   default:
      unreachable("Invalid SSBO atomic");
   }
}

static nir_intrinsic_op
global_atomic_for_deref(nir_intrinsic_op deref_op)
{
   switch (deref_op) {
#define OP(O) case nir_intrinsic_deref_##O: return nir_intrinsic_global_##O;
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
   OP(atomic_fadd)
   OP(atomic_fmin)
   OP(atomic_fmax)
   OP(atomic_fcomp_swap)
#undef OP
   default:
      unreachable("Invalid SSBO atomic");
   }
}

static nir_intrinsic_op
shared_atomic_for_deref(nir_intrinsic_op deref_op)
{
   switch (deref_op) {
#define OP(O) case nir_intrinsic_deref_##O: return nir_intrinsic_shared_##O;
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
   OP(atomic_fadd)
   OP(atomic_fmin)
   OP(atomic_fmax)
   OP(atomic_fcomp_swap)
#undef OP
   default:
      unreachable("Invalid shared atomic");
   }
}

void
nir_assign_var_locations(struct exec_list *var_list, unsigned *size,
                         int (*type_size)(const struct glsl_type *, bool))
{
   unsigned location = 0;

   nir_foreach_variable(var, var_list) {
      /*
       * UBOs have their own address spaces, so don't count them towards the
       * number of global uniforms
       */
      if (var->data.mode == nir_var_mem_ubo || var->data.mode == nir_var_mem_ssbo)
         continue;

      var->data.driver_location = location;
      bool bindless_type_size = var->data.mode == nir_var_shader_in ||
                                var->data.mode == nir_var_shader_out ||
                                var->data.bindless;
      location += type_size(var->type, bindless_type_size);
   }

   *size = location;
}

/**
 * Return true if the given variable is a per-vertex input/output array.
 * (such as geometry shader inputs).
 */
bool
nir_is_per_vertex_io(const nir_variable *var, gl_shader_stage stage)
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
get_io_offset(nir_builder *b, nir_deref_instr *deref,
              nir_ssa_def **vertex_index,
              int (*type_size)(const struct glsl_type *, bool),
              unsigned *component, bool bts)
{
   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);

   assert(path.path[0]->deref_type == nir_deref_type_var);
   nir_deref_instr **p = &path.path[1];

   /* For per-vertex input arrays (i.e. geometry shader inputs), keep the
    * outermost array index separate.  Process the rest normally.
    */
   if (vertex_index != NULL) {
      assert((*p)->deref_type == nir_deref_type_array);
      *vertex_index = nir_ssa_for_src(b, (*p)->arr.index, 1);
      p++;
   }

   if (path.path[0]->var->data.compact) {
      assert((*p)->deref_type == nir_deref_type_array);
      assert(glsl_type_is_scalar((*p)->type));

      /* We always lower indirect dereferences for "compact" array vars. */
      const unsigned index = nir_src_as_uint((*p)->arr.index);
      const unsigned total_offset = *component + index;
      const unsigned slot_offset = total_offset / 4;
      *component = total_offset % 4;
      return nir_imm_int(b, type_size(glsl_vec4_type(), bts) * slot_offset);
   }

   /* Just emit code and let constant-folding go to town */
   nir_ssa_def *offset = nir_imm_int(b, 0);

   for (; *p; p++) {
      if ((*p)->deref_type == nir_deref_type_array) {
         unsigned size = type_size((*p)->type, bts);

         nir_ssa_def *mul =
            nir_amul_imm(b, nir_ssa_for_src(b, (*p)->arr.index, 1), size);

         offset = nir_iadd(b, offset, mul);
      } else if ((*p)->deref_type == nir_deref_type_struct) {
         /* p starts at path[1], so this is safe */
         nir_deref_instr *parent = *(p - 1);

         unsigned field_offset = 0;
         for (unsigned i = 0; i < (*p)->strct.index; i++) {
            field_offset += type_size(glsl_get_struct_field(parent->type, i), bts);
         }
         offset = nir_iadd_imm(b, offset, field_offset);
      } else {
         unreachable("Unsupported deref type");
      }
   }

   nir_deref_path_finish(&path);

   return offset;
}

static nir_ssa_def *
emit_load(struct lower_io_state *state,
          nir_ssa_def *vertex_index, nir_variable *var, nir_ssa_def *offset,
          unsigned component, unsigned num_components, unsigned bit_size,
          nir_alu_type type)
{
   nir_builder *b = &state->builder;
   const nir_shader *nir = b->shader;
   nir_variable_mode mode = var->data.mode;
   nir_ssa_def *barycentric = NULL;

   nir_intrinsic_op op;
   switch (mode) {
   case nir_var_shader_in:
      if (nir->info.stage == MESA_SHADER_FRAGMENT &&
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
   case nir_var_mem_shared:
      op = nir_intrinsic_load_shared;
      break;
   default:
      unreachable("Unknown variable mode");
   }

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(state->builder.shader, op);
   load->num_components = num_components;

   nir_intrinsic_set_base(load, var->data.driver_location);
   if (mode == nir_var_shader_in || mode == nir_var_shader_out)
      nir_intrinsic_set_component(load, component);

   if (load->intrinsic == nir_intrinsic_load_uniform)
      nir_intrinsic_set_range(load,
                              state->type_size(var->type, var->data.bindless));

   if (load->intrinsic == nir_intrinsic_load_input ||
       load->intrinsic == nir_intrinsic_load_uniform)
      nir_intrinsic_set_type(load, type);

   if (vertex_index) {
      load->src[0] = nir_src_for_ssa(vertex_index);
      load->src[1] = nir_src_for_ssa(offset);
   } else if (barycentric) {
      load->src[0] = nir_src_for_ssa(barycentric);
      load->src[1] = nir_src_for_ssa(offset);
   } else {
      load->src[0] = nir_src_for_ssa(offset);
   }

   nir_ssa_dest_init(&load->instr, &load->dest,
                     num_components, bit_size, NULL);
   nir_builder_instr_insert(b, &load->instr);

   return &load->dest.ssa;
}

static nir_ssa_def *
lower_load(nir_intrinsic_instr *intrin, struct lower_io_state *state,
           nir_ssa_def *vertex_index, nir_variable *var, nir_ssa_def *offset,
           unsigned component, const struct glsl_type *type)
{
   assert(intrin->dest.is_ssa);
   if (intrin->dest.ssa.bit_size == 64 &&
       (state->options & nir_lower_io_lower_64bit_to_32)) {
      nir_builder *b = &state->builder;

      const unsigned slot_size = state->type_size(glsl_dvec_type(2), false);

      nir_ssa_def *comp64[4];
      assert(component == 0 || component == 2);
      unsigned dest_comp = 0;
      while (dest_comp < intrin->dest.ssa.num_components) {
         const unsigned num_comps =
            MIN2(intrin->dest.ssa.num_components - dest_comp,
                 (4 - component) / 2);

         nir_ssa_def *data32 =
            emit_load(state, vertex_index, var, offset, component,
                      num_comps * 2, 32, nir_type_uint32);
         for (unsigned i = 0; i < num_comps; i++) {
            comp64[dest_comp + i] =
               nir_pack_64_2x32(b, nir_channels(b, data32, 3 << (i * 2)));
         }

         /* Only the first store has a component offset */
         component = 0;
         dest_comp += num_comps;
         offset = nir_iadd_imm(b, offset, slot_size);
      }

      return nir_vec(b, comp64, intrin->dest.ssa.num_components);
   } else {
      return emit_load(state, vertex_index, var, offset, component,
                       intrin->dest.ssa.num_components,
                       intrin->dest.ssa.bit_size,
                       nir_get_nir_type_for_glsl_type(type));
   }
}

static void
emit_store(struct lower_io_state *state, nir_ssa_def *data,
           nir_ssa_def *vertex_index, nir_variable *var, nir_ssa_def *offset,
           unsigned component, unsigned num_components,
           nir_component_mask_t write_mask, nir_alu_type type)
{
   nir_builder *b = &state->builder;
   nir_variable_mode mode = var->data.mode;

   nir_intrinsic_op op;
   if (mode == nir_var_mem_shared) {
      op = nir_intrinsic_store_shared;
   } else {
      assert(mode == nir_var_shader_out);
      op = vertex_index ? nir_intrinsic_store_per_vertex_output :
                          nir_intrinsic_store_output;
   }

   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(state->builder.shader, op);
   store->num_components = num_components;

   store->src[0] = nir_src_for_ssa(data);

   nir_intrinsic_set_base(store, var->data.driver_location);

   if (mode == nir_var_shader_out)
      nir_intrinsic_set_component(store, component);

   if (store->intrinsic == nir_intrinsic_store_output)
      nir_intrinsic_set_type(store, type);

   nir_intrinsic_set_write_mask(store, write_mask);

   if (vertex_index)
      store->src[1] = nir_src_for_ssa(vertex_index);

   store->src[vertex_index ? 2 : 1] = nir_src_for_ssa(offset);

   nir_builder_instr_insert(b, &store->instr);
}

static void
lower_store(nir_intrinsic_instr *intrin, struct lower_io_state *state,
            nir_ssa_def *vertex_index, nir_variable *var, nir_ssa_def *offset,
            unsigned component, const struct glsl_type *type)
{
   assert(intrin->src[1].is_ssa);
   if (intrin->src[1].ssa->bit_size == 64 &&
       (state->options & nir_lower_io_lower_64bit_to_32)) {
      nir_builder *b = &state->builder;

      const unsigned slot_size = state->type_size(glsl_dvec_type(2), false);

      assert(component == 0 || component == 2);
      unsigned src_comp = 0;
      nir_component_mask_t write_mask = nir_intrinsic_write_mask(intrin);
      while (src_comp < intrin->num_components) {
         const unsigned num_comps =
            MIN2(intrin->num_components - src_comp,
                 (4 - component) / 2);

         if (write_mask & BITFIELD_MASK(num_comps)) {
            nir_ssa_def *data =
               nir_channels(b, intrin->src[1].ssa,
                            BITFIELD_RANGE(src_comp, num_comps));
            nir_ssa_def *data32 = nir_bitcast_vector(b, data, 32);

            nir_component_mask_t write_mask32 = 0;
            for (unsigned i = 0; i < num_comps; i++) {
               if (write_mask & BITFIELD_MASK(num_comps) & (1 << i))
                  write_mask32 |= 3 << (i * 2);
            }

            emit_store(state, data32, vertex_index, var, offset,
                       component, data32->num_components, write_mask32,
                       nir_type_uint32);
         }

         /* Only the first store has a component offset */
         component = 0;
         src_comp += num_comps;
         write_mask >>= num_comps;
         offset = nir_iadd_imm(b, offset, slot_size);
      }
   } else {
      emit_store(state, intrin->src[1].ssa, vertex_index, var, offset,
                 component, intrin->num_components,
                 nir_intrinsic_write_mask(intrin),
                 nir_get_nir_type_for_glsl_type(type));
   }
}

static nir_ssa_def *
lower_atomic(nir_intrinsic_instr *intrin, struct lower_io_state *state,
             nir_variable *var, nir_ssa_def *offset)
{
   nir_builder *b = &state->builder;
   assert(var->data.mode == nir_var_mem_shared);

   nir_intrinsic_op op = shared_atomic_for_deref(intrin->intrinsic);

   nir_intrinsic_instr *atomic =
      nir_intrinsic_instr_create(state->builder.shader, op);

   nir_intrinsic_set_base(atomic, var->data.driver_location);

   atomic->src[0] = nir_src_for_ssa(offset);
   assert(nir_intrinsic_infos[intrin->intrinsic].num_srcs ==
          nir_intrinsic_infos[op].num_srcs);
   for (unsigned i = 1; i < nir_intrinsic_infos[op].num_srcs; i++) {
      nir_src_copy(&atomic->src[i], &intrin->src[i], atomic);
   }

   if (nir_intrinsic_infos[op].has_dest) {
      assert(intrin->dest.is_ssa);
      assert(nir_intrinsic_infos[intrin->intrinsic].has_dest);
      nir_ssa_dest_init(&atomic->instr, &atomic->dest,
                        intrin->dest.ssa.num_components,
                        intrin->dest.ssa.bit_size, NULL);
   }

   nir_builder_instr_insert(b, &atomic->instr);

   return nir_intrinsic_infos[op].has_dest ? &atomic->dest.ssa : NULL;
}

static nir_ssa_def *
lower_interpolate_at(nir_intrinsic_instr *intrin, struct lower_io_state *state,
                     nir_variable *var, nir_ssa_def *offset, unsigned component,
                     const struct glsl_type *type)
{
   nir_builder *b = &state->builder;
   assert(var->data.mode == nir_var_shader_in);

   /* Ignore interpolateAt() for flat variables - flat is flat. */
   if (var->data.interpolation == INTERP_MODE_FLAT)
      return lower_load(intrin, state, NULL, var, offset, component, type);

   /* None of the supported APIs allow interpolation on 64-bit things */
   assert(intrin->dest.is_ssa && intrin->dest.ssa.bit_size <= 32);

   nir_intrinsic_op bary_op;
   switch (intrin->intrinsic) {
   case nir_intrinsic_interp_deref_at_centroid:
      bary_op = (state->options & nir_lower_io_force_sample_interpolation) ?
                nir_intrinsic_load_barycentric_sample :
                nir_intrinsic_load_barycentric_centroid;
      break;
   case nir_intrinsic_interp_deref_at_sample:
      bary_op = nir_intrinsic_load_barycentric_at_sample;
      break;
   case nir_intrinsic_interp_deref_at_offset:
      bary_op = nir_intrinsic_load_barycentric_at_offset;
      break;
   default:
      unreachable("Bogus interpolateAt() intrinsic.");
   }

   nir_intrinsic_instr *bary_setup =
      nir_intrinsic_instr_create(state->builder.shader, bary_op);

   nir_ssa_dest_init(&bary_setup->instr, &bary_setup->dest, 2, 32, NULL);
   nir_intrinsic_set_interp_mode(bary_setup, var->data.interpolation);

   if (intrin->intrinsic == nir_intrinsic_interp_deref_at_sample ||
       intrin->intrinsic == nir_intrinsic_interp_deref_at_offset)
      nir_src_copy(&bary_setup->src[0], &intrin->src[1], bary_setup);

   nir_builder_instr_insert(b, &bary_setup->instr);

   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(state->builder.shader,
                                 nir_intrinsic_load_interpolated_input);
   load->num_components = intrin->num_components;

   nir_intrinsic_set_base(load, var->data.driver_location);
   nir_intrinsic_set_component(load, component);

   load->src[0] = nir_src_for_ssa(&bary_setup->dest.ssa);
   load->src[1] = nir_src_for_ssa(offset);

   assert(intrin->dest.is_ssa);
   nir_ssa_dest_init(&load->instr, &load->dest,
                     intrin->dest.ssa.num_components,
                     intrin->dest.ssa.bit_size, NULL);
   nir_builder_instr_insert(b, &load->instr);

   return &load->dest.ssa;
}

static bool
nir_lower_io_block(nir_block *block,
                   struct lower_io_state *state)
{
   nir_builder *b = &state->builder;
   const nir_shader_compiler_options *options = b->shader->options;
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_deref:
      case nir_intrinsic_store_deref:
      case nir_intrinsic_deref_atomic_add:
      case nir_intrinsic_deref_atomic_imin:
      case nir_intrinsic_deref_atomic_umin:
      case nir_intrinsic_deref_atomic_imax:
      case nir_intrinsic_deref_atomic_umax:
      case nir_intrinsic_deref_atomic_and:
      case nir_intrinsic_deref_atomic_or:
      case nir_intrinsic_deref_atomic_xor:
      case nir_intrinsic_deref_atomic_exchange:
      case nir_intrinsic_deref_atomic_comp_swap:
      case nir_intrinsic_deref_atomic_fadd:
      case nir_intrinsic_deref_atomic_fmin:
      case nir_intrinsic_deref_atomic_fmax:
      case nir_intrinsic_deref_atomic_fcomp_swap:
         /* We can lower the io for this nir instrinsic */
         break;
      case nir_intrinsic_interp_deref_at_centroid:
      case nir_intrinsic_interp_deref_at_sample:
      case nir_intrinsic_interp_deref_at_offset:
         /* We can optionally lower these to load_interpolated_input */
         if (options->use_interpolated_input_intrinsics)
            break;
      default:
         /* We can't lower the io for this nir instrinsic, so skip it */
         continue;
      }

      nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

      nir_variable_mode mode = deref->mode;

      if ((state->modes & mode) == 0)
         continue;

      if (mode != nir_var_shader_in &&
          mode != nir_var_shader_out &&
          mode != nir_var_mem_shared &&
          mode != nir_var_uniform)
         continue;

      nir_variable *var = nir_deref_instr_get_variable(deref);

      b->cursor = nir_before_instr(instr);

      const bool per_vertex = nir_is_per_vertex_io(var, b->shader->info.stage);

      nir_ssa_def *offset;
      nir_ssa_def *vertex_index = NULL;
      unsigned component_offset = var->data.location_frac;
      bool bindless_type_size = mode == nir_var_shader_in ||
                                mode == nir_var_shader_out ||
                                var->data.bindless;

      offset = get_io_offset(b, deref, per_vertex ? &vertex_index : NULL,
                             state->type_size, &component_offset,
                             bindless_type_size);

      nir_ssa_def *replacement = NULL;

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_deref:
         replacement = lower_load(intrin, state, vertex_index, var, offset,
                                  component_offset, deref->type);
         break;

      case nir_intrinsic_store_deref:
         lower_store(intrin, state, vertex_index, var, offset,
                     component_offset, deref->type);
         break;

      case nir_intrinsic_deref_atomic_add:
      case nir_intrinsic_deref_atomic_imin:
      case nir_intrinsic_deref_atomic_umin:
      case nir_intrinsic_deref_atomic_imax:
      case nir_intrinsic_deref_atomic_umax:
      case nir_intrinsic_deref_atomic_and:
      case nir_intrinsic_deref_atomic_or:
      case nir_intrinsic_deref_atomic_xor:
      case nir_intrinsic_deref_atomic_exchange:
      case nir_intrinsic_deref_atomic_comp_swap:
      case nir_intrinsic_deref_atomic_fadd:
      case nir_intrinsic_deref_atomic_fmin:
      case nir_intrinsic_deref_atomic_fmax:
      case nir_intrinsic_deref_atomic_fcomp_swap:
         assert(vertex_index == NULL);
         replacement = lower_atomic(intrin, state, var, offset);
         break;

      case nir_intrinsic_interp_deref_at_centroid:
      case nir_intrinsic_interp_deref_at_sample:
      case nir_intrinsic_interp_deref_at_offset:
         assert(vertex_index == NULL);
         replacement = lower_interpolate_at(intrin, state, var, offset,
                                            component_offset, deref->type);
         break;

      default:
         continue;
      }

      if (replacement) {
         nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                  nir_src_for_ssa(replacement));
      }
      nir_instr_remove(&intrin->instr);
      progress = true;
   }

   return progress;
}

static bool
nir_lower_io_impl(nir_function_impl *impl,
                  nir_variable_mode modes,
                  int (*type_size)(const struct glsl_type *, bool),
                  nir_lower_io_options options)
{
   struct lower_io_state state;
   bool progress = false;

   nir_builder_init(&state.builder, impl);
   state.dead_ctx = ralloc_context(NULL);
   state.modes = modes;
   state.type_size = type_size;
   state.options = options;

   nir_foreach_block(block, impl) {
      progress |= nir_lower_io_block(block, &state);
   }

   ralloc_free(state.dead_ctx);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
   return progress;
}

bool
nir_lower_io(nir_shader *shader, nir_variable_mode modes,
             int (*type_size)(const struct glsl_type *, bool),
             nir_lower_io_options options)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         progress |= nir_lower_io_impl(function->impl, modes,
                                       type_size, options);
      }
   }

   return progress;
}

static unsigned
type_scalar_size_bytes(const struct glsl_type *type)
{
   assert(glsl_type_is_vector_or_scalar(type) ||
          glsl_type_is_matrix(type));
   return glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
}

static nir_ssa_def *
build_addr_iadd(nir_builder *b, nir_ssa_def *addr,
                nir_address_format addr_format, nir_ssa_def *offset)
{
   assert(offset->num_components == 1);
   assert(addr->bit_size == offset->bit_size);

   switch (addr_format) {
   case nir_address_format_32bit_global:
   case nir_address_format_64bit_global:
   case nir_address_format_32bit_offset:
      assert(addr->num_components == 1);
      return nir_iadd(b, addr, offset);

   case nir_address_format_64bit_bounded_global:
      assert(addr->num_components == 4);
      return nir_vec4(b, nir_channel(b, addr, 0),
                         nir_channel(b, addr, 1),
                         nir_channel(b, addr, 2),
                         nir_iadd(b, nir_channel(b, addr, 3), offset));

   case nir_address_format_32bit_index_offset:
      assert(addr->num_components == 2);
      return nir_vec2(b, nir_channel(b, addr, 0),
                         nir_iadd(b, nir_channel(b, addr, 1), offset));
   case nir_address_format_logical:
      unreachable("Unsupported address format");
   }
   unreachable("Invalid address format");
}

static nir_ssa_def *
build_addr_iadd_imm(nir_builder *b, nir_ssa_def *addr,
                    nir_address_format addr_format, int64_t offset)
{
   return build_addr_iadd(b, addr, addr_format,
                             nir_imm_intN_t(b, offset, addr->bit_size));
}

static nir_ssa_def *
addr_to_index(nir_builder *b, nir_ssa_def *addr,
              nir_address_format addr_format)
{
   assert(addr_format == nir_address_format_32bit_index_offset);
   assert(addr->num_components == 2);
   return nir_channel(b, addr, 0);
}

static nir_ssa_def *
addr_to_offset(nir_builder *b, nir_ssa_def *addr,
               nir_address_format addr_format)
{
   assert(addr_format == nir_address_format_32bit_index_offset);
   assert(addr->num_components == 2);
   return nir_channel(b, addr, 1);
}

/** Returns true if the given address format resolves to a global address */
static bool
addr_format_is_global(nir_address_format addr_format)
{
   return addr_format == nir_address_format_32bit_global ||
          addr_format == nir_address_format_64bit_global ||
          addr_format == nir_address_format_64bit_bounded_global;
}

static nir_ssa_def *
addr_to_global(nir_builder *b, nir_ssa_def *addr,
               nir_address_format addr_format)
{
   switch (addr_format) {
   case nir_address_format_32bit_global:
   case nir_address_format_64bit_global:
      assert(addr->num_components == 1);
      return addr;

   case nir_address_format_64bit_bounded_global:
      assert(addr->num_components == 4);
      return nir_iadd(b, nir_pack_64_2x32(b, nir_channels(b, addr, 0x3)),
                         nir_u2u64(b, nir_channel(b, addr, 3)));

   case nir_address_format_32bit_index_offset:
   case nir_address_format_32bit_offset:
   case nir_address_format_logical:
      unreachable("Cannot get a 64-bit address with this address format");
   }

   unreachable("Invalid address format");
}

static bool
addr_format_needs_bounds_check(nir_address_format addr_format)
{
   return addr_format == nir_address_format_64bit_bounded_global;
}

static nir_ssa_def *
addr_is_in_bounds(nir_builder *b, nir_ssa_def *addr,
                  nir_address_format addr_format, unsigned size)
{
   assert(addr_format == nir_address_format_64bit_bounded_global);
   assert(addr->num_components == 4);
   return nir_ige(b, nir_channel(b, addr, 2),
                     nir_iadd_imm(b, nir_channel(b, addr, 3), size));
}

static nir_ssa_def *
build_explicit_io_load(nir_builder *b, nir_intrinsic_instr *intrin,
                       nir_ssa_def *addr, nir_address_format addr_format,
                       unsigned num_components)
{
   nir_variable_mode mode = nir_src_as_deref(intrin->src[0])->mode;

   nir_intrinsic_op op;
   switch (mode) {
   case nir_var_mem_ubo:
      op = nir_intrinsic_load_ubo;
      break;
   case nir_var_mem_ssbo:
      if (addr_format_is_global(addr_format))
         op = nir_intrinsic_load_global;
      else
         op = nir_intrinsic_load_ssbo;
      break;
   case nir_var_mem_global:
      assert(addr_format_is_global(addr_format));
      op = nir_intrinsic_load_global;
      break;
   case nir_var_shader_in:
      assert(addr_format_is_global(addr_format));
      op = nir_intrinsic_load_kernel_input;
      break;
   case nir_var_mem_shared:
      assert(addr_format == nir_address_format_32bit_offset);
      op = nir_intrinsic_load_shared;
      break;
   default:
      unreachable("Unsupported explicit IO variable mode");
   }

   nir_intrinsic_instr *load = nir_intrinsic_instr_create(b->shader, op);

   if (addr_format_is_global(addr_format)) {
      load->src[0] = nir_src_for_ssa(addr_to_global(b, addr, addr_format));
   } else if (addr_format == nir_address_format_32bit_offset) {
      assert(addr->num_components == 1);
      load->src[0] = nir_src_for_ssa(addr);
   } else {
      load->src[0] = nir_src_for_ssa(addr_to_index(b, addr, addr_format));
      load->src[1] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   }

   if (mode != nir_var_mem_ubo && mode != nir_var_shader_in && mode != nir_var_mem_shared)
      nir_intrinsic_set_access(load, nir_intrinsic_access(intrin));

   unsigned bit_size = intrin->dest.ssa.bit_size;
   if (bit_size == 1) {
      /* TODO: Make the native bool bit_size an option. */
      bit_size = 32;
   }

   /* TODO: We should try and provide a better alignment.  For OpenCL, we need
    * to plumb the alignment through from SPIR-V when we have one.
    */
   nir_intrinsic_set_align(load, bit_size / 8, 0);

   assert(intrin->dest.is_ssa);
   load->num_components = num_components;
   nir_ssa_dest_init(&load->instr, &load->dest, num_components,
                     bit_size, intrin->dest.ssa.name);

   assert(bit_size % 8 == 0);

   nir_ssa_def *result;
   if (addr_format_needs_bounds_check(addr_format)) {
      /* The Vulkan spec for robustBufferAccess gives us quite a few options
       * as to what we can do with an OOB read.  Unfortunately, returning
       * undefined values isn't one of them so we return an actual zero.
       */
      nir_ssa_def *zero = nir_imm_zero(b, load->num_components, bit_size);

      const unsigned load_size = (bit_size / 8) * load->num_components;
      nir_push_if(b, addr_is_in_bounds(b, addr, addr_format, load_size));

      nir_builder_instr_insert(b, &load->instr);

      nir_pop_if(b, NULL);

      result = nir_if_phi(b, &load->dest.ssa, zero);
   } else {
      nir_builder_instr_insert(b, &load->instr);
      result = &load->dest.ssa;
   }

   if (intrin->dest.ssa.bit_size == 1)
      result = nir_i2b(b, result);

   return result;
}

static void
build_explicit_io_store(nir_builder *b, nir_intrinsic_instr *intrin,
                        nir_ssa_def *addr, nir_address_format addr_format,
                        nir_ssa_def *value, nir_component_mask_t write_mask)
{
   nir_variable_mode mode = nir_src_as_deref(intrin->src[0])->mode;

   nir_intrinsic_op op;
   switch (mode) {
   case nir_var_mem_ssbo:
      if (addr_format_is_global(addr_format))
         op = nir_intrinsic_store_global;
      else
         op = nir_intrinsic_store_ssbo;
      break;
   case nir_var_mem_global:
      assert(addr_format_is_global(addr_format));
      op = nir_intrinsic_store_global;
      break;
   case nir_var_mem_shared:
      assert(addr_format == nir_address_format_32bit_offset);
      op = nir_intrinsic_store_shared;
      break;
   default:
      unreachable("Unsupported explicit IO variable mode");
   }

   nir_intrinsic_instr *store = nir_intrinsic_instr_create(b->shader, op);

   if (value->bit_size == 1) {
      /* TODO: Make the native bool bit_size an option. */
      value = nir_b2i(b, value, 32);
   }

   store->src[0] = nir_src_for_ssa(value);
   if (addr_format_is_global(addr_format)) {
      store->src[1] = nir_src_for_ssa(addr_to_global(b, addr, addr_format));
   } else if (addr_format == nir_address_format_32bit_offset) {
      assert(addr->num_components == 1);
      store->src[1] = nir_src_for_ssa(addr);
   } else {
      store->src[1] = nir_src_for_ssa(addr_to_index(b, addr, addr_format));
      store->src[2] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   }

   nir_intrinsic_set_write_mask(store, write_mask);

   if (mode != nir_var_mem_shared)
      nir_intrinsic_set_access(store, nir_intrinsic_access(intrin));

   /* TODO: We should try and provide a better alignment.  For OpenCL, we need
    * to plumb the alignment through from SPIR-V when we have one.
    */
   nir_intrinsic_set_align(store, value->bit_size / 8, 0);

   assert(value->num_components == 1 ||
          value->num_components == intrin->num_components);
   store->num_components = value->num_components;

   assert(value->bit_size % 8 == 0);

   if (addr_format_needs_bounds_check(addr_format)) {
      const unsigned store_size = (value->bit_size / 8) * store->num_components;
      nir_push_if(b, addr_is_in_bounds(b, addr, addr_format, store_size));

      nir_builder_instr_insert(b, &store->instr);

      nir_pop_if(b, NULL);
   } else {
      nir_builder_instr_insert(b, &store->instr);
   }
}

static nir_ssa_def *
build_explicit_io_atomic(nir_builder *b, nir_intrinsic_instr *intrin,
                         nir_ssa_def *addr, nir_address_format addr_format)
{
   nir_variable_mode mode = nir_src_as_deref(intrin->src[0])->mode;
   const unsigned num_data_srcs =
      nir_intrinsic_infos[intrin->intrinsic].num_srcs - 1;

   nir_intrinsic_op op;
   switch (mode) {
   case nir_var_mem_ssbo:
      if (addr_format_is_global(addr_format))
         op = global_atomic_for_deref(intrin->intrinsic);
      else
         op = ssbo_atomic_for_deref(intrin->intrinsic);
      break;
   case nir_var_mem_global:
      assert(addr_format_is_global(addr_format));
      op = global_atomic_for_deref(intrin->intrinsic);
      break;
   case nir_var_mem_shared:
      assert(addr_format == nir_address_format_32bit_offset);
      op = shared_atomic_for_deref(intrin->intrinsic);
      break;
   default:
      unreachable("Unsupported explicit IO variable mode");
   }

   nir_intrinsic_instr *atomic = nir_intrinsic_instr_create(b->shader, op);

   unsigned src = 0;
   if (addr_format_is_global(addr_format)) {
      atomic->src[src++] = nir_src_for_ssa(addr_to_global(b, addr, addr_format));
   } else if (addr_format == nir_address_format_32bit_offset) {
      assert(addr->num_components == 1);
      atomic->src[src++] = nir_src_for_ssa(addr);
   } else {
      atomic->src[src++] = nir_src_for_ssa(addr_to_index(b, addr, addr_format));
      atomic->src[src++] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   }
   for (unsigned i = 0; i < num_data_srcs; i++) {
      atomic->src[src++] = nir_src_for_ssa(intrin->src[1 + i].ssa);
   }

   /* Global atomics don't have access flags because they assume that the
    * address may be non-uniform.
    */
   if (!addr_format_is_global(addr_format) && mode != nir_var_mem_shared)
      nir_intrinsic_set_access(atomic, nir_intrinsic_access(intrin));

   assert(intrin->dest.ssa.num_components == 1);
   nir_ssa_dest_init(&atomic->instr, &atomic->dest,
                     1, intrin->dest.ssa.bit_size, intrin->dest.ssa.name);

   assert(atomic->dest.ssa.bit_size % 8 == 0);

   if (addr_format_needs_bounds_check(addr_format)) {
      const unsigned atomic_size = atomic->dest.ssa.bit_size / 8;
      nir_push_if(b, addr_is_in_bounds(b, addr, addr_format, atomic_size));

      nir_builder_instr_insert(b, &atomic->instr);

      nir_pop_if(b, NULL);
      return nir_if_phi(b, &atomic->dest.ssa,
                           nir_ssa_undef(b, 1, atomic->dest.ssa.bit_size));
   } else {
      nir_builder_instr_insert(b, &atomic->instr);
      return &atomic->dest.ssa;
   }
}

nir_ssa_def *
nir_explicit_io_address_from_deref(nir_builder *b, nir_deref_instr *deref,
                                   nir_ssa_def *base_addr,
                                   nir_address_format addr_format)
{
   assert(deref->dest.is_ssa);
   switch (deref->deref_type) {
   case nir_deref_type_var:
      assert(deref->mode & (nir_var_shader_in | nir_var_mem_shared));
      return nir_imm_intN_t(b, deref->var->data.driver_location,
                            deref->dest.ssa.bit_size);

   case nir_deref_type_array: {
      nir_deref_instr *parent = nir_deref_instr_parent(deref);

      unsigned stride = glsl_get_explicit_stride(parent->type);
      if ((glsl_type_is_matrix(parent->type) &&
           glsl_matrix_type_is_row_major(parent->type)) ||
          (glsl_type_is_vector(parent->type) && stride == 0))
         stride = type_scalar_size_bytes(parent->type);

      assert(stride > 0);

      nir_ssa_def *index = nir_ssa_for_src(b, deref->arr.index, 1);
      index = nir_i2i(b, index, base_addr->bit_size);
      return build_addr_iadd(b, base_addr, addr_format,
                                nir_amul_imm(b, index, stride));
   }

   case nir_deref_type_ptr_as_array: {
      nir_ssa_def *index = nir_ssa_for_src(b, deref->arr.index, 1);
      index = nir_i2i(b, index, base_addr->bit_size);
      unsigned stride = nir_deref_instr_ptr_as_array_stride(deref);
      return build_addr_iadd(b, base_addr, addr_format,
                                nir_amul_imm(b, index, stride));
   }

   case nir_deref_type_array_wildcard:
      unreachable("Wildcards should be lowered by now");
      break;

   case nir_deref_type_struct: {
      nir_deref_instr *parent = nir_deref_instr_parent(deref);
      int offset = glsl_get_struct_field_offset(parent->type,
                                                deref->strct.index);
      assert(offset >= 0);
      return build_addr_iadd_imm(b, base_addr, addr_format, offset);
   }

   case nir_deref_type_cast:
      /* Nothing to do here */
      return base_addr;
   }

   unreachable("Invalid NIR deref type");
}

void
nir_lower_explicit_io_instr(nir_builder *b,
                            nir_intrinsic_instr *intrin,
                            nir_ssa_def *addr,
                            nir_address_format addr_format)
{
   b->cursor = nir_after_instr(&intrin->instr);

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   unsigned vec_stride = glsl_get_explicit_stride(deref->type);
   unsigned scalar_size = type_scalar_size_bytes(deref->type);
   assert(vec_stride == 0 || glsl_type_is_vector(deref->type));
   assert(vec_stride == 0 || vec_stride >= scalar_size);

   if (intrin->intrinsic == nir_intrinsic_load_deref) {
      nir_ssa_def *value;
      if (vec_stride > scalar_size) {
         nir_ssa_def *comps[4] = { NULL, };
         for (unsigned i = 0; i < intrin->num_components; i++) {
            nir_ssa_def *comp_addr = build_addr_iadd_imm(b, addr, addr_format,
                                                         vec_stride * i);
            comps[i] = build_explicit_io_load(b, intrin, comp_addr,
                                              addr_format, 1);
         }
         value = nir_vec(b, comps, intrin->num_components);
      } else {
         value = build_explicit_io_load(b, intrin, addr, addr_format,
                                        intrin->num_components);
      }
      nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(value));
   } else if (intrin->intrinsic == nir_intrinsic_store_deref) {
      assert(intrin->src[1].is_ssa);
      nir_ssa_def *value = intrin->src[1].ssa;
      nir_component_mask_t write_mask = nir_intrinsic_write_mask(intrin);
      if (vec_stride > scalar_size) {
         for (unsigned i = 0; i < intrin->num_components; i++) {
            if (!(write_mask & (1 << i)))
               continue;

            nir_ssa_def *comp_addr = build_addr_iadd_imm(b, addr, addr_format,
                                                         vec_stride * i);
            build_explicit_io_store(b, intrin, comp_addr, addr_format,
                                    nir_channel(b, value, i), 1);
         }
      } else {
         build_explicit_io_store(b, intrin, addr, addr_format,
                                 value, write_mask);
      }
   } else {
      nir_ssa_def *value =
         build_explicit_io_atomic(b, intrin, addr, addr_format);
      nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(value));
   }

   nir_instr_remove(&intrin->instr);
}

static void
lower_explicit_io_deref(nir_builder *b, nir_deref_instr *deref,
                        nir_address_format addr_format)
{
   /* Just delete the deref if it's not used.  We can't use
    * nir_deref_instr_remove_if_unused here because it may remove more than
    * one deref which could break our list walking since we walk the list
    * backwards.
    */
   assert(list_is_empty(&deref->dest.ssa.if_uses));
   if (list_is_empty(&deref->dest.ssa.uses)) {
      nir_instr_remove(&deref->instr);
      return;
   }

   b->cursor = nir_after_instr(&deref->instr);

   nir_ssa_def *base_addr = NULL;
   if (deref->deref_type != nir_deref_type_var) {
      assert(deref->parent.is_ssa);
      base_addr = deref->parent.ssa;
   }

   nir_ssa_def *addr = nir_explicit_io_address_from_deref(b, deref, base_addr,
                                                          addr_format);

   nir_instr_remove(&deref->instr);
   nir_ssa_def_rewrite_uses(&deref->dest.ssa, nir_src_for_ssa(addr));
}

static void
lower_explicit_io_access(nir_builder *b, nir_intrinsic_instr *intrin,
                         nir_address_format addr_format)
{
   assert(intrin->src[0].is_ssa);
   nir_lower_explicit_io_instr(b, intrin, intrin->src[0].ssa, addr_format);
}

static void
lower_explicit_io_array_length(nir_builder *b, nir_intrinsic_instr *intrin,
                               nir_address_format addr_format)
{
   b->cursor = nir_after_instr(&intrin->instr);

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   assert(glsl_type_is_array(deref->type));
   assert(glsl_get_length(deref->type) == 0);
   unsigned stride = glsl_get_explicit_stride(deref->type);
   assert(stride > 0);

   assert(addr_format == nir_address_format_32bit_index_offset);
   nir_ssa_def *addr = &deref->dest.ssa;
   nir_ssa_def *index = addr_to_index(b, addr, addr_format);
   nir_ssa_def *offset = addr_to_offset(b, addr, addr_format);

   nir_intrinsic_instr *bsize =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_get_buffer_size);
   bsize->src[0] = nir_src_for_ssa(index);
   nir_ssa_dest_init(&bsize->instr, &bsize->dest, 1, 32, NULL);
   nir_builder_instr_insert(b, &bsize->instr);

   nir_ssa_def *arr_size =
      nir_idiv(b, nir_isub(b, &bsize->dest.ssa, offset),
                  nir_imm_int(b, stride));

   nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(arr_size));
   nir_instr_remove(&intrin->instr);
}

static bool
nir_lower_explicit_io_impl(nir_function_impl *impl, nir_variable_mode modes,
                           nir_address_format addr_format)
{
   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   /* Walk in reverse order so that we can see the full deref chain when we
    * lower the access operations.  We lower them assuming that the derefs
    * will be turned into address calculations later.
    */
   nir_foreach_block_reverse(block, impl) {
      nir_foreach_instr_reverse_safe(instr, block) {
         switch (instr->type) {
         case nir_instr_type_deref: {
            nir_deref_instr *deref = nir_instr_as_deref(instr);
            if (deref->mode & modes) {
               lower_explicit_io_deref(&b, deref, addr_format);
               progress = true;
            }
            break;
         }

         case nir_instr_type_intrinsic: {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_load_deref:
            case nir_intrinsic_store_deref:
            case nir_intrinsic_deref_atomic_add:
            case nir_intrinsic_deref_atomic_imin:
            case nir_intrinsic_deref_atomic_umin:
            case nir_intrinsic_deref_atomic_imax:
            case nir_intrinsic_deref_atomic_umax:
            case nir_intrinsic_deref_atomic_and:
            case nir_intrinsic_deref_atomic_or:
            case nir_intrinsic_deref_atomic_xor:
            case nir_intrinsic_deref_atomic_exchange:
            case nir_intrinsic_deref_atomic_comp_swap:
            case nir_intrinsic_deref_atomic_fadd:
            case nir_intrinsic_deref_atomic_fmin:
            case nir_intrinsic_deref_atomic_fmax:
            case nir_intrinsic_deref_atomic_fcomp_swap: {
               nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
               if (deref->mode & modes) {
                  lower_explicit_io_access(&b, intrin, addr_format);
                  progress = true;
               }
               break;
            }

            case nir_intrinsic_deref_buffer_array_length: {
               nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
               if (deref->mode & modes) {
                  lower_explicit_io_array_length(&b, intrin, addr_format);
                  progress = true;
               }
               break;
            }

            default:
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

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   }

   return progress;
}

bool
nir_lower_explicit_io(nir_shader *shader, nir_variable_mode modes,
                      nir_address_format addr_format)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl &&
          nir_lower_explicit_io_impl(function->impl, modes, addr_format))
         progress = true;
   }

   return progress;
}

static bool
nir_lower_vars_to_explicit_types_impl(nir_function_impl *impl,
                                      nir_variable_mode modes,
                                      glsl_type_size_align_func type_info)
{
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_deref)
            continue;

         nir_deref_instr *deref = nir_instr_as_deref(instr);
         if (!(deref->mode & modes))
            continue;

         unsigned size, alignment;
         const struct glsl_type *new_type =
            glsl_get_explicit_type_for_size_align(deref->type, type_info, &size, &alignment);
         if (new_type != deref->type) {
            progress = true;
            deref->type = new_type;
         }
         if (deref->deref_type == nir_deref_type_cast) {
            /* See also glsl_type::get_explicit_type_for_size_align() */
            unsigned new_stride = align(size, alignment);
            if (new_stride != deref->cast.ptr_stride) {
               deref->cast.ptr_stride = new_stride;
               progress = true;
            }
         }
      }
   }

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance |
                                  nir_metadata_live_ssa_defs |
                                  nir_metadata_loop_analysis);
   }

   return progress;
}

static bool
lower_vars_to_explicit(nir_shader *shader,
                       struct exec_list *vars, nir_variable_mode mode,
                       glsl_type_size_align_func type_info)
{
   bool progress = false;
   unsigned offset = 0;
   nir_foreach_variable(var, vars) {
      unsigned size, align;
      const struct glsl_type *explicit_type =
         glsl_get_explicit_type_for_size_align(var->type, type_info, &size, &align);

      if (explicit_type != var->type) {
         progress = true;
         var->type = explicit_type;
      }

      var->data.driver_location = ALIGN_POT(offset, align);
      offset = var->data.driver_location + size;
   }

   if (mode == nir_var_mem_shared) {
      shader->info.cs.shared_size = offset;
      shader->num_shared = offset;
   }

   return progress;
}

bool
nir_lower_vars_to_explicit_types(nir_shader *shader,
                                 nir_variable_mode modes,
                                 glsl_type_size_align_func type_info)
{
   /* TODO: Situations which need to be handled to support more modes:
    * - row-major matrices
    * - compact shader inputs/outputs
    * - interface types
    */
   nir_variable_mode supported = nir_var_mem_shared | nir_var_shader_temp | nir_var_function_temp;
   assert(!(modes & ~supported) && "unsupported");

   bool progress = false;

   if (modes & nir_var_mem_shared)
      progress |= lower_vars_to_explicit(shader, &shader->shared, nir_var_mem_shared, type_info);
   if (modes & nir_var_shader_temp)
      progress |= lower_vars_to_explicit(shader, &shader->globals, nir_var_shader_temp, type_info);

   nir_foreach_function(function, shader) {
      if (function->impl) {
         if (modes & nir_var_function_temp)
            progress |= lower_vars_to_explicit(shader, &function->impl->locals, nir_var_function_temp, type_info);

         progress |= nir_lower_vars_to_explicit_types_impl(function->impl, modes, type_info);
      }
   }

   return progress;
}

/**
 * Return the offset source for a load/store intrinsic.
 */
nir_src *
nir_get_io_offset_src(nir_intrinsic_instr *instr)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_input:
   case nir_intrinsic_load_output:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_uniform:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_load_fs_input_interp_deltas:
      return &instr->src[0];
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_scratch:
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

/**
 * Return the numeric constant that identify a NULL pointer for each address
 * format.
 */
const nir_const_value *
nir_address_format_null_value(nir_address_format addr_format)
{
   const static nir_const_value null_values[][NIR_MAX_VEC_COMPONENTS] = {
      [nir_address_format_32bit_global] = {{0}},
      [nir_address_format_64bit_global] = {{0}},
      [nir_address_format_64bit_bounded_global] = {{0}},
      [nir_address_format_32bit_index_offset] = {{.u32 = ~0}, {.u32 = ~0}},
      [nir_address_format_32bit_offset] = {{.u32 = ~0}},
      [nir_address_format_logical] = {{.u32 = ~0}},
   };

   assert(addr_format < ARRAY_SIZE(null_values));
   return null_values[addr_format];
}

nir_ssa_def *
nir_build_addr_ieq(nir_builder *b, nir_ssa_def *addr0, nir_ssa_def *addr1,
                   nir_address_format addr_format)
{
   switch (addr_format) {
   case nir_address_format_32bit_global:
   case nir_address_format_64bit_global:
   case nir_address_format_64bit_bounded_global:
   case nir_address_format_32bit_index_offset:
   case nir_address_format_32bit_offset:
      return nir_ball_iequal(b, addr0, addr1);

   case nir_address_format_logical:
      unreachable("Unsupported address format");
   }

   unreachable("Invalid address format");
}

nir_ssa_def *
nir_build_addr_isub(nir_builder *b, nir_ssa_def *addr0, nir_ssa_def *addr1,
                    nir_address_format addr_format)
{
   switch (addr_format) {
   case nir_address_format_32bit_global:
   case nir_address_format_64bit_global:
   case nir_address_format_32bit_offset:
      assert(addr0->num_components == 1);
      assert(addr1->num_components == 1);
      return nir_isub(b, addr0, addr1);

   case nir_address_format_64bit_bounded_global:
      return nir_isub(b, addr_to_global(b, addr0, addr_format),
                         addr_to_global(b, addr1, addr_format));

   case nir_address_format_32bit_index_offset:
      assert(addr0->num_components == 2);
      assert(addr1->num_components == 2);
      /* Assume the same buffer index. */
      return nir_isub(b, nir_channel(b, addr0, 1), nir_channel(b, addr1, 1));

   case nir_address_format_logical:
      unreachable("Unsupported address format");
   }

   unreachable("Invalid address format");
}

static bool
is_input(nir_intrinsic_instr *intrin)
{
   return intrin->intrinsic == nir_intrinsic_load_input ||
          intrin->intrinsic == nir_intrinsic_load_per_vertex_input ||
          intrin->intrinsic == nir_intrinsic_load_interpolated_input ||
          intrin->intrinsic == nir_intrinsic_load_fs_input_interp_deltas;
}

static bool
is_output(nir_intrinsic_instr *intrin)
{
   return intrin->intrinsic == nir_intrinsic_load_output ||
          intrin->intrinsic == nir_intrinsic_load_per_vertex_output ||
          intrin->intrinsic == nir_intrinsic_store_output ||
          intrin->intrinsic == nir_intrinsic_store_per_vertex_output;
}


/**
 * This pass adds constant offsets to instr->const_index[0] for input/output
 * intrinsics, and resets the offset source to 0.  Non-constant offsets remain
 * unchanged - since we don't know what part of a compound variable is
 * accessed, we allocate storage for the entire thing. For drivers that use
 * nir_lower_io_to_temporaries() before nir_lower_io(), this guarantees that
 * the offset source will be 0, so that they don't have to add it in manually.
 */

static bool
add_const_offset_to_base_block(nir_block *block, nir_builder *b,
                               nir_variable_mode mode)
{
   bool progress = false;
   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      if ((mode == nir_var_shader_in && is_input(intrin)) ||
          (mode == nir_var_shader_out && is_output(intrin))) {
         nir_src *offset = nir_get_io_offset_src(intrin);

         if (nir_src_is_const(*offset)) {
            intrin->const_index[0] += nir_src_as_uint(*offset);
            b->cursor = nir_before_instr(&intrin->instr);
            nir_instr_rewrite_src(&intrin->instr, offset,
                                  nir_src_for_ssa(nir_imm_int(b, 0)));
            progress = true;
         }
      }
   }

   return progress;
}

bool
nir_io_add_const_offset_to_base(nir_shader *nir, nir_variable_mode mode)
{
   bool progress = false;

   nir_foreach_function(f, nir) {
      if (f->impl) {
         nir_builder b;
         nir_builder_init(&b, f->impl);
         nir_foreach_block(block, f->impl) {
            progress |= add_const_offset_to_base_block(block, &b, mode);
         }
      }
   }

   return progress;
}

