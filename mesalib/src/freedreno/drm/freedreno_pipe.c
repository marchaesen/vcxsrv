/*
 * Copyright (C) 2012-2018 Rob Clark <robclark@freedesktop.org>
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
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "freedreno_drmif.h"
#include "freedreno_priv.h"

/**
 * priority of zero is highest priority, and higher numeric values are
 * lower priorities
 */
struct fd_pipe *
fd_pipe_new2(struct fd_device *dev, enum fd_pipe_id id, uint32_t prio)
{
   struct fd_pipe *pipe;
   uint64_t val;

   if (id > FD_PIPE_MAX) {
      ERROR_MSG("invalid pipe id: %d", id);
      return NULL;
   }

   if ((prio != 1) && (fd_device_version(dev) < FD_VERSION_SUBMIT_QUEUES)) {
      ERROR_MSG("invalid priority!");
      return NULL;
   }

   pipe = dev->funcs->pipe_new(dev, id, prio);
   if (!pipe) {
      ERROR_MSG("allocation failed");
      return NULL;
   }

   pipe->dev = fd_device_ref(dev);
   pipe->id = id;
   p_atomic_set(&pipe->refcnt, 1);

   fd_pipe_get_param(pipe, FD_GPU_ID, &val);
   pipe->dev_id.gpu_id = val;

   fd_pipe_get_param(pipe, FD_CHIP_ID, &val);
   pipe->dev_id.chip_id = val;

   pipe->control_mem = fd_bo_new(dev, sizeof(*pipe->control),
                                 FD_BO_CACHED_COHERENT,
                                 "pipe-control");
   pipe->control = fd_bo_map(pipe->control_mem);

   /* We could be getting a bo from the bo-cache, make sure the fence value
    * is not garbage:
    */
   pipe->control->fence = 0;

   /* We don't want the control_mem bo to hold a reference to the ourself,
    * so disable userspace fencing.  This also means that we won't be able
    * to determine if the buffer is idle which is needed by bo-cache.  But
    * pipe creation/destroy is not a high frequency event so just disable
    * the bo-cache as well:
    */
   pipe->control_mem->nosync = true;
   pipe->control_mem->bo_reuse = NO_CACHE;

   return pipe;
}

struct fd_pipe *
fd_pipe_new(struct fd_device *dev, enum fd_pipe_id id)
{
   return fd_pipe_new2(dev, id, 1);
}

struct fd_pipe *
fd_pipe_ref(struct fd_pipe *pipe)
{
   simple_mtx_lock(&table_lock);
   fd_pipe_ref_locked(pipe);
   simple_mtx_unlock(&table_lock);
   return pipe;
}

struct fd_pipe *
fd_pipe_ref_locked(struct fd_pipe *pipe)
{
   simple_mtx_assert_locked(&table_lock);
   pipe->refcnt++;
   return pipe;
}

void
fd_pipe_del(struct fd_pipe *pipe)
{
   simple_mtx_lock(&table_lock);
   fd_pipe_del_locked(pipe);
   simple_mtx_unlock(&table_lock);
}

void
fd_pipe_del_locked(struct fd_pipe *pipe)
{
   simple_mtx_assert_locked(&table_lock);
   if (!p_atomic_dec_zero(&pipe->refcnt))
      return;
   fd_bo_del_locked(pipe->control_mem);
   fd_device_del_locked(pipe->dev);
   pipe->funcs->destroy(pipe);
}

/**
 * Discard any unflushed deferred submits.  This is called at context-
 * destroy to make sure we don't leak unflushed submits.
 */
void
fd_pipe_purge(struct fd_pipe *pipe)
{
   struct fd_device *dev = pipe->dev;
   struct list_head deferred_submits;

   list_inithead(&deferred_submits);

   simple_mtx_lock(&dev->submit_lock);

   foreach_submit_safe (deferred_submit, &dev->deferred_submits) {
      if (deferred_submit->pipe != pipe)
         continue;

      list_del(&deferred_submit->node);
      list_addtail(&deferred_submit->node, &deferred_submits);
      dev->deferred_cmds -= fd_ringbuffer_cmd_count(deferred_submit->primary);
   }

   simple_mtx_unlock(&dev->submit_lock);

   foreach_submit_safe (deferred_submit, &deferred_submits) {
      list_del(&deferred_submit->node);
      fd_submit_del(deferred_submit);
   }
}

int
fd_pipe_get_param(struct fd_pipe *pipe, enum fd_param_id param, uint64_t *value)
{
   return pipe->funcs->get_param(pipe, param, value);
}

const struct fd_dev_id *
fd_pipe_dev_id(struct fd_pipe *pipe)
{
   return &pipe->dev_id;
}

int
fd_pipe_wait(struct fd_pipe *pipe, const struct fd_fence *fence)
{
   return fd_pipe_wait_timeout(pipe, fence, ~0);
}

int
fd_pipe_wait_timeout(struct fd_pipe *pipe, const struct fd_fence *fence,
                     uint64_t timeout)
{
   if (!fd_fence_after(fence->ufence, pipe->control->fence))
      return 0;

   fd_pipe_flush(pipe, fence->ufence);

   return pipe->funcs->wait(pipe, fence, timeout);
}

uint32_t
fd_pipe_emit_fence(struct fd_pipe *pipe, struct fd_ringbuffer *ring)
{
   uint32_t fence = ++pipe->last_fence;

   if (fd_dev_64b(&pipe->dev_id)) {
      OUT_PKT7(ring, CP_EVENT_WRITE, 4);
      OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(CACHE_FLUSH_TS));
      OUT_RELOC(ring, control_ptr(pipe, fence));   /* ADDR_LO/HI */
      OUT_RING(ring, fence);
   } else {
      OUT_PKT3(ring, CP_EVENT_WRITE, 3);
      OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(CACHE_FLUSH_TS));
      OUT_RELOC(ring, control_ptr(pipe, fence));   /* ADDR */
      OUT_RING(ring, fence);
   }

   return fence;
}
