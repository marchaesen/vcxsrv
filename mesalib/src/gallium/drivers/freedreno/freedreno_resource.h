/*
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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

#ifndef FREEDRENO_RESOURCE_H_
#define FREEDRENO_RESOURCE_H_

#include "util/bitset.h"
#include "util/list.h"
#include "util/set.h"
#include "util/simple_mtx.h"
#include "util/u_dump.h"
#include "util/u_range.h"
#include "util/u_transfer_helper.h"

#include "freedreno/fdl/freedreno_layout.h"
#include "freedreno_batch.h"
#include "freedreno_util.h"

#define PRSC_FMT                                                               \
   "p: target=%s, format=%s, %ux%ux%u, "                                       \
   "array_size=%u, last_level=%u, "                                            \
   "nr_samples=%u, usage=%u, bind=%x, flags=%x"
#define PRSC_ARGS(p)                                                           \
   (p), util_str_tex_target((p)->target, true),                                \
      util_format_short_name((p)->format), (p)->width0, (p)->height0,          \
      (p)->depth0, (p)->array_size, (p)->last_level, (p)->nr_samples,          \
      (p)->usage, (p)->bind, (p)->flags

enum fd_lrz_direction {
   FD_LRZ_UNKNOWN,
   /* Depth func less/less-than: */
   FD_LRZ_LESS,
   /* Depth func greater/greater-than: */
   FD_LRZ_GREATER,
};

/**
 * A resource (any buffer/texture/image/etc)
 */
struct fd_resource {
   struct threaded_resource b;
   struct fd_bo *bo; /* use fd_resource_set_bo() to write */
   enum pipe_format internal_format;
   uint32_t hash; /* _mesa_hash_pointer() on this resource's address. */

   /* Atomic counter of the number of batches referencing this resource in any
    * batch cache.  Used for fd_resource_busy(), which needs to check for busy
    * in the batch cache from the frontend thread.
    */
   uint32_t batch_references;

   struct fdl_layout layout;

   /* buffer range that has been initialized */
   struct util_range valid_buffer_range;
   bool valid;
   struct renderonly_scanout *scanout;

   /* reference to the resource holding stencil data for a z32_s8 texture */
   /* TODO rename to secondary or auxiliary? */
   struct fd_resource *stencil;

   simple_mtx_t lock;

   /* bitmask of state this resource could potentially dirty when rebound,
    * see rebind_resource()
    */
   enum fd_dirty_3d_state dirty;

   /* Sequence # incremented each time bo changes: */
   uint16_t seqno;

   /* Is this buffer a replacement created by threaded_context to avoid
    * a stall in PIPE_MAP_DISCARD_WHOLE_RESOURCE|PIPE_MAP_WRITE case?
    */
   bool is_replacement : 1;

   /* Uninitialized resources with UBWC format need their UBWC flag data
    * cleared before writes, as the UBWC state is read and used during
    * writes, so undefined UBWC flag data results in undefined results.
    */
   bool needs_ubwc_clear : 1;

   /*
    * LRZ
    *
    * TODO lrz width/height/pitch should probably also move to
    * fdl_layout
    */
   bool lrz_valid : 1;
   enum fd_lrz_direction lrz_direction : 2;
   uint16_t lrz_width; // for lrz clear, does this differ from lrz_pitch?
   uint16_t lrz_height;
   uint16_t lrz_pitch;
   struct fd_bo *lrz;
};

struct fd_memory_object {
   struct pipe_memory_object b;
   struct fd_bo *bo;
};

static inline struct fd_resource *
fd_resource(struct pipe_resource *ptex)
{
   return (struct fd_resource *)ptex;
}

static inline const struct fd_resource *
fd_resource_const(const struct pipe_resource *ptex)
{
   return (const struct fd_resource *)ptex;
}

static inline struct fd_memory_object *
fd_memory_object(struct pipe_memory_object *pmemobj)
{
   return (struct fd_memory_object *)pmemobj;
}

static inline bool
fd_batch_references(struct fd_batch *batch, struct fd_resource *rsc)
{
   /* Currently each rsc has an individual BO, so we can use the bo handle as a
    * unique index for the resource.
    */
   uint32_t handle = fd_bo_id(rsc->bo);
   return handle < batch->bos_size && BITSET_TEST(batch->bos, handle);
}

static inline struct fd_batch *
fd_bc_writer(struct fd_context *ctx, struct fd_resource *rsc)
{
   struct hash_entry *entry =
      _mesa_hash_table_search_pre_hashed(ctx->batch_cache.written_resources, rsc->hash, rsc);
   if (entry)
      return entry->data;
   return NULL;
}

static inline bool
pending(struct fd_context *ctx, struct fd_resource *rsc, bool write) assert_dt
{
   if (write) {
      foreach_batch (batch, &ctx->batch_cache) {
         if (fd_batch_references(batch, rsc))
            return true;
      }
   } else {
      if (fd_bc_writer(ctx, rsc))
         return true;
   }

   if (rsc->stencil && pending(ctx, rsc->stencil, write))
      return true;

   return false;
}

static inline bool
resource_busy(struct fd_resource *rsc, unsigned op)
{
   return fd_bo_cpu_prep(rsc->bo, NULL, op | FD_BO_PREP_NOSYNC) != 0;
}

int __fd_resource_wait(struct fd_context *ctx, struct fd_resource *rsc,
                       unsigned op, const char *func);
#define fd_resource_wait(ctx, rsc, op)                                         \
   __fd_resource_wait(ctx, rsc, op, __func__)

static inline void
fd_resource_lock(struct fd_resource *rsc)
{
   simple_mtx_lock(&rsc->lock);
}

static inline void
fd_resource_unlock(struct fd_resource *rsc)
{
   simple_mtx_unlock(&rsc->lock);
}

static inline void
fd_resource_set_usage(struct pipe_resource *prsc, enum fd_dirty_3d_state usage)
{
   if (!prsc)
      return;
   struct fd_resource *rsc = fd_resource(prsc);
   /* Bits are only ever ORed in, and we expect many set_usage() per
    * resource, so do the quick check outside of the lock.
    */
   if (likely(rsc->dirty & usage))
      return;
   fd_resource_lock(rsc);
   rsc->dirty |= usage;
   fd_resource_unlock(rsc);
}

static inline bool
has_depth(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);
   return util_format_has_depth(desc);
}

struct fd_transfer {
   struct threaded_transfer b;
   struct pipe_resource *staging_prsc;
   struct pipe_box staging_box;
};

static inline struct fd_transfer *
fd_transfer(struct pipe_transfer *ptrans)
{
   return (struct fd_transfer *)ptrans;
}

static inline struct fdl_slice *
fd_resource_slice(struct fd_resource *rsc, unsigned level)
{
   assert(level <= rsc->b.b.last_level);
   return &rsc->layout.slices[level];
}

static inline uint32_t
fd_resource_layer_stride(struct fd_resource *rsc, unsigned level)
{
   return fdl_layer_stride(&rsc->layout, level);
}

/* get pitch (in bytes) for specified mipmap level */
static inline uint32_t
fd_resource_pitch(struct fd_resource *rsc, unsigned level)
{
   if (is_a2xx(fd_screen(rsc->b.b.screen)))
      return fdl2_pitch(&rsc->layout, level);

   return fdl_pitch(&rsc->layout, level);
}

/* get offset for specified mipmap level and texture/array layer */
static inline uint32_t
fd_resource_offset(struct fd_resource *rsc, unsigned level, unsigned layer)
{
   uint32_t offset = fdl_surface_offset(&rsc->layout, level, layer);
   debug_assert(offset < fd_bo_size(rsc->bo));
   return offset;
}

static inline uint32_t
fd_resource_ubwc_offset(struct fd_resource *rsc, unsigned level, unsigned layer)
{
   uint32_t offset = fdl_ubwc_offset(&rsc->layout, level, layer);
   debug_assert(offset < fd_bo_size(rsc->bo));
   return offset;
}

/* This might be a5xx specific, but higher mipmap levels are always linear: */
static inline bool
fd_resource_level_linear(const struct pipe_resource *prsc, int level)
{
   struct fd_screen *screen = fd_screen(prsc->screen);
   debug_assert(!is_a3xx(screen));

   return fdl_level_linear(&fd_resource_const(prsc)->layout, level);
}

static inline uint32_t
fd_resource_tile_mode(struct pipe_resource *prsc, int level)
{
   return fdl_tile_mode(&fd_resource(prsc)->layout, level);
}

static inline const char *
fd_resource_tile_mode_desc(const struct fd_resource *rsc, int level)
{
   return fdl_tile_mode_desc(&rsc->layout, level);
}

static inline bool
fd_resource_ubwc_enabled(struct fd_resource *rsc, int level)
{
   return fdl_ubwc_enabled(&rsc->layout, level);
}

/* access # of samples, with 0 normalized to 1 (which is what we care about
 * most of the time)
 */
static inline unsigned
fd_resource_nr_samples(struct pipe_resource *prsc)
{
   return MAX2(1, prsc->nr_samples);
}

void fd_resource_screen_init(struct pipe_screen *pscreen);
void fd_resource_context_init(struct pipe_context *pctx);

uint32_t fd_setup_slices(struct fd_resource *rsc);
void fd_resource_resize(struct fd_context *ctx, struct pipe_resource *prsc, uint32_t sz);
void fd_replace_buffer_storage(struct pipe_context *ctx,
                               struct pipe_resource *dst,
                               struct pipe_resource *src,
                               unsigned num_rebinds,
                               uint32_t rebind_mask,
                               uint32_t delete_buffer_id) in_dt;
bool fd_resource_busy(struct pipe_screen *pscreen, struct pipe_resource *prsc,
                      unsigned usage);

void fd_resource_uncompress(struct fd_context *ctx,
                            struct fd_resource *rsc,
                            bool linear) assert_dt;
void fd_resource_dump(struct fd_resource *rsc, const char *name);

bool fd_render_condition_check(struct pipe_context *pctx) assert_dt;

static inline void
fd_batch_write_prep(struct fd_batch *batch, struct fd_resource *rsc) assert_dt
{
   if (unlikely(rsc->needs_ubwc_clear)) {
      batch->ctx->clear_ubwc(batch, rsc);
      rsc->needs_ubwc_clear = false;
   }
}

static inline void
fd_batch_resource_read(struct fd_batch *batch,
                       struct fd_resource *rsc) assert_dt
{
   /* Fast path: if we hit this then we know we don't have anyone else
    * writing to it (since both _write and _read flush other writers), and
    * that we've already recursed for stencil.
    */
   if (unlikely(!fd_batch_references(batch, rsc)))
      fd_batch_resource_read_slowpath(batch, rsc);
}

#endif /* FREEDRENO_RESOURCE_H_ */
