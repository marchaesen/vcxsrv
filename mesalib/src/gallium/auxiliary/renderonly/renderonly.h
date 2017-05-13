/*
 * Copyright (C) 2016 Christian Gmeiner <christian.gmeiner@gmail.com>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Christian Gmeiner <christian.gmeiner@gmail.com>
 */

#ifndef RENDERONLY_H
#define RENDERONLY_H

#include <stdint.h>
#include "state_tracker/drm_driver.h"
#include "pipe/p_state.h"

struct renderonly_scanout {
   uint32_t handle;
   uint32_t stride;

   struct pipe_resource *prime;
};

struct renderonly {
   /**
    * Create a renderonly_scanout object for scanout resource.
    *
    * This function creates a renderonly_scanout object based on the provided
    * resource. The library is designed that the driver specific pipe_resource
    * struct holds a pointer to a renderonly_scanout struct.
    *
    * struct driver_resource {
    *    struct pipe_resource base;
    *    struct renderonly_scanout *scanout;
    *   ...
    * };
    *
    * The renderonly_scanout object exits for two reasons:
    * - Do any special treatment for a scanout resource like importing the GPU
    *   resource into the scanout hw.
    * - Make it easier for a gallium driver to detect if anything special needs
    *   to be done in flush_resource(..) like a resolve to linear.
    */
   struct renderonly_scanout *(*create_for_resource)(struct pipe_resource *rsc,
                                                     struct renderonly *ro);
   int kms_fd;
   int gpu_fd;
};

struct renderonly *
renderonly_dup(const struct renderonly *ro);

static inline struct renderonly_scanout *
renderonly_scanout_for_resource(struct pipe_resource *rsc, struct renderonly *ro)
{
   return ro->create_for_resource(rsc, ro);
}

struct renderonly_scanout *
renderonly_scanout_for_prime(struct pipe_resource *rsc, struct renderonly *ro);

void
renderonly_scanout_destroy(struct renderonly_scanout *scanout,
			   struct renderonly *ro);

static inline boolean
renderonly_get_handle(struct renderonly_scanout *scanout,
      struct winsys_handle *handle)
{
   if (!scanout)
      return FALSE;

   handle->handle = scanout->handle;
   handle->stride = scanout->stride;

   return TRUE;
}

/**
 * Create a dumb buffer object for a resource at scanout hw.
 */
struct renderonly_scanout *
renderonly_create_kms_dumb_buffer_for_resource(struct pipe_resource *rsc,
                                               struct renderonly *ro);

/**
 * Import GPU resource into scanout hw.
 */
struct renderonly_scanout *
renderonly_create_gpu_import_for_resource(struct pipe_resource *rsc,
                                          struct renderonly *ro);

#endif /* RENDERONLY_H_ */
