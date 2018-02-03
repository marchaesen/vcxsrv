/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "util/u_threaded_context.h"
#include "util/u_cpu_detect.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"

/* 0 = disabled, 1 = assertions, 2 = printfs */
#define TC_DEBUG 0

#if TC_DEBUG >= 1
#define tc_assert assert
#else
#define tc_assert(x)
#endif

#if TC_DEBUG >= 2
#define tc_printf printf
#define tc_asprintf asprintf
#define tc_strcmp strcmp
#else
#define tc_printf(...)
#define tc_asprintf(...) 0
#define tc_strcmp(...) 0
#endif

#define TC_SENTINEL 0x5ca1ab1e

enum tc_call_id {
#define CALL(name) TC_CALL_##name,
#include "u_threaded_context_calls.h"
#undef CALL
   TC_NUM_CALLS,
};

typedef void (*tc_execute)(struct pipe_context *pipe, union tc_payload *payload);

static const tc_execute execute_func[TC_NUM_CALLS];

static void
tc_batch_check(MAYBE_UNUSED struct tc_batch *batch)
{
   tc_assert(batch->sentinel == TC_SENTINEL);
   tc_assert(batch->num_total_call_slots <= TC_CALLS_PER_BATCH);
}

static void
tc_debug_check(struct threaded_context *tc)
{
   for (unsigned i = 0; i < TC_MAX_BATCHES; i++) {
      tc_batch_check(&tc->batch_slots[i]);
      tc_assert(tc->batch_slots[i].pipe == tc->pipe);
   }
}

static void
tc_batch_execute(void *job, UNUSED int thread_index)
{
   struct tc_batch *batch = job;
   struct pipe_context *pipe = batch->pipe;
   struct tc_call *last = &batch->call[batch->num_total_call_slots];

   tc_batch_check(batch);

   assert(!batch->token);

   for (struct tc_call *iter = batch->call; iter != last;
        iter += iter->num_call_slots) {
      tc_assert(iter->sentinel == TC_SENTINEL);
      execute_func[iter->call_id](pipe, &iter->payload);
   }

   tc_batch_check(batch);
   batch->num_total_call_slots = 0;
}

static void
tc_batch_flush(struct threaded_context *tc)
{
   struct tc_batch *next = &tc->batch_slots[tc->next];

   tc_assert(next->num_total_call_slots != 0);
   tc_batch_check(next);
   tc_debug_check(tc);
   p_atomic_add(&tc->num_offloaded_slots, next->num_total_call_slots);

   if (next->token) {
      next->token->tc = NULL;
      tc_unflushed_batch_token_reference(&next->token, NULL);
   }

   util_queue_add_job(&tc->queue, next, &next->fence, tc_batch_execute,
                      NULL);
   tc->last = tc->next;
   tc->next = (tc->next + 1) % TC_MAX_BATCHES;
}

/* This is the function that adds variable-sized calls into the current
 * batch. It also flushes the batch if there is not enough space there.
 * All other higher-level "add" functions use it.
 */
static union tc_payload *
tc_add_sized_call(struct threaded_context *tc, enum tc_call_id id,
                  unsigned payload_size)
{
   struct tc_batch *next = &tc->batch_slots[tc->next];
   unsigned total_size = offsetof(struct tc_call, payload) + payload_size;
   unsigned num_call_slots = DIV_ROUND_UP(total_size, sizeof(struct tc_call));

   tc_debug_check(tc);

   if (unlikely(next->num_total_call_slots + num_call_slots > TC_CALLS_PER_BATCH)) {
      tc_batch_flush(tc);
      next = &tc->batch_slots[tc->next];
      tc_assert(next->num_total_call_slots == 0);
   }

   tc_assert(util_queue_fence_is_signalled(&next->fence));

   struct tc_call *call = &next->call[next->num_total_call_slots];
   next->num_total_call_slots += num_call_slots;

   call->sentinel = TC_SENTINEL;
   call->call_id = id;
   call->num_call_slots = num_call_slots;

   tc_debug_check(tc);
   return &call->payload;
}

#define tc_add_struct_typed_call(tc, execute, type) \
   ((struct type*)tc_add_sized_call(tc, execute, sizeof(struct type)))

#define tc_add_slot_based_call(tc, execute, type, num_slots) \
   ((struct type*)tc_add_sized_call(tc, execute, \
                                    sizeof(struct type) + \
                                    sizeof(((struct type*)NULL)->slot[0]) * \
                                    (num_slots)))

static union tc_payload *
tc_add_small_call(struct threaded_context *tc, enum tc_call_id id)
{
   return tc_add_sized_call(tc, id, 0);
}

static bool
tc_is_sync(struct threaded_context *tc)
{
   struct tc_batch *last = &tc->batch_slots[tc->last];
   struct tc_batch *next = &tc->batch_slots[tc->next];

   return util_queue_fence_is_signalled(&last->fence) &&
          !next->num_total_call_slots;
}

static void
_tc_sync(struct threaded_context *tc, MAYBE_UNUSED const char *info, MAYBE_UNUSED const char *func)
{
   struct tc_batch *last = &tc->batch_slots[tc->last];
   struct tc_batch *next = &tc->batch_slots[tc->next];
   bool synced = false;

   tc_debug_check(tc);

   /* Only wait for queued calls... */
   if (!util_queue_fence_is_signalled(&last->fence)) {
      util_queue_fence_wait(&last->fence);
      synced = true;
   }

   tc_debug_check(tc);

   if (next->token) {
      next->token->tc = NULL;
      tc_unflushed_batch_token_reference(&next->token, NULL);
   }

   /* .. and execute unflushed calls directly. */
   if (next->num_total_call_slots) {
      p_atomic_add(&tc->num_direct_slots, next->num_total_call_slots);
      tc_batch_execute(next, 0);
      synced = true;
   }

   if (synced) {
      p_atomic_inc(&tc->num_syncs);

      if (tc_strcmp(func, "tc_destroy") != 0) {
         tc_printf("sync %s %s\n", func, info);
	  }
   }

   tc_debug_check(tc);
}

#define tc_sync(tc) _tc_sync(tc, "", __func__)
#define tc_sync_msg(tc, info) _tc_sync(tc, info, __func__)

/**
 * Call this from fence_finish for same-context fence waits of deferred fences
 * that haven't been flushed yet.
 *
 * The passed pipe_context must be the one passed to pipe_screen::fence_finish,
 * i.e., the wrapped one.
 */
void
threaded_context_flush(struct pipe_context *_pipe,
                       struct tc_unflushed_batch_token *token,
                       bool prefer_async)
{
   struct threaded_context *tc = threaded_context(_pipe);

   /* This is called from the state-tracker / application thread. */
   if (token->tc && token->tc == tc) {
      struct tc_batch *last = &tc->batch_slots[tc->last];

      /* Prefer to do the flush in the driver thread if it is already
       * running. That should be better for cache locality.
       */
      if (prefer_async || !util_queue_fence_is_signalled(&last->fence))
         tc_batch_flush(tc);
      else
         tc_sync(token->tc);
   }
}

static void
tc_set_resource_reference(struct pipe_resource **dst, struct pipe_resource *src)
{
   *dst = NULL;
   pipe_resource_reference(dst, src);
}

void
threaded_resource_init(struct pipe_resource *res)
{
   struct threaded_resource *tres = threaded_resource(res);

   tres->latest = &tres->b;
   util_range_init(&tres->valid_buffer_range);
   tres->base_valid_buffer_range = &tres->valid_buffer_range;
   tres->is_shared = false;
   tres->is_user_ptr = false;
}

void
threaded_resource_deinit(struct pipe_resource *res)
{
   struct threaded_resource *tres = threaded_resource(res);

   if (tres->latest != &tres->b)
           pipe_resource_reference(&tres->latest, NULL);
   util_range_destroy(&tres->valid_buffer_range);
}

struct pipe_context *
threaded_context_unwrap_sync(struct pipe_context *pipe)
{
   if (!pipe || !pipe->priv)
      return pipe;

   tc_sync(threaded_context(pipe));
   return (struct pipe_context*)pipe->priv;
}


/********************************************************************
 * simple functions
 */

#define TC_FUNC1(func, m_payload, qualifier, type, deref, deref2) \
   static void \
   tc_call_##func(struct pipe_context *pipe, union tc_payload *payload) \
   { \
      pipe->func(pipe, deref2((type*)payload)); \
   } \
   \
   static void \
   tc_##func(struct pipe_context *_pipe, qualifier type deref param) \
   { \
      struct threaded_context *tc = threaded_context(_pipe); \
      type *p = (type*)tc_add_sized_call(tc, TC_CALL_##func, sizeof(type)); \
      *p = deref(param); \
   }

TC_FUNC1(set_active_query_state, flags, , boolean, , *)

TC_FUNC1(set_blend_color, blend_color, const, struct pipe_blend_color, *, )
TC_FUNC1(set_stencil_ref, stencil_ref, const, struct pipe_stencil_ref, *, )
TC_FUNC1(set_clip_state, clip_state, const, struct pipe_clip_state, *, )
TC_FUNC1(set_sample_mask, sample_mask, , unsigned, , *)
TC_FUNC1(set_min_samples, min_samples, , unsigned, , *)
TC_FUNC1(set_polygon_stipple, polygon_stipple, const, struct pipe_poly_stipple, *, )

TC_FUNC1(texture_barrier, flags, , unsigned, , *)
TC_FUNC1(memory_barrier, flags, , unsigned, , *)


/********************************************************************
 * queries
 */

static struct pipe_query *
tc_create_query(struct pipe_context *_pipe, unsigned query_type,
                unsigned index)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   return pipe->create_query(pipe, query_type, index);
}

static struct pipe_query *
tc_create_batch_query(struct pipe_context *_pipe, unsigned num_queries,
                      unsigned *query_types)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   return pipe->create_batch_query(pipe, num_queries, query_types);
}

static void
tc_call_destroy_query(struct pipe_context *pipe, union tc_payload *payload)
{
   struct threaded_query *tq = threaded_query(payload->query);

   if (tq->head_unflushed.next)
      LIST_DEL(&tq->head_unflushed);

   pipe->destroy_query(pipe, payload->query);
}

static void
tc_destroy_query(struct pipe_context *_pipe, struct pipe_query *query)
{
   struct threaded_context *tc = threaded_context(_pipe);

   tc_add_small_call(tc, TC_CALL_destroy_query)->query = query;
}

static void
tc_call_begin_query(struct pipe_context *pipe, union tc_payload *payload)
{
   pipe->begin_query(pipe, payload->query);
}

static boolean
tc_begin_query(struct pipe_context *_pipe, struct pipe_query *query)
{
   struct threaded_context *tc = threaded_context(_pipe);
   union tc_payload *payload = tc_add_small_call(tc, TC_CALL_begin_query);

   payload->query = query;
   return true; /* we don't care about the return value for this call */
}

struct tc_end_query_payload {
   struct threaded_context *tc;
   struct pipe_query *query;
};

static void
tc_call_end_query(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_end_query_payload *p = (struct tc_end_query_payload *)payload;
   struct threaded_query *tq = threaded_query(p->query);

   if (!tq->head_unflushed.next)
      LIST_ADD(&tq->head_unflushed, &p->tc->unflushed_queries);

   pipe->end_query(pipe, p->query);
}

static bool
tc_end_query(struct pipe_context *_pipe, struct pipe_query *query)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct threaded_query *tq = threaded_query(query);
   struct tc_end_query_payload *payload =
      tc_add_struct_typed_call(tc, TC_CALL_end_query, tc_end_query_payload);

   payload->tc = tc;
   payload->query = query;

   tq->flushed = false;

   return true; /* we don't care about the return value for this call */
}

static boolean
tc_get_query_result(struct pipe_context *_pipe,
                    struct pipe_query *query, boolean wait,
                    union pipe_query_result *result)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct threaded_query *tq = threaded_query(query);
   struct pipe_context *pipe = tc->pipe;

   if (!tq->flushed)
      tc_sync_msg(tc, wait ? "wait" : "nowait");

   bool success = pipe->get_query_result(pipe, query, wait, result);

   if (success) {
      tq->flushed = true;
      if (tq->head_unflushed.next) {
         /* This is safe because it can only happen after we sync'd. */
         LIST_DEL(&tq->head_unflushed);
      }
   }
   return success;
}

struct tc_query_result_resource {
   struct pipe_query *query;
   boolean wait;
   enum pipe_query_value_type result_type;
   int index;
   struct pipe_resource *resource;
   unsigned offset;
};

static void
tc_call_get_query_result_resource(struct pipe_context *pipe,
                                  union tc_payload *payload)
{
   struct tc_query_result_resource *p = (struct tc_query_result_resource *)payload;

   pipe->get_query_result_resource(pipe, p->query, p->wait, p->result_type,
                                   p->index, p->resource, p->offset);
   pipe_resource_reference(&p->resource, NULL);
}

static void
tc_get_query_result_resource(struct pipe_context *_pipe,
                             struct pipe_query *query, boolean wait,
                             enum pipe_query_value_type result_type, int index,
                             struct pipe_resource *resource, unsigned offset)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_query_result_resource *p =
      tc_add_struct_typed_call(tc, TC_CALL_get_query_result_resource,
                               tc_query_result_resource);

   p->query = query;
   p->wait = wait;
   p->result_type = result_type;
   p->index = index;
   tc_set_resource_reference(&p->resource, resource);
   p->offset = offset;
}

struct tc_render_condition {
   struct pipe_query *query;
   bool condition;
   unsigned mode;
};

static void
tc_call_render_condition(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_render_condition *p = (struct tc_render_condition *)payload;
   pipe->render_condition(pipe, p->query, p->condition, p->mode);
}

static void
tc_render_condition(struct pipe_context *_pipe,
                    struct pipe_query *query, boolean condition,
                    enum pipe_render_cond_flag mode)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_render_condition *p =
      tc_add_struct_typed_call(tc, TC_CALL_render_condition, tc_render_condition);

   p->query = query;
   p->condition = condition;
   p->mode = mode;
}


/********************************************************************
 * constant (immutable) states
 */

#define TC_CSO_CREATE(name, sname) \
   static void * \
   tc_create_##name##_state(struct pipe_context *_pipe, \
                            const struct pipe_##sname##_state *state) \
   { \
      struct pipe_context *pipe = threaded_context(_pipe)->pipe; \
      return pipe->create_##name##_state(pipe, state); \
   }

#define TC_CSO_BIND(name) TC_FUNC1(bind_##name##_state, cso, , void *, , *)
#define TC_CSO_DELETE(name) TC_FUNC1(delete_##name##_state, cso, , void *, , *)

#define TC_CSO_WHOLE2(name, sname) \
   TC_CSO_CREATE(name, sname) \
   TC_CSO_BIND(name) \
   TC_CSO_DELETE(name)

#define TC_CSO_WHOLE(name) TC_CSO_WHOLE2(name, name)

TC_CSO_WHOLE(blend)
TC_CSO_WHOLE(rasterizer)
TC_CSO_WHOLE(depth_stencil_alpha)
TC_CSO_WHOLE(compute)
TC_CSO_WHOLE2(fs, shader)
TC_CSO_WHOLE2(vs, shader)
TC_CSO_WHOLE2(gs, shader)
TC_CSO_WHOLE2(tcs, shader)
TC_CSO_WHOLE2(tes, shader)
TC_CSO_CREATE(sampler, sampler)
TC_CSO_DELETE(sampler)
TC_CSO_BIND(vertex_elements)
TC_CSO_DELETE(vertex_elements)

static void *
tc_create_vertex_elements_state(struct pipe_context *_pipe, unsigned count,
                                const struct pipe_vertex_element *elems)
{
   struct pipe_context *pipe = threaded_context(_pipe)->pipe;

   return pipe->create_vertex_elements_state(pipe, count, elems);
}

struct tc_sampler_states {
   ubyte shader, start, count;
   void *slot[0]; /* more will be allocated if needed */
};

static void
tc_call_bind_sampler_states(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_sampler_states *p = (struct tc_sampler_states *)payload;
   pipe->bind_sampler_states(pipe, p->shader, p->start, p->count, p->slot);
}

static void
tc_bind_sampler_states(struct pipe_context *_pipe,
                       enum pipe_shader_type shader,
                       unsigned start, unsigned count, void **states)
{
   if (!count)
      return;

   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_sampler_states *p =
      tc_add_slot_based_call(tc, TC_CALL_bind_sampler_states, tc_sampler_states, count);

   p->shader = shader;
   p->start = start;
   p->count = count;
   memcpy(p->slot, states, count * sizeof(states[0]));
}


/********************************************************************
 * immediate states
 */

static void
tc_call_set_framebuffer_state(struct pipe_context *pipe, union tc_payload *payload)
{
   struct pipe_framebuffer_state *p = (struct pipe_framebuffer_state *)payload;

   pipe->set_framebuffer_state(pipe, p);

   unsigned nr_cbufs = p->nr_cbufs;
   for (unsigned i = 0; i < nr_cbufs; i++)
      pipe_surface_reference(&p->cbufs[i], NULL);
   pipe_surface_reference(&p->zsbuf, NULL);
}

static void
tc_set_framebuffer_state(struct pipe_context *_pipe,
                         const struct pipe_framebuffer_state *fb)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_framebuffer_state *p =
      tc_add_struct_typed_call(tc, TC_CALL_set_framebuffer_state,
                               pipe_framebuffer_state);
   unsigned nr_cbufs = fb->nr_cbufs;

   p->width = fb->width;
   p->height = fb->height;
   p->samples = fb->samples;
   p->layers = fb->layers;
   p->nr_cbufs = nr_cbufs;

   for (unsigned i = 0; i < nr_cbufs; i++) {
      p->cbufs[i] = NULL;
      pipe_surface_reference(&p->cbufs[i], fb->cbufs[i]);
   }
   p->zsbuf = NULL;
   pipe_surface_reference(&p->zsbuf, fb->zsbuf);
}

static void
tc_call_set_tess_state(struct pipe_context *pipe, union tc_payload *payload)
{
   float *p = (float*)payload;
   pipe->set_tess_state(pipe, p, p + 4);
}

static void
tc_set_tess_state(struct pipe_context *_pipe,
                  const float default_outer_level[4],
                  const float default_inner_level[2])
{
   struct threaded_context *tc = threaded_context(_pipe);
   float *p = (float*)tc_add_sized_call(tc, TC_CALL_set_tess_state,
                                        sizeof(float) * 6);

   memcpy(p, default_outer_level, 4 * sizeof(float));
   memcpy(p + 4, default_inner_level, 2 * sizeof(float));
}

struct tc_constant_buffer {
   ubyte shader, index;
   struct pipe_constant_buffer cb;
};

static void
tc_call_set_constant_buffer(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_constant_buffer *p = (struct tc_constant_buffer *)payload;

   pipe->set_constant_buffer(pipe,
                             p->shader,
                             p->index,
                             &p->cb);
   pipe_resource_reference(&p->cb.buffer, NULL);
}

static void
tc_set_constant_buffer(struct pipe_context *_pipe,
                       enum pipe_shader_type shader, uint index,
                       const struct pipe_constant_buffer *cb)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_resource *buffer = NULL;
   unsigned offset;

   /* This must be done before adding set_constant_buffer, because it could
    * generate e.g. transfer_unmap and flush partially-uninitialized
    * set_constant_buffer to the driver if it was done afterwards.
    */
   if (cb && cb->user_buffer) {
      u_upload_data(tc->base.const_uploader, 0, cb->buffer_size, 64,
                    cb->user_buffer, &offset, &buffer);
   }

   struct tc_constant_buffer *p =
      tc_add_struct_typed_call(tc, TC_CALL_set_constant_buffer,
                               tc_constant_buffer);
   p->shader = shader;
   p->index = index;

   if (cb) {
      if (cb->user_buffer) {
         p->cb.buffer_size = cb->buffer_size;
         p->cb.user_buffer = NULL;
         p->cb.buffer_offset = offset;
         p->cb.buffer = buffer;
      } else {
         tc_set_resource_reference(&p->cb.buffer,
                                   cb->buffer);
         memcpy(&p->cb, cb, sizeof(*cb));
      }
   } else {
      memset(&p->cb, 0, sizeof(*cb));
   }
}

struct tc_scissors {
   ubyte start, count;
   struct pipe_scissor_state slot[0]; /* more will be allocated if needed */
};

static void
tc_call_set_scissor_states(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_scissors *p = (struct tc_scissors *)payload;
   pipe->set_scissor_states(pipe, p->start, p->count, p->slot);
}

static void
tc_set_scissor_states(struct pipe_context *_pipe,
                      unsigned start, unsigned count,
                      const struct pipe_scissor_state *states)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_scissors *p =
      tc_add_slot_based_call(tc, TC_CALL_set_scissor_states, tc_scissors, count);

   p->start = start;
   p->count = count;
   memcpy(&p->slot, states, count * sizeof(states[0]));
}

struct tc_viewports {
   ubyte start, count;
   struct pipe_viewport_state slot[0]; /* more will be allocated if needed */
};

static void
tc_call_set_viewport_states(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_viewports *p = (struct tc_viewports *)payload;
   pipe->set_viewport_states(pipe, p->start, p->count, p->slot);
}

static void
tc_set_viewport_states(struct pipe_context *_pipe,
                       unsigned start, unsigned count,
                       const struct pipe_viewport_state *states)
{
   if (!count)
      return;

   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_viewports *p =
      tc_add_slot_based_call(tc, TC_CALL_set_viewport_states, tc_viewports, count);

   p->start = start;
   p->count = count;
   memcpy(&p->slot, states, count * sizeof(states[0]));
}

struct tc_window_rects {
   bool include;
   ubyte count;
   struct pipe_scissor_state slot[0]; /* more will be allocated if needed */
};

static void
tc_call_set_window_rectangles(struct pipe_context *pipe,
                              union tc_payload *payload)
{
   struct tc_window_rects *p = (struct tc_window_rects *)payload;
   pipe->set_window_rectangles(pipe, p->include, p->count, p->slot);
}

static void
tc_set_window_rectangles(struct pipe_context *_pipe, boolean include,
                         unsigned count,
                         const struct pipe_scissor_state *rects)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_window_rects *p =
      tc_add_slot_based_call(tc, TC_CALL_set_window_rectangles, tc_window_rects, count);

   p->include = include;
   p->count = count;
   memcpy(p->slot, rects, count * sizeof(rects[0]));
}

struct tc_sampler_views {
   ubyte shader, start, count;
   struct pipe_sampler_view *slot[0]; /* more will be allocated if needed */
};

static void
tc_call_set_sampler_views(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_sampler_views *p = (struct tc_sampler_views *)payload;
   unsigned count = p->count;

   pipe->set_sampler_views(pipe, p->shader, p->start, p->count, p->slot);
   for (unsigned i = 0; i < count; i++)
      pipe_sampler_view_reference(&p->slot[i], NULL);
}

static void
tc_set_sampler_views(struct pipe_context *_pipe,
                     enum pipe_shader_type shader,
                     unsigned start, unsigned count,
                     struct pipe_sampler_view **views)
{
   if (!count)
      return;

   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_sampler_views *p =
      tc_add_slot_based_call(tc, TC_CALL_set_sampler_views, tc_sampler_views, count);

   p->shader = shader;
   p->start = start;
   p->count = count;

   if (views) {
      for (unsigned i = 0; i < count; i++) {
         p->slot[i] = NULL;
         pipe_sampler_view_reference(&p->slot[i], views[i]);
      }
   } else {
      memset(p->slot, 0, count * sizeof(views[0]));
   }
}

struct tc_shader_images {
   ubyte shader, start, count;
   bool unbind;
   struct pipe_image_view slot[0]; /* more will be allocated if needed */
};

static void
tc_call_set_shader_images(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_shader_images *p = (struct tc_shader_images *)payload;
   unsigned count = p->count;

   if (p->unbind) {
      pipe->set_shader_images(pipe, p->shader, p->start, p->count, NULL);
      return;
   }

   pipe->set_shader_images(pipe, p->shader, p->start, p->count, p->slot);

   for (unsigned i = 0; i < count; i++)
      pipe_resource_reference(&p->slot[i].resource, NULL);
}

static void
tc_set_shader_images(struct pipe_context *_pipe,
                     enum pipe_shader_type shader,
                     unsigned start, unsigned count,
                     const struct pipe_image_view *images)
{
   if (!count)
      return;

   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_shader_images *p =
      tc_add_slot_based_call(tc, TC_CALL_set_shader_images, tc_shader_images,
                             images ? count : 0);

   p->shader = shader;
   p->start = start;
   p->count = count;
   p->unbind = images == NULL;

   if (images) {
      for (unsigned i = 0; i < count; i++) {
         tc_set_resource_reference(&p->slot[i].resource, images[i].resource);

         if (images[i].access & PIPE_IMAGE_ACCESS_WRITE &&
             images[i].resource &&
             images[i].resource->target == PIPE_BUFFER) {
            struct threaded_resource *tres =
               threaded_resource(images[i].resource);

            util_range_add(&tres->valid_buffer_range, images[i].u.buf.offset,
                           images[i].u.buf.offset + images[i].u.buf.size);
         }
      }
      memcpy(p->slot, images, count * sizeof(images[0]));
   }
}

struct tc_shader_buffers {
   ubyte shader, start, count;
   bool unbind;
   struct pipe_shader_buffer slot[0]; /* more will be allocated if needed */
};

static void
tc_call_set_shader_buffers(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_shader_buffers *p = (struct tc_shader_buffers *)payload;
   unsigned count = p->count;

   if (p->unbind) {
      pipe->set_shader_buffers(pipe, p->shader, p->start, p->count, NULL);
      return;
   }

   pipe->set_shader_buffers(pipe, p->shader, p->start, p->count, p->slot);

   for (unsigned i = 0; i < count; i++)
      pipe_resource_reference(&p->slot[i].buffer, NULL);
}

static void
tc_set_shader_buffers(struct pipe_context *_pipe,
                      enum pipe_shader_type shader,
                      unsigned start, unsigned count,
                      const struct pipe_shader_buffer *buffers)
{
   if (!count)
      return;

   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_shader_buffers *p =
      tc_add_slot_based_call(tc, TC_CALL_set_shader_buffers, tc_shader_buffers,
                             buffers ? count : 0);

   p->shader = shader;
   p->start = start;
   p->count = count;
   p->unbind = buffers == NULL;

   if (buffers) {
      for (unsigned i = 0; i < count; i++) {
         struct pipe_shader_buffer *dst = &p->slot[i];
         const struct pipe_shader_buffer *src = buffers + i;

         tc_set_resource_reference(&dst->buffer, src->buffer);
         dst->buffer_offset = src->buffer_offset;
         dst->buffer_size = src->buffer_size;

         if (src->buffer) {
            struct threaded_resource *tres = threaded_resource(src->buffer);

            util_range_add(&tres->valid_buffer_range, src->buffer_offset,
                           src->buffer_offset + src->buffer_size);
         }
      }
   }
}

struct tc_vertex_buffers {
   ubyte start, count;
   bool unbind;
   struct pipe_vertex_buffer slot[0]; /* more will be allocated if needed */
};

static void
tc_call_set_vertex_buffers(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_vertex_buffers *p = (struct tc_vertex_buffers *)payload;
   unsigned count = p->count;

   if (p->unbind) {
      pipe->set_vertex_buffers(pipe, p->start, count, NULL);
      return;
   }

   for (unsigned i = 0; i < count; i++)
      tc_assert(!p->slot[i].is_user_buffer);

   pipe->set_vertex_buffers(pipe, p->start, count, p->slot);
   for (unsigned i = 0; i < count; i++)
      pipe_resource_reference(&p->slot[i].buffer.resource, NULL);
}

static void
tc_set_vertex_buffers(struct pipe_context *_pipe,
                      unsigned start, unsigned count,
                      const struct pipe_vertex_buffer *buffers)
{
   struct threaded_context *tc = threaded_context(_pipe);

   if (!count)
      return;

   if (buffers) {
      struct tc_vertex_buffers *p =
         tc_add_slot_based_call(tc, TC_CALL_set_vertex_buffers, tc_vertex_buffers, count);
      p->start = start;
      p->count = count;
      p->unbind = false;

      for (unsigned i = 0; i < count; i++) {
         struct pipe_vertex_buffer *dst = &p->slot[i];
         const struct pipe_vertex_buffer *src = buffers + i;

         tc_assert(!src->is_user_buffer);
         dst->stride = src->stride;
         dst->is_user_buffer = false;
         tc_set_resource_reference(&dst->buffer.resource,
                                   src->buffer.resource);
         dst->buffer_offset = src->buffer_offset;
      }
   } else {
      struct tc_vertex_buffers *p =
         tc_add_slot_based_call(tc, TC_CALL_set_vertex_buffers, tc_vertex_buffers, 0);
      p->start = start;
      p->count = count;
      p->unbind = true;
   }
}

struct tc_stream_outputs {
   unsigned count;
   struct pipe_stream_output_target *targets[PIPE_MAX_SO_BUFFERS];
   unsigned offsets[PIPE_MAX_SO_BUFFERS];
};

static void
tc_call_set_stream_output_targets(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_stream_outputs *p = (struct tc_stream_outputs *)payload;
   unsigned count = p->count;

   pipe->set_stream_output_targets(pipe, count, p->targets, p->offsets);
   for (unsigned i = 0; i < count; i++)
      pipe_so_target_reference(&p->targets[i], NULL);
}

static void
tc_set_stream_output_targets(struct pipe_context *_pipe,
                             unsigned count,
                             struct pipe_stream_output_target **tgs,
                             const unsigned *offsets)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_stream_outputs *p =
      tc_add_struct_typed_call(tc, TC_CALL_set_stream_output_targets,
                               tc_stream_outputs);

   for (unsigned i = 0; i < count; i++) {
      p->targets[i] = NULL;
      pipe_so_target_reference(&p->targets[i], tgs[i]);
   }
   p->count = count;
   memcpy(p->offsets, offsets, count * sizeof(unsigned));
}

static void
tc_set_compute_resources(struct pipe_context *_pipe, unsigned start,
                         unsigned count, struct pipe_surface **resources)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   pipe->set_compute_resources(pipe, start, count, resources);
}

static void
tc_set_global_binding(struct pipe_context *_pipe, unsigned first,
                      unsigned count, struct pipe_resource **resources,
                      uint32_t **handles)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   pipe->set_global_binding(pipe, first, count, resources, handles);
}


/********************************************************************
 * views
 */

static struct pipe_surface *
tc_create_surface(struct pipe_context *_pipe,
                  struct pipe_resource *resource,
                  const struct pipe_surface *surf_tmpl)
{
   struct pipe_context *pipe = threaded_context(_pipe)->pipe;
   struct pipe_surface *view =
         pipe->create_surface(pipe, resource, surf_tmpl);

   if (view)
      view->context = _pipe;
   return view;
}

static void
tc_surface_destroy(struct pipe_context *_pipe,
                   struct pipe_surface *surf)
{
   struct pipe_context *pipe = threaded_context(_pipe)->pipe;

   pipe->surface_destroy(pipe, surf);
}

static struct pipe_sampler_view *
tc_create_sampler_view(struct pipe_context *_pipe,
                       struct pipe_resource *resource,
                       const struct pipe_sampler_view *templ)
{
   struct pipe_context *pipe = threaded_context(_pipe)->pipe;
   struct pipe_sampler_view *view =
         pipe->create_sampler_view(pipe, resource, templ);

   if (view)
      view->context = _pipe;
   return view;
}

static void
tc_sampler_view_destroy(struct pipe_context *_pipe,
                        struct pipe_sampler_view *view)
{
   struct pipe_context *pipe = threaded_context(_pipe)->pipe;

   pipe->sampler_view_destroy(pipe, view);
}

static struct pipe_stream_output_target *
tc_create_stream_output_target(struct pipe_context *_pipe,
                               struct pipe_resource *res,
                               unsigned buffer_offset,
                               unsigned buffer_size)
{
   struct pipe_context *pipe = threaded_context(_pipe)->pipe;
   struct threaded_resource *tres = threaded_resource(res);
   struct pipe_stream_output_target *view;

   tc_sync(threaded_context(_pipe));
   util_range_add(&tres->valid_buffer_range, buffer_offset,
                  buffer_offset + buffer_size);

   view = pipe->create_stream_output_target(pipe, res, buffer_offset,
                                            buffer_size);
   if (view)
      view->context = _pipe;
   return view;
}

static void
tc_stream_output_target_destroy(struct pipe_context *_pipe,
                                struct pipe_stream_output_target *target)
{
   struct pipe_context *pipe = threaded_context(_pipe)->pipe;

   pipe->stream_output_target_destroy(pipe, target);
}


/********************************************************************
 * bindless
 */

static uint64_t
tc_create_texture_handle(struct pipe_context *_pipe,
                         struct pipe_sampler_view *view,
                         const struct pipe_sampler_state *state)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   return pipe->create_texture_handle(pipe, view, state);
}

static void
tc_call_delete_texture_handle(struct pipe_context *pipe,
                              union tc_payload *payload)
{
   pipe->delete_texture_handle(pipe, payload->handle);
}

static void
tc_delete_texture_handle(struct pipe_context *_pipe, uint64_t handle)
{
   struct threaded_context *tc = threaded_context(_pipe);
   union tc_payload *payload =
      tc_add_small_call(tc, TC_CALL_delete_texture_handle);

   payload->handle = handle;
}

struct tc_make_texture_handle_resident
{
   uint64_t handle;
   bool resident;
};

static void
tc_call_make_texture_handle_resident(struct pipe_context *pipe,
                                     union tc_payload *payload)
{
   struct tc_make_texture_handle_resident *p =
      (struct tc_make_texture_handle_resident *)payload;

   pipe->make_texture_handle_resident(pipe, p->handle, p->resident);
}

static void
tc_make_texture_handle_resident(struct pipe_context *_pipe, uint64_t handle,
                                bool resident)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_make_texture_handle_resident *p =
      tc_add_struct_typed_call(tc, TC_CALL_make_texture_handle_resident,
                               tc_make_texture_handle_resident);

   p->handle = handle;
   p->resident = resident;
}

static uint64_t
tc_create_image_handle(struct pipe_context *_pipe,
                       const struct pipe_image_view *image)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   return pipe->create_image_handle(pipe, image);
}

static void
tc_call_delete_image_handle(struct pipe_context *pipe,
                            union tc_payload *payload)
{
   pipe->delete_image_handle(pipe, payload->handle);
}

static void
tc_delete_image_handle(struct pipe_context *_pipe, uint64_t handle)
{
   struct threaded_context *tc = threaded_context(_pipe);
   union tc_payload *payload =
      tc_add_small_call(tc, TC_CALL_delete_image_handle);

   payload->handle = handle;
}

struct tc_make_image_handle_resident
{
   uint64_t handle;
   unsigned access;
   bool resident;
};

static void
tc_call_make_image_handle_resident(struct pipe_context *pipe,
                                     union tc_payload *payload)
{
   struct tc_make_image_handle_resident *p =
      (struct tc_make_image_handle_resident *)payload;

   pipe->make_image_handle_resident(pipe, p->handle, p->access, p->resident);
}

static void
tc_make_image_handle_resident(struct pipe_context *_pipe, uint64_t handle,
                              unsigned access, bool resident)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_make_image_handle_resident *p =
      tc_add_struct_typed_call(tc, TC_CALL_make_image_handle_resident,
                               tc_make_image_handle_resident);

   p->handle = handle;
   p->access = access;
   p->resident = resident;
}


/********************************************************************
 * transfer
 */

struct tc_replace_buffer_storage {
   struct pipe_resource *dst;
   struct pipe_resource *src;
   tc_replace_buffer_storage_func func;
};

static void
tc_call_replace_buffer_storage(struct pipe_context *pipe,
                               union tc_payload *payload)
{
   struct tc_replace_buffer_storage *p =
      (struct tc_replace_buffer_storage *)payload;

   p->func(pipe, p->dst, p->src);
   pipe_resource_reference(&p->dst, NULL);
   pipe_resource_reference(&p->src, NULL);
}

static bool
tc_invalidate_buffer(struct threaded_context *tc,
                     struct threaded_resource *tbuf)
{
   /* We can't check if the buffer is idle, so we invalidate it
    * unconditionally. */
   struct pipe_screen *screen = tc->base.screen;
   struct pipe_resource *new_buf;

   /* Shared, pinned, and sparse buffers can't be reallocated. */
   if (tbuf->is_shared ||
       tbuf->is_user_ptr ||
       tbuf->b.flags & PIPE_RESOURCE_FLAG_SPARSE)
      return false;

   /* Allocate a new one. */
   new_buf = screen->resource_create(screen, &tbuf->b);
   if (!new_buf)
      return false;

   /* Replace the "latest" pointer. */
   if (tbuf->latest != &tbuf->b)
      pipe_resource_reference(&tbuf->latest, NULL);

   tbuf->latest = new_buf;
   util_range_set_empty(&tbuf->valid_buffer_range);

   /* The valid range should point to the original buffer. */
   threaded_resource(new_buf)->base_valid_buffer_range =
      &tbuf->valid_buffer_range;

   /* Enqueue storage replacement of the original buffer. */
   struct tc_replace_buffer_storage *p =
      tc_add_struct_typed_call(tc, TC_CALL_replace_buffer_storage,
                               tc_replace_buffer_storage);

   p->func = tc->replace_buffer_storage;
   tc_set_resource_reference(&p->dst, &tbuf->b);
   tc_set_resource_reference(&p->src, new_buf);
   return true;
}

static unsigned
tc_improve_map_buffer_flags(struct threaded_context *tc,
                            struct threaded_resource *tres, unsigned usage,
                            unsigned offset, unsigned size)
{
   /* Never invalidate inside the driver and never infer "unsynchronized". */
   unsigned tc_flags = TC_TRANSFER_MAP_NO_INVALIDATE |
                       TC_TRANSFER_MAP_NO_INFER_UNSYNCHRONIZED;

   /* Prevent a reentry. */
   if (usage & tc_flags)
      return usage;

   /* Use the staging upload if it's preferred. */
   if (usage & (PIPE_TRANSFER_DISCARD_RANGE |
                PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE) &&
       !(usage & PIPE_TRANSFER_PERSISTENT) &&
       /* Try not to decrement the counter if it's not positive. Still racy,
        * but it makes it harder to wrap the counter from INT_MIN to INT_MAX. */
       tres->max_forced_staging_uploads > 0 &&
       p_atomic_dec_return(&tres->max_forced_staging_uploads) >= 0) {
      usage &= ~(PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE |
                 PIPE_TRANSFER_UNSYNCHRONIZED);

      return usage | tc_flags | PIPE_TRANSFER_DISCARD_RANGE;
   }

   /* Sparse buffers can't be mapped directly and can't be reallocated
    * (fully invalidated). That may just be a radeonsi limitation, but
    * the threaded context must obey it with radeonsi.
    */
   if (tres->b.flags & PIPE_RESOURCE_FLAG_SPARSE) {
      /* We can use DISCARD_RANGE instead of full discard. This is the only
       * fast path for sparse buffers that doesn't need thread synchronization.
       */
      if (usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE)
         usage |= PIPE_TRANSFER_DISCARD_RANGE;

      /* Allow DISCARD_WHOLE_RESOURCE and infering UNSYNCHRONIZED in drivers.
       * The threaded context doesn't do unsychronized mappings and invalida-
       * tions of sparse buffers, therefore a correct driver behavior won't
       * result in an incorrect behavior with the threaded context.
       */
      return usage;
   }

   usage |= tc_flags;

   /* Handle CPU reads trivially. */
   if (usage & PIPE_TRANSFER_READ) {
      /* Drivers aren't allowed to do buffer invalidations. */
      return usage & ~PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE;
   }

   /* See if the buffer range being mapped has never been initialized,
    * in which case it can be mapped unsynchronized. */
   if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED) &&
       !tres->is_shared &&
       !util_ranges_intersect(&tres->valid_buffer_range, offset, offset + size))
      usage |= PIPE_TRANSFER_UNSYNCHRONIZED;

   if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
      /* If discarding the entire range, discard the whole resource instead. */
      if (usage & PIPE_TRANSFER_DISCARD_RANGE &&
          offset == 0 && size == tres->b.width0)
         usage |= PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE;

      /* Discard the whole resource if needed. */
      if (usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE) {
         if (tc_invalidate_buffer(tc, tres))
            usage |= PIPE_TRANSFER_UNSYNCHRONIZED;
         else
            usage |= PIPE_TRANSFER_DISCARD_RANGE; /* fallback */
      }
   }

   /* We won't need this flag anymore. */
   /* TODO: We might not need TC_TRANSFER_MAP_NO_INVALIDATE with this. */
   usage &= ~PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE;

   /* GL_AMD_pinned_memory and persistent mappings can't use staging
    * buffers. */
   if (usage & (PIPE_TRANSFER_UNSYNCHRONIZED |
                PIPE_TRANSFER_PERSISTENT) ||
       tres->is_user_ptr)
      usage &= ~PIPE_TRANSFER_DISCARD_RANGE;

   /* Unsychronized buffer mappings don't have to synchronize the thread. */
   if (usage & PIPE_TRANSFER_UNSYNCHRONIZED) {
      usage &= ~PIPE_TRANSFER_DISCARD_RANGE;
      usage |= TC_TRANSFER_MAP_THREADED_UNSYNC; /* notify the driver */
   }

   return usage;
}

static void *
tc_transfer_map(struct pipe_context *_pipe,
                struct pipe_resource *resource, unsigned level,
                unsigned usage, const struct pipe_box *box,
                struct pipe_transfer **transfer)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct threaded_resource *tres = threaded_resource(resource);
   struct pipe_context *pipe = tc->pipe;

   if (resource->target == PIPE_BUFFER) {
      usage = tc_improve_map_buffer_flags(tc, tres, usage, box->x, box->width);

      /* Do a staging transfer within the threaded context. The driver should
       * only get resource_copy_region.
       */
      if (usage & PIPE_TRANSFER_DISCARD_RANGE) {
         struct threaded_transfer *ttrans = slab_alloc(&tc->pool_transfers);
         uint8_t *map;

         ttrans->staging = NULL;

         u_upload_alloc(tc->base.stream_uploader, 0,
                        box->width + (box->x % tc->map_buffer_alignment),
                        64, &ttrans->offset, &ttrans->staging, (void**)&map);
         if (!map) {
            slab_free(&tc->pool_transfers, ttrans);
            return NULL;
         }

         tc_set_resource_reference(&ttrans->b.resource, resource);
         ttrans->b.level = 0;
         ttrans->b.usage = usage;
         ttrans->b.box = *box;
         ttrans->b.stride = 0;
         ttrans->b.layer_stride = 0;
         *transfer = &ttrans->b;
         return map + (box->x % tc->map_buffer_alignment);
      }
   }

   /* Unsychronized buffer mappings don't have to synchronize the thread. */
   if (!(usage & TC_TRANSFER_MAP_THREADED_UNSYNC))
      tc_sync_msg(tc, resource->target != PIPE_BUFFER ? "  texture" :
                      usage & PIPE_TRANSFER_DISCARD_RANGE ? "  discard_range" :
                      usage & PIPE_TRANSFER_READ ? "  read" : "  ??");

   return pipe->transfer_map(pipe, tres->latest ? tres->latest : resource,
                             level, usage, box, transfer);
}

struct tc_transfer_flush_region {
   struct pipe_transfer *transfer;
   struct pipe_box box;
};

static void
tc_call_transfer_flush_region(struct pipe_context *pipe,
                              union tc_payload *payload)
{
   struct tc_transfer_flush_region *p =
      (struct tc_transfer_flush_region *)payload;

   pipe->transfer_flush_region(pipe, p->transfer, &p->box);
}

struct tc_resource_copy_region {
   struct pipe_resource *dst;
   unsigned dst_level;
   unsigned dstx, dsty, dstz;
   struct pipe_resource *src;
   unsigned src_level;
   struct pipe_box src_box;
};

static void
tc_resource_copy_region(struct pipe_context *_pipe,
                        struct pipe_resource *dst, unsigned dst_level,
                        unsigned dstx, unsigned dsty, unsigned dstz,
                        struct pipe_resource *src, unsigned src_level,
                        const struct pipe_box *src_box);

static void
tc_buffer_do_flush_region(struct threaded_context *tc,
                          struct threaded_transfer *ttrans,
                          const struct pipe_box *box)
{
   struct threaded_resource *tres = threaded_resource(ttrans->b.resource);

   if (ttrans->staging) {
      struct pipe_box src_box;

      u_box_1d(ttrans->offset + box->x % tc->map_buffer_alignment,
               box->width, &src_box);

      /* Copy the staging buffer into the original one. */
      tc_resource_copy_region(&tc->base, ttrans->b.resource, 0, box->x, 0, 0,
                              ttrans->staging, 0, &src_box);
   }

   util_range_add(tres->base_valid_buffer_range, box->x, box->x + box->width);
}

static void
tc_transfer_flush_region(struct pipe_context *_pipe,
                         struct pipe_transfer *transfer,
                         const struct pipe_box *rel_box)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct threaded_transfer *ttrans = threaded_transfer(transfer);
   struct threaded_resource *tres = threaded_resource(transfer->resource);
   unsigned required_usage = PIPE_TRANSFER_WRITE |
                             PIPE_TRANSFER_FLUSH_EXPLICIT;

   if (tres->b.target == PIPE_BUFFER) {
      if ((transfer->usage & required_usage) == required_usage) {
         struct pipe_box box;

         u_box_1d(transfer->box.x + rel_box->x, rel_box->width, &box);
         tc_buffer_do_flush_region(tc, ttrans, &box);
      }

      /* Staging transfers don't send the call to the driver. */
      if (ttrans->staging)
         return;
   }

   struct tc_transfer_flush_region *p =
      tc_add_struct_typed_call(tc, TC_CALL_transfer_flush_region,
                               tc_transfer_flush_region);
   p->transfer = transfer;
   p->box = *rel_box;
}

static void
tc_call_transfer_unmap(struct pipe_context *pipe, union tc_payload *payload)
{
   pipe->transfer_unmap(pipe, payload->transfer);
}

static void
tc_transfer_unmap(struct pipe_context *_pipe, struct pipe_transfer *transfer)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct threaded_transfer *ttrans = threaded_transfer(transfer);
   struct threaded_resource *tres = threaded_resource(transfer->resource);

   if (tres->b.target == PIPE_BUFFER) {
      if (transfer->usage & PIPE_TRANSFER_WRITE &&
          !(transfer->usage & PIPE_TRANSFER_FLUSH_EXPLICIT))
         tc_buffer_do_flush_region(tc, ttrans, &transfer->box);

      /* Staging transfers don't send the call to the driver. */
      if (ttrans->staging) {
         pipe_resource_reference(&ttrans->staging, NULL);
         pipe_resource_reference(&ttrans->b.resource, NULL);
         slab_free(&tc->pool_transfers, ttrans);
         return;
      }
   }

   tc_add_small_call(tc, TC_CALL_transfer_unmap)->transfer = transfer;
}

struct tc_buffer_subdata {
   struct pipe_resource *resource;
   unsigned usage, offset, size;
   char slot[0]; /* more will be allocated if needed */
};

static void
tc_call_buffer_subdata(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_buffer_subdata *p = (struct tc_buffer_subdata *)payload;

   pipe->buffer_subdata(pipe, p->resource, p->usage, p->offset, p->size,
                        p->slot);
   pipe_resource_reference(&p->resource, NULL);
}

static void
tc_buffer_subdata(struct pipe_context *_pipe,
                  struct pipe_resource *resource,
                  unsigned usage, unsigned offset,
                  unsigned size, const void *data)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct threaded_resource *tres = threaded_resource(resource);

   if (!size)
      return;

   usage |= PIPE_TRANSFER_WRITE |
            PIPE_TRANSFER_DISCARD_RANGE;

   usage = tc_improve_map_buffer_flags(tc, tres, usage, offset, size);

   /* Unsychronized and big transfers should use transfer_map. Also handle
    * full invalidations, because drivers aren't allowed to do them.
    */
   if (usage & (PIPE_TRANSFER_UNSYNCHRONIZED |
                PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE) ||
       size > TC_MAX_SUBDATA_BYTES) {
      struct pipe_transfer *transfer;
      struct pipe_box box;
      uint8_t *map = NULL;

      u_box_1d(offset, size, &box);

      map = tc_transfer_map(_pipe, resource, 0, usage, &box, &transfer);
      if (map) {
         memcpy(map, data, size);
         tc_transfer_unmap(_pipe, transfer);
      }
      return;
   }

   util_range_add(&tres->valid_buffer_range, offset, offset + size);

   /* The upload is small. Enqueue it. */
   struct tc_buffer_subdata *p =
      tc_add_slot_based_call(tc, TC_CALL_buffer_subdata, tc_buffer_subdata, size);

   tc_set_resource_reference(&p->resource, resource);
   p->usage = usage;
   p->offset = offset;
   p->size = size;
   memcpy(p->slot, data, size);
}

struct tc_texture_subdata {
   struct pipe_resource *resource;
   unsigned level, usage, stride, layer_stride;
   struct pipe_box box;
   char slot[0]; /* more will be allocated if needed */
};

static void
tc_call_texture_subdata(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_texture_subdata *p = (struct tc_texture_subdata *)payload;

   pipe->texture_subdata(pipe, p->resource, p->level, p->usage, &p->box,
                         p->slot, p->stride, p->layer_stride);
   pipe_resource_reference(&p->resource, NULL);
}

static void
tc_texture_subdata(struct pipe_context *_pipe,
                   struct pipe_resource *resource,
                   unsigned level, unsigned usage,
                   const struct pipe_box *box,
                   const void *data, unsigned stride,
                   unsigned layer_stride)
{
   struct threaded_context *tc = threaded_context(_pipe);
   unsigned size;

   assert(box->height >= 1);
   assert(box->depth >= 1);

   size = (box->depth - 1) * layer_stride +
          (box->height - 1) * stride +
          box->width * util_format_get_blocksize(resource->format);
   if (!size)
      return;

   /* Small uploads can be enqueued, big uploads must sync. */
   if (size <= TC_MAX_SUBDATA_BYTES) {
      struct tc_texture_subdata *p =
         tc_add_slot_based_call(tc, TC_CALL_texture_subdata, tc_texture_subdata, size);

      tc_set_resource_reference(&p->resource, resource);
      p->level = level;
      p->usage = usage;
      p->box = *box;
      p->stride = stride;
      p->layer_stride = layer_stride;
      memcpy(p->slot, data, size);
   } else {
      struct pipe_context *pipe = tc->pipe;

      tc_sync(tc);
      pipe->texture_subdata(pipe, resource, level, usage, box, data,
                            stride, layer_stride);
   }
}


/********************************************************************
 * miscellaneous
 */

#define TC_FUNC_SYNC_RET0(ret_type, func) \
   static ret_type \
   tc_##func(struct pipe_context *_pipe) \
   { \
      struct threaded_context *tc = threaded_context(_pipe); \
      struct pipe_context *pipe = tc->pipe; \
      tc_sync(tc); \
      return pipe->func(pipe); \
   }

TC_FUNC_SYNC_RET0(enum pipe_reset_status, get_device_reset_status)
TC_FUNC_SYNC_RET0(uint64_t, get_timestamp)

static void
tc_get_sample_position(struct pipe_context *_pipe,
                       unsigned sample_count, unsigned sample_index,
                       float *out_value)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   pipe->get_sample_position(pipe, sample_count, sample_index,
                             out_value);
}

static void
tc_set_device_reset_callback(struct pipe_context *_pipe,
                             const struct pipe_device_reset_callback *cb)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   pipe->set_device_reset_callback(pipe, cb);
}

struct tc_string_marker {
   int len;
   char slot[0]; /* more will be allocated if needed */
};

static void
tc_call_emit_string_marker(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_string_marker *p = (struct tc_string_marker *)payload;
   pipe->emit_string_marker(pipe, p->slot, p->len);
}

static void
tc_emit_string_marker(struct pipe_context *_pipe,
                      const char *string, int len)
{
   struct threaded_context *tc = threaded_context(_pipe);

   if (len <= TC_MAX_STRING_MARKER_BYTES) {
      struct tc_string_marker *p =
         tc_add_slot_based_call(tc, TC_CALL_emit_string_marker, tc_string_marker, len);

      memcpy(p->slot, string, len);
      p->len = len;
   } else {
      struct pipe_context *pipe = tc->pipe;

      tc_sync(tc);
      pipe->emit_string_marker(pipe, string, len);
   }
}

static void
tc_dump_debug_state(struct pipe_context *_pipe, FILE *stream,
                    unsigned flags)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   pipe->dump_debug_state(pipe, stream, flags);
}

static void
tc_set_debug_callback(struct pipe_context *_pipe,
                      const struct pipe_debug_callback *cb)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   /* Drop all synchronous debug callbacks. Drivers are expected to be OK
    * with this. shader-db will use an environment variable to disable
    * the threaded context.
    */
   if (cb && cb->debug_message && !cb->async)
      return;

   tc_sync(tc);
   pipe->set_debug_callback(pipe, cb);
}

static void
tc_set_log_context(struct pipe_context *_pipe, struct u_log_context *log)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   pipe->set_log_context(pipe, log);
}

static void
tc_create_fence_fd(struct pipe_context *_pipe,
                   struct pipe_fence_handle **fence, int fd,
                   enum pipe_fd_type type)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   pipe->create_fence_fd(pipe, fence, fd, type);
}

static void
tc_call_fence_server_sync(struct pipe_context *pipe, union tc_payload *payload)
{
   pipe->fence_server_sync(pipe, payload->fence);
   pipe->screen->fence_reference(pipe->screen, &payload->fence, NULL);
}

static void
tc_fence_server_sync(struct pipe_context *_pipe,
                     struct pipe_fence_handle *fence)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_screen *screen = tc->pipe->screen;
   union tc_payload *payload = tc_add_small_call(tc, TC_CALL_fence_server_sync);

   payload->fence = NULL;
   screen->fence_reference(screen, &payload->fence, fence);
}

static void
tc_call_fence_server_signal(struct pipe_context *pipe, union tc_payload *payload)
{
   pipe->fence_server_signal(pipe, payload->fence);
   pipe->screen->fence_reference(pipe->screen, &payload->fence, NULL);
}

static void
tc_fence_server_signal(struct pipe_context *_pipe,
                           struct pipe_fence_handle *fence)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_screen *screen = tc->pipe->screen;
   union tc_payload *payload = tc_add_small_call(tc, TC_CALL_fence_server_signal);

   payload->fence = NULL;
   screen->fence_reference(screen, &payload->fence, fence);
}

static struct pipe_video_codec *
tc_create_video_codec(UNUSED struct pipe_context *_pipe,
                      UNUSED const struct pipe_video_codec *templ)
{
   unreachable("Threaded context should not be enabled for video APIs");
   return NULL;
}

static struct pipe_video_buffer *
tc_create_video_buffer(UNUSED struct pipe_context *_pipe,
                       UNUSED const struct pipe_video_buffer *templ)
{
   unreachable("Threaded context should not be enabled for video APIs");
   return NULL;
}


/********************************************************************
 * draw, launch, clear, blit, copy, flush
 */

struct tc_flush_payload {
   struct threaded_context *tc;
   struct pipe_fence_handle *fence;
   unsigned flags;
};

static void
tc_flush_queries(struct threaded_context *tc)
{
   struct threaded_query *tq, *tmp;
   LIST_FOR_EACH_ENTRY_SAFE(tq, tmp, &tc->unflushed_queries, head_unflushed) {
      LIST_DEL(&tq->head_unflushed);

      /* Memory release semantics: due to a possible race with
       * tc_get_query_result, we must ensure that the linked list changes
       * are visible before setting tq->flushed.
       */
      p_atomic_set(&tq->flushed, true);
   }
}

static void
tc_call_flush(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_flush_payload *p = (struct tc_flush_payload *)payload;
   struct pipe_screen *screen = pipe->screen;

   pipe->flush(pipe, p->fence ? &p->fence : NULL, p->flags);
   screen->fence_reference(screen, &p->fence, NULL);

   if (!(p->flags & PIPE_FLUSH_DEFERRED))
      tc_flush_queries(p->tc);
}

static void
tc_flush(struct pipe_context *_pipe, struct pipe_fence_handle **fence,
         unsigned flags)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;
   struct pipe_screen *screen = pipe->screen;
   bool async = flags & PIPE_FLUSH_DEFERRED;

   if (flags & PIPE_FLUSH_ASYNC) {
      struct tc_batch *last = &tc->batch_slots[tc->last];

      /* Prefer to do the flush in the driver thread, but avoid the inter-thread
       * communication overhead if the driver thread is currently idle and the
       * caller is going to wait for the fence immediately anyway.
       */
      if (!(util_queue_fence_is_signalled(&last->fence) &&
            (flags & PIPE_FLUSH_HINT_FINISH)))
         async = true;
   }

   if (async && tc->create_fence) {
      if (fence) {
         struct tc_batch *next = &tc->batch_slots[tc->next];

         if (!next->token) {
            next->token = malloc(sizeof(*next->token));
            if (!next->token)
               goto out_of_memory;

            pipe_reference_init(&next->token->ref, 1);
            next->token->tc = tc;
         }

         screen->fence_reference(screen, fence, tc->create_fence(pipe, next->token));
         if (!*fence)
            goto out_of_memory;
      }

      struct tc_flush_payload *p =
         tc_add_struct_typed_call(tc, TC_CALL_flush, tc_flush_payload);
      p->tc = tc;
      p->fence = fence ? *fence : NULL;
      p->flags = flags | TC_FLUSH_ASYNC;

      if (!(flags & PIPE_FLUSH_DEFERRED))
         tc_batch_flush(tc);
      return;
   }

out_of_memory:
   tc_sync_msg(tc, flags & PIPE_FLUSH_END_OF_FRAME ? "end of frame" :
                   flags & PIPE_FLUSH_DEFERRED ? "deferred fence" : "normal");

   if (!(flags & PIPE_FLUSH_DEFERRED))
      tc_flush_queries(tc);
   pipe->flush(pipe, fence, flags);
}

/* This is actually variable-sized, because indirect isn't allocated if it's
 * not needed. */
struct tc_full_draw_info {
   struct pipe_draw_info draw;
   struct pipe_draw_indirect_info indirect;
};

static void
tc_call_draw_vbo(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_full_draw_info *info = (struct tc_full_draw_info*)payload;

   pipe->draw_vbo(pipe, &info->draw);
   pipe_so_target_reference(&info->draw.count_from_stream_output, NULL);
   if (info->draw.index_size)
      pipe_resource_reference(&info->draw.index.resource, NULL);
   if (info->draw.indirect) {
      pipe_resource_reference(&info->indirect.buffer, NULL);
      pipe_resource_reference(&info->indirect.indirect_draw_count, NULL);
   }
}

static struct tc_full_draw_info *
tc_add_draw_vbo(struct pipe_context *_pipe, bool indirect)
{
   return (struct tc_full_draw_info*)
          tc_add_sized_call(threaded_context(_pipe), TC_CALL_draw_vbo,
                            indirect ? sizeof(struct tc_full_draw_info) :
                                       sizeof(struct pipe_draw_info));
}

static void
tc_draw_vbo(struct pipe_context *_pipe, const struct pipe_draw_info *info)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_draw_indirect_info *indirect = info->indirect;
   unsigned index_size = info->index_size;
   bool has_user_indices = info->has_user_indices;

   if (index_size && has_user_indices) {
      unsigned size = info->count * index_size;
      struct pipe_resource *buffer = NULL;
      unsigned offset;

      tc_assert(!indirect);

      /* This must be done before adding draw_vbo, because it could generate
       * e.g. transfer_unmap and flush partially-uninitialized draw_vbo
       * to the driver if it was done afterwards.
       */
      u_upload_data(tc->base.stream_uploader, 0, size, 4, info->index.user,
                    &offset, &buffer);
      if (unlikely(!buffer))
         return;

      struct tc_full_draw_info *p = tc_add_draw_vbo(_pipe, false);
      p->draw.count_from_stream_output = NULL;
      pipe_so_target_reference(&p->draw.count_from_stream_output,
                               info->count_from_stream_output);
      memcpy(&p->draw, info, sizeof(*info));
      p->draw.has_user_indices = false;
      p->draw.index.resource = buffer;
      p->draw.start = offset / index_size;
   } else {
      /* Non-indexed call or indexed with a real index buffer. */
      struct tc_full_draw_info *p = tc_add_draw_vbo(_pipe, indirect != NULL);
      p->draw.count_from_stream_output = NULL;
      pipe_so_target_reference(&p->draw.count_from_stream_output,
                               info->count_from_stream_output);
      if (index_size) {
         tc_set_resource_reference(&p->draw.index.resource,
                                   info->index.resource);
      }
      memcpy(&p->draw, info, sizeof(*info));

      if (indirect) {
         tc_set_resource_reference(&p->draw.indirect->buffer, indirect->buffer);
         tc_set_resource_reference(&p->indirect.indirect_draw_count,
                                   indirect->indirect_draw_count);
         memcpy(&p->indirect, indirect, sizeof(*indirect));
         p->draw.indirect = &p->indirect;
      }
   }
}

static void
tc_call_launch_grid(struct pipe_context *pipe, union tc_payload *payload)
{
   struct pipe_grid_info *p = (struct pipe_grid_info *)payload;

   pipe->launch_grid(pipe, p);
   pipe_resource_reference(&p->indirect, NULL);
}

static void
tc_launch_grid(struct pipe_context *_pipe,
               const struct pipe_grid_info *info)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_grid_info *p = tc_add_struct_typed_call(tc, TC_CALL_launch_grid,
                                                       pipe_grid_info);
   assert(info->input == NULL);

   tc_set_resource_reference(&p->indirect, info->indirect);
   memcpy(p, info, sizeof(*info));
}

static void
tc_call_resource_copy_region(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_resource_copy_region *p = (struct tc_resource_copy_region *)payload;

   pipe->resource_copy_region(pipe, p->dst, p->dst_level, p->dstx, p->dsty,
                              p->dstz, p->src, p->src_level, &p->src_box);
   pipe_resource_reference(&p->dst, NULL);
   pipe_resource_reference(&p->src, NULL);
}

static void
tc_resource_copy_region(struct pipe_context *_pipe,
                        struct pipe_resource *dst, unsigned dst_level,
                        unsigned dstx, unsigned dsty, unsigned dstz,
                        struct pipe_resource *src, unsigned src_level,
                        const struct pipe_box *src_box)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct threaded_resource *tdst = threaded_resource(dst);
   struct tc_resource_copy_region *p =
      tc_add_struct_typed_call(tc, TC_CALL_resource_copy_region,
                               tc_resource_copy_region);

   tc_set_resource_reference(&p->dst, dst);
   p->dst_level = dst_level;
   p->dstx = dstx;
   p->dsty = dsty;
   p->dstz = dstz;
   tc_set_resource_reference(&p->src, src);
   p->src_level = src_level;
   p->src_box = *src_box;

   if (dst->target == PIPE_BUFFER)
      util_range_add(&tdst->valid_buffer_range, dstx, dstx + src_box->width);
}

static void
tc_call_blit(struct pipe_context *pipe, union tc_payload *payload)
{
   struct pipe_blit_info *blit = (struct pipe_blit_info*)payload;

   pipe->blit(pipe, blit);
   pipe_resource_reference(&blit->dst.resource, NULL);
   pipe_resource_reference(&blit->src.resource, NULL);
}

static void
tc_blit(struct pipe_context *_pipe, const struct pipe_blit_info *info)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_blit_info *blit =
      tc_add_struct_typed_call(tc, TC_CALL_blit, pipe_blit_info);

   tc_set_resource_reference(&blit->dst.resource, info->dst.resource);
   tc_set_resource_reference(&blit->src.resource, info->src.resource);
   memcpy(blit, info, sizeof(*info));
}

struct tc_generate_mipmap {
   struct pipe_resource *res;
   enum pipe_format format;
   unsigned base_level;
   unsigned last_level;
   unsigned first_layer;
   unsigned last_layer;
};

static void
tc_call_generate_mipmap(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_generate_mipmap *p = (struct tc_generate_mipmap *)payload;
   MAYBE_UNUSED bool result = pipe->generate_mipmap(pipe, p->res, p->format,
                                                    p->base_level,
                                                    p->last_level,
                                                    p->first_layer,
                                                    p->last_layer);
   assert(result);
   pipe_resource_reference(&p->res, NULL);
}

static boolean
tc_generate_mipmap(struct pipe_context *_pipe,
                   struct pipe_resource *res,
                   enum pipe_format format,
                   unsigned base_level,
                   unsigned last_level,
                   unsigned first_layer,
                   unsigned last_layer)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;
   struct pipe_screen *screen = pipe->screen;
   unsigned bind = PIPE_BIND_SAMPLER_VIEW;

   if (util_format_is_depth_or_stencil(format))
      bind = PIPE_BIND_DEPTH_STENCIL;
   else
      bind = PIPE_BIND_RENDER_TARGET;

   if (!screen->is_format_supported(screen, format, res->target,
                                    res->nr_samples, bind))
      return false;

   struct tc_generate_mipmap *p =
      tc_add_struct_typed_call(tc, TC_CALL_generate_mipmap, tc_generate_mipmap);

   tc_set_resource_reference(&p->res, res);
   p->format = format;
   p->base_level = base_level;
   p->last_level = last_level;
   p->first_layer = first_layer;
   p->last_layer = last_layer;
   return true;
}

static void
tc_call_flush_resource(struct pipe_context *pipe, union tc_payload *payload)
{
   pipe->flush_resource(pipe, payload->resource);
   pipe_resource_reference(&payload->resource, NULL);
}

static void
tc_flush_resource(struct pipe_context *_pipe,
                  struct pipe_resource *resource)
{
   struct threaded_context *tc = threaded_context(_pipe);
   union tc_payload *payload = tc_add_small_call(tc, TC_CALL_flush_resource);

   tc_set_resource_reference(&payload->resource, resource);
}

static void
tc_call_invalidate_resource(struct pipe_context *pipe, union tc_payload *payload)
{
   pipe->invalidate_resource(pipe, payload->resource);
   pipe_resource_reference(&payload->resource, NULL);
}

static void
tc_invalidate_resource(struct pipe_context *_pipe,
                       struct pipe_resource *resource)
{
   struct threaded_context *tc = threaded_context(_pipe);

   if (resource->target == PIPE_BUFFER) {
      tc_invalidate_buffer(tc, threaded_resource(resource));
      return;
   }

   union tc_payload *payload = tc_add_small_call(tc, TC_CALL_invalidate_resource);
   tc_set_resource_reference(&payload->resource, resource);
}

struct tc_clear {
   unsigned buffers;
   union pipe_color_union color;
   double depth;
   unsigned stencil;
};

static void
tc_call_clear(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_clear *p = (struct tc_clear *)payload;
   pipe->clear(pipe, p->buffers, &p->color, p->depth, p->stencil);
}

static void
tc_clear(struct pipe_context *_pipe, unsigned buffers,
         const union pipe_color_union *color, double depth,
         unsigned stencil)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_clear *p = tc_add_struct_typed_call(tc, TC_CALL_clear, tc_clear);

   p->buffers = buffers;
   p->color = *color;
   p->depth = depth;
   p->stencil = stencil;
}

static void
tc_clear_render_target(struct pipe_context *_pipe,
                       struct pipe_surface *dst,
                       const union pipe_color_union *color,
                       unsigned dstx, unsigned dsty,
                       unsigned width, unsigned height,
                       bool render_condition_enabled)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   pipe->clear_render_target(pipe, dst, color, dstx, dsty, width, height,
                             render_condition_enabled);
}

static void
tc_clear_depth_stencil(struct pipe_context *_pipe,
                       struct pipe_surface *dst, unsigned clear_flags,
                       double depth, unsigned stencil, unsigned dstx,
                       unsigned dsty, unsigned width, unsigned height,
                       bool render_condition_enabled)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   tc_sync(tc);
   pipe->clear_depth_stencil(pipe, dst, clear_flags, depth, stencil,
                             dstx, dsty, width, height,
                             render_condition_enabled);
}

struct tc_clear_buffer {
   struct pipe_resource *res;
   unsigned offset;
   unsigned size;
   char clear_value[16];
   int clear_value_size;
};

static void
tc_call_clear_buffer(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_clear_buffer *p = (struct tc_clear_buffer *)payload;

   pipe->clear_buffer(pipe, p->res, p->offset, p->size, p->clear_value,
                      p->clear_value_size);
   pipe_resource_reference(&p->res, NULL);
}

static void
tc_clear_buffer(struct pipe_context *_pipe, struct pipe_resource *res,
                unsigned offset, unsigned size,
                const void *clear_value, int clear_value_size)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct threaded_resource *tres = threaded_resource(res);
   struct tc_clear_buffer *p =
      tc_add_struct_typed_call(tc, TC_CALL_clear_buffer, tc_clear_buffer);

   tc_set_resource_reference(&p->res, res);
   p->offset = offset;
   p->size = size;
   memcpy(p->clear_value, clear_value, clear_value_size);
   p->clear_value_size = clear_value_size;

   util_range_add(&tres->valid_buffer_range, offset, offset + size);
}

struct tc_clear_texture {
   struct pipe_resource *res;
   unsigned level;
   struct pipe_box box;
   char data[16];
};

static void
tc_call_clear_texture(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_clear_texture *p = (struct tc_clear_texture *)payload;

   pipe->clear_texture(pipe, p->res, p->level, &p->box, p->data);
   pipe_resource_reference(&p->res, NULL);
}

static void
tc_clear_texture(struct pipe_context *_pipe, struct pipe_resource *res,
                 unsigned level, const struct pipe_box *box, const void *data)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_clear_texture *p =
      tc_add_struct_typed_call(tc, TC_CALL_clear_texture, tc_clear_texture);

   tc_set_resource_reference(&p->res, res);
   p->level = level;
   p->box = *box;
   memcpy(p->data, data,
          util_format_get_blocksize(res->format));
}

struct tc_resource_commit {
   struct pipe_resource *res;
   unsigned level;
   struct pipe_box box;
   bool commit;
};

static void
tc_call_resource_commit(struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_resource_commit *p = (struct tc_resource_commit *)payload;

   pipe->resource_commit(pipe, p->res, p->level, &p->box, p->commit);
   pipe_resource_reference(&p->res, NULL);
}

static bool
tc_resource_commit(struct pipe_context *_pipe, struct pipe_resource *res,
                   unsigned level, struct pipe_box *box, bool commit)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct tc_resource_commit *p =
      tc_add_struct_typed_call(tc, TC_CALL_resource_commit, tc_resource_commit);

   tc_set_resource_reference(&p->res, res);
   p->level = level;
   p->box = *box;
   p->commit = commit;
   return true; /* we don't care about the return value for this call */
}


/********************************************************************
 * callback
 */

struct tc_callback_payload {
   void (*fn)(void *data);
   void *data;
};

static void
tc_call_callback(UNUSED struct pipe_context *pipe, union tc_payload *payload)
{
   struct tc_callback_payload *p = (struct tc_callback_payload *)payload;

   p->fn(p->data);
}

static void
tc_callback(struct pipe_context *_pipe, void (*fn)(void *), void *data,
            bool asap)
{
   struct threaded_context *tc = threaded_context(_pipe);

   if (asap && tc_is_sync(tc)) {
      fn(data);
      return;
   }

   struct tc_callback_payload *p =
      tc_add_struct_typed_call(tc, TC_CALL_callback, tc_callback_payload);
   p->fn = fn;
   p->data = data;
}


/********************************************************************
 * create & destroy
 */

static void
tc_destroy(struct pipe_context *_pipe)
{
   struct threaded_context *tc = threaded_context(_pipe);
   struct pipe_context *pipe = tc->pipe;

   if (tc->base.const_uploader &&
       tc->base.stream_uploader != tc->base.const_uploader)
      u_upload_destroy(tc->base.const_uploader);

   if (tc->base.stream_uploader)
      u_upload_destroy(tc->base.stream_uploader);

   tc_sync(tc);

   if (util_queue_is_initialized(&tc->queue)) {
      util_queue_destroy(&tc->queue);

      for (unsigned i = 0; i < TC_MAX_BATCHES; i++) {
         util_queue_fence_destroy(&tc->batch_slots[i].fence);
         assert(!tc->batch_slots[i].token);
      }
   }

   slab_destroy_child(&tc->pool_transfers);
   assert(tc->batch_slots[tc->next].num_total_call_slots == 0);
   pipe->destroy(pipe);
   os_free_aligned(tc);
}

static const tc_execute execute_func[TC_NUM_CALLS] = {
#define CALL(name) tc_call_##name,
#include "u_threaded_context_calls.h"
#undef CALL
};

/**
 * Wrap an existing pipe_context into a threaded_context.
 *
 * \param pipe                 pipe_context to wrap
 * \param parent_transfer_pool parent slab pool set up for creating pipe_-
 *                             transfer objects; the driver should have one
 *                             in pipe_screen.
 * \param replace_buffer  callback for replacing a pipe_resource's storage
 *                        with another pipe_resource's storage.
 * \param out  if successful, the threaded_context will be returned here in
 *             addition to the return value if "out" != NULL
 */
struct pipe_context *
threaded_context_create(struct pipe_context *pipe,
                        struct slab_parent_pool *parent_transfer_pool,
                        tc_replace_buffer_storage_func replace_buffer,
                        tc_create_fence_func create_fence,
                        struct threaded_context **out)
{
   struct threaded_context *tc;

   STATIC_ASSERT(sizeof(union tc_payload) <= 8);
   STATIC_ASSERT(sizeof(struct tc_call) <= 16);

   if (!pipe)
      return NULL;

   util_cpu_detect();

   if (!debug_get_bool_option("GALLIUM_THREAD", util_cpu_caps.nr_cpus > 1))
      return pipe;

   tc = os_malloc_aligned(sizeof(struct threaded_context), 16);
   if (!tc) {
      pipe->destroy(pipe);
      return NULL;
   }
   memset(tc, 0, sizeof(*tc));

   assert((uintptr_t)tc % 16 == 0);
   /* These should be static asserts, but they don't work with MSVC */
   assert(offsetof(struct threaded_context, batch_slots) % 16 == 0);
   assert(offsetof(struct threaded_context, batch_slots[0].call) % 16 == 0);
   assert(offsetof(struct threaded_context, batch_slots[0].call[1]) % 16 == 0);
   assert(offsetof(struct threaded_context, batch_slots[1].call) % 16 == 0);

   /* The driver context isn't wrapped, so set its "priv" to NULL. */
   pipe->priv = NULL;

   tc->pipe = pipe;
   tc->replace_buffer_storage = replace_buffer;
   tc->create_fence = create_fence;
   tc->map_buffer_alignment =
      pipe->screen->get_param(pipe->screen, PIPE_CAP_MIN_MAP_BUFFER_ALIGNMENT);
   tc->base.priv = pipe; /* priv points to the wrapped driver context */
   tc->base.screen = pipe->screen;
   tc->base.destroy = tc_destroy;
   tc->base.callback = tc_callback;

   tc->base.stream_uploader = u_upload_clone(&tc->base, pipe->stream_uploader);
   if (pipe->stream_uploader == pipe->const_uploader)
      tc->base.const_uploader = tc->base.stream_uploader;
   else
      tc->base.const_uploader = u_upload_clone(&tc->base, pipe->const_uploader);

   if (!tc->base.stream_uploader || !tc->base.const_uploader)
      goto fail;

   /* The queue size is the number of batches "waiting". Batches are removed
    * from the queue before being executed, so keep one tc_batch slot for that
    * execution. Also, keep one unused slot for an unflushed batch.
    */
   if (!util_queue_init(&tc->queue, "gallium_drv", TC_MAX_BATCHES - 2, 1, 0))
      goto fail;

   for (unsigned i = 0; i < TC_MAX_BATCHES; i++) {
      tc->batch_slots[i].sentinel = TC_SENTINEL;
      tc->batch_slots[i].pipe = pipe;
      util_queue_fence_init(&tc->batch_slots[i].fence);
   }

   LIST_INITHEAD(&tc->unflushed_queries);

   slab_create_child(&tc->pool_transfers, parent_transfer_pool);

#define CTX_INIT(_member) \
   tc->base._member = tc->pipe->_member ? tc_##_member : NULL

   CTX_INIT(flush);
   CTX_INIT(draw_vbo);
   CTX_INIT(launch_grid);
   CTX_INIT(resource_copy_region);
   CTX_INIT(blit);
   CTX_INIT(clear);
   CTX_INIT(clear_render_target);
   CTX_INIT(clear_depth_stencil);
   CTX_INIT(clear_buffer);
   CTX_INIT(clear_texture);
   CTX_INIT(flush_resource);
   CTX_INIT(generate_mipmap);
   CTX_INIT(render_condition);
   CTX_INIT(create_query);
   CTX_INIT(create_batch_query);
   CTX_INIT(destroy_query);
   CTX_INIT(begin_query);
   CTX_INIT(end_query);
   CTX_INIT(get_query_result);
   CTX_INIT(get_query_result_resource);
   CTX_INIT(set_active_query_state);
   CTX_INIT(create_blend_state);
   CTX_INIT(bind_blend_state);
   CTX_INIT(delete_blend_state);
   CTX_INIT(create_sampler_state);
   CTX_INIT(bind_sampler_states);
   CTX_INIT(delete_sampler_state);
   CTX_INIT(create_rasterizer_state);
   CTX_INIT(bind_rasterizer_state);
   CTX_INIT(delete_rasterizer_state);
   CTX_INIT(create_depth_stencil_alpha_state);
   CTX_INIT(bind_depth_stencil_alpha_state);
   CTX_INIT(delete_depth_stencil_alpha_state);
   CTX_INIT(create_fs_state);
   CTX_INIT(bind_fs_state);
   CTX_INIT(delete_fs_state);
   CTX_INIT(create_vs_state);
   CTX_INIT(bind_vs_state);
   CTX_INIT(delete_vs_state);
   CTX_INIT(create_gs_state);
   CTX_INIT(bind_gs_state);
   CTX_INIT(delete_gs_state);
   CTX_INIT(create_tcs_state);
   CTX_INIT(bind_tcs_state);
   CTX_INIT(delete_tcs_state);
   CTX_INIT(create_tes_state);
   CTX_INIT(bind_tes_state);
   CTX_INIT(delete_tes_state);
   CTX_INIT(create_compute_state);
   CTX_INIT(bind_compute_state);
   CTX_INIT(delete_compute_state);
   CTX_INIT(create_vertex_elements_state);
   CTX_INIT(bind_vertex_elements_state);
   CTX_INIT(delete_vertex_elements_state);
   CTX_INIT(set_blend_color);
   CTX_INIT(set_stencil_ref);
   CTX_INIT(set_sample_mask);
   CTX_INIT(set_min_samples);
   CTX_INIT(set_clip_state);
   CTX_INIT(set_constant_buffer);
   CTX_INIT(set_framebuffer_state);
   CTX_INIT(set_polygon_stipple);
   CTX_INIT(set_scissor_states);
   CTX_INIT(set_viewport_states);
   CTX_INIT(set_window_rectangles);
   CTX_INIT(set_sampler_views);
   CTX_INIT(set_tess_state);
   CTX_INIT(set_shader_buffers);
   CTX_INIT(set_shader_images);
   CTX_INIT(set_vertex_buffers);
   CTX_INIT(create_stream_output_target);
   CTX_INIT(stream_output_target_destroy);
   CTX_INIT(set_stream_output_targets);
   CTX_INIT(create_sampler_view);
   CTX_INIT(sampler_view_destroy);
   CTX_INIT(create_surface);
   CTX_INIT(surface_destroy);
   CTX_INIT(transfer_map);
   CTX_INIT(transfer_flush_region);
   CTX_INIT(transfer_unmap);
   CTX_INIT(buffer_subdata);
   CTX_INIT(texture_subdata);
   CTX_INIT(texture_barrier);
   CTX_INIT(memory_barrier);
   CTX_INIT(resource_commit);
   CTX_INIT(create_video_codec);
   CTX_INIT(create_video_buffer);
   CTX_INIT(set_compute_resources);
   CTX_INIT(set_global_binding);
   CTX_INIT(get_sample_position);
   CTX_INIT(invalidate_resource);
   CTX_INIT(get_device_reset_status);
   CTX_INIT(set_device_reset_callback);
   CTX_INIT(dump_debug_state);
   CTX_INIT(set_log_context);
   CTX_INIT(emit_string_marker);
   CTX_INIT(set_debug_callback);
   CTX_INIT(create_fence_fd);
   CTX_INIT(fence_server_sync);
   CTX_INIT(fence_server_signal);
   CTX_INIT(get_timestamp);
   CTX_INIT(create_texture_handle);
   CTX_INIT(delete_texture_handle);
   CTX_INIT(make_texture_handle_resident);
   CTX_INIT(create_image_handle);
   CTX_INIT(delete_image_handle);
   CTX_INIT(make_image_handle_resident);
#undef CTX_INIT

   if (out)
      *out = tc;

   return &tc->base;

fail:
   tc_destroy(&tc->base);
   return NULL;
}
