/*
 * Copyright © 2019 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "sparse_array.h"

struct util_sparse_array_node {
   uint32_t level;
   uint32_t _pad;
   uint64_t max_idx;
};

void
util_sparse_array_init(struct util_sparse_array *arr,
                       size_t elem_size, size_t node_size)
{
   memset(arr, 0, sizeof(*arr));
   arr->elem_size = elem_size;
   arr->node_size_log2 = util_logbase2_64(node_size);
   assert(node_size >= 2 && node_size == (1ull << arr->node_size_log2));
}

static inline void *
_util_sparse_array_node_data(struct util_sparse_array_node *node)
{
   return node + 1;
}

static inline void
_util_sparse_array_node_finish(struct util_sparse_array *arr,
                               struct util_sparse_array_node *node)
{
   if (node->level > 0) {
      struct util_sparse_array_node **children =
         _util_sparse_array_node_data(node);
      size_t node_size = 1ull << arr->node_size_log2;
      for (size_t i = 0; i < node_size; i++) {
         if (children[i] != NULL)
            _util_sparse_array_node_finish(arr, children[i]);
      }
   }

   free(node);
}

void
util_sparse_array_finish(struct util_sparse_array *arr)
{
   if (arr->root)
      _util_sparse_array_node_finish(arr, arr->root);
}

static inline struct util_sparse_array_node *
_util_sparse_array_alloc_node(struct util_sparse_array *arr,
                              unsigned level)
{
   size_t size = sizeof(struct util_sparse_array_node);
   if (level == 0) {
      size += arr->elem_size << arr->node_size_log2;
   } else {
      size += sizeof(struct util_sparse_array_node *) << arr->node_size_log2;
   }

   struct util_sparse_array_node *node = calloc(1, size);
   node->level = level;

   return node;
}

static inline struct util_sparse_array_node *
_util_sparse_array_set_or_free_node(struct util_sparse_array_node **node_ptr,
                                    struct util_sparse_array_node *cmp_node,
                                    struct util_sparse_array_node *node)
{
   struct util_sparse_array_node *prev_node =
      p_atomic_cmpxchg(node_ptr, cmp_node, node);

   if (prev_node != cmp_node) {
      /* We lost the race.  Free this one and return the one that was already
       * allocated.
       */
      free(node);
      return prev_node;
   } else {
      return node;
   }
}

void *
util_sparse_array_get(struct util_sparse_array *arr, uint64_t idx)
{
   struct util_sparse_array_node *root = p_atomic_read(&arr->root);
   if (unlikely(root == NULL)) {
      unsigned root_level = 0;
      uint64_t idx_iter = idx >> arr->node_size_log2;
      while (idx_iter) {
         idx_iter >>= arr->node_size_log2;
         root_level++;
      }
      struct util_sparse_array_node *new_root =
         _util_sparse_array_alloc_node(arr, root_level);
      root = _util_sparse_array_set_or_free_node(&arr->root, NULL, new_root);
   }

   while (1) {
      uint64_t root_idx = idx >> (root->level * arr->node_size_log2);
      if (likely(root_idx < (1ull << arr->node_size_log2)))
         break;

      /* In this case, we have a root but its level is low enough that the
       * requested index is out-of-bounds.
       */
      struct util_sparse_array_node *new_root =
         _util_sparse_array_alloc_node(arr, root->level + 1);

      struct util_sparse_array_node **new_root_children =
         _util_sparse_array_node_data(new_root);
      new_root_children[0] = root;

      /* We only add one at a time instead of the whole tree because it's
       * easier to ensure correctness of both the tree building and the
       * clean-up path.  Because we're only adding one node we never have to
       * worry about trying to free multiple things without freeing the old
       * things.
       */
      root = _util_sparse_array_set_or_free_node(&arr->root, root, new_root);
   }

   struct util_sparse_array_node *node = root;
   while (node->level > 0) {
      uint64_t child_idx = (idx >> (node->level * arr->node_size_log2)) &
                           ((1ull << arr->node_size_log2) - 1);

      struct util_sparse_array_node **children =
         _util_sparse_array_node_data(node);
      struct util_sparse_array_node *child =
         p_atomic_read(&children[child_idx]);

      if (unlikely(child == NULL)) {
         child = _util_sparse_array_alloc_node(arr, node->level - 1);
         child = _util_sparse_array_set_or_free_node(&children[child_idx],
                                                     NULL, child);
      }

      node = child;
   }

   uint64_t elem_idx = idx & ((1ull << arr->node_size_log2) - 1);
   return (void *)((char *)_util_sparse_array_node_data(node) +
                   (elem_idx * arr->elem_size));
}

static void
validate_node_level(struct util_sparse_array *arr,
                    struct util_sparse_array_node *node,
                    unsigned level)
{
   assert(node->level == level);

   if (node->level > 0) {
      struct util_sparse_array_node **children =
         _util_sparse_array_node_data(node);
      size_t node_size = 1ull << arr->node_size_log2;
      for (size_t i = 0; i < node_size; i++) {
         if (children[i] != NULL)
            validate_node_level(arr, children[i], level - 1);
      }
   }
}

void
util_sparse_array_validate(struct util_sparse_array *arr)
{
   validate_node_level(arr, arr->root, arr->root->level);
}

void
util_sparse_array_free_list_init(struct util_sparse_array_free_list *fl,
                                 struct util_sparse_array *arr,
                                 uint32_t sentinel,
                                 uint32_t next_offset)
{
   fl->head = sentinel;
   fl->arr = arr;
   fl->sentinel = sentinel;
   fl->next_offset = next_offset;
}

static uint64_t
free_list_head(uint64_t old, uint32_t next)
{
   return ((old & 0xffffffff00000000ull) + 0x100000000ull) | next;
}

void
util_sparse_array_free_list_push(struct util_sparse_array_free_list *fl,
                                 uint32_t *items, unsigned num_items)
{
   assert(num_items > 0);
   assert(items[0] != fl->sentinel);
   void *last_elem = util_sparse_array_get(fl->arr, items[0]);
   uint32_t *last_next = (uint32_t *)((char *)last_elem + fl->next_offset);
   for (unsigned i = 1; i < num_items; i++) {
      *last_next = items[i];
      assert(items[i] != fl->sentinel);
      last_elem = util_sparse_array_get(fl->arr, items[i]);
      last_next = (uint32_t *)((char *)last_elem + fl->next_offset);
   }

   uint64_t current_head, old_head;
   old_head = p_atomic_read(&fl->head);
   do {
      current_head = old_head;
      *last_next = current_head; /* Index is the bottom 32 bits */
      uint64_t new_head = free_list_head(current_head, items[0]);
      old_head = p_atomic_cmpxchg(&fl->head, current_head, new_head);
   } while (old_head != current_head);
}

uint32_t
util_sparse_array_free_list_pop_idx(struct util_sparse_array_free_list *fl)
{
   uint64_t current_head;

   current_head = p_atomic_read(&fl->head);
   while (1) {
      if ((uint32_t)current_head == fl->sentinel)
         return fl->sentinel;

      uint32_t head_idx = current_head; /* Index is the bottom 32 bits */
      void *head_elem = util_sparse_array_get(fl->arr, head_idx);
      uint32_t *head_next = (uint32_t *)((char *)head_elem + fl->next_offset);
      uint32_t new_head = free_list_head(current_head, *head_next);
      uint64_t old_head = p_atomic_cmpxchg(&fl->head, current_head, new_head);
      if (old_head == current_head)
         return head_idx;
      current_head = old_head;
   }
}

void *
util_sparse_array_free_list_pop_elem(struct util_sparse_array_free_list *fl)
{
   uint64_t current_head;

   current_head = p_atomic_read(&fl->head);
   while (1) {
      if ((uint32_t)current_head == fl->sentinel)
         return NULL;

      uint32_t head_idx = current_head; /* Index is the bottom 32 bits */
      void *head_elem = util_sparse_array_get(fl->arr, head_idx);
      uint32_t *head_next = (uint32_t *)((char *)head_elem + fl->next_offset);
      uint32_t new_head = free_list_head(current_head, *head_next);
      uint64_t old_head = p_atomic_cmpxchg(&fl->head, current_head, new_head);
      if (old_head == current_head)
         return head_elem;
      current_head = old_head;
   }
}
