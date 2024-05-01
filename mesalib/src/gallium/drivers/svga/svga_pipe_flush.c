/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#include "pipe/p_defines.h"
#include "util/u_debug_image.h"
#include "util/u_string.h"
#include "svga_screen.h"
#include "svga_surface.h"
#include "svga_context.h"
#include "svga_debug.h"


static void svga_flush( struct pipe_context *pipe,
                        struct pipe_fence_handle **fence,
                        unsigned flags)
{
   struct svga_context *svga = svga_context(pipe);

   /* Emit buffered drawing commands, and any back copies.
    */
   svga_surfaces_flush( svga );

   if (flags & PIPE_FLUSH_FENCE_FD)
      svga->swc->hints |= SVGA_HINT_FLAG_EXPORT_FENCE_FD;

   /* Flush command queue.
    */
   svga_context_flush(svga, fence);

   SVGA_DBG(DEBUG_DMA|DEBUG_PERF, "%s fence_ptr %p\n",
            __func__, fence ? *fence : NULL);

   /* Enable to dump BMPs of the color/depth buffers each frame */
   if (0) {
      struct pipe_framebuffer_state *fb = &svga->curr.framebuffer;
      static unsigned frame_no = 1;
      char filename[256];
      unsigned i;

      for (i = 0; i < fb->nr_cbufs; i++) {
         snprintf(filename, sizeof(filename), "cbuf%u_%04u.bmp", i, frame_no);
         debug_dump_surface_bmp(&svga->pipe, filename, fb->cbufs[i]);
      }

      if (0 && fb->zsbuf) {
         snprintf(filename, sizeof(filename), "zsbuf_%04u.bmp", frame_no);
         debug_dump_surface_bmp(&svga->pipe, filename, fb->zsbuf);
      }

      ++frame_no;
   }
}


/**
 * svga_create_fence_fd
 *
 * Wraps a SVGA fence around an imported file descriptor.  This
 * fd represents a fence from another process/device.  The fence created
 * here can then be fed into fence_server_sync() so SVGA can synchronize
 * with an external process
 */
static void
svga_create_fence_fd(struct pipe_context *pipe,
                     struct pipe_fence_handle **fence,
                     int fd,
                     enum pipe_fd_type type)
{
   struct svga_winsys_screen *sws = svga_winsys_screen(pipe->screen);

   assert(type == PIPE_FD_TYPE_NATIVE_SYNC);
   sws->fence_create_fd(sws, fence, fd);
}


/**
 * svga_fence_server_sync
 *
 * This function imports a fence from another process/device into the current
 * software context so that SVGA can synchronize with it.
 */
static void
svga_fence_server_sync(struct pipe_context *pipe,
                       struct pipe_fence_handle *fence)
{
   struct svga_winsys_screen *sws = svga_winsys_screen(pipe->screen);
   struct svga_context *svga = svga_context(pipe);

   sws->fence_server_sync(sws, &svga->swc->imported_fence_fd, fence);
}


void svga_init_flush_functions( struct svga_context *svga )
{
   svga->pipe.flush = svga_flush;
   svga->pipe.create_fence_fd = svga_create_fence_fd;
   svga->pipe.fence_server_sync = svga_fence_server_sync;
}
