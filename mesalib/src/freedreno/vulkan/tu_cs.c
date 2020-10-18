/*
 * Copyright © 2019 Google LLC
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_cs.h"

/**
 * Initialize a command stream.
 */
void
tu_cs_init(struct tu_cs *cs,
           struct tu_device *device,
           enum tu_cs_mode mode,
           uint32_t initial_size)
{
   assert(mode != TU_CS_MODE_EXTERNAL);

   memset(cs, 0, sizeof(*cs));

   cs->device = device;
   cs->mode = mode;
   cs->next_bo_size = initial_size;
}

/**
 * Initialize a command stream as a wrapper to an external buffer.
 */
void
tu_cs_init_external(struct tu_cs *cs, uint32_t *start, uint32_t *end)
{
   memset(cs, 0, sizeof(*cs));

   cs->mode = TU_CS_MODE_EXTERNAL;
   cs->start = cs->reserved_end = cs->cur = start;
   cs->end = end;
}

/**
 * Finish and release all resources owned by a command stream.
 */
void
tu_cs_finish(struct tu_cs *cs)
{
   for (uint32_t i = 0; i < cs->bo_count; ++i) {
      tu_bo_finish(cs->device, cs->bos[i]);
      free(cs->bos[i]);
   }

   free(cs->entries);
   free(cs->bos);
}

/**
 * Get the offset of the command packets emitted since the last call to
 * tu_cs_add_entry.
 */
static uint32_t
tu_cs_get_offset(const struct tu_cs *cs)
{
   assert(cs->bo_count);
   return cs->start - (uint32_t *) cs->bos[cs->bo_count - 1]->map;
}

/*
 * Allocate and add a BO to a command stream.  Following command packets will
 * be emitted to the new BO.
 */
static VkResult
tu_cs_add_bo(struct tu_cs *cs, uint32_t size)
{
   /* no BO for TU_CS_MODE_EXTERNAL */
   assert(cs->mode != TU_CS_MODE_EXTERNAL);

   /* no dangling command packet */
   assert(tu_cs_is_empty(cs));

   /* grow cs->bos if needed */
   if (cs->bo_count == cs->bo_capacity) {
      uint32_t new_capacity = MAX2(4, 2 * cs->bo_capacity);
      struct tu_bo **new_bos =
         realloc(cs->bos, new_capacity * sizeof(struct tu_bo *));
      if (!new_bos)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      cs->bo_capacity = new_capacity;
      cs->bos = new_bos;
   }

   struct tu_bo *new_bo = malloc(sizeof(struct tu_bo));
   if (!new_bo)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   VkResult result =
      tu_bo_init_new(cs->device, new_bo, size * sizeof(uint32_t), true);
   if (result != VK_SUCCESS) {
      free(new_bo);
      return result;
   }

   result = tu_bo_map(cs->device, new_bo);
   if (result != VK_SUCCESS) {
      tu_bo_finish(cs->device, new_bo);
      free(new_bo);
      return result;
   }

   cs->bos[cs->bo_count++] = new_bo;

   cs->start = cs->cur = cs->reserved_end = (uint32_t *) new_bo->map;
   cs->end = cs->start + new_bo->size / sizeof(uint32_t);

   return VK_SUCCESS;
}

/**
 * Reserve an IB entry.
 */
static VkResult
tu_cs_reserve_entry(struct tu_cs *cs)
{
   /* entries are only for TU_CS_MODE_GROW */
   assert(cs->mode == TU_CS_MODE_GROW);

   /* grow cs->entries if needed */
   if (cs->entry_count == cs->entry_capacity) {
      uint32_t new_capacity = MAX2(4, cs->entry_capacity * 2);
      struct tu_cs_entry *new_entries =
         realloc(cs->entries, new_capacity * sizeof(struct tu_cs_entry));
      if (!new_entries)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      cs->entry_capacity = new_capacity;
      cs->entries = new_entries;
   }

   return VK_SUCCESS;
}

/**
 * Add an IB entry for the command packets emitted since the last call to this
 * function.
 */
static void
tu_cs_add_entry(struct tu_cs *cs)
{
   /* entries are only for TU_CS_MODE_GROW */
   assert(cs->mode == TU_CS_MODE_GROW);

   /* disallow empty entry */
   assert(!tu_cs_is_empty(cs));

   /*
    * because we disallow empty entry, tu_cs_add_bo and tu_cs_reserve_entry
    * must both have been called
    */
   assert(cs->bo_count);
   assert(cs->entry_count < cs->entry_capacity);

   /* add an entry for [cs->start, cs->cur] */
   cs->entries[cs->entry_count++] = (struct tu_cs_entry) {
      .bo = cs->bos[cs->bo_count - 1],
      .size = tu_cs_get_size(cs) * sizeof(uint32_t),
      .offset = tu_cs_get_offset(cs) * sizeof(uint32_t),
   };

   cs->start = cs->cur;
}

/**
 * same behavior as tu_cs_emit_call but without the indirect
 */
VkResult
tu_cs_add_entries(struct tu_cs *cs, struct tu_cs *target)
{
   VkResult result;

   assert(cs->mode == TU_CS_MODE_GROW);
   assert(target->mode == TU_CS_MODE_GROW);

   if (!tu_cs_is_empty(cs))
      tu_cs_add_entry(cs);

   for (unsigned i = 0; i < target->entry_count; i++) {
      result = tu_cs_reserve_entry(cs);
      if (result != VK_SUCCESS)
         return result;
      cs->entries[cs->entry_count++] = target->entries[i];
   }

   return VK_SUCCESS;
}

/**
 * Begin (or continue) command packet emission.  This does nothing but sanity
 * checks currently.  \a cs must not be in TU_CS_MODE_SUB_STREAM mode.
 */
void
tu_cs_begin(struct tu_cs *cs)
{
   assert(cs->mode != TU_CS_MODE_SUB_STREAM);
   assert(tu_cs_is_empty(cs));
}

/**
 * End command packet emission.  This adds an IB entry when \a cs is in
 * TU_CS_MODE_GROW mode.
 */
void
tu_cs_end(struct tu_cs *cs)
{
   assert(cs->mode != TU_CS_MODE_SUB_STREAM);

   if (cs->mode == TU_CS_MODE_GROW && !tu_cs_is_empty(cs))
      tu_cs_add_entry(cs);
}

/**
 * Begin command packet emission to a sub-stream.  \a cs must be in
 * TU_CS_MODE_SUB_STREAM mode.
 *
 * Return \a sub_cs which is in TU_CS_MODE_EXTERNAL mode.  tu_cs_begin and
 * tu_cs_reserve_space are implied and \a sub_cs is ready for command packet
 * emission.
 */
VkResult
tu_cs_begin_sub_stream(struct tu_cs *cs, uint32_t size, struct tu_cs *sub_cs)
{
   assert(cs->mode == TU_CS_MODE_SUB_STREAM);
   assert(size);

   VkResult result = tu_cs_reserve_space(cs, size);
   if (result != VK_SUCCESS)
      return result;

   tu_cs_init_external(sub_cs, cs->cur, cs->reserved_end);
   tu_cs_begin(sub_cs);
   result = tu_cs_reserve_space(sub_cs, size);
   assert(result == VK_SUCCESS);

   return VK_SUCCESS;
}

/**
 * Allocate count*size dwords, aligned to size dwords.
 * \a cs must be in TU_CS_MODE_SUB_STREAM mode.
 *
 */
VkResult
tu_cs_alloc(struct tu_cs *cs,
            uint32_t count,
            uint32_t size,
            struct tu_cs_memory *memory)
{
   assert(cs->mode == TU_CS_MODE_SUB_STREAM);
   assert(size && size <= 1024);

   if (!count)
      return VK_SUCCESS;

   /* TODO: smarter way to deal with alignment? */

   VkResult result = tu_cs_reserve_space(cs, count * size + (size-1));
   if (result != VK_SUCCESS)
      return result;

   struct tu_bo *bo = cs->bos[cs->bo_count - 1];
   size_t offset = align(tu_cs_get_offset(cs), size);

   memory->map = bo->map + offset * sizeof(uint32_t);
   memory->iova = bo->iova + offset * sizeof(uint32_t);

   cs->start = cs->cur = (uint32_t*) bo->map + offset + count * size;

   return VK_SUCCESS;
}

/**
 * End command packet emission to a sub-stream.  \a sub_cs becomes invalid
 * after this call.
 *
 * Return an IB entry for the sub-stream.  The entry has the same lifetime as
 * \a cs.
 */
struct tu_cs_entry
tu_cs_end_sub_stream(struct tu_cs *cs, struct tu_cs *sub_cs)
{
   assert(cs->mode == TU_CS_MODE_SUB_STREAM);
   assert(cs->bo_count);
   assert(sub_cs->start == cs->cur && sub_cs->end == cs->reserved_end);
   tu_cs_sanity_check(sub_cs);

   tu_cs_end(sub_cs);

   cs->cur = sub_cs->cur;

   struct tu_cs_entry entry = {
      .bo = cs->bos[cs->bo_count - 1],
      .size = tu_cs_get_size(cs) * sizeof(uint32_t),
      .offset = tu_cs_get_offset(cs) * sizeof(uint32_t),
   };

   cs->start = cs->cur;

   return entry;
}

/**
 * Reserve space from a command stream for \a reserved_size uint32_t values.
 * This never fails when \a cs has mode TU_CS_MODE_EXTERNAL.
 */
VkResult
tu_cs_reserve_space(struct tu_cs *cs, uint32_t reserved_size)
{
   if (tu_cs_get_space(cs) < reserved_size) {
      if (cs->mode == TU_CS_MODE_EXTERNAL) {
         unreachable("cannot grow external buffer");
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      /* add an entry for the exiting command packets */
      if (!tu_cs_is_empty(cs)) {
         /* no direct command packet for TU_CS_MODE_SUB_STREAM */
         assert(cs->mode != TU_CS_MODE_SUB_STREAM);

         tu_cs_add_entry(cs);
      }

      if (cs->cond_flags) {
         /* Subtract one here to account for the DWORD field itself. */
         *cs->cond_dwords = cs->cur - cs->cond_dwords - 1;

         /* space for CP_COND_REG_EXEC in next bo */
         reserved_size += 3;
      }

      /* switch to a new BO */
      uint32_t new_size = MAX2(cs->next_bo_size, reserved_size);
      VkResult result = tu_cs_add_bo(cs, new_size);
      if (result != VK_SUCCESS)
         return result;

      /* if inside a condition, emit a new CP_COND_REG_EXEC */
      if (cs->cond_flags) {
         cs->reserved_end = cs->cur + reserved_size;

         tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 2);
         tu_cs_emit(cs, cs->cond_flags);

         cs->cond_dwords = cs->cur;

         /* Emit dummy DWORD field here */
         tu_cs_emit(cs, CP_COND_REG_EXEC_1_DWORDS(0));
      }

      /* double the size for the next bo */
      new_size <<= 1;
      if (cs->next_bo_size < new_size)
         cs->next_bo_size = new_size;
   }

   assert(tu_cs_get_space(cs) >= reserved_size);
   cs->reserved_end = cs->cur + reserved_size;

   if (cs->mode == TU_CS_MODE_GROW) {
      /* reserve an entry for the next call to this function or tu_cs_end */
      return tu_cs_reserve_entry(cs);
   }

   return VK_SUCCESS;
}

/**
 * Reset a command stream to its initial state.  This discards all comand
 * packets in \a cs, but does not necessarily release all resources.
 */
void
tu_cs_reset(struct tu_cs *cs)
{
   if (cs->mode == TU_CS_MODE_EXTERNAL) {
      assert(!cs->bo_count && !cs->entry_count);
      cs->reserved_end = cs->cur = cs->start;
      return;
   }

   for (uint32_t i = 0; i + 1 < cs->bo_count; ++i) {
      tu_bo_finish(cs->device, cs->bos[i]);
      free(cs->bos[i]);
   }

   if (cs->bo_count) {
      cs->bos[0] = cs->bos[cs->bo_count - 1];
      cs->bo_count = 1;

      cs->start = cs->cur = cs->reserved_end = (uint32_t *) cs->bos[0]->map;
      cs->end = cs->start + cs->bos[0]->size / sizeof(uint32_t);
   }

   cs->entry_count = 0;
}
