/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

/* This is a pre-link lowering and optimization pass that modifies the shader for the purpose
 * of gathering accurate shader_info and determining hw registers. It should be run before
 * linking passes and it doesn't produce AMD intrinsics that would break linking passes.
 * Some of the options come from dynamic state.
 *
 * It should be run after nir_lower_io, but before nir_opt_varyings.
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

   bool frag_color_is_frag_data0;
   bool seen_color0_alpha;
   bool uses_fragcoord_xy_as_float;
   bool use_fragcoord;

   nir_def *load_helper_invoc_at_top;
} lower_ps_early_state;

static nir_variable *
get_baryc_var_common(nir_builder *b, bool will_replace, nir_variable **var, const char *var_name)
{
   if (will_replace) {
      if (!*var) {
         *var = nir_local_variable_create(b->impl, glsl_vec_type(2), var_name);
      }
      return *var;
   }
   return NULL;
}

static nir_variable *
get_baryc_var(nir_builder *b, nir_intrinsic_op baryc_op, enum glsl_interp_mode mode,
              lower_ps_early_state *s)
{
   switch (baryc_op) {
   case nir_intrinsic_load_barycentric_pixel:
      if (mode == INTERP_MODE_NOPERSPECTIVE) {
         return get_baryc_var_common(b, s->options->ps_iter_samples > 1, &s->linear_center,
                                     "linear_center");
      } else {
         return get_baryc_var_common(b, s->options->ps_iter_samples > 1, &s->persp_center,
                                     "persp_center");
      }
   case nir_intrinsic_load_barycentric_centroid:
      if (mode == INTERP_MODE_NOPERSPECTIVE) {
         return get_baryc_var_common(b, s->options->ps_iter_samples > 1 ||
                                     s->options->force_center_interp_no_msaa, &s->linear_centroid,
                                     "linear_centroid");
      } else {
         return get_baryc_var_common(b, s->options->ps_iter_samples > 1 ||
                                     s->options->force_center_interp_no_msaa, &s->persp_centroid,
                                     "persp_centroid");
      }
   case nir_intrinsic_load_barycentric_sample:
      if (mode == INTERP_MODE_NOPERSPECTIVE) {
         return get_baryc_var_common(b, s->options->force_center_interp_no_msaa, &s->linear_sample,
                                     "linear_sample");
      } else {
         return get_baryc_var_common(b, s->options->force_center_interp_no_msaa, &s->persp_sample,
                                     "persp_sample");
      }
   default:
      return NULL;
   }
}

static void
set_interp_vars(nir_builder *b, nir_def *new_baryc, nir_variable *baryc1, nir_variable *baryc2)
{
   if (baryc1)
      nir_store_var(b, baryc1, new_baryc, 0x3);
   if (baryc2)
      nir_store_var(b, baryc2, new_baryc, 0x3);
}

static void
init_interp_param(nir_builder *b, lower_ps_early_state *s)
{
   b->cursor = nir_before_cf_list(&b->impl->body);

   if (s->options->ps_iter_samples > 1) {
      set_interp_vars(b, nir_load_barycentric_sample(b, 32, .interp_mode = INTERP_MODE_SMOOTH),
                      s->persp_center, s->persp_centroid);
      set_interp_vars(b, nir_load_barycentric_sample(b, 32, .interp_mode = INTERP_MODE_NOPERSPECTIVE),
                      s->linear_center, s->linear_centroid);
   }

   if (s->options->force_center_interp_no_msaa) {
      set_interp_vars(b, nir_load_barycentric_pixel(b, 32, .interp_mode = INTERP_MODE_SMOOTH),
                      s->persp_sample, s->persp_centroid);
      set_interp_vars(b, nir_load_barycentric_pixel(b, 32, .interp_mode = INTERP_MODE_NOPERSPECTIVE),
                      s->linear_sample, s->linear_centroid);
   }
}

static bool
rewrite_ps_load_barycentric(nir_builder *b, nir_intrinsic_instr *intrin, lower_ps_early_state *s)
{
   nir_variable *baryc_var = get_baryc_var(b, intrin->intrinsic,
                                           nir_intrinsic_interp_mode(intrin), s);
   if (!baryc_var)
      return false;

   nir_def_replace(&intrin->def, nir_load_var(b, baryc_var));
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

   if (slot == FRAG_RESULT_COLOR && !s->frag_color_is_frag_data0) {
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

   if (intrin->src[0].ssa != value) {
      assert(progress);
      nir_src_rewrite(&intrin->src[0], value);
      intrin->num_components = value->num_components;
   } else {
      assert(intrin->src[0].ssa == value);
   }

   return progress;
}

static nir_def *
get_load_helper_invocation(nir_function_impl *impl, lower_ps_early_state *s)
{
   /* Insert this only once. */
   if (!s->load_helper_invoc_at_top) {
      nir_builder b = nir_builder_at(nir_before_impl(impl));
      s->load_helper_invoc_at_top = nir_load_helper_invocation(&b, 1);
   }

   return s->load_helper_invoc_at_top;
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
   nir_def *replacement = NULL;

   /* Set ps_iter_samples=8 if full sample shading is enabled even for 2x and 4x MSAA
    * to get this fast path that fully replaces sample_mask_in with sample_id.
    */
   if (s->options->force_center_interp_no_msaa && !s->options->uses_vrs_coarse_shading) {
      replacement = nir_b2i32(b, nir_inot(b, get_load_helper_invocation(b->impl, s)));
   } else if (s->options->ps_iter_samples == 8) {
      replacement = nir_bcsel(b, get_load_helper_invocation(b->impl, s), nir_imm_int(b, 0),
                              nir_ishl(b, nir_imm_int(b, 1), nir_load_sample_id(b)));
   } else if (s->options->ps_iter_samples > 1) {
      uint32_t ps_iter_mask = ac_get_ps_iter_mask(s->options->ps_iter_samples);
      nir_def *submask = nir_ishl(b, nir_imm_int(b, ps_iter_mask), nir_load_sample_id(b));
      replacement = nir_iand(b, nir_load_sample_mask_in(b), submask);
   } else {
      return false;
   }

   nir_def_replace(&intrin->def, replacement);
   return true;
}

static nir_def *
lower_load_barycentric_at_offset(nir_builder *b, nir_def *offset, enum glsl_interp_mode mode)
{
   /* ddx/ddy must execute before terminate (discard). */
   nir_builder sb = nir_builder_at(nir_before_impl(b->impl));
   nir_def *baryc = nir_load_barycentric_pixel(&sb, 32, .interp_mode = mode);
   nir_def *i = nir_channel(&sb, baryc, 0);
   nir_def *j = nir_channel(&sb, baryc, 1);
   nir_def *ddx_i = nir_ddx(&sb, i);
   nir_def *ddx_j = nir_ddx(&sb, j);
   nir_def *ddy_i = nir_ddy(&sb, i);
   nir_def *ddy_j = nir_ddy(&sb, j);

   nir_def *offset_x = nir_channel(b, offset, 0);
   nir_def *offset_y = nir_channel(b, offset, 1);

   /* Interpolate standard barycentrics by offset. */
   nir_def *offset_i = nir_ffma(b, ddy_i, offset_y, nir_ffma(b, ddx_i, offset_x, i));
   nir_def *offset_j = nir_ffma(b, ddy_j, offset_y, nir_ffma(b, ddx_j, offset_x, j));
   return nir_vec2(b, offset_i, offset_j);
}

static nir_def *
fbfetch_color_buffer0(nir_builder *b, lower_ps_early_state *s)
{
   nir_def *zero = nir_imm_zero(b, 1, 32);
   nir_def *undef = nir_undef(b, 1, 32);

   unsigned chan = 0;
   nir_def *coord_vec[4] = {undef, undef, undef, undef};
   nir_def *pixel_coord = nir_u2u32(b, nir_load_pixel_coord(b));

   coord_vec[chan++] = nir_channel(b, pixel_coord, 0);

   if (!s->options->fbfetch_is_1D)
      coord_vec[chan++] = nir_channel(b, pixel_coord, 1);

   /* Get the current render target layer index. */
   if (s->options->fbfetch_layered)
      coord_vec[chan++] = nir_load_layer_id(b);

   nir_def *coords = nir_vec(b, coord_vec, 4);

   enum glsl_sampler_dim dim;
   if (s->options->fbfetch_msaa)
      dim = GLSL_SAMPLER_DIM_MS;
   else if (s->options->fbfetch_is_1D)
      dim = GLSL_SAMPLER_DIM_1D;
   else
      dim = GLSL_SAMPLER_DIM_2D;

   nir_def *sample_id;
   if (s->options->fbfetch_msaa) {
      sample_id = nir_load_sample_id(b);

      if (s->options->fbfetch_apply_fmask) {
         nir_def *fmask =
            nir_bindless_image_fragment_mask_load_amd(
               b, nir_load_fbfetch_image_fmask_desc_amd(b), coords,
               .image_dim = dim,
               .image_array = s->options->fbfetch_layered,
               .access = ACCESS_CAN_REORDER);
         sample_id = nir_ubfe(b, fmask, nir_ishl_imm(b, sample_id, 2), nir_imm_int(b, 3));
      }
   } else {
      sample_id = zero;
   }

   return nir_bindless_image_load(b, 4, 32, nir_load_fbfetch_image_desc_amd(b), coords, sample_id,
                                  zero,
                                  .image_dim = dim,
                                  .image_array = s->options->fbfetch_layered,
                                  .access = ACCESS_CAN_REORDER);
}

static bool
lower_ps_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   lower_ps_early_state *s = (lower_ps_early_state *)state;

   b->cursor = nir_before_instr(&intrin->instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_store_output:
      return optimize_lower_ps_outputs(b, intrin, s);
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_sample:
      return rewrite_ps_load_barycentric(b, intrin, s);
   case nir_intrinsic_load_sample_mask_in:
      return lower_ps_load_sample_mask_in(b, intrin, s);
   case nir_intrinsic_load_front_face:
      if (s->options->force_front_face) {
         nir_def_replace(&intrin->def, nir_imm_bool(b, s->options->force_front_face == 1));
         return true;
      }
      break;
   case nir_intrinsic_load_front_face_fsign:
      if (s->options->force_front_face) {
         nir_def_replace(&intrin->def, nir_imm_float(b, s->options->force_front_face == 1 ? 1 : -1));
         return true;
      }
      break;
   case nir_intrinsic_load_sample_pos:
      if (s->options->frag_coord_is_center) {
         /* We have to use the alternative way to get sample_pos. */
         nir_def *num_samples = s->options->load_sample_positions_always_loads_current_ones ?
                                   nir_undef(b, 1, 32) : nir_load_rasterization_samples_amd(b);
         nir_def_replace(&intrin->def, nir_load_sample_positions_amd(b, 32, nir_load_sample_id(b),
                                                                     num_samples));
      } else {
         /* sample_pos = ffract(frag_coord.xy); */
         nir_def_replace(&intrin->def, nir_ffract(b, nir_channels(b, nir_load_frag_coord(b), 0x3)));
      }
      return true;
   case nir_intrinsic_load_barycentric_at_offset:
      nir_def_replace(&intrin->def,
                      lower_load_barycentric_at_offset(b, intrin->src[0].ssa,
                                                       nir_intrinsic_interp_mode(intrin)));
      return true;
   case nir_intrinsic_load_barycentric_at_sample: {
      unsigned mode = nir_intrinsic_interp_mode(intrin);
      nir_def *sample_id = intrin->src[0].ssa;

      if (s->options->force_center_interp_no_msaa) {
         nir_def_replace(&intrin->def, nir_load_barycentric_pixel(b, 32, .interp_mode = mode));
         return true;
      }

      if (s->options->ps_iter_samples >= 2 &&
          sample_id->parent_instr->type == nir_instr_type_intrinsic &&
          nir_instr_as_intrinsic(sample_id->parent_instr)->intrinsic == nir_intrinsic_load_sample_id) {
         nir_def_replace(&intrin->def, nir_load_barycentric_sample(b, 32, .interp_mode = mode));
         return true;
      }

      /* If load_sample_positions_always_loads_current_ones is true, load_sample_positions_amd
       * always loads the sample positions that are currently set in the rasterizer state
       * even if MSAA is disabled.
       */
      nir_def *num_samples = s->options->load_sample_positions_always_loads_current_ones ?
                                nir_undef(b, 1, 32) : nir_load_rasterization_samples_amd(b);
      nir_def *sample_pos = nir_load_sample_positions_amd(b, 32, sample_id, num_samples);
      sample_pos = nir_fadd_imm(b, sample_pos, -0.5f);

      if (s->options->dynamic_rasterization_samples) {
         assert(!s->options->load_sample_positions_always_loads_current_ones);
         nir_def *pixel, *at_sample;

         nir_push_if(b, nir_ieq_imm(b, num_samples, 1));
         {
            pixel = nir_load_barycentric_pixel(b, 32, .interp_mode = mode);
         }
         nir_push_else(b, NULL);
         {
            at_sample = lower_load_barycentric_at_offset(b, sample_pos, mode);
         }
         nir_pop_if(b, NULL);
         nir_def_replace(&intrin->def, nir_if_phi(b, pixel, at_sample));
      } else {
         nir_def_replace(&intrin->def,
                         lower_load_barycentric_at_offset(b, sample_pos, mode));
      }
      return true;
   }
   case nir_intrinsic_load_output:
      if (nir_intrinsic_io_semantics(intrin).fb_fetch_output) {
         nir_def_replace(&intrin->def, fbfetch_color_buffer0(b, s));
         return true;
      }
      break;
   case nir_intrinsic_load_frag_coord:
      if (!s->options->optimize_frag_coord)
         break;
      /* Compute frag_coord.xy from pixel_coord. */
      if (!s->use_fragcoord && nir_def_components_read(&intrin->def) & 0x3) {
         nir_def *new_fragcoord_xy = nir_u2f32(b, nir_load_pixel_coord(b));
         if (!b->shader->info.fs.pixel_center_integer)
            new_fragcoord_xy = nir_fadd_imm(b, new_fragcoord_xy, 0.5);
         nir_def *fragcoord = nir_load_frag_coord(b);
         nir_def_replace(&intrin->def,
                         nir_vec4(b, nir_channel(b, new_fragcoord_xy, 0),
                                  nir_channel(b, new_fragcoord_xy, 1),
                                  nir_channel(b, fragcoord, 2),
                                  nir_channel(b, fragcoord, 3)));
         return true;
      }
      break;
   case nir_intrinsic_load_pixel_coord:
      if (!s->options->optimize_frag_coord)
         break;
      /* There is already a floating-point frag_coord.xy use in the shader. Don't add pixel_coord.
       * Instead, compute pixel_coord from frag_coord.
       */
      if (s->use_fragcoord) {
         nir_def *new_pixel_coord = nir_f2u16(b, nir_channels(b, nir_load_frag_coord(b), 0x3));
         nir_def_replace(&intrin->def, new_pixel_coord);
         return true;
      }
      break;
   default:
      break;
   }

   return false;
}

static bool
gather_info(nir_builder *b, nir_intrinsic_instr *intr, void *state)
{
   lower_ps_early_state *s = (lower_ps_early_state *)state;

   switch (intr->intrinsic) {
   case nir_intrinsic_store_output:
      /* FRAG_RESULT_COLOR can't broadcast results to all color buffers if another
       * FRAG_RESULT_COLOR output exists with dual_src_blend_index=1. This happens
       * with gl_SecondaryFragColorEXT in GLES.
       */
      if (nir_intrinsic_io_semantics(intr).location == FRAG_RESULT_COLOR &&
          nir_intrinsic_io_semantics(intr).dual_source_blend_index)
         s->frag_color_is_frag_data0 = true;
      break;
   case nir_intrinsic_load_frag_coord:
      assert(intr->def.bit_size == 32);
      nir_foreach_use(use, &intr->def) {
         if (nir_src_parent_instr(use)->type == nir_instr_type_alu &&
             nir_src_components_read(use) & 0x3) {
            switch (nir_instr_as_alu(nir_src_parent_instr(use))->op) {
            case nir_op_f2i8:
            case nir_op_f2i16:
            case nir_op_f2i32:
            case nir_op_f2i64:
            case nir_op_f2u8:
            case nir_op_f2u16:
            case nir_op_f2u32:
            case nir_op_f2u64:
            case nir_op_ftrunc:
            case nir_op_ffloor:
               continue;
            default:
               break;
            }
         }
         s->uses_fragcoord_xy_as_float = true;
         break;
      }
      break;
   case nir_intrinsic_load_sample_pos:
      if (!s->options->frag_coord_is_center)
         s->uses_fragcoord_xy_as_float = true;
      break;
   default:
      break;
   }

   return false;
}

bool
ac_nir_lower_ps_early(nir_shader *nir, const ac_nir_lower_ps_early_options *options)
{
   assert(nir->info.stage == MESA_SHADER_FRAGMENT);
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder builder = nir_builder_create(impl);
   nir_builder *b = &builder;

   lower_ps_early_state state = {
      .options = options,
   };

   /* Don't gather shader_info. Just gather the single thing we want to know. */
   nir_shader_intrinsics_pass(nir, gather_info, nir_metadata_all, &state);

   /* The preferred option is replacing frag_coord by pixel_coord.xy + 0.5. The goal is to reduce
    * input VGPRs to increase PS wave launch rate. pixel_coord uses 1 input VGPR, while
    * frag_coord.xy uses 2 input VGPRs. It only helps performance if the number of input VGPRs
    * decreases to an even number. If it only decreases to an odd number, it has no effect.
    *
    * TODO: estimate input VGPRs and don't lower to pixel_coord if their number doesn't decrease to
    * an even number?
    */
   state.use_fragcoord = !options->frag_coord_is_center && state.options->ps_iter_samples != 1 &&
                         !state.options->force_center_interp_no_msaa &&
                         state.uses_fragcoord_xy_as_float;

   bool progress = nir_shader_intrinsics_pass(nir, lower_ps_intrinsic,
                                              nir_metadata_control_flow, &state);

   if (state.persp_center || state.persp_centroid || state.persp_sample ||
       state.linear_center || state.linear_centroid || state.linear_sample) {
      assert(progress);

      /* This must be after lower_ps_intrinsic. */
      init_interp_param(b, &state);

      /* Cleanup local variables, as RADV won't do this. */
      NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   }

   return progress;
}
