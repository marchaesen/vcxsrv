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

/**
 * @file
 * A simple allocator that allocates and release "numbers".
 *
 * @author Samuel Pitoiset <samuel.pitoiset@gmail.com>
 */

#include "util/u_idalloc.h"
#include "util/u_math.h"
#include <stdlib.h>

ASSERTED static bool
util_idalloc_exists(struct util_idalloc *buf, unsigned id)
{
   return id / 32 < buf->num_set_elements &&
          buf->data[id / 32] & BITFIELD_BIT(id % 32);
}

static void
util_idalloc_resize(struct util_idalloc *buf, unsigned new_num_elements)
{
   if (new_num_elements > buf->num_elements) {
      buf->data = realloc(buf->data, new_num_elements * sizeof(*buf->data));
      memset(&buf->data[buf->num_elements], 0,
             (new_num_elements - buf->num_elements) * sizeof(*buf->data));
      buf->num_elements = new_num_elements;
   }
}

void
util_idalloc_init(struct util_idalloc *buf, unsigned initial_num_ids)
{
   memset(buf, 0, sizeof(*buf));
   assert(initial_num_ids);
   util_idalloc_resize(buf, DIV_ROUND_UP(initial_num_ids, 32));
}

void
util_idalloc_fini(struct util_idalloc *buf)
{
   if (buf->data)
      free(buf->data);
}

unsigned
util_idalloc_alloc(struct util_idalloc *buf)
{
   unsigned num_elements = buf->num_elements;

   for (unsigned i = buf->lowest_free_idx; i < num_elements; i++) {
      if (buf->data[i] == 0xffffffff)
         continue;

      unsigned bit = ffs(~buf->data[i]) - 1;
      buf->data[i] |= 1u << bit;
      buf->lowest_free_idx = i;
      buf->num_set_elements = MAX2(buf->num_set_elements, i + 1);
      return i * 32 + bit;
   }

   /* No slots available, resize and return the first free. */
   util_idalloc_resize(buf, MAX2(num_elements, 1) * 2);

   buf->lowest_free_idx = num_elements;
   buf->data[num_elements] |= 1;
   buf->num_set_elements = MAX2(buf->num_set_elements, num_elements + 1);
   return num_elements * 32;
}

static unsigned
find_free_block(struct util_idalloc *buf, unsigned start)
{
   for (unsigned i = start; i < buf->num_elements; i++) {
      if (!buf->data[i])
         return i;
   }
   return buf->num_elements;
}

/* Allocate a range of consecutive IDs. Return the first ID. */
unsigned
util_idalloc_alloc_range(struct util_idalloc *buf, unsigned num)
{
   if (num == 1)
      return util_idalloc_alloc(buf);

   unsigned num_alloc = DIV_ROUND_UP(num, 32);
   unsigned num_elements = buf->num_elements;
   unsigned base = find_free_block(buf, buf->lowest_free_idx);

   while (1) {
      unsigned i;
      for (i = base;
           i < num_elements && i - base < num_alloc && !buf->data[i]; i++);

      if (i - base == num_alloc)
         goto ret; /* found */

      if (i == num_elements)
         break; /* not found */

      /* continue searching */
      base = !buf->data[i] ? i : i + 1;
   }

   /* No slots available, allocate more. */
   util_idalloc_resize(buf, num_elements * 2 + num_alloc);

ret:
   /* Mark the bits as used. */
   for (unsigned i = base; i < base + num_alloc - (num % 32 != 0); i++)
      buf->data[i] = 0xffffffff;
   if (num % 32 != 0)
      buf->data[base + num_alloc - 1] |= BITFIELD_MASK(num % 32);

   if (buf->lowest_free_idx == base)
      buf->lowest_free_idx = base + num / 32;

   buf->num_set_elements = MAX2(buf->num_set_elements, base + num_alloc);

   /* Validate this algorithm. */
   for (unsigned i = 0; i < num; i++)
      assert(util_idalloc_exists(buf, base * 32 + i));

   return base * 32;
}

void
util_idalloc_free(struct util_idalloc *buf, unsigned id)
{
   unsigned idx = id / 32;

   if (idx >= buf->num_elements)
       return;

   buf->lowest_free_idx = MIN2(idx, buf->lowest_free_idx);
   buf->data[idx] &= ~(1u << (id % 32));

   /* Decrease num_used to the last used element + 1. */
   if (buf->num_set_elements == idx + 1) {
      while (buf->num_set_elements > 0 && !buf->data[buf->num_set_elements - 1])
         buf->num_set_elements--;
   }
}

void
util_idalloc_reserve(struct util_idalloc *buf, unsigned id)
{
   unsigned idx = id / 32;

   if (idx >= buf->num_elements)
      util_idalloc_resize(buf, (idx + 1) * 2);
   buf->data[idx] |= 1u << (id % 32);
   buf->num_set_elements = MAX2(buf->num_set_elements, idx + 1);
}

/*********************************************
 * util_idalloc_mt
 *********************************************/

void
util_idalloc_mt_init(struct util_idalloc_mt *buf,
                     unsigned initial_num_ids, bool skip_zero)
{
   simple_mtx_init(&buf->mutex, mtx_plain);
   util_idalloc_init(&buf->buf, initial_num_ids);
   buf->skip_zero = skip_zero;

   if (skip_zero) {
      ASSERTED unsigned zero = util_idalloc_alloc(&buf->buf);
      assert(zero == 0);
   }
}

/* Callback for drivers using u_threaded_context (abbreviated as tc). */
void
util_idalloc_mt_init_tc(struct util_idalloc_mt *buf)
{
   util_idalloc_mt_init(buf, 1 << 16, true);
}

void
util_idalloc_mt_fini(struct util_idalloc_mt *buf)
{
   util_idalloc_fini(&buf->buf);
   simple_mtx_destroy(&buf->mutex);
}

unsigned
util_idalloc_mt_alloc(struct util_idalloc_mt *buf)
{
   simple_mtx_lock(&buf->mutex);
   unsigned id = util_idalloc_alloc(&buf->buf);
   simple_mtx_unlock(&buf->mutex);
   return id;
}

void
util_idalloc_mt_free(struct util_idalloc_mt *buf, unsigned id)
{
   if (id == 0 && buf->skip_zero)
      return;

   simple_mtx_lock(&buf->mutex);
   util_idalloc_free(&buf->buf, id);
   simple_mtx_unlock(&buf->mutex);
}

/*********************************************
 * util_idalloc_sparse
 *********************************************/

void
util_idalloc_sparse_init(struct util_idalloc_sparse *buf)
{
   static_assert(IS_POT_NONZERO(ARRAY_SIZE(buf->segment)),
         "buf->segment[] must have a power of two number of elements");

   for (unsigned i = 0; i < ARRAY_SIZE(buf->segment); i++)
      util_idalloc_init(&buf->segment[i], 1);
}

void
util_idalloc_sparse_fini(struct util_idalloc_sparse *buf)
{
   for (unsigned i = 0; i < ARRAY_SIZE(buf->segment); i++)
      util_idalloc_fini(&buf->segment[i]);
}

unsigned
util_idalloc_sparse_alloc(struct util_idalloc_sparse *buf)
{
   unsigned max_ids = UTIL_IDALLOC_MAX_IDS_PER_SEGMENT(buf);

   for (unsigned i = 0; i < ARRAY_SIZE(buf->segment); i++) {
      if (buf->segment[i].lowest_free_idx <
          UTIL_IDALLOC_MAX_ELEMS_PER_SEGMENT(buf))
         return max_ids * i + util_idalloc_alloc(&buf->segment[i]);
   }

   fprintf(stderr, "mesa: util_idalloc_sparse_alloc: "
                   "all 2^32 IDs are used, this shouldn't happen\n");
   assert(0);
   return 0;
}

unsigned
util_idalloc_sparse_alloc_range(struct util_idalloc_sparse *buf, unsigned num)
{
   unsigned max_ids = UTIL_IDALLOC_MAX_IDS_PER_SEGMENT(buf);
   unsigned num_elems = DIV_ROUND_UP(num, 32);

   /* TODO: This doesn't try to find a range that spans 2 different segments */
   for (unsigned i = 0; i < ARRAY_SIZE(buf->segment); i++) {
      if (buf->segment[i].lowest_free_idx + num_elems <=
          UTIL_IDALLOC_MAX_ELEMS_PER_SEGMENT(buf)) {
         unsigned base = util_idalloc_alloc_range(&buf->segment[i], num);

         if (base + num <= max_ids)
            return max_ids * i + base;

         /* Back off the allocation and try again with the next segment. */
         for (unsigned i = 0; i < num; i++)
            util_idalloc_free(&buf->segment[i], base + i);
      }
   }

   fprintf(stderr, "mesa: util_idalloc_sparse_alloc_range: can't find a free consecutive range of IDs\n");
   assert(0);
   return 0;
}

void
util_idalloc_sparse_free(struct util_idalloc_sparse *buf, unsigned id)
{
   unsigned max_ids = UTIL_IDALLOC_MAX_IDS_PER_SEGMENT(buf);
   util_idalloc_free(&buf->segment[id / max_ids], id % max_ids);
}

void
util_idalloc_sparse_reserve(struct util_idalloc_sparse *buf, unsigned id)
{
   unsigned max_ids = UTIL_IDALLOC_MAX_IDS_PER_SEGMENT(buf);
   util_idalloc_reserve(&buf->segment[id / max_ids], id % max_ids);
}
