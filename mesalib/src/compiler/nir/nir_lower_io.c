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

struct lower_io_state {
   void *dead_ctx;
   nir_builder builder;
   int (*type_size)(const struct glsl_type *type);
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

void
nir_assign_var_locations(struct exec_list *var_list, unsigned *size,
                         int (*type_size)(const struct glsl_type *))
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
      location += type_size(var->type);
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
              int (*type_size)(const struct glsl_type *),
              unsigned *component)
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
      return nir_imm_int(b, type_size(glsl_vec4_type()) * slot_offset);
   }

   /* Just emit code and let constant-folding go to town */
   nir_ssa_def *offset = nir_imm_int(b, 0);

   for (; *p; p++) {
      if ((*p)->deref_type == nir_deref_type_array) {
         unsigned size = type_size((*p)->type);

         nir_ssa_def *mul =
            nir_imul_imm(b, nir_ssa_for_src(b, (*p)->arr.index, 1), size);

         offset = nir_iadd(b, offset, mul);
      } else if ((*p)->deref_type == nir_deref_type_struct) {
         /* p starts at path[1], so this is safe */
         nir_deref_instr *parent = *(p - 1);

         unsigned field_offset = 0;
         for (unsigned i = 0; i < (*p)->strct.index; i++) {
            field_offset += type_size(glsl_get_struct_field(parent->type, i));
         }
         offset = nir_iadd_imm(b, offset, field_offset);
      } else {
         unreachable("Unsupported deref type");
      }
   }

   nir_deref_path_finish(&path);

   return offset;
}

static nir_intrinsic_instr *
lower_load(nir_intrinsic_instr *intrin, struct lower_io_state *state,
           nir_ssa_def *vertex_index, nir_variable *var, nir_ssa_def *offset,
           unsigned component)
{
   const nir_shader *nir = state->builder.shader;
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
            nir_ssa_def *vertex_index, nir_variable *var, nir_ssa_def *offset,
            unsigned component)
{
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
   store->num_components = intrin->num_components;

   nir_src_copy(&store->src[0], &intrin->src[1], store);

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
             nir_variable *var, nir_ssa_def *offset)
{
   assert(var->data.mode == nir_var_mem_shared);

   nir_intrinsic_op op;
   switch (intrin->intrinsic) {
#define OP(O) case nir_intrinsic_deref_##O: op = nir_intrinsic_shared_##O; break;
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
      unreachable("Invalid atomic");
   }

   nir_intrinsic_instr *atomic =
      nir_intrinsic_instr_create(state->builder.shader, op);

   nir_intrinsic_set_base(atomic, var->data.driver_location);

   atomic->src[0] = nir_src_for_ssa(offset);
   assert(nir_intrinsic_infos[intrin->intrinsic].num_srcs ==
          nir_intrinsic_infos[op].num_srcs);
   for (unsigned i = 1; i < nir_intrinsic_infos[op].num_srcs; i++) {
      nir_src_copy(&atomic->src[i], &intrin->src[i], atomic);
   }

   return atomic;
}

static nir_intrinsic_instr *
lower_interpolate_at(nir_intrinsic_instr *intrin, struct lower_io_state *state,
                     nir_variable *var, nir_ssa_def *offset, unsigned component)
{
   assert(var->data.mode == nir_var_shader_in);

   /* Ignore interpolateAt() for flat variables - flat is flat. */
   if (var->data.interpolation == INTERP_MODE_FLAT)
      return lower_load(intrin, state, NULL, var, offset, component);

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

      nir_variable *var = nir_deref_instr_get_variable(deref);
      nir_variable_mode mode = var->data.mode;

      if ((state->modes & mode) == 0)
         continue;

      if (mode != nir_var_shader_in &&
          mode != nir_var_shader_out &&
          mode != nir_var_mem_shared &&
          mode != nir_var_uniform)
         continue;

      b->cursor = nir_before_instr(instr);

      const bool per_vertex = nir_is_per_vertex_io(var, b->shader->info.stage);

      nir_ssa_def *offset;
      nir_ssa_def *vertex_index = NULL;
      unsigned component_offset = var->data.location_frac;

      offset = get_io_offset(b, deref, per_vertex ? &vertex_index : NULL,
                             state->type_size, &component_offset);

      nir_intrinsic_instr *replacement;

      switch (intrin->intrinsic) {
      case nir_intrinsic_load_deref:
         replacement = lower_load(intrin, state, vertex_index, var, offset,
                                  component_offset);
         break;

      case nir_intrinsic_store_deref:
         replacement = lower_store(intrin, state, vertex_index, var, offset,
                                   component_offset);
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
      progress = true;
   }

   return progress;
}

static bool
nir_lower_io_impl(nir_function_impl *impl,
                  nir_variable_mode modes,
                  int (*type_size)(const struct glsl_type *),
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
             int (*type_size)(const struct glsl_type *),
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
      assert(addr->num_components == 1);
      return nir_iadd(b, addr, offset);

   case nir_address_format_vk_index_offset:
      assert(addr->num_components == 2);
      return nir_vec2(b, nir_channel(b, addr, 0),
                         nir_iadd(b, nir_channel(b, addr, 1), offset));
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
   assert(addr_format == nir_address_format_vk_index_offset);
   assert(addr->num_components == 2);
   return nir_channel(b, addr, 0);
}

static nir_ssa_def *
addr_to_offset(nir_builder *b, nir_ssa_def *addr,
               nir_address_format addr_format)
{
   assert(addr_format == nir_address_format_vk_index_offset);
   assert(addr->num_components == 2);
   return nir_channel(b, addr, 1);
}

/** Returns true if the given address format resolves to a global address */
static bool
addr_format_is_global(nir_address_format addr_format)
{
   return addr_format == nir_address_format_32bit_global ||
          addr_format == nir_address_format_64bit_global;
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

   case nir_address_format_vk_index_offset:
      unreachable("Cannot get a 64-bit address with this address format");
   }

   unreachable("Invalid address format");
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
   default:
      unreachable("Unsupported explicit IO variable mode");
   }

   nir_intrinsic_instr *load = nir_intrinsic_instr_create(b->shader, op);

   if (addr_format_is_global(addr_format)) {
      assert(op == nir_intrinsic_load_global);
      load->src[0] = nir_src_for_ssa(addr_to_global(b, addr, addr_format));
   } else {
      load->src[0] = nir_src_for_ssa(addr_to_index(b, addr, addr_format));
      load->src[1] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   }

   if (mode != nir_var_mem_ubo)
      nir_intrinsic_set_access(load, nir_intrinsic_access(intrin));

   /* TODO: We should try and provide a better alignment.  For OpenCL, we need
    * to plumb the alignment through from SPIR-V when we have one.
    */
   nir_intrinsic_set_align(load, intrin->dest.ssa.bit_size / 8, 0);

   assert(intrin->dest.is_ssa);
   load->num_components = num_components;
   nir_ssa_dest_init(&load->instr, &load->dest, num_components,
                     intrin->dest.ssa.bit_size, intrin->dest.ssa.name);
   nir_builder_instr_insert(b, &load->instr);

   return &load->dest.ssa;
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
   default:
      unreachable("Unsupported explicit IO variable mode");
   }

   nir_intrinsic_instr *store = nir_intrinsic_instr_create(b->shader, op);

   store->src[0] = nir_src_for_ssa(value);
   if (addr_format_is_global(addr_format)) {
      store->src[1] = nir_src_for_ssa(addr_to_global(b, addr, addr_format));
   } else {
      store->src[1] = nir_src_for_ssa(addr_to_index(b, addr, addr_format));
      store->src[2] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   }

   nir_intrinsic_set_write_mask(store, write_mask);

   nir_intrinsic_set_access(store, nir_intrinsic_access(intrin));

   /* TODO: We should try and provide a better alignment.  For OpenCL, we need
    * to plumb the alignment through from SPIR-V when we have one.
    */
   nir_intrinsic_set_align(store, value->bit_size / 8, 0);

   assert(value->num_components == 1 ||
          value->num_components == intrin->num_components);
   store->num_components = value->num_components;
   nir_builder_instr_insert(b, &store->instr);
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
   default:
      unreachable("Unsupported explicit IO variable mode");
   }

   nir_intrinsic_instr *atomic = nir_intrinsic_instr_create(b->shader, op);

   unsigned src = 0;
   if (addr_format_is_global(addr_format)) {
      atomic->src[src++] = nir_src_for_ssa(addr_to_global(b, addr, addr_format));
   } else {
      atomic->src[src++] = nir_src_for_ssa(addr_to_index(b, addr, addr_format));
      atomic->src[src++] = nir_src_for_ssa(addr_to_offset(b, addr, addr_format));
   }
   for (unsigned i = 0; i < num_data_srcs; i++) {
      atomic->src[src++] = nir_src_for_ssa(intrin->src[1 + i].ssa);
   }

   assert(intrin->dest.ssa.num_components == 1);
   nir_ssa_dest_init(&atomic->instr, &atomic->dest,
                     1, intrin->dest.ssa.bit_size, intrin->dest.ssa.name);
   nir_builder_instr_insert(b, &atomic->instr);

   return &atomic->dest.ssa;
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
   assert(list_empty(&deref->dest.ssa.if_uses));
   if (list_empty(&deref->dest.ssa.uses)) {
      nir_instr_remove(&deref->instr);
      return;
   }

   b->cursor = nir_after_instr(&deref->instr);

   /* Var derefs must be lowered away by the driver */
   assert(deref->deref_type != nir_deref_type_var);

   assert(deref->parent.is_ssa);
   nir_ssa_def *parent_addr = deref->parent.ssa;

   nir_ssa_def *addr = NULL;
   assert(deref->dest.is_ssa);
   switch (deref->deref_type) {
   case nir_deref_type_var:
      unreachable("Must be lowered by the driver");
      break;

   case nir_deref_type_array: {
      nir_deref_instr *parent = nir_deref_instr_parent(deref);

      unsigned stride = glsl_get_explicit_stride(parent->type);
      if ((glsl_type_is_matrix(parent->type) &&
           glsl_matrix_type_is_row_major(parent->type)) ||
          (glsl_type_is_vector(parent->type) && stride == 0))
         stride = type_scalar_size_bytes(parent->type);

      assert(stride > 0);

      nir_ssa_def *index = nir_ssa_for_src(b, deref->arr.index, 1);
      index = nir_i2i(b, index, parent_addr->bit_size);
      addr = build_addr_iadd(b, parent_addr, addr_format,
                                nir_imul_imm(b, index, stride));
      break;
   }

   case nir_deref_type_ptr_as_array: {
      nir_ssa_def *index = nir_ssa_for_src(b, deref->arr.index, 1);
      index = nir_i2i(b, index, parent_addr->bit_size);
      unsigned stride = nir_deref_instr_ptr_as_array_stride(deref);
      addr = build_addr_iadd(b, parent_addr, addr_format,
                                nir_imul_imm(b, index, stride));
      break;
   }

   case nir_deref_type_array_wildcard:
      unreachable("Wildcards should be lowered by now");
      break;

   case nir_deref_type_struct: {
      nir_deref_instr *parent = nir_deref_instr_parent(deref);
      int offset = glsl_get_struct_field_offset(parent->type,
                                                deref->strct.index);
      assert(offset >= 0);
      addr = build_addr_iadd_imm(b, parent_addr, addr_format, offset);
      break;
   }

   case nir_deref_type_cast:
      /* Nothing to do here */
      addr = parent_addr;
      break;
   }

   nir_instr_remove(&deref->instr);
   nir_ssa_def_rewrite_uses(&deref->dest.ssa, nir_src_for_ssa(addr));
}

static void
lower_explicit_io_access(nir_builder *b, nir_intrinsic_instr *intrin,
                         nir_address_format addr_format)
{
   b->cursor = nir_after_instr(&intrin->instr);

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   unsigned vec_stride = glsl_get_explicit_stride(deref->type);
   unsigned scalar_size = type_scalar_size_bytes(deref->type);
   assert(vec_stride == 0 || glsl_type_is_vector(deref->type));
   assert(vec_stride == 0 || vec_stride >= scalar_size);

   nir_ssa_def *addr = &deref->dest.ssa;
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
      return &instr->src[0];
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_shared:
   case nir_intrinsic_store_global:
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
