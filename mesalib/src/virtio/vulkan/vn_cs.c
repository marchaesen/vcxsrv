/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vn_cs.h"

#include "vn_instance.h"
#include "vn_renderer.h"

static void
vn_cs_encoder_sanity_check(struct vn_cs_encoder *enc)
{
   assert(enc->buffer_count <= enc->buffer_max);

   size_t total_committed_size = 0;
   for (uint32_t i = 0; i < enc->buffer_count; i++)
      total_committed_size += enc->buffers[i].committed_size;
   assert(enc->total_committed_size == total_committed_size);

   if (enc->buffer_count) {
      const struct vn_cs_encoder_buffer *cur_buf =
         &enc->buffers[enc->buffer_count - 1];
      assert(cur_buf->base <= enc->cur && enc->cur <= enc->end &&
             enc->end <= cur_buf->base + enc->current_buffer_size);
      if (cur_buf->committed_size)
         assert(enc->cur == enc->end);
   } else {
      assert(!enc->current_buffer_size);
      assert(!enc->cur && !enc->end);
   }
}

static void
vn_cs_encoder_add_buffer(struct vn_cs_encoder *enc,
                         struct vn_renderer_shmem *shmem,
                         size_t offset,
                         void *base,
                         size_t size)
{
   /* add a buffer and make it current */
   assert(enc->buffer_count < enc->buffer_max);
   struct vn_cs_encoder_buffer *cur_buf = &enc->buffers[enc->buffer_count++];
   /* shmem ownership transferred */
   cur_buf->shmem = shmem;
   cur_buf->offset = offset;
   cur_buf->base = base;
   cur_buf->committed_size = 0;

   /* update the write pointer */
   enc->cur = base;
   enc->end = base + size;
}

static void
vn_cs_encoder_commit_buffer(struct vn_cs_encoder *enc)
{
   assert(enc->buffer_count);
   struct vn_cs_encoder_buffer *cur_buf =
      &enc->buffers[enc->buffer_count - 1];
   const size_t written_size = enc->cur - cur_buf->base;
   if (cur_buf->committed_size) {
      assert(cur_buf->committed_size == written_size);
   } else {
      cur_buf->committed_size = written_size;
      enc->total_committed_size += written_size;
   }
}

static void
vn_cs_encoder_gc_buffers(struct vn_cs_encoder *enc)
{
   /* free all but the current buffer */
   assert(enc->buffer_count);
   struct vn_cs_encoder_buffer *cur_buf =
      &enc->buffers[enc->buffer_count - 1];
   for (uint32_t i = 0; i < enc->buffer_count - 1; i++)
      vn_renderer_shmem_unref(enc->instance->renderer, enc->buffers[i].shmem);

   /* move the current buffer to the beginning, skipping the used part */
   const size_t used = cur_buf->offset + cur_buf->committed_size;
   enc->buffer_count = 0;
   vn_cs_encoder_add_buffer(enc, cur_buf->shmem, used,
                            cur_buf->base + cur_buf->committed_size,
                            enc->current_buffer_size - used);

   enc->total_committed_size = 0;
}

void
vn_cs_encoder_init_indirect(struct vn_cs_encoder *enc,
                            struct vn_instance *instance,
                            size_t min_size)
{
   memset(enc, 0, sizeof(*enc));
   enc->instance = instance;
   enc->min_buffer_size = min_size;
   enc->indirect = true;
}

void
vn_cs_encoder_fini(struct vn_cs_encoder *enc)
{
   if (unlikely(!enc->indirect))
      return;

   for (uint32_t i = 0; i < enc->buffer_count; i++)
      vn_renderer_shmem_unref(enc->instance->renderer, enc->buffers[i].shmem);
   if (enc->buffers)
      free(enc->buffers);
}

/**
 * Reset a cs for reuse.
 */
void
vn_cs_encoder_reset(struct vn_cs_encoder *enc)
{
   /* enc->error is sticky */
   if (likely(enc->buffer_count))
      vn_cs_encoder_gc_buffers(enc);
}

static uint32_t
next_array_size(uint32_t cur_size, uint32_t min_size)
{
   const uint32_t next_size = cur_size ? cur_size * 2 : min_size;
   return next_size > cur_size ? next_size : 0;
}

static size_t
next_buffer_size(size_t cur_size, size_t min_size, size_t need)
{
   size_t next_size = cur_size ? cur_size * 2 : min_size;
   while (next_size < need) {
      next_size *= 2;
      if (!next_size)
         return 0;
   }
   return next_size;
}

static bool
vn_cs_encoder_grow_buffer_array(struct vn_cs_encoder *enc)
{
   const uint32_t buf_max = next_array_size(enc->buffer_max, 4);
   if (!buf_max)
      return false;

   void *bufs = realloc(enc->buffers, sizeof(*enc->buffers) * buf_max);
   if (!bufs)
      return false;

   enc->buffers = bufs;
   enc->buffer_max = buf_max;

   return true;
}

/**
 * Add a new vn_cs_encoder_buffer to a cs.
 */
bool
vn_cs_encoder_reserve_internal(struct vn_cs_encoder *enc, size_t size)
{
   if (unlikely(!enc->indirect))
      return false;

   if (enc->buffer_count >= enc->buffer_max) {
      if (!vn_cs_encoder_grow_buffer_array(enc))
         return false;
      assert(enc->buffer_count < enc->buffer_max);
   }

   size_t buf_size = 0;
   if (likely(enc->buffer_count)) {
      vn_cs_encoder_commit_buffer(enc);

      /* TODO better strategy to grow buffer size */
      const struct vn_cs_encoder_buffer *cur_buf =
         &enc->buffers[enc->buffer_count - 1];
      if (cur_buf->offset)
         buf_size = next_buffer_size(0, enc->current_buffer_size, size);
   }

   if (!buf_size) {
      buf_size = next_buffer_size(enc->current_buffer_size,
                                  enc->min_buffer_size, size);
      if (!buf_size)
         return false;
   }

   struct vn_renderer_shmem *shmem =
      vn_renderer_shmem_create(enc->instance->renderer, buf_size);
   if (!shmem)
      return false;

   uint32_t roundtrip;
   VkResult result = vn_instance_submit_roundtrip(enc->instance, &roundtrip);
   if (result != VK_SUCCESS) {
      vn_renderer_shmem_unref(enc->instance->renderer, shmem);
      return false;
   }

   vn_cs_encoder_add_buffer(enc, shmem, 0, shmem->mmap_ptr, buf_size);
   enc->current_buffer_size = buf_size;
   enc->current_buffer_roundtrip = roundtrip;

   vn_cs_encoder_sanity_check(enc);

   return true;
}

/*
 * Commit written data.
 */
void
vn_cs_encoder_commit(struct vn_cs_encoder *enc)
{
   if (likely(enc->buffer_count)) {
      vn_cs_encoder_commit_buffer(enc);

      /* trigger the slow path on next vn_cs_encoder_reserve */
      enc->end = enc->cur;
   }

   vn_cs_encoder_sanity_check(enc);
}
