/*
 * Copyright © 2014 Broadcom
 * Copyright © 208 Alyssa Rosenzweig
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
 */

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "util/format/u_format.h"
#include "util/os_file.h"
#include "util/u_math.h"
#include "util/u_memory.h"

#include "drm-uapi/drm.h"
#include "renderonly/renderonly.h"
#include "panfrost_drm_public.h"
#include "panfrost/pan_public.h"
#include "xf86drm.h"

static struct renderonly_scanout *
panfrost_create_kms_dumb_buffer_for_resource(struct pipe_resource *rsc,
                                             struct renderonly *ro,
                                             struct winsys_handle *out_handle)
{
   /* Find the smallest width alignment that gives us a 64byte aligned stride */
   unsigned blk_sz = util_format_get_blocksize(rsc->format);

   assert(blk_sz);

   unsigned align_w = 1;
   for (unsigned i = 1; i <= blk_sz; i++) {
      if (!((64 * i) % blk_sz)) {
         align_w = (64 * i) / blk_sz;
	 break;
      }
   }

   struct drm_mode_create_dumb create_dumb = {
      .width = ALIGN_NPOT(rsc->width0, align_w),
      .height = rsc->height0,
      .bpp = util_format_get_blocksizebits(rsc->format),
   };
   struct drm_mode_destroy_dumb destroy_dumb = {0};

   /* Align width to end up with a buffer that's aligned on 64 bytes. */

   struct renderonly_scanout *scanout = CALLOC_STRUCT(renderonly_scanout);
   if (!scanout)
      return NULL;

   /* create dumb buffer at scanout GPU */
   int err = drmIoctl(ro->kms_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb);
   if (err < 0) {
      fprintf(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed: %s\n",
            strerror(errno));
      goto free_scanout;
   }

   if (create_dumb.pitch % 64)
      goto free_dumb;

   scanout->handle = create_dumb.handle;
   scanout->stride = create_dumb.pitch;

   if (!out_handle)
      return scanout;

   /* fill in winsys handle */
   memset(out_handle, 0, sizeof(*out_handle));
   out_handle->type = WINSYS_HANDLE_TYPE_FD;
   out_handle->stride = create_dumb.pitch;

   err = drmPrimeHandleToFD(ro->kms_fd, create_dumb.handle, O_CLOEXEC,
                            (int *)&out_handle->handle);
   if (err < 0) {
      fprintf(stderr, "failed to export dumb buffer: %s\n", strerror(errno));
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

struct pipe_screen *
panfrost_drm_screen_create(int fd)
{
   return panfrost_create_screen(os_dupfd_cloexec(fd), NULL);
}

struct pipe_screen *
panfrost_drm_screen_create_renderonly(struct renderonly *ro)
{
   ro->create_for_resource = panfrost_create_kms_dumb_buffer_for_resource;
   return panfrost_create_screen(os_dupfd_cloexec(ro->gpu_fd), ro);
}
