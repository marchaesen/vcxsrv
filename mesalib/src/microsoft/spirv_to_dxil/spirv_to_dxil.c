/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "spirv_to_dxil.h"
#include "nir_to_dxil.h"
#include "shader_enums.h"
#include "spirv/nir_spirv.h"
#include "util/blob.h"

bool
spirv_to_dxil(const uint32_t *words, size_t word_count,
              struct dxil_spirv_specialization *specializations,
              unsigned int num_specializations, dxil_spirv_shader_stage stage,
              const char *entry_point_name, void **buffer, size_t *size)
{
   if (stage == MESA_SHADER_NONE || stage == MESA_SHADER_KERNEL)
      return false;

   struct spirv_to_nir_options spirv_opts = {0};

   glsl_type_singleton_init_or_ref();

   nir_shader *nir = spirv_to_nir(
      words, word_count, (struct nir_spirv_specialization *)specializations,
      num_specializations, (gl_shader_stage)stage, entry_point_name,
      &spirv_opts, dxil_get_nir_compiler_options());
   if (!nir) {
      glsl_type_singleton_decref();
      return false;
   }

   nir_validate_shader(nir,
                       "Validate before feeding NIR to the DXIL compiler");

   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);

   struct nir_to_dxil_options opts = {0};

   struct blob dxil_blob;
   if (!nir_to_dxil(nir, &opts, &dxil_blob)) {
      if (dxil_blob.allocated)
         blob_finish(&dxil_blob);
      glsl_type_singleton_decref();
      return false;
   }

   blob_finish_get_buffer(&dxil_blob, buffer, size);

   glsl_type_singleton_decref();
   return true;
}

void
spirv_to_dxil_free(void* buffer)
{
   free(buffer);
}
