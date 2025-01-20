/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/* This is a pre-link lowering and optimization pass that modifies the shader for the purpose
 * of gathering accurate shader_info and determining hw registers. It should be run before
 * linking passes and it doesn't produce AMD intrinsics that would break linking passes.
 * Some of the options come from dynamic state.
 */

#include "ac_nir.h"
#include "sid.h"
#include "nir_builder.h"
#include "nir_builtin_builder.h"

typedef struct {
   const ac_nir_lower_ps_early_options *options;

   nir_variable *persp_center;
   nir_variable *persp_centroid;
   nir_variable *persp_sample;
   nir_variable *linear_center;
   nir_variable *linear_centroid;
   nir_variable *linear_sample;
   bool lower_load_barycentric;

   bool seen_color0_alpha;
} lower_ps_early_state;

static void
create_interp_param(nir_builder *b, lower_ps_early_state *s)
{
   if (s->options->force_persp_sample_interp) {
      s->persp_center =
         nir_local_variable_create(b->impl, glsl_vec_type(2), "persp_center");
   }

   if (s->options->force_persp_sample_interp ||
       s->options->force_persp_center_interp) {
      s->persp_centroid =
         nir_local_variable_create(b->impl, glsl_vec_type(2), "persp_centroid");
   }

   if (s->options->force_persp_center_interp) {
      s->persp_sample =
         nir_local_variable_create(b->impl, glsl_vec_type(2), "persp_sample");
   }

   if (s->options->force_linear_sample_interp) {
      s->linear_center =
         nir_local_variable_create(b->impl, glsl_vec_type(2), "linear_center");
   }

   if (s->options->force_linear_sample_interp ||
       s->options->force_linear_center_interp) {
      s->linear_centroid =
         nir_local_variable_create(b->impl, glsl_vec_type(2), "linear_centroid");
   }

   if (s->options->force_linear_center_interp) {
      s->linear_sample =
         nir_local_variable_create(b->impl, glsl_vec_type(2), "linear_sample");
   }

   s->lower_load_barycentric =
      s->persp_center || s->persp_centroid || s->persp_sample ||
      s->linear_center || s->linear_centroid || s->linear_sample;
}

static void
init_interp_param(nir_builder *b, lower_ps_early_state *s)
{
   b->cursor = nir_before_cf_list(&b->impl->body);

   if (s->options->force_persp_sample_interp) {
      nir_def *sample =
         nir_load_barycentric_sample(b, 32, .interp_mode = INTERP_MODE_SMOOTH);
      nir_store_var(b, s->persp_center, sample, 0x3);
      nir_store_var(b, s->persp_centroid, sample, 0x3);
   }

   if (s->options->force_linear_sample_interp) {
      nir_def *sample =
         nir_load_barycentric_sample(b, 32, .interp_mode = INTERP_MODE_NOPERSPECTIVE);
      nir_store_var(b, s->linear_center, sample, 0x3);
      nir_store_var(b, s->linear_centroid, sample, 0x3);
   }

   if (s->options->force_persp_center_interp) {
      nir_def *center =
         nir_load_barycentric_pixel(b, 32, .interp_mode = INTERP_MODE_SMOOTH);
      nir_store_var(b, s->persp_sample, center, 0x3);
      nir_store_var(b, s->persp_centroid, center, 0x3);
   }

   if (s->options->force_linear_center_interp) {
      nir_def *center =
         nir_load_barycentric_pixel(b, 32, .interp_mode = INTERP_MODE_NOPERSPECTIVE);
      nir_store_var(b, s->linear_sample, center, 0x3);
      nir_store_var(b, s->linear_centroid, center, 0x3);
   }
}

static bool
rewrite_ps_load_barycentric(nir_builder *b, nir_intrinsic_instr *intrin, lower_ps_early_state *s)
{
   enum glsl_interp_mode mode = nir_intrinsic_interp_mode(intrin);
   nir_variable *var = NULL;

   switch (mode) {
   case INTERP_MODE_NONE:
   case INTERP_MODE_SMOOTH:
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_barycentric_pixel:
         var = s->persp_center;
         break;
      case nir_intrinsic_load_barycentric_centroid:
         var = s->persp_centroid;
         break;
      case nir_intrinsic_load_barycentric_sample:
         var = s->persp_sample;
         break;
      default:
         break;
      }
      break;

   case INTERP_MODE_NOPERSPECTIVE:
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_barycentric_pixel:
         var = s->linear_center;
         break;
      case nir_intrinsic_load_barycentric_centroid:
         var = s->linear_centroid;
         break;
      case nir_intrinsic_load_barycentric_sample:
         var = s->linear_sample;
         break;
      default:
         break;
      }
      break;

   default:
      break;
   }

   if (!var)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *replacement = nir_load_var(b, var);
   nir_def_replace(&intrin->def, replacement);
   return true;
}

static bool
optimize_lower_ps_outputs(nir_builder *b, nir_intrinsic_instr *intrin, lower_ps_early_state *s)
{
   unsigned slot = nir_intrinsic_io_semantics(intrin).location;

   switch (slot) {
   case FRAG_RESULT_DEPTH:
      if (!s->options->kill_z)
         return false;
      nir_instr_remove(&intrin->instr);
      return true;

   case FRAG_RESULT_STENCIL:
      if (!s->options->kill_stencil)
         return false;
      nir_instr_remove(&intrin->instr);
      return true;

   case FRAG_RESULT_SAMPLE_MASK:
      if (!s->options->kill_samplemask)
         return false;
      nir_instr_remove(&intrin->instr);
      return true;
   }

   unsigned writemask = nir_intrinsic_write_mask(intrin);
   unsigned component = nir_intrinsic_component(intrin);
   unsigned color_index = (slot >= FRAG_RESULT_DATA0 ? slot - FRAG_RESULT_DATA0 : 0) +
                          nir_intrinsic_io_semantics(intrin).dual_source_blend_index;
   nir_def *value = intrin->src[0].ssa;
   bool progress = false;

   b->cursor = nir_before_instr(&intrin->instr);

   /* Clamp color. */
   if (s->options->clamp_color) {
      value = nir_fsat(b, value);
      progress = true;
   }

   /* Alpha test. */
   if (color_index == 0 && s->options->alpha_func != COMPARE_FUNC_ALWAYS &&
       (writemask << component) & BITFIELD_BIT(3)) {
      assert(!s->seen_color0_alpha);
      s->seen_color0_alpha = true;

      if (s->options->alpha_func == COMPARE_FUNC_NEVER) {
         nir_discard(b);
      } else {
         nir_def *ref = nir_load_alpha_reference_amd(b);
         ref = nir_convert_to_bit_size(b, ref, nir_type_float, value->bit_size);
         nir_def *alpha = s->options->alpha_test_alpha_to_one ?
                             nir_imm_floatN_t(b, 1, value->bit_size) :
                             nir_channel(b, value, 3 - component);
         nir_def *cond = nir_compare_func(b, s->options->alpha_func, alpha, ref);
         nir_discard_if(b, nir_inot(b, cond));
      }
      progress = true;
   }

   /* Trim the src according to the format and writemask. */
   unsigned cb_shader_mask = ac_get_cb_shader_mask(s->options->spi_shader_col_format_hint);
   unsigned format_mask;

   if (slot == FRAG_RESULT_COLOR) {
      /* cb_shader_mask is 0 for disabled color buffers, so combine all of them. */
      format_mask = 0;
      for (unsigned i = 0; i < 8; i++)
         format_mask |= (cb_shader_mask >> (i * 4)) & 0xf;
   } else {
      format_mask = (cb_shader_mask >> (color_index * 4)) & 0xf;
   }

   if (s->options->keep_alpha_for_mrtz && color_index == 0)
      format_mask |= BITFIELD_BIT(3);

   writemask = (format_mask >> component) & writemask;
   nir_intrinsic_set_write_mask(intrin, writemask);

   /* Empty writemask. */
   if (!writemask) {
      nir_instr_remove(&intrin->instr);
      return true;
   }

   /* Trim the src to the last bit of writemask. */
   unsigned num_components = util_last_bit(writemask);

   if (num_components != value->num_components) {
      assert(num_components < value->num_components);
      value = nir_trim_vector(b, value, num_components);
      progress = true;
   }

   /* Replace disabled channels in a non-contiguous writemask with undef. */
   if (!util_is_power_of_two_nonzero(writemask + 1)) {
      u_foreach_bit(i, BITFIELD_MASK(num_components) & ~writemask) {
         value = nir_vector_insert_imm(b, value, nir_undef(b, 1, value->bit_size), i);
         progress = true;
      }
   }

   if (progress && intrin->src[0].ssa != value) {
      nir_src_rewrite(&intrin->src[0], value);
      intrin->num_components = value->num_components;
   } else {
      assert(intrin->src[0].ssa == value);
   }

   return progress;
}

static bool
lower_ps_load_sample_mask_in(nir_builder *b, nir_intrinsic_instr *intrin, lower_ps_early_state *s)
{
   /* Section 15.2.2 (Shader Inputs) of the OpenGL 4.5 (Core Profile) spec
    * says:
    *
    *    "When per-sample shading is active due to the use of a fragment
    *     input qualified by sample or due to the use of the gl_SampleID
    *     or gl_SamplePosition variables, only the bit for the current
    *     sample is set in gl_SampleMaskIn. When state specifies multiple
    *     fragment shader invocations for a given fragment, the sample
    *     mask for any single fragment shader invocation may specify a
    *     subset of the covered samples for the fragment. In this case,
    *     the bit corresponding to each covered sample will be set in
    *     exactly one fragment shader invocation."
    *
    * The samplemask loaded by hardware is always the coverage of the
    * entire pixel/fragment, so mask bits out based on the sample ID.
    */

   b->cursor = nir_before_instr(&intrin->instr);

   uint32_t ps_iter_mask = ac_get_ps_iter_mask(s->options->ps_iter_samples);
   nir_def *sampleid = nir_load_sample_id(b);
   nir_def *submask = nir_ishl(b, nir_imm_int(b, ps_iter_mask), sampleid);

   nir_def *sample_mask = nir_load_sample_mask_in(b);
   nir_def *replacement = nir_iand(b, sample_mask, submask);

   nir_def_replace(&intrin->def, replacement);
   return true;
}

static bool
lower_ps_intrinsic(nir_builder *b, nir_instr *instr, void *state)
{
   lower_ps_early_state *s = (lower_ps_early_state *)state;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_store_output:
      return optimize_lower_ps_outputs(b, intrin, s);
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_sample:
      if (s->lower_load_barycentric)
         return rewrite_ps_load_barycentric(b, intrin, s);
      break;
   case nir_intrinsic_load_sample_mask_in:
      if (s->options->ps_iter_samples > 1)
         return lower_ps_load_sample_mask_in(b, intrin, s);
      break;
   default:
      break;
   }

   return false;
}

void
ac_nir_lower_ps_early(nir_shader *nir, const ac_nir_lower_ps_early_options *options)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder builder = nir_builder_create(impl);
   nir_builder *b = &builder;

   lower_ps_early_state state = {
      .options = options,
   };

   create_interp_param(b, &state);

   nir_shader_instructions_pass(nir, lower_ps_intrinsic,
                                nir_metadata_control_flow,
                                &state);

   /* This must be after lower_ps_intrinsic. */
   init_interp_param(b, &state);

   /* Cleanup local variables, as RADV won't do this. */
   if (state.lower_load_barycentric)
      nir_lower_vars_to_ssa(nir);
}
