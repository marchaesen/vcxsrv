/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_GMEM_H_
#define FREEDRENO_GMEM_H_

#include "pipe/p_state.h"
#include "util/list.h"

#include "freedreno_util.h"

BEGINC;

/* per-pipe configuration for hw binning: */
struct fd_vsc_pipe {
   uint8_t x, y, w, h; /* VSC_PIPE[p].CONFIG */
};

/* per-tile configuration for hw binning: */
struct fd_tile {
   uint8_t p; /* index into vsc_pipe[]s */
   uint8_t n; /* slot within pipe */
   uint16_t bin_w, bin_h;
   uint16_t xoff, yoff;
};

struct fd_gmem_stateobj {
   struct pipe_reference reference;
   struct fd_screen *screen;
   void *key;

   uint32_t cbuf_base[MAX_RENDER_TARGETS];
   uint32_t zsbuf_base[2];
   uint8_t cbuf_cpp[MAX_RENDER_TARGETS];
   uint8_t zsbuf_cpp[2];
   uint16_t bin_h, nbins_y;
   uint16_t bin_w, nbins_x;
   uint16_t minx, miny;
   uint16_t width, height;
   uint16_t maxpw, maxph; /* maximum pipe width/height */
   uint8_t num_vsc_pipes; /* number of pipes for a20x */

   struct fd_vsc_pipe vsc_pipe[32];
   struct fd_tile *tile;

   struct list_head node;
};

void __fd_gmem_destroy(struct fd_gmem_stateobj *gmem);

static inline void
fd_gmem_reference(struct fd_gmem_stateobj **ptr, struct fd_gmem_stateobj *gmem)
{
   struct fd_gmem_stateobj *old_gmem = *ptr;

   if (pipe_reference(&(*ptr)->reference, &gmem->reference))
      __fd_gmem_destroy(old_gmem);

   *ptr = gmem;
}

struct fd_gmem_cache {
   struct hash_table *ht;
   struct list_head lru;
};

struct fd_batch;

void fd_gmem_render_tiles(struct fd_batch *batch) assert_dt;
unsigned fd_gmem_estimate_bins_per_pipe(struct fd_batch *batch);
bool fd_gmem_needs_restore(struct fd_batch *batch, const struct fd_tile *tile,
                           uint32_t buffers);

struct pipe_screen;
void fd_gmem_screen_init(struct pipe_screen *pscreen);
void fd_gmem_screen_fini(struct pipe_screen *pscreen);

ENDC;

#endif /* FREEDRENO_GMEM_H_ */
