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
#include "zink_fence.h"

#include "zink_query.h"
#include "zink_resource.h"
#include "zink_screen.h"

#include "util/set.h"
#include "util/u_memory.h"

static void
destroy_fence(struct zink_screen *screen, struct zink_fence *fence)
{
   if (fence->fence)
      vkDestroyFence(screen->dev, fence->fence, NULL);
   util_dynarray_fini(&fence->resources);
   FREE(fence);
}

struct zink_fence *
zink_create_fence(struct pipe_screen *pscreen, struct zink_batch *batch)
{
   struct zink_screen *screen = zink_screen(pscreen);

   VkFenceCreateInfo fci = {};
   fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

   struct zink_fence *ret = CALLOC_STRUCT(zink_fence);
   if (!ret) {
      debug_printf("CALLOC_STRUCT failed\n");
      return NULL;
   }

   if (vkCreateFence(screen->dev, &fci, NULL, &ret->fence) != VK_SUCCESS) {
      debug_printf("vkCreateFence failed\n");
      goto fail;
   }
   ret->active_queries = batch->active_queries;
   batch->active_queries = NULL;

   ret->batch_id = batch->batch_id;
   util_dynarray_init(&ret->resources, NULL);
   set_foreach(batch->resources, entry) {
      /* the fence needs its own reference to ensure it can safely access lifetime-dependent
       * resource members
       */
      struct pipe_resource *r = NULL, *pres = (struct pipe_resource *)entry->key;
      pipe_resource_reference(&r, pres);
      util_dynarray_append(&ret->resources, struct pipe_resource*, pres);
   }

   pipe_reference_init(&ret->reference, 1);
   return ret;

fail:
   destroy_fence(screen, ret);
   return NULL;
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

static inline void
fence_remove_resource_access(struct zink_fence *fence, struct zink_resource *res)
{
   p_atomic_set(&res->batch_uses[fence->batch_id], 0);
}

bool
zink_fence_finish(struct zink_screen *screen, struct zink_fence *fence,
                  uint64_t timeout_ns)
{
   bool success = vkWaitForFences(screen->dev, 1, &fence->fence, VK_TRUE,
                                  timeout_ns) == VK_SUCCESS;
   if (success) {
      if (fence->active_queries)
         zink_prune_queries(screen, fence);

      /* unref all used resources */
      util_dynarray_foreach(&fence->resources, struct pipe_resource*, pres) {
         struct zink_resource *stencil, *res = zink_resource(*pres);
         fence_remove_resource_access(fence, res);

         /* we still hold a ref, so this doesn't need to be atomic */
         zink_get_depth_stencil_resources((struct pipe_resource*)res, NULL, &stencil);
         if (stencil)
            fence_remove_resource_access(fence, stencil);
         pipe_resource_reference(pres, NULL);
      }
      util_dynarray_clear(&fence->resources);
   }
   return success;
}

static bool
fence_finish(struct pipe_screen *pscreen, struct pipe_context *pctx,
                  struct pipe_fence_handle *pfence, uint64_t timeout_ns)
{
   return zink_fence_finish(zink_screen(pscreen), zink_fence(pfence),
                            timeout_ns);
}

void
zink_screen_fence_init(struct pipe_screen *pscreen)
{
   pscreen->fence_reference = fence_reference;
   pscreen->fence_finish = fence_finish;
}
