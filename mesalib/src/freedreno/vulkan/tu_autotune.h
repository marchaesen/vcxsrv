/*
 * Copyright © 2021 Igalia S.L.
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

#ifndef TU_AUTOTUNE_H
#define TU_AUTOTUNE_H

#include "util/hash_table.h"
#include "util/list.h"
#include "util/rwlock.h"

#define autotune_offset(base, ptr) ((uint8_t *)(ptr) - (uint8_t *)(base))
#define autotune_results_ptr(at, member)             \
   (at->results_bo->iova +                           \
      autotune_offset((at)->results, &(at)->results->member))

struct tu_device;
struct tu_cmd_buffer;

struct tu_autotune_results;
struct tu_renderpass_history;

/**
 * "autotune" our decisions about bypass vs GMEM rendering, based on historical
 * data about a given render target.
 *
 * In deciding which path to take there are tradeoffs, including some that
 * are not reasonably estimateable without having some additional information:
 *
 *  (1) If you know you are touching every pixel (ie. there is a clear),
 *      then the GMEM path will at least not cost more memory bandwidth than
 *      sysmem[1]
 *
 *  (2) If there is no clear, GMEM could potentially cost *more* bandwidth
 *      if there is sysmem->GMEM restore pass.
 *
 *  (3) If you see a high draw count, that is an indication that there will be
 *      enough pixels accessed multiple times to benefit from the reduced
 *      memory bandwidth that GMEM brings
 *
 *  (4) But high draw count where there is not much overdraw can actually be
 *      faster in bypass mode if it is pushing a lot of state change, due to
 *      not having to go thru the state changes per-tile[1]
 *
 * The approach taken is to measure the samples-passed for the batch to estimate
 * the amount of overdraw to detect cases where the number of pixels touched is
 * low.
 *
 * [1] ignoring early-tile-exit optimizations, but any draw that touches all/
 *     most of the tiles late in the tile-pass can defeat that
 */
struct tu_autotune {

   /* We may have to disable autotuner if there are too many
    * renderpasses in-flight.
    */
   bool enabled;

   /**
    * Cache to map renderpass key to historical information about
    * rendering to that particular render target.
    */
   struct hash_table *ht;
   struct u_rwlock ht_lock;

   /**
    * GPU buffer used to communicate back results to the CPU
    */
   struct tu_bo *results_bo;
   struct tu_autotune_results *results;

   /**
    * List of per-renderpass results that we are waiting for the GPU
    * to finish with before reading back the results.
    */
   struct list_head pending_results;

   /**
    * List of per-submission CS that we are waiting for the GPU
    * to finish using.
    */
   struct list_head pending_submission_cs;

   uint32_t fence_counter;
   uint32_t idx_counter;
};

#define TU_AUTOTUNE_MAX_RESULTS 256

/**
 * The layout of the memory used to read back per-batch results from the
 * GPU
 *
 * Note this struct is intentionally aligned to 4k.  And hw requires the
 * sample start/stop locations to be 128b aligned.
 */
struct tu_autotune_results {

   /**
    * The GPU writes back a "fence" seqno value from the cmdstream after
    * it finishes the submission, so that the CPU knows when
    * results are valid.
    */
   uint32_t fence;

   uint32_t __pad0;
   uint64_t __pad1;

   /**
    * From the cmdstream, the captured samples-passed values are recorded
    * at the start and end of the batch.
    *
    * Note that we do the math on the CPU to avoid a WFI.  But pre-emption
    * may force us to revisit that.
    */
   struct {
      uint64_t samples_start;
      uint64_t __pad0;
      uint64_t samples_end;
      uint64_t __pad1;
   } result[TU_AUTOTUNE_MAX_RESULTS];
};

/**
 * Tracks the results from an individual renderpass. Initially created
 * per renderpass, and appended to the tail of at->pending_results. At a later
 * time, when the GPU has finished writing the results, we fill samples_passed.
 */
struct tu_renderpass_result {

   /**
    * The index/slot in tu_autotune_results::result[] to write start/end
    * counter to
    */
   unsigned idx;

   /*
    * Below here, only used internally within autotune
    */
   uint64_t rp_key;
   struct tu_renderpass_history *history;
   struct list_head node;
   uint32_t fence;
   uint64_t samples_passed;
};

VkResult tu_autotune_init(struct tu_autotune *at, struct tu_device *dev);
void tu_autotune_fini(struct tu_autotune *at, struct tu_device *dev);

bool tu_autotune_use_bypass(struct tu_autotune *at,
                            struct tu_cmd_buffer *cmd_buffer,
                            struct tu_renderpass_result **autotune_result);
void tu_autotune_free_results(struct list_head *results);

bool tu_autotune_submit_requires_fence(struct tu_cmd_buffer **cmd_buffers,
                                       uint32_t cmd_buffer_count);

/**
 * A magic 8-ball that tells the gmem code whether we should do bypass mode
 * for moar fps.
 */
struct tu_cs *tu_autotune_on_submit(struct tu_device *dev,
                                    struct tu_autotune *at,
                                    struct tu_cmd_buffer **cmd_buffers,
                                    uint32_t cmd_buffer_count);


#endif /* TU_AUTOTUNE_H */
