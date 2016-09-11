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

#include "slab.h"
#include "macros.h"
#include "simple_list.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define ALIGN(value, align) (((value) + (align) - 1) & ~((align) - 1))

#ifdef DEBUG
#define SLAB_MAGIC 0xcafe4321
#define SET_MAGIC(element)   (element)->magic = SLAB_MAGIC
#define CHECK_MAGIC(element) assert((element)->magic == SLAB_MAGIC)
#else
#define SET_MAGIC(element)
#define CHECK_MAGIC(element)
#endif

/* One array element within a big buffer. */
struct slab_element_header {
   /* The next free element. */
   struct slab_element_header *next_free;

#ifdef DEBUG
   /* Use intptr_t to keep the header aligned to a pointer size. */
   intptr_t magic;
#endif
};

static struct slab_element_header *
slab_get_element(struct slab_mempool *pool,
                 struct slab_page_header *page, unsigned index)
{
   return (struct slab_element_header*)
          ((uint8_t*)&page[1] + (pool->element_size * index));
}

static bool
slab_add_new_page(struct slab_mempool *pool)
{
   struct slab_page_header *page;
   struct slab_element_header *element;
   unsigned i;

   page = malloc(sizeof(struct slab_page_header) +
                 pool->num_elements * pool->element_size);
   if (!page)
      return false;

   if (!pool->list.prev)
      make_empty_list(&pool->list);

   insert_at_tail(&pool->list, page);

   /* Mark all elements as free. */
   for (i = 0; i < pool->num_elements-1; i++) {
      element = slab_get_element(pool, page, i);
      element->next_free = slab_get_element(pool, page, i + 1);
      SET_MAGIC(element);
   }

   element = slab_get_element(pool, page, pool->num_elements - 1);
   element->next_free = pool->first_free;
   SET_MAGIC(element);
   pool->first_free = slab_get_element(pool, page, 0);
   return true;
}

/**
 * Allocate an object from the slab. Single-threaded (no mutex).
 */
void *
slab_alloc_st(struct slab_mempool *pool)
{
   struct slab_element_header *element;

   /* Allocate a new page. */
   if (!pool->first_free &&
       !slab_add_new_page(pool))
      return NULL;

   element = pool->first_free;
   CHECK_MAGIC(element);
   pool->first_free = element->next_free;
   return &element[1];
}

/**
 * Free an object allocated from the slab. Single-threaded (no mutex).
 */
void
slab_free_st(struct slab_mempool *pool, void *ptr)
{
   struct slab_element_header *element =
      ((struct slab_element_header*)ptr - 1);

   CHECK_MAGIC(element);
   element->next_free = pool->first_free;
   pool->first_free = element;
}

/**
 * Allocate an object from the slab. Thread-safe.
 */
void *
slab_alloc_mt(struct slab_mempool *pool)
{
   void *mem;

   mtx_lock(&pool->mutex);
   mem = slab_alloc_st(pool);
   mtx_unlock(&pool->mutex);
   return mem;
}

/**
 * Free an object allocated from the slab. Thread-safe.
 */
void
slab_free_mt(struct slab_mempool *pool, void *ptr)
{
   mtx_lock(&pool->mutex);
   slab_free_st(pool, ptr);
   mtx_unlock(&pool->mutex);
}

void
slab_destroy(struct slab_mempool *pool)
{
   struct slab_page_header *page, *temp;

   if (pool->list.next) {
      foreach_s(page, temp, &pool->list) {
         remove_from_list(page);
         free(page);
      }
   }

   mtx_destroy(&pool->mutex);
}

/**
 * Create an allocator for same-sized objects.
 *
 * \param item_size     Size of one object.
 * \param num_items     Number of objects to allocate at once.
 */
void
slab_create(struct slab_mempool *pool,
            unsigned item_size,
            unsigned num_items)
{
   mtx_init(&pool->mutex, mtx_plain);
   pool->element_size = ALIGN(sizeof(struct slab_element_header) + item_size,
                              sizeof(intptr_t));
   pool->num_elements = num_items;
   pool->first_free = NULL;
}
