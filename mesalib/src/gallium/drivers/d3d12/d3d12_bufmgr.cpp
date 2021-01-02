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

#include "d3d12_bufmgr.h"
#include "d3d12_format.h"
#include "d3d12_screen.h"

#include "D3D12ResourceState.h"

#include "pipebuffer/pb_buffer.h"
#include "pipebuffer/pb_bufmgr.h"

#include "util/format/u_format.h"
#include "util/u_memory.h"

#include <directx/d3d12.h>
#include <dxguids/dxguids.h>

struct d3d12_bufmgr {
   struct pb_manager base;

   ID3D12Device *dev;
};

extern const struct pb_vtbl d3d12_buffer_vtbl;

static inline struct d3d12_bufmgr *
d3d12_bufmgr(struct pb_manager *mgr)
{
   assert(mgr);

   return (struct d3d12_bufmgr *)mgr;
}

static struct TransitionableResourceState *
create_trans_state(ID3D12Resource *res, enum pipe_format format)
{
   D3D12_RESOURCE_DESC desc = res->GetDesc();

   // Calculate the total number of subresources
   unsigned arraySize = desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ?
                        1 : desc.DepthOrArraySize;
   unsigned total_subresources = desc.MipLevels *
                                 arraySize *
                                 d3d12_non_opaque_plane_count(desc.Format);
   total_subresources *= util_format_has_stencil(util_format_description(format)) ?
                         2 : 1;

   return new TransitionableResourceState(res,
                                          total_subresources,
                                          SupportsSimultaneousAccess(desc));
}

struct d3d12_bo *
d3d12_bo_wrap_res(ID3D12Resource *res, enum pipe_format format)
{
   struct d3d12_bo *bo;

   bo = CALLOC_STRUCT(d3d12_bo);
   if (!bo)
      return NULL;

   bo->refcount = 1;
   bo->res = res;
   bo->trans_state = create_trans_state(res, format);

   return bo;
}

struct d3d12_bo *
d3d12_bo_new(ID3D12Device *dev, uint64_t size, uint64_t alignment)
{
   ID3D12Resource *res;

   D3D12_RESOURCE_DESC res_desc;
   res_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
   res_desc.Format = DXGI_FORMAT_UNKNOWN;
   res_desc.Alignment = alignment;
   res_desc.Width = size;
   res_desc.Height = 1;
   res_desc.DepthOrArraySize = 1;
   res_desc.MipLevels = 1;
   res_desc.SampleDesc.Count = 1;
   res_desc.SampleDesc.Quality = 0;
   res_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
   res_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

   D3D12_HEAP_PROPERTIES heap_pris = dev->GetCustomHeapProperties(0, D3D12_HEAP_TYPE_UPLOAD);
   HRESULT hres = dev->CreateCommittedResource(&heap_pris,
                                               D3D12_HEAP_FLAG_NONE,
                                               &res_desc,
                                               D3D12_RESOURCE_STATE_COMMON,
                                               NULL,
                                               IID_PPV_ARGS(&res));

   if (FAILED(hres))
      return NULL;

   return d3d12_bo_wrap_res(res, PIPE_FORMAT_NONE);
}

struct d3d12_bo *
d3d12_bo_wrap_buffer(struct pb_buffer *buf)
{
   struct d3d12_bo *bo;

   bo = CALLOC_STRUCT(d3d12_bo);
   if (!bo)
      return NULL;

   bo->refcount = 1;
   bo->buffer = buf;
   bo->trans_state = NULL; /* State from base BO will be used */

   return bo;
}

void
d3d12_bo_unreference(struct d3d12_bo *bo)
{
   if (bo == NULL)
      return;

   assert(p_atomic_read(&bo->refcount) > 0);

   if (p_atomic_dec_zero(&bo->refcount)) {
      if (bo->buffer) {
         pb_reference(&bo->buffer, NULL);
      } else {
         delete bo->trans_state;
         bo->res->Release();
      }
      FREE(bo);
   }
}

void *
d3d12_bo_map(struct d3d12_bo *bo, D3D12_RANGE *range)
{
   struct d3d12_bo *base_bo;
   D3D12_RANGE offset_range = {0, 0};
   uint64_t offset;
   void *ptr;

   base_bo = d3d12_bo_get_base(bo, &offset);

   if (!range || offset == 0) {
      /* Nothing to do */
   } else if (range->Begin >= range->End) {
      offset_range.Begin = offset;
      offset_range.End = offset + d3d12_bo_get_size(bo);
      range = &offset_range;
   } else {
      offset_range.Begin = range->Begin + offset;
      offset_range.End = range->End + offset;
      range = &offset_range;
   }

   if (FAILED(base_bo->res->Map(0, range, &ptr)))
      return NULL;

   return (uint8_t *)ptr + (range ? range->Begin : 0);
}

void
d3d12_bo_unmap(struct d3d12_bo *bo, D3D12_RANGE *range)
{
   struct d3d12_bo *base_bo;
   D3D12_RANGE offset_range = {0, 0};
   uint64_t offset;

   base_bo = d3d12_bo_get_base(bo, &offset);

   if (!range || bo == base_bo)
   {
      /* Nothing to do */
   } else if (range->Begin >= range->End) {
      offset_range.Begin = offset;
      offset_range.End = offset + base_bo->res->GetDesc().Width;
   } else {
      offset_range.Begin = range->Begin + offset;
      offset_range.End = range->End + offset;
   }

   base_bo->res->Unmap(0, range);
}

static void
d3d12_buffer_destroy(struct pb_buffer *pbuf)
{
   struct d3d12_buffer *buf = d3d12_buffer(pbuf);

   d3d12_bo_unmap(buf->bo, &buf->range);
   d3d12_bo_unreference(buf->bo);
   FREE(buf);
}

static void *
d3d12_buffer_map(struct pb_buffer *pbuf,
                 enum pb_usage_flags flags,
                 void *flush_ctx)
{
   return d3d12_buffer(pbuf)->map;
}

static void
d3d12_buffer_unmap(struct pb_buffer *pbuf)
{
}

static void
d3d12_buffer_get_base_buffer(struct pb_buffer *buf,
                             struct pb_buffer **base_buf,
                             pb_size *offset)
{
   *base_buf = buf;
   *offset = 0;
}

static enum pipe_error
d3d12_buffer_validate(struct pb_buffer *pbuf,
                      struct pb_validate *vl,
                      enum pb_usage_flags flags )
{
   /* Always pinned */
   return PIPE_OK;
}

static void
d3d12_buffer_fence(struct pb_buffer *pbuf,
                   struct pipe_fence_handle *fence )
{
}

const struct pb_vtbl d3d12_buffer_vtbl = {
   d3d12_buffer_destroy,
   d3d12_buffer_map,
   d3d12_buffer_unmap,
   d3d12_buffer_validate,
   d3d12_buffer_fence,
   d3d12_buffer_get_base_buffer
};

static struct pb_buffer *
d3d12_bufmgr_create_buffer(struct pb_manager *pmgr,
                           pb_size size,
                           const struct pb_desc *pb_desc)
{
   struct d3d12_bufmgr *mgr = d3d12_bufmgr(pmgr);
   struct d3d12_buffer *buf;

   buf = CALLOC_STRUCT(d3d12_buffer);
   if (!buf)
      return NULL;

   // Align the buffer to D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT
   // in case it is to be used as a CBV.
   size = align64(size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

   pipe_reference_init(&buf->base.reference, 1);
   buf->base.alignment = pb_desc->alignment;
   buf->base.usage = pb_desc->usage;
   buf->base.vtbl = &d3d12_buffer_vtbl;
   buf->base.size = size;
   buf->range.Begin = 0;
   buf->range.End = size;

   buf->bo = d3d12_bo_new(mgr->dev, size, pb_desc->alignment);
   if (!buf->bo) {
      FREE(buf);
      return NULL;
   }

   buf->map = d3d12_bo_map(buf->bo, &buf->range);
   if (!buf->map) {
      d3d12_bo_unreference(buf->bo);
      FREE(buf);
      return NULL;
   }

   return &buf->base;
}

static void
d3d12_bufmgr_flush(struct pb_manager *mgr)
{
   /* No-op */
}

static void
d3d12_bufmgr_destroy(struct pb_manager *_mgr)
{
   struct d3d12_bufmgr *mgr = d3d12_bufmgr(_mgr);
   FREE(mgr);
}

struct pb_manager *
d3d12_bufmgr_create(struct d3d12_screen *screen)
{
   struct d3d12_bufmgr *mgr;

   mgr = CALLOC_STRUCT(d3d12_bufmgr);
   if (!mgr)
      return NULL;

   mgr->base.destroy = d3d12_bufmgr_destroy;
   mgr->base.create_buffer = d3d12_bufmgr_create_buffer;
   mgr->base.flush = d3d12_bufmgr_flush;

   mgr->dev = screen->dev;

   return &mgr->base;
}
