/*
 * Copyright (C) 2012-2018 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_PRIV_H_
#define FREEDRENO_PRIV_H_

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <xf86drm.h>

#include "util/hash_table.h"
#include "util/list.h"
#include "util/log.h"
#include "util/simple_mtx.h"
#include "util/slab.h"
#include "util/u_atomic.h"
#include "util/u_debug.h"
#include "util/u_math.h"

#include "freedreno_dev_info.h"
#include "freedreno_drmif.h"
#include "freedreno_ringbuffer.h"

extern simple_mtx_t table_lock;

/*
 * Stupid/simple growable array implementation:
 */

#define MAX_ARRAY_SIZE ((unsigned short)~0)

static inline void
grow(void **ptr, uint16_t nr, uint16_t *max, uint16_t sz)
{
   assert((nr + 1) < MAX_ARRAY_SIZE);
   if ((nr + 1) > *max) {
      if (*max > MAX_ARRAY_SIZE/2)
         *max = MAX_ARRAY_SIZE;
      else if ((*max * 2) < (nr + 1))
         *max = nr + 5;
      else
         *max = *max * 2;
      *ptr = realloc(*ptr, *max * sz);
   }
}

#define DECLARE_ARRAY(type, name)                                              \
   unsigned short nr_##name, max_##name;                                       \
   type *name;

#define APPEND(x, name, ...)                                                   \
   ({                                                                          \
      grow((void **)&(x)->name, (x)->nr_##name, &(x)->max_##name,              \
           sizeof((x)->name[0]));                                              \
      (x)->name[(x)->nr_##name] = __VA_ARGS__;                                 \
      (x)->nr_##name++;                                                        \
   })

#define READ_ONCE(x) (*(volatile __typeof__(x) *)&(x))


struct fd_device_funcs {
   /* Create a new buffer object:
    */
   struct fd_bo *(*bo_new)(struct fd_device *dev, uint32_t size, uint32_t flags);

   /* Create a new buffer object from existing handle (ie. dma-buf or
    * flink import):
    */
   struct fd_bo *(*bo_from_handle)(struct fd_device *dev, uint32_t size,
                                   uint32_t handle);

   struct fd_pipe *(*pipe_new)(struct fd_device *dev, enum fd_pipe_id id,
                               unsigned prio);
   void (*destroy)(struct fd_device *dev);
};

struct fd_bo_bucket {
   uint32_t size;
   struct list_head list;
};

struct fd_bo_cache {
   struct fd_bo_bucket cache_bucket[14 * 4];
   int num_buckets;
   time_t time;
};

struct fd_device {
   int fd;
   enum fd_version version;
   int32_t refcnt;

   /* tables to keep track of bo's, to avoid "evil-twin" fd_bo objects:
    *
    *   handle_table: maps handle to fd_bo
    *   name_table: maps flink name to fd_bo
    *
    * We end up needing two tables, because DRM_IOCTL_GEM_OPEN always
    * returns a new handle.  So we need to figure out if the bo is already
    * open in the process first, before calling gem-open.
    */
   struct hash_table *handle_table, *name_table;

   const struct fd_device_funcs *funcs;

   struct fd_bo_cache bo_cache;
   struct fd_bo_cache ring_cache;

   bool has_cached_coherent;

   bool closefd; /* call close(fd) upon destruction */

   /* just for valgrind: */
   int bo_size;

   /**
    * List of deferred submits, protected by submit_lock.  The deferred
    * submits are tracked globally per-device, even if they execute in
    * different order on the kernel side (ie. due to different priority
    * submitqueues, etc) to preserve the order that they are passed off
    * to the kernel.  Once the kernel has them, it is the fences' job
    * to preserve correct order of execution.
    */
   struct list_head deferred_submits;
   unsigned deferred_cmds;
   simple_mtx_t submit_lock;

   /**
    * BO for suballocating long-lived state objects.
    *
    * Note: one would be tempted to put this in fd_pipe to avoid locking.
    * But that is a bad idea for a couple of reasons:
    *
    *  1) With TC, stateobj allocation can happen in either frontend thread
    *     (ie. most CSOs), and also driver thread (a6xx cached tex state)
    *  2) It is best for fd_pipe to not hold a reference to a BO that can
    *     be free'd to bo cache, as that can cause unexpected re-entrancy
    *     (fd_bo_cache_alloc() -> find_in_bucket() -> fd_bo_state() ->
    *     cleanup_fences() -> drop pipe ref which free's bo's).
    */
   struct fd_bo *suballoc_bo;
   uint32_t suballoc_offset;
   simple_mtx_t suballoc_lock;

   struct util_queue submit_queue;
};

#define foreach_submit(name, list) \
   list_for_each_entry(struct fd_submit, name, list, node)
#define foreach_submit_safe(name, list) \
   list_for_each_entry_safe(struct fd_submit, name, list, node)
#define last_submit(list) \
   list_last_entry(list, struct fd_submit, node)

void fd_bo_cache_init(struct fd_bo_cache *cache, int coarse);
void fd_bo_cache_cleanup(struct fd_bo_cache *cache, time_t time);
struct fd_bo *fd_bo_cache_alloc(struct fd_bo_cache *cache, uint32_t *size,
                                uint32_t flags);
int fd_bo_cache_free(struct fd_bo_cache *cache, struct fd_bo *bo);

/* for where @table_lock is already held: */
void fd_bo_del_locked(struct fd_bo *bo);
void fd_device_del_locked(struct fd_device *dev);
void fd_pipe_del_locked(struct fd_pipe *pipe);

struct fd_pipe_funcs {
   struct fd_ringbuffer *(*ringbuffer_new_object)(struct fd_pipe *pipe,
                                                  uint32_t size);
   struct fd_submit *(*submit_new)(struct fd_pipe *pipe);

   /**
    * Flush any deferred submits (if deferred submits are supported by
    * the pipe implementation)
    */
   void (*flush)(struct fd_pipe *pipe, uint32_t fence);

   int (*get_param)(struct fd_pipe *pipe, enum fd_param_id param,
                    uint64_t *value);
   int (*set_param)(struct fd_pipe *pipe, enum fd_param_id param,
                    uint64_t value);
   int (*wait)(struct fd_pipe *pipe, const struct fd_fence *fence,
               uint64_t timeout);
   void (*destroy)(struct fd_pipe *pipe);
};

struct fd_pipe_control {
   uint32_t fence;
};
#define control_ptr(pipe, member) \
   (pipe)->control_mem, offsetof(struct fd_pipe_control, member), 0, 0

struct fd_pipe {
   struct fd_device *dev;
   enum fd_pipe_id id;
   struct fd_dev_id dev_id;

   /**
    * Note refcnt is *not* atomic, but protected by table_lock, since the
    * table_lock is held in fd_bo_add_fence(), which is the hotpath.
    */
   int32_t refcnt;

   /**
    * Previous fence seqno allocated for this pipe.  The fd_pipe represents
    * a single timeline, fences allocated by this pipe can be compared to
    * each other, but fences from different pipes are not comparable (as
    * there could be preemption of multiple priority level submitqueues at
    * play)
    */
   uint32_t last_fence;

   /**
    * The last fence seqno that was flushed to kernel (doesn't mean that it
    * is complete, just that the kernel knows about it)
    */
   uint32_t last_submit_fence;

   uint32_t last_enqueue_fence;   /* just for debugging */

   struct fd_bo *control_mem;
   volatile struct fd_pipe_control *control;

   struct slab_parent_pool ring_pool;

   const struct fd_pipe_funcs *funcs;
};

uint32_t fd_pipe_emit_fence(struct fd_pipe *pipe, struct fd_ringbuffer *ring);

static inline void
fd_pipe_flush(struct fd_pipe *pipe, uint32_t fence)
{
   if (!pipe->funcs->flush)
      return;
   pipe->funcs->flush(pipe, fence);
}

struct fd_submit_funcs {
   struct fd_ringbuffer *(*new_ringbuffer)(struct fd_submit *submit,
                                           uint32_t size,
                                           enum fd_ringbuffer_flags flags);
   int (*flush)(struct fd_submit *submit, int in_fence_fd,
                struct fd_submit_fence *out_fence);
   void (*destroy)(struct fd_submit *submit);
};

struct fd_submit {
   int32_t refcnt;
   struct fd_pipe *pipe;
   const struct fd_submit_funcs *funcs;

   struct fd_ringbuffer *primary;
   uint32_t fence;
   struct list_head node;  /* node in fd_pipe::deferred_submits */
};

static inline unsigned
fd_dev_count_deferred_cmds(struct fd_device *dev)
{
   unsigned nr = 0;

   simple_mtx_assert_locked(&dev->submit_lock);

   list_for_each_entry (struct fd_submit, submit, &dev->deferred_submits, node) {
      nr += fd_ringbuffer_cmd_count(submit->primary);
   }

   return nr;
}

struct fd_bo_funcs {
   int (*offset)(struct fd_bo *bo, uint64_t *offset);
   int (*cpu_prep)(struct fd_bo *bo, struct fd_pipe *pipe, uint32_t op);
   void (*cpu_fini)(struct fd_bo *bo);
   int (*madvise)(struct fd_bo *bo, int willneed);
   uint64_t (*iova)(struct fd_bo *bo);
   void (*set_name)(struct fd_bo *bo, const char *fmt, va_list ap);
   void (*destroy)(struct fd_bo *bo);

   /**
    * Optional, copy data into bo, falls back to mmap+memcpy.  If not
    * implemented, it must be possible to mmap all buffers
    */
   void (*upload)(struct fd_bo *bo, void *src, unsigned off, unsigned len);

   /**
    * Optional, if upload is supported, should upload be preferred?
    */
   bool (*prefer_upload)(struct fd_bo *bo, unsigned len);
};

void fd_bo_add_fence(struct fd_bo *bo, struct fd_pipe *pipe, uint32_t fence);

enum fd_bo_state {
   FD_BO_STATE_IDLE,
   FD_BO_STATE_BUSY,
   FD_BO_STATE_UNKNOWN,
};
enum fd_bo_state fd_bo_state(struct fd_bo *bo);

void fd_bo_init_common(struct fd_bo *bo, struct fd_device *dev);

struct fd_bo *fd_bo_new_ring(struct fd_device *dev, uint32_t size);

#define enable_debug 0 /* TODO make dynamic */

bool fd_dbg(void);

#define INFO_MSG(fmt, ...)                                                     \
   do {                                                                        \
      if (fd_dbg())                                                            \
         mesa_logi("%s:%d: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);      \
   } while (0)
#define DEBUG_MSG(fmt, ...)                                                    \
   do                                                                          \
      if (enable_debug) {                                                      \
         mesa_logd("%s:%d: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);      \
      }                                                                        \
   while (0)
#define WARN_MSG(fmt, ...)                                                     \
   do {                                                                        \
      mesa_logw("%s:%d: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);         \
   } while (0)
#define ERROR_MSG(fmt, ...)                                                    \
   do {                                                                        \
      mesa_loge("%s:%d: " fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);         \
   } while (0)

#define U642VOID(x) ((void *)(unsigned long)(x))
#define VOID2U64(x) ((uint64_t)(unsigned long)(x))

#if HAVE_VALGRIND
#include <memcheck.h>

/*
 * For tracking the backing memory (if valgrind enabled, we force a mmap
 * for the purposes of tracking)
 */
static inline void
VG_BO_ALLOC(struct fd_bo *bo)
{
   if (bo && RUNNING_ON_VALGRIND) {
      VALGRIND_MALLOCLIKE_BLOCK(fd_bo_map(bo), bo->size, 0, 1);
   }
}

static inline void
VG_BO_FREE(struct fd_bo *bo)
{
   VALGRIND_FREELIKE_BLOCK(bo->map, 0);
}

/*
 * For tracking bo structs that are in the buffer-cache, so that valgrind
 * doesn't attribute ownership to the first one to allocate the recycled
 * bo.
 *
 * Note that the list_head in fd_bo is used to track the buffers in cache
 * so disable error reporting on the range while they are in cache so
 * valgrind doesn't squawk about list traversal.
 *
 */
static inline void
VG_BO_RELEASE(struct fd_bo *bo)
{
   if (RUNNING_ON_VALGRIND) {
      VALGRIND_DISABLE_ADDR_ERROR_REPORTING_IN_RANGE(bo, bo->dev->bo_size);
      VALGRIND_MAKE_MEM_NOACCESS(bo, bo->dev->bo_size);
      VALGRIND_FREELIKE_BLOCK(bo->map, 0);
   }
}
static inline void
VG_BO_OBTAIN(struct fd_bo *bo)
{
   if (RUNNING_ON_VALGRIND) {
      VALGRIND_MAKE_MEM_DEFINED(bo, bo->dev->bo_size);
      VALGRIND_ENABLE_ADDR_ERROR_REPORTING_IN_RANGE(bo, bo->dev->bo_size);
      VALGRIND_MALLOCLIKE_BLOCK(bo->map, bo->size, 0, 1);
   }
}
#else
static inline void
VG_BO_ALLOC(struct fd_bo *bo)
{
}
static inline void
VG_BO_FREE(struct fd_bo *bo)
{
}
static inline void
VG_BO_RELEASE(struct fd_bo *bo)
{
}
static inline void
VG_BO_OBTAIN(struct fd_bo *bo)
{
}
#endif

#define FD_DEFINE_CAST(parent, child)                                          \
   static inline struct child *to_##child(struct parent *x)                    \
   {                                                                           \
      return (struct child *)x;                                                \
   }

#endif /* FREEDRENO_PRIV_H_ */
