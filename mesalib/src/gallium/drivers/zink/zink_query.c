#include "zink_query.h"

#include "zink_context.h"
#include "zink_fence.h"
#include "zink_resource.h"
#include "zink_screen.h"

#include "util/hash_table.h"
#include "util/set.h"
#include "util/u_dump.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

#define NUM_QUERIES 50

struct zink_query {
   enum pipe_query_type type;

   VkQueryPool query_pool;
   VkQueryPool xfb_query_pool[PIPE_MAX_VERTEX_STREAMS - 1]; //stream 0 is in the base pool
   unsigned curr_query, num_queries, last_start;

   VkQueryType vkqtype;
   unsigned index;
   bool precise;
   bool xfb_running;
   bool xfb_overflow;

   bool active; /* query is considered active by vk */
   bool needs_reset; /* query is considered active by vk and cannot be destroyed */
   bool dead; /* query should be destroyed when its fence finishes */

   unsigned fences;
   struct list_head active_list;

   struct list_head stats_list; /* when active, statistics queries are added to ctx->primitives_generated_queries */
   bool have_gs[NUM_QUERIES]; /* geometry shaders use GEOMETRY_SHADER_PRIMITIVES_BIT */
   bool have_xfb[NUM_QUERIES]; /* xfb was active during this query */

   struct zink_batch_usage batch_id; //batch that the query was started in

   union pipe_query_result accumulated_result;
   struct zink_resource *predicate;
   bool predicate_dirty;
};

static VkQueryPipelineStatisticFlags
pipeline_statistic_convert(enum pipe_statistics_query_index idx)
{
   unsigned map[] = {
      [PIPE_STAT_QUERY_IA_VERTICES] = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT,
      [PIPE_STAT_QUERY_IA_PRIMITIVES] = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT,
      [PIPE_STAT_QUERY_VS_INVOCATIONS] = VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT,
      [PIPE_STAT_QUERY_GS_INVOCATIONS] = VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT,
      [PIPE_STAT_QUERY_GS_PRIMITIVES] = VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT,
      [PIPE_STAT_QUERY_C_INVOCATIONS] = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT,
      [PIPE_STAT_QUERY_C_PRIMITIVES] = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT,
      [PIPE_STAT_QUERY_PS_INVOCATIONS] = VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT,
      [PIPE_STAT_QUERY_HS_INVOCATIONS] = VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT,
      [PIPE_STAT_QUERY_DS_INVOCATIONS] = VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT,
      [PIPE_STAT_QUERY_CS_INVOCATIONS] = VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT
   };
   assert(idx < ARRAY_SIZE(map));
   return map[idx];
}

static void
timestamp_to_nanoseconds(struct zink_screen *screen, uint64_t *timestamp)
{
   /* The number of valid bits in a timestamp value is determined by
    * the VkQueueFamilyProperties::timestampValidBits property of the queue on which the timestamp is written.
    * - 17.5. Timestamp Queries
    */
   if (screen->timestamp_valid_bits < 64)
      *timestamp &= (1ull << screen->timestamp_valid_bits) - 1;

   /* The number of nanoseconds it takes for a timestamp value to be incremented by 1
    * can be obtained from VkPhysicalDeviceLimits::timestampPeriod
    * - 17.5. Timestamp Queries
    */
   *timestamp *= screen->info.props.limits.timestampPeriod;
}

static VkQueryType
convert_query_type(unsigned query_type, bool *precise)
{
   *precise = false;
   switch (query_type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      *precise = true;
      /* fallthrough */
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      return VK_QUERY_TYPE_OCCLUSION;
   case PIPE_QUERY_TIME_ELAPSED:
   case PIPE_QUERY_TIMESTAMP:
      return VK_QUERY_TYPE_TIMESTAMP;
   case PIPE_QUERY_PIPELINE_STATISTICS_SINGLE:
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      return VK_QUERY_TYPE_PIPELINE_STATISTICS;
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      return VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
   default:
      debug_printf("unknown query: %s\n",
                   util_str_query_type(query_type, true));
      unreachable("zink: unknown query type");
   }
}

static bool
needs_stats_list(struct zink_query *query)
{
   return query->type == PIPE_QUERY_PRIMITIVES_GENERATED ||
          query->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE ||
          query->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE;
}

static bool
is_time_query(struct zink_query *query)
{
   return query->type == PIPE_QUERY_TIMESTAMP || query->type == PIPE_QUERY_TIME_ELAPSED;
}

static bool
is_so_overflow_query(struct zink_query *query)
{
   return query->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE || query->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE;
}

static struct pipe_query *
zink_create_query(struct pipe_context *pctx,
                  unsigned query_type, unsigned index)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = CALLOC_STRUCT(zink_query);
   VkQueryPoolCreateInfo pool_create = {};

   if (!query)
      return NULL;

   query->index = index;
   query->type = query_type;
   query->vkqtype = convert_query_type(query_type, &query->precise);
   if (query->vkqtype == -1)
      return NULL;

   query->num_queries = NUM_QUERIES;
   query->curr_query = 0;

   pool_create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
   pool_create.queryType = query->vkqtype;
   pool_create.queryCount = query->num_queries;
   if (query_type == PIPE_QUERY_PRIMITIVES_GENERATED)
     pool_create.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT |
                                      VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT;
   else if (query_type == PIPE_QUERY_PIPELINE_STATISTICS_SINGLE)
      pool_create.pipelineStatistics = pipeline_statistic_convert(index);

   VkResult status = vkCreateQueryPool(screen->dev, &pool_create, NULL, &query->query_pool);
   if (status != VK_SUCCESS) {
      FREE(query);
      return NULL;
   }
   if (query_type == PIPE_QUERY_PRIMITIVES_GENERATED) {
      /* if xfb is active, we need to use an xfb query, otherwise we need pipeline statistics */
      pool_create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
      pool_create.queryType = VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
      pool_create.queryCount = query->num_queries;

      status = vkCreateQueryPool(screen->dev, &pool_create, NULL, &query->xfb_query_pool[0]);
      if (status != VK_SUCCESS) {
         vkDestroyQueryPool(screen->dev, query->query_pool, NULL);
         FREE(query);
         return NULL;
      }
   } else if (query_type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      /* need to monitor all xfb streams */
      for (unsigned i = 0; i < ARRAY_SIZE(query->xfb_query_pool); i++) {
         status = vkCreateQueryPool(screen->dev, &pool_create, NULL, &query->xfb_query_pool[i]);
         if (status != VK_SUCCESS) {
            vkDestroyQueryPool(screen->dev, query->query_pool, NULL);
            for (unsigned j = 0; j < i; j++)
               vkDestroyQueryPool(screen->dev, query->xfb_query_pool[j], NULL);
            FREE(query);
            return NULL;
         }
      }
   }
   struct zink_batch *batch = &zink_context(pctx)->batch;
   batch->has_work = true;
   vkCmdResetQueryPool(batch->state->cmdbuf, query->query_pool, 0, query->num_queries);
   if (query->type == PIPE_QUERY_PRIMITIVES_GENERATED)
      vkCmdResetQueryPool(batch->state->cmdbuf, query->xfb_query_pool[0], 0, query->num_queries);
   if (query->type == PIPE_QUERY_TIMESTAMP)
      query->active = true;
   return (struct pipe_query *)query;
}

static void
destroy_query(struct zink_screen *screen, struct zink_query *query)
{
   assert(!p_atomic_read(&query->fences));
   vkDestroyQueryPool(screen->dev, query->query_pool, NULL);
   for (unsigned i = 0; i < ARRAY_SIZE(query->xfb_query_pool); i++)
      if (query->xfb_query_pool[i])
         vkDestroyQueryPool(screen->dev, query->xfb_query_pool[i], NULL);
   pipe_resource_reference((struct pipe_resource**)&query->predicate, NULL);
   FREE(query);
}

static void
zink_destroy_query(struct pipe_context *pctx,
                   struct pipe_query *q)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = (struct zink_query *)q;

   p_atomic_set(&query->dead, true);
   if (p_atomic_read(&query->fences)) {
      if (query->xfb_running)
        zink_fence_wait(pctx);
      return;
   }

   destroy_query(screen, query);
}

void
zink_prune_query(struct zink_screen *screen, struct zink_query *query)
{
   if (!p_atomic_dec_return(&query->fences)) {
      if (p_atomic_read(&query->dead))
         destroy_query(screen, query);
   }
}

static void
check_query_results(struct zink_query *query, union pipe_query_result *result,
                    int num_results, int result_size, uint64_t *results, uint64_t *xfb_results)
{
   uint64_t last_val = 0;
   for (int i = 0; i < num_results * result_size; i += result_size) {
      switch (query->type) {
      case PIPE_QUERY_OCCLUSION_PREDICATE:
      case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      case PIPE_QUERY_GPU_FINISHED:
         result->b |= results[i] != 0;
         break;

      case PIPE_QUERY_TIME_ELAPSED:
      case PIPE_QUERY_TIMESTAMP:
         /* the application can sum the differences between all N queries to determine the total execution time.
          * - 17.5. Timestamp Queries
          */
         if (query->type != PIPE_QUERY_TIME_ELAPSED || i)
            result->u64 += results[i] - last_val;
         last_val = results[i];
         break;
      case PIPE_QUERY_OCCLUSION_COUNTER:
         result->u64 += results[i];
         break;
      case PIPE_QUERY_PRIMITIVES_GENERATED:
         if (query->have_xfb[query->last_start + i / 2] || query->index)
            result->u64 += xfb_results[i + 1];
         else
            /* if a given draw had a geometry shader, we need to use the second result */
            result->u64 += results[i + query->have_gs[query->last_start + i / 2]];
         break;
      case PIPE_QUERY_PRIMITIVES_EMITTED:
         /* A query pool created with this type will capture 2 integers -
          * numPrimitivesWritten and numPrimitivesNeeded -
          * for the specified vertex stream output from the last vertex processing stage.
          * - from VK_EXT_transform_feedback spec
          */
         result->u64 += results[i];
         break;
      case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
      case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
         /* A query pool created with this type will capture 2 integers -
          * numPrimitivesWritten and numPrimitivesNeeded -
          * for the specified vertex stream output from the last vertex processing stage.
          * - from VK_EXT_transform_feedback spec
          */
         if (query->have_xfb[query->last_start + i / 2])
            result->b |= results[i] != results[i + 1];
         break;
      case PIPE_QUERY_PIPELINE_STATISTICS_SINGLE:
         result->u64 += results[i];
         break;

      default:
         debug_printf("unhandled query type: %s\n",
                      util_str_query_type(query->type, true));
         unreachable("unexpected query type");
      }
   }
}

static bool
get_query_result(struct pipe_context *pctx,
                      struct pipe_query *q,
                      bool wait,
                      union pipe_query_result *result)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = (struct zink_query *)q;
   VkQueryResultFlagBits flags = 0;

   if (wait)
      flags |= VK_QUERY_RESULT_WAIT_BIT;

   flags |= VK_QUERY_RESULT_64_BIT;

   if (result != &query->accumulated_result) {
      if (query->type == PIPE_QUERY_TIMESTAMP ||
          is_so_overflow_query(query))
         util_query_clear_result(result, query->type);
      else {
         memcpy(result, &query->accumulated_result, sizeof(query->accumulated_result));
         util_query_clear_result(&query->accumulated_result, query->type);
      }
   } else
      flags |= VK_QUERY_RESULT_PARTIAL_BIT;

   // union pipe_query_result results[NUM_QUERIES * 2];
   /* xfb queries return 2 results */
   uint64_t results[NUM_QUERIES * 2];
   memset(results, 0, sizeof(results));
   uint64_t xfb_results[NUM_QUERIES * 2];
   memset(xfb_results, 0, sizeof(xfb_results));
   int num_results = query->curr_query - query->last_start;
   int result_size = 1;
      /* these query types emit 2 values */
   if (query->vkqtype == VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT ||
       query->type == PIPE_QUERY_PRIMITIVES_GENERATED ||
       query->type == PIPE_QUERY_PRIMITIVES_EMITTED)
      result_size = 2;

   for (unsigned last_start = query->last_start; last_start + num_results <= query->curr_query; last_start++) {
      /* verify that we have the expected number of results pending */
      assert(num_results <= ARRAY_SIZE(results) / result_size);
      VkResult status = vkGetQueryPoolResults(screen->dev, query->query_pool,
                                              last_start, num_results,
                                              sizeof(results),
                                              results,
                                              sizeof(uint64_t) * result_size,
                                              flags);
      if (status != VK_SUCCESS)
         return false;

      if (query->type == PIPE_QUERY_PRIMITIVES_GENERATED) {
         status = vkGetQueryPoolResults(screen->dev, query->xfb_query_pool[0],
                                                 last_start, num_results,
                                                 sizeof(xfb_results),
                                                 xfb_results,
                                                 2 * sizeof(uint64_t),
                                                 flags | VK_QUERY_RESULT_64_BIT);
         if (status != VK_SUCCESS)
            return false;

      }

      check_query_results(query, result, num_results, result_size, results, xfb_results);
   }

   if (query->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE && !result->b) {
      for (unsigned i = 0; i < ARRAY_SIZE(query->xfb_query_pool) && !result->b; i++) {
         memset(results, 0, sizeof(results));
         VkResult status = vkGetQueryPoolResults(screen->dev, query->xfb_query_pool[i],
                                                    query->last_start, num_results,
                                                    sizeof(results),
                                                    results,
                                                    sizeof(uint64_t) * 2,
                                                    flags);
         if (status != VK_SUCCESS)
            return false;
         check_query_results(query, result, num_results, result_size, results, xfb_results);
      }
   }

   if (is_time_query(query))
      timestamp_to_nanoseconds(screen, &result->u64);

   return TRUE;
}

static void
force_cpu_read(struct zink_context *ctx, struct pipe_query *pquery, bool wait, enum pipe_query_value_type result_type, struct pipe_resource *pres, unsigned offset)
{
   struct pipe_context *pctx = &ctx->base;
   unsigned result_size = result_type <= PIPE_QUERY_TYPE_U32 ? sizeof(uint32_t) : sizeof(uint64_t);
   struct zink_query *query = (struct zink_query*)pquery;
   union pipe_query_result result;
   if (zink_batch_usage_matches(&query->batch_id, ctx->curr_batch))
      pctx->flush(pctx, NULL, PIPE_FLUSH_HINT_FINISH);

   bool success = get_query_result(pctx, pquery, wait, &result);
   if (!success) {
      debug_printf("zink: getting query result failed\n");
      return;
   }

   if (result_type <= PIPE_QUERY_TYPE_U32) {
      uint32_t u32;
      uint32_t limit;
      if (result_type == PIPE_QUERY_TYPE_I32)
         limit = INT_MAX;
      else
         limit = UINT_MAX;
      if (is_so_overflow_query(query))
         u32 = result.b;
      else
         u32 = MIN2(limit, result.u64);
      pipe_buffer_write(pctx, pres, offset, result_size, &u32);
   } else {
      uint64_t u64;
      if (is_so_overflow_query(query))
         u64 = result.b;
      else
         u64 = result.u64;
      pipe_buffer_write(pctx, pres, offset, result_size, &u64);
   }
}

static void
copy_results_to_buffer(struct zink_context *ctx, struct zink_query *query, struct zink_resource *res, unsigned offset, int num_results, VkQueryResultFlags flags)
{
   unsigned query_id = query->last_start;
   struct zink_batch *batch = &ctx->batch;
   unsigned base_result_size = (flags & VK_QUERY_RESULT_64_BIT) ? sizeof(uint64_t) : sizeof(uint32_t);
   unsigned result_size = base_result_size * num_results;
   if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
      result_size += base_result_size;
   /* if it's a single query that doesn't need special handling, we can copy it and be done */
   zink_batch_reference_resource_rw(batch, res, true);
   zink_resource_buffer_barrier(ctx, batch, res, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
   util_range_add(&res->base, &res->valid_buffer_range, offset, offset + result_size);
   vkCmdCopyQueryPoolResults(batch->state->cmdbuf, query->query_pool, query_id, num_results, res->obj->buffer,
                             offset, 0, flags);
   /* this is required for compute batch sync and will be removed later */
   zink_flush_queue(ctx);

}

static void
reset_pool(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   /* This command must only be called outside of a render pass instance
    *
    * - vkCmdResetQueryPool spec
    */
   zink_batch_no_rp(ctx);

   if (q->type != PIPE_QUERY_TIMESTAMP)
      get_query_result(&ctx->base, (struct pipe_query*)q, false, &q->accumulated_result);
   vkCmdResetQueryPool(batch->state->cmdbuf, q->query_pool, 0, q->num_queries);
   if (q->type == PIPE_QUERY_PRIMITIVES_GENERATED)
      vkCmdResetQueryPool(batch->state->cmdbuf, q->xfb_query_pool[0], 0, q->num_queries);
   else if (q->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      for (unsigned i = 0; i < ARRAY_SIZE(q->xfb_query_pool); i++)
         vkCmdResetQueryPool(batch->state->cmdbuf, q->xfb_query_pool[i], 0, q->num_queries);
   }
   memset(q->have_gs, 0, sizeof(q->have_gs));
   memset(q->have_xfb, 0, sizeof(q->have_xfb));
   q->last_start = q->curr_query = 0;
   q->needs_reset = false;
}

static void
begin_query(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   VkQueryControlFlags flags = 0;

   q->predicate_dirty = true;
   if (q->needs_reset)
      reset_pool(ctx, batch, q);
   assert(q->curr_query < q->num_queries);
   q->active = true;
   batch->has_work = true;
   if (q->type == PIPE_QUERY_TIME_ELAPSED)
      vkCmdWriteTimestamp(batch->state->cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, q->query_pool, q->curr_query++);
   /* ignore the rest of begin_query for timestamps */
   if (is_time_query(q))
      return;
   if (q->precise)
      flags |= VK_QUERY_CONTROL_PRECISE_BIT;
   if (q->type == PIPE_QUERY_PRIMITIVES_EMITTED ||
       q->type == PIPE_QUERY_PRIMITIVES_GENERATED ||
       q->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE) {
      zink_screen(ctx->base.screen)->vk_CmdBeginQueryIndexedEXT(batch->state->cmdbuf,
                                                                q->xfb_query_pool[0] ? q->xfb_query_pool[0] : q->query_pool,
                                                                q->curr_query,
                                                                flags,
                                                                q->index);
      q->xfb_running = true;
   } else if (q->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      zink_screen(ctx->base.screen)->vk_CmdBeginQueryIndexedEXT(batch->state->cmdbuf,
                                                                q->query_pool,
                                                                q->curr_query,
                                                                flags,
                                                                0);
      for (unsigned i = 0; i < ARRAY_SIZE(q->xfb_query_pool); i++)
         zink_screen(ctx->base.screen)->vk_CmdBeginQueryIndexedEXT(batch->state->cmdbuf,
                                                                   q->xfb_query_pool[i],
                                                                   q->curr_query,
                                                                   flags,
                                                                   i + 1);
      q->xfb_running = true;
   }
   if (q->vkqtype != VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT)
      vkCmdBeginQuery(batch->state->cmdbuf, q->query_pool, q->curr_query, flags);
   if (needs_stats_list(q))
      list_addtail(&q->stats_list, &ctx->primitives_generated_queries);
   p_atomic_inc(&q->fences);
   zink_batch_usage_set(&q->batch_id, batch->state->fence.batch_id);
   _mesa_set_add(batch->state->active_queries, q);
}

static bool
zink_begin_query(struct pipe_context *pctx,
                 struct pipe_query *q)
{
   struct zink_query *query = (struct zink_query *)q;
   struct zink_context *ctx = zink_context(pctx);
   struct zink_batch *batch = &ctx->batch;

   query->last_start = query->curr_query;

   util_query_clear_result(&query->accumulated_result, query->type);

   begin_query(ctx, batch, query);

   return true;
}

static void
end_query(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   batch->has_work = true;
   q->active = q->type == PIPE_QUERY_TIMESTAMP;
   if (is_time_query(q)) {
      vkCmdWriteTimestamp(batch->state->cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                          q->query_pool, q->curr_query);
      zink_batch_usage_set(&q->batch_id, batch->state->fence.batch_id);
   } else if (q->type == PIPE_QUERY_PRIMITIVES_EMITTED ||
            q->type == PIPE_QUERY_PRIMITIVES_GENERATED ||
            q->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE)
      screen->vk_CmdEndQueryIndexedEXT(batch->state->cmdbuf, q->xfb_query_pool[0] ? q->xfb_query_pool[0] :
                                                                                    q->query_pool,
                                       q->curr_query, q->index);

   else if (q->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      screen->vk_CmdEndQueryIndexedEXT(batch->state->cmdbuf, q->query_pool, q->curr_query, 0);
      for (unsigned i = 0; i < ARRAY_SIZE(q->xfb_query_pool); i++)
         screen->vk_CmdEndQueryIndexedEXT(batch->state->cmdbuf, q->xfb_query_pool[i], q->curr_query, i + 1);
   }
   if (q->vkqtype != VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT && !is_time_query(q))
      vkCmdEndQuery(batch->state->cmdbuf, q->query_pool, q->curr_query);
   if (needs_stats_list(q))
      list_delinit(&q->stats_list);
   if (++q->curr_query == q->num_queries) {
      /* always reset on start; this ensures we can actually submit the batch that the current query is on */
      q->needs_reset = true;
   }
}

static bool
zink_end_query(struct pipe_context *pctx,
               struct pipe_query *q)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_query *query = (struct zink_query *)q;
   struct zink_batch *batch = &ctx->batch;

   if (needs_stats_list(query))
      list_delinit(&query->stats_list);
   if (query->active)
      end_query(ctx, batch, query);

   return true;
}

static bool
zink_get_query_result(struct pipe_context *pctx,
                      struct pipe_query *q,
                      bool wait,
                      union pipe_query_result *result)
{
   struct zink_query *query = (void*)q;
   struct zink_context *ctx = zink_context(pctx);
   uint32_t batch_id = p_atomic_read(&query->batch_id.usage);

   if (wait)
      zink_wait_on_batch(ctx, batch_id);
   else if (batch_id == ctx->curr_batch)
      zink_flush_queue(ctx);

   return get_query_result(pctx, q, wait, result);
}

void
zink_suspend_queries(struct zink_context *ctx, struct zink_batch *batch)
{
   set_foreach(batch->state->active_queries, entry) {
      struct zink_query *query = (void*)entry->key;
      /* if a query isn't active here then we don't need to reactivate it on the next batch */
      if (query->active) {
         end_query(ctx, batch, query);
         /* the fence is going to steal the set off the batch, so we have to copy
          * the active queries onto a list
          */
         list_addtail(&query->active_list, &ctx->suspended_queries);
      }
   }
}

void
zink_resume_queries(struct zink_context *ctx, struct zink_batch *batch)
{
   struct zink_query *query, *next;
   LIST_FOR_EACH_ENTRY_SAFE(query, next, &ctx->suspended_queries, active_list) {
      begin_query(ctx, batch, query);
      list_delinit(&query->active_list);
   }
}

void
zink_query_update_gs_states(struct zink_context *ctx)
{
   struct zink_query *query;
   LIST_FOR_EACH_ENTRY(query, &ctx->primitives_generated_queries, stats_list) {
      assert(query->curr_query < ARRAY_SIZE(query->have_gs));
      assert(query->active);
      query->have_gs[query->curr_query] = !!ctx->gfx_stages[PIPE_SHADER_GEOMETRY];
      query->have_xfb[query->curr_query] = !!ctx->num_so_targets;
   }
}

static void
zink_set_active_query_state(struct pipe_context *pctx, bool enable)
{
   struct zink_context *ctx = zink_context(pctx);
   ctx->queries_disabled = !enable;

   struct zink_batch *batch = &ctx->batch;
   if (ctx->queries_disabled)
      zink_suspend_queries(ctx, batch);
   else
      zink_resume_queries(ctx, batch);
}

void
zink_start_conditional_render(struct zink_context *ctx)
{
   struct zink_batch *batch = &ctx->batch;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkConditionalRenderingFlagsEXT begin_flags = 0;
   if (ctx->render_condition.inverted)
      begin_flags = VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
   VkConditionalRenderingBeginInfoEXT begin_info = {};
   begin_info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
   begin_info.buffer = ctx->render_condition.query->predicate->obj->buffer;
   begin_info.flags = begin_flags;
   screen->vk_CmdBeginConditionalRenderingEXT(batch->state->cmdbuf, &begin_info);
   zink_batch_reference_resource_rw(batch, ctx->render_condition.query->predicate, false);
}

void
zink_stop_conditional_render(struct zink_context *ctx)
{
   struct zink_batch *batch = &ctx->batch;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   zink_clear_apply_conditionals(ctx);
   screen->vk_CmdEndConditionalRenderingEXT(batch->state->cmdbuf);
}

static void
zink_render_condition(struct pipe_context *pctx,
                      struct pipe_query *pquery,
                      bool condition,
                      enum pipe_render_cond_flag mode)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_query *query = (struct zink_query *)pquery;
   zink_batch_no_rp(ctx);
   VkQueryResultFlagBits flags = 0;

   if (query == NULL) {
      /* force conditional clears if they exist */
      if (ctx->clears_enabled && !ctx->batch.in_rp)
         zink_batch_rp(ctx);
      if (ctx->batch.in_rp)
         zink_stop_conditional_render(ctx);
      ctx->render_condition_active = false;
      ctx->render_condition.query = NULL;
      return;
   }

   if (!query->predicate) {
      struct pipe_resource *pres;

      /* need to create a vulkan buffer to copy the data into */
      pres = pipe_buffer_create(pctx->screen, PIPE_BIND_QUERY_BUFFER, PIPE_USAGE_DEFAULT, sizeof(uint64_t));
      if (!pres)
         return;

      query->predicate = zink_resource(pres);
   }
   if (query->predicate_dirty) {
      struct zink_resource *res = query->predicate;

      if (mode == PIPE_RENDER_COND_WAIT || mode == PIPE_RENDER_COND_BY_REGION_WAIT)
         flags |= VK_QUERY_RESULT_WAIT_BIT;

      flags |= VK_QUERY_RESULT_64_BIT;
      int num_results = query->curr_query - query->last_start;
      if (query->type != PIPE_QUERY_PRIMITIVES_GENERATED &&
          !is_so_overflow_query(query)) {
         copy_results_to_buffer(ctx, query, res, 0, num_results, flags);
      } else {
         /* these need special handling */
         force_cpu_read(ctx, pquery, true, PIPE_QUERY_TYPE_U32, &res->base, 0);
      }
      query->predicate_dirty = false;
   }
   ctx->render_condition.inverted = condition;
   ctx->render_condition_active = true;
   ctx->render_condition.query = query;
   if (ctx->batch.in_rp)
      zink_start_conditional_render(ctx);
}

static void
zink_get_query_result_resource(struct pipe_context *pctx,
                               struct pipe_query *pquery,
                               bool wait,
                               enum pipe_query_value_type result_type,
                               int index,
                               struct pipe_resource *pres,
                               unsigned offset)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = (struct zink_query*)pquery;
   struct zink_resource *res = zink_resource(pres);
   unsigned result_size = result_type <= PIPE_QUERY_TYPE_U32 ? sizeof(uint32_t) : sizeof(uint64_t);
   VkQueryResultFlagBits size_flags = result_type <= PIPE_QUERY_TYPE_U32 ? 0 : VK_QUERY_RESULT_64_BIT;
   unsigned num_queries = query->curr_query - query->last_start;
   unsigned query_id = query->last_start;
   unsigned fences = p_atomic_read(&query->fences);

   if (index == -1) {
      /* VK_QUERY_RESULT_WITH_AVAILABILITY_BIT will ALWAYS write some kind of result data
       * in addition to the availability result, which is a problem if we're just trying to get availability data
       *
       * if we know that there's no valid buffer data in the preceding buffer range, then we can just
       * stomp on it with a glorious queued buffer copy instead of forcing a stall to manually write to the
       * buffer
       */

      if (fences) {
         struct pipe_resource *staging = pipe_buffer_create(pctx->screen, 0, PIPE_USAGE_STAGING, result_size * 2);
         copy_results_to_buffer(ctx, query, zink_resource(staging), 0, 1, size_flags | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT | VK_QUERY_RESULT_PARTIAL_BIT);
         zink_copy_buffer(ctx, &ctx->batch, res, zink_resource(staging), offset, result_size, result_size);
         pipe_resource_reference(&staging, NULL);
      } else {
         uint64_t u64[2] = {0};
         if (vkGetQueryPoolResults(screen->dev, query->query_pool, query_id, 1, 2 * result_size, u64,
                                   0, size_flags | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT | VK_QUERY_RESULT_PARTIAL_BIT) != VK_SUCCESS) {
            debug_printf("zink: getting query result failed\n");
            return;
         }
         pipe_buffer_write(pctx, pres, offset, result_size, (unsigned char*)u64 + result_size);
      }
      return;
   }

   if (!is_time_query(query) && (!fences || wait)) {
      /* result happens to be ready or we're waiting */
      if (num_queries == 1 && query->type != PIPE_QUERY_PRIMITIVES_GENERATED &&
                              query->type != PIPE_QUERY_PRIMITIVES_EMITTED &&
                              /* FIXME: I don't know why, but occlusion is broken here */
                              query->type != PIPE_QUERY_OCCLUSION_PREDICATE &&
                              query->type != PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE &&
                              !is_so_overflow_query(query)) {
         copy_results_to_buffer(ctx, query, res, offset, 1, size_flags);
         return;
      }
   }

   /* unfortunately, there's no way to accumulate results from multiple queries on the gpu without either
    * clobbering all but the last result or writing the results sequentially, so we have to manually write the result
    */
   force_cpu_read(ctx, pquery, true, result_type, pres, offset);
}

static uint64_t
zink_get_timestamp(struct pipe_context *pctx)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   uint64_t timestamp, deviation;
   assert(screen->info.have_EXT_calibrated_timestamps);
   VkCalibratedTimestampInfoEXT cti = {};
   cti.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
   cti.timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;
   screen->vk_GetCalibratedTimestampsEXT(screen->dev, 1, &cti, &timestamp, &deviation);
   timestamp_to_nanoseconds(screen, &timestamp);
   return timestamp;
}

void
zink_context_query_init(struct pipe_context *pctx)
{
   struct zink_context *ctx = zink_context(pctx);
   list_inithead(&ctx->suspended_queries);
   list_inithead(&ctx->primitives_generated_queries);

   pctx->create_query = zink_create_query;
   pctx->destroy_query = zink_destroy_query;
   pctx->begin_query = zink_begin_query;
   pctx->end_query = zink_end_query;
   pctx->get_query_result = zink_get_query_result;
   pctx->get_query_result_resource = zink_get_query_result_resource;
   pctx->set_active_query_state = zink_set_active_query_state;
   pctx->render_condition = zink_render_condition;
   pctx->get_timestamp = zink_get_timestamp;
}
