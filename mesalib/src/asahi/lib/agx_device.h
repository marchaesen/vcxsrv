/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 */

#ifndef __AGX_DEVICE_H
#define __AGX_DEVICE_H

#include "util/sparse_array.h"
#include "io.h"
#include "agx_formats.h"

#if __APPLE__
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>
#endif

enum agx_dbg {
   AGX_DBG_TRACE = BITFIELD_BIT(0),
   AGX_DBG_DEQP  = BITFIELD_BIT(1),
   AGX_DBG_NO16  = BITFIELD_BIT(2),
};

struct agx_device {
   void *memctx;
   uint32_t debug;

   /* XXX What to bind to? I don't understand the IOGPU UABI */
   struct agx_command_queue queue;
   struct agx_bo cmdbuf, memmap;
   uint64_t next_global_id, last_global_id;

   /* Device handle */
#if __APPLE__
   io_connect_t fd;
#else
   int fd;
#endif

   pthread_mutex_t bo_map_lock;
   struct util_sparse_array bo_map;

   /* Fixed shaders */
   struct {
      struct agx_bo *bo;
      uint32_t clear;
      uint32_t store;
   } internal;

   struct {
      struct agx_bo *bo;
      uint32_t format[AGX_NUM_FORMATS];
   } reload;
};

bool
agx_open_device(void *memctx, struct agx_device *dev);

void
agx_close_device(struct agx_device *dev);

static inline struct agx_bo *
agx_lookup_bo(struct agx_device *dev, uint32_t handle)
{
   return util_sparse_array_get(&dev->bo_map, handle);
}

struct agx_bo
agx_shmem_alloc(struct agx_device *dev, size_t size, bool cmdbuf);

void
agx_shmem_free(struct agx_device *dev, unsigned handle);

uint64_t
agx_get_global_id(struct agx_device *dev);

struct agx_command_queue
agx_create_command_queue(struct agx_device *dev);

void
agx_submit_cmdbuf(struct agx_device *dev, unsigned cmdbuf, unsigned mappings, uint64_t scalar);

void
agx_wait_queue(struct agx_command_queue queue);

#endif
