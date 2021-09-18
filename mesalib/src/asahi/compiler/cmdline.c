/*
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "main/mtypes.h"
#include "compiler/glsl/standalone.h"
#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/glsl/gl_nir.h"
#include "compiler/nir_types.h"
#include "util/u_dynarray.h"
#include "agx_compile.h"
#include "agx_minifloat.h"

static int
st_packed_uniforms_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_dword_slots(type, bindless);
}

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static void
insert_sorted(struct exec_list *var_list, nir_variable *new_var)
{
   nir_foreach_variable_in_list (var, var_list) {
      if (var->data.location > new_var->data.location) {
         exec_node_insert_node_before(&var->node, &new_var->node);
         return;
      }
   }
   exec_list_push_tail(var_list, &new_var->node);
}

static void
sort_varyings(nir_shader *nir, nir_variable_mode mode)
{
   struct exec_list new_list;
   exec_list_make_empty(&new_list);
   nir_foreach_variable_with_modes_safe (var, nir, mode) {
      exec_node_remove(&var->node);
      insert_sorted(&new_list, var);
   }
   exec_list_append(&nir->variables, &new_list);
}

static void
fixup_varying_slots(nir_shader *nir, nir_variable_mode mode)
{
   nir_foreach_variable_with_modes (var, nir, mode) {
      if (var->data.location >= VARYING_SLOT_VAR0) {
         var->data.location += 9;
      } else if ((var->data.location >= VARYING_SLOT_TEX0) &&
                 (var->data.location <= VARYING_SLOT_TEX7)) {
         var->data.location += VARYING_SLOT_VAR0 - VARYING_SLOT_TEX0;
      }
   }
}

static void
compile_shader(char **argv)
{
   struct gl_shader_program *prog;
   nir_shader *nir[2];
   unsigned shader_types[2] = {
      MESA_SHADER_VERTEX,
      MESA_SHADER_FRAGMENT,
   };

   struct standalone_options options = {
      .glsl_version = 300, /* ES - needed for precision */
      .do_link = true,
      .lower_precision = true
   };

   static struct gl_context local_ctx;

   prog = standalone_compile_shader(&options, 2, argv, &local_ctx);
   prog->_LinkedShaders[MESA_SHADER_FRAGMENT]->Program->info.stage = MESA_SHADER_FRAGMENT;

   struct util_dynarray binary;

   util_dynarray_init(&binary, NULL);

   for (unsigned i = 0; i < 2; ++i) {
      nir[i] = glsl_to_nir(&local_ctx, prog, shader_types[i], &agx_nir_options);

      if (i == 0) {
         nir_assign_var_locations(nir[i], nir_var_shader_in, &nir[i]->num_inputs,
               glsl_type_size);
         sort_varyings(nir[i], nir_var_shader_out);
         nir_assign_var_locations(nir[i], nir_var_shader_out, &nir[i]->num_outputs,
               glsl_type_size);
         fixup_varying_slots(nir[i], nir_var_shader_out);
      } else {
         sort_varyings(nir[i], nir_var_shader_in);
         nir_assign_var_locations(nir[i], nir_var_shader_in, &nir[i]->num_inputs,
               glsl_type_size);
         fixup_varying_slots(nir[i], nir_var_shader_in);
         nir_assign_var_locations(nir[i], nir_var_shader_out, &nir[i]->num_outputs,
               glsl_type_size);
      }

      nir_assign_var_locations(nir[i], nir_var_uniform, &nir[i]->num_uniforms,
            glsl_type_size);

      NIR_PASS_V(nir[i], nir_lower_global_vars_to_local);
      NIR_PASS_V(nir[i], nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir[i]), true, i == 0);
      NIR_PASS_V(nir[i], nir_lower_system_values);
      NIR_PASS_V(nir[i], gl_nir_lower_samplers, prog);
      NIR_PASS_V(nir[i], nir_split_var_copies);
      NIR_PASS_V(nir[i], nir_lower_var_copies);

      NIR_PASS_V(nir[i], nir_lower_io, nir_var_uniform,
                 st_packed_uniforms_type_size,
                 (nir_lower_io_options)0);
      NIR_PASS_V(nir[i], nir_lower_uniforms_to_ubo, true, false);

      /* before buffers and vars_to_ssa */
      NIR_PASS_V(nir[i], gl_nir_lower_images, true);

      NIR_PASS_V(nir[i], gl_nir_lower_buffers, prog);
      NIR_PASS_V(nir[i], nir_opt_constant_folding);

      struct agx_shader_info out = { 0 };
      struct agx_shader_key keys[2] = {
         {
            .vs = {
               .num_vbufs = 1,
               .vbuf_strides = { 16 },
               .attributes = {
                  {
                     .buf = 0,
                     .src_offset = 0,
                     .format = AGX_FORMAT_I32,
                     .nr_comps_minus_1 = 4 - 1
                  }
               },
            }
         },
         {
            .fs = {
               .tib_formats = { AGX_FORMAT_U8NORM }
            }
         }
      };

      agx_compile_shader_nir(nir[i], &keys[i], &binary, &out);

      char *fn = NULL;
      asprintf(&fn, "shader_%u.bin", i);
      assert(fn != NULL);
      FILE *fp = fopen(fn, "wb");
      fwrite(binary.data, 1, binary.size, fp);
      fclose(fp);
      free(fn);

      util_dynarray_clear(&binary);
   }

   util_dynarray_fini(&binary);
}

static void
disassemble(const char *filename, bool verbose)
{
   FILE *fp = fopen(filename, "rb");
   assert(fp);

   fseek(fp, 0, SEEK_END);
   unsigned filesize = ftell(fp);
   rewind(fp);

   uint32_t *code = malloc(filesize);
   unsigned res = fread(code, 1, filesize, fp);
   if (res != filesize) {
      printf("Couldn't read full file\n");
   }
   fclose(fp);

   /* TODO: stub */

   free(code);
}

static void
tests()
{
#ifndef NDEBUG
   agx_minifloat_tests();
   printf("Pass.\n");
#else
   fprintf(stderr, "tests not compiled in NDEBUG mode");
#endif
}

int
main(int argc, char **argv)
{
   if (argc < 2) {
      printf("Pass a command\n");
      exit(1);
   }

   if (strcmp(argv[1], "compile") == 0)
      compile_shader(&argv[2]);
   else if (strcmp(argv[1], "disasm") == 0)
      disassemble(argv[2], false);
   else if (strcmp(argv[1], "disasm-verbose") == 0)
      disassemble(argv[2], true);
   else if (strcmp(argv[1], "test") == 0)
      tests();
   else
      unreachable("Unknown command. Valid: compile/disasm/disasm-verbose");

   return 0;
}
