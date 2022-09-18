/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stddef.h>

#include "compiler/glsl_types.h"
#include "rogue_compiler.h"
#include "util/ralloc.h"

/**
 * \file rogue_compiler.c
 *
 * \brief Contains the Rogue compiler interface.
 */

/**
 * \brief Creates and sets up a Rogue compiler context.
 *
 * \param[in] dev_info Device info pointer.
 * \return A pointer to the new compiler context, or NULL on failure.
 */
struct rogue_compiler *
rogue_compiler_create(const struct pvr_device_info *dev_info)
{
   struct rogue_compiler *compiler;

   compiler = rzalloc_size(NULL, sizeof(*compiler));
   if (!compiler)
      return NULL;

   compiler->dev_info = dev_info;

   /* TODO: Additional compiler setup (allocators? error message output
    * location?).
    */

   glsl_type_singleton_init_or_ref();

   return compiler;
}

/**
 * \brief Destroys and frees a compiler context.
 *
 * \param[in] compiler The compiler context.
 */
void rogue_compiler_destroy(struct rogue_compiler *compiler)
{
   glsl_type_singleton_decref();

   ralloc_free(compiler);
}
