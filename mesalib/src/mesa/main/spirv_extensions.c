/*
 * Copyright © 2017 Intel Corporation
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

/**
 * \file
 * \brief SPIRV-V extension handling. See ARB_spirv_extensions
 */

#include <stdbool.h>
#include "spirv_extensions.h"

GLuint
_mesa_get_spirv_extension_count(struct gl_context *ctx)
{
   if (ctx->Const.SpirVExtensions == NULL)
      return 0;

   return ctx->Const.SpirVExtensions->count;
}

const GLubyte *
_mesa_get_enabled_spirv_extension(struct gl_context *ctx,
                                  GLuint index)
{
   unsigned int n = 0;

   if (ctx->Const.SpirVExtensions == NULL)
      return (const GLubyte *) 0;

   for (unsigned int i = 0; i < SPV_EXTENSIONS_COUNT; i++) {
      if (ctx->Const.SpirVExtensions->supported[i]) {
         if (n == index)
            return (const GLubyte *) _mesa_spirv_extensions_to_string(i);
         else
            n++;
      }
   }

   return (const GLubyte *) 0;
}

const char *
_mesa_spirv_extensions_to_string(enum SpvExtension ext)
{
#define STR(x) case x: return #x;
   switch (ext) {
   STR(SPV_KHR_16bit_storage);
   STR(SPV_KHR_device_group);
   STR(SPV_KHR_multiview);
   STR(SPV_KHR_shader_ballot);
   STR(SPV_KHR_shader_draw_parameters);
   STR(SPV_KHR_storage_buffer_storage_class);
   STR(SPV_KHR_subgroup_vote);
   STR(SPV_KHR_variable_pointers);
   STR(SPV_AMD_gcn_shader);
   case SPV_EXTENSIONS_COUNT:
      unreachable("Unknown SPIR-V extension");
   }
#undef STR

   return "unknown";
}
