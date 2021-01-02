/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file iris_query.c
 *
 * ============================= GENXML CODE =============================
 *              [This file is compiled once per generation.]
 * =======================================================================
 *
 * Query object support.  This allows measuring various simple statistics
 * via counters on the GPU.  We use GenX code for MI_MATH calculations.
 */

#include <stdio.h>
#include <errno.h>
#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_inlines.h"
#include "util/u_upload_mgr.h"
#include "iris_context.h"
#include "iris_defines.h"
#include "iris_fence.h"
#include "iris_monitor.h"
#include "iris_resource.h"
#include "iris_screen.h"

#include "iris_genx_macros.h"

#define SO_PRIM_STORAGE_NEEDED(n) (GENX(SO_PRIM_STORAGE_NEEDED0_num) + (n) * 8)
#define SO_NUM_PRIMS_WRITTEN(n)   (GENX(SO_NUM_PRIMS_WRITTEN0_num) + (n) * 8)

struct iris_query {
   enum pipe_query_type type;
   int index;

   bool ready;

   bool stalled;

   uint64_t result;

   struct iris_state_ref query_state_ref;
   struct iris_query_snapshots *map;
   struct iris_syncobj *syncobj;

   int batch_idx;

   struct iris_monitor_object *monitor;

   /* Fence for PIPE_QUERY_GPU_FINISHED. */
   struct pipe_fence_handle *fence;
};

struct iris_query_snapshots {
   /** iris_render_condition's saved MI_PREDICATE_RESULT value. */
   uint64_t predicate_result;

   /** Have the start/end snapshots landed? */
   uint64_t snapshots_landed;

   /** Starting and ending counter snapshots */
   uint64_t start;
   uint64_t end;
};

struct iris_query_so_overflow {
   uint64_t predicate_result;
   uint64_t snapshots_landed;

   struct {
      uint64_t prim_storage_needed[2];
      uint64_t num_prims[2];
   } stream[4];
};

static struct gen_mi_value
query_mem64(struct iris_query *q, uint32_t offset)
{
   struct iris_address addr = {
      .bo = iris_resource_bo(q->query_state_ref.res),
      .offset = q->query_state_ref.offset + offset,
      .access = IRIS_DOMAIN_OTHER_WRITE
   };
   return gen_mi_mem64(addr);
}

/**
 * Is this type of query written by PIPE_CONTROL?
 */
static bool
iris_is_query_pipelined(struct iris_query *q)
{
   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIMESTAMP_DISJOINT:
   case PIPE_QUERY_TIME_ELAPSED:
      return true;

   default:
      return false;
   }
}

static void
mark_available(struct iris_context *ice, struct iris_query *q)
{
   struct iris_batch *batch = &ice->batches[q->batch_idx];
   unsigned flags = PIPE_CONTROL_WRITE_IMMEDIATE;
   unsigned offset = offsetof(struct iris_query_snapshots, snapshots_landed);
   struct iris_bo *bo = iris_resource_bo(q->query_state_ref.res);
   offset += q->query_state_ref.offset;

   if (!iris_is_query_pipelined(q)) {
      batch->screen->vtbl.store_data_imm64(batch, bo, offset, true);
   } else {
      /* Order available *after* the query results. */
      flags |= PIPE_CONTROL_FLUSH_ENABLE;
      iris_emit_pipe_control_write(batch, "query: mark available",
                                   flags, bo, offset, true);
   }
}

/**
 * Write PS_DEPTH_COUNT to q->(dest) via a PIPE_CONTROL.
 */
static void
iris_pipelined_write(struct iris_batch *batch,
                     struct iris_query *q,
                     enum pipe_control_flags flags,
                     unsigned offset)
{
   const struct gen_device_info *devinfo = &batch->screen->devinfo;
   const unsigned optional_cs_stall =
      GEN_GEN == 9 && devinfo->gt == 4 ?  PIPE_CONTROL_CS_STALL : 0;
   struct iris_bo *bo = iris_resource_bo(q->query_state_ref.res);

   iris_emit_pipe_control_write(batch, "query: pipelined snapshot write",
                                flags | optional_cs_stall,
                                bo, offset, 0ull);
}

static void
write_value(struct iris_context *ice, struct iris_query *q, unsigned offset)
{
   struct iris_batch *batch = &ice->batches[q->batch_idx];
   struct iris_bo *bo = iris_resource_bo(q->query_state_ref.res);

   if (!iris_is_query_pipelined(q)) {
      iris_emit_pipe_control_flush(batch,
                                   "query: non-pipelined snapshot write",
                                   PIPE_CONTROL_CS_STALL |
                                   PIPE_CONTROL_STALL_AT_SCOREBOARD);
      q->stalled = true;
   }

   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      if (GEN_GEN >= 10) {
         /* "Driver must program PIPE_CONTROL with only Depth Stall Enable
          *  bit set prior to programming a PIPE_CONTROL with Write PS Depth
          *  Count sync operation."
          */
         iris_emit_pipe_control_flush(batch,
                                      "workaround: depth stall before writing "
                                      "PS_DEPTH_COUNT",
                                      PIPE_CONTROL_DEPTH_STALL);
      }
      iris_pipelined_write(&ice->batches[IRIS_BATCH_RENDER], q,
                           PIPE_CONTROL_WRITE_DEPTH_COUNT |
                           PIPE_CONTROL_DEPTH_STALL,
                           offset);
      break;
   case PIPE_QUERY_TIME_ELAPSED:
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIMESTAMP_DISJOINT:
      iris_pipelined_write(&ice->batches[IRIS_BATCH_RENDER], q,
                           PIPE_CONTROL_WRITE_TIMESTAMP,
                           offset);
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      batch->screen->vtbl.store_register_mem64(batch,
                                     q->index == 0 ?
                                     GENX(CL_INVOCATION_COUNT_num) :
                                     SO_PRIM_STORAGE_NEEDED(q->index),
                                     bo, offset, false);
      break;
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      batch->screen->vtbl.store_register_mem64(batch,
                                     SO_NUM_PRIMS_WRITTEN(q->index),
                                     bo, offset, false);
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS_SINGLE: {
      static const uint32_t index_to_reg[] = {
         GENX(IA_VERTICES_COUNT_num),
         GENX(IA_PRIMITIVES_COUNT_num),
         GENX(VS_INVOCATION_COUNT_num),
         GENX(GS_INVOCATION_COUNT_num),
         GENX(GS_PRIMITIVES_COUNT_num),
         GENX(CL_INVOCATION_COUNT_num),
         GENX(CL_PRIMITIVES_COUNT_num),
         GENX(PS_INVOCATION_COUNT_num),
         GENX(HS_INVOCATION_COUNT_num),
         GENX(DS_INVOCATION_COUNT_num),
         GENX(CS_INVOCATION_COUNT_num),
      };
      const uint32_t reg = index_to_reg[q->index];

      batch->screen->vtbl.store_register_mem64(batch, reg, bo, offset, false);
      break;
   }
   default:
      assert(false);
   }
}

static void
write_overflow_values(struct iris_context *ice, struct iris_query *q, bool end)
{
   struct iris_batch *batch = &ice->batches[IRIS_BATCH_RENDER];
   uint32_t count = q->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE ? 1 : 4;
   struct iris_bo *bo = iris_resource_bo(q->query_state_ref.res);
   uint32_t offset = q->query_state_ref.offset;

   iris_emit_pipe_control_flush(batch,
                                "query: write SO overflow snapshots",
                                PIPE_CONTROL_CS_STALL |
                                PIPE_CONTROL_STALL_AT_SCOREBOARD);
   for (uint32_t i = 0; i < count; i++) {
      int s = q->index + i;
      int g_idx = offset + offsetof(struct iris_query_so_overflow,
                           stream[s].num_prims[end]);
      int w_idx = offset + offsetof(struct iris_query_so_overflow,
                           stream[s].prim_storage_needed[end]);
      batch->screen->vtbl.store_register_mem64(batch, SO_NUM_PRIMS_WRITTEN(s),
                                     bo, g_idx, false);
      batch->screen->vtbl.store_register_mem64(batch, SO_PRIM_STORAGE_NEEDED(s),
                                     bo, w_idx, false);
   }
}

static uint64_t
iris_raw_timestamp_delta(uint64_t time0, uint64_t time1)
{
   if (time0 > time1) {
      return (1ULL << TIMESTAMP_BITS) + time1 - time0;
   } else {
      return time1 - time0;
   }
}

static bool
stream_overflowed(struct iris_query_so_overflow *so, int s)
{
   return (so->stream[s].prim_storage_needed[1] -
           so->stream[s].prim_storage_needed[0]) !=
          (so->stream[s].num_prims[1] - so->stream[s].num_prims[0]);
}

static void
calculate_result_on_cpu(const struct gen_device_info *devinfo,
                        struct iris_query *q)
{
   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      q->result = q->map->end != q->map->start;
      break;
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_TIMESTAMP_DISJOINT:
      /* The timestamp is the single starting snapshot. */
      q->result = gen_device_info_timebase_scale(devinfo, q->map->start);
      q->result &= (1ull << TIMESTAMP_BITS) - 1;
      break;
   case PIPE_QUERY_TIME_ELAPSED:
      q->result = iris_raw_timestamp_delta(q->map->start, q->map->end);
      q->result = gen_device_info_timebase_scale(devinfo, q->result);
      q->result &= (1ull << TIMESTAMP_BITS) - 1;
      break;
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
      q->result = stream_overflowed((void *) q->map, q->index);
      break;
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
      q->result = false;
      for (int i = 0; i < MAX_VERTEX_STREAMS; i++)
         q->result |= stream_overflowed((void *) q->map, i);
      break;
   case PIPE_QUERY_PIPELINE_STATISTICS_SINGLE:
      q->result = q->map->end - q->map->start;

      /* WaDividePSInvocationCountBy4:HSW,BDW */
      if (GEN_GEN == 8 && q->index == PIPE_STAT_QUERY_PS_INVOCATIONS)
         q->result /= 4;
      break;
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_PRIMITIVES_GENERATED:
   case PIPE_QUERY_PRIMITIVES_EMITTED:
   default:
      q->result = q->map->end - q->map->start;
      break;
   }

   q->ready = true;
}

/**
 * Calculate the streamout overflow for stream \p idx:
 *
 * (num_prims[1] - num_prims[0]) - (storage_needed[1] - storage_needed[0])
 */
static struct gen_mi_value
calc_overflow_for_stream(struct gen_mi_builder *b,
                         struct iris_query *q,
                         int idx)
{
#define C(counter, i) query_mem64(q, \
   offsetof(struct iris_query_so_overflow, stream[idx].counter[i]))

   return gen_mi_isub(b, gen_mi_isub(b, C(num_prims, 1), C(num_prims, 0)),
                         gen_mi_isub(b, C(prim_storage_needed, 1),
                                        C(prim_storage_needed, 0)));
#undef C
}

/**
 * Calculate whether any stream has overflowed.
 */
static struct gen_mi_value
calc_overflow_any_stream(struct gen_mi_builder *b, struct iris_query *q)
{
   struct gen_mi_value stream_result[MAX_VERTEX_STREAMS];
   for (int i = 0; i < MAX_VERTEX_STREAMS; i++)
      stream_result[i] = calc_overflow_for_stream(b, q, i);

   struct gen_mi_value result = stream_result[0];
   for (int i = 1; i < MAX_VERTEX_STREAMS; i++)
      result = gen_mi_ior(b, result, stream_result[i]);

   return result;
}

static bool
query_is_boolean(enum pipe_query_type type)
{
   switch (type) {
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
      return true;
   default:
      return false;
   }
}

/**
 * Calculate the result using MI_MATH.
 */
static struct gen_mi_value
calculate_result_on_gpu(const struct gen_device_info *devinfo,
                        struct gen_mi_builder *b,
                        struct iris_query *q)
{
   struct gen_mi_value result;
   struct gen_mi_value start_val =
      query_mem64(q, offsetof(struct iris_query_snapshots, start));
   struct gen_mi_value end_val =
      query_mem64(q, offsetof(struct iris_query_snapshots, end));

   switch (q->type) {
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
      result = calc_overflow_for_stream(b, q, q->index);
      break;
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
      result = calc_overflow_any_stream(b, q);
      break;
   case PIPE_QUERY_TIMESTAMP: {
      /* TODO: This discards any fractional bits of the timebase scale.
       * We would need to do a bit of fixed point math on the CS ALU, or
       * launch an actual shader to calculate this with full precision.
       */
      uint32_t scale = 1000000000ull / devinfo->timestamp_frequency;
      result = gen_mi_iand(b, gen_mi_imm((1ull << 36) - 1),
                           gen_mi_imul_imm(b, start_val, scale));
      break;
   }
   case PIPE_QUERY_TIME_ELAPSED: {
      /* TODO: This discards fractional bits (see above). */
      uint32_t scale = 1000000000ull / devinfo->timestamp_frequency;
      result = gen_mi_imul_imm(b, gen_mi_isub(b, end_val, start_val), scale);
      break;
   }
   default:
      result = gen_mi_isub(b, end_val, start_val);
      break;
   }

   /* WaDividePSInvocationCountBy4:HSW,BDW */
   if (GEN_GEN == 8 &&
       q->type == PIPE_QUERY_PIPELINE_STATISTICS_SINGLE &&
       q->index == PIPE_STAT_QUERY_PS_INVOCATIONS)
      result = gen_mi_ushr32_imm(b, result, 2);

   if (query_is_boolean(q->type))
      result = gen_mi_iand(b, gen_mi_nz(b, result), gen_mi_imm(1));

   return result;
}

static struct pipe_query *
iris_create_query(struct pipe_context *ctx,
                  unsigned query_type,
                  unsigned index)
{
   struct iris_query *q = calloc(1, sizeof(struct iris_query));

   q->type = query_type;
   q->index = index;
   q->monitor = NULL;

   if (q->type == PIPE_QUERY_PIPELINE_STATISTICS_SINGLE &&
       q->index == PIPE_STAT_QUERY_CS_INVOCATIONS)
      q->batch_idx = IRIS_BATCH_COMPUTE;
   else
      q->batch_idx = IRIS_BATCH_RENDER;
   return (struct pipe_query *) q;
}

static struct pipe_query *
iris_create_batch_query(struct pipe_context *ctx,
                        unsigned num_queries,
                        unsigned *query_types)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_query *q = calloc(1, sizeof(struct iris_query));
   if (unlikely(!q))
      return NULL;
   q->type = PIPE_QUERY_DRIVER_SPECIFIC;
   q->index = -1;
   q->monitor = iris_create_monitor_object(ice, num_queries, query_types);
   if (unlikely(!q->monitor)) {
      free(q);
      return NULL;
   }

   return (struct pipe_query *) q;
}

static void
iris_destroy_query(struct pipe_context *ctx, struct pipe_query *p_query)
{
   struct iris_query *query = (void *) p_query;
   struct iris_screen *screen = (void *) ctx->screen;
   if (query->monitor) {
      iris_destroy_monitor_object(ctx, query->monitor);
      query->monitor = NULL;
   } else {
      iris_syncobj_reference(screen, &query->syncobj, NULL);
      screen->base.fence_reference(ctx->screen, &query->fence, NULL);
   }
   pipe_resource_reference(&query->query_state_ref.res, NULL);
   free(query);
}


static bool
iris_begin_query(struct pipe_context *ctx, struct pipe_query *query)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_query *q = (void *) query;

   if (q->monitor)
      return iris_begin_monitor(ctx, q->monitor);

   void *ptr = NULL;
   uint32_t size;

   if (q->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE ||
       q->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE)
      size = sizeof(struct iris_query_so_overflow);
   else
      size = sizeof(struct iris_query_snapshots);

   u_upload_alloc(ice->query_buffer_uploader, 0,
                  size, size, &q->query_state_ref.offset,
                  &q->query_state_ref.res, &ptr);

   if (!iris_resource_bo(q->query_state_ref.res))
      return false;

   q->map = ptr;
   if (!q->map)
      return false;

   q->result = 0ull;
   q->ready = false;
   WRITE_ONCE(q->map->snapshots_landed, false);

   if (q->type == PIPE_QUERY_PRIMITIVES_GENERATED && q->index == 0) {
      ice->state.prims_generated_query_active = true;
      ice->state.dirty |= IRIS_DIRTY_STREAMOUT | IRIS_DIRTY_CLIP;
   }

   if (q->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE ||
       q->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE)
      write_overflow_values(ice, q, false);
   else
      write_value(ice, q,
                  q->query_state_ref.offset +
                  offsetof(struct iris_query_snapshots, start));

   return true;
}

static bool
iris_end_query(struct pipe_context *ctx, struct pipe_query *query)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_query *q = (void *) query;

   if (q->monitor)
      return iris_end_monitor(ctx, q->monitor);

   if (q->type == PIPE_QUERY_GPU_FINISHED) {
      ctx->flush(ctx, &q->fence, PIPE_FLUSH_DEFERRED);
      return true;
   }

   struct iris_batch *batch = &ice->batches[q->batch_idx];

   if (q->type == PIPE_QUERY_TIMESTAMP) {
      iris_begin_query(ctx, query);
      iris_batch_reference_signal_syncobj(batch, &q->syncobj);
      mark_available(ice, q);
      return true;
   }

   if (q->type == PIPE_QUERY_PRIMITIVES_GENERATED && q->index == 0) {
      ice->state.prims_generated_query_active = false;
      ice->state.dirty |= IRIS_DIRTY_STREAMOUT | IRIS_DIRTY_CLIP;
   }

   if (q->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE ||
       q->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE)
      write_overflow_values(ice, q, true);
   else
      write_value(ice, q,
                  q->query_state_ref.offset +
                  offsetof(struct iris_query_snapshots, end));

   iris_batch_reference_signal_syncobj(batch, &q->syncobj);
   mark_available(ice, q);

   return true;
}

/**
 * See if the snapshots have landed for a query, and if so, compute the
 * result and mark it ready.  Does not flush (unlike iris_get_query_result).
 */
static void
iris_check_query_no_flush(struct iris_context *ice, struct iris_query *q)
{
   struct iris_screen *screen = (void *) ice->ctx.screen;
   const struct gen_device_info *devinfo = &screen->devinfo;

   if (!q->ready && READ_ONCE(q->map->snapshots_landed)) {
      calculate_result_on_cpu(devinfo, q);
   }
}

static bool
iris_get_query_result(struct pipe_context *ctx,
                      struct pipe_query *query,
                      bool wait,
                      union pipe_query_result *result)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_query *q = (void *) query;

   if (q->monitor)
      return iris_get_monitor_result(ctx, q->monitor, wait, result->batch);

   struct iris_screen *screen = (void *) ctx->screen;
   const struct gen_device_info *devinfo = &screen->devinfo;

   if (unlikely(screen->no_hw)) {
      result->u64 = 0;
      return true;
   }

   if (q->type == PIPE_QUERY_GPU_FINISHED) {
      struct pipe_screen *screen = ctx->screen;

      result->b = screen->fence_finish(screen, ctx, q->fence,
                                       wait ? PIPE_TIMEOUT_INFINITE : 0);
      return result->b;
   }

   if (!q->ready) {
      struct iris_batch *batch = &ice->batches[q->batch_idx];
      if (q->syncobj == iris_batch_get_signal_syncobj(batch))
         iris_batch_flush(batch);

      while (!READ_ONCE(q->map->snapshots_landed)) {
         if (wait)
            iris_wait_syncobj(ctx->screen, q->syncobj, INT64_MAX);
         else
            return false;
      }

      assert(READ_ONCE(q->map->snapshots_landed));
      calculate_result_on_cpu(devinfo, q);
   }

   assert(q->ready);

   result->u64 = q->result;

   return true;
}

static void
iris_get_query_result_resource(struct pipe_context *ctx,
                               struct pipe_query *query,
                               bool wait,
                               enum pipe_query_value_type result_type,
                               int index,
                               struct pipe_resource *p_res,
                               unsigned offset)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_query *q = (void *) query;
   struct iris_batch *batch = &ice->batches[q->batch_idx];
   const struct gen_device_info *devinfo = &batch->screen->devinfo;
   struct iris_resource *res = (void *) p_res;
   struct iris_bo *query_bo = iris_resource_bo(q->query_state_ref.res);
   struct iris_bo *dst_bo = iris_resource_bo(p_res);
   unsigned snapshots_landed_offset =
      offsetof(struct iris_query_snapshots, snapshots_landed);

   res->bind_history |= PIPE_BIND_QUERY_BUFFER;

   if (index == -1) {
      /* They're asking for the availability of the result.  If we still
       * have commands queued up which produce the result, submit them
       * now so that progress happens.  Either way, copy the snapshots
       * landed field to the destination resource.
       */
      if (q->syncobj == iris_batch_get_signal_syncobj(batch))
         iris_batch_flush(batch);

      batch->screen->vtbl.copy_mem_mem(batch, dst_bo, offset,
                             query_bo, snapshots_landed_offset,
                             result_type <= PIPE_QUERY_TYPE_U32 ? 4 : 8);
      return;
   }

   if (!q->ready && READ_ONCE(q->map->snapshots_landed)) {
      /* The final snapshots happen to have landed, so let's just compute
       * the result on the CPU now...
       */
      calculate_result_on_cpu(devinfo, q);
   }

   if (q->ready) {
      /* We happen to have the result on the CPU, so just copy it. */
      if (result_type <= PIPE_QUERY_TYPE_U32) {
         batch->screen->vtbl.store_data_imm32(batch, dst_bo, offset, q->result);
      } else {
         batch->screen->vtbl.store_data_imm64(batch, dst_bo, offset, q->result);
      }

      /* Make sure the result lands before they use bind the QBO elsewhere
       * and use the result.
       */
      // XXX: Why?  i965 doesn't do this.
      iris_emit_pipe_control_flush(batch,
                                   "query: unknown QBO flushing hack",
                                   PIPE_CONTROL_CS_STALL);
      return;
   }

   bool predicated = !wait && !q->stalled;

   struct gen_mi_builder b;
   gen_mi_builder_init(&b, batch);

   iris_batch_sync_region_start(batch);

   struct gen_mi_value result = calculate_result_on_gpu(devinfo, &b, q);
   struct gen_mi_value dst =
      result_type <= PIPE_QUERY_TYPE_U32 ?
      gen_mi_mem32(rw_bo(dst_bo, offset, IRIS_DOMAIN_OTHER_WRITE)) :
      gen_mi_mem64(rw_bo(dst_bo, offset, IRIS_DOMAIN_OTHER_WRITE));

   if (predicated) {
      gen_mi_store(&b, gen_mi_reg32(MI_PREDICATE_RESULT),
                   gen_mi_mem64(ro_bo(query_bo, snapshots_landed_offset)));
      gen_mi_store_if(&b, dst, result);
   } else {
      gen_mi_store(&b, dst, result);
   }

   iris_batch_sync_region_end(batch);
}

static void
iris_set_active_query_state(struct pipe_context *ctx, bool enable)
{
   struct iris_context *ice = (void *) ctx;

   if (ice->state.statistics_counters_enabled == enable)
      return;

   // XXX: most packets aren't paying attention to this yet, because it'd
   // have to be done dynamically at draw time, which is a pain
   ice->state.statistics_counters_enabled = enable;
   ice->state.dirty |= IRIS_DIRTY_CLIP |
                       IRIS_DIRTY_RASTER |
                       IRIS_DIRTY_STREAMOUT |
                       IRIS_DIRTY_WM;
   ice->state.stage_dirty |= IRIS_STAGE_DIRTY_GS |
                             IRIS_STAGE_DIRTY_TCS |
                             IRIS_STAGE_DIRTY_TES |
                             IRIS_STAGE_DIRTY_VS;
}

static void
set_predicate_enable(struct iris_context *ice, bool value)
{
   if (value)
      ice->state.predicate = IRIS_PREDICATE_STATE_RENDER;
   else
      ice->state.predicate = IRIS_PREDICATE_STATE_DONT_RENDER;
}

static void
set_predicate_for_result(struct iris_context *ice,
                         struct iris_query *q,
                         bool inverted)
{
   struct iris_batch *batch = &ice->batches[IRIS_BATCH_RENDER];
   struct iris_bo *bo = iris_resource_bo(q->query_state_ref.res);

   iris_batch_sync_region_start(batch);

   /* The CPU doesn't have the query result yet; use hardware predication */
   ice->state.predicate = IRIS_PREDICATE_STATE_USE_BIT;

   /* Ensure the memory is coherent for MI_LOAD_REGISTER_* commands. */
   iris_emit_pipe_control_flush(batch,
                                "conditional rendering: set predicate",
                                PIPE_CONTROL_FLUSH_ENABLE);
   q->stalled = true;

   struct gen_mi_builder b;
   gen_mi_builder_init(&b, batch);

   struct gen_mi_value result;

   switch (q->type) {
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
      result = calc_overflow_for_stream(&b, q, q->index);
      break;
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
      result = calc_overflow_any_stream(&b, q);
      break;
   default: {
      /* PIPE_QUERY_OCCLUSION_* */
      struct gen_mi_value start =
         query_mem64(q, offsetof(struct iris_query_snapshots, start));
      struct gen_mi_value end =
         query_mem64(q, offsetof(struct iris_query_snapshots, end));
      result = gen_mi_isub(&b, end, start);
      break;
   }
   }

   result = inverted ? gen_mi_z(&b, result) : gen_mi_nz(&b, result);
   result = gen_mi_iand(&b, result, gen_mi_imm(1));

   /* We immediately set the predicate on the render batch, as all the
    * counters come from 3D operations.  However, we may need to predicate
    * a compute dispatch, which executes in a different GEM context and has
    * a different MI_PREDICATE_RESULT register.  So, we save the result to
    * memory and reload it in iris_launch_grid.
    */
   gen_mi_value_ref(&b, result);
   gen_mi_store(&b, gen_mi_reg32(MI_PREDICATE_RESULT), result);
   gen_mi_store(&b, query_mem64(q, offsetof(struct iris_query_snapshots,
                                            predicate_result)), result);
   ice->state.compute_predicate = bo;

   iris_batch_sync_region_end(batch);
}

static void
iris_render_condition(struct pipe_context *ctx,
                      struct pipe_query *query,
                      bool condition,
                      enum pipe_render_cond_flag mode)
{
   struct iris_context *ice = (void *) ctx;
   struct iris_query *q = (void *) query;

   /* The old condition isn't relevant; we'll update it if necessary */
   ice->state.compute_predicate = NULL;
   ice->condition.query = q;
   ice->condition.condition = condition;

   if (!q) {
      ice->state.predicate = IRIS_PREDICATE_STATE_RENDER;
      return;
   }

   iris_check_query_no_flush(ice, q);

   if (q->result || q->ready) {
      set_predicate_enable(ice, (q->result != 0) ^ condition);
   } else {
      if (mode == PIPE_RENDER_COND_NO_WAIT ||
          mode == PIPE_RENDER_COND_BY_REGION_NO_WAIT) {
         perf_debug(&ice->dbg, "Conditional rendering demoted from "
                    "\"no wait\" to \"wait\".");
      }
      set_predicate_for_result(ice, q, condition);
   }
}

static void
iris_resolve_conditional_render(struct iris_context *ice)
{
   struct pipe_context *ctx = (void *) ice;
   struct iris_query *q = ice->condition.query;
   struct pipe_query *query = (void *) q;
   union pipe_query_result result;

   if (ice->state.predicate != IRIS_PREDICATE_STATE_USE_BIT)
      return;

   assert(q);

   iris_get_query_result(ctx, query, true, &result);
   set_predicate_enable(ice, (q->result != 0) ^ ice->condition.condition);
}

void
genX(init_query)(struct iris_context *ice)
{
   struct pipe_context *ctx = &ice->ctx;
   struct iris_screen *screen = (struct iris_screen *)ctx->screen;

   ctx->create_query = iris_create_query;
   ctx->create_batch_query = iris_create_batch_query;
   ctx->destroy_query = iris_destroy_query;
   ctx->begin_query = iris_begin_query;
   ctx->end_query = iris_end_query;
   ctx->get_query_result = iris_get_query_result;
   ctx->get_query_result_resource = iris_get_query_result_resource;
   ctx->set_active_query_state = iris_set_active_query_state;
   ctx->render_condition = iris_render_condition;

   screen->vtbl.resolve_conditional_render = iris_resolve_conditional_render;
}
