/*
 * Copyright © 2008 Jérôme Glisse
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AMDGPU_BO_H
#define AMDGPU_BO_H

#include "amdgpu_winsys.h"

#include "pipebuffer/pb_slab.h"

struct amdgpu_sparse_backing_chunk;

/*
 * Sub-allocation information for a real buffer used as backing memory of a
 * sparse buffer.
 */
struct amdgpu_sparse_backing {
   struct list_head list;

   struct amdgpu_bo_real *bo;

   /* Sorted list of free chunks. */
   struct amdgpu_sparse_backing_chunk *chunks;
   uint32_t max_chunks;
   uint32_t num_chunks;
};

struct amdgpu_sparse_commitment {
   struct amdgpu_sparse_backing *backing;
   uint32_t page;
};

enum amdgpu_bo_type {
   AMDGPU_BO_SLAB,
   AMDGPU_BO_SPARSE,
   AMDGPU_BO_REAL, /* only REAL enums can be present after this */
   AMDGPU_BO_REAL_REUSABLE,
};

/* Anything above REAL will use the BO list for REAL. */
#define NUM_BO_LIST_TYPES (AMDGPU_BO_REAL + 1)

/* Base class of the buffer object that other structures inherit. */
struct amdgpu_winsys_bo {
   struct pb_buffer base;
   enum amdgpu_bo_type type;

   uint32_t unique_id;
   uint64_t va;

   /* how many command streams, which are being emitted in a separate
    * thread, is this bo referenced in? */
   volatile int num_active_ioctls;

   /* Fences for buffer synchronization. */
   uint16_t num_fences;
   uint16_t max_fences;
   struct pipe_fence_handle **fences;
};

/* Real GPU memory allocation managed by the amdgpu kernel driver.
 *
 * There are also types of buffers that are not "real" kernel allocations, such as slab entry
 * BOs, which are suballocated from real BOs, and sparse BOs, which initially only allocate
 * the virtual address range, not memory.
 */
struct amdgpu_bo_real {
   struct amdgpu_winsys_bo b;

   amdgpu_bo_handle bo;
   amdgpu_va_handle va_handle;
   void *cpu_ptr; /* for user_ptr and permanent maps */
   int map_count;
   uint32_t kms_handle;
#if DEBUG
   struct list_head global_list_item;
#endif
   simple_mtx_t lock;

   bool is_user_ptr;

   /* Whether buffer_get_handle or buffer_from_handle has been called,
    * it can only transition from false to true. Protected by lock.
    */
   bool is_shared;
};

/* Same as amdgpu_bo_real except this BO isn't destroyed when its reference count drops to 0.
 * Instead it's cached in pb_cache for later reuse.
 */
struct amdgpu_bo_real_reusable {
   struct amdgpu_bo_real b;
   struct pb_cache_entry cache_entry;
};

/* Sparse BO. This only allocates the virtual address range for the BO. The physical storage is
 * allocated on demand by the user using radeon_winsys::buffer_commit with 64KB granularity.
 */
struct amdgpu_bo_sparse {
   struct amdgpu_winsys_bo b;
   amdgpu_va_handle va_handle;

   uint32_t num_va_pages;
   uint32_t num_backing_pages;
   simple_mtx_t lock;

   struct list_head backing;

   /* Commitment information for each page of the virtual memory area. */
   struct amdgpu_sparse_commitment *commitments;
};

/* Suballocated buffer using the slab allocator. This BO is only 1 piece of a larger buffer
 * called slab, which is a buffer that's divided into smaller equal-sized buffers.
 */
struct amdgpu_bo_slab {
   struct amdgpu_winsys_bo b;
   struct amdgpu_bo_real *real;
   struct pb_slab_entry entry;
};

struct amdgpu_slab {
   struct pb_slab base;
   unsigned entry_size;
   struct amdgpu_winsys_bo *buffer;
   struct amdgpu_bo_slab *entries;
};

static inline bool is_real_bo(struct amdgpu_winsys_bo *bo)
{
   return bo->type >= AMDGPU_BO_REAL;
}

static struct amdgpu_bo_real *get_real_bo(struct amdgpu_winsys_bo *bo)
{
   assert(is_real_bo(bo));
   return (struct amdgpu_bo_real*)bo;
}

static struct amdgpu_bo_real_reusable *get_real_bo_reusable(struct amdgpu_winsys_bo *bo)
{
   assert(bo->type == AMDGPU_BO_REAL_REUSABLE);
   return (struct amdgpu_bo_real_reusable*)bo;
}

static struct amdgpu_bo_sparse *get_sparse_bo(struct amdgpu_winsys_bo *bo)
{
   assert(bo->type == AMDGPU_BO_SPARSE && bo->base.usage & RADEON_FLAG_SPARSE);
   return (struct amdgpu_bo_sparse*)bo;
}

static struct amdgpu_bo_slab *get_slab_bo(struct amdgpu_winsys_bo *bo)
{
   assert(bo->type == AMDGPU_BO_SLAB);
   return (struct amdgpu_bo_slab*)bo;
}

bool amdgpu_bo_can_reclaim(struct amdgpu_winsys *ws, struct pb_buffer *_buf);
struct pb_buffer *amdgpu_bo_create(struct amdgpu_winsys *ws,
                                   uint64_t size,
                                   unsigned alignment,
                                   enum radeon_bo_domain domain,
                                   enum radeon_bo_flag flags);
void amdgpu_bo_destroy(struct amdgpu_winsys *ws, struct pb_buffer *_buf);
void *amdgpu_bo_map(struct radeon_winsys *rws,
                    struct pb_buffer *buf,
                    struct radeon_cmdbuf *rcs,
                    enum pipe_map_flags usage);
void amdgpu_bo_unmap(struct radeon_winsys *rws, struct pb_buffer *buf);
void amdgpu_bo_init_functions(struct amdgpu_screen_winsys *ws);

bool amdgpu_bo_can_reclaim_slab(void *priv, struct pb_slab_entry *entry);
struct pb_slab *amdgpu_bo_slab_alloc(void *priv, unsigned heap, unsigned entry_size,
                                     unsigned group_index);
void amdgpu_bo_slab_free(struct amdgpu_winsys *ws, struct pb_slab *slab);

static inline
struct amdgpu_winsys_bo *amdgpu_winsys_bo(struct pb_buffer *bo)
{
   return (struct amdgpu_winsys_bo *)bo;
}

static inline
struct amdgpu_slab *amdgpu_slab(struct pb_slab *slab)
{
   return (struct amdgpu_slab *)slab;
}

static inline
void amdgpu_winsys_bo_reference(struct amdgpu_winsys *ws,
                                struct amdgpu_winsys_bo **dst,
                                struct amdgpu_winsys_bo *src)
{
   radeon_bo_reference(&ws->dummy_ws.base,
                       (struct pb_buffer**)dst, (struct pb_buffer*)src);
}

#endif
