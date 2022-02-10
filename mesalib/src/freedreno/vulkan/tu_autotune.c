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

#include <vulkan/vulkan_core.h>

#include "tu_autotune.h"
#include "tu_private.h"
#include "tu_cs.h"

/* In Vulkan application may fill command buffer from many threads
 * and expect no locking to occur. We do introduce the possibility of
 * locking on renderpass end, however assuming that application
 * doesn't have a huge amount of slightly different renderpasses,
 * there would be minimal to none contention.
 *
 * Other assumptions are:
 * - Application does submit command buffers soon after their creation.
 *
 * Breaking the above may lead to some decrease in performance or
 * autotuner turning itself off.
 */

#define TU_AUTOTUNE_DEBUG_LOG 0
/* Dump history entries on autotuner finish,
 * could be used to gather data from traces.
 */
#define TU_AUTOTUNE_LOG_AT_FINISH 0

#define MAX_HISTORY_RESULTS 5
#define MAX_HISTORY_LIFETIME 128

/**
 * Tracks results for a given renderpass key
 */
struct tu_renderpass_history {
   uint64_t key;

   /* We would delete old history entries */
   uint32_t last_fence;

   /**
    * List of recent fd_renderpass_result's
    */
   struct list_head results;
   uint32_t num_results;

   uint32_t avg_samples;
};

/* Holds per-submission cs which writes the fence. */
struct tu_submission_fence_cs {
   struct list_head node;
   struct tu_cs cs;
   uint32_t fence;
};

#define APPEND_TO_HASH(state, field) \
   XXH64_update(state, &field, sizeof(field));

static uint64_t
hash_renderpass_instance(const struct tu_render_pass *pass,
                         const struct tu_framebuffer *framebuffer,
                         const struct tu_cmd_buffer *cmd) {
   XXH64_state_t hash_state;
   XXH64_reset(&hash_state, 0);

   APPEND_TO_HASH(&hash_state, framebuffer->width);
   APPEND_TO_HASH(&hash_state, framebuffer->height);
   APPEND_TO_HASH(&hash_state, framebuffer->layers);

   APPEND_TO_HASH(&hash_state, pass->attachment_count);
   XXH64_update(&hash_state, pass->attachments, pass->attachment_count * sizeof(pass->attachments[0]));

   for (unsigned i = 0; i < pass->attachment_count; i++) {
      APPEND_TO_HASH(&hash_state, cmd->state.attachments[i]->view.width);
      APPEND_TO_HASH(&hash_state, cmd->state.attachments[i]->view.height);
      APPEND_TO_HASH(&hash_state, cmd->state.attachments[i]->image->vk_format);
      APPEND_TO_HASH(&hash_state, cmd->state.attachments[i]->image->layer_count);
      APPEND_TO_HASH(&hash_state, cmd->state.attachments[i]->image->level_count);
   }

   APPEND_TO_HASH(&hash_state, pass->subpass_count);
   for (unsigned i = 0; i < pass->subpass_count; i++) {
      APPEND_TO_HASH(&hash_state, pass->subpasses[i].samples);
      APPEND_TO_HASH(&hash_state, pass->subpasses[i].input_count);
      APPEND_TO_HASH(&hash_state, pass->subpasses[i].color_count);
      APPEND_TO_HASH(&hash_state, pass->subpasses[i].resolve_count);
   }

   return XXH64_digest(&hash_state);
}

static void
history_destructor(void *h)
{
   struct tu_renderpass_history *history = h;

   list_for_each_entry_safe(struct tu_renderpass_result, result,
                            &history->results, node) {
      ralloc_free(result);
   }
}

static void
result_destructor(void *r)
{
   struct tu_renderpass_result *result = r;

   /* Just in case we manage to somehow still be on the pending_results list: */
   list_del(&result->node);
}

static bool
get_history(struct tu_autotune *at, uint64_t rp_key, uint32_t *avg_samples)
{
   bool has_history = false;

   /* If the lock contantion would be found in the wild -
    * we could use try_lock here.
    */
   u_rwlock_rdlock(&at->ht_lock);
   struct hash_entry *entry =
      _mesa_hash_table_search(at->ht, &rp_key);
   if (entry) {
      struct tu_renderpass_history *history = entry->data;
      if (history->num_results > 0) {
         *avg_samples = p_atomic_read(&history->avg_samples);
         has_history = true;
      }
   }
   u_rwlock_rdunlock(&at->ht_lock);

   return has_history;
}

static struct tu_renderpass_result *
create_history_result(struct tu_autotune *at, uint64_t rp_key)
{
   struct tu_renderpass_result *result = rzalloc_size(NULL, sizeof(*result));

   result->idx = p_atomic_inc_return(&at->idx_counter);
   result->rp_key = rp_key;

   ralloc_set_destructor(result, result_destructor);

   return result;
}

static void
history_add_result(struct tu_renderpass_history *history,
                      struct tu_renderpass_result *result)
{
   list_delinit(&result->node);
   list_add(&result->node, &history->results);

   if (history->num_results < MAX_HISTORY_RESULTS) {
      history->num_results++;
   } else {
      /* Once above the limit, start popping old results off the
       * tail of the list:
       */
      struct tu_renderpass_result *old_result =
         list_last_entry(&history->results, struct tu_renderpass_result, node);
      list_delinit(&old_result->node);
      ralloc_free(old_result);
   }

   /* Do calculations here to avoid locking history in tu_autotune_use_bypass */
   uint32_t total_samples = 0;
   list_for_each_entry(struct tu_renderpass_result, result,
                       &history->results, node) {
      total_samples += result->samples_passed;
   }

   float avg_samples = (float)total_samples / (float)history->num_results;
   p_atomic_set(&history->avg_samples, (uint32_t)avg_samples);
}

static void
process_results(struct tu_autotune *at)
{
   uint32_t current_fence = at->results->fence;

   uint32_t min_idx = ~0;
   uint32_t max_idx = 0;

   list_for_each_entry_safe(struct tu_renderpass_result, result,
                            &at->pending_results, node) {
      if (result->fence > current_fence)
         break;

      struct tu_renderpass_history *history = result->history;

      min_idx = MIN2(min_idx, result->idx);
      max_idx = MAX2(max_idx, result->idx);
      uint32_t idx = result->idx % ARRAY_SIZE(at->results->result);

      result->samples_passed = at->results->result[idx].samples_end -
                               at->results->result[idx].samples_start;

      history_add_result(history, result);
   }

   list_for_each_entry_safe(struct tu_submission_fence_cs, submission_cs,
                            &at->pending_submission_cs, node) {
      if (submission_cs->fence > current_fence)
         break;

      list_del(&submission_cs->node);
      tu_cs_finish(&submission_cs->cs);
      free(submission_cs);
   }

   if (max_idx - min_idx > TU_AUTOTUNE_MAX_RESULTS) {
      /* If results start to trample each other it's better to bail out */
      at->enabled = false;
      mesa_logw("disabling sysmem vs gmem autotuner because results "
                "are trampling each other: min_idx=%u, max_idx=%u",
                min_idx, max_idx);
   }
}

static struct tu_cs *
create_fence_cs(struct tu_device *dev, struct tu_autotune *at)
{
   struct tu_submission_fence_cs *submission_cs =
      calloc(1, sizeof(struct tu_submission_fence_cs));
   submission_cs->fence = at->fence_counter;

   tu_cs_init(&submission_cs->cs, dev, TU_CS_MODE_GROW, 5);
   tu_cs_begin(&submission_cs->cs);

   tu_cs_emit_pkt7(&submission_cs->cs, CP_EVENT_WRITE, 4);
   tu_cs_emit(&submission_cs->cs, CP_EVENT_WRITE_0_EVENT(CACHE_FLUSH_TS));
   tu_cs_emit_qw(&submission_cs->cs, autotune_results_ptr(at, fence));
   tu_cs_emit(&submission_cs->cs, at->fence_counter);

   tu_cs_end(&submission_cs->cs);

   list_addtail(&submission_cs->node, &at->pending_submission_cs);

   return &submission_cs->cs;
}

struct tu_cs *
tu_autotune_on_submit(struct tu_device *dev,
                      struct tu_autotune *at,
                      struct tu_cmd_buffer **cmd_buffers,
                      uint32_t cmd_buffer_count)
{
   /* We are single-threaded here */

   process_results(at);

   /* pre-increment so zero isn't valid fence */
   uint32_t new_fence = ++at->fence_counter;

   /* Create history entries here to minimize work and locking being
    * done on renderpass end.
    */
   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      struct tu_cmd_buffer *cmdbuf = cmd_buffers[i];
      list_for_each_entry_safe(struct tu_renderpass_result, result,
                          &cmdbuf->renderpass_autotune_results, node) {
         struct tu_renderpass_history *history;
         struct hash_entry *entry =
            _mesa_hash_table_search(at->ht, &result->rp_key);
         if (!entry) {
            history = rzalloc_size(NULL, sizeof(*history));
            ralloc_set_destructor(history, history_destructor);
            history->key = result->rp_key;
            list_inithead(&history->results);

            u_rwlock_wrlock(&at->ht_lock);
            _mesa_hash_table_insert(at->ht, &history->key, history);
            u_rwlock_wrunlock(&at->ht_lock);
         } else {
            history = (struct tu_renderpass_history *) entry->data;
         }

         history->last_fence = new_fence;

         result->fence = new_fence;
         result->history = history;
      }

      if (!list_is_empty(&cmdbuf->renderpass_autotune_results)) {
         list_splicetail(&cmdbuf->renderpass_autotune_results,
                         &at->pending_results);
         list_inithead(&cmdbuf->renderpass_autotune_results);
      }
   }

#if TU_AUTOTUNE_DEBUG_LOG != 0
   mesa_logi("Total history entries: %u", at->ht->entries);
#endif

   /* Cleanup old entries from history table. The assumption
    * here is that application doesn't hold many old unsubmitted
    * command buffers, otherwise this table may grow big.
    */
   hash_table_foreach(at->ht, entry) {
      struct tu_renderpass_history *history = entry->data;
      if (history->last_fence == 0 ||
          (new_fence - history->last_fence) <= MAX_HISTORY_LIFETIME)
         continue;

#if TU_AUTOTUNE_DEBUG_LOG != 0
      mesa_logi("Removed old history entry %016"PRIx64"", history->key);
#endif

      u_rwlock_wrlock(&at->ht_lock);
      _mesa_hash_table_remove_key(at->ht, &history->key);
      u_rwlock_wrunlock(&at->ht_lock);

      ralloc_free(history);
   }

   return create_fence_cs(dev, at);
}

static bool
renderpass_key_equals(const void *_a, const void *_b)
{
   return *(uint64_t *)_a == *(uint64_t *)_b;
}

static uint32_t
renderpass_key_hash(const void *_a)
{
   return *((uint64_t *) _a) & 0xffffffff;
}

VkResult
tu_autotune_init(struct tu_autotune *at, struct tu_device *dev)
{
   VkResult result;

   at->enabled = true;
   at->ht = _mesa_hash_table_create(NULL,
                                    renderpass_key_hash,
                                    renderpass_key_equals);
   u_rwlock_init(&at->ht_lock);

   at->results_bo = malloc(sizeof(struct tu_bo));
   result = tu_bo_init_new(dev, at->results_bo,
                           sizeof(struct tu_autotune_results),
                           TU_BO_ALLOC_NO_FLAGS);
   if (result != VK_SUCCESS) {
      vk_startup_errorf(dev->instance, result, "Autotune BO init");
      goto fail_bo;
   }

   result = tu_bo_map(dev, at->results_bo);

   if (result != VK_SUCCESS) {
      vk_startup_errorf(dev->instance, result, "Autotune BO map");
      goto fail_map_bo;
   }

   at->results = at->results_bo->map;

   list_inithead(&at->pending_results);
   list_inithead(&at->pending_submission_cs);

   return VK_SUCCESS;

fail_map_bo:
   tu_bo_finish(dev, at->results_bo);

fail_bo:
   free(at->results_bo);
   u_rwlock_destroy(&at->ht_lock);
   _mesa_hash_table_destroy(at->ht, NULL);

   return result;
}

void
tu_autotune_fini(struct tu_autotune *at, struct tu_device *dev)
{
#if TU_AUTOTUNE_LOG_AT_FINISH != 0
   while (!list_is_empty(&at->pending_results)) {
      process_results(at);
   }

   hash_table_foreach(at->ht, entry) {
      struct tu_renderpass_history *history = entry->data;

      mesa_logi("%016"PRIx64" \tavg_passed=%u results=%u",
                history->key, history->avg_samples, history->num_results);
   }
#endif

   tu_autotune_free_results(&at->pending_results);

   hash_table_foreach(at->ht, entry) {
      struct tu_renderpass_history *history = entry->data;
      ralloc_free(history);
   }

   list_for_each_entry_safe(struct tu_submission_fence_cs, submission_cs,
                            &at->pending_submission_cs, node) {
      tu_cs_finish(&submission_cs->cs);
      free(submission_cs);
   }

   _mesa_hash_table_destroy(at->ht, NULL);
   u_rwlock_destroy(&at->ht_lock);
   tu_bo_finish(dev, at->results_bo);
   free(at->results_bo);
}

bool
tu_autotune_submit_requires_fence(struct tu_cmd_buffer **cmd_buffers,
                                  uint32_t cmd_buffer_count)
{
   for (uint32_t i = 0; i < cmd_buffer_count; i++) {
      struct tu_cmd_buffer *cmdbuf = cmd_buffers[i];
      if (!list_is_empty(&cmdbuf->renderpass_autotune_results))
         return true;
   }

   return false;
}

void
tu_autotune_free_results(struct list_head *results)
{
   list_for_each_entry_safe(struct tu_renderpass_result, result,
                            results, node) {
      ralloc_free(result);
   }
}

static bool
fallback_use_bypass(const struct tu_render_pass *pass,
                    const struct tu_framebuffer *framebuffer,
                    const struct tu_cmd_buffer *cmd_buffer)
{
   if (cmd_buffer->state.drawcall_count > 5)
      return false;

   for (unsigned i = 0; i < pass->subpass_count; i++) {
      if (pass->subpasses[i].samples != VK_SAMPLE_COUNT_1_BIT)
         return false;
   }

   return true;
}

bool
tu_autotune_use_bypass(struct tu_autotune *at,
                       struct tu_cmd_buffer *cmd_buffer,
                       struct tu_renderpass_result **autotune_result)
{
   const struct tu_render_pass *pass = cmd_buffer->state.pass;
   const struct tu_framebuffer *framebuffer = cmd_buffer->state.framebuffer;

   /* If we would want to support buffers that could be submitted
    * several times we would have to copy the sample counts of renderpasses
    * after each submission of such buffer (like with u_trace support).
    * This is rather messy and since almost all apps use ONE_TIME_SUBMIT
    * we choose to unconditionally use fallback.
    */
   bool one_time_submit = cmd_buffer->usage_flags &
      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   if (!at->enabled || !one_time_submit)
      return fallback_use_bypass(pass, framebuffer, cmd_buffer);

   /* We use 64bit hash as a key since we don't fear rare hash collision,
    * the worst that would happen is sysmem being selected when it should
    * have not, and with 64bit it would be extremely rare.
    *
    * Q: Why not make the key from framebuffer + renderpass pointers?
    * A: At least DXVK creates new framebuffers each frame while keeping
    *    renderpasses the same. Also we want to support replaying a single
    *    frame in a loop for testing.
    */
   uint64_t renderpass_key = hash_renderpass_instance(pass, framebuffer, cmd_buffer);

   *autotune_result = create_history_result(at, renderpass_key);

   uint32_t avg_samples = 0;
   if (get_history(at, renderpass_key, &avg_samples)) {
      /* TODO we should account for load/stores/clears/resolves especially
       * with low drawcall count and ~fb_size samples passed, in D3D11 games
       * we are seeing many renderpasses like:
       *  - color attachment load
       *  - single fullscreen draw
       *  - color attachment store
       */

      /* Low sample count could mean there was only a clear.. or there was
       * a clear plus draws that touch no or few samples
       */
      if (avg_samples < 500) {
#if TU_AUTOTUNE_DEBUG_LOG != 0
         mesa_logi("%016"PRIx64":%u\t avg_samples=%u selecting sysmem",
            renderpass_key, cmd_buffer->state.drawcall_count, avg_samples);
#endif
         return true;
      }

      /* Cost-per-sample is an estimate for the average number of reads+
       * writes for a given passed sample.
       */
      float sample_cost = cmd_buffer->state.total_drawcalls_cost;
      sample_cost /= cmd_buffer->state.drawcall_count;

      float single_draw_cost = (avg_samples * sample_cost) / cmd_buffer->state.drawcall_count;

      bool select_sysmem = single_draw_cost < 6000.0;

#if TU_AUTOTUNE_DEBUG_LOG != 0
      mesa_logi("%016"PRIx64":%u\t avg_samples=%u, "
          "sample_cost=%f, single_draw_cost=%f selecting %s",
          renderpass_key, cmd_buffer->state.drawcall_count, avg_samples,
          sample_cost, single_draw_cost, select_sysmem ? "sysmem" : "gmem");
#endif

      return select_sysmem;
   }

   return fallback_use_bypass(pass, framebuffer, cmd_buffer);
}
