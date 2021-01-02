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

#ifndef D3D12_RESOURCE_H
#define D3D12_RESOURCE_H

struct pipe_screen;
#include "d3d12_bufmgr.h"
#include "util/u_range.h"
#include "util/u_transfer.h"

#include <directx/d3d12.h>

struct d3d12_resource {
   struct pipe_resource base;
   struct d3d12_bo *bo;
   DXGI_FORMAT dxgi_format;
   unsigned mip_levels;
   struct sw_displaytarget *dt;
   unsigned dt_stride;
   struct util_range valid_buffer_range;
};

struct d3d12_transfer {
   struct pipe_transfer base;
   struct pipe_resource *staging_res;
   void *data;
};

static inline struct d3d12_resource *
d3d12_resource(struct pipe_resource *r)
{
   return (struct d3d12_resource *)r;
}

/* Returns the underlying ID3D12Resource and offset for this resource */
static inline ID3D12Resource *
d3d12_resource_underlying(struct d3d12_resource *res, uint64_t *offset)
{
   if (!res->bo)
      return NULL;

   return d3d12_bo_get_base(res->bo, offset)->res;
}

/* Returns the underlying ID3D12Resource for this resource. */
static inline ID3D12Resource *
d3d12_resource_resource(struct d3d12_resource *res)
{
   ID3D12Resource *ret;
   uint64_t offset;
   ret = d3d12_resource_underlying(res, &offset);
   return ret;
}

static inline struct TransitionableResourceState *
d3d12_resource_state(struct d3d12_resource *res)
{
   uint64_t offset;
   if (!res->bo)
      return NULL;
   return d3d12_bo_get_base(res->bo, &offset)->trans_state;
}

static inline D3D12_GPU_VIRTUAL_ADDRESS
d3d12_resource_gpu_virtual_address(struct d3d12_resource *res)
{
   uint64_t offset;
   ID3D12Resource *base_res = d3d12_resource_underlying(res, &offset);
   return base_res->GetGPUVirtualAddress() + offset;
}

static inline bool
d3d12_subresource_id_uses_layer(enum pipe_texture_target target)
{
   return target == PIPE_TEXTURE_CUBE ||
          target == PIPE_TEXTURE_1D_ARRAY ||
          target == PIPE_TEXTURE_2D_ARRAY;
}

void
d3d12_resource_release(struct d3d12_resource *res);

void
d3d12_resource_wait_idle(struct d3d12_context *ctx,
                         struct d3d12_resource *res);

void
d3d12_resource_make_writeable(struct pipe_context *pctx,
                              struct pipe_resource *pres);

void
d3d12_screen_resource_init(struct pipe_screen *pscreen);

void
d3d12_context_resource_init(struct pipe_context *pctx);

#endif
