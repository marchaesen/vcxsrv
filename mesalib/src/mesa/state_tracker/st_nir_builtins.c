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
#include "st_nir.h"
#include "st_program.h"

#include "compiler/nir/nir_builder.h"
#include "compiler/glsl/gl_nir.h"
#include "compiler/glsl/gl_nir_linker.h"
#include "tgsi/tgsi_parse.h"

struct pipe_shader_state *
st_nir_finish_builtin_shader(struct st_context *st,
                             nir_shader *nir)
{
   struct pipe_screen *screen = st->screen;
   gl_shader_stage stage = nir->info.stage;

   nir->info.separate_shader = true;
   if (stage == MESA_SHADER_FRAGMENT)
      nir->info.fs.untyped_color_outputs = true;

   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   if (nir->options->lower_to_scalar) {
      nir_variable_mode mask =
          (stage > MESA_SHADER_VERTEX ? nir_var_shader_in : 0) |
          (stage < MESA_SHADER_FRAGMENT ? nir_var_shader_out : 0);

      NIR_PASS_V(nir, nir_lower_io_to_scalar_early, mask);
   }

   if (st->lower_rect_tex) {
      const struct nir_lower_tex_options opts = { .lower_rect = true, };
      NIR_PASS_V(nir, nir_lower_tex, &opts);
   }

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   st_nir_assign_vs_in_locations(nir);
   st_nir_assign_varying_locations(st, nir);

   st_nir_lower_samplers(screen, nir, NULL, NULL);
   st_nir_lower_uniforms(st, nir);
   if (!screen->get_param(screen, PIPE_CAP_NIR_IMAGES_AS_DEREF))
      NIR_PASS_V(nir, gl_nir_lower_images, false);

   if (screen->finalize_nir) {
      char *msg = screen->finalize_nir(screen, nir);
      free(msg);
   } else {
      gl_nir_opts(nir);
   }

   struct pipe_shader_state state = {
      .type = PIPE_SHADER_IR_NIR,
      .ir.nir = nir,
   };

   return st_create_nir_shader(st, &state);
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
   const struct glsl_type *vec4 = glsl_vec4_type();
   const nir_shader_compiler_options *options =
      st_get_nir_compiler_options(st, stage);

   nir_builder b = nir_builder_init_simple_shader(stage, options,
                                                  "%s", shader_name);

   char var_name[15];

   for (unsigned i = 0; i < num_vars; i++) {
      nir_variable *in;
      if (sysval_mask & (1 << i)) {
         snprintf(var_name, sizeof(var_name), "sys_%u", input_locations[i]);
         in = nir_variable_create(b.shader, nir_var_system_value,
                                  glsl_int_type(), var_name);
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

   return st_nir_finish_builtin_shader(st, b.shader);
}
