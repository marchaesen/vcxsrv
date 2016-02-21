/*
 * Copyright © 2015 Intel Corporation
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
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
   void *mem_ctx;
   size_t size;
   size_t alloc;
   void *data;
} nir_array;

static inline void
nir_array_init(nir_array *arr, void *mem_ctx)
{
   arr->mem_ctx = mem_ctx;
   arr->size = 0;
   arr->alloc = 0;
   arr->data = NULL;
}

static inline void
nir_array_fini(nir_array *arr)
{
   if (arr->mem_ctx)
      ralloc_free(arr->data);
   else
      free(arr->data);
}

#define NIR_ARRAY_INITIAL_SIZE 64

/* Increments the size of the array by the given ammount and returns a
 * pointer to the beginning of the newly added space.
 */
static inline void *
nir_array_grow(nir_array *arr, size_t additional)
{
   size_t new_size = arr->size + additional;
   if (new_size > arr->alloc) {
      if (arr->alloc == 0)
         arr->alloc = NIR_ARRAY_INITIAL_SIZE;

      while (new_size > arr->alloc)
         arr->alloc *= 2;

      if (arr->mem_ctx)
         arr->data = reralloc_size(arr->mem_ctx, arr->data, arr->alloc);
      else
         arr->data = realloc(arr->data, arr->alloc);
   }

   void *ptr = (void *)((char *)arr->data + arr->size);
   arr->size = new_size;

   return ptr;
}

#define nir_array_add(arr, type, elem) \
   *(type *)nir_array_grow(arr, sizeof(type)) = (elem)

#define nir_array_foreach(arr, type, elem) \
   for (type *elem = (type *)(arr)->data; \
        elem < (type *)((char *)(arr)->data + (arr)->size); elem++)

#ifdef __cplusplus
} /* extern "C" */
#endif
