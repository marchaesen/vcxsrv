/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef D3D12_BUFMGR_H
#define D3D12_BUFMGR_H

#include "pipebuffer/pb_buffer.h"
#include "util/u_atomic.h"

#ifndef _WIN32
#include <wsl/winadapter.h>
#endif

#include <directx/d3d12.h>

struct d3d12_bufmgr;
struct d3d12_screen;
struct pb_manager;
struct TransitionableResourceState;

struct d3d12_bo {
   int refcount;
   ID3D12Resource *res;
   struct pb_buffer *buffer;
   struct TransitionableResourceState *trans_state;
};

struct d3d12_buffer {
   struct pb_buffer base;

   struct d3d12_bo *bo;
   D3D12_RANGE range;
   void *map;
};

static inline struct d3d12_buffer *
d3d12_buffer(struct pb_buffer *buf)
{
   assert(buf);
   return (struct d3d12_buffer *)buf;
}

static inline struct d3d12_bo *
d3d12_bo_get_base(struct d3d12_bo *bo, uint64_t *offset)
{
   if (bo->buffer) {
      struct pb_buffer *base_buffer;
      pb_get_base_buffer(bo->buffer, &base_buffer, offset);
      return d3d12_buffer(base_buffer)->bo;
   } else {
      *offset = 0;
      return bo;
   }
}

static inline uint64_t
d3d12_bo_get_size(struct d3d12_bo *bo)
{
   if (bo->buffer)
      return bo->buffer->size;
   else
      return bo->res->GetDesc().Width;
}

static inline bool
d3d12_bo_is_suballocated(struct d3d12_bo *bo)
{
   struct d3d12_bo *base_bo;
   uint64_t offset;

   if (!bo->buffer)
      return false;

   base_bo = d3d12_bo_get_base(bo, &offset);
   return d3d12_bo_get_size(base_bo) != d3d12_bo_get_size(bo);
}

struct d3d12_bo *
d3d12_bo_new(ID3D12Device *dev, uint64_t size, uint64_t alignment);

struct d3d12_bo *
d3d12_bo_wrap_res(ID3D12Resource *res, enum pipe_format format);

struct d3d12_bo *
d3d12_bo_wrap_buffer(struct pb_buffer *buf);

static inline void
d3d12_bo_reference(struct d3d12_bo *bo)
{
   p_atomic_inc(&bo->refcount);
}

void
d3d12_bo_unreference(struct d3d12_bo *bo);

void *
d3d12_bo_map(struct d3d12_bo *bo, D3D12_RANGE *range);

void
d3d12_bo_unmap(struct d3d12_bo *bo, D3D12_RANGE *range);

struct pb_manager *
d3d12_bufmgr_create(struct d3d12_screen *screen);

#endif
