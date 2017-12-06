/*
 * Copyright (C) 2005-2007  Brian Paul   All Rights Reserved.
 * Copyright (C) 2008  VMware, Inc.   All Rights Reserved.
 * Copyright © 2010 Intel Corporation
 * Copyright © 2011 Bryan Cain
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

#include "st_glsl_types.h"

/**
 * Returns the number of places to offset the uniform index, given the type of
 * a struct member. We use this because samplers and images have backing
 * storeage only when they are bindless.
 */
int
st_glsl_storage_type_size(const struct glsl_type *type, bool is_bindless)
{
   unsigned int i;
   int size;

   switch (type->base_type) {
   case GLSL_TYPE_UINT:
   case GLSL_TYPE_INT:
   case GLSL_TYPE_FLOAT:
   case GLSL_TYPE_BOOL:
      if (type->is_matrix()) {
         return type->matrix_columns;
      } else {
         /* Regardless of size of vector, it gets a vec4. This is bad
          * packing for things like floats, but otherwise arrays become a
          * mess.  Hopefully a later pass over the code can pack scalars
          * down if appropriate.
          */
         return 1;
      }
      break;
   case GLSL_TYPE_DOUBLE:
      if (type->is_matrix()) {
         if (type->vector_elements <= 2)
            return type->matrix_columns;
         else
            return type->matrix_columns * 2;
      } else {
         /* For doubles if we have a double or dvec2 they fit in one
          * vec4, else they need 2 vec4s.
          */
         if (type->vector_elements <= 2)
            return 1;
         else
            return 2;
      }
      break;
   case GLSL_TYPE_UINT64:
   case GLSL_TYPE_INT64:
      if (type->vector_elements <= 2)
         return 1;
      else
         return 2;
   case GLSL_TYPE_ARRAY:
      assert(type->length > 0);
      return st_glsl_storage_type_size(type->fields.array, is_bindless) *
         type->length;
   case GLSL_TYPE_STRUCT:
      size = 0;
      for (i = 0; i < type->length; i++) {
         size += st_glsl_storage_type_size(type->fields.structure[i].type,
                                               is_bindless);
      }
      return size;
   case GLSL_TYPE_SAMPLER:
   case GLSL_TYPE_IMAGE:
      if (!is_bindless)
         return 0;
      /* fall through */
   case GLSL_TYPE_SUBROUTINE:
      return 1;
   case GLSL_TYPE_ATOMIC_UINT:
   case GLSL_TYPE_INTERFACE:
   case GLSL_TYPE_VOID:
   case GLSL_TYPE_ERROR:
   case GLSL_TYPE_FUNCTION:
   case GLSL_TYPE_FLOAT16:
   case GLSL_TYPE_UINT16:
   case GLSL_TYPE_INT16:
      assert(!"Invalid type in type_size");
      break;
   }
   return 0;
}
