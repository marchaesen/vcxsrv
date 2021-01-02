/*
 * Copyright © 2017 Intel Corporation
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
 */

#ifndef IRIS_BUFMGR_H
#define IRIS_BUFMGR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include "c11/threads.h"
#include "util/macros.h"
#include "util/u_atomic.h"
#include "util/list.h"
#include "pipe/p_defines.h"

struct iris_batch;
struct gen_device_info;
struct pipe_debug_callback;

/**
 * Memory zones.  When allocating a buffer, you can request that it is
 * placed into a specific region of the virtual address space (PPGTT).
 *
 * Most buffers can go anywhere (IRIS_MEMZONE_OTHER).  Some buffers are
 * accessed via an offset from a base address.  STATE_BASE_ADDRESS has
 * a maximum 4GB size for each region, so we need to restrict those
 * buffers to be within 4GB of the base.  Each memory zone corresponds
 * to a particular base address.
 *
 * We lay out the virtual address space as follows:
 *
 * - [0,   4K): Nothing            (empty page for null address)
 * - [4K,  4G): Shaders            (Instruction Base Address)
 * - [4G,  8G): Surfaces & Binders (Surface State Base Address, Bindless ...)
 * - [8G, 12G): Dynamic            (Dynamic State Base Address)
 * - [12G, *):  Other              (everything else in the full 48-bit VMA)
 *
 * A special buffer for border color lives at the start of the dynamic state
 * memory zone.  This unfortunately has to be handled specially because the
 * SAMPLER_STATE "Indirect State Pointer" field is only a 24-bit pointer.
 *
 * Each GL context uses a separate GEM context, which technically gives them
 * each a separate VMA.  However, we assign address globally, so buffers will
 * have the same address in all GEM contexts.  This lets us have a single BO
 * field for the address, which is easy and cheap.
 */
enum iris_memory_zone {
   IRIS_MEMZONE_SHADER,
   IRIS_MEMZONE_BINDER,
   IRIS_MEMZONE_SURFACE,
   IRIS_MEMZONE_DYNAMIC,
   IRIS_MEMZONE_OTHER,

   IRIS_MEMZONE_BORDER_COLOR_POOL,
};

/* Intentionally exclude single buffer "zones" */
#define IRIS_MEMZONE_COUNT (IRIS_MEMZONE_OTHER + 1)

#define IRIS_BINDER_SIZE (64 * 1024)
#define IRIS_MAX_BINDERS 100

#define IRIS_MEMZONE_SHADER_START     (0ull * (1ull << 32))
#define IRIS_MEMZONE_BINDER_START     (1ull * (1ull << 32))
#define IRIS_MEMZONE_SURFACE_START    (IRIS_MEMZONE_BINDER_START + IRIS_MAX_BINDERS * IRIS_BINDER_SIZE)
#define IRIS_MEMZONE_DYNAMIC_START    (2ull * (1ull << 32))
#define IRIS_MEMZONE_OTHER_START      (3ull * (1ull << 32))

#define IRIS_BORDER_COLOR_POOL_ADDRESS IRIS_MEMZONE_DYNAMIC_START
#define IRIS_BORDER_COLOR_POOL_SIZE (64 * 1024)

/**
 * Classification of the various incoherent caches of the GPU into a number of
 * caching domains.
 */
enum iris_domain {
   /** Render color cache. */
   IRIS_DOMAIN_RENDER_WRITE = 0,
   /** (Hi)Z/stencil cache. */
   IRIS_DOMAIN_DEPTH_WRITE,
   /** Any other read-write cache. */
   IRIS_DOMAIN_OTHER_WRITE,
   /** Any other read-only cache. */
   IRIS_DOMAIN_OTHER_READ,
   /** Number of caching domains. */
   NUM_IRIS_DOMAINS,
   /** Not a real cache, use to opt out of the cache tracking mechanism. */
   IRIS_DOMAIN_NONE = NUM_IRIS_DOMAINS
};

/**
 * Whether a caching domain is guaranteed not to write any data to memory.
 */
static inline bool
iris_domain_is_read_only(enum iris_domain access)
{
   return access == IRIS_DOMAIN_OTHER_READ;
}

struct iris_bo {
   /**
    * Size in bytes of the buffer object.
    *
    * The size may be larger than the size originally requested for the
    * allocation, such as being aligned to page size.
    */
   uint64_t size;

   /** Buffer manager context associated with this buffer object */
   struct iris_bufmgr *bufmgr;

   /** Pre-computed hash using _mesa_hash_pointer for cache tracking sets */
   uint32_t hash;

   /** The GEM handle for this buffer object. */
   uint32_t gem_handle;

   /**
    * Virtual address of the buffer inside the PPGTT (Per-Process Graphics
    * Translation Table).
    *
    * Although each hardware context has its own VMA, we assign BO's to the
    * same address in all contexts, for simplicity.
    */
   uint64_t gtt_offset;

   /**
    * If non-zero, then this bo has an aux-map translation to this address.
    */
   uint64_t aux_map_address;

   /**
    * The validation list index for this buffer, or -1 when not in a batch.
    * Note that a single buffer may be in multiple batches (contexts), and
    * this is a global field, which refers to the last batch using the BO.
    * It should not be considered authoritative, but can be used to avoid a
    * linear walk of the validation list in the common case by guessing that
    * exec_bos[bo->index] == bo and confirming whether that's the case.
    *
    * XXX: this is not ideal now that we have more than one batch per context,
    * XXX: as the index will flop back and forth between the render index and
    * XXX: compute index...
    */
   unsigned index;

   int refcount;
   const char *name;

   uint64_t kflags;

   /**
    * Kenel-assigned global name for this object
    *
    * List contains both flink named and prime fd'd objects
    */
   unsigned global_name;

   /**
    * Current tiling mode
    */
   uint32_t tiling_mode;
   uint32_t stride;

   time_t free_time;

   /** Mapped address for the buffer, saved across map/unmap cycles */
   void *map_cpu;
   /** GTT virtual address for the buffer, saved across map/unmap cycles */
   void *map_gtt;
   /** WC CPU address for the buffer, saved across map/unmap cycles */
   void *map_wc;

   /** BO cache list */
   struct list_head head;

   /** List of GEM handle exports of this buffer (bo_export) */
   struct list_head exports;

   /**
    * Synchronization sequence number of most recent access of this BO from
    * each caching domain.
    *
    * Although this is a global field, use in multiple contexts should be
    * safe, see iris_emit_buffer_barrier_for() for details.
    *
    * Also align it to 64 bits. This will make atomic operations faster on 32
    * bit platforms.
    */
   uint64_t last_seqnos[NUM_IRIS_DOMAINS] __attribute__ ((aligned (8)));

   /**
    * Boolean of whether the GPU is definitely not accessing the buffer.
    *
    * This is only valid when reusable, since non-reusable
    * buffers are those that have been shared with other
    * processes, so we don't know their state.
    */
   bool idle;

   /**
    * Boolean of whether this buffer can be re-used
    */
   bool reusable;

   /**
    * Boolean of whether this buffer has been shared with an external client.
    */
   bool external;

   /**
    * Boolean of whether this buffer is cache coherent
    */
   bool cache_coherent;

   /**
    * Boolean of whether this buffer points into user memory
    */
   bool userptr;
};

#define BO_ALLOC_ZEROED     (1<<0)
#define BO_ALLOC_COHERENT   (1<<1)

/**
 * Allocate a buffer object.
 *
 * Buffer objects are not necessarily initially mapped into CPU virtual
 * address space or graphics device aperture.  They must be mapped
 * using iris_bo_map() to be used by the CPU.
 */
struct iris_bo *iris_bo_alloc(struct iris_bufmgr *bufmgr,
                              const char *name,
                              uint64_t size,
                              enum iris_memory_zone memzone);

/**
 * Allocate a tiled buffer object.
 *
 * Alignment for tiled objects is set automatically; the 'flags'
 * argument provides a hint about how the object will be used initially.
 *
 * Valid tiling formats are:
 *  I915_TILING_NONE
 *  I915_TILING_X
 *  I915_TILING_Y
 */
struct iris_bo *iris_bo_alloc_tiled(struct iris_bufmgr *bufmgr,
                                    const char *name,
                                    uint64_t size,
                                    uint32_t alignment,
                                    enum iris_memory_zone memzone,
                                    uint32_t tiling_mode,
                                    uint32_t pitch,
                                    unsigned flags);

struct iris_bo *
iris_bo_create_userptr(struct iris_bufmgr *bufmgr, const char *name,
                       void *ptr, size_t size,
                       enum iris_memory_zone memzone);

/** Takes a reference on a buffer object */
static inline void
iris_bo_reference(struct iris_bo *bo)
{
   p_atomic_inc(&bo->refcount);
}

/**
 * Releases a reference on a buffer object, freeing the data if
 * no references remain.
 */
void iris_bo_unreference(struct iris_bo *bo);

#define MAP_READ          PIPE_MAP_READ
#define MAP_WRITE         PIPE_MAP_WRITE
#define MAP_ASYNC         PIPE_MAP_UNSYNCHRONIZED
#define MAP_PERSISTENT    PIPE_MAP_PERSISTENT
#define MAP_COHERENT      PIPE_MAP_COHERENT
/* internal */
#define MAP_INTERNAL_MASK (0xffu << 24)
#define MAP_RAW           (0x01 << 24)

#define MAP_FLAGS         (MAP_READ | MAP_WRITE | MAP_ASYNC | \
                           MAP_PERSISTENT | MAP_COHERENT | MAP_INTERNAL_MASK)

/**
 * Maps the buffer into userspace.
 *
 * This function will block waiting for any existing execution on the
 * buffer to complete, first.  The resulting mapping is returned.
 */
MUST_CHECK void *iris_bo_map(struct pipe_debug_callback *dbg,
                             struct iris_bo *bo, unsigned flags);

/**
 * Reduces the refcount on the userspace mapping of the buffer
 * object.
 */
static inline int iris_bo_unmap(struct iris_bo *bo) { return 0; }

/**
 * Waits for rendering to an object by the GPU to have completed.
 *
 * This is not required for any access to the BO by bo_map,
 * bo_subdata, etc.  It is merely a way for the driver to implement
 * glFinish.
 */
void iris_bo_wait_rendering(struct iris_bo *bo);


/**
 * Unref a buffer manager instance.
 */
void iris_bufmgr_unref(struct iris_bufmgr *bufmgr);

/**
 * Create a visible name for a buffer which can be used by other apps
 *
 * \param buf Buffer to create a name for
 * \param name Returned name
 */
int iris_bo_flink(struct iris_bo *bo, uint32_t *name);

/**
 * Make a BO externally accessible.
 *
 * \param bo Buffer to make external
 */
void iris_bo_make_external(struct iris_bo *bo);

/**
 * Returns 1 if mapping the buffer for write could cause the process
 * to block, due to the object being active in the GPU.
 */
int iris_bo_busy(struct iris_bo *bo);

/**
 * Specify the volatility of the buffer.
 * \param bo Buffer to create a name for
 * \param madv The purgeable status
 *
 * Use I915_MADV_DONTNEED to mark the buffer as purgeable, and it will be
 * reclaimed under memory pressure. If you subsequently require the buffer,
 * then you must pass I915_MADV_WILLNEED to mark the buffer as required.
 *
 * Returns 1 if the buffer was retained, or 0 if it was discarded whilst
 * marked as I915_MADV_DONTNEED.
 */
int iris_bo_madvise(struct iris_bo *bo, int madv);

/* drm_bacon_bufmgr_gem.c */
struct iris_bufmgr *iris_bufmgr_get_for_fd(struct gen_device_info *devinfo, int fd,
                                           bool bo_reuse);
int iris_bufmgr_get_fd(struct iris_bufmgr *bufmgr);

struct iris_bo *iris_bo_gem_create_from_name(struct iris_bufmgr *bufmgr,
                                             const char *name,
                                             unsigned handle);

void* iris_bufmgr_get_aux_map_context(struct iris_bufmgr *bufmgr);

int iris_bo_wait(struct iris_bo *bo, int64_t timeout_ns);

uint32_t iris_create_hw_context(struct iris_bufmgr *bufmgr);
uint32_t iris_clone_hw_context(struct iris_bufmgr *bufmgr, uint32_t ctx_id);

#define IRIS_CONTEXT_LOW_PRIORITY    ((I915_CONTEXT_MIN_USER_PRIORITY-1)/2)
#define IRIS_CONTEXT_MEDIUM_PRIORITY (I915_CONTEXT_DEFAULT_PRIORITY)
#define IRIS_CONTEXT_HIGH_PRIORITY   ((I915_CONTEXT_MAX_USER_PRIORITY+1)/2)

int iris_hw_context_set_priority(struct iris_bufmgr *bufmgr,
                                 uint32_t ctx_id, int priority);

void iris_destroy_hw_context(struct iris_bufmgr *bufmgr, uint32_t ctx_id);

int iris_bo_export_dmabuf(struct iris_bo *bo, int *prime_fd);
struct iris_bo *iris_bo_import_dmabuf(struct iris_bufmgr *bufmgr, int prime_fd,
                                      uint64_t modifier);

/**
 * Exports a bo as a GEM handle into a given DRM file descriptor
 * \param bo Buffer to export
 * \param drm_fd File descriptor where the new handle is created
 * \param out_handle Pointer to store the new handle
 *
 * Returns 0 if the buffer was successfully exported, a non zero error code
 * otherwise.
 */
int iris_bo_export_gem_handle_for_device(struct iris_bo *bo, int drm_fd,
                                         uint32_t *out_handle);

uint32_t iris_bo_export_gem_handle(struct iris_bo *bo);

int iris_reg_read(struct iris_bufmgr *bufmgr, uint32_t offset, uint64_t *out);

int drm_ioctl(int fd, unsigned long request, void *arg);

/**
 * Returns the BO's address relative to the appropriate base address.
 *
 * All of our base addresses are programmed to the start of a 4GB region,
 * so simply returning the bottom 32 bits of the BO address will give us
 * the offset from whatever base address corresponds to that memory region.
 */
static inline uint32_t
iris_bo_offset_from_base_address(struct iris_bo *bo)
{
   /* This only works for buffers in the memory zones corresponding to a
    * base address - the top, unbounded memory zone doesn't have a base.
    */
   assert(bo->gtt_offset < IRIS_MEMZONE_OTHER_START);
   return bo->gtt_offset;
}

/**
 * Track access of a BO from the specified caching domain and sequence number.
 *
 * Can be used without locking.  Only the most recent access (i.e. highest
 * seqno) is tracked.
 */
static inline void
iris_bo_bump_seqno(struct iris_bo *bo, uint64_t seqno,
                   enum iris_domain type)
{
   uint64_t *const last_seqno = &bo->last_seqnos[type];
   uint64_t tmp, prev_seqno = p_atomic_read(last_seqno);

   while (prev_seqno < seqno &&
          prev_seqno != (tmp = p_atomic_cmpxchg(last_seqno, prev_seqno, seqno)))
      prev_seqno = tmp;
}

enum iris_memory_zone iris_memzone_for_address(uint64_t address);

#endif /* IRIS_BUFMGR_H */
