/*
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 * Copyright © 2021 Valve Corporation
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * Authors:
 *    Mike Blumenkrantz <michael.blumenkrantz@gmail.com>
 */

#include "zink_bo.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "util/u_hash_table.h"

struct zink_bo;

struct zink_sparse_backing_chunk {
   uint32_t begin, end;
};


/*
 * Sub-allocation information for a real buffer used as backing memory of a
 * sparse buffer.
 */
struct zink_sparse_backing {
   struct list_head list;

   struct zink_bo *bo;

   /* Sorted list of free chunks. */
   struct zink_sparse_backing_chunk *chunks;
   uint32_t max_chunks;
   uint32_t num_chunks;
};

struct zink_sparse_commitment {
   struct zink_sparse_backing *backing;
   uint32_t page;
};

struct zink_slab {
   struct pb_slab base;
   unsigned entry_size;
   struct zink_bo *buffer;
   struct zink_bo *entries;
};


ALWAYS_INLINE static struct zink_slab *
zink_slab(struct pb_slab *pslab)
{
   return (struct zink_slab*)pslab;
}

static struct pb_slabs *
get_slabs(struct zink_screen *screen, uint64_t size, enum zink_alloc_flag flags)
{
   //struct pb_slabs *bo_slabs = ((flags & RADEON_FLAG_ENCRYPTED) && screen->info.has_tmz_support) ?
      //screen->bo_slabs_encrypted : screen->bo_slabs;

   struct pb_slabs *bo_slabs = screen->pb.bo_slabs;
   /* Find the correct slab allocator for the given size. */
   for (unsigned i = 0; i < NUM_SLAB_ALLOCATORS; i++) {
      struct pb_slabs *slabs = &bo_slabs[i];

      if (size <= 1ULL << (slabs->min_order + slabs->num_orders - 1))
         return slabs;
   }

   assert(0);
   return NULL;
}

/* Return the power of two size of a slab entry matching the input size. */
static unsigned
get_slab_pot_entry_size(struct zink_screen *screen, unsigned size)
{
   unsigned entry_size = util_next_power_of_two(size);
   unsigned min_entry_size = 1 << screen->pb.bo_slabs[0].min_order;

   return MAX2(entry_size, min_entry_size);
}

/* Return the slab entry alignment. */
static unsigned get_slab_entry_alignment(struct zink_screen *screen, unsigned size)
{
   unsigned entry_size = get_slab_pot_entry_size(screen, size);

   if (size <= entry_size * 3 / 4)
      return entry_size / 4;

   return entry_size;
}

static void
bo_destroy(struct zink_screen *screen, struct pb_buffer *pbuf)
{
   struct zink_bo *bo = zink_bo(pbuf);

   simple_mtx_lock(&screen->pb.bo_export_table_lock);
   _mesa_hash_table_remove_key(screen->pb.bo_export_table, bo);
   simple_mtx_unlock(&screen->pb.bo_export_table_lock);

   if (!bo->u.real.is_user_ptr && bo->u.real.cpu_ptr) {
      bo->u.real.map_count = 1;
      bo->u.real.cpu_ptr = NULL;
      zink_bo_unmap(screen, bo);
   }

   VKSCR(FreeMemory)(screen->dev, bo->mem, NULL);

   simple_mtx_destroy(&bo->lock);
   FREE(bo);
}

static bool
bo_can_reclaim(struct zink_screen *screen, struct pb_buffer *pbuf)
{
   struct zink_bo *bo = zink_bo(pbuf);

   return zink_screen_usage_check_completion(screen, bo->reads) && zink_screen_usage_check_completion(screen, bo->writes);
}

static bool
bo_can_reclaim_slab(void *priv, struct pb_slab_entry *entry)
{
   struct zink_bo *bo = container_of(entry, struct zink_bo, u.slab.entry);

   return bo_can_reclaim(priv, &bo->base);
}

static void
bo_slab_free(struct zink_screen *screen, struct pb_slab *pslab)
{
   struct zink_slab *slab = zink_slab(pslab);
   ASSERTED unsigned slab_size = slab->buffer->base.size;

   assert(slab->base.num_entries * slab->entry_size <= slab_size);
   FREE(slab->entries);
   zink_bo_unref(screen, slab->buffer);
   FREE(slab);
}

static void
bo_slab_destroy(struct zink_screen *screen, struct pb_buffer *pbuf)
{
   struct zink_bo *bo = zink_bo(pbuf);

   assert(!bo->mem);

   //if (bo->base.usage & RADEON_FLAG_ENCRYPTED)
      //pb_slab_free(get_slabs(screen, bo->base.size, RADEON_FLAG_ENCRYPTED), &bo->u.slab.entry);
   //else
      pb_slab_free(get_slabs(screen, bo->base.size, 0), &bo->u.slab.entry);
}

static void
clean_up_buffer_managers(struct zink_screen *screen)
{
   for (unsigned i = 0; i < NUM_SLAB_ALLOCATORS; i++) {
      pb_slabs_reclaim(&screen->pb.bo_slabs[i]);
      //if (screen->info.has_tmz_support)
         //pb_slabs_reclaim(&screen->bo_slabs_encrypted[i]);
   }

   pb_cache_release_all_buffers(&screen->pb.bo_cache);
}

static unsigned
get_optimal_alignment(struct zink_screen *screen, uint64_t size, unsigned alignment)
{
   /* Increase the alignment for faster address translation and better memory
    * access pattern.
    */
   if (size >= 4096) {
      alignment = MAX2(alignment, 4096);
   } else if (size) {
      unsigned msb = util_last_bit(size);

      alignment = MAX2(alignment, 1u << (msb - 1));
   }
   return alignment;
}

static void
bo_destroy_or_cache(struct zink_screen *screen, struct pb_buffer *pbuf)
{
   struct zink_bo *bo = zink_bo(pbuf);

   assert(bo->mem); /* slab buffers have a separate vtbl */
   bo->reads = NULL;
   bo->writes = NULL;

   if (bo->u.real.use_reusable_pool)
      pb_cache_add_buffer(bo->cache_entry);
   else
      bo_destroy(screen, pbuf);
}

static const struct pb_vtbl bo_vtbl = {
   /* Cast to void* because one of the function parameters is a struct pointer instead of void*. */
   (void*)bo_destroy_or_cache
   /* other functions are never called */
};

static struct zink_bo *
bo_create_internal(struct zink_screen *screen,
                   uint64_t size,
                   unsigned alignment,
                   enum zink_heap heap,
                   unsigned flags,
                   const void *pNext)
{
   struct zink_bo *bo = NULL;
   bool init_pb_cache;

   /* too big for vk alloc */
   if (size > UINT32_MAX)
      return NULL;

   alignment = get_optimal_alignment(screen, size, alignment);

   VkMemoryAllocateInfo mai;
   mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mai.pNext = pNext;
   mai.allocationSize = size;
demote:
   mai.memoryTypeIndex = screen->heap_map[heap];
   if (screen->info.mem_props.memoryTypes[mai.memoryTypeIndex].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      alignment = MAX2(alignment, screen->info.props.limits.minMemoryMapAlignment);
      mai.allocationSize = align64(mai.allocationSize, screen->info.props.limits.minMemoryMapAlignment);
   }
   unsigned heap_idx = screen->info.mem_props.memoryTypes[screen->heap_map[heap]].heapIndex;
   if (mai.allocationSize > screen->info.mem_props.memoryHeaps[heap_idx].size) {
      mesa_loge("zink: can't allocate %"PRIu64" bytes from heap that's only %"PRIu64" bytes!\n", mai.allocationSize, screen->info.mem_props.memoryHeaps[heap_idx].size);
      return NULL;
   }

   /* all non-suballocated bo can cache */
   init_pb_cache = !pNext;

   if (!bo)
      bo = CALLOC(1, sizeof(struct zink_bo) + init_pb_cache * sizeof(struct pb_cache_entry));
   if (!bo) {
      return NULL;
   }

   VkResult ret = VKSCR(AllocateMemory)(screen->dev, &mai, NULL, &bo->mem);
   if (!zink_screen_handle_vkresult(screen, ret)) {
      if (heap == ZINK_HEAP_DEVICE_LOCAL_VISIBLE) {
         heap = ZINK_HEAP_DEVICE_LOCAL;
         mesa_loge("zink: %p couldn't allocate memory! from BAR heap: retrying as device-local", bo);
         goto demote;
      }
      mesa_loge("zink: couldn't allocate memory! from heap %u", heap);
      goto fail;
   }

   if (init_pb_cache) {
      bo->u.real.use_reusable_pool = true;
      pb_cache_init_entry(&screen->pb.bo_cache, bo->cache_entry, &bo->base, heap);
   }


   simple_mtx_init(&bo->lock, mtx_plain);
   pipe_reference_init(&bo->base.reference, 1);
   bo->base.alignment_log2 = util_logbase2(alignment);
   bo->base.size = mai.allocationSize;
   bo->base.vtbl = &bo_vtbl;
   bo->base.placement = vk_domain_from_heap(heap);
   bo->base.usage = flags;
   bo->unique_id = p_atomic_inc_return(&screen->pb.next_bo_unique_id);

   return bo;

fail:
   bo_destroy(screen, (void*)bo);
   return NULL;
}

/*
 * Attempt to allocate the given number of backing pages. Fewer pages may be
 * allocated (depending on the fragmentation of existing backing buffers),
 * which will be reflected by a change to *pnum_pages.
 */
static struct zink_sparse_backing *
sparse_backing_alloc(struct zink_screen *screen, struct zink_bo *bo,
                     uint32_t *pstart_page, uint32_t *pnum_pages)
{
   struct zink_sparse_backing *best_backing;
   unsigned best_idx;
   uint32_t best_num_pages;

   best_backing = NULL;
   best_idx = 0;
   best_num_pages = 0;

   /* This is a very simple and inefficient best-fit algorithm. */
   list_for_each_entry(struct zink_sparse_backing, backing, &bo->u.sparse.backing, list) {
      for (unsigned idx = 0; idx < backing->num_chunks; ++idx) {
         uint32_t cur_num_pages = backing->chunks[idx].end - backing->chunks[idx].begin;
         if ((best_num_pages < *pnum_pages && cur_num_pages > best_num_pages) ||
            (best_num_pages > *pnum_pages && cur_num_pages < best_num_pages)) {
            best_backing = backing;
            best_idx = idx;
            best_num_pages = cur_num_pages;
         }
      }
   }

   /* Allocate a new backing buffer if necessary. */
   if (!best_backing) {
      struct pb_buffer *buf;
      uint64_t size;
      uint32_t pages;

      best_backing = CALLOC_STRUCT(zink_sparse_backing);
      if (!best_backing)
         return NULL;

      best_backing->max_chunks = 4;
      best_backing->chunks = CALLOC(best_backing->max_chunks,
                                    sizeof(*best_backing->chunks));
      if (!best_backing->chunks) {
         FREE(best_backing);
         return NULL;
      }

      assert(bo->u.sparse.num_backing_pages < DIV_ROUND_UP(bo->base.size, ZINK_SPARSE_BUFFER_PAGE_SIZE));

      size = MIN3(bo->base.size / 16,
                  8 * 1024 * 1024,
                  bo->base.size - (uint64_t)bo->u.sparse.num_backing_pages * ZINK_SPARSE_BUFFER_PAGE_SIZE);
      size = MAX2(size, ZINK_SPARSE_BUFFER_PAGE_SIZE);

      buf = zink_bo_create(screen, size, ZINK_SPARSE_BUFFER_PAGE_SIZE,
                           ZINK_HEAP_DEVICE_LOCAL, 0, NULL);
      if (!buf) {
         FREE(best_backing->chunks);
         FREE(best_backing);
         return NULL;
      }

      /* We might have gotten a bigger buffer than requested via caching. */
      pages = buf->size / ZINK_SPARSE_BUFFER_PAGE_SIZE;

      best_backing->bo = zink_bo(buf);
      best_backing->num_chunks = 1;
      best_backing->chunks[0].begin = 0;
      best_backing->chunks[0].end = pages;

      list_add(&best_backing->list, &bo->u.sparse.backing);
      bo->u.sparse.num_backing_pages += pages;

      best_idx = 0;
      best_num_pages = pages;
   }

   *pnum_pages = MIN2(*pnum_pages, best_num_pages);
   *pstart_page = best_backing->chunks[best_idx].begin;
   best_backing->chunks[best_idx].begin += *pnum_pages;

   if (best_backing->chunks[best_idx].begin >= best_backing->chunks[best_idx].end) {
      memmove(&best_backing->chunks[best_idx], &best_backing->chunks[best_idx + 1],
              sizeof(*best_backing->chunks) * (best_backing->num_chunks - best_idx - 1));
      best_backing->num_chunks--;
   }

   return best_backing;
}

static void
sparse_free_backing_buffer(struct zink_screen *screen, struct zink_bo *bo,
                           struct zink_sparse_backing *backing)
{
   bo->u.sparse.num_backing_pages -= backing->bo->base.size / ZINK_SPARSE_BUFFER_PAGE_SIZE;

   list_del(&backing->list);
   zink_bo_unref(screen, backing->bo);
   FREE(backing->chunks);
   FREE(backing);
}

/*
 * Return a range of pages from the given backing buffer back into the
 * free structure.
 */
static bool
sparse_backing_free(struct zink_screen *screen, struct zink_bo *bo,
                    struct zink_sparse_backing *backing,
                    uint32_t start_page, uint32_t num_pages)
{
   uint32_t end_page = start_page + num_pages;
   unsigned low = 0;
   unsigned high = backing->num_chunks;

   /* Find the first chunk with begin >= start_page. */
   while (low < high) {
      unsigned mid = low + (high - low) / 2;

      if (backing->chunks[mid].begin >= start_page)
         high = mid;
      else
         low = mid + 1;
   }

   assert(low >= backing->num_chunks || end_page <= backing->chunks[low].begin);
   assert(low == 0 || backing->chunks[low - 1].end <= start_page);

   if (low > 0 && backing->chunks[low - 1].end == start_page) {
      backing->chunks[low - 1].end = end_page;

      if (low < backing->num_chunks && end_page == backing->chunks[low].begin) {
         backing->chunks[low - 1].end = backing->chunks[low].end;
         memmove(&backing->chunks[low], &backing->chunks[low + 1],
                 sizeof(*backing->chunks) * (backing->num_chunks - low - 1));
         backing->num_chunks--;
      }
   } else if (low < backing->num_chunks && end_page == backing->chunks[low].begin) {
      backing->chunks[low].begin = start_page;
   } else {
      if (backing->num_chunks >= backing->max_chunks) {
         unsigned new_max_chunks = 2 * backing->max_chunks;
         struct zink_sparse_backing_chunk *new_chunks =
            REALLOC(backing->chunks,
                    sizeof(*backing->chunks) * backing->max_chunks,
                    sizeof(*backing->chunks) * new_max_chunks);
         if (!new_chunks)
            return false;

         backing->max_chunks = new_max_chunks;
         backing->chunks = new_chunks;
      }

      memmove(&backing->chunks[low + 1], &backing->chunks[low],
              sizeof(*backing->chunks) * (backing->num_chunks - low));
      backing->chunks[low].begin = start_page;
      backing->chunks[low].end = end_page;
      backing->num_chunks++;
   }

   if (backing->num_chunks == 1 && backing->chunks[0].begin == 0 &&
       backing->chunks[0].end == backing->bo->base.size / ZINK_SPARSE_BUFFER_PAGE_SIZE)
      sparse_free_backing_buffer(screen, bo, backing);

   return true;
}

static void
bo_sparse_destroy(struct zink_screen *screen, struct pb_buffer *pbuf)
{
   struct zink_bo *bo = zink_bo(pbuf);

   assert(!bo->mem && bo->base.usage & ZINK_ALLOC_SPARSE);

   while (!list_is_empty(&bo->u.sparse.backing)) {
      sparse_free_backing_buffer(screen, bo,
                                 container_of(bo->u.sparse.backing.next,
                                              struct zink_sparse_backing, list));
   }

   FREE(bo->u.sparse.commitments);
   simple_mtx_destroy(&bo->lock);
   FREE(bo);
}

static const struct pb_vtbl bo_sparse_vtbl = {
   /* Cast to void* because one of the function parameters is a struct pointer instead of void*. */
   (void*)bo_sparse_destroy
   /* other functions are never called */
};

static struct pb_buffer *
bo_sparse_create(struct zink_screen *screen, uint64_t size)
{
   struct zink_bo *bo;

   /* We use 32-bit page numbers; refuse to attempt allocating sparse buffers
    * that exceed this limit. This is not really a restriction: we don't have
    * that much virtual address space anyway.
    */
   if (size > (uint64_t)INT32_MAX * ZINK_SPARSE_BUFFER_PAGE_SIZE)
      return NULL;

   bo = CALLOC_STRUCT(zink_bo);
   if (!bo)
      return NULL;

   simple_mtx_init(&bo->lock, mtx_plain);
   pipe_reference_init(&bo->base.reference, 1);
   bo->base.alignment_log2 = util_logbase2(ZINK_SPARSE_BUFFER_PAGE_SIZE);
   bo->base.size = size;
   bo->base.vtbl = &bo_sparse_vtbl;
   bo->base.placement = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
   bo->unique_id = p_atomic_inc_return(&screen->pb.next_bo_unique_id);
   bo->base.usage = ZINK_ALLOC_SPARSE;

   bo->u.sparse.num_va_pages = DIV_ROUND_UP(size, ZINK_SPARSE_BUFFER_PAGE_SIZE);
   bo->u.sparse.commitments = CALLOC(bo->u.sparse.num_va_pages,
                                     sizeof(*bo->u.sparse.commitments));
   if (!bo->u.sparse.commitments)
      goto error_alloc_commitments;

   list_inithead(&bo->u.sparse.backing);

   return &bo->base;

error_alloc_commitments:
   simple_mtx_destroy(&bo->lock);
   FREE(bo);
   return NULL;
}

struct pb_buffer *
zink_bo_create(struct zink_screen *screen, uint64_t size, unsigned alignment, enum zink_heap heap, enum zink_alloc_flag flags, const void *pNext)
{
   struct zink_bo *bo;
   /* pull in sparse flag */
   flags |= zink_alloc_flags_from_heap(heap);

   //struct pb_slabs *slabs = ((flags & RADEON_FLAG_ENCRYPTED) && screen->info.has_tmz_support) ?
      //screen->bo_slabs_encrypted : screen->bo_slabs;
   struct pb_slabs *slabs = screen->pb.bo_slabs;

   struct pb_slabs *last_slab = &slabs[NUM_SLAB_ALLOCATORS - 1];
   unsigned max_slab_entry_size = 1 << (last_slab->min_order + last_slab->num_orders - 1);

   /* Sub-allocate small buffers from slabs. */
   if (!(flags & (ZINK_ALLOC_NO_SUBALLOC | ZINK_ALLOC_SPARSE)) &&
       size <= max_slab_entry_size) {
      struct pb_slab_entry *entry;

      if (heap < 0 || heap >= ZINK_HEAP_MAX)
         goto no_slab;

      unsigned alloc_size = size;

      /* Always use slabs for sizes less than 4 KB because the kernel aligns
       * everything to 4 KB.
       */
      if (size < alignment && alignment <= 4 * 1024)
         alloc_size = alignment;

      if (alignment > get_slab_entry_alignment(screen, alloc_size)) {
         /* 3/4 allocations can return too small alignment. Try again with a power of two
          * allocation size.
          */
         unsigned pot_size = get_slab_pot_entry_size(screen, alloc_size);

         if (alignment <= pot_size) {
            /* This size works but wastes some memory to fulfil the alignment. */
            alloc_size = pot_size;
         } else {
            goto no_slab; /* can't fulfil alignment requirements */
         }
      }

      struct pb_slabs *slabs = get_slabs(screen, alloc_size, flags);
      bool reclaim_all = false;
      if (heap == ZINK_HEAP_DEVICE_LOCAL_VISIBLE && !screen->resizable_bar) {
         unsigned low_bound = 128 * 1024 * 1024; //128MB is a very small BAR
         if (screen->info.driver_props.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY)
            low_bound *= 2; //nvidia has fat textures or something
         unsigned heapidx = screen->info.mem_props.memoryTypes[screen->heap_map[heap]].heapIndex;
         reclaim_all = screen->info.mem_props.memoryHeaps[heapidx].size <= low_bound;
      }
      entry = pb_slab_alloc_reclaimed(slabs, alloc_size, heap, reclaim_all);
      if (!entry) {
         /* Clean up buffer managers and try again. */
         clean_up_buffer_managers(screen);

         entry = pb_slab_alloc_reclaimed(slabs, alloc_size, heap, true);
      }
      if (!entry)
         return NULL;

      bo = container_of(entry, struct zink_bo, u.slab.entry);
      pipe_reference_init(&bo->base.reference, 1);
      bo->base.size = size;
      assert(alignment <= 1 << bo->base.alignment_log2);

      return &bo->base;
   }
no_slab:

   if (flags & ZINK_ALLOC_SPARSE) {
      assert(ZINK_SPARSE_BUFFER_PAGE_SIZE % alignment == 0);

      return bo_sparse_create(screen, size);
   }

   /* Align size to page size. This is the minimum alignment for normal
    * BOs. Aligning this here helps the cached bufmgr. Especially small BOs,
    * like constant/uniform buffers, can benefit from better and more reuse.
    */
   if (heap == ZINK_HEAP_DEVICE_LOCAL_VISIBLE) {
      size = align64(size, screen->info.props.limits.minMemoryMapAlignment);
      alignment = align(alignment, screen->info.props.limits.minMemoryMapAlignment);
   }

   bool use_reusable_pool = !(flags & ZINK_ALLOC_NO_SUBALLOC);

   if (use_reusable_pool) {
       /* Get a buffer from the cache. */
       bo = (struct zink_bo*)
            pb_cache_reclaim_buffer(&screen->pb.bo_cache, size, alignment, 0, heap);
       if (bo)
          return &bo->base;
   }

   /* Create a new one. */
   bo = bo_create_internal(screen, size, alignment, heap, flags, pNext);
   if (!bo) {
      /* Clean up buffer managers and try again. */
      clean_up_buffer_managers(screen);

      bo = bo_create_internal(screen, size, alignment, heap, flags, pNext);
      if (!bo)
         return NULL;
   }

   return &bo->base;
}

void *
zink_bo_map(struct zink_screen *screen, struct zink_bo *bo)
{
   void *cpu = NULL;
   uint64_t offset = 0;
   struct zink_bo *real;

   if (bo->mem) {
      real = bo;
   } else {
      real = bo->u.slab.real;
      offset = bo->offset - real->offset;
   }

   cpu = p_atomic_read(&real->u.real.cpu_ptr);
   if (!cpu) {
      simple_mtx_lock(&real->lock);
      /* Must re-check due to the possibility of a race. Re-check need not
       * be atomic thanks to the lock. */
      cpu = real->u.real.cpu_ptr;
      if (!cpu) {
         VkResult result = VKSCR(MapMemory)(screen->dev, real->mem, 0, real->base.size, 0, &cpu);
         if (result != VK_SUCCESS) {
            simple_mtx_unlock(&real->lock);
            return NULL;
         }
         p_atomic_set(&real->u.real.cpu_ptr, cpu);
      }
      simple_mtx_unlock(&real->lock);
   }
   p_atomic_inc(&real->u.real.map_count);

   return (uint8_t*)cpu + offset;
}

void
zink_bo_unmap(struct zink_screen *screen, struct zink_bo *bo)
{
   struct zink_bo *real = bo->mem ? bo : bo->u.slab.real;

   assert(real->u.real.map_count != 0 && "too many unmaps");

   if (p_atomic_dec_zero(&real->u.real.map_count)) {
      p_atomic_set(&real->u.real.cpu_ptr, NULL);
      VKSCR(UnmapMemory)(screen->dev, real->mem);
   }
}

static bool
buffer_commit_single(struct zink_screen *screen, struct zink_resource *res, struct zink_bo *bo, uint32_t offset, uint32_t size, bool commit)
{
   VkBindSparseInfo sparse = {0};
   sparse.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
   sparse.bufferBindCount = res->obj->storage_buffer ? 2 : 1;

   VkSparseBufferMemoryBindInfo sparse_bind[2];
   sparse_bind[0].buffer = res->obj->buffer;
   sparse_bind[1].buffer = res->obj->storage_buffer;
   sparse_bind[0].bindCount = 1;
   sparse_bind[1].bindCount = 1;
   sparse.pBufferBinds = sparse_bind;

   VkSparseMemoryBind mem_bind;
   mem_bind.resourceOffset = offset;
   mem_bind.size = MIN2(res->base.b.width0 - offset, size);
   mem_bind.memory = commit ? (bo->mem ? bo->mem : bo->u.slab.real->mem) : VK_NULL_HANDLE;
   mem_bind.memoryOffset = commit ? (bo->mem ? 0 : bo->offset) : 0;
   mem_bind.flags = 0;
   sparse_bind[0].pBinds = &mem_bind;
   sparse_bind[1].pBinds = &mem_bind;

   VkQueue queue = screen->threaded ? screen->thread_queue : screen->queue;

   VkResult ret = VKSCR(QueueBindSparse)(queue, 1, &sparse, VK_NULL_HANDLE);
   return zink_screen_handle_vkresult(screen, ret);
}

static bool
buffer_bo_commit(struct zink_screen *screen, struct zink_resource *res, uint32_t offset, uint32_t size, bool commit)
{
   bool ok = true;
   struct zink_bo *bo = res->obj->bo;
   assert(offset % ZINK_SPARSE_BUFFER_PAGE_SIZE == 0);
   assert(offset <= bo->base.size);
   assert(size <= bo->base.size - offset);
   assert(size % ZINK_SPARSE_BUFFER_PAGE_SIZE == 0 || offset + size == bo->base.size);

   struct zink_sparse_commitment *comm = bo->u.sparse.commitments;

   uint32_t va_page = offset / ZINK_SPARSE_BUFFER_PAGE_SIZE;
   uint32_t end_va_page = va_page + DIV_ROUND_UP(size, ZINK_SPARSE_BUFFER_PAGE_SIZE);

   if (commit) {
      while (va_page < end_va_page) {
         uint32_t span_va_page;

         /* Skip pages that are already committed. */
         if (comm[va_page].backing) {
            va_page++;
            continue;
         }

         /* Determine length of uncommitted span. */
         span_va_page = va_page;
         while (va_page < end_va_page && !comm[va_page].backing)
            va_page++;

         /* Fill the uncommitted span with chunks of backing memory. */
         while (span_va_page < va_page) {
            struct zink_sparse_backing *backing;
            uint32_t backing_start, backing_size;

            backing_size = va_page - span_va_page;
            backing = sparse_backing_alloc(screen, bo, &backing_start, &backing_size);
            if (!backing) {
               ok = false;
               goto out;
            }
            if (!buffer_commit_single(screen, res, backing->bo,
                                      (uint64_t)span_va_page * ZINK_SPARSE_BUFFER_PAGE_SIZE,
                                      (uint64_t)backing_size * ZINK_SPARSE_BUFFER_PAGE_SIZE, true)) {

               ok = sparse_backing_free(screen, bo, backing, backing_start, backing_size);
               assert(ok && "sufficient memory should already be allocated");

               ok = false;
               goto out;
            }

            while (backing_size) {
               comm[span_va_page].backing = backing;
               comm[span_va_page].page = backing_start;
               span_va_page++;
               backing_start++;
               backing_size--;
            }
         }
      }
   } else {
      if (!buffer_commit_single(screen, res, NULL,
                                (uint64_t)va_page * ZINK_SPARSE_BUFFER_PAGE_SIZE,
                                (uint64_t)(end_va_page - va_page) * ZINK_SPARSE_BUFFER_PAGE_SIZE, false)) {
         ok = false;
         goto out;
      }

      while (va_page < end_va_page) {
         struct zink_sparse_backing *backing;
         uint32_t backing_start;
         uint32_t span_pages;

         /* Skip pages that are already uncommitted. */
         if (!comm[va_page].backing) {
            va_page++;
            continue;
         }

         /* Group contiguous spans of pages. */
         backing = comm[va_page].backing;
         backing_start = comm[va_page].page;
         comm[va_page].backing = NULL;

         span_pages = 1;
         va_page++;

         while (va_page < end_va_page &&
                comm[va_page].backing == backing &&
                comm[va_page].page == backing_start + span_pages) {
            comm[va_page].backing = NULL;
            va_page++;
            span_pages++;
         }

         if (!sparse_backing_free(screen, bo, backing, backing_start, span_pages)) {
            /* Couldn't allocate tracking data structures, so we have to leak */
            fprintf(stderr, "zink: leaking sparse backing memory\n");
            ok = false;
         }
      }
   }
out:
   return ok;
}

static bool
texture_commit_single(struct zink_screen *screen, struct zink_resource *res, VkSparseImageMemoryBind *ibind, unsigned num_binds, bool commit)
{
   VkBindSparseInfo sparse = {0};
   sparse.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
   sparse.imageBindCount = 1;

   VkSparseImageMemoryBindInfo sparse_ibind;
   sparse_ibind.image = res->obj->image;
   sparse_ibind.bindCount = num_binds;
   sparse_ibind.pBinds = ibind;
   sparse.pImageBinds = &sparse_ibind;

   VkQueue queue = screen->threaded ? screen->thread_queue : screen->queue;

   VkResult ret = VKSCR(QueueBindSparse)(queue, 1, &sparse, VK_NULL_HANDLE);
   return zink_screen_handle_vkresult(screen, ret);
}

static bool
texture_commit_miptail(struct zink_screen *screen, struct zink_resource *res, struct zink_bo *bo, uint32_t offset, bool commit)
{
   VkBindSparseInfo sparse = {0};
   sparse.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
   sparse.imageOpaqueBindCount = 1;

   VkSparseImageOpaqueMemoryBindInfo sparse_bind;
   sparse_bind.image = res->obj->image;
   sparse_bind.bindCount = 1;
   sparse.pImageOpaqueBinds = &sparse_bind;

   VkSparseMemoryBind mem_bind;
   mem_bind.resourceOffset = offset;
   mem_bind.size = MIN2(ZINK_SPARSE_BUFFER_PAGE_SIZE, res->sparse.imageMipTailSize - offset);
   mem_bind.memory = commit ? (bo->mem ? bo->mem : bo->u.slab.real->mem) : VK_NULL_HANDLE;
   mem_bind.memoryOffset = commit ? (bo->mem ? 0 : bo->offset) : 0;
   mem_bind.flags = 0;
   sparse_bind.pBinds = &mem_bind;

   VkQueue queue = screen->threaded ? screen->thread_queue : screen->queue;

   VkResult ret = VKSCR(QueueBindSparse)(queue, 1, &sparse, VK_NULL_HANDLE);
   return zink_screen_handle_vkresult(screen, ret);
}

bool
zink_bo_commit(struct zink_screen *screen, struct zink_resource *res, unsigned level, struct pipe_box *box, bool commit)
{
   bool ok = true;
   struct zink_bo *bo = res->obj->bo;

   if (screen->faked_e5sparse && res->base.b.format == PIPE_FORMAT_R9G9B9E5_FLOAT)
      return true;

   simple_mtx_lock(&screen->queue_lock);
   simple_mtx_lock(&bo->lock);
   if (res->base.b.target == PIPE_BUFFER) {
      ok = buffer_bo_commit(screen, res, box->x, box->width, commit);
      goto out;
   }

   int gwidth, gheight, gdepth;
   gwidth = res->sparse.formatProperties.imageGranularity.width;
   gheight = res->sparse.formatProperties.imageGranularity.height;
   gdepth = res->sparse.formatProperties.imageGranularity.depth;
   assert(gwidth && gheight && gdepth);

   struct zink_sparse_commitment *comm = bo->u.sparse.commitments;
   VkImageSubresource subresource = { res->aspect, level, 0 };
   unsigned nwidth = DIV_ROUND_UP(box->width, gwidth);
   unsigned nheight = DIV_ROUND_UP(box->height, gheight);
   unsigned ndepth = DIV_ROUND_UP(box->depth, gdepth);
   VkExtent3D lastBlockExtent = {
			   (box->width % gwidth) ? box->width % gwidth : gwidth,
			   (box->height % gheight) ? box->height % gheight : gheight,
			   (box->depth % gdepth) ? box->depth % gdepth : gdepth
   };
   /* TODO: msaa needs miptail */
   //VkSparseImageOpaqueMemoryBindInfo sparse_obind;
   VkSparseImageMemoryBind ibind[10];
   uint32_t backing_start[10], backing_size[10];
   struct zink_sparse_backing *backing[10];
   unsigned i = 0;
   bool commits_pending = false;
   uint32_t va_page_offset = 0;
   for (unsigned l = 0; l < level; l++) {
      unsigned mipwidth = DIV_ROUND_UP(MAX2(res->base.b.width0 >> l, 1), gwidth);
      unsigned mipheight = DIV_ROUND_UP(MAX2(res->base.b.height0 >> l, 1), gheight);
      unsigned mipdepth = DIV_ROUND_UP(MAX2(res->base.b.depth0 >> l, 1), gdepth);
      va_page_offset += mipwidth * mipheight * mipdepth;
   }
   for (unsigned d = 0; d < ndepth; d++) {
      for (unsigned h = 0; h < nheight; h++) {
         for (unsigned w = 0; w < nwidth; w++) {
            ibind[i].subresource = subresource;
            ibind[i].flags = 0;
            // Offset
            ibind[i].offset.x = w * gwidth;
            ibind[i].offset.y = h * gheight;
            if (res->base.b.target == PIPE_TEXTURE_CUBE) {
               ibind[i].subresource.arrayLayer = d * gdepth;
               ibind[i].offset.z = 0;
            } else {
               ibind[i].offset.z = d * gdepth;
            }
            // Size of the page
            ibind[i].extent.width = (w == nwidth - 1) ? lastBlockExtent.width : gwidth;
            ibind[i].extent.height = (h == nheight - 1) ? lastBlockExtent.height : gheight;
            ibind[i].extent.depth = (d == ndepth - 1 && res->base.b.target != PIPE_TEXTURE_CUBE) ? lastBlockExtent.depth : gdepth;
            uint32_t va_page = va_page_offset +
                              (d + (box->z / gdepth)) * ((MAX2(res->base.b.width0 >> level, 1) / gwidth) * (MAX2(res->base.b.height0 >> level, 1) / gheight)) +
                              (h + (box->y / gheight)) * (MAX2(res->base.b.width0 >> level, 1) / gwidth) +
                              (w + (box->x / gwidth));

            uint32_t end_va_page = va_page + 1;

            if (commit) {
               while (va_page < end_va_page) {
                  uint32_t span_va_page;

                  /* Skip pages that are already committed. */
                  if (comm[va_page].backing) {
                     va_page++;
                     continue;
                  }

                  /* Determine length of uncommitted span. */
                  span_va_page = va_page;
                  while (va_page < end_va_page && !comm[va_page].backing)
                     va_page++;

                  /* Fill the uncommitted span with chunks of backing memory. */
                  while (span_va_page < va_page) {
                     backing_size[i] = va_page - span_va_page;
                     backing[i] = sparse_backing_alloc(screen, bo, &backing_start[i], &backing_size[i]);
                     if (!backing[i]) {
                        ok = false;
                        goto out;
                     }
                     if (level >= res->sparse.imageMipTailFirstLod) {
                        uint32_t offset = res->sparse.imageMipTailOffset + d * res->sparse.imageMipTailStride;
                        ok = texture_commit_miptail(screen, res, backing[i]->bo, offset, commit);
                        if (!ok)
                           goto out;
                     } else {
                        ibind[i].memory = backing[i]->bo->mem ? backing[i]->bo->mem : backing[i]->bo->u.slab.real->mem;
                        ibind[i].memoryOffset = backing[i]->bo->mem ? 0 : backing[i]->bo->offset;
                        commits_pending = true;
                     }

                     while (backing_size[i]) {
                        comm[span_va_page].backing = backing[i];
                        comm[span_va_page].page = backing_start[i];
                        span_va_page++;
                        backing_start[i]++;
                        backing_size[i]--;
                     }
                     i++;
                  }
               }
            } else {
               ibind[i].memory = VK_NULL_HANDLE;
               ibind[i].memoryOffset = 0;

               while (va_page < end_va_page) {
                  /* Skip pages that are already uncommitted. */
                  if (!comm[va_page].backing) {
                     va_page++;
                     continue;
                  }

                  /* Group contiguous spans of pages. */
                  backing[i] = comm[va_page].backing;
                  backing_start[i] = comm[va_page].page;
                  comm[va_page].backing = NULL;

                  backing_size[i] = 1;
                  va_page++;

                  while (va_page < end_va_page &&
                         comm[va_page].backing == backing[i] &&
                         comm[va_page].page == backing_start[i] + backing_size[i]) {
                     comm[va_page].backing = NULL;
                     va_page++;
                     backing_size[i]++;
                  }
                  if (level >= res->sparse.imageMipTailFirstLod) {
                     uint32_t offset = res->sparse.imageMipTailOffset + d * res->sparse.imageMipTailStride;
                     ok = texture_commit_miptail(screen, res, NULL, offset, commit);
                     if (!ok)
                        goto out;
                  } else {
                     commits_pending = true;
                  }
                  i++;
               }
            }
            if (i == ARRAY_SIZE(ibind)) {
               if (!texture_commit_single(screen, res, ibind, ARRAY_SIZE(ibind), commit)) {
                  for (unsigned s = 0; s < i; s++) {
                     ok = sparse_backing_free(screen, backing[s]->bo, backing[s], backing_start[s], backing_size[s]);
                     if (!ok) {
                        /* Couldn't allocate tracking data structures, so we have to leak */
                        fprintf(stderr, "zink: leaking sparse backing memory\n");
                     }
                  }
                  ok = false;
                  goto out;
               }
               commits_pending = false;
               i = 0;
            }
         }
      }
   }
   if (commits_pending && !texture_commit_single(screen, res, ibind, i, commit)) {
      for (unsigned s = 0; s < i; s++) {
         ok = sparse_backing_free(screen, backing[s]->bo, backing[s], backing_start[s], backing_size[s]);
         if (!ok) {
            /* Couldn't allocate tracking data structures, so we have to leak */
            fprintf(stderr, "zink: leaking sparse backing memory\n");
         }
      }
      ok = false;
   }
out:

   simple_mtx_unlock(&bo->lock);
   simple_mtx_unlock(&screen->queue_lock);
   return ok;
}

static const struct pb_vtbl bo_slab_vtbl = {
   /* Cast to void* because one of the function parameters is a struct pointer instead of void*. */
   (void*)bo_slab_destroy
   /* other functions are never called */
};

static struct pb_slab *
bo_slab_alloc(void *priv, unsigned heap, unsigned entry_size, unsigned group_index, bool encrypted)
{
   struct zink_screen *screen = priv;
   uint32_t base_id;
   unsigned slab_size = 0;
   struct zink_slab *slab = CALLOC_STRUCT(zink_slab);

   if (!slab)
      return NULL;

   //struct pb_slabs *slabs = ((flags & RADEON_FLAG_ENCRYPTED) && screen->info.has_tmz_support) ?
      //screen->bo_slabs_encrypted : screen->bo_slabs;
   struct pb_slabs *slabs = screen->pb.bo_slabs;

   /* Determine the slab buffer size. */
   for (unsigned i = 0; i < NUM_SLAB_ALLOCATORS; i++) {
      unsigned max_entry_size = 1 << (slabs[i].min_order + slabs[i].num_orders - 1);

      if (entry_size <= max_entry_size) {
         /* The slab size is twice the size of the largest possible entry. */
         slab_size = max_entry_size * 2;

         if (!util_is_power_of_two_nonzero(entry_size)) {
            assert(util_is_power_of_two_nonzero(entry_size * 4 / 3));

            /* If the entry size is 3/4 of a power of two, we would waste space and not gain
             * anything if we allocated only twice the power of two for the backing buffer:
             *   2 * 3/4 = 1.5 usable with buffer size 2
             *
             * Allocating 5 times the entry size leads us to the next power of two and results
             * in a much better memory utilization:
             *   5 * 3/4 = 3.75 usable with buffer size 4
             */
            if (entry_size * 5 > slab_size)
               slab_size = util_next_power_of_two(entry_size * 5);
         }

         break;
      }
   }
   assert(slab_size != 0);

   slab->buffer = zink_bo(zink_bo_create(screen, slab_size, slab_size, heap, 0, NULL));
   if (!slab->buffer)
      goto fail;

   slab_size = slab->buffer->base.size;

   slab->base.num_entries = slab_size / entry_size;
   slab->base.num_free = slab->base.num_entries;
   slab->entry_size = entry_size;
   slab->entries = CALLOC(slab->base.num_entries, sizeof(*slab->entries));
   if (!slab->entries)
      goto fail_buffer;

   list_inithead(&slab->base.free);

#ifdef _MSC_VER
   /* C11 too hard for msvc, no __sync_fetch_and_add */
   base_id = p_atomic_add_return(&screen->pb.next_bo_unique_id, slab->base.num_entries) - slab->base.num_entries;
#else
   base_id = __sync_fetch_and_add(&screen->pb.next_bo_unique_id, slab->base.num_entries);
#endif
   for (unsigned i = 0; i < slab->base.num_entries; ++i) {
      struct zink_bo *bo = &slab->entries[i];

      simple_mtx_init(&bo->lock, mtx_plain);
      bo->base.alignment_log2 = util_logbase2(get_slab_entry_alignment(screen, entry_size));
      bo->base.size = entry_size;
      bo->base.vtbl = &bo_slab_vtbl;
      bo->offset = slab->buffer->offset + i * entry_size;
      bo->unique_id = base_id + i;
      bo->u.slab.entry.slab = &slab->base;
      bo->u.slab.entry.group_index = group_index;
      bo->u.slab.entry.entry_size = entry_size;

      if (slab->buffer->mem) {
         /* The slab is not suballocated. */
         bo->u.slab.real = slab->buffer;
      } else {
         /* The slab is allocated out of a bigger slab. */
         bo->u.slab.real = slab->buffer->u.slab.real;
         assert(bo->u.slab.real->mem);
      }
      bo->base.placement = bo->u.slab.real->base.placement;

      list_addtail(&bo->u.slab.entry.head, &slab->base.free);
   }

   /* Wasted alignment due to slabs with 3/4 allocations being aligned to a power of two. */
   assert(slab->base.num_entries * entry_size <= slab_size);

   return &slab->base;

fail_buffer:
   zink_bo_unref(screen, slab->buffer);
fail:
   FREE(slab);
   return NULL;
}

static struct pb_slab *
bo_slab_alloc_normal(void *priv, unsigned heap, unsigned entry_size, unsigned group_index)
{
   return bo_slab_alloc(priv, heap, entry_size, group_index, false);
}

bool
zink_bo_init(struct zink_screen *screen)
{
   uint64_t total_mem = 0;
   for (uint32_t i = 0; i < screen->info.mem_props.memoryHeapCount; ++i)
      total_mem += screen->info.mem_props.memoryHeaps[i].size;
   /* Create managers. */
   pb_cache_init(&screen->pb.bo_cache, ZINK_HEAP_MAX,
                 500000, 2.0f, 0,
                 total_mem / 8, screen,
                 (void*)bo_destroy, (void*)bo_can_reclaim);

   unsigned min_slab_order = MIN_SLAB_ORDER;  /* 256 bytes */
   unsigned max_slab_order = 20; /* 1 MB (slab size = 2 MB) */
   unsigned num_slab_orders_per_allocator = (max_slab_order - min_slab_order) /
                                            NUM_SLAB_ALLOCATORS;

   /* Divide the size order range among slab managers. */
   for (unsigned i = 0; i < NUM_SLAB_ALLOCATORS; i++) {
      unsigned min_order = min_slab_order;
      unsigned max_order = MIN2(min_order + num_slab_orders_per_allocator,
                                max_slab_order);

      if (!pb_slabs_init(&screen->pb.bo_slabs[i],
                         min_order, max_order,
                         ZINK_HEAP_MAX, true,
                         screen,
                         bo_can_reclaim_slab,
                         bo_slab_alloc_normal,
                         (void*)bo_slab_free)) {
         return false;
      }
      min_slab_order = max_order + 1;
   }
   screen->pb.min_alloc_size = 1 << screen->pb.bo_slabs[0].min_order;
   screen->pb.bo_export_table = util_hash_table_create_ptr_keys();
   simple_mtx_init(&screen->pb.bo_export_table_lock, mtx_plain);
   return true;
}

void
zink_bo_deinit(struct zink_screen *screen)
{
   for (unsigned i = 0; i < NUM_SLAB_ALLOCATORS; i++) {
      if (screen->pb.bo_slabs[i].groups)
         pb_slabs_deinit(&screen->pb.bo_slabs[i]);
   }
   pb_cache_deinit(&screen->pb.bo_cache);
   _mesa_hash_table_destroy(screen->pb.bo_export_table, NULL);
   simple_mtx_destroy(&screen->pb.bo_export_table_lock);
}
