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

#include "renderonly/renderonly.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <xf86drm.h>

#include "state_tracker/drm_driver.h"
#include "pipe/p_screen.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

struct renderonly *
renderonly_dup(const struct renderonly *ro)
{
   struct renderonly *copy;

   copy = CALLOC_STRUCT(renderonly);
   if (!copy)
      return NULL;

   memcpy(copy, ro, sizeof(*ro));

   return copy;
}

struct renderonly_scanout *
renderonly_scanout_for_prime(struct pipe_resource *rsc, struct renderonly *ro)
{
   struct renderonly_scanout *scanout;

   scanout = CALLOC_STRUCT(renderonly_scanout);
   if (!scanout)
      return NULL;

   scanout->prime = rsc;

   return scanout;
}

void
renderonly_scanout_destroy(struct renderonly_scanout *scanout,
			   struct renderonly *ro)
{
   struct drm_mode_destroy_dumb destroy_dumb = { };

   pipe_resource_reference(&scanout->prime, NULL);
   if (ro->kms_fd != -1) {
      destroy_dumb.handle = scanout->handle;
      drmIoctl(ro->kms_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
   }
   FREE(scanout);
}

struct renderonly_scanout *
renderonly_create_kms_dumb_buffer_for_resource(struct pipe_resource *rsc,
                                               struct renderonly *ro)
{
   struct pipe_screen *screen = rsc->screen;
   struct renderonly_scanout *scanout;
   struct winsys_handle handle;
   int prime_fd, err;
   struct drm_mode_create_dumb create_dumb = {
      .width = rsc->width0,
      .height = rsc->height0,
      .bpp = 32,
   };
   struct drm_mode_destroy_dumb destroy_dumb = { };

   scanout = CALLOC_STRUCT(renderonly_scanout);
   if (!scanout)
      return NULL;

   /* create dumb buffer at scanout GPU */
   err = drmIoctl(ro->kms_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
   if (err < 0) {
      fprintf(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed: %s\n",
            strerror(errno));
      goto free_scanout;
   }

   scanout->handle = create_dumb.handle;
   scanout->stride = create_dumb.pitch;

   /* export dumb buffer */
   err = drmPrimeHandleToFD(ro->kms_fd, create_dumb.handle, O_CLOEXEC,
         &prime_fd);
   if (err < 0) {
      fprintf(stderr, "failed to export dumb buffer: %s\n", strerror(errno));
      goto free_dumb;
   }

   /* import dumb buffer */
   memset(&handle, 0, sizeof(handle));
   handle.type = DRM_API_HANDLE_TYPE_FD;
   handle.handle = prime_fd;
   handle.stride = create_dumb.pitch;

   scanout->prime = screen->resource_from_handle(screen, rsc,
         &handle, PIPE_HANDLE_USAGE_READ_WRITE);

   close(prime_fd);

   if (!scanout->prime) {
      fprintf(stderr, "failed to create resource_from_handle: %s\n", strerror(errno));
      goto free_dumb;
   }

   return scanout;

free_dumb:
   destroy_dumb.handle = scanout->handle;
   drmIoctl(ro->kms_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);

free_scanout:
   FREE(scanout);

   return NULL;
}

struct renderonly_scanout *
renderonly_create_gpu_import_for_resource(struct pipe_resource *rsc,
                                          struct renderonly *ro)
{
   struct pipe_screen *screen = rsc->screen;
   struct renderonly_scanout *scanout;
   boolean status;
   int fd, err;
   struct winsys_handle handle = {
      .type = DRM_API_HANDLE_TYPE_FD
   };

   scanout = CALLOC_STRUCT(renderonly_scanout);
   if (!scanout)
      return NULL;

   status = screen->resource_get_handle(screen, NULL, rsc, &handle,
         PIPE_HANDLE_USAGE_READ_WRITE);
   if (!status)
      goto free_scanout;

   scanout->stride = handle.stride;
   fd = handle.handle;

   err = drmPrimeFDToHandle(ro->kms_fd, fd, &scanout->handle);
   close(fd);

   if (err < 0) {
      fprintf(stderr, "drmPrimeFDToHandle() failed: %s\n", strerror(errno));
      goto free_scanout;
   }

   return scanout;

free_scanout:
   FREE(scanout);

   return NULL;
}

