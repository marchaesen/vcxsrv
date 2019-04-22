/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tgsi/tgsi_from_mesa.h"
#include "st_glsl_types.h"
#include "st_nir.h"

#include "compiler/nir/nir_builder.h"

struct pipe_shader_state *
st_nir_finish_builtin_shader(struct st_context *st,
                             nir_shader *nir,
                             const char *name)
{
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   enum pipe_shader_type p_stage = pipe_shader_type_from_mesa(nir->info.stage);
   bool is_scalar =
      screen->get_shader_param(screen, p_stage, PIPE_SHADER_CAP_SCALAR_ISA);

   nir->info.name = ralloc_strdup(nir, name);
   nir->info.separate_shader = true;
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      nir->info.fs.untyped_color_outputs = true;

   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_lower_system_values);

   if (is_scalar) {
      nir_variable_mode mask =
         (nir->info.stage > MESA_SHADER_VERTEX ? nir_var_shader_in : 0) |
         (nir->info.stage < MESA_SHADER_FRAGMENT ? nir_var_shader_out : 0);

      NIR_PASS_V(nir, nir_lower_io_to_scalar_early, mask);
   }

   st_nir_opts(nir, is_scalar);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   st_nir_assign_varying_locations(st, nir);

   st_nir_lower_samplers(screen, nir, NULL, NULL);

   if (st->ctx->Const.PackedDriverUniformStorage) {
      NIR_PASS_V(nir, nir_lower_io, nir_var_uniform, st_glsl_type_dword_size,
                 (nir_lower_io_options)0);
      NIR_PASS_V(nir, nir_lower_uniforms_to_ubo, 4);
   } else {
      NIR_PASS_V(nir, nir_lower_io, nir_var_uniform, st_glsl_uniforms_type_size,
                 (nir_lower_io_options)0);
   }

   struct pipe_shader_state state = {
      .type = PIPE_SHADER_IR_NIR,
      .ir.nir = nir,
   };

   switch (nir->info.stage) {
   case MESA_SHADER_VERTEX:
      return pipe->create_vs_state(pipe, &state);
   case MESA_SHADER_TESS_CTRL:
      return pipe->create_tcs_state(pipe, &state);
   case MESA_SHADER_TESS_EVAL:
      return pipe->create_tes_state(pipe, &state);
   case MESA_SHADER_GEOMETRY:
      return pipe->create_gs_state(pipe, &state);
   case MESA_SHADER_FRAGMENT:
      return pipe->create_fs_state(pipe, &state);
   default:
      unreachable("unsupported shader stage");
      return NULL;
   }
}

/**
 * Make a simple shader that copies inputs to corresponding outputs.
 */
struct pipe_shader_state *
st_nir_make_passthrough_shader(struct st_context *st,
                               const char *shader_name,
                               gl_shader_stage stage,
                               unsigned num_vars,
                               unsigned *input_locations,
                               unsigned *output_locations,
                               unsigned *interpolation_modes,
                               unsigned sysval_mask)
{
   struct nir_builder b;
   const struct glsl_type *vec4 = glsl_vec4_type();
   const nir_shader_compiler_options *options =
      st->ctx->Const.ShaderCompilerOptions[stage].NirOptions;

   nir_builder_init_simple_shader(&b, NULL, stage, options);

   char var_name[15];

   for (unsigned i = 0; i < num_vars; i++) {
      nir_variable *in;
      if (sysval_mask & (1 << i)) {
         snprintf(var_name, sizeof(var_name), "sys_%u", input_locations[i]);
         in = nir_variable_create(b.shader, nir_var_system_value,
                                  glsl_int_type(), var_name);
         in->data.interpolation = INTERP_MODE_FLAT;
      } else {
         snprintf(var_name, sizeof(var_name), "in_%u", input_locations[i]);
         in = nir_variable_create(b.shader, nir_var_shader_in, vec4, var_name);
      }
      in->data.location = input_locations[i];
      if (interpolation_modes)
         in->data.interpolation = interpolation_modes[i];

      snprintf(var_name, sizeof(var_name), "out_%u", output_locations[i]);
      nir_variable *out =
         nir_variable_create(b.shader, nir_var_shader_out, in->type, var_name);
      out->data.location = output_locations[i];
      out->data.interpolation = in->data.interpolation;

      nir_copy_var(&b, out, in);
   }

   return st_nir_finish_builtin_shader(st, b.shader, shader_name);
}
