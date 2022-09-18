/*
 * Copyright © 2021 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <cstdio>

#include "dxil_validation.h"

#if DETECT_OS_WINDOWS

#include "dxil_validator.h"

bool
validate_dxil(dxil_spirv_object *dxil_obj)
{
   struct dxil_validator *val = dxil_create_validator(NULL);

   char *err;
   bool res = dxil_validate_module(val, dxil_obj->binary.buffer,
                                   dxil_obj->binary.size, &err);
   if (!res && err)
      fprintf(stderr, "DXIL: %s\n\n", err);

   dxil_destroy_validator(val);
   return res;
}

#else

bool
validate_dxil(dxil_spirv_object *dxil_obj)
{
   fprintf(stderr, "DXIL validation only available in Windows.\n");
   return false;
}

#endif
