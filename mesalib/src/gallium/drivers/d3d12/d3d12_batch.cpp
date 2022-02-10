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

#include "d3d12_batch.h"
#include "d3d12_context.h"
#include "d3d12_fence.h"
#include "d3d12_query.h"
#include "d3d12_resource.h"
#include "d3d12_screen.h"
#include "d3d12_surface.h"

#include "util/hash_table.h"
#include "util/set.h"
#include "util/u_inlines.h"

#include <dxguids/dxguids.h>

bool
d3d12_init_batch(struct d3d12_context *ctx, struct d3d12_batch *batch)
{
   struct d3d12_screen *screen = d3d12_screen(ctx->base.screen);

   batch->bos = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                        _mesa_key_pointer_equal);
   batch->sampler_views = _mesa_set_create(NULL, _mesa_hash_pointer,
                                           _mesa_key_pointer_equal);
   batch->surfaces = _mesa_set_create(NULL, _mesa_hash_pointer,
                                      _mesa_key_pointer_equal);
   batch->objects = _mesa_set_create(NULL,
                                     _mesa_hash_pointer,
                                     _mesa_key_pointer_equal);

   if (!batch->bos || !batch->sampler_views || !batch->surfaces || !batch->objects)
      return false;

   util_dynarray_init(&batch->zombie_samplers, NULL);

   if (FAILED(screen->dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&batch->cmdalloc))))
      return false;


   batch->sampler_heap =
      d3d12_descriptor_heap_new(screen->dev,
                                D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                                128);

   batch->view_heap =
      d3d12_descriptor_heap_new(screen->dev,
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
                                1024);

   if (!batch->sampler_heap && !batch->view_heap)
      return false;

   return true;
}

static void
delete_bo(hash_entry *entry)
{
   struct d3d12_bo *bo = (struct d3d12_bo *)entry->key;
   d3d12_bo_unreference(bo);
}

static void
delete_sampler_view(set_entry *entry)
{
   struct pipe_sampler_view *pres = (struct pipe_sampler_view *)entry->key;
   pipe_sampler_view_reference(&pres, NULL);
}

static void
delete_surface(set_entry *entry)
{
   struct pipe_surface *surf = (struct pipe_surface *)entry->key;
   pipe_surface_reference(&surf, NULL);
}

static void
delete_object(set_entry *entry)
{
   ID3D12Object *object = (ID3D12Object *)entry->key;
   object->Release();
}

bool
d3d12_reset_batch(struct d3d12_context *ctx, struct d3d12_batch *batch, uint64_t timeout_ns)
{
   // batch hasn't been submitted before
   if (!batch->fence && !batch->has_errors)
      return true;

   if (batch->fence) {
      if (!d3d12_fence_finish(batch->fence, timeout_ns))
         return false;
      d3d12_fence_reference(&batch->fence, NULL);
   }

   _mesa_hash_table_clear(batch->bos, delete_bo);
   _mesa_set_clear(batch->sampler_views, delete_sampler_view);
   _mesa_set_clear(batch->surfaces, delete_surface);
   _mesa_set_clear(batch->objects, delete_object);

   util_dynarray_foreach(&batch->zombie_samplers, d3d12_descriptor_handle, handle)
      d3d12_descriptor_handle_free(handle);
   util_dynarray_clear(&batch->zombie_samplers);

   d3d12_descriptor_heap_clear(batch->view_heap);
   d3d12_descriptor_heap_clear(batch->sampler_heap);

   if (FAILED(batch->cmdalloc->Reset())) {
      debug_printf("D3D12: resetting ID3D12CommandAllocator failed\n");
      return false;
   }
   batch->has_errors = false;
   batch->pending_memory_barrier = false;
   return true;
}

void
d3d12_destroy_batch(struct d3d12_context *ctx, struct d3d12_batch *batch)
{
   d3d12_reset_batch(ctx, batch, PIPE_TIMEOUT_INFINITE);
   batch->cmdalloc->Release();
   d3d12_descriptor_heap_free(batch->sampler_heap);
   d3d12_descriptor_heap_free(batch->view_heap);
   _mesa_hash_table_destroy(batch->bos, NULL);
   _mesa_set_destroy(batch->sampler_views, NULL);
   _mesa_set_destroy(batch->surfaces, NULL);
   _mesa_set_destroy(batch->objects, NULL);
   util_dynarray_fini(&batch->zombie_samplers);
}

void
d3d12_start_batch(struct d3d12_context *ctx, struct d3d12_batch *batch)
{
   struct d3d12_screen *screen = d3d12_screen(ctx->base.screen);
   ID3D12DescriptorHeap* heaps[2] = { d3d12_descriptor_heap_get(batch->view_heap),
                                      d3d12_descriptor_heap_get(batch->sampler_heap) };

   d3d12_reset_batch(ctx, batch, PIPE_TIMEOUT_INFINITE);

   /* Create or reset global command list */
   if (ctx->cmdlist) {
      if (FAILED(ctx->cmdlist->Reset(batch->cmdalloc, NULL))) {
         debug_printf("D3D12: resetting ID3D12GraphicsCommandList failed\n");
         batch->has_errors = true;
         return;
      }
   } else {
      if (FAILED(screen->dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                batch->cmdalloc, NULL,
                                                IID_PPV_ARGS(&ctx->cmdlist)))) {
         debug_printf("D3D12: creating ID3D12GraphicsCommandList failed\n");
         batch->has_errors = true;
         return;
      }
   }

   ctx->cmdlist->SetDescriptorHeaps(2, heaps);
   ctx->cmdlist_dirty = ~0;
   for (int i = 0; i < PIPE_SHADER_TYPES; ++i)
      ctx->shader_dirty[i] = ~0;

   if (!ctx->queries_disabled)
      d3d12_resume_queries(ctx);
   if (ctx->current_predication)
      d3d12_enable_predication(ctx);
}

void
d3d12_end_batch(struct d3d12_context *ctx, struct d3d12_batch *batch)
{
   struct d3d12_screen *screen = d3d12_screen(ctx->base.screen);

   if (!ctx->queries_disabled)
      d3d12_suspend_queries(ctx);

   if (FAILED(ctx->cmdlist->Close())) {
      debug_printf("D3D12: closing ID3D12GraphicsCommandList failed\n");
      batch->has_errors = true;
      return;
   }

   ID3D12CommandList* cmdlists[] = { ctx->cmdlist };
   screen->cmdqueue->ExecuteCommandLists(1, cmdlists);
   batch->fence = d3d12_create_fence(screen, ctx);
}

enum batch_bo_reference_state
{
   batch_bo_reference_read = (1 << 0),
   batch_bo_reference_written = (1 << 1),
};

bool
d3d12_batch_has_references(struct d3d12_batch *batch,
                           struct d3d12_bo *bo,
                           bool want_to_write)
{
   hash_entry *entry = _mesa_hash_table_search(batch->bos, bo);
   if (entry == NULL)
      return false;
   bool resource_was_written = ((batch_bo_reference_state)(size_t)entry->data & batch_bo_reference_written) != 0;
   return want_to_write || resource_was_written;
}

void
d3d12_batch_reference_resource(struct d3d12_batch *batch,
                               struct d3d12_resource *res,
                               bool write)
{
   hash_entry *entry = _mesa_hash_table_search(batch->bos, res->bo);
   if (entry == NULL) {
      d3d12_bo_reference(res->bo);
      entry = _mesa_hash_table_insert(batch->bos, res->bo, NULL);
   }
   size_t new_data = write ? batch_bo_reference_written : batch_bo_reference_read;
   size_t old_data = (size_t)entry->data;
   entry->data = (void*)(old_data | new_data);
}

void
d3d12_batch_reference_sampler_view(struct d3d12_batch *batch,
                                   struct d3d12_sampler_view *sv)
{
   struct set_entry *entry = _mesa_set_search(batch->sampler_views, sv);
   if (!entry) {
      entry = _mesa_set_add(batch->sampler_views, sv);
      pipe_reference(NULL, &sv->base.reference);
   }
}

void
d3d12_batch_reference_surface_texture(struct d3d12_batch *batch,
                                      struct d3d12_surface *surf)
{
   d3d12_batch_reference_resource(batch, d3d12_resource(surf->base.texture), true);
}

void
d3d12_batch_reference_object(struct d3d12_batch *batch,
                             ID3D12Object *object)
{
   struct set_entry *entry = _mesa_set_search(batch->objects, object);
   if (!entry) {
      entry = _mesa_set_add(batch->objects, object);
      object->AddRef();
   }
}
