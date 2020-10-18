/*
 * Copyright © 2017 Red Hat.
 * Copyright © 2017 Valve Corporation.
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

#ifndef ST_CB_MEMORYOBJECTS_H
#define ST_CB_MEMORYOBJECTS_H

#include "main/mtypes.h"

struct dd_function_table;
struct pipe_screen;

struct st_memory_object
{
   struct gl_memory_object Base;
   struct pipe_memory_object *memory;

   /* TEXTURE_TILING_EXT param from gl_texture_object */
   GLuint TextureTiling;
};

static inline struct st_memory_object *
st_memory_object(struct gl_memory_object *obj)
{
   return (struct st_memory_object *)obj;
}

extern void
st_init_memoryobject_functions(struct dd_function_table *functions);

#endif
