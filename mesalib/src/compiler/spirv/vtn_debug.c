/*
 * Copyright © 2024 Collabora, Ltd.
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
 *    Faith Ekstrand (faith@gfxstrand.net)
 *
 */

#include "vtn_private.h"

#ifdef HAVE_SPIRV_TOOLS
#include <spirv-tools/libspirv.h>
#endif /* HAVE_SPIRV_TOOLS */

void
spirv_print_asm(FILE *fp, const uint32_t *words, size_t word_count)
{
#ifdef HAVE_SPIRV_TOOLS
   spv_context ctx = spvContextCreate(SPV_ENV_UNIVERSAL_1_6);

   spv_binary_to_text_options_t options =
      SPV_BINARY_TO_TEXT_OPTION_INDENT |
      SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES;

   if (MESA_SPIRV_DEBUG(COLOR))
      options |= SPV_BINARY_TO_TEXT_OPTION_COLOR;

   spv_text text = NULL;
   spv_diagnostic diagnostic = NULL;
   spv_result_t res = spvBinaryToText(ctx, words, word_count, options,
                                      &text, &diagnostic);
   if (res == SPV_SUCCESS) {
      fprintf(fp, "SPIR-V assembly:\n");
      fwrite(text->str, 1, text->length, fp);
   } else {
      fprintf(fp, "Failed to disassemble SPIR-V:\n");
      spvDiagnosticPrint(diagnostic);
      spvDiagnosticDestroy(diagnostic);
   }

   spvTextDestroy(text);
#else
   fprintf(fp, "Cannot dump SPIR-V assembly. "
               "You need to build against SPIR-V tools.\n");
#endif /* HAVE_SPIRV_TOOLS */
}
