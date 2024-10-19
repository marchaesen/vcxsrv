/**************************************************************************
 *
 * Copyright 2017 Valve Corporation
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
 * IN NO EVENT SHALL THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/* Allocator of IDs (e.g. OpenGL object IDs), or simply an allocator of
 * numbers.
 *
 * The allocator uses a bit array to track allocated IDs.
 */

#ifndef U_IDALLOC_H
#define U_IDALLOC_H

#include <inttypes.h>
#include <stdbool.h>
#include "simple_mtx.h"
#include "bitscan.h"

#ifdef __cplusplus
extern "C" {
#endif

struct util_idalloc
{
   uint32_t *data;
   unsigned num_elements;     /* number of allocated elements of "data" */
   unsigned num_set_elements; /* the last non-zero element of "data" + 1 */
   unsigned lowest_free_idx;
};

void
util_idalloc_init(struct util_idalloc *buf, unsigned initial_num_ids);

void
util_idalloc_fini(struct util_idalloc *buf);

unsigned
util_idalloc_alloc(struct util_idalloc *buf);

unsigned
util_idalloc_alloc_range(struct util_idalloc *buf, unsigned num);

void
util_idalloc_free(struct util_idalloc *buf, unsigned id);

void
util_idalloc_reserve(struct util_idalloc *buf, unsigned id);

#define util_idalloc_foreach(buf, id) \
   for (uint32_t _i = 0, _mask = (buf)->num_set_elements ? (buf)->data[0] : 0, id, \
                 _count = (buf)->num_used; \
        _i < _count; _mask = ++_i < _count ? (buf)->data[_i] : 0) \
      while (_mask) \
         if ((id = _i * 32 + u_bit_scan(&_mask)), true)

/* This allows freeing IDs while iterating, excluding ID=0. */
#define util_idalloc_foreach_no_zero_safe(buf, id) \
   for (uint32_t _i = 0, _bit, id, _count = (buf)->num_set_elements, \
         _mask = _count ? (buf)->data[0] & ~0x1 : 0; \
        _i < _count; _mask = ++_i < _count ? (buf)->data[_i] : 0) \
      while (_mask) \
         if ((_bit = u_bit_scan(&_mask), id = _i * 32 + _bit), \
             (buf)->data[_i] & BITFIELD_BIT(_bit))

/* Thread-safe variant. */
struct util_idalloc_mt {
   struct util_idalloc buf;
   simple_mtx_t mutex;
   bool skip_zero;
};

void
util_idalloc_mt_init(struct util_idalloc_mt *buf,
                     unsigned initial_num_ids, bool skip_zero);

void
util_idalloc_mt_init_tc(struct util_idalloc_mt *buf);

void
util_idalloc_mt_fini(struct util_idalloc_mt *buf);

unsigned
util_idalloc_mt_alloc(struct util_idalloc_mt *buf);

void
util_idalloc_mt_free(struct util_idalloc_mt *buf, unsigned id);

/* util_idalloc_sparse: The 32-bit ID range is divided into separately managed
 * segments. This reduces virtual memory usage when IDs are sparse.
 * It's done by layering util_idalloc_sparse on top of util_idalloc.
 *
 * If the last ID is allocated:
 * - "util_idalloc" occupies 512 MB of virtual memory
 * - "util_idalloc_sparse" occupies only 512 KB of virtual memory
 */
struct util_idalloc_sparse {
   struct util_idalloc segment[1024];
};

#define UTIL_IDALLOC_MAX_IDS_PER_SEGMENT(buf) \
   ((uint32_t)(BITFIELD64_BIT(32) / ARRAY_SIZE((buf)->segment)))

#define UTIL_IDALLOC_MAX_ELEMS_PER_SEGMENT(buf) \
   (UTIL_IDALLOC_MAX_IDS_PER_SEGMENT(buf) / 32)

void
util_idalloc_sparse_init(struct util_idalloc_sparse *buf);

void
util_idalloc_sparse_fini(struct util_idalloc_sparse *buf);

unsigned
util_idalloc_sparse_alloc(struct util_idalloc_sparse *buf);

unsigned
util_idalloc_sparse_alloc_range(struct util_idalloc_sparse *buf, unsigned num);

void
util_idalloc_sparse_free(struct util_idalloc_sparse *buf, unsigned id);

void
util_idalloc_sparse_reserve(struct util_idalloc_sparse *buf, unsigned id);

/* This allows freeing IDs while iterating, excluding ID=0. */
#define util_idalloc_sparse_foreach_no_zero_safe(buf, id) \
   for (uint32_t _s = 0; _s < ARRAY_SIZE((buf)->segment); _s++) \
      for (uint32_t _i = 0, _bit, id, _count = (buf)->segment[_s].num_set_elements, \
            _mask = _count ? (buf)->segment[_s].data[0] & ~0x1 : 0; \
           _i < _count; _mask = ++_i < _count ? (buf)->segment[_s].data[_i] : 0) \
         while (_mask) \
            if ((_bit = u_bit_scan(&_mask), id = _s * UTIL_IDALLOC_MAX_IDS_PER_SEGMENT(buf) + _i * 32 + _bit), \
                (buf)->segment[_s].data[_i] & BITFIELD_BIT(_bit))

#ifdef __cplusplus
}
#endif

#endif /* U_IDALLOC_H */
