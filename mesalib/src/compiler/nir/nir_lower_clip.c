/*
 * Copyright © 2015 Red Hat
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
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "nir.h"
#include "nir_builder.h"

#define MAX_CLIP_PLANES 8

/* Generates the lowering code for user-clip-planes, generating CLIPDIST
 * from UCP[n] + CLIPVERTEX or POSITION.  Additionally, an optional pass
 * for fragment shaders to insert conditional kills based on the inter-
 * polated CLIPDIST
 *
 * NOTE: should be run after nir_lower_outputs_to_temporaries() (or at
 * least in scenarios where you can count on each output written once
 * and only once).
 */

static nir_variable *
create_clipdist_var(nir_shader *shader,
                    bool output, gl_varying_slot slot, unsigned array_size)
{
   nir_variable *var = rzalloc(shader, nir_variable);

   if (output) {
      var->data.driver_location = shader->num_outputs;
      var->data.mode = nir_var_shader_out;
      shader->num_outputs += MAX2(1, DIV_ROUND_UP(array_size, 4));
   } else {
      var->data.driver_location = shader->num_inputs;
      var->data.mode = nir_var_shader_in;
      shader->num_inputs += MAX2(1, DIV_ROUND_UP(array_size, 4));
   }
   var->name = ralloc_asprintf(var, "clipdist_%d", slot - VARYING_SLOT_CLIP_DIST0);
   var->data.index = 0;
   var->data.location = slot;

   if (array_size > 0) {
      var->type = glsl_array_type(glsl_float_type(), array_size,
                                  sizeof(float));
      var->data.compact = 1;
   } else
      var->type = glsl_vec4_type();

   nir_shader_add_variable(shader, var);
   return var;
}

static void
create_clipdist_vars(nir_shader *shader, nir_variable **io_vars,
                     unsigned ucp_enables, bool output,
                     bool use_clipdist_array)
{
   if (use_clipdist_array) {
      io_vars[0] =
         create_clipdist_var(shader, output,
                             VARYING_SLOT_CLIP_DIST0,
                             shader->info.clip_distance_array_size);
   } else {
      if (ucp_enables & 0x0f)
         io_vars[0] =
            create_clipdist_var(shader, output,
                                VARYING_SLOT_CLIP_DIST0, 0);
      if (ucp_enables & 0xf0)
         io_vars[1] =
            create_clipdist_var(shader, output,
                                VARYING_SLOT_CLIP_DIST1, 0);
   }
}

static void
store_clipdist_output(nir_builder *b, nir_variable *out, int location, int location_offset,
                      nir_def **val, bool use_clipdist_array)
{
   unsigned num_slots = b->shader->info.clip_distance_array_size;
   nir_io_semantics semantics = {
      .location = location,
      .num_slots = b->shader->options->compact_arrays ? num_slots : 1,
   };

   if (location == VARYING_SLOT_CLIP_DIST1 || location_offset)
      num_slots -= 4;
   else
      num_slots = MIN2(num_slots, 4);
   for (unsigned i = 0; i < num_slots; i++) {
      nir_store_output(b,
                       val[i] ? val[i] : nir_imm_zero(b, 1, 32),
                       nir_imm_int(b, location_offset),
                       .write_mask = 0x1,
                       .component = i,
                       .io_semantics = semantics,
                       .base = out ? out->data.driver_location : 0);
   }
}

static void
load_clipdist_input(nir_builder *b, nir_variable *in, int location_offset,
                    nir_def **val, bool use_load_interp)
{
   nir_def *load;
   if (use_load_interp) {
      /* TODO: use sample when per-sample shading? */
      nir_def *barycentric = nir_load_barycentric(
         b, nir_intrinsic_load_barycentric_pixel, INTERP_MODE_NONE);
      load = nir_load_interpolated_input(
         b, 4, 32, barycentric, nir_imm_int(b, location_offset),
         .base = in->data.driver_location,
         .io_semantics.location = in->data.location);

   } else {
      load = nir_load_input(b, 4, 32, nir_imm_int(b, location_offset),
                            .base = in->data.driver_location,
                            .io_semantics.location = in->data.location);
   }

   val[0] = nir_channel(b, load, 0);
   val[1] = nir_channel(b, load, 1);
   val[2] = nir_channel(b, load, 2);
   val[3] = nir_channel(b, load, 3);
}

static nir_def *
find_output(nir_builder *b, unsigned location)
{
   nir_def *comp[4] = {NULL};

   nir_foreach_function_impl(impl, b->shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            if ((intr->intrinsic == nir_intrinsic_store_output ||
                 intr->intrinsic == nir_intrinsic_store_per_vertex_output ||
                 intr->intrinsic == nir_intrinsic_store_per_view_output ||
                 intr->intrinsic == nir_intrinsic_store_per_primitive_output) &&
                nir_intrinsic_io_semantics(intr).location == location) {
               assert(nir_src_is_const(*nir_get_io_offset_src(intr)));
               unsigned component = nir_intrinsic_component(intr);
               unsigned wrmask = nir_intrinsic_write_mask(intr);

               u_foreach_bit(i, wrmask) {
                  unsigned index = component + i;

                  /* Each component should be written only once. */
                  assert(!comp[index]);
                  comp[index] = nir_channel(b, intr->src[0].ssa, i);
               }

               /* Remove it because it's going to be replaced by CLIP_DIST. */
               if (location == VARYING_SLOT_CLIP_VERTEX)
                  nir_instr_remove(instr);
            }
         }
      }
   }
   assert(comp[0] || comp[1] || comp[2] || comp[3]);

   for (unsigned i = 0; i < 4; i++) {
      if (!comp[i])
         comp[i] = nir_undef(b, 1, 32);
   }

   return nir_vec(b, comp, 4);
}

static bool
find_clipvertex_and_position_outputs(nir_shader *shader,
                                     nir_variable **clipvertex,
                                     nir_variable **position)
{
   if (shader->info.io_lowered) {
      if (shader->info.outputs_written & (VARYING_BIT_CLIP_DIST0 | VARYING_BIT_CLIP_DIST1))
         return false;
      if (shader->info.outputs_written & (VARYING_BIT_POS | VARYING_BIT_CLIP_VERTEX))
         return true;
      return false;
   }
   nir_foreach_shader_out_variable(var, shader) {
      switch (var->data.location) {
      case VARYING_SLOT_POS:
         *position = var;
         break;
      case VARYING_SLOT_CLIP_VERTEX:
         *clipvertex = var;
         break;
      case VARYING_SLOT_CLIP_DIST0:
      case VARYING_SLOT_CLIP_DIST1:
         /* if shader is already writing CLIPDIST, then
          * there should be no user-clip-planes to deal
          * with.
          *
          * We assume nir_remove_dead_variables has removed the clipdist
          * variables if they're not written.
          */
         return false;
      }
   }

   return *clipvertex || *position;
}

static nir_def *
get_ucp(nir_builder *b, int plane,
        const gl_state_index16 clipplane_state_tokens[][STATE_LENGTH])
{
   if (clipplane_state_tokens) {
      char tmp[100];
      snprintf(tmp, ARRAY_SIZE(tmp), "gl_ClipPlane%dMESA", plane);
      nir_variable *var = nir_state_variable_create(b->shader,
                                                    glsl_vec4_type(),
                                                    tmp, clipplane_state_tokens[plane]);
      return nir_load_var(b, var);
   } else
      return nir_load_user_clip_plane(b, plane);
}

static uint64_t
update_mask(uint32_t ucp_enables)
{
   uint64_t mask = 0;

   if (ucp_enables & 0x0f)
      mask |= VARYING_BIT_CLIP_DIST0;
   if (ucp_enables & 0xf0)
      mask |= VARYING_BIT_CLIP_DIST1;

   return mask;
}

struct lower_clip_state {
   nir_variable *position;
   nir_variable *clipvertex;
   nir_variable *out[2];
   unsigned ucp_enables;
   bool use_clipdist_array;
   const gl_state_index16 (*clipplane_state_tokens)[STATE_LENGTH];

   /* This holds the current CLIP_VERTEX value for GS. */
   nir_variable *clipvertex_gs_temp;
};

static void
lower_clip_vertex_var(nir_builder *b, const struct lower_clip_state *state)
{
   nir_def *clipdist[MAX_CLIP_PLANES] = {NULL};
   nir_def *cv = nir_load_var(b, state->clipvertex ? state->clipvertex
                                                   : state->position);

   if (state->clipvertex) {
      state->clipvertex->data.mode = nir_var_shader_temp;
      nir_fixup_deref_modes(b->shader);
   }

   for (int plane = 0; plane < MAX_CLIP_PLANES; plane++) {
      if (state->ucp_enables & (1 << plane)) {
         nir_def *ucp = get_ucp(b, plane, state->clipplane_state_tokens);

         /* calculate clipdist[plane] - dot(ucp, cv): */
         clipdist[plane] = nir_fdot(b, ucp, cv);
      } else {
         /* 0.0 == don't-clip == disabled: */
         clipdist[plane] = nir_imm_float(b, 0.0);
      }
      if (state->use_clipdist_array &&
          plane < util_last_bit(state->ucp_enables)) {
         nir_deref_instr *deref;
         deref = nir_build_deref_array_imm(b,
                                           nir_build_deref_var(b, state->out[0]),
                                           plane);
         nir_store_deref(b, deref, clipdist[plane], 1);
      }
   }

   if (!state->use_clipdist_array) {
      if (state->ucp_enables & 0x0f)
         nir_store_var(b, state->out[0], nir_vec(b, clipdist, 4), 0xf);
      if (state->ucp_enables & 0xf0)
         nir_store_var(b, state->out[1], nir_vec(b, &clipdist[4], 4), 0xf);
      b->shader->info.outputs_written |= update_mask(state->ucp_enables);
   }
}

static void
lower_clip_vertex_intrin(nir_builder *b, const struct lower_clip_state *state)
{
   nir_def *clipdist[MAX_CLIP_PLANES] = {NULL};
   nir_def *cv;

   if (state->clipvertex_gs_temp) {
      cv = nir_load_deref(b, nir_build_deref_var(b, state->clipvertex_gs_temp));
   } else {
      cv = find_output(b, b->shader->info.outputs_written &
                       VARYING_BIT_CLIP_VERTEX ?
                          VARYING_SLOT_CLIP_VERTEX : VARYING_SLOT_POS);
   }

   for (int plane = 0; plane < MAX_CLIP_PLANES; plane++) {
      if (state->ucp_enables & (1 << plane)) {
         nir_def *ucp = get_ucp(b, plane, state->clipplane_state_tokens);

         /* calculate clipdist[plane] - dot(ucp, cv): */
         clipdist[plane] = nir_fdot(b, ucp, cv);
      } else {
         /* 0.0 == don't-clip == disabled: */
         clipdist[plane] = nir_imm_float(b, 0.0);
      }
   }

   if (state->use_clipdist_array) {
      /* Always emit the first vec4. */
      store_clipdist_output(b, state->out[0], VARYING_SLOT_CLIP_DIST0, 0,
                            &clipdist[0], state->use_clipdist_array);
      if (state->ucp_enables & 0xf0) {
         store_clipdist_output(b, state->out[0], VARYING_SLOT_CLIP_DIST0, 1,
                               &clipdist[4], state->use_clipdist_array);
      }
   } else {
      /* Always emit the first vec4. */
      store_clipdist_output(b, state->out[0], VARYING_SLOT_CLIP_DIST0, 0,
                            &clipdist[0], state->use_clipdist_array);
      if (state->ucp_enables & 0xf0) {
         store_clipdist_output(b, state->out[1], VARYING_SLOT_CLIP_DIST1, 0,
                               &clipdist[4], state->use_clipdist_array);
      }
   }
   b->shader->info.outputs_written |= update_mask(state->ucp_enables);
}

/*
 * VS lowering
 */

/* ucp_enables is bitmask of enabled ucps.  Actual ucp values are
 * passed in to shader via user_clip_plane system-values
 *
 * If use_vars is true, the pass will use variable loads and stores instead
 * of working with store_output intrinsics.
 *
 * If use_clipdist_array is true, the pass will use compact arrays for the
 * clipdist output instead of two vec4s.
 */
bool
nir_lower_clip_vs(nir_shader *shader, unsigned ucp_enables, bool use_vars,
                  bool use_clipdist_array,
                  const gl_state_index16 clipplane_state_tokens[][STATE_LENGTH])
{
   if (!ucp_enables)
      return false;

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_builder b = nir_builder_create(impl);

   /* NIR should ensure that, even in case of loops/if-else, there
    * should be only a single predecessor block to end_block, which
    * makes the perfect place to insert the clipdist calculations.
    *
    * NOTE: in case of early returns, these would have to be lowered
    * to jumps to end_block predecessor in a previous pass.  Not sure
    * if there is a good way to sanity check this, but for now the
    * users of this pass don't support sub-routines.
    */
   assert(impl->end_block->predecessors->entries == 1);
   b.cursor = nir_after_impl(impl);

   struct lower_clip_state state = {NULL};
   state.ucp_enables = ucp_enables;
   state.use_clipdist_array = use_clipdist_array;
   state.clipplane_state_tokens = clipplane_state_tokens;

   /* find clipvertex/position outputs */
   if (!find_clipvertex_and_position_outputs(shader, &state.clipvertex,
                                             &state.position))
      return false;

   shader->info.clip_distance_array_size = util_last_bit(ucp_enables);

   if (!use_vars || shader->info.io_lowered) {
      /* If the driver has lowered IO instead of st/mesa, the driver expects
       * that variables are present even with lowered IO, so create them.
       */
      if (!shader->info.io_lowered) {
         create_clipdist_vars(shader, state.out, ucp_enables, true,
                              use_clipdist_array);
      }

      lower_clip_vertex_intrin(&b, &state);
   } else {
      create_clipdist_vars(shader, state.out, ucp_enables, true,
                           use_clipdist_array);
      lower_clip_vertex_var(&b, &state);
   }

   nir_metadata_preserve(impl, nir_metadata_dominance);

   return true;
}

/*
 * GS lowering
 */

static bool
lower_clip_vertex_gs(nir_builder *b, nir_intrinsic_instr *intr, void *opaque)
{
   const struct lower_clip_state *state =
      (const struct lower_clip_state *)opaque;

   switch (intr->intrinsic) {
   case nir_intrinsic_emit_vertex_with_counter:
   case nir_intrinsic_emit_vertex:
      b->cursor = nir_before_instr(&intr->instr);
      if (b->shader->info.io_lowered)
         lower_clip_vertex_intrin(b, state);
      else
         lower_clip_vertex_var(b, state);
      return true;
   default:
      return false;
   }
}

/* Track the CLIP_VERTEX or POS value in a local variable, so that we can
 * retrieve it at emit_vertex.
 */
static bool
save_clipvertex_to_temp_gs(nir_builder *b, nir_intrinsic_instr *intr,
                           void *opaque)
{
   const struct lower_clip_state *state =
      (const struct lower_clip_state *)opaque;
   gl_varying_slot clip_output_slot =
      b->shader->info.outputs_written & VARYING_BIT_CLIP_VERTEX ?
            VARYING_SLOT_CLIP_VERTEX : VARYING_SLOT_POS;

   if (intr->intrinsic != nir_intrinsic_store_output ||
       nir_intrinsic_io_semantics(intr).location != clip_output_slot)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   unsigned component = nir_intrinsic_component(intr);
   unsigned writemask = nir_intrinsic_write_mask(intr);
   nir_def *value = intr->src[0].ssa;

   /* Shift vector elements to the right by component. */
   if (component) {
      unsigned swizzle[4] = {0};

      for (unsigned i = 1; i < value->num_components; i++)
         swizzle[component + i] = i;
      value = nir_swizzle(b, value, swizzle,
                          component + value->num_components);
   }

   nir_store_deref(b, nir_build_deref_var(b, state->clipvertex_gs_temp),
                   nir_pad_vec4(b, value), writemask << component);

   /* Remove the CLIP_VERTEX store because it will be replaced by CLIP_DIST
    * stores.
    */
   if (clip_output_slot == VARYING_SLOT_CLIP_VERTEX)
      nir_instr_remove(&intr->instr);
   return true;
}

bool
nir_lower_clip_gs(nir_shader *shader, unsigned ucp_enables,
                  bool use_clipdist_array,
                  const gl_state_index16 clipplane_state_tokens[][STATE_LENGTH])
{
   if (!ucp_enables)
      return false;

   struct lower_clip_state state = {NULL};
   state.ucp_enables = ucp_enables;
   state.use_clipdist_array = use_clipdist_array;
   state.clipplane_state_tokens = clipplane_state_tokens;

   /* find clipvertex/position outputs */
   if (!find_clipvertex_and_position_outputs(shader, &state.clipvertex,
                                             &state.position))
      return false;

   shader->info.clip_distance_array_size = util_last_bit(ucp_enables);

   if (shader->info.io_lowered) {
      /* Track the current value of CLIP_VERTEX or POS in a local variable. */
      state.clipvertex_gs_temp =
         nir_local_variable_create(nir_shader_get_entrypoint(shader),
                                   glsl_vec4_type(), "clipvertex_gs_temp");
      if (!nir_shader_intrinsics_pass(shader, save_clipvertex_to_temp_gs,
                                      nir_metadata_control_flow, &state))
         return false;
   } else {
      /* insert CLIPDIST outputs */
      create_clipdist_vars(shader, state.out, ucp_enables, true,
                           use_clipdist_array);
   }

   nir_shader_intrinsics_pass(shader, lower_clip_vertex_gs,
                              nir_metadata_control_flow, &state);
   return true;
}

/*
 * FS lowering
 */

static void
lower_clip_fs(nir_function_impl *impl, unsigned ucp_enables,
              nir_variable **in, bool use_clipdist_array, bool use_load_interp)
{
   nir_def *clipdist[MAX_CLIP_PLANES];
   nir_builder b = nir_builder_at(nir_before_impl(impl));

   if (!use_clipdist_array) {
      if (ucp_enables & 0x0f)
         load_clipdist_input(&b, in[0], 0, &clipdist[0], use_load_interp);
      if (ucp_enables & 0xf0)
         load_clipdist_input(&b, in[1], 0, &clipdist[4], use_load_interp);
   } else {
      if (ucp_enables & 0x0f)
         load_clipdist_input(&b, in[0], 0, &clipdist[0], use_load_interp);
      if (ucp_enables & 0xf0)
         load_clipdist_input(&b, in[0], 1, &clipdist[4], use_load_interp);
   }
   b.shader->info.inputs_read |= update_mask(ucp_enables);

   nir_def *cond = NULL;

   for (int plane = 0; plane < MAX_CLIP_PLANES; plane++) {
      if (ucp_enables & (1 << plane)) {
         nir_def *this_cond =
            nir_flt_imm(&b, clipdist[plane], 0.0);

         cond = cond ? nir_ior(&b, cond, this_cond) : this_cond;
      }
   }

   if (cond != NULL) {
      nir_discard_if(&b, cond);
      b.shader->info.fs.uses_discard = true;
   }

   nir_metadata_preserve(impl, nir_metadata_dominance);
}

static bool
fs_has_clip_dist_input_var(nir_shader *shader, nir_variable **io_vars,
                           unsigned *ucp_enables)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);
   nir_foreach_shader_in_variable(var, shader) {
      switch (var->data.location) {
      case VARYING_SLOT_CLIP_DIST0:
         assert(var->data.compact);
         io_vars[0] = var;
         *ucp_enables &= (1 << glsl_get_length(var->type)) - 1;
         return true;
      default:
         break;
      }
   }
   return false;
}

/* insert conditional kill based on interpolated CLIPDIST
 */
bool
nir_lower_clip_fs(nir_shader *shader, unsigned ucp_enables,
                  bool use_clipdist_array, bool use_load_interp)
{
   nir_variable *in[2] = { 0 };

   if (!ucp_enables)
      return false;

   /* this is probably broken until https://gitlab.freedesktop.org/mesa/mesa/-/issues/10826 is fixed */
   assert(!shader->info.io_lowered);
   shader->info.clip_distance_array_size = util_last_bit(ucp_enables);

   /* No hard reason to require use_clipdist_arr to work with
    * frag-shader-based gl_ClipDistance, except that the only user that does
    * not enable this does not support GL 3.0 (or EXT_clip_cull_distance).
    */
   if (!fs_has_clip_dist_input_var(shader, in, &ucp_enables))
      create_clipdist_vars(shader, in, ucp_enables, false, use_clipdist_array);
   else
      assert(use_clipdist_array);

   nir_foreach_function_with_impl(function, impl, shader) {
      if (!strcmp(function->name, "main")) {
         lower_clip_fs(impl, ucp_enables, in, use_clipdist_array,
                       use_load_interp);
      }
   }

   return true;
}
