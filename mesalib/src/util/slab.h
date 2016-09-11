/*
 * Copyright 2010 Marek Olšák <maraeo@gmail.com>
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

/**
 * Slab allocator for equally sized memory allocations.
 * The thread-safe path ("*_mt" functions) is usually slower than malloc/free.
 * The single-threaded path ("*_st" functions) is faster than malloc/free.
 */

#ifndef SLAB_H
#define SLAB_H

#include "c11/threads.h"

/* The page is an array of allocations in one block. */
struct slab_page_header {
   /* The header (linked-list pointers). */
   struct slab_page_header *prev, *next;

   /* Memory after the last member is dedicated to the page itself.
    * The allocated size is always larger than this structure.
    */
};

struct slab_mempool {
   mtx_t mutex;
   unsigned element_size;
   unsigned num_elements;
   struct slab_element_header *first_free;
   struct slab_page_header list;
};

void slab_create(struct slab_mempool *pool,
                 unsigned item_size,
                 unsigned num_items);
void slab_destroy(struct slab_mempool *pool);
void *slab_alloc_st(struct slab_mempool *pool);
void slab_free_st(struct slab_mempool *pool, void *ptr);
void *slab_alloc_mt(struct slab_mempool *pool);
void slab_free_mt(struct slab_mempool *pool, void *ptr);

#endif
