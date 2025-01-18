/**************************************************************************
 *
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include <assert.h>

#include "pipe/p_screen.h"
#include "pipe-loader/pipe_loader.h"
#include "frontend/drm_driver.h"

#include "util/u_memory.h"
#include "vl/vl_winsys.h"

#include "loader.h"

static void
vl_drm_screen_destroy(struct vl_screen *vscreen);

struct vl_screen *
vl_drm_screen_create(int fd, bool honor_dri_prime)
{
   struct vl_screen *vscreen;
   int libva_owned_fd = -1;

   if (honor_dri_prime) {
      /* Pass a non-NULL value as the 2nd param in order to not
       * close the original fd - it's owned by libva.
       * If fd is overriden, we'll close after the call to
       * pipe_loader_drm_probe_fd because pipe_loader dups the fd.
       */
      loader_get_user_preferred_fd(&fd, &libva_owned_fd);
   }

   vscreen = CALLOC_STRUCT(vl_screen);
   if (!vscreen)
      return NULL;

   if (pipe_loader_drm_probe_fd(&vscreen->dev, fd, false))
      vscreen->pscreen = pipe_loader_create_screen(vscreen->dev, false);

   if (libva_owned_fd >= 0 && libva_owned_fd != fd)
      close(fd);

   if (!vscreen->pscreen)
      goto release_pipe;

   vscreen->destroy = vl_drm_screen_destroy;
   vscreen->texture_from_drawable = NULL;
   vscreen->get_dirty_area = NULL;
   vscreen->get_timestamp = NULL;
   vscreen->set_next_timestamp = NULL;
   vscreen->get_private = NULL;
   return vscreen;

release_pipe:
   if (vscreen->dev)
      pipe_loader_release(&vscreen->dev, 1);

   FREE(vscreen);
   return NULL;
}

static void
vl_drm_screen_destroy(struct vl_screen *vscreen)
{
   assert(vscreen);

   vscreen->pscreen->destroy(vscreen->pscreen);
   pipe_loader_release(&vscreen->dev, 1);
   /* CHECK: The VAAPI loader/user preserves ownership of the original fd */
   FREE(vscreen);
}
