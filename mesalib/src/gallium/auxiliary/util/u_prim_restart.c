/*
 * Copyright 2014 VMware, Inc.
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
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */



#include "u_inlines.h"
#include "u_memory.h"
#include "u_prim_restart.h"


/**
 * Translate an index buffer for primitive restart.
 * Create a new index buffer which is a copy of the original index buffer
 * except that instances of 'restart_index' are converted to 0xffff or
 * 0xffffffff.
 * Also, index buffers using 1-byte indexes are converted to 2-byte indexes.
 */
enum pipe_error
util_translate_prim_restart_ib(struct pipe_context *context,
                               struct pipe_index_buffer *src_buffer,
                               struct pipe_resource **dst_buffer,
                               unsigned num_indexes,
                               unsigned restart_index)
{
   struct pipe_screen *screen = context->screen;
   struct pipe_transfer *src_transfer = NULL, *dst_transfer = NULL;
   void *src_map = NULL, *dst_map = NULL;
   const unsigned src_index_size = src_buffer->index_size;
   unsigned dst_index_size;

   /* 1-byte indexes are converted to 2-byte indexes, 4-byte stays 4-byte */
   dst_index_size = MAX2(2, src_buffer->index_size);
   assert(dst_index_size == 2 || dst_index_size == 4);

   /* no user buffers for now */
   assert(src_buffer->user_buffer == NULL);

   /* Create new index buffer */
   *dst_buffer = pipe_buffer_create(screen, PIPE_BIND_INDEX_BUFFER,
                                    PIPE_USAGE_STREAM,
                                    num_indexes * dst_index_size);
   if (!*dst_buffer)
      goto error;

   /* Map new / dest index buffer */
   dst_map = pipe_buffer_map(context, *dst_buffer,
                             PIPE_TRANSFER_WRITE, &dst_transfer);
   if (!dst_map)
      goto error;

   /* Map original / src index buffer */
   src_map = pipe_buffer_map_range(context, src_buffer->buffer,
                                   src_buffer->offset,
                                   num_indexes * src_index_size,
                                   PIPE_TRANSFER_READ,
                                   &src_transfer);
   if (!src_map)
      goto error;

   if (src_index_size == 1 && dst_index_size == 2) {
      uint8_t *src = (uint8_t *) src_map;
      uint16_t *dst = (uint16_t *) dst_map;
      unsigned i;
      for (i = 0; i < num_indexes; i++) {
         dst[i] = (src[i] == restart_index) ? 0xffff : src[i];
      }
   }
   else if (src_index_size == 2 && dst_index_size == 2) {
      uint16_t *src = (uint16_t *) src_map;
      uint16_t *dst = (uint16_t *) dst_map;
      unsigned i;
      for (i = 0; i < num_indexes; i++) {
         dst[i] = (src[i] == restart_index) ? 0xffff : src[i];
      }
   }
   else {
      uint32_t *src = (uint32_t *) src_map;
      uint32_t *dst = (uint32_t *) dst_map;
      unsigned i;
      assert(src_index_size == 4);
      assert(dst_index_size == 4);
      for (i = 0; i < num_indexes; i++) {
         dst[i] = (src[i] == restart_index) ? 0xffffffff : src[i];
      }
   }

   pipe_buffer_unmap(context, src_transfer);
   pipe_buffer_unmap(context, dst_transfer);

   return PIPE_OK;

error:
   if (src_transfer)
      pipe_buffer_unmap(context, src_transfer);
   if (dst_transfer)
      pipe_buffer_unmap(context, dst_transfer);
   if (*dst_buffer)
      screen->resource_destroy(screen, *dst_buffer);
   return PIPE_ERROR_OUT_OF_MEMORY;
}


/** Helper structs for util_draw_vbo_without_prim_restart() */

struct range {
   unsigned start, count;
};

struct range_info {
   struct range *ranges;
   unsigned count, max;
};


/**
 * Helper function for util_draw_vbo_without_prim_restart()
 * \return true for success, false if out of memory
 */
static boolean
add_range(struct range_info *info, unsigned start, unsigned count)
{
   if (info->max == 0) {
      info->max = 10;
      info->ranges = MALLOC(info->max * sizeof(struct range));
      if (!info->ranges) {
         return FALSE;
      }
   }
   else if (info->count == info->max) {
      /* grow the ranges[] array */
      info->ranges = REALLOC(info->ranges,
                             info->max * sizeof(struct range),
                             2 * info->max * sizeof(struct range));
      if (!info->ranges) {
         return FALSE;
      }

      info->max *= 2;
   }

   /* save the range */
   info->ranges[info->count].start = start;
   info->ranges[info->count].count = count;
   info->count++;

   return TRUE;
}


/**
 * Implement primitive restart by breaking an indexed primitive into
 * pieces which do not contain restart indexes.  Each piece is then
 * drawn by calling pipe_context::draw_vbo().
 * \return PIPE_OK if no error, an error code otherwise.
 */
enum pipe_error
util_draw_vbo_without_prim_restart(struct pipe_context *context,
                                   const struct pipe_index_buffer *ib,
                                   const struct pipe_draw_info *info)
{
   const void *src_map;
   struct range_info ranges = {0};
   struct pipe_draw_info new_info;
   struct pipe_transfer *src_transfer = NULL;
   unsigned i, start, count;

   assert(info->indexed);
   assert(info->primitive_restart);

   /* Get pointer to the index data */
   if (ib->buffer) {
      /* map the index buffer (only the range we need to scan) */
      src_map = pipe_buffer_map_range(context, ib->buffer,
                                      ib->offset + info->start * ib->index_size,
                                      info->count * ib->index_size,
                                      PIPE_TRANSFER_READ,
                                      &src_transfer);
      if (!src_map) {
         return PIPE_ERROR_OUT_OF_MEMORY;
      }
   }
   else {
      if (!ib->user_buffer) {
         debug_printf("User-space index buffer is null!");
         return PIPE_ERROR_BAD_INPUT;
      }
      src_map = (const uint8_t *) ib->user_buffer
         + ib->offset
         + info->start * ib->index_size;
   }

#define SCAN_INDEXES(TYPE) \
   for (i = 0; i <= info->count; i++) { \
      if (i == info->count || \
          ((const TYPE *) src_map)[i] == info->restart_index) { \
         /* cut / restart */ \
         if (count > 0) { \
            if (!add_range(&ranges, info->start + start, count)) { \
               if (src_transfer) \
                  pipe_buffer_unmap(context, src_transfer); \
               return PIPE_ERROR_OUT_OF_MEMORY; \
            } \
         } \
         start = i + 1; \
         count = 0; \
      } \
      else { \
         count++; \
      } \
   }

   start = info->start;
   count = 0;
   switch (ib->index_size) {
   case 1:
      SCAN_INDEXES(uint8_t);
      break;
   case 2:
      SCAN_INDEXES(uint16_t);
      break;
   case 4:
      SCAN_INDEXES(uint32_t);
      break;
   default:
      assert(!"Bad index size");
      return PIPE_ERROR_BAD_INPUT;
   }

   /* unmap index buffer */
   if (src_transfer)
      pipe_buffer_unmap(context, src_transfer);

   /* draw ranges between the restart indexes */
   new_info = *info;
   new_info.primitive_restart = FALSE;
   for (i = 0; i < ranges.count; i++) {
      new_info.start = ranges.ranges[i].start;
      new_info.count = ranges.ranges[i].count;
      context->draw_vbo(context, &new_info);
   }

   FREE(ranges.ranges);

   return PIPE_OK;
}
