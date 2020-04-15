/*
 * Copyright © 2015 Intel Corporation
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
#include "nir_deref.h"
#include "main/menums.h"

static void
get_deref_info(nir_shader *shader, nir_variable *var, nir_deref_instr *deref,
               bool *cross_invocation, bool *indirect)
{
   *cross_invocation = false;
   *indirect = false;

   const bool per_vertex = nir_is_per_vertex_io(var, shader->info.stage);

   nir_deref_path path;
   nir_deref_path_init(&path, deref, NULL);
   assert(path.path[0]->deref_type == nir_deref_type_var);
   nir_deref_instr **p = &path.path[1];

   /* Vertex index is the outermost array index. */
   if (per_vertex) {
      assert((*p)->deref_type == nir_deref_type_array);
      nir_instr *vertex_index_instr = (*p)->arr.index.ssa->parent_instr;
      *cross_invocation =
         vertex_index_instr->type != nir_instr_type_intrinsic ||
         nir_instr_as_intrinsic(vertex_index_instr)->intrinsic !=
            nir_intrinsic_load_invocation_id;
      p++;
   }

   /* We always lower indirect dereferences for "compact" array vars. */
   if (!path.path[0]->var->data.compact) {
      /* Non-compact array vars: find out if they are indirect. */
      for (; *p; p++) {
         if ((*p)->deref_type == nir_deref_type_array) {
            *indirect |= !nir_src_is_const((*p)->arr.index);
         } else if ((*p)->deref_type == nir_deref_type_struct) {
            /* Struct indices are always constant. */
         } else {
            unreachable("Unsupported deref type");
         }
      }
   }

   nir_deref_path_finish(&path);
}

static void
set_io_mask(nir_shader *shader, nir_variable *var, int offset, int len,
            nir_deref_instr *deref, bool is_output_read)
{
   for (int i = 0; i < len; i++) {
      assert(var->data.location != -1);

      int idx = var->data.location + offset + i;
      bool is_patch_generic = var->data.patch &&
                              idx != VARYING_SLOT_TESS_LEVEL_INNER &&
                              idx != VARYING_SLOT_TESS_LEVEL_OUTER &&
                              idx != VARYING_SLOT_BOUNDING_BOX0 &&
                              idx != VARYING_SLOT_BOUNDING_BOX1;
      uint64_t bitfield;

      if (is_patch_generic) {
         assert(idx >= VARYING_SLOT_PATCH0 && idx < VARYING_SLOT_TESS_MAX);
         bitfield = BITFIELD64_BIT(idx - VARYING_SLOT_PATCH0);
      }
      else {
         assert(idx < VARYING_SLOT_MAX);
         bitfield = BITFIELD64_BIT(idx);
      }

      bool cross_invocation;
      bool indirect;
      get_deref_info(shader, var, deref, &cross_invocation, &indirect);

      if (var->data.mode == nir_var_shader_in) {
         if (is_patch_generic) {
            shader->info.patch_inputs_read |= bitfield;
            if (indirect)
               shader->info.patch_inputs_read_indirectly |= bitfield;
         } else {
            shader->info.inputs_read |= bitfield;
            if (indirect)
               shader->info.inputs_read_indirectly |= bitfield;
         }

         if (cross_invocation && shader->info.stage == MESA_SHADER_TESS_CTRL)
            shader->info.tess.tcs_cross_invocation_inputs_read |= bitfield;

         if (shader->info.stage == MESA_SHADER_FRAGMENT) {
            shader->info.fs.uses_sample_qualifier |= var->data.sample;
         }
      } else {
         assert(var->data.mode == nir_var_shader_out);
         if (is_output_read) {
            if (is_patch_generic) {
               shader->info.patch_outputs_read |= bitfield;
               if (indirect)
                  shader->info.patch_outputs_accessed_indirectly |= bitfield;
            } else {
               shader->info.outputs_read |= bitfield;
               if (indirect)
                  shader->info.outputs_accessed_indirectly |= bitfield;
            }

            if (cross_invocation && shader->info.stage == MESA_SHADER_TESS_CTRL)
               shader->info.tess.tcs_cross_invocation_outputs_read |= bitfield;
         } else {
            if (is_patch_generic) {
               shader->info.patch_outputs_written |= bitfield;
               if (indirect)
                  shader->info.patch_outputs_accessed_indirectly |= bitfield;
            } else if (!var->data.read_only) {
               shader->info.outputs_written |= bitfield;
               if (indirect)
                  shader->info.outputs_accessed_indirectly |= bitfield;
            }
         }


         if (var->data.fb_fetch_output)
            shader->info.outputs_read |= bitfield;
      }
   }
}

/**
 * Mark an entire variable as used.  Caller must ensure that the variable
 * represents a shader input or output.
 */
static void
mark_whole_variable(nir_shader *shader, nir_variable *var,
                    nir_deref_instr *deref, bool is_output_read)
{
   const struct glsl_type *type = var->type;

   if (nir_is_per_vertex_io(var, shader->info.stage)) {
      assert(glsl_type_is_array(type));
      type = glsl_get_array_element(type);
   }

   if (var->data.per_view) {
      /* TODO: Per view and Per Vertex are not currently used together.  When
       * they start to be used (e.g. when adding Primitive Replication for GS
       * on Intel), verify that "peeling" the type twice is correct.  This
       * assert ensures we remember it.
       */
      assert(!nir_is_per_vertex_io(var, shader->info.stage));
      assert(glsl_type_is_array(type));
      type = glsl_get_array_element(type);
   }

   const unsigned slots =
      var->data.compact ? DIV_ROUND_UP(glsl_get_length(type), 4)
                        : glsl_count_attribute_slots(type, false);

   set_io_mask(shader, var, 0, slots, deref, is_output_read);
}

static unsigned
get_io_offset(nir_deref_instr *deref, bool is_vertex_input, bool per_vertex)
{
   unsigned offset = 0;

   for (nir_deref_instr *d = deref; d; d = nir_deref_instr_parent(d)) {
      if (d->deref_type == nir_deref_type_array) {
         if (per_vertex && nir_deref_instr_parent(d)->deref_type == nir_deref_type_var)
            break;

         if (!nir_src_is_const(d->arr.index))
            return -1;

         offset += glsl_count_attribute_slots(d->type, is_vertex_input) *
                   nir_src_as_uint(d->arr.index);
      }
      /* TODO: we can get the offset for structs here see nir_lower_io() */
   }

   return offset;
}

/**
 * Try to mark a portion of the given varying as used.  Caller must ensure
 * that the variable represents a shader input or output.
 *
 * If the index can't be interpreted as a constant, or some other problem
 * occurs, then nothing will be marked and false will be returned.
 */
static bool
try_mask_partial_io(nir_shader *shader, nir_variable *var,
                    nir_deref_instr *deref, bool is_output_read)
{
   const struct glsl_type *type = var->type;
   bool per_vertex = nir_is_per_vertex_io(var, shader->info.stage);

   if (per_vertex) {
      assert(glsl_type_is_array(type));
      type = glsl_get_array_element(type);
   }

   /* Per view variables will be considered as a whole. */
   if (var->data.per_view)
      return false;

   /* The code below only handles:
    *
    * - Indexing into matrices
    * - Indexing into arrays of (arrays, matrices, vectors, or scalars)
    *
    * For now, we just give up if we see varying structs and arrays of structs
    * here marking the entire variable as used.
    */
   if (!(glsl_type_is_matrix(type) ||
         (glsl_type_is_array(type) && !var->data.compact &&
          (glsl_type_is_numeric(glsl_without_array(type)) ||
           glsl_type_is_boolean(glsl_without_array(type)))))) {

      /* If we don't know how to handle this case, give up and let the
       * caller mark the whole variable as used.
       */
      return false;
   }

   unsigned offset = get_io_offset(deref, false, per_vertex);
   if (offset == -1)
      return false;

   unsigned num_elems;
   unsigned elem_width = 1;
   unsigned mat_cols = 1;
   if (glsl_type_is_array(type)) {
      num_elems = glsl_get_aoa_size(type);
      if (glsl_type_is_matrix(glsl_without_array(type)))
         mat_cols = glsl_get_matrix_columns(glsl_without_array(type));
   } else {
      num_elems = glsl_get_matrix_columns(type);
   }

   /* double element width for double types that takes two slots */
   if (glsl_type_is_dual_slot(glsl_without_array(type)))
      elem_width *= 2;

   if (offset >= num_elems * elem_width * mat_cols) {
      /* Constant index outside the bounds of the matrix/array.  This could
       * arise as a result of constant folding of a legal GLSL program.
       *
       * Even though the spec says that indexing outside the bounds of a
       * matrix/array results in undefined behaviour, we don't want to pass
       * out-of-range values to set_io_mask() (since this could result in
       * slots that don't exist being marked as used), so just let the caller
       * mark the whole variable as used.
       */
      return false;
   }

   set_io_mask(shader, var, offset, elem_width, deref, is_output_read);
   return true;
}

static void
gather_intrinsic_info(nir_intrinsic_instr *instr, nir_shader *shader,
                      void *dead_ctx)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_demote:
   case nir_intrinsic_demote_if:
      shader->info.fs.uses_demote = true;
   /* fallthrough: quads with helper lanes only might be discarded entirely */
   case nir_intrinsic_discard:
   case nir_intrinsic_discard_if:
      assert(shader->info.stage == MESA_SHADER_FRAGMENT);
      shader->info.fs.uses_discard = true;
      break;

   case nir_intrinsic_interp_deref_at_centroid:
   case nir_intrinsic_interp_deref_at_sample:
   case nir_intrinsic_interp_deref_at_offset:
   case nir_intrinsic_interp_deref_at_vertex:
   case nir_intrinsic_load_deref:
   case nir_intrinsic_store_deref:{
      nir_deref_instr *deref = nir_src_as_deref(instr->src[0]);
      if (deref->mode == nir_var_shader_in ||
          deref->mode == nir_var_shader_out) {
         nir_variable *var = nir_deref_instr_get_variable(deref);
         bool is_output_read = false;
         if (var->data.mode == nir_var_shader_out &&
             instr->intrinsic == nir_intrinsic_load_deref)
            is_output_read = true;

         if (!try_mask_partial_io(shader, var, deref, is_output_read))
            mark_whole_variable(shader, var, deref, is_output_read);

         /* We need to track which input_reads bits correspond to a
          * dvec3/dvec4 input attribute */
         if (shader->info.stage == MESA_SHADER_VERTEX &&
             var->data.mode == nir_var_shader_in &&
             glsl_type_is_dual_slot(glsl_without_array(var->type))) {
            for (unsigned i = 0; i < glsl_count_attribute_slots(var->type, false); i++) {
               int idx = var->data.location + i;
               shader->info.vs.double_inputs |= BITFIELD64_BIT(idx);
            }
         }
      }
      break;
   }

   case nir_intrinsic_load_draw_id:
   case nir_intrinsic_load_frag_coord:
   case nir_intrinsic_load_point_coord:
   case nir_intrinsic_load_front_face:
   case nir_intrinsic_load_vertex_id:
   case nir_intrinsic_load_vertex_id_zero_base:
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_first_vertex:
   case nir_intrinsic_load_is_indexed_draw:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_instance_id:
   case nir_intrinsic_load_sample_id:
   case nir_intrinsic_load_sample_pos:
   case nir_intrinsic_load_sample_mask_in:
   case nir_intrinsic_load_primitive_id:
   case nir_intrinsic_load_invocation_id:
   case nir_intrinsic_load_local_invocation_id:
   case nir_intrinsic_load_local_invocation_index:
   case nir_intrinsic_load_work_group_id:
   case nir_intrinsic_load_num_work_groups:
   case nir_intrinsic_load_tess_coord:
   case nir_intrinsic_load_tess_level_outer:
   case nir_intrinsic_load_tess_level_inner:
   case nir_intrinsic_load_patch_vertices_in:
      shader->info.system_values_read |=
         (1ull << nir_system_value_from_intrinsic(instr->intrinsic));
      break;

   case nir_intrinsic_quad_broadcast:
   case nir_intrinsic_quad_swap_horizontal:
   case nir_intrinsic_quad_swap_vertical:
   case nir_intrinsic_quad_swap_diagonal:
      if (shader->info.stage == MESA_SHADER_FRAGMENT)
         shader->info.fs.needs_helper_invocations = true;
      break;

   case nir_intrinsic_end_primitive:
   case nir_intrinsic_end_primitive_with_counter:
      assert(shader->info.stage == MESA_SHADER_GEOMETRY);
      shader->info.gs.uses_end_primitive = 1;
      /* fall through */

   case nir_intrinsic_emit_vertex:
   case nir_intrinsic_emit_vertex_with_counter:
      if (nir_intrinsic_stream_id(instr) > 0)
         shader->info.gs.uses_streams = true;

      break;

   case nir_intrinsic_bindless_image_atomic_add:
   case nir_intrinsic_bindless_image_atomic_and:
   case nir_intrinsic_bindless_image_atomic_comp_swap:
   case nir_intrinsic_bindless_image_atomic_dec_wrap:
   case nir_intrinsic_bindless_image_atomic_exchange:
   case nir_intrinsic_bindless_image_atomic_fadd:
   case nir_intrinsic_bindless_image_atomic_imax:
   case nir_intrinsic_bindless_image_atomic_imin:
   case nir_intrinsic_bindless_image_atomic_inc_wrap:
   case nir_intrinsic_bindless_image_atomic_or:
   case nir_intrinsic_bindless_image_atomic_umax:
   case nir_intrinsic_bindless_image_atomic_umin:
   case nir_intrinsic_bindless_image_atomic_xor:
   case nir_intrinsic_bindless_image_store:
   case nir_intrinsic_bindless_image_store_raw_intel:
   case nir_intrinsic_global_atomic_add:
   case nir_intrinsic_global_atomic_and:
   case nir_intrinsic_global_atomic_comp_swap:
   case nir_intrinsic_global_atomic_exchange:
   case nir_intrinsic_global_atomic_fadd:
   case nir_intrinsic_global_atomic_fcomp_swap:
   case nir_intrinsic_global_atomic_fmax:
   case nir_intrinsic_global_atomic_fmin:
   case nir_intrinsic_global_atomic_imax:
   case nir_intrinsic_global_atomic_imin:
   case nir_intrinsic_global_atomic_or:
   case nir_intrinsic_global_atomic_umax:
   case nir_intrinsic_global_atomic_umin:
   case nir_intrinsic_global_atomic_xor:
   case nir_intrinsic_image_atomic_add:
   case nir_intrinsic_image_atomic_and:
   case nir_intrinsic_image_atomic_comp_swap:
   case nir_intrinsic_image_atomic_dec_wrap:
   case nir_intrinsic_image_atomic_exchange:
   case nir_intrinsic_image_atomic_fadd:
   case nir_intrinsic_image_atomic_imax:
   case nir_intrinsic_image_atomic_imin:
   case nir_intrinsic_image_atomic_inc_wrap:
   case nir_intrinsic_image_atomic_or:
   case nir_intrinsic_image_atomic_umax:
   case nir_intrinsic_image_atomic_umin:
   case nir_intrinsic_image_atomic_xor:
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_image_deref_atomic_comp_swap:
   case nir_intrinsic_image_deref_atomic_dec_wrap:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_fadd:
   case nir_intrinsic_image_deref_atomic_imax:
   case nir_intrinsic_image_deref_atomic_imin:
   case nir_intrinsic_image_deref_atomic_inc_wrap:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_image_deref_atomic_umax:
   case nir_intrinsic_image_deref_atomic_umin:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_store_raw_intel:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_store_raw_intel:
   case nir_intrinsic_ssbo_atomic_add:
   case nir_intrinsic_ssbo_atomic_add_ir3:
   case nir_intrinsic_ssbo_atomic_and:
   case nir_intrinsic_ssbo_atomic_and_ir3:
   case nir_intrinsic_ssbo_atomic_comp_swap:
   case nir_intrinsic_ssbo_atomic_comp_swap_ir3:
   case nir_intrinsic_ssbo_atomic_exchange:
   case nir_intrinsic_ssbo_atomic_exchange_ir3:
   case nir_intrinsic_ssbo_atomic_fadd:
   case nir_intrinsic_ssbo_atomic_fcomp_swap:
   case nir_intrinsic_ssbo_atomic_fmax:
   case nir_intrinsic_ssbo_atomic_fmin:
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_ssbo_atomic_imax_ir3:
   case nir_intrinsic_ssbo_atomic_imin:
   case nir_intrinsic_ssbo_atomic_imin_ir3:
   case nir_intrinsic_ssbo_atomic_or:
   case nir_intrinsic_ssbo_atomic_or_ir3:
   case nir_intrinsic_ssbo_atomic_umax:
   case nir_intrinsic_ssbo_atomic_umax_ir3:
   case nir_intrinsic_ssbo_atomic_umin:
   case nir_intrinsic_ssbo_atomic_umin_ir3:
   case nir_intrinsic_ssbo_atomic_xor:
   case nir_intrinsic_ssbo_atomic_xor_ir3:
   case nir_intrinsic_store_global:
   case nir_intrinsic_store_global_ir3:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_store_ssbo_ir3:
      /* Only set this for globally visible memory, not scratch and not
       * shared.
       */
      shader->info.writes_memory = true;
      break;

   default:
      break;
   }
}

static void
gather_tex_info(nir_tex_instr *instr, nir_shader *shader)
{
   if (shader->info.stage == MESA_SHADER_FRAGMENT &&
       nir_tex_instr_has_implicit_derivative(instr))
      shader->info.fs.needs_helper_invocations = true;

   switch (instr->op) {
   case nir_texop_tg4:
      shader->info.uses_texture_gather = true;
      break;
   default:
      break;
   }
}

static void
gather_alu_info(nir_alu_instr *instr, nir_shader *shader)
{
   switch (instr->op) {
   case nir_op_fddx:
   case nir_op_fddy:
      shader->info.uses_fddx_fddy = true;
      /* Fall through */
   case nir_op_fddx_fine:
   case nir_op_fddy_fine:
   case nir_op_fddx_coarse:
   case nir_op_fddy_coarse:
      if (shader->info.stage == MESA_SHADER_FRAGMENT)
         shader->info.fs.needs_helper_invocations = true;
      break;
   default:
      break;
   }

   shader->info.uses_64bit |= instr->dest.dest.ssa.bit_size == 64;
   unsigned num_srcs = nir_op_infos[instr->op].num_inputs;
   for (unsigned i = 0; i < num_srcs; i++) {
      shader->info.uses_64bit |= nir_src_bit_size(instr->src[i].src) == 64;
   }
}

static void
gather_info_block(nir_block *block, nir_shader *shader, void *dead_ctx)
{
   nir_foreach_instr(instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu:
         gather_alu_info(nir_instr_as_alu(instr), shader);
         break;
      case nir_instr_type_intrinsic:
         gather_intrinsic_info(nir_instr_as_intrinsic(instr), shader, dead_ctx);
         break;
      case nir_instr_type_tex:
         gather_tex_info(nir_instr_as_tex(instr), shader);
         break;
      case nir_instr_type_call:
         assert(!"nir_shader_gather_info only works if functions are inlined");
         break;
      default:
         break;
      }
   }
}

void
nir_shader_gather_info(nir_shader *shader, nir_function_impl *entrypoint)
{
   shader->info.num_textures = 0;
   shader->info.num_images = 0;
   shader->info.last_msaa_image = -1;

   nir_foreach_variable(var, &shader->uniforms) {
      /* Bindless textures and images don't use non-bindless slots. */
      if (var->data.bindless)
         continue;

      shader->info.num_textures += glsl_type_get_sampler_count(var->type);
      shader->info.num_images += glsl_type_get_image_count(var->type);

      /* Assuming image slots don't have holes (e.g. OpenGL) */
      if (glsl_type_is_image(var->type) &&
          glsl_get_sampler_dim(var->type) == GLSL_SAMPLER_DIM_MS)
         shader->info.last_msaa_image = shader->info.num_images - 1;
   }

   shader->info.inputs_read = 0;
   shader->info.outputs_written = 0;
   shader->info.outputs_read = 0;
   shader->info.patch_outputs_read = 0;
   shader->info.patch_inputs_read = 0;
   shader->info.patch_outputs_written = 0;
   shader->info.system_values_read = 0;
   shader->info.inputs_read_indirectly = 0;
   shader->info.outputs_accessed_indirectly = 0;
   shader->info.patch_inputs_read_indirectly = 0;
   shader->info.patch_outputs_accessed_indirectly = 0;

   if (shader->info.stage == MESA_SHADER_VERTEX) {
      shader->info.vs.double_inputs = 0;
   }
   if (shader->info.stage == MESA_SHADER_FRAGMENT) {
      shader->info.fs.uses_sample_qualifier = false;
      shader->info.fs.uses_discard = false;
      shader->info.fs.uses_demote = false;
      shader->info.fs.needs_helper_invocations = false;
   }
   if (shader->info.stage == MESA_SHADER_TESS_CTRL) {
      shader->info.tess.tcs_cross_invocation_inputs_read = 0;
      shader->info.tess.tcs_cross_invocation_outputs_read = 0;
   }

   shader->info.writes_memory = shader->info.has_transform_feedback_varyings;

   void *dead_ctx = ralloc_context(NULL);
   nir_foreach_block(block, entrypoint) {
      gather_info_block(block, shader, dead_ctx);
   }
   ralloc_free(dead_ctx);
}
