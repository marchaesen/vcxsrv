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
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 ***************************************************************************/

#ifndef SWR_FENCE_WORK_H
#define SWR_FENCE_WORK_H

typedef void(*SWR_WORK_CALLBACK_FUNC)(struct swr_fence_work *work);

struct swr_fence_work {
   SWR_WORK_CALLBACK_FUNC callback;

   union {
      void *data;
      struct swr_vertex_shader *swr_vs;
      struct swr_fragment_shader *swr_fs;
      struct swr_geometry_shader *swr_gs;
      struct swr_tess_control_shader *swr_tcs;
      struct swr_tess_evaluation_shader *swr_tes;
   } free;

   struct swr_fence_work *next;
};

void swr_fence_do_work(struct swr_fence *fence);

bool swr_fence_work_free(struct pipe_fence_handle *fence, void *data,
                         bool aligned_free = false);
bool swr_fence_work_delete_vs(struct pipe_fence_handle *fence,
                              struct swr_vertex_shader *swr_vs);
bool swr_fence_work_delete_fs(struct pipe_fence_handle *fence,
                              struct swr_fragment_shader *swr_vs);
bool swr_fence_work_delete_gs(struct pipe_fence_handle *fence,
                              struct swr_geometry_shader *swr_gs);
bool swr_fence_work_delete_tcs(struct pipe_fence_handle *fence,
                               struct swr_tess_control_shader *swr_tcs);
bool swr_fence_work_delete_tes(struct pipe_fence_handle *fence,
                               struct swr_tess_evaluation_shader *swr_tes);
#endif
