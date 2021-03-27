/**************************************************************************
 * 
 * Copyright 2005 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#ifndef ST_CB_BUFFEROBJECTS_H
#define ST_CB_BUFFEROBJECTS_H

#include "main/mtypes.h"

struct dd_function_table;
struct pipe_resource;
struct pipe_screen;
struct st_context;

/**
 * State_tracker vertex/pixel buffer object, derived from Mesa's
 * gl_buffer_object.
 */
struct st_buffer_object
{
   struct gl_buffer_object Base;
   struct pipe_resource *buffer;     /* GPU storage */

   struct gl_context *ctx;  /* the context that owns private_refcount */

   /* This mechanism allows passing buffer references to the driver without
    * using atomics to increase the reference count.
    *
    * This private refcount can be decremented without atomics but only one
    * context (ctx above) can use this counter to be thread-safe.
    *
    * This number is atomically added to buffer->reference.count at
    * initialization. If it's never used, the same number is atomically
    * subtracted from buffer->reference.count before destruction. If this
    * number is decremented, we can pass that reference to the driver without
    * touching reference.count. At buffer destruction we only subtract
    * the number of references we did not return. This can possibly turn
    * a million atomic increments into 1 add and 1 subtract atomic op.
    */
   int private_refcount;

   struct pipe_transfer *transfer[MAP_COUNT];
};


/** cast wrapper */
static inline struct st_buffer_object *
st_buffer_object(struct gl_buffer_object *obj)
{
   return (struct st_buffer_object *) obj;
}


enum pipe_map_flags
st_access_flags_to_transfer_flags(GLbitfield access, bool wholeBuffer);


extern void
st_init_bufferobject_functions(struct pipe_screen *screen,
                               struct dd_function_table *functions);

static inline struct pipe_resource *
st_get_buffer_reference(struct gl_context *ctx, struct gl_buffer_object *obj)
{
   if (unlikely(!obj))
      return NULL;

   struct st_buffer_object *stobj = st_buffer_object(obj);
   struct pipe_resource *buffer = stobj->buffer;

   if (unlikely(!buffer))
      return NULL;

   /* Only one context is using the fast path. All other contexts must use
    * the slow path.
    */
   if (unlikely(stobj->ctx != ctx)) {
      p_atomic_inc(&buffer->reference.count);
      return buffer;
   }

   if (unlikely(stobj->private_refcount <= 0)) {
      assert(stobj->private_refcount == 0);

      /* This is the number of atomic increments we will skip. */
      stobj->private_refcount = 100000000;
      p_atomic_add(&buffer->reference.count, stobj->private_refcount);
   }

   /* Return a buffer reference while decrementing the private refcount. */
   stobj->private_refcount--;
   return buffer;
}

#endif
