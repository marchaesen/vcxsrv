/*
 * Copyright (c) 2020 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Christian Gmeiner <christian.gmeiner@gmail.com>
 */

#include "etnaviv_compiler.h"
#include "etnaviv_compiler_nir.h"
#include "etnaviv_debug.h"
#include "etnaviv_disk_cache.h"
#include "util/ralloc.h"

struct etna_compiler *
etna_compiler_create(const char *renderer)
{
   struct etna_compiler *compiler = rzalloc(NULL, struct etna_compiler);

   if (!DBG_ENABLED(ETNA_DBG_NIR))
      return compiler;

   compiler->regs = etna_ra_setup(compiler);
   if (!compiler->regs) {
      ralloc_free((void *)compiler);
      compiler = NULL;
   }

   etna_disk_cache_init(compiler, renderer);

   return compiler;
}

void
etna_compiler_destroy(const struct etna_compiler *compiler)
{
   ralloc_free((void *)compiler);
}
