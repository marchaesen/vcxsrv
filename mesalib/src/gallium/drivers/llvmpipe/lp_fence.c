/**************************************************************************
 *
 * Copyright 2009 VMware, Inc.
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


#include "pipe/p_screen.h"
#include "util/u_memory.h"
#include "util/os_file.h"
#include "lp_debug.h"
#include "lp_fence.h"
#include "lp_screen.h"
#include "lp_texture.h"
#include "lp_flush.h"
#include "lp_context.h"


#include "util/timespec.h"

#ifdef HAVE_LIBDRM
#include <xf86drm.h>
#include <drm-uapi/dma-buf.h>
#include <poll.h>
#include "util/libsync.h"
#include "util/list.h"
#endif

static unsigned fence_id = 0;

#ifdef HAVE_LIBDRM
static int sync_fd_wait(int fd, uint64_t timeout)
{
   struct pollfd fds = {0};
   int ret;
   struct timespec poll_start, poll_end, timeout_ts, diff;
   timespec_from_nsec(&timeout_ts, timeout);

   fds.fd = fd;
   fds.events = POLLIN;

   do {
      clock_gettime(CLOCK_MONOTONIC, &poll_start);
      ret = ppoll(&fds, 1, &timeout_ts, NULL);
      clock_gettime(CLOCK_MONOTONIC, &poll_end);
      if (ret > 0) {
         if (fds.revents & (POLLERR | POLLNVAL)) {
            errno = EINVAL;
            return -1;
         }
         return 0;
      } else if (ret == 0) {
         errno = ETIME;
         return -1;
      }

      timespec_sub(&diff, &poll_end, &poll_start);
      timespec_sub_saturate(&timeout_ts, &timeout_ts, &diff);
   } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

   return ret;
}
#endif

/**
 * Create a new fence object.
 *
 * The rank will be the number of bins in the scene.  Whenever a rendering
 * thread hits a fence command, it'll increment the fence counter.  When
 * the counter == the rank, the fence is finished.
 *
 * \param rank  the expected finished value of the fence counter.
 */
struct lp_fence *
lp_fence_create(unsigned rank)
{
   struct lp_fence *fence = CALLOC_STRUCT(lp_fence);

   if (!fence)
      return NULL;

   pipe_reference_init(&fence->reference, 1);
   fence->type = LP_FENCE_TYPE_SW;

   (void) mtx_init(&fence->mutex, mtx_plain);
   cnd_init(&fence->signalled);

   fence->id = p_atomic_inc_return(&fence_id) - 1;
   fence->rank = rank;

#ifdef HAVE_LIBDRM
   fence->sync_fd = -1;
#endif

   if (LP_DEBUG & DEBUG_FENCE)
      debug_printf("%s %d\n", __func__, fence->id);

   return fence;
}

/** Destroy a fence.  Called when refcount hits zero. */
void
lp_fence_destroy(struct lp_fence *fence)
{
   if (LP_DEBUG & DEBUG_FENCE)
      debug_printf("%s %d\n", __func__, fence->id);

   if (fence->type == LP_FENCE_TYPE_SW) {
      mtx_destroy(&fence->mutex);
      cnd_destroy(&fence->signalled);
   }
#ifdef HAVE_LIBDRM
   else {
      close(fence->sync_fd);
   }
#endif

   FREE(fence);
}


/**
 * Called by the rendering threads to increment the fence counter.
 * When the counter == the rank, the fence is finished.
 */
void
lp_fence_signal(struct lp_fence *fence)
{
   if (LP_DEBUG & DEBUG_FENCE)
      debug_printf("%s %d\n", __func__, fence->id);

   if (fence->type == LP_FENCE_TYPE_SW) {
      mtx_lock(&fence->mutex);

      fence->count++;
      assert(fence->count <= fence->rank);

      if (LP_DEBUG & DEBUG_FENCE)
         debug_printf("%s count=%u rank=%u\n", __func__,
               fence->count, fence->rank);

      /* Wakeup all threads waiting on the mutex:
      */
      cnd_broadcast(&fence->signalled);

      mtx_unlock(&fence->mutex);
   }

   /* sync fd fence we create ourselves are always signalled so
    * we don't need an else clause
    */
}


bool
lp_fence_signalled(struct lp_fence *f)
{
   if (f->type == LP_FENCE_TYPE_SW)
      return f->count == f->rank;
#ifdef HAVE_LIBDRM
   else {
      return sync_wait(f->sync_fd, 0) == 0;
   }
#endif

   unreachable("Fence is an unknown type");
   return false;
}


void
lp_fence_wait(struct lp_fence *f)
{
   if (LP_DEBUG & DEBUG_FENCE)
      debug_printf("%s %d\n", __func__, f->id);

   if (f->type == LP_FENCE_TYPE_SW) {
      mtx_lock(&f->mutex);
      assert(f->issued);
      while (f->count < f->rank) {
         cnd_wait(&f->signalled, &f->mutex);
      }
      mtx_unlock(&f->mutex);
   }
#ifdef HAVE_LIBDRM
   else {
      assert(f->sync_fd != -1);
      sync_wait(f->sync_fd, -1);
   }
#endif
}


bool
lp_fence_timedwait(struct lp_fence *f, uint64_t timeout)
{
   struct timespec ts, abs_ts;

   timespec_get(&ts, TIME_UTC);

   bool ts_overflow = timespec_add_nsec(&abs_ts, &ts, timeout);

   if (LP_DEBUG & DEBUG_FENCE)
      debug_printf("%s %d\n", __func__, f->id);

   if (f->type == LP_FENCE_TYPE_SW) {
      mtx_lock(&f->mutex);
      assert(f->issued);
      while (f->count < f->rank) {
         int ret;
         if (ts_overflow)
            ret = cnd_wait(&f->signalled, &f->mutex);
         else
            ret = cnd_timedwait(&f->signalled, &f->mutex, &abs_ts);
         if (ret != thrd_success)
            break;
      }

      const bool result = (f->count >= f->rank);
      mtx_unlock(&f->mutex);
      return result;
   }
#ifdef HAVE_LIBDRM
   else {
      assert(f->sync_fd != -1);
      return sync_fd_wait(f->sync_fd, timeout) == 0;
   }
#endif

   unreachable("Fence is an unknown type");
   return false;
}

#ifdef HAVE_LIBDRM
static int
lp_fence_get_fd(struct pipe_screen *pscreen,
                struct pipe_fence_handle *fence)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(pscreen);
   struct lp_fence *lp_fence = (struct lp_fence *)fence;

   /* It's not ideal, but since we cannot properly support sync files
    * from userspace, what we will do instead is wait for llvmpipe to
    * finish rendering, and then export the sync file. If its not a
    * sync file we imported we can just export a dummy one that is always
    * signalled since llvmpipe should have now finished all its work.
    */
   list_for_each_entry(struct llvmpipe_context, ctx, &screen->ctx_list, list) {
      llvmpipe_finish((struct pipe_context *)ctx, __func__);
   }

   if (lp_fence && lp_fence->sync_fd != -1) {
      return os_dupfd_cloexec(lp_fence->sync_fd);
   } else if (screen->dummy_sync_fd != -1) {
      return os_dupfd_cloexec(screen->dummy_sync_fd);
   }

   return -1;
}

static void
lp_create_fence_fd(struct pipe_context *pipe,
                   struct pipe_fence_handle **fence,
                   int fd,
                   enum pipe_fd_type type)
{
   /* Only sync fd are supported */
   if (type != PIPE_FD_TYPE_NATIVE_SYNC)
      goto fail;

   struct lp_fence *f = CALLOC_STRUCT(lp_fence);

   if (!fence)
      goto fail;

   pipe_reference_init(&f->reference, 1);
   f->type = LP_FENCE_TYPE_SYNC_FD;
   f->id = p_atomic_inc_return(&fence_id) - 1;
   f->sync_fd = os_dupfd_cloexec(fd);
   f->issued = true;

   *fence = (struct pipe_fence_handle*)f;
   return;
fail:
   *fence = NULL;
   return;
}

void
llvmpipe_init_screen_fence_funcs(struct pipe_screen *pscreen)
{
   struct llvmpipe_screen *screen = llvmpipe_screen(pscreen);
   screen->dummy_sync_fd = -1;

   /* Try to create dummy dmabuf, and only set functions if we were able to */
   int fd = -1;
   screen->dummy_dmabuf =
      (struct llvmpipe_memory_allocation*)pscreen->allocate_memory_fd(
            pscreen, 1, &fd, true);

   /* We don't need this fd handle and API always creates it */
   if (fd != -1)
      close(fd);

   if (screen->dummy_dmabuf) {
      struct dma_buf_export_sync_file export = {
         .flags = DMA_BUF_SYNC_RW,
         .fd = -1,
      };

      if (drmIoctl(screen->dummy_dmabuf->dmabuf_fd,
                   DMA_BUF_IOCTL_EXPORT_SYNC_FILE,
                   &export))
         goto fail;

      screen->dummy_sync_fd = export.fd;
   }

   pscreen->fence_get_fd = lp_fence_get_fd;
   return;
fail:
   if (screen->dummy_dmabuf) {
      pscreen->free_memory_fd(
            pscreen, (struct pipe_memory_allocation*)screen->dummy_dmabuf);
      screen->dummy_dmabuf = NULL;
   }
   return;
}

void
llvmpipe_init_fence_funcs(struct pipe_context *pipe)
{
   pipe->create_fence_fd = lp_create_fence_fd;
}
#endif
