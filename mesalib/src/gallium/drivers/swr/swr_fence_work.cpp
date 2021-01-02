/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

#include "swr_context.h"
#include "swr_fence.h"

#include "util/u_inlines.h"
#include "util/u_memory.h"

/*
 * Called by swr_fence_cb to complete the work queue
 */
void
swr_fence_do_work(struct swr_fence *fence)
{
   struct swr_fence_work *work, *tmp;

   if (fence->work.head.next) {
      work = fence->work.head.next;
      /* Immediately clear the head so any new work gets added to a new work
       * queue */
      p_atomic_set(&fence->work.head.next, 0);
      p_atomic_set(&fence->work.tail, &fence->work.head);
      p_atomic_set(&fence->work.count, 0);

      do {
         tmp = work->next;
         work->callback(work);
         FREE(work);
         work = tmp;
      } while(work);
   }
}


/*
 * Called by one of the specialized work routines below
 */
static inline void
swr_add_fence_work(struct pipe_fence_handle *fh,
                   struct swr_fence_work *work)
{
   /* If no fence, just do the work now */
   if (!fh) {
      work->callback(work);
      FREE(work);
      return;
   }

   struct swr_fence *fence  = swr_fence(fh);
   p_atomic_set(&fence->work.tail->next, work);
   p_atomic_set(&fence->work.tail, work);
   p_atomic_inc(&fence->work.count);
}


/*
 * Generic free/free_aligned, and delete vs/fs
 */
template<bool aligned_free>
static void
swr_free_cb(struct swr_fence_work *work)
{
   if (aligned_free)
      AlignedFree(work->free.data);
   else
      FREE(work->free.data);
}

static void
swr_delete_vs_cb(struct swr_fence_work *work)
{
   delete work->free.swr_vs;
}

static void
swr_delete_fs_cb(struct swr_fence_work *work)
{
   delete work->free.swr_fs;
}

static void
swr_delete_gs_cb(struct swr_fence_work *work)
{
   delete work->free.swr_gs;
}

static void
swr_delete_tcs_cb(struct swr_fence_work *work)
{
   delete work->free.swr_tcs;
}

static void
swr_delete_tes_cb(struct swr_fence_work *work)
{
   delete work->free.swr_tes;
}


bool
swr_fence_work_free(struct pipe_fence_handle *fence, void *data,
                    bool aligned_free)
{
   struct swr_fence_work *work = CALLOC_STRUCT(swr_fence_work);
   if (!work)
      return false;
   if (aligned_free)
      work->callback = swr_free_cb<true>;
   else
      work->callback = swr_free_cb<false>;
   work->free.data = data;

   swr_add_fence_work(fence, work);

   return true;
}

bool
swr_fence_work_delete_vs(struct pipe_fence_handle *fence,
                         struct swr_vertex_shader *swr_vs)
{
   struct swr_fence_work *work = CALLOC_STRUCT(swr_fence_work);
   if (!work)
      return false;
   work->callback = swr_delete_vs_cb;
   work->free.swr_vs = swr_vs;

   swr_add_fence_work(fence, work);

   return true;
}

bool
swr_fence_work_delete_fs(struct pipe_fence_handle *fence,
                         struct swr_fragment_shader *swr_fs)
{
   struct swr_fence_work *work = CALLOC_STRUCT(swr_fence_work);
   if (!work)
      return false;
   work->callback = swr_delete_fs_cb;
   work->free.swr_fs = swr_fs;

   swr_add_fence_work(fence, work);

   return true;
}

bool
swr_fence_work_delete_gs(struct pipe_fence_handle *fence,
                         struct swr_geometry_shader *swr_gs)
{
   struct swr_fence_work *work = CALLOC_STRUCT(swr_fence_work);
   if (!work)
      return false;
   work->callback = swr_delete_gs_cb;
   work->free.swr_gs = swr_gs;

   swr_add_fence_work(fence, work);

   return true;
}

bool
swr_fence_work_delete_tcs(struct pipe_fence_handle *fence,
                          struct swr_tess_control_shader *swr_tcs)
{
   struct swr_fence_work *work = CALLOC_STRUCT(swr_fence_work);
   if (!work)
      return false;
   work->callback = swr_delete_tcs_cb;
   work->free.swr_tcs = swr_tcs;

   swr_add_fence_work(fence, work);

   return true;
}


bool
swr_fence_work_delete_tes(struct pipe_fence_handle *fence,
                          struct swr_tess_evaluation_shader *swr_tes)
{
   struct swr_fence_work *work = CALLOC_STRUCT(swr_fence_work);
   if (!work)
      return false;
   work->callback = swr_delete_tes_cb;
   work->free.swr_tes = swr_tes;

   swr_add_fence_work(fence, work);

   return true;
}