/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "glspirv.h"
#include "errors.h"
#include "shaderobj.h"

#include "compiler/nir/nir.h"
#include "compiler/spirv/nir_spirv.h"

#include "program/program.h"

#include "util/u_atomic.h"

void
_mesa_spirv_module_reference(struct gl_spirv_module **dest,
                             struct gl_spirv_module *src)
{
   struct gl_spirv_module *old = *dest;

   if (old && p_atomic_dec_zero(&old->RefCount))
      free(old);

   *dest = src;

   if (src)
      p_atomic_inc(&src->RefCount);
}

void
_mesa_shader_spirv_data_reference(struct gl_shader_spirv_data **dest,
                                  struct gl_shader_spirv_data *src)
{
   struct gl_shader_spirv_data *old = *dest;

   if (old && p_atomic_dec_zero(&old->RefCount)) {
      _mesa_spirv_module_reference(&(*dest)->SpirVModule, NULL);
      ralloc_free(old);
   }

   *dest = src;

   if (src)
      p_atomic_inc(&src->RefCount);
}

void
_mesa_spirv_shader_binary(struct gl_context *ctx,
                          unsigned n, struct gl_shader **shaders,
                          const void* binary, size_t length)
{
   struct gl_spirv_module *module;
   struct gl_shader_spirv_data *spirv_data;

   assert(length >= 0);

   module = malloc(sizeof(*module) + length);
   if (!module) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glShaderBinary");
      return;
   }

   p_atomic_set(&module->RefCount, 0);
   module->Length = length;
   memcpy(&module->Binary[0], binary, length);

   for (int i = 0; i < n; ++i) {
      struct gl_shader *sh = shaders[i];

      spirv_data = rzalloc(NULL, struct gl_shader_spirv_data);
      _mesa_shader_spirv_data_reference(&sh->spirv_data, spirv_data);
      _mesa_spirv_module_reference(&spirv_data->SpirVModule, module);

      sh->CompileStatus = COMPILE_FAILURE;

      free((void *)sh->Source);
      sh->Source = NULL;
      free((void *)sh->FallbackSource);
      sh->FallbackSource = NULL;

      ralloc_free(sh->ir);
      sh->ir = NULL;
      ralloc_free(sh->symbols);
      sh->symbols = NULL;
   }
}

/**
 * This is the equivalent to compiler/glsl/linker.cpp::link_shaders()
 * but for SPIR-V programs.
 *
 * This method just creates the gl_linked_shader structs with a reference to
 * the SPIR-V data collected during previous steps.
 *
 * The real linking happens later in the driver-specifc call LinkShader().
 * This is so backends can implement different linking strategies for
 * SPIR-V programs.
 */
void
_mesa_spirv_link_shaders(struct gl_context *ctx, struct gl_shader_program *prog)
{
   prog->data->LinkStatus = LINKING_SUCCESS;
   prog->data->Validated = false;

   for (unsigned i = 0; i < prog->NumShaders; i++) {
      struct gl_shader *shader = prog->Shaders[i];
      gl_shader_stage shader_type = shader->Stage;

      /* We only support one shader per stage. The gl_spirv spec doesn't seem
       * to prevent this, but the way the API is designed, requiring all shaders
       * to be specialized with an entry point, makes supporting this quite
       * undefined.
       *
       * TODO: Turn this into a proper error once the spec bug
       * <https://gitlab.khronos.org/opengl/API/issues/58> is resolved.
       */
      if (prog->_LinkedShaders[shader_type]) {
         ralloc_strcat(&prog->data->InfoLog,
                       "\nError trying to link more than one SPIR-V shader "
                       "per stage.\n");
         prog->data->LinkStatus = LINKING_FAILURE;
         return;
      }

      assert(shader->spirv_data);

      struct gl_linked_shader *linked = rzalloc(NULL, struct gl_linked_shader);
      linked->Stage = shader_type;

      /* Create program and attach it to the linked shader */
      struct gl_program *gl_prog =
         ctx->Driver.NewProgram(ctx,
                                _mesa_shader_stage_to_program(shader_type),
                                prog->Name, false);
      if (!gl_prog) {
         prog->data->LinkStatus = LINKING_FAILURE;
         _mesa_delete_linked_shader(ctx, linked);
         return;
      }

      _mesa_reference_shader_program_data(ctx,
                                          &gl_prog->sh.data,
                                          prog->data);

      /* Don't use _mesa_reference_program() just take ownership */
      linked->Program = gl_prog;

      /* Reference the SPIR-V data from shader to the linked shader */
      _mesa_shader_spirv_data_reference(&linked->spirv_data,
                                        shader->spirv_data);

      prog->_LinkedShaders[shader_type] = linked;
      prog->data->linked_stages |= 1 << shader_type;
   }
}

nir_shader *
_mesa_spirv_to_nir(struct gl_context *ctx,
                   const struct gl_shader_program *prog,
                   gl_shader_stage stage,
                   const nir_shader_compiler_options *options)
{
   nir_shader *nir = NULL;

   struct gl_linked_shader *linked_shader = prog->_LinkedShaders[stage];
   assert (linked_shader);

   struct gl_shader_spirv_data *spirv_data = linked_shader->spirv_data;
   assert(spirv_data);

   struct gl_spirv_module *spirv_module = spirv_data->SpirVModule;
   assert (spirv_module != NULL);

   const char *entry_point_name = spirv_data->SpirVEntryPoint;
   assert(entry_point_name);

   struct nir_spirv_specialization *spec_entries =
      calloc(sizeof(*spec_entries),
             spirv_data->NumSpecializationConstants);

   for (unsigned i = 0; i < spirv_data->NumSpecializationConstants; ++i) {
      spec_entries[i].id = spirv_data->SpecializationConstantsIndex[i];
      spec_entries[i].data32 = spirv_data->SpecializationConstantsValue[i];
      spec_entries[i].defined_on_module = false;
   }

   const struct spirv_to_nir_options spirv_options = {
      .caps = ctx->Const.SpirVCapabilities
   };

   nir_function *entry_point =
      spirv_to_nir((const uint32_t *) &spirv_module->Binary[0],
                   spirv_module->Length / 4,
                   spec_entries, spirv_data->NumSpecializationConstants,
                   stage, entry_point_name,
                   &spirv_options,
                   options);
   free(spec_entries);

   assert (entry_point);
   nir = entry_point->shader;
   assert(nir->info.stage == stage);

   nir->options = options;

   nir->info.name =
      ralloc_asprintf(nir, "SPIRV:%s:%d",
                      _mesa_shader_stage_to_abbrev(nir->info.stage),
                      prog->Name);
   nir_validate_shader(nir);

   return nir;
}

void GLAPIENTRY
_mesa_SpecializeShaderARB(GLuint shader,
                          const GLchar *pEntryPoint,
                          GLuint numSpecializationConstants,
                          const GLuint *pConstantIndex,
                          const GLuint *pConstantValue)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_shader *sh;
   bool has_entry_point;
   struct nir_spirv_specialization *spec_entries = NULL;

   if (!ctx->Extensions.ARB_gl_spirv) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glSpecializeShaderARB");
      return;
   }

   sh = _mesa_lookup_shader_err(ctx, shader, "glSpecializeShaderARB");
   if (!sh)
      return;

   if (!sh->spirv_data) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glSpecializeShaderARB(not SPIR-V)");
      return;
   }

   if (sh->CompileStatus) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glSpecializeShaderARB(already specialized)");
      return;
   }

   struct gl_shader_spirv_data *spirv_data = sh->spirv_data;

   /* From the GL_ARB_gl_spirv spec:
    *
    *    "The OpenGL API expects the SPIR-V module to have already been
    *     validated, and can return an error if it discovers anything invalid
    *     in the module. An invalid SPIR-V module is allowed to result in
    *     undefined behavior."
    *
    * However, the following errors still need to be detected (from the same
    * spec):
    *
    *    "INVALID_VALUE is generated if <pEntryPoint> does not name a valid
    *     entry point for <shader>.
    *
    *     INVALID_VALUE is generated if any element of <pConstantIndex>
    *     refers to a specialization constant that does not exist in the
    *     shader module contained in <shader>."
    *
    * We cannot flag those errors a-priori because detecting them requires
    * parsing the module. However, flagging them during specialization is okay,
    * since it makes no difference in terms of application-visible state.
    */
   spec_entries = calloc(sizeof(*spec_entries), numSpecializationConstants);

   for (unsigned i = 0; i < numSpecializationConstants; ++i) {
      spec_entries[i].id = pConstantIndex[i];
      spec_entries[i].data32 = pConstantValue[i];
      spec_entries[i].defined_on_module = false;
   }

   has_entry_point =
      gl_spirv_validation((uint32_t *)&spirv_data->SpirVModule->Binary[0],
                          spirv_data->SpirVModule->Length / 4,
                          spec_entries, numSpecializationConstants,
                          sh->Stage, pEntryPoint);

   /* See previous spec comment */
   if (!has_entry_point) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glSpecializeShaderARB(\"%s\" is not a valid entry point"
                  " for shader)", pEntryPoint);
      goto end;
   }

   for (unsigned i = 0; i < numSpecializationConstants; ++i) {
      if (spec_entries[i].defined_on_module == false) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glSpecializeShaderARB(constant \"%i\" does not exist "
                     "in shader)", spec_entries[i].id);
         goto end;
      }
   }

   spirv_data->SpirVEntryPoint = ralloc_strdup(spirv_data, pEntryPoint);

   /* Note that we didn't make a real compilation of the module (spirv_to_nir),
    * but just checked some error conditions. Real "compilation" will be done
    * later, upon linking.
    */
   sh->CompileStatus = COMPILE_SUCCESS;

   spirv_data->NumSpecializationConstants = numSpecializationConstants;
   spirv_data->SpecializationConstantsIndex =
      rzalloc_array_size(spirv_data, sizeof(GLuint),
                         numSpecializationConstants);
   spirv_data->SpecializationConstantsValue =
      rzalloc_array_size(spirv_data, sizeof(GLuint),
                         numSpecializationConstants);
   for (unsigned i = 0; i < numSpecializationConstants; ++i) {
      spirv_data->SpecializationConstantsIndex[i] = pConstantIndex[i];
      spirv_data->SpecializationConstantsValue[i] = pConstantValue[i];
   }

 end:
   free(spec_entries);
}
