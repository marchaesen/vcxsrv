/**************************************************************************
 *
 * Copyright 2007-2008 VMware, Inc.
 * Copyright 2015 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef PB_CACHE_H
#define PB_CACHE_H

#include "pb_buffer.h"
#include "util/list.h"
#include "os/os_thread.h"

/**
 * Statically inserted into the driver-specific buffer structure.
 */
struct pb_cache_entry
{
   struct list_head head;
   struct pb_buffer *buffer; /**< Pointer to the structure this is part of. */
   struct pb_cache *mgr;
   int64_t start, end; /**< Caching time interval */
   unsigned bucket_index;
};

struct pb_cache
{
   /* The cache is divided into buckets for minimizing cache misses.
    * The driver controls which buffer goes into which bucket.
    */
   struct list_head *buckets;

   mtx_t mutex;
   uint64_t cache_size;
   uint64_t max_cache_size;
   unsigned num_heaps;
   unsigned usecs;
   unsigned num_buffers;
   unsigned bypass_usage;
   float size_factor;

   void (*destroy_buffer)(struct pb_buffer *buf);
   bool (*can_reclaim)(struct pb_buffer *buf);
};

void pb_cache_add_buffer(struct pb_cache_entry *entry);
struct pb_buffer *pb_cache_reclaim_buffer(struct pb_cache *mgr, pb_size size,
                                          unsigned alignment, unsigned usage,
                                          unsigned bucket_index);
void pb_cache_release_all_buffers(struct pb_cache *mgr);
void pb_cache_init_entry(struct pb_cache *mgr, struct pb_cache_entry *entry,
                         struct pb_buffer *buf, unsigned bucket_index);
void pb_cache_init(struct pb_cache *mgr, uint num_heaps,
                   uint usecs, float size_factor,
                   unsigned bypass_usage, uint64_t maximum_cache_size,
                   void (*destroy_buffer)(struct pb_buffer *buf),
                   bool (*can_reclaim)(struct pb_buffer *buf));
void pb_cache_deinit(struct pb_cache *mgr);

#endif
