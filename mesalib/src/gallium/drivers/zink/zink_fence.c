/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_batch.h"
#include "zink_context.h"
#include "zink_fence.h"

#include "zink_resource.h"
#include "zink_screen.h"

#include "util/set.h"
#include "util/u_memory.h"


void
zink_fence_clear_resources(struct zink_screen *screen, struct zink_fence *fence)
{
   /* unref all used resources */
   set_foreach(fence->resources, entry) {
      struct zink_resource_object *obj = (struct zink_resource_object *)entry->key;
      zink_batch_usage_unset(&obj->reads, fence->batch_id);
      zink_batch_usage_unset(&obj->writes, fence->batch_id);
      zink_resource_object_reference(screen, &obj, NULL);
      _mesa_set_remove(fence->resources, entry);
   }
}

static void
destroy_fence(struct zink_screen *screen, struct zink_fence *fence)
{
   if (fence->fence)
      vkDestroyFence(screen->dev, fence->fence, NULL);
   zink_batch_state_destroy(screen, zink_batch_state(fence));
}

bool
zink_create_fence(struct zink_screen *screen, struct zink_batch_state *bs)
{
   struct zink_fence *fence = zink_fence(bs);

   VkFenceCreateInfo fci = {};
   fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

   if (vkCreateFence(screen->dev, &fci, NULL, &fence->fence) != VK_SUCCESS) {
      debug_printf("vkCreateFence failed\n");
      goto fail;
   }

   pipe_reference_init(&fence->reference, 1);
   return true;
fail:
   destroy_fence(screen, fence);
   return false;
}

void
zink_fence_init(struct zink_context *ctx, struct zink_batch *batch)
{
   struct zink_fence *fence = zink_fence(batch->state);

   vkResetFences(zink_screen(ctx->base.screen)->dev, 1, &fence->fence);
   fence->deferred_ctx = NULL;
   fence->submitted = true;
}

void
zink_fence_reference(struct zink_screen *screen,
                     struct zink_fence **ptr,
                     struct zink_fence *fence)
{
   if (pipe_reference(&(*ptr)->reference, &fence->reference))
      destroy_fence(screen, *ptr);

   *ptr = fence;
}

static void
fence_reference(struct pipe_screen *pscreen,
                struct pipe_fence_handle **pptr,
                struct pipe_fence_handle *pfence)
{
   zink_fence_reference(zink_screen(pscreen), (struct zink_fence **)pptr,
                        zink_fence(pfence));
}

bool
zink_fence_finish(struct zink_screen *screen, struct pipe_context *pctx, struct zink_fence *fence,
                  uint64_t timeout_ns)
{
   if (pctx && fence->deferred_ctx == pctx) {
      zink_context(pctx)->batch.has_work = true;
      /* this must be the current batch */
      pctx->flush(pctx, NULL, 0);
   }

   if (!fence->submitted)
      return true;
   bool success;

   if (timeout_ns)
      success = vkWaitForFences(screen->dev, 1, &fence->fence, VK_TRUE, timeout_ns) == VK_SUCCESS;
   else
      success = vkGetFenceStatus(screen->dev, fence->fence) == VK_SUCCESS;

   if (success) {
      zink_fence_clear_resources(screen, fence);
      p_atomic_set(&fence->submitted, false);
   }
   return success;
}

static bool
fence_finish(struct pipe_screen *pscreen, struct pipe_context *pctx,
                  struct pipe_fence_handle *pfence, uint64_t timeout_ns)
{
   return zink_fence_finish(zink_screen(pscreen), pctx, zink_fence(pfence),
                            timeout_ns);
}

void
zink_fence_server_sync(struct pipe_context *pctx, struct pipe_fence_handle *pfence)
{
   struct zink_fence *fence = zink_fence(pfence);

   if (pctx && fence->deferred_ctx == pctx)
      return;

   if (fence->deferred_ctx) {
      zink_context(pctx)->batch.has_work = true;
      /* this must be the current batch */
      pctx->flush(pctx, NULL, 0);
   }
   zink_fence_finish(zink_screen(pctx->screen), pctx, fence, PIPE_TIMEOUT_INFINITE);
}

void
zink_screen_fence_init(struct pipe_screen *pscreen)
{
   pscreen->fence_reference = fence_reference;
   pscreen->fence_finish = fence_finish;
}
