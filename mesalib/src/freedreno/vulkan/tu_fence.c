/*
 * Copyright © 2019 Google LLC
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

#include "tu_private.h"

#include <fcntl.h>
#include <libsync.h>
#include <unistd.h>

#include "util/os_time.h"

/**
 * Internally, a fence can be in one of these states.
 */
enum tu_fence_state
{
   TU_FENCE_STATE_RESET,
   TU_FENCE_STATE_PENDING,
   TU_FENCE_STATE_SIGNALED,
};

static enum tu_fence_state
tu_fence_get_state(const struct tu_fence *fence)
{
   if (fence->signaled)
      assert(fence->fd < 0);

   if (fence->signaled)
      return TU_FENCE_STATE_SIGNALED;
   else if (fence->fd >= 0)
      return TU_FENCE_STATE_PENDING;
   else
      return TU_FENCE_STATE_RESET;
}

static void
tu_fence_set_state(struct tu_fence *fence, enum tu_fence_state state, int fd)
{
   if (fence->fd >= 0)
      close(fence->fd);

   switch (state) {
   case TU_FENCE_STATE_RESET:
      assert(fd < 0);
      fence->signaled = false;
      fence->fd = -1;
      break;
   case TU_FENCE_STATE_PENDING:
      assert(fd >= 0);
      fence->signaled = false;
      fence->fd = fd;
      break;
   case TU_FENCE_STATE_SIGNALED:
      assert(fd < 0);
      fence->signaled = true;
      fence->fd = -1;
      break;
   default:
      unreachable("unknown fence state");
      break;
   }
}

void
tu_fence_init(struct tu_fence *fence, bool signaled)
{
   fence->signaled = signaled;
   fence->fd = -1;
}

void
tu_fence_finish(struct tu_fence *fence)
{
   if (fence->fd >= 0)
      close(fence->fd);
}

/**
 * Update the associated fd of a fence.  Ownership of \a fd is transferred to
 * \a fence.
 *
 * This function does not block.  \a fence can also be in any state when this
 * function is called.  To be able to do that, the caller must make sure that,
 * when both the currently associated fd and the new fd are valid, they are on
 * the same timeline with the new fd being later on the timeline.
 */
void
tu_fence_update_fd(struct tu_fence *fence, int fd)
{
   const enum tu_fence_state state =
      fd >= 0 ? TU_FENCE_STATE_PENDING : TU_FENCE_STATE_SIGNALED;
   tu_fence_set_state(fence, state, fd);
}

/**
 * Make a fence a copy of another fence.  \a fence must be in the reset state.
 */
void
tu_fence_copy(struct tu_fence *fence, const struct tu_fence *src)
{
   assert(tu_fence_get_state(fence) == TU_FENCE_STATE_RESET);

   /* dup src->fd */
   int fd = -1;
   if (src->fd >= 0) {
      fd = fcntl(src->fd, F_DUPFD_CLOEXEC, 0);
      if (fd < 0) {
         tu_loge("failed to dup fd %d for fence", src->fd);
         sync_wait(src->fd, -1);
      }
   }

   tu_fence_update_fd(fence, fd);
}

/**
 * Signal a fence.  \a fence must be in the reset state.
 */
void
tu_fence_signal(struct tu_fence *fence)
{
   assert(tu_fence_get_state(fence) == TU_FENCE_STATE_RESET);
   tu_fence_set_state(fence, TU_FENCE_STATE_SIGNALED, -1);
}

/**
 * Wait until a fence is idle (i.e., not pending).
 */
void
tu_fence_wait_idle(struct tu_fence *fence)
{
   if (fence->fd >= 0) {
      if (sync_wait(fence->fd, -1))
         tu_loge("sync_wait on fence fd %d failed", fence->fd);

      tu_fence_set_state(fence, TU_FENCE_STATE_SIGNALED, -1);
   }
}

VkResult
tu_CreateFence(VkDevice _device,
               const VkFenceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkFence *pFence)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   struct tu_fence *fence =
      vk_alloc2(&device->alloc, pAllocator, sizeof(*fence), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!fence)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   tu_fence_init(fence, pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT);

   *pFence = tu_fence_to_handle(fence);

   return VK_SUCCESS;
}

void
tu_DestroyFence(VkDevice _device,
                VkFence _fence,
                const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_fence, fence, _fence);

   if (!fence)
      return;

   tu_fence_finish(fence);

   vk_free2(&device->alloc, pAllocator, fence);
}

/**
 * Initialize a pollfd array from fences.
 */
static nfds_t
tu_fence_init_poll_fds(uint32_t fence_count,
                       const VkFence *fences,
                       bool wait_all,
                       struct pollfd *fds)
{
   nfds_t nfds = 0;
   for (uint32_t i = 0; i < fence_count; i++) {
      TU_FROM_HANDLE(tu_fence, fence, fences[i]);

      if (fence->signaled) {
         if (wait_all) {
            /* skip signaled fences */
            continue;
         } else {
            /* no need to poll any fd */
            nfds = 0;
            break;
         }
      }

      /* negative fds are never ready, which is the desired behavior */
      fds[nfds].fd = fence->fd;
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      nfds++;
   }

   return nfds;
}

/**
 * Translate timeout from nanoseconds to milliseconds for poll().
 */
static int
tu_fence_get_poll_timeout(uint64_t timeout_ns)
{
   const uint64_t ns_per_ms = 1000 * 1000;
   uint64_t timeout_ms = timeout_ns / ns_per_ms;

   /* round up if needed */
   if (timeout_ns - timeout_ms * ns_per_ms >= ns_per_ms / 2)
      timeout_ms++;

   return timeout_ms < INT_MAX ? timeout_ms : INT_MAX;
}

/**
 * Poll a pollfd array.
 */
static VkResult
tu_fence_poll_fds(struct pollfd *fds, nfds_t nfds, uint64_t *timeout_ns)
{
   while (true) {
      /* poll */
      uint64_t duration = os_time_get_nano();
      int ret = poll(fds, nfds, tu_fence_get_poll_timeout(*timeout_ns));
      duration = os_time_get_nano() - duration;

      /* update timeout_ns */
      if (*timeout_ns > duration)
         *timeout_ns -= duration;
      else
         *timeout_ns = 0;

      if (ret > 0) {
         return VK_SUCCESS;
      } else if (ret == 0) {
         if (!*timeout_ns)
            return VK_TIMEOUT;
      } else if (errno != EINTR && errno != EAGAIN) {
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }
}

/**
 * Update a pollfd array and the fence states.  This should be called after a
 * successful call to tu_fence_poll_fds.
 */
static nfds_t
tu_fence_update_fences_and_poll_fds(uint32_t fence_count,
                                    const VkFence *fences,
                                    bool wait_all,
                                    struct pollfd *fds)
{
   uint32_t nfds = 0;
   uint32_t fds_idx = 0;
   for (uint32_t i = 0; i < fence_count; i++) {
      TU_FROM_HANDLE(tu_fence, fence, fences[i]);

      /* no signaled fence in fds */
      if (fence->signaled)
         continue;

      /* fds[fds_idx] corresponds to fences[i] */
      assert(fence->fd == fds[fds_idx].fd);

      assert(nfds <= fds_idx && fds_idx <= i);

      /* fd is ready (errors are treated as ready) */
      if (fds[fds_idx].revents) {
         tu_fence_set_state(fence, TU_FENCE_STATE_SIGNALED, -1);
      } else if (wait_all) {
         /* add to fds again for another poll */
         fds[nfds].fd = fence->fd;
         fds[nfds].events = POLLIN;
         fds[nfds].revents = 0;
         nfds++;
      }

      fds_idx++;
   }

   return nfds;
}

VkResult
tu_WaitForFences(VkDevice _device,
                 uint32_t fenceCount,
                 const VkFence *pFences,
                 VkBool32 waitAll,
                 uint64_t timeout)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   /* add a simpler path for when fenceCount == 1? */

   struct pollfd stack_fds[8];
   struct pollfd *fds = stack_fds;
   if (fenceCount > ARRAY_SIZE(stack_fds)) {
      fds = vk_alloc(&device->alloc, sizeof(*fds) * fenceCount, 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!fds)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   /* set up pollfd array and start polling */
   nfds_t nfds = tu_fence_init_poll_fds(fenceCount, pFences, waitAll, fds);
   VkResult result = VK_SUCCESS;
   while (nfds) {
      result = tu_fence_poll_fds(fds, nfds, &timeout);
      if (result != VK_SUCCESS)
         break;
      nfds = tu_fence_update_fences_and_poll_fds(fenceCount, pFences, waitAll,
                                                 fds);
   }

   if (fds != stack_fds)
      vk_free(&device->alloc, fds);

   return result;
}

VkResult
tu_ResetFences(VkDevice _device, uint32_t fenceCount, const VkFence *pFences)
{
   for (unsigned i = 0; i < fenceCount; ++i) {
      TU_FROM_HANDLE(tu_fence, fence, pFences[i]);
      assert(tu_fence_get_state(fence) != TU_FENCE_STATE_PENDING);
      tu_fence_set_state(fence, TU_FENCE_STATE_RESET, -1);
   }

   return VK_SUCCESS;
}

VkResult
tu_GetFenceStatus(VkDevice _device, VkFence _fence)
{
   TU_FROM_HANDLE(tu_fence, fence, _fence);

   if (fence->fd >= 0) {
      int err = sync_wait(fence->fd, 0);
      if (!err)
         tu_fence_set_state(fence, TU_FENCE_STATE_SIGNALED, -1);
      else if (err && errno != ETIME)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return fence->signaled ? VK_SUCCESS : VK_NOT_READY;
}
