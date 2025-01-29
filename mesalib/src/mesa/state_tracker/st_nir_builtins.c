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

#include "st_nir.h"
#include "st_program.h"

#include "compiler/nir/nir_builder.h"
#include "compiler/glsl/gl_nir.h"
#include "compiler/glsl/gl_nir_linker.h"

void
st_nir_finish_builtin_nir(struct st_context *st, nir_shader *nir)
{
   struct pipe_screen *screen = st->screen;
   gl_shader_stage stage = nir->info.stage;

   MESA_TRACE_FUNC();

   nir->info.separate_shader = true;
   if (stage == MESA_SHADER_FRAGMENT)
      nir->info.fs.untyped_color_outputs = true;

   NIR_PASS(_, nir, nir_lower_system_values);

   struct nir_lower_compute_system_values_options cs_options = {
      .has_base_global_invocation_id = false,
      .has_base_workgroup_id = false,
   };
   NIR_PASS(_, nir, nir_lower_compute_system_values, &cs_options);

   if (st->lower_rect_tex) {
      const struct nir_lower_tex_options opts = { .lower_rect = true, };
      NIR_PASS(_, nir, nir_lower_tex, &opts);
   }

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   nir_recompute_io_bases(nir, nir_var_shader_in | nir_var_shader_out);

   st_nir_lower_samplers(screen, nir, NULL, NULL);
   st_nir_lower_uniforms(st, nir);
   if (!screen->caps.nir_images_as_deref)
      NIR_PASS(_, nir, gl_nir_lower_images, false);

   assert(nir->info.stage == MESA_SHADER_COMPUTE || nir->info.io_lowered);

   if (nir->info.io_lowered &&
       !(nir->options->io_options & nir_io_has_intrinsics)) {
      NIR_PASS(_, nir, st_nir_unlower_io_to_vars);
      gl_nir_opts(nir);
   }

   if (screen->finalize_nir) {
      char *msg = screen->finalize_nir(screen, nir);
      free(msg);
   } else {
      gl_nir_opts(nir);
   }
}

void *
st_nir_finish_builtin_shader(struct st_context *st,
                             nir_shader *nir)
{
   st_nir_finish_builtin_nir(st, nir);

   struct pipe_shader_state state = {
      .type = PIPE_SHADER_IR_NIR,
      .ir.nir = nir,
   };

   return st_create_nir_shader(st, &state);
}

/**
 * Make a simple vertex shader that copies inputs to corresponding outputs.
 */
void *
st_nir_make_passthrough_vs(struct st_context *st,
                           const char *shader_name,
                           unsigned num_vars,
                           const unsigned *input_locations,
                           const gl_varying_slot *output_locations,
                           unsigned sysval_mask)
{
   const nir_shader_compiler_options *options =
      st_get_nir_compiler_options(st, MESA_SHADER_VERTEX);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, options,
                                                  "%s", shader_name);
   b.shader->info.io_lowered = true;

   for (unsigned i = 0; i < num_vars; i++) {
      nir_def *in;

      if (sysval_mask & (1 << i)) {
         nir_variable *var =
            nir_create_variable_with_location(b.shader, nir_var_system_value,
                                              input_locations[i],
                                              glsl_int_type());
         in = nir_load_var(&b, var);
      } else {
         in = nir_load_input(&b, 4, 32, nir_imm_int(&b, 0),
                             .io_semantics.location = input_locations[i]);
      }

      nir_store_output(&b, in, nir_imm_int(&b, 0),
                       .src_type = output_locations[i] == VARYING_SLOT_LAYER ?
                                      nir_type_int32 : nir_type_float32,
                       .io_semantics.location = output_locations[i]);
   }

   return st_nir_finish_builtin_shader(st, b.shader);
}

/**
 * Make a simple shader that reads color value from a constant buffer
 * and uses it to clear all color buffers.
 */
void *
st_nir_make_clearcolor_shader(struct st_context *st)
{
   const nir_shader_compiler_options *options =
      st_get_nir_compiler_options(st, MESA_SHADER_FRAGMENT);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, options,
                                                  "clear color FS");
   b.shader->info.num_ubos = 1;
   b.shader->num_outputs = 1;
   b.shader->num_uniforms = 1;
   b.shader->info.io_lowered = true;

   /* Read clear color from constant buffer */
   nir_def *clear_color = nir_load_uniform(&b, 4, 32, nir_imm_int(&b,0),
                                               .range = 16,
                                               .dest_type = nir_type_float32);

   nir_store_output(&b, clear_color, nir_imm_int(&b, 0),
                    .io_semantics.location = FRAG_RESULT_COLOR);

   return st_nir_finish_builtin_shader(st, b.shader);
}
