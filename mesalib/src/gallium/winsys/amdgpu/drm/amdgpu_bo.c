/*
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <sys/ioctl.h>

#include "amdgpu_cs.h"

#include "util/hash_table.h"
#include "util/os_time.h"
#include "util/u_hash_table.h"
#include "util/u_process.h"
#include "frontend/drm_driver.h"
#include "drm-uapi/amdgpu_drm.h"
#include "drm-uapi/dma-buf.h"
#include <xf86drm.h>
#include <stdio.h>
#include <inttypes.h>

#ifndef AMDGPU_VA_RANGE_HIGH
#define AMDGPU_VA_RANGE_HIGH	0x2
#endif

/* Set to 1 for verbose output showing committed sparse buffer ranges. */
#define DEBUG_SPARSE_COMMITS 0

struct amdgpu_sparse_backing_chunk {
   uint32_t begin, end;
};

static bool amdgpu_bo_wait(struct radeon_winsys *rws,
                           struct pb_buffer *_buf, uint64_t timeout,
                           unsigned usage)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_winsys_bo *bo = amdgpu_winsys_bo(_buf);
   int64_t abs_timeout = 0;

   if (timeout == 0) {
      if (p_atomic_read(&bo->num_active_ioctls))
         return false;

   } else {
      abs_timeout = os_time_get_absolute_timeout(timeout);

      /* Wait if any ioctl is being submitted with this buffer. */
      if (!os_wait_until_zero_abs_timeout(&bo->num_active_ioctls, abs_timeout))
         return false;
   }

   if (is_real_bo(bo) && get_real_bo(bo)->is_shared) {
      /* We can't use user fences for shared buffers, because user fences
       * are local to this process only. If we want to wait for all buffer
       * uses in all processes, we have to use amdgpu_bo_wait_for_idle.
       */
      bool buffer_busy = true;
      int r;

      r = amdgpu_bo_wait_for_idle(get_real_bo(bo)->bo, timeout, &buffer_busy);
      if (r)
         fprintf(stderr, "%s: amdgpu_bo_wait_for_idle failed %i\n", __func__, r);
      return !buffer_busy;
   }

   if (timeout == 0) {
      unsigned idle_fences;
      bool buffer_idle;

      simple_mtx_lock(&ws->bo_fence_lock);

      for (idle_fences = 0; idle_fences < bo->num_fences; ++idle_fences) {
         if (!amdgpu_fence_wait(bo->fences[idle_fences], 0, false))
            break;
      }

      /* Release the idle fences to avoid checking them again later. */
      for (unsigned i = 0; i < idle_fences; ++i)
         amdgpu_fence_reference(&bo->fences[i], NULL);

      memmove(&bo->fences[0], &bo->fences[idle_fences],
              (bo->num_fences - idle_fences) * sizeof(*bo->fences));
      bo->num_fences -= idle_fences;

      buffer_idle = !bo->num_fences;
      simple_mtx_unlock(&ws->bo_fence_lock);

      return buffer_idle;
   } else {
      bool buffer_idle = true;

      simple_mtx_lock(&ws->bo_fence_lock);
      while (bo->num_fences && buffer_idle) {
         struct pipe_fence_handle *fence = NULL;
         bool fence_idle = false;

         amdgpu_fence_reference(&fence, bo->fences[0]);

         /* Wait for the fence. */
         simple_mtx_unlock(&ws->bo_fence_lock);
         if (amdgpu_fence_wait(fence, abs_timeout, true))
            fence_idle = true;
         else
            buffer_idle = false;
         simple_mtx_lock(&ws->bo_fence_lock);

         /* Release an idle fence to avoid checking it again later, keeping in
          * mind that the fence array may have been modified by other threads.
          */
         if (fence_idle && bo->num_fences && bo->fences[0] == fence) {
            amdgpu_fence_reference(&bo->fences[0], NULL);
            memmove(&bo->fences[0], &bo->fences[1],
                    (bo->num_fences - 1) * sizeof(*bo->fences));
            bo->num_fences--;
         }

         amdgpu_fence_reference(&fence, NULL);
      }
      simple_mtx_unlock(&ws->bo_fence_lock);

      return buffer_idle;
   }
}

static enum radeon_bo_domain amdgpu_bo_get_initial_domain(
      struct pb_buffer *buf)
{
   return ((struct amdgpu_winsys_bo*)buf)->base.placement;
}

static enum radeon_bo_flag amdgpu_bo_get_flags(
      struct pb_buffer *buf)
{
   return ((struct amdgpu_winsys_bo*)buf)->base.usage;
}

static void amdgpu_bo_remove_fences(struct amdgpu_winsys_bo *bo)
{
   for (unsigned i = 0; i < bo->num_fences; ++i)
      amdgpu_fence_reference(&bo->fences[i], NULL);

   FREE(bo->fences);
   bo->num_fences = 0;
   bo->max_fences = 0;
}

void amdgpu_bo_destroy(struct amdgpu_winsys *ws, struct pb_buffer *_buf)
{
   struct amdgpu_bo_real *bo = get_real_bo(amdgpu_winsys_bo(_buf));
   struct amdgpu_screen_winsys *sws_iter;

   simple_mtx_lock(&ws->bo_export_table_lock);

   /* amdgpu_bo_from_handle might have revived the bo */
   if (p_atomic_read(&bo->b.base.reference.count)) {
      simple_mtx_unlock(&ws->bo_export_table_lock);
      return;
   }

   _mesa_hash_table_remove_key(ws->bo_export_table, bo->bo);

   if (bo->b.base.placement & RADEON_DOMAIN_VRAM_GTT) {
      amdgpu_bo_va_op(bo->bo, 0, bo->b.base.size, bo->b.va, 0, AMDGPU_VA_OP_UNMAP);
      amdgpu_va_range_free(bo->va_handle);
   }

   simple_mtx_unlock(&ws->bo_export_table_lock);

   if (!bo->is_user_ptr && bo->cpu_ptr) {
      bo->cpu_ptr = NULL;
      amdgpu_bo_unmap(&ws->dummy_ws.base, &bo->b.base);
   }
   assert(bo->is_user_ptr || bo->map_count == 0);

   amdgpu_bo_free(bo->bo);

#if DEBUG
   if (ws->debug_all_bos) {
      simple_mtx_lock(&ws->global_bo_list_lock);
      list_del(&bo->global_list_item);
      ws->num_buffers--;
      simple_mtx_unlock(&ws->global_bo_list_lock);
   }
#endif

   /* Close all KMS handles retrieved for other DRM file descriptions */
   simple_mtx_lock(&ws->sws_list_lock);
   for (sws_iter = ws->sws_list; sws_iter; sws_iter = sws_iter->next) {
      struct hash_entry *entry;

      if (!sws_iter->kms_handles)
         continue;

      entry = _mesa_hash_table_search(sws_iter->kms_handles, bo);
      if (entry) {
         struct drm_gem_close args = { .handle = (uintptr_t)entry->data };

         drmIoctl(sws_iter->fd, DRM_IOCTL_GEM_CLOSE, &args);
         _mesa_hash_table_remove(sws_iter->kms_handles, entry);
      }
   }
   simple_mtx_unlock(&ws->sws_list_lock);

   amdgpu_bo_remove_fences(&bo->b);

   if (bo->b.base.placement & RADEON_DOMAIN_VRAM)
      ws->allocated_vram -= align64(bo->b.base.size, ws->info.gart_page_size);
   else if (bo->b.base.placement & RADEON_DOMAIN_GTT)
      ws->allocated_gtt -= align64(bo->b.base.size, ws->info.gart_page_size);

   simple_mtx_destroy(&bo->lock);
   FREE(bo);
}

static void amdgpu_bo_destroy_or_cache(struct radeon_winsys *rws, struct pb_buffer *_buf)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_winsys_bo *bo = amdgpu_winsys_bo(_buf);

   assert(is_real_bo(bo)); /* slab buffers have a separate vtbl */

   if (bo->type == AMDGPU_BO_REAL_REUSABLE)
      pb_cache_add_buffer(&((struct amdgpu_bo_real_reusable*)bo)->cache_entry);
   else
      amdgpu_bo_destroy(ws, _buf);
}

static void amdgpu_clean_up_buffer_managers(struct amdgpu_winsys *ws)
{
   for (unsigned i = 0; i < NUM_SLAB_ALLOCATORS; i++)
      pb_slabs_reclaim(&ws->bo_slabs[i]);

   pb_cache_release_all_buffers(&ws->bo_cache);
}

static bool amdgpu_bo_do_map(struct radeon_winsys *rws, struct amdgpu_bo_real *bo, void **cpu)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);

   assert(!bo->is_user_ptr);

   int r = amdgpu_bo_cpu_map(bo->bo, cpu);
   if (r) {
      /* Clean up buffer managers and try again. */
      amdgpu_clean_up_buffer_managers(ws);
      r = amdgpu_bo_cpu_map(bo->bo, cpu);
      if (r)
         return false;
   }

   if (p_atomic_inc_return(&bo->map_count) == 1) {
      if (bo->b.base.placement & RADEON_DOMAIN_VRAM)
         ws->mapped_vram += bo->b.base.size;
      else if (bo->b.base.placement & RADEON_DOMAIN_GTT)
         ws->mapped_gtt += bo->b.base.size;
      ws->num_mapped_buffers++;
   }

   return true;
}

void *amdgpu_bo_map(struct radeon_winsys *rws,
                    struct pb_buffer *buf,
                    struct radeon_cmdbuf *rcs,
                    enum pipe_map_flags usage)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_winsys_bo *bo = (struct amdgpu_winsys_bo*)buf;
   struct amdgpu_bo_real *real;
   struct amdgpu_cs *cs = rcs ? amdgpu_cs(rcs) : NULL;

   assert(bo->type != AMDGPU_BO_SPARSE);

   /* If it's not unsynchronized bo_map, flush CS if needed and then wait. */
   if (!(usage & PIPE_MAP_UNSYNCHRONIZED)) {
      /* DONTBLOCK doesn't make sense with UNSYNCHRONIZED. */
      if (usage & PIPE_MAP_DONTBLOCK) {
         if (!(usage & PIPE_MAP_WRITE)) {
            /* Mapping for read.
             *
             * Since we are mapping for read, we don't need to wait
             * if the GPU is using the buffer for read too
             * (neither one is changing it).
             *
             * Only check whether the buffer is being used for write. */
            if (cs && amdgpu_bo_is_referenced_by_cs_with_usage(cs, bo,
                                                               RADEON_USAGE_WRITE)) {
               cs->flush_cs(cs->flush_data,
			    RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW, NULL);
               return NULL;
            }

            if (!amdgpu_bo_wait(rws, (struct pb_buffer*)bo, 0,
                                RADEON_USAGE_WRITE)) {
               return NULL;
            }
         } else {
            if (cs && amdgpu_bo_is_referenced_by_cs(cs, bo)) {
               cs->flush_cs(cs->flush_data,
			    RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW, NULL);
               return NULL;
            }

            if (!amdgpu_bo_wait(rws, (struct pb_buffer*)bo, 0,
                                RADEON_USAGE_READWRITE)) {
               return NULL;
            }
         }
      } else {
         uint64_t time = os_time_get_nano();

         if (!(usage & PIPE_MAP_WRITE)) {
            /* Mapping for read.
             *
             * Since we are mapping for read, we don't need to wait
             * if the GPU is using the buffer for read too
             * (neither one is changing it).
             *
             * Only check whether the buffer is being used for write. */
            if (cs) {
               if (amdgpu_bo_is_referenced_by_cs_with_usage(cs, bo,
                                                            RADEON_USAGE_WRITE)) {
                  cs->flush_cs(cs->flush_data,
			       RADEON_FLUSH_START_NEXT_GFX_IB_NOW, NULL);
               } else {
                  /* Try to avoid busy-waiting in amdgpu_bo_wait. */
                  if (p_atomic_read(&bo->num_active_ioctls))
                     amdgpu_cs_sync_flush(rcs);
               }
            }

            amdgpu_bo_wait(rws, (struct pb_buffer*)bo, OS_TIMEOUT_INFINITE,
                           RADEON_USAGE_WRITE);
         } else {
            /* Mapping for write. */
            if (cs) {
               if (amdgpu_bo_is_referenced_by_cs(cs, bo)) {
                  cs->flush_cs(cs->flush_data,
			       RADEON_FLUSH_START_NEXT_GFX_IB_NOW, NULL);
               } else {
                  /* Try to avoid busy-waiting in amdgpu_bo_wait. */
                  if (p_atomic_read(&bo->num_active_ioctls))
                     amdgpu_cs_sync_flush(rcs);
               }
            }

            amdgpu_bo_wait(rws, (struct pb_buffer*)bo, OS_TIMEOUT_INFINITE,
                           RADEON_USAGE_READWRITE);
         }

         ws->buffer_wait_time += os_time_get_nano() - time;
      }
   }

   /* Buffer synchronization has been checked, now actually map the buffer. */
   void *cpu = NULL;
   uint64_t offset = 0;

   if (is_real_bo(bo)) {
      real = get_real_bo(bo);
   } else {
      real = get_slab_bo(bo)->real;
      offset = bo->va - real->b.va;
   }

   if (usage & RADEON_MAP_TEMPORARY) {
      if (real->is_user_ptr) {
         cpu = real->cpu_ptr;
      } else {
         if (!amdgpu_bo_do_map(rws, real, &cpu))
            return NULL;
      }
   } else {
      cpu = p_atomic_read(&real->cpu_ptr);
      if (!cpu) {
         simple_mtx_lock(&real->lock);
         /* Must re-check due to the possibility of a race. Re-check need not
          * be atomic thanks to the lock. */
         cpu = real->cpu_ptr;
         if (!cpu) {
            if (!amdgpu_bo_do_map(rws, real, &cpu)) {
               simple_mtx_unlock(&real->lock);
               return NULL;
            }
            p_atomic_set(&real->cpu_ptr, cpu);
         }
         simple_mtx_unlock(&real->lock);
      }
   }

   return (uint8_t*)cpu + offset;
}

void amdgpu_bo_unmap(struct radeon_winsys *rws, struct pb_buffer *buf)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_winsys_bo *bo = (struct amdgpu_winsys_bo*)buf;
   struct amdgpu_bo_real *real;

   assert(bo->type != AMDGPU_BO_SPARSE);

   real = is_real_bo(bo) ? get_real_bo(bo) : get_slab_bo(bo)->real;

   if (real->is_user_ptr)
      return;

   assert(real->map_count != 0 && "too many unmaps");
   if (p_atomic_dec_zero(&real->map_count)) {
      assert(!real->cpu_ptr &&
             "too many unmaps or forgot RADEON_MAP_TEMPORARY flag");

      if (real->b.base.placement & RADEON_DOMAIN_VRAM)
         ws->mapped_vram -= real->b.base.size;
      else if (real->b.base.placement & RADEON_DOMAIN_GTT)
         ws->mapped_gtt -= real->b.base.size;
      ws->num_mapped_buffers--;
   }

   amdgpu_bo_cpu_unmap(real->bo);
}

static const struct pb_vtbl amdgpu_winsys_bo_vtbl = {
   /* Cast to void* because one of the function parameters is a struct pointer instead of void*. */
   (void*)amdgpu_bo_destroy_or_cache
   /* other functions are never called */
};

static void amdgpu_add_buffer_to_global_list(struct amdgpu_winsys *ws, struct amdgpu_bo_real *bo)
{
#if DEBUG
   if (ws->debug_all_bos) {
      simple_mtx_lock(&ws->global_bo_list_lock);
      list_addtail(&bo->global_list_item, &ws->global_bo_list);
      ws->num_buffers++;
      simple_mtx_unlock(&ws->global_bo_list_lock);
   }
#endif
}

static unsigned amdgpu_get_optimal_alignment(struct amdgpu_winsys *ws,
                                             uint64_t size, unsigned alignment)
{
   /* Increase the alignment for faster address translation and better memory
    * access pattern.
    */
   if (size >= ws->info.pte_fragment_size) {
      alignment = MAX2(alignment, ws->info.pte_fragment_size);
   } else if (size) {
      unsigned msb = util_last_bit(size);

      alignment = MAX2(alignment, 1u << (msb - 1));
   }
   return alignment;
}

static struct amdgpu_winsys_bo *amdgpu_create_bo(struct amdgpu_winsys *ws,
                                                 uint64_t size,
                                                 unsigned alignment,
                                                 enum radeon_bo_domain initial_domain,
                                                 unsigned flags,
                                                 int heap)
{
   struct amdgpu_bo_alloc_request request = {0};
   amdgpu_bo_handle buf_handle;
   uint64_t va = 0;
   struct amdgpu_bo_real *bo;
   amdgpu_va_handle va_handle = NULL;
   int r;

   /* VRAM or GTT must be specified, but not both at the same time. */
   assert(util_bitcount(initial_domain & (RADEON_DOMAIN_VRAM_GTT |
                                          RADEON_DOMAIN_GDS |
                                          RADEON_DOMAIN_OA)) == 1);

   alignment = amdgpu_get_optimal_alignment(ws, size, alignment);

   if (heap >= 0 && flags & RADEON_FLAG_NO_INTERPROCESS_SHARING) {
      struct amdgpu_bo_real_reusable *new_bo = CALLOC_STRUCT(amdgpu_bo_real_reusable);
      if (!new_bo)
         return NULL;

      bo = &new_bo->b;
      pb_cache_init_entry(&ws->bo_cache, &new_bo->cache_entry, &bo->b.base, heap);
      bo->b.type = AMDGPU_BO_REAL_REUSABLE;
   } else {
      bo = CALLOC_STRUCT(amdgpu_bo_real);
      if (!bo)
         return NULL;

      bo->b.type = AMDGPU_BO_REAL;
   }

   request.alloc_size = size;
   request.phys_alignment = alignment;

   if (initial_domain & RADEON_DOMAIN_VRAM) {
      request.preferred_heap |= AMDGPU_GEM_DOMAIN_VRAM;

      /* Since VRAM and GTT have almost the same performance on APUs, we could
       * just set GTT. However, in order to decrease GTT(RAM) usage, which is
       * shared with the OS, allow VRAM placements too. The idea is not to use
       * VRAM usefully, but to use it so that it's not unused and wasted.
       */
      if (!ws->info.has_dedicated_vram)
         request.preferred_heap |= AMDGPU_GEM_DOMAIN_GTT;
   }

   if (initial_domain & RADEON_DOMAIN_GTT)
      request.preferred_heap |= AMDGPU_GEM_DOMAIN_GTT;
   if (initial_domain & RADEON_DOMAIN_GDS)
      request.preferred_heap |= AMDGPU_GEM_DOMAIN_GDS;
   if (initial_domain & RADEON_DOMAIN_OA)
      request.preferred_heap |= AMDGPU_GEM_DOMAIN_OA;

   if (flags & RADEON_FLAG_NO_CPU_ACCESS)
      request.flags |= AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
   if (flags & RADEON_FLAG_GTT_WC)
      request.flags |= AMDGPU_GEM_CREATE_CPU_GTT_USWC;

   if (flags & RADEON_FLAG_DISCARDABLE &&
       ws->info.drm_minor >= 47)
      request.flags |= AMDGPU_GEM_CREATE_DISCARDABLE;

   if (ws->zero_all_vram_allocs &&
       (request.preferred_heap & AMDGPU_GEM_DOMAIN_VRAM))
      request.flags |= AMDGPU_GEM_CREATE_VRAM_CLEARED;

   if ((flags & RADEON_FLAG_ENCRYPTED) &&
       ws->info.has_tmz_support) {
      request.flags |= AMDGPU_GEM_CREATE_ENCRYPTED;

      if (!(flags & RADEON_FLAG_DRIVER_INTERNAL)) {
         struct amdgpu_screen_winsys *sws_iter;
         simple_mtx_lock(&ws->sws_list_lock);
         for (sws_iter = ws->sws_list; sws_iter; sws_iter = sws_iter->next) {
            *((bool*) &sws_iter->base.uses_secure_bos) = true;
         }
         simple_mtx_unlock(&ws->sws_list_lock);
      }
   }

   r = amdgpu_bo_alloc(ws->dev, &request, &buf_handle);
   if (r) {
      fprintf(stderr, "amdgpu: Failed to allocate a buffer:\n");
      fprintf(stderr, "amdgpu:    size      : %"PRIu64" bytes\n", size);
      fprintf(stderr, "amdgpu:    alignment : %u bytes\n", alignment);
      fprintf(stderr, "amdgpu:    domains   : %u\n", initial_domain);
      fprintf(stderr, "amdgpu:    flags   : %" PRIx64 "\n", request.flags);
      goto error_bo_alloc;
   }

   if (initial_domain & RADEON_DOMAIN_VRAM_GTT) {
      unsigned va_gap_size = ws->check_vm ? MAX2(4 * alignment, 64 * 1024) : 0;

      r = amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
                                size + va_gap_size, alignment,
                                0, &va, &va_handle,
                                (flags & RADEON_FLAG_32BIT ? AMDGPU_VA_RANGE_32_BIT : 0) |
                                AMDGPU_VA_RANGE_HIGH);
      if (r)
         goto error_va_alloc;

      unsigned vm_flags = AMDGPU_VM_PAGE_READABLE |
                          AMDGPU_VM_PAGE_EXECUTABLE;

      if (!(flags & RADEON_FLAG_READ_ONLY))
         vm_flags |= AMDGPU_VM_PAGE_WRITEABLE;

      if (flags & RADEON_FLAG_GL2_BYPASS)
         vm_flags |= AMDGPU_VM_MTYPE_UC;

      r = amdgpu_bo_va_op_raw(ws->dev, buf_handle, 0, size, va, vm_flags,
			   AMDGPU_VA_OP_MAP);
      if (r)
         goto error_va_map;
   }

   simple_mtx_init(&bo->lock, mtx_plain);
   pipe_reference_init(&bo->b.base.reference, 1);
   bo->b.base.placement = initial_domain;
   bo->b.base.alignment_log2 = util_logbase2(alignment);
   bo->b.base.usage = flags;
   bo->b.base.size = size;
   bo->b.base.vtbl = &amdgpu_winsys_bo_vtbl;
   bo->b.va = va;
   bo->b.unique_id = __sync_fetch_and_add(&ws->next_bo_unique_id, 1);
   bo->bo = buf_handle;
   bo->va_handle = va_handle;

   if (initial_domain & RADEON_DOMAIN_VRAM)
      ws->allocated_vram += align64(size, ws->info.gart_page_size);
   else if (initial_domain & RADEON_DOMAIN_GTT)
      ws->allocated_gtt += align64(size, ws->info.gart_page_size);

   amdgpu_bo_export(bo->bo, amdgpu_bo_handle_type_kms, &bo->kms_handle);
   amdgpu_add_buffer_to_global_list(ws, bo);

   return &bo->b;

error_va_map:
   amdgpu_va_range_free(va_handle);

error_va_alloc:
   amdgpu_bo_free(buf_handle);

error_bo_alloc:
   FREE(bo);
   return NULL;
}

bool amdgpu_bo_can_reclaim(struct amdgpu_winsys *ws, struct pb_buffer *_buf)
{
   return amdgpu_bo_wait(&ws->dummy_ws.base, _buf, 0, RADEON_USAGE_READWRITE);
}

bool amdgpu_bo_can_reclaim_slab(void *priv, struct pb_slab_entry *entry)
{
   struct amdgpu_bo_slab *bo = container_of(entry, struct amdgpu_bo_slab, entry);

   return amdgpu_bo_can_reclaim(priv, &bo->b.base);
}

static struct pb_slabs *get_slabs(struct amdgpu_winsys *ws, uint64_t size)
{
   /* Find the correct slab allocator for the given size. */
   for (unsigned i = 0; i < NUM_SLAB_ALLOCATORS; i++) {
      struct pb_slabs *slabs = &ws->bo_slabs[i];

      if (size <= 1 << (slabs->min_order + slabs->num_orders - 1))
         return slabs;
   }

   assert(0);
   return NULL;
}

static unsigned get_slab_wasted_size(struct amdgpu_winsys *ws, struct amdgpu_bo_slab *bo)
{
   assert(bo->b.base.size <= bo->entry.entry_size);
   assert(bo->b.base.size < (1 << bo->b.base.alignment_log2) ||
          bo->b.base.size < 1 << ws->bo_slabs[0].min_order ||
          bo->b.base.size > bo->entry.entry_size / 2);
   return bo->entry.entry_size - bo->b.base.size;
}

static void amdgpu_bo_slab_destroy(struct radeon_winsys *rws, struct pb_buffer *_buf)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_bo_slab *bo = get_slab_bo(amdgpu_winsys_bo(_buf));
   struct pb_slabs *slabs;

   slabs = get_slabs(ws, bo->b.base.size);

   if (bo->b.base.placement & RADEON_DOMAIN_VRAM)
      ws->slab_wasted_vram -= get_slab_wasted_size(ws, bo);
   else
      ws->slab_wasted_gtt -= get_slab_wasted_size(ws, bo);

   pb_slab_free(slabs, &bo->entry);
}

static const struct pb_vtbl amdgpu_winsys_bo_slab_vtbl = {
   /* Cast to void* because one of the function parameters is a struct pointer instead of void*. */
   (void*)amdgpu_bo_slab_destroy
   /* other functions are never called */
};

/* Return the power of two size of a slab entry matching the input size. */
static unsigned get_slab_pot_entry_size(struct amdgpu_winsys *ws, unsigned size)
{
   unsigned entry_size = util_next_power_of_two(size);
   unsigned min_entry_size = 1 << ws->bo_slabs[0].min_order;

   return MAX2(entry_size, min_entry_size);
}

/* Return the slab entry alignment. */
static unsigned get_slab_entry_alignment(struct amdgpu_winsys *ws, unsigned size)
{
   unsigned entry_size = get_slab_pot_entry_size(ws, size);

   if (size <= entry_size * 3 / 4)
      return entry_size / 4;

   return entry_size;
}

struct pb_slab *amdgpu_bo_slab_alloc(void *priv, unsigned heap, unsigned entry_size,
                                     unsigned group_index)
{
   struct amdgpu_winsys *ws = priv;
   struct amdgpu_slab *slab = CALLOC_STRUCT(amdgpu_slab);
   enum radeon_bo_domain domains = radeon_domain_from_heap(heap);
   enum radeon_bo_flag flags = radeon_flags_from_heap(heap);
   uint32_t base_id;
   unsigned slab_size = 0;

   if (!slab)
      return NULL;

   /* Determine the slab buffer size. */
   for (unsigned i = 0; i < NUM_SLAB_ALLOCATORS; i++) {
      unsigned max_entry_size = 1 << (ws->bo_slabs[i].min_order + ws->bo_slabs[i].num_orders - 1);

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

         /* The largest slab should have the same size as the PTE fragment
          * size to get faster address translation.
          */
         if (i == NUM_SLAB_ALLOCATORS - 1 &&
             slab_size < ws->info.pte_fragment_size)
            slab_size = ws->info.pte_fragment_size;
         break;
      }
   }
   assert(slab_size != 0);

   slab->buffer = amdgpu_winsys_bo(amdgpu_bo_create(ws,
                                                    slab_size, slab_size,
                                                    domains, flags));
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

   base_id = __sync_fetch_and_add(&ws->next_bo_unique_id, slab->base.num_entries);

   for (unsigned i = 0; i < slab->base.num_entries; ++i) {
      struct amdgpu_bo_slab *bo = &slab->entries[i];

      bo->b.base.placement = domains;
      bo->b.base.alignment_log2 = util_logbase2(get_slab_entry_alignment(ws, entry_size));
      bo->b.base.size = entry_size;
      bo->b.base.vtbl = &amdgpu_winsys_bo_slab_vtbl;
      bo->b.type = AMDGPU_BO_SLAB;
      bo->b.va = slab->buffer->va + i * entry_size;
      bo->b.unique_id = base_id + i;

      if (is_real_bo(slab->buffer)) {
         /* The slab is not suballocated. */
         bo->real = get_real_bo(slab->buffer);
      } else {
         /* The slab is allocated out of a bigger slab. */
         bo->real = get_slab_bo(slab->buffer)->real;
      }

      bo->entry.slab = &slab->base;
      bo->entry.group_index = group_index;
      bo->entry.entry_size = entry_size;
      list_addtail(&bo->entry.head, &slab->base.free);
   }

   /* Wasted alignment due to slabs with 3/4 allocations being aligned to a power of two. */
   assert(slab->base.num_entries * entry_size <= slab_size);
   if (domains & RADEON_DOMAIN_VRAM)
      ws->slab_wasted_vram += slab_size - slab->base.num_entries * entry_size;
   else
      ws->slab_wasted_gtt += slab_size - slab->base.num_entries * entry_size;

   return &slab->base;

fail_buffer:
   amdgpu_winsys_bo_reference(ws, &slab->buffer, NULL);
fail:
   FREE(slab);
   return NULL;
}

void amdgpu_bo_slab_free(struct amdgpu_winsys *ws, struct pb_slab *pslab)
{
   struct amdgpu_slab *slab = amdgpu_slab(pslab);
   unsigned slab_size = slab->buffer->base.size;

   assert(slab->base.num_entries * slab->entry_size <= slab_size);
   if (slab->buffer->base.placement & RADEON_DOMAIN_VRAM)
      ws->slab_wasted_vram -= slab_size - slab->base.num_entries * slab->entry_size;
   else
      ws->slab_wasted_gtt -= slab_size - slab->base.num_entries * slab->entry_size;

   for (unsigned i = 0; i < slab->base.num_entries; ++i)
      amdgpu_bo_remove_fences(&slab->entries[i].b);

   FREE(slab->entries);
   amdgpu_winsys_bo_reference(ws, &slab->buffer, NULL);
   FREE(slab);
}

#if DEBUG_SPARSE_COMMITS
static void
sparse_dump(struct amdgpu_bo_sparse *bo, const char *func)
{
   fprintf(stderr, "%s: %p (size=%"PRIu64", num_va_pages=%u) @ %s\n"
                   "Commitments:\n",
           __func__, bo, bo->b.base.size, bo->num_va_pages, func);

   struct amdgpu_sparse_backing *span_backing = NULL;
   uint32_t span_first_backing_page = 0;
   uint32_t span_first_va_page = 0;
   uint32_t va_page = 0;

   for (;;) {
      struct amdgpu_sparse_backing *backing = 0;
      uint32_t backing_page = 0;

      if (va_page < bo->num_va_pages) {
         backing = bo->commitments[va_page].backing;
         backing_page = bo->commitments[va_page].page;
      }

      if (span_backing &&
          (backing != span_backing ||
           backing_page != span_first_backing_page + (va_page - span_first_va_page))) {
         fprintf(stderr, " %u..%u: backing=%p:%u..%u\n",
                 span_first_va_page, va_page - 1, span_backing,
                 span_first_backing_page,
                 span_first_backing_page + (va_page - span_first_va_page) - 1);

         span_backing = NULL;
      }

      if (va_page >= bo->num_va_pages)
         break;

      if (backing && !span_backing) {
         span_backing = backing;
         span_first_backing_page = backing_page;
         span_first_va_page = va_page;
      }

      va_page++;
   }

   fprintf(stderr, "Backing:\n");

   list_for_each_entry(struct amdgpu_sparse_backing, backing, &bo->backing, list) {
      fprintf(stderr, " %p (size=%"PRIu64")\n", backing, backing->bo->b.base.size);
      for (unsigned i = 0; i < backing->num_chunks; ++i)
         fprintf(stderr, "   %u..%u\n", backing->chunks[i].begin, backing->chunks[i].end);
   }
}
#endif

/*
 * Attempt to allocate the given number of backing pages. Fewer pages may be
 * allocated (depending on the fragmentation of existing backing buffers),
 * which will be reflected by a change to *pnum_pages.
 */
static struct amdgpu_sparse_backing *
sparse_backing_alloc(struct amdgpu_winsys *ws, struct amdgpu_bo_sparse *bo,
                     uint32_t *pstart_page, uint32_t *pnum_pages)
{
   struct amdgpu_sparse_backing *best_backing;
   unsigned best_idx;
   uint32_t best_num_pages;

   best_backing = NULL;
   best_idx = 0;
   best_num_pages = 0;

   /* This is a very simple and inefficient best-fit algorithm. */
   list_for_each_entry(struct amdgpu_sparse_backing, backing, &bo->backing, list) {
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

      best_backing = CALLOC_STRUCT(amdgpu_sparse_backing);
      if (!best_backing)
         return NULL;

      best_backing->max_chunks = 4;
      best_backing->chunks = CALLOC(best_backing->max_chunks,
                                    sizeof(*best_backing->chunks));
      if (!best_backing->chunks) {
         FREE(best_backing);
         return NULL;
      }

      assert(bo->num_backing_pages < DIV_ROUND_UP(bo->b.base.size, RADEON_SPARSE_PAGE_SIZE));

      size = MIN3(bo->b.base.size / 16,
                  8 * 1024 * 1024,
                  bo->b.base.size - (uint64_t)bo->num_backing_pages * RADEON_SPARSE_PAGE_SIZE);
      size = MAX2(size, RADEON_SPARSE_PAGE_SIZE);

      buf = amdgpu_bo_create(ws, size, RADEON_SPARSE_PAGE_SIZE,
                             bo->b.base.placement,
                             (bo->b.base.usage & ~RADEON_FLAG_SPARSE &
                              /* Set the interprocess sharing flag to disable pb_cache because
                               * amdgpu_bo_wait doesn't wait for active CS jobs.
                               */
                              ~RADEON_FLAG_NO_INTERPROCESS_SHARING) | RADEON_FLAG_NO_SUBALLOC);
      if (!buf) {
         FREE(best_backing->chunks);
         FREE(best_backing);
         return NULL;
      }

      /* We might have gotten a bigger buffer than requested via caching. */
      pages = buf->size / RADEON_SPARSE_PAGE_SIZE;

      best_backing->bo = get_real_bo(amdgpu_winsys_bo(buf));
      best_backing->num_chunks = 1;
      best_backing->chunks[0].begin = 0;
      best_backing->chunks[0].end = pages;

      list_add(&best_backing->list, &bo->backing);
      bo->num_backing_pages += pages;

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
sparse_free_backing_buffer(struct amdgpu_winsys *ws, struct amdgpu_bo_sparse *bo,
                           struct amdgpu_sparse_backing *backing)
{
   bo->num_backing_pages -= backing->bo->b.base.size / RADEON_SPARSE_PAGE_SIZE;

   simple_mtx_lock(&ws->bo_fence_lock);
   amdgpu_add_fences(&backing->bo->b, bo->b.num_fences, bo->b.fences);
   simple_mtx_unlock(&ws->bo_fence_lock);

   list_del(&backing->list);
   amdgpu_winsys_bo_reference(ws, (struct amdgpu_winsys_bo**)&backing->bo, NULL);
   FREE(backing->chunks);
   FREE(backing);
}

/*
 * Return a range of pages from the given backing buffer back into the
 * free structure.
 */
static bool
sparse_backing_free(struct amdgpu_winsys *ws, struct amdgpu_bo_sparse *bo,
                    struct amdgpu_sparse_backing *backing,
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
         struct amdgpu_sparse_backing_chunk *new_chunks =
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
       backing->chunks[0].end == backing->bo->b.base.size / RADEON_SPARSE_PAGE_SIZE)
      sparse_free_backing_buffer(ws, bo, backing);

   return true;
}

static void amdgpu_bo_sparse_destroy(struct radeon_winsys *rws, struct pb_buffer *_buf)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_bo_sparse *bo = get_sparse_bo(amdgpu_winsys_bo(_buf));
   int r;

   r = amdgpu_bo_va_op_raw(ws->dev, NULL, 0,
                           (uint64_t)bo->num_va_pages * RADEON_SPARSE_PAGE_SIZE,
                           bo->b.va, 0, AMDGPU_VA_OP_CLEAR);
   if (r) {
      fprintf(stderr, "amdgpu: clearing PRT VA region on destroy failed (%d)\n", r);
   }

   while (!list_is_empty(&bo->backing)) {
      sparse_free_backing_buffer(ws, bo,
                                 container_of(bo->backing.next,
                                              struct amdgpu_sparse_backing, list));
   }

   amdgpu_va_range_free(bo->va_handle);
   FREE(bo->commitments);
   simple_mtx_destroy(&bo->lock);
   FREE(bo);
}

static const struct pb_vtbl amdgpu_winsys_bo_sparse_vtbl = {
   /* Cast to void* because one of the function parameters is a struct pointer instead of void*. */
   (void*)amdgpu_bo_sparse_destroy
   /* other functions are never called */
};

static struct pb_buffer *
amdgpu_bo_sparse_create(struct amdgpu_winsys *ws, uint64_t size,
                        enum radeon_bo_domain domain,
                        enum radeon_bo_flag flags)
{
   struct amdgpu_bo_sparse *bo;
   uint64_t map_size;
   uint64_t va_gap_size;
   int r;

   /* We use 32-bit page numbers; refuse to attempt allocating sparse buffers
    * that exceed this limit. This is not really a restriction: we don't have
    * that much virtual address space anyway.
    */
   if (size > (uint64_t)INT32_MAX * RADEON_SPARSE_PAGE_SIZE)
      return NULL;

   bo = CALLOC_STRUCT(amdgpu_bo_sparse);
   if (!bo)
      return NULL;

   simple_mtx_init(&bo->lock, mtx_plain);
   pipe_reference_init(&bo->b.base.reference, 1);
   bo->b.base.placement = domain;
   bo->b.base.alignment_log2 = util_logbase2(RADEON_SPARSE_PAGE_SIZE);
   bo->b.base.usage = flags;
   bo->b.base.size = size;
   bo->b.base.vtbl = &amdgpu_winsys_bo_sparse_vtbl;
   bo->b.unique_id =  __sync_fetch_and_add(&ws->next_bo_unique_id, 1);
   bo->b.type = AMDGPU_BO_SPARSE;

   bo->num_va_pages = DIV_ROUND_UP(size, RADEON_SPARSE_PAGE_SIZE);
   bo->commitments = CALLOC(bo->num_va_pages, sizeof(*bo->commitments));
   if (!bo->commitments)
      goto error_alloc_commitments;

   list_inithead(&bo->backing);

   /* For simplicity, we always map a multiple of the page size. */
   map_size = align64(size, RADEON_SPARSE_PAGE_SIZE);
   va_gap_size = ws->check_vm ? 4 * RADEON_SPARSE_PAGE_SIZE : 0;
   r = amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
                             map_size + va_gap_size, RADEON_SPARSE_PAGE_SIZE,
                             0, &bo->b.va, &bo->va_handle,
			     AMDGPU_VA_RANGE_HIGH);
   if (r)
      goto error_va_alloc;

   r = amdgpu_bo_va_op_raw(ws->dev, NULL, 0, map_size, bo->b.va,
                           AMDGPU_VM_PAGE_PRT, AMDGPU_VA_OP_MAP);
   if (r)
      goto error_va_map;

   return &bo->b.base;

error_va_map:
   amdgpu_va_range_free(bo->va_handle);
error_va_alloc:
   FREE(bo->commitments);
error_alloc_commitments:
   simple_mtx_destroy(&bo->lock);
   FREE(bo);
   return NULL;
}

static bool
amdgpu_bo_sparse_commit(struct radeon_winsys *rws, struct pb_buffer *buf,
                        uint64_t offset, uint64_t size, bool commit)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_bo_sparse *bo = get_sparse_bo(amdgpu_winsys_bo(buf));
   struct amdgpu_sparse_commitment *comm;
   uint32_t va_page, end_va_page;
   bool ok = true;
   int r;

   assert(offset % RADEON_SPARSE_PAGE_SIZE == 0);
   assert(offset <= bo->b.base.size);
   assert(size <= bo->b.base.size - offset);
   assert(size % RADEON_SPARSE_PAGE_SIZE == 0 || offset + size == bo->b.base.size);

   comm = bo->commitments;
   va_page = offset / RADEON_SPARSE_PAGE_SIZE;
   end_va_page = va_page + DIV_ROUND_UP(size, RADEON_SPARSE_PAGE_SIZE);

   simple_mtx_lock(&bo->lock);

#if DEBUG_SPARSE_COMMITS
   sparse_dump(bo, __func__);
#endif

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
            struct amdgpu_sparse_backing *backing;
            uint32_t backing_start, backing_size;

            backing_size = va_page - span_va_page;
            backing = sparse_backing_alloc(ws, bo, &backing_start, &backing_size);
            if (!backing) {
               ok = false;
               goto out;
            }

            r = amdgpu_bo_va_op_raw(ws->dev, backing->bo->bo,
                                    (uint64_t)backing_start * RADEON_SPARSE_PAGE_SIZE,
                                    (uint64_t)backing_size * RADEON_SPARSE_PAGE_SIZE,
                                    bo->b.va + (uint64_t)span_va_page * RADEON_SPARSE_PAGE_SIZE,
                                    AMDGPU_VM_PAGE_READABLE |
                                    AMDGPU_VM_PAGE_WRITEABLE |
                                    AMDGPU_VM_PAGE_EXECUTABLE,
                                    AMDGPU_VA_OP_REPLACE);
            if (r) {
               ok = sparse_backing_free(ws, bo, backing, backing_start, backing_size);
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
      r = amdgpu_bo_va_op_raw(ws->dev, NULL, 0,
                              (uint64_t)(end_va_page - va_page) * RADEON_SPARSE_PAGE_SIZE,
                              bo->b.va + (uint64_t)va_page * RADEON_SPARSE_PAGE_SIZE,
                              AMDGPU_VM_PAGE_PRT, AMDGPU_VA_OP_REPLACE);
      if (r) {
         ok = false;
         goto out;
      }

      while (va_page < end_va_page) {
         struct amdgpu_sparse_backing *backing;
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

         if (!sparse_backing_free(ws, bo, backing, backing_start, span_pages)) {
            /* Couldn't allocate tracking data structures, so we have to leak */
            fprintf(stderr, "amdgpu: leaking PRT backing memory\n");
            ok = false;
         }
      }
   }
out:

   simple_mtx_unlock(&bo->lock);

   return ok;
}

static unsigned
amdgpu_bo_find_next_committed_memory(struct pb_buffer *buf,
                                     uint64_t range_offset, unsigned *range_size)
{
   struct amdgpu_bo_sparse *bo = get_sparse_bo(amdgpu_winsys_bo(buf));
   struct amdgpu_sparse_commitment *comm;
   uint32_t va_page, end_va_page;
   uint32_t span_va_page, start_va_page;
   unsigned uncommitted_range_prev, uncommitted_range_next;

   if (*range_size == 0)
      return 0;

   assert(*range_size + range_offset <= bo->b.base.size);

   uncommitted_range_prev = uncommitted_range_next = 0;
   comm = bo->commitments;
   start_va_page = va_page = range_offset / RADEON_SPARSE_PAGE_SIZE;
   end_va_page = (*range_size + range_offset) / RADEON_SPARSE_PAGE_SIZE;

   simple_mtx_lock(&bo->lock);
   /* Lookup the first committed page with backing physical storage */
   while (va_page < end_va_page && !comm[va_page].backing)
      va_page++;

   /* Fisrt committed page lookup failed, return early. */
   if (va_page == end_va_page && !comm[va_page].backing) {
      uncommitted_range_prev = *range_size;
      *range_size = 0;
      simple_mtx_unlock(&bo->lock);
      return uncommitted_range_prev;
   }

   /* Lookup the first uncommitted page without backing physical storage */
   span_va_page = va_page;
   while (va_page < end_va_page && comm[va_page].backing)
      va_page++;
   simple_mtx_unlock(&bo->lock);

   /* Calc byte count that need to skip before committed range */
   if (span_va_page != start_va_page)
      uncommitted_range_prev = span_va_page * RADEON_SPARSE_PAGE_SIZE - range_offset;

   /* Calc byte count that need to skip after committed range */
   if (va_page != end_va_page || !comm[va_page].backing) {
      uncommitted_range_next = *range_size + range_offset - va_page * RADEON_SPARSE_PAGE_SIZE;
   }

   /* Calc size of first committed part */
   *range_size = *range_size - uncommitted_range_next - uncommitted_range_prev;
   return *range_size ? uncommitted_range_prev
	   : uncommitted_range_prev + uncommitted_range_next;
}

static void amdgpu_buffer_get_metadata(struct radeon_winsys *rws,
                                       struct pb_buffer *_buf,
                                       struct radeon_bo_metadata *md,
                                       struct radeon_surf *surf)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_bo_real *bo = get_real_bo(amdgpu_winsys_bo(_buf));
   struct amdgpu_bo_info info = {0};
   int r;

   r = amdgpu_bo_query_info(bo->bo, &info);
   if (r)
      return;

   ac_surface_apply_bo_metadata(&ws->info, surf, info.metadata.tiling_info,
                                &md->mode);

   md->size_metadata = info.metadata.size_metadata;
   memcpy(md->metadata, info.metadata.umd_metadata, sizeof(md->metadata));
}

static void amdgpu_buffer_set_metadata(struct radeon_winsys *rws,
                                       struct pb_buffer *_buf,
                                       struct radeon_bo_metadata *md,
                                       struct radeon_surf *surf)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_bo_real *bo = get_real_bo(amdgpu_winsys_bo(_buf));
   struct amdgpu_bo_metadata metadata = {0};

   ac_surface_compute_bo_metadata(&ws->info, surf, &metadata.tiling_info);

   metadata.size_metadata = md->size_metadata;
   memcpy(metadata.umd_metadata, md->metadata, sizeof(md->metadata));

   amdgpu_bo_set_metadata(bo->bo, &metadata);
}

struct pb_buffer *
amdgpu_bo_create(struct amdgpu_winsys *ws,
                 uint64_t size,
                 unsigned alignment,
                 enum radeon_bo_domain domain,
                 enum radeon_bo_flag flags)
{
   struct amdgpu_winsys_bo *bo;

   radeon_canonicalize_bo_flags(&domain, &flags);

   /* Handle sparse buffers first. */
   if (flags & RADEON_FLAG_SPARSE) {
      assert(RADEON_SPARSE_PAGE_SIZE % alignment == 0);

      return amdgpu_bo_sparse_create(ws, size, domain, flags);
   }

   struct pb_slabs *last_slab = &ws->bo_slabs[NUM_SLAB_ALLOCATORS - 1];
   unsigned max_slab_entry_size = 1 << (last_slab->min_order + last_slab->num_orders - 1);
   int heap = radeon_get_heap_index(domain, flags);

   /* Sub-allocate small buffers from slabs. */
   if (heap >= 0 && size <= max_slab_entry_size) {
      struct pb_slab_entry *entry;
      unsigned alloc_size = size;

      /* Always use slabs for sizes less than 4 KB because the kernel aligns
       * everything to 4 KB.
       */
      if (size < alignment && alignment <= 4 * 1024)
         alloc_size = alignment;

      if (alignment > get_slab_entry_alignment(ws, alloc_size)) {
         /* 3/4 allocations can return too small alignment. Try again with a power of two
          * allocation size.
          */
         unsigned pot_size = get_slab_pot_entry_size(ws, alloc_size);

         if (alignment <= pot_size) {
            /* This size works but wastes some memory to fulfil the alignment. */
            alloc_size = pot_size;
         } else {
            goto no_slab; /* can't fulfil alignment requirements */
         }
      }

      struct pb_slabs *slabs = get_slabs(ws, alloc_size);
      entry = pb_slab_alloc(slabs, alloc_size, heap);
      if (!entry) {
         /* Clean up buffer managers and try again. */
         amdgpu_clean_up_buffer_managers(ws);

         entry = pb_slab_alloc(slabs, alloc_size, heap);
      }
      if (!entry)
         return NULL;

      struct amdgpu_bo_slab *slab_bo = container_of(entry, struct amdgpu_bo_slab, entry);
      pipe_reference_init(&slab_bo->b.base.reference, 1);
      slab_bo->b.base.size = size;
      assert(alignment <= 1 << slab_bo->b.base.alignment_log2);

      if (domain & RADEON_DOMAIN_VRAM)
         ws->slab_wasted_vram += get_slab_wasted_size(ws, slab_bo);
      else
         ws->slab_wasted_gtt += get_slab_wasted_size(ws, slab_bo);

      return &slab_bo->b.base;
   }
no_slab:

   /* Align size to page size. This is the minimum alignment for normal
    * BOs. Aligning this here helps the cached bufmgr. Especially small BOs,
    * like constant/uniform buffers, can benefit from better and more reuse.
    */
   if (domain & RADEON_DOMAIN_VRAM_GTT) {
      size = align64(size, ws->info.gart_page_size);
      alignment = align(alignment, ws->info.gart_page_size);
   }

   bool use_reusable_pool = flags & RADEON_FLAG_NO_INTERPROCESS_SHARING &&
                            !(flags & RADEON_FLAG_DISCARDABLE);

   if (use_reusable_pool) {
       /* RADEON_FLAG_NO_SUBALLOC is irrelevant for the cache. */
       heap = radeon_get_heap_index(domain, flags & ~RADEON_FLAG_NO_SUBALLOC);
       assert(heap >= 0 && heap < RADEON_NUM_HEAPS);

       /* Get a buffer from the cache. */
       bo = (struct amdgpu_winsys_bo*)
            pb_cache_reclaim_buffer(&ws->bo_cache, size, alignment, 0, heap);
       if (bo)
          return &bo->base;
   }

   /* Create a new one. */
   bo = amdgpu_create_bo(ws, size, alignment, domain, flags, heap);
   if (!bo) {
      /* Clean up buffer managers and try again. */
      amdgpu_clean_up_buffer_managers(ws);

      bo = amdgpu_create_bo(ws, size, alignment, domain, flags, heap);
      if (!bo)
         return NULL;
   }

   return &bo->base;
}

static struct pb_buffer *
amdgpu_buffer_create(struct radeon_winsys *ws,
                     uint64_t size,
                     unsigned alignment,
                     enum radeon_bo_domain domain,
                     enum radeon_bo_flag flags)
{
   struct pb_buffer * res = amdgpu_bo_create(amdgpu_winsys(ws), size, alignment, domain,
                           flags);
   return res;
}

static struct pb_buffer *amdgpu_bo_from_handle(struct radeon_winsys *rws,
                                               struct winsys_handle *whandle,
                                               unsigned vm_alignment,
                                               bool is_prime_linear_buffer)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_bo_real *bo = NULL;
   enum amdgpu_bo_handle_type type;
   struct amdgpu_bo_import_result result = {0};
   uint64_t va;
   amdgpu_va_handle va_handle = NULL;
   struct amdgpu_bo_info info = {0};
   enum radeon_bo_domain initial = 0;
   enum radeon_bo_flag flags = 0;
   int r;

   switch (whandle->type) {
   case WINSYS_HANDLE_TYPE_SHARED:
      type = amdgpu_bo_handle_type_gem_flink_name;
      break;
   case WINSYS_HANDLE_TYPE_FD:
      type = amdgpu_bo_handle_type_dma_buf_fd;
      break;
   default:
      return NULL;
   }

   r = amdgpu_bo_import(ws->dev, type, whandle->handle, &result);
   if (r)
      return NULL;

   simple_mtx_lock(&ws->bo_export_table_lock);
   bo = util_hash_table_get(ws->bo_export_table, result.buf_handle);

   /* If the amdgpu_winsys_bo instance already exists, bump the reference
    * counter and return it.
    */
   if (bo) {
      p_atomic_inc(&bo->b.base.reference.count);
      simple_mtx_unlock(&ws->bo_export_table_lock);

      /* Release the buffer handle, because we don't need it anymore.
       * This function is returning an existing buffer, which has its own
       * handle.
       */
      amdgpu_bo_free(result.buf_handle);
      return &bo->b.base;
   }

   /* Get initial domains. */
   r = amdgpu_bo_query_info(result.buf_handle, &info);
   if (r)
      goto error;

   r = amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
                             result.alloc_size,
                             amdgpu_get_optimal_alignment(ws, result.alloc_size,
                                                          vm_alignment),
                             0, &va, &va_handle, AMDGPU_VA_RANGE_HIGH);
   if (r)
      goto error;

   bo = CALLOC_STRUCT(amdgpu_bo_real);
   if (!bo)
      goto error;

   r = amdgpu_bo_va_op_raw(ws->dev, result.buf_handle, 0, result.alloc_size, va,
                           AMDGPU_VM_PAGE_READABLE | AMDGPU_VM_PAGE_WRITEABLE |
                           AMDGPU_VM_PAGE_EXECUTABLE |
                           (is_prime_linear_buffer ? AMDGPU_VM_MTYPE_UC : 0),
                           AMDGPU_VA_OP_MAP);
   if (r)
      goto error;

   if (info.preferred_heap & AMDGPU_GEM_DOMAIN_VRAM)
      initial |= RADEON_DOMAIN_VRAM;
   if (info.preferred_heap & AMDGPU_GEM_DOMAIN_GTT)
      initial |= RADEON_DOMAIN_GTT;
   if (info.alloc_flags & AMDGPU_GEM_CREATE_NO_CPU_ACCESS)
      flags |= RADEON_FLAG_NO_CPU_ACCESS;
   if (info.alloc_flags & AMDGPU_GEM_CREATE_CPU_GTT_USWC)
      flags |= RADEON_FLAG_GTT_WC;
   if (info.alloc_flags & AMDGPU_GEM_CREATE_ENCRYPTED) {
      /* Imports are always possible even if the importer isn't using TMZ.
       * For instance libweston needs to import the buffer to be able to determine
       * if it can be used for scanout.
       */
      flags |= RADEON_FLAG_ENCRYPTED;
      *((bool*)&rws->uses_secure_bos) = true;
   }

   /* Initialize the structure. */
   pipe_reference_init(&bo->b.base.reference, 1);
   bo->b.base.placement = initial;
   bo->b.base.alignment_log2 = util_logbase2(info.phys_alignment ?
				info.phys_alignment : ws->info.gart_page_size);
   bo->b.base.usage = flags;
   bo->b.base.size = result.alloc_size;
   bo->b.base.vtbl = &amdgpu_winsys_bo_vtbl;
   bo->b.type = AMDGPU_BO_REAL;
   bo->b.va = va;
   bo->b.unique_id = __sync_fetch_and_add(&ws->next_bo_unique_id, 1);
   simple_mtx_init(&bo->lock, mtx_plain);
   bo->bo = result.buf_handle;
   bo->va_handle = va_handle;
   bo->is_shared = true;

   if (bo->b.base.placement & RADEON_DOMAIN_VRAM)
      ws->allocated_vram += align64(bo->b.base.size, ws->info.gart_page_size);
   else if (bo->b.base.placement & RADEON_DOMAIN_GTT)
      ws->allocated_gtt += align64(bo->b.base.size, ws->info.gart_page_size);

   amdgpu_bo_export(bo->bo, amdgpu_bo_handle_type_kms, &bo->kms_handle);

   amdgpu_add_buffer_to_global_list(ws, bo);

   _mesa_hash_table_insert(ws->bo_export_table, bo->bo, bo);
   simple_mtx_unlock(&ws->bo_export_table_lock);

   return &bo->b.base;

error:
   simple_mtx_unlock(&ws->bo_export_table_lock);
   if (bo)
      FREE(bo);
   if (va_handle)
      amdgpu_va_range_free(va_handle);
   amdgpu_bo_free(result.buf_handle);
   return NULL;
}

static bool amdgpu_bo_get_handle(struct radeon_winsys *rws,
                                 struct pb_buffer *buffer,
                                 struct winsys_handle *whandle)
{
   struct amdgpu_screen_winsys *sws = amdgpu_screen_winsys(rws);
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   enum amdgpu_bo_handle_type type;
   struct hash_entry *entry;
   int r;

   /* Don't allow exports of slab entries and sparse buffers. */
   if (!is_real_bo(amdgpu_winsys_bo(buffer)))
      return false;

   struct amdgpu_bo_real *bo = get_real_bo(amdgpu_winsys_bo(buffer));

   /* This removes the REUSABLE enum if it's set. */
   bo->b.type = AMDGPU_BO_REAL;

   switch (whandle->type) {
   case WINSYS_HANDLE_TYPE_SHARED:
      type = amdgpu_bo_handle_type_gem_flink_name;
      break;
   case WINSYS_HANDLE_TYPE_KMS:
      if (sws->fd == ws->fd) {
         whandle->handle = bo->kms_handle;

         if (bo->is_shared)
            return true;

         goto hash_table_set;
      }

      simple_mtx_lock(&ws->sws_list_lock);
      entry = _mesa_hash_table_search(sws->kms_handles, bo);
      simple_mtx_unlock(&ws->sws_list_lock);
      if (entry) {
         whandle->handle = (uintptr_t)entry->data;
         return true;
      }
      FALLTHROUGH;
   case WINSYS_HANDLE_TYPE_FD:
      type = amdgpu_bo_handle_type_dma_buf_fd;
      break;
   default:
      return false;
   }

   r = amdgpu_bo_export(bo->bo, type, &whandle->handle);
   if (r)
      return false;

#if defined(DMA_BUF_SET_NAME_B)
   if (whandle->type == WINSYS_HANDLE_TYPE_FD &&
       !bo->is_shared) {
      char dmabufname[32];
      snprintf(dmabufname, 32, "%d-%s", getpid(), util_get_process_name());
      r = ioctl(whandle->handle, DMA_BUF_SET_NAME_B, (uint64_t)(uintptr_t)dmabufname);
   }
#endif

   if (whandle->type == WINSYS_HANDLE_TYPE_KMS) {
      int dma_fd = whandle->handle;

      r = drmPrimeFDToHandle(sws->fd, dma_fd, &whandle->handle);
      close(dma_fd);

      if (r)
         return false;

      simple_mtx_lock(&ws->sws_list_lock);
      _mesa_hash_table_insert_pre_hashed(sws->kms_handles,
                                         bo->kms_handle, bo,
                                         (void*)(uintptr_t)whandle->handle);
      simple_mtx_unlock(&ws->sws_list_lock);
   }

 hash_table_set:
   simple_mtx_lock(&ws->bo_export_table_lock);
   _mesa_hash_table_insert(ws->bo_export_table, bo->bo, bo);
   simple_mtx_unlock(&ws->bo_export_table_lock);

   bo->is_shared = true;
   return true;
}

static struct pb_buffer *amdgpu_bo_from_ptr(struct radeon_winsys *rws,
					    void *pointer, uint64_t size,
					    enum radeon_bo_flag flags)
{
    struct amdgpu_winsys *ws = amdgpu_winsys(rws);
    amdgpu_bo_handle buf_handle;
    struct amdgpu_bo_real *bo;
    uint64_t va;
    amdgpu_va_handle va_handle;
    /* Avoid failure when the size is not page aligned */
    uint64_t aligned_size = align64(size, ws->info.gart_page_size);

    bo = CALLOC_STRUCT(amdgpu_bo_real);
    if (!bo)
        return NULL;

    if (amdgpu_create_bo_from_user_mem(ws->dev, pointer,
                                       aligned_size, &buf_handle))
        goto error;

    if (amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
                              aligned_size,
                              amdgpu_get_optimal_alignment(ws, aligned_size,
                                                           ws->info.gart_page_size),
                              0, &va, &va_handle, AMDGPU_VA_RANGE_HIGH))
        goto error_va_alloc;

    if (amdgpu_bo_va_op(buf_handle, 0, aligned_size, va, 0, AMDGPU_VA_OP_MAP))
        goto error_va_map;

    /* Initialize it. */
    bo->is_user_ptr = true;
    pipe_reference_init(&bo->b.base.reference, 1);
    bo->b.base.placement = RADEON_DOMAIN_GTT;
    bo->b.base.alignment_log2 = 0;
    bo->b.base.size = size;
    bo->b.base.vtbl = &amdgpu_winsys_bo_vtbl;
    bo->b.type = AMDGPU_BO_REAL;
    bo->b.va = va;
    bo->b.unique_id = __sync_fetch_and_add(&ws->next_bo_unique_id, 1);
    simple_mtx_init(&bo->lock, mtx_plain);
    bo->bo = buf_handle;
    bo->cpu_ptr = pointer;
    bo->va_handle = va_handle;

    ws->allocated_gtt += aligned_size;

    amdgpu_add_buffer_to_global_list(ws, bo);

    amdgpu_bo_export(bo->bo, amdgpu_bo_handle_type_kms, &bo->kms_handle);

    return (struct pb_buffer*)bo;

error_va_map:
    amdgpu_va_range_free(va_handle);

error_va_alloc:
    amdgpu_bo_free(buf_handle);

error:
    FREE(bo);
    return NULL;
}

static bool amdgpu_bo_is_user_ptr(struct pb_buffer *buf)
{
   struct amdgpu_winsys_bo *bo = (struct amdgpu_winsys_bo*)buf;

   return is_real_bo(bo) ? get_real_bo(bo)->is_user_ptr : false;
}

static bool amdgpu_bo_is_suballocated(struct pb_buffer *buf)
{
   struct amdgpu_winsys_bo *bo = (struct amdgpu_winsys_bo*)buf;

   return bo->type == AMDGPU_BO_SLAB;
}

static uint64_t amdgpu_bo_get_va(struct pb_buffer *buf)
{
   return ((struct amdgpu_winsys_bo*)buf)->va;
}

void amdgpu_bo_init_functions(struct amdgpu_screen_winsys *ws)
{
   ws->base.buffer_set_metadata = amdgpu_buffer_set_metadata;
   ws->base.buffer_get_metadata = amdgpu_buffer_get_metadata;
   ws->base.buffer_map = amdgpu_bo_map;
   ws->base.buffer_unmap = amdgpu_bo_unmap;
   ws->base.buffer_wait = amdgpu_bo_wait;
   ws->base.buffer_create = amdgpu_buffer_create;
   ws->base.buffer_from_handle = amdgpu_bo_from_handle;
   ws->base.buffer_from_ptr = amdgpu_bo_from_ptr;
   ws->base.buffer_is_user_ptr = amdgpu_bo_is_user_ptr;
   ws->base.buffer_is_suballocated = amdgpu_bo_is_suballocated;
   ws->base.buffer_get_handle = amdgpu_bo_get_handle;
   ws->base.buffer_commit = amdgpu_bo_sparse_commit;
   ws->base.buffer_find_next_committed_memory = amdgpu_bo_find_next_committed_memory;
   ws->base.buffer_get_virtual_address = amdgpu_bo_get_va;
   ws->base.buffer_get_initial_domain = amdgpu_bo_get_initial_domain;
   ws->base.buffer_get_flags = amdgpu_bo_get_flags;
}
