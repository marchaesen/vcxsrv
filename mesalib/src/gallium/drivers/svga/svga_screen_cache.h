/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_SCREEN_CACHE_H_
#define SVGA_SCREEN_CACHE_H_

#include "svga_reg.h"
#include "svga3d_reg.h"

#include "util/u_thread.h"

#include "util/list.h"


/* Guess the storage size of cached surfaces and try and keep it under
 * this amount:
 */ 
#define SVGA_HOST_SURFACE_CACHE_BYTES (16 * 1024 * 1024)

/* Maximum number of discrete surfaces in the cache:
 */
#define SVGA_HOST_SURFACE_CACHE_SIZE 1024

/* Number of hash buckets:
 */
#define SVGA_HOST_SURFACE_CACHE_BUCKETS 256


struct svga_winsys_surface;
struct svga_screen;
struct svga_context;

/**
 * Same as svga_winsys_screen::surface_create.
 */
struct svga_host_surface_cache_key
{
   SVGA3dSurfaceAllFlags flags;
   SVGA3dSurfaceFormat format;
   SVGA3dSize size;
   uint32_t numFaces:3;
   uint32_t arraySize:16;
   uint32_t numMipLevels:6;
   uint32_t cachable:1;         /* False if this is a shared surface */
   uint32_t sampleCount:5;
   uint32_t scanout:1;
   uint32_t coherent:1;
   uint32_t persistent:1;
};


struct svga_host_surface_cache_entry
{
   /** 
    * Head for the LRU list, svga_host_surface_cache::unused, and
    * svga_host_surface_cache::empty
    */
   struct list_head head;
   
   /** Head for the bucket lists. */
   struct list_head bucket_head;

   struct svga_host_surface_cache_key key;
   struct svga_winsys_surface *handle;
   
   struct pipe_fence_handle *fence;
};


/**
 * Cache of the host surfaces.
 * 
 * A cache entry can be in the following stages:
 * 1. empty (entry->handle = NULL)
 * 2. holding a buffer in a validate list
 * 3. holding a buffer in an invalidate list
 * 4. holding a flushed buffer (not in any validate list) with an active fence
 * 5. holding a flushed buffer with an expired fence
 * 
 * An entry progresses from 1 -> 2 -> 3 -> 4 -> 5. When we need an entry to put a 
 * buffer into we preferentially take from 1, or from the least recently used 
 * buffer from 4/5.
 */
struct svga_host_surface_cache 
{
   mtx_t mutex;
   
   /* Unused buffers are put in buckets to speed up lookups */
   struct list_head bucket[SVGA_HOST_SURFACE_CACHE_BUCKETS];
   
   /* Entries with unused buffers, ordered from most to least recently used 
    * (3 and 4) */
   struct list_head unused;
   
   /* Entries with buffers still in validate list (2) */
   struct list_head validated;
   
   /* Entries with buffers still in invalidate list (3) */
   struct list_head invalidated;
   
   /** Empty entries (1) */
   struct list_head empty;

   /** The actual storage for the entries */
   struct svga_host_surface_cache_entry entries[SVGA_HOST_SURFACE_CACHE_SIZE];

   /** Sum of sizes of all surfaces (in bytes) */
   unsigned total_size;
};


void
svga_screen_cache_cleanup(struct svga_screen *svgascreen);

void
svga_screen_cache_flush(struct svga_screen *svgascreen,
                        struct svga_context *svga,
                        struct pipe_fence_handle *fence);

enum pipe_error
svga_screen_cache_init(struct svga_screen *svgascreen);


struct svga_winsys_surface *
svga_screen_surface_create(struct svga_screen *svgascreen,
                           unsigned bind_flags, enum pipe_resource_usage usage,
                           bool *invalidated,
                           struct svga_host_surface_cache_key *key);

void
svga_screen_surface_destroy(struct svga_screen *svgascreen,
                            const struct svga_host_surface_cache_key *key,
                            bool to_invalidate,
                            struct svga_winsys_surface **handle);

void
svga_screen_cache_dump(const struct svga_screen *svgascreen);

unsigned
svga_surface_size(const struct svga_host_surface_cache_key *key);


#endif /* SVGA_SCREEN_CACHE_H_ */
