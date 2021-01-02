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
   VkQueryPool xfb_query_pool;
   unsigned curr_query, num_queries, last_start;

   VkQueryType vkqtype;
   unsigned index;
   bool use_64bit;
   bool precise;
   bool xfb_running;

   bool active; /* query is considered active by vk */
   bool needs_reset; /* query is considered active by vk and cannot be destroyed */
   bool dead; /* query should be destroyed when its fence finishes */

   unsigned fences;
   struct list_head active_list;

   struct list_head stats_list; /* when active, statistics queries are added to ctx->primitives_generated_queries */
   bool have_gs[NUM_QUERIES]; /* geometry shaders use GEOMETRY_SHADER_PRIMITIVES_BIT */
   bool have_xfb[NUM_QUERIES]; /* xfb was active during this query */

   unsigned batch_id : 2; //batch that the query was started in

   union pipe_query_result accumulated_result;
};

static void
timestamp_to_nanoseconds(struct zink_screen *screen, uint64_t *timestamp)
{
   /* The number of valid bits in a timestamp value is determined by
    * the VkQueueFamilyProperties::timestampValidBits property of the queue on which the timestamp is written.
    * - 17.5. Timestamp Queries
    */
   *timestamp &= ((1ull << screen->timestamp_valid_bits) - 1);
   /* The number of nanoseconds it takes for a timestamp value to be incremented by 1
    * can be obtained from VkPhysicalDeviceLimits::timestampPeriod
    * - 17.5. Timestamp Queries
    */
   *timestamp *= screen->info.props.limits.timestampPeriod;
}

static VkQueryType
convert_query_type(unsigned query_type, bool *use_64bit, bool *precise)
{
   *use_64bit = false;
   *precise = false;
   switch (query_type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      *precise = true;
      *use_64bit = true;
      /* fallthrough */
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      return VK_QUERY_TYPE_OCCLUSION;
   case PIPE_QUERY_TIME_ELAPSED:
   case PIPE_QUERY_TIMESTAMP:
      *use_64bit = true;
      return VK_QUERY_TYPE_TIMESTAMP;
   case PIPE_QUERY_PIPELINE_STATISTICS:
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      return VK_QUERY_TYPE_PIPELINE_STATISTICS;
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      *use_64bit = true;
      return VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
   default:
      debug_printf("unknown query: %s\n",
                   util_str_query_type(query_type, true));
      unreachable("zink: unknown query type");
   }
}

static bool
is_time_query(struct zink_query *query)
{
   return query->type == PIPE_QUERY_TIMESTAMP || query->type == PIPE_QUERY_TIME_ELAPSED;
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
   query->vkqtype = convert_query_type(query_type, &query->use_64bit, &query->precise);
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

      status = vkCreateQueryPool(screen->dev, &pool_create, NULL, &query->xfb_query_pool);
      if (status != VK_SUCCESS) {
         vkDestroyQueryPool(screen->dev, query->query_pool, NULL);
         FREE(query);
         return NULL;
      }
   }
   struct zink_batch *batch = zink_batch_no_rp(zink_context(pctx));
   vkCmdResetQueryPool(batch->cmdbuf, query->query_pool, 0, query->num_queries);
   if (query->type == PIPE_QUERY_PRIMITIVES_GENERATED)
      vkCmdResetQueryPool(batch->cmdbuf, query->xfb_query_pool, 0, query->num_queries);
   if (query->type == PIPE_QUERY_TIMESTAMP)
      query->active = true;
   return (struct pipe_query *)query;
}

static void
destroy_query(struct zink_screen *screen, struct zink_query *query)
{
   assert(!p_atomic_read(&query->fences));
   vkDestroyQueryPool(screen->dev, query->query_pool, NULL);
   if (query->type == PIPE_QUERY_PRIMITIVES_GENERATED)
      vkDestroyQueryPool(screen->dev, query->xfb_query_pool, NULL);
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
zink_prune_queries(struct zink_screen *screen, struct zink_fence *fence)
{
   set_foreach(fence->active_queries, entry) {
      struct zink_query *query = (void*)entry->key;
      if (!p_atomic_dec_return(&query->fences)) {
         if (p_atomic_read(&query->dead))
            destroy_query(screen, query);
      }
   }
   _mesa_set_destroy(fence->active_queries, NULL);
   fence->active_queries = NULL;
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

   if (query->use_64bit)
      flags |= VK_QUERY_RESULT_64_BIT;

   if (result != &query->accumulated_result) {
      if (query->type == PIPE_QUERY_TIMESTAMP)
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
   if (query->type == PIPE_QUERY_PRIMITIVES_GENERATED ||
       query->type == PIPE_QUERY_PRIMITIVES_EMITTED)
      result_size = 2;

   /* verify that we have the expected number of results pending */
   assert(query->curr_query <= ARRAY_SIZE(results) / result_size);
   VkResult status = vkGetQueryPoolResults(screen->dev, query->query_pool,
                                           query->last_start, num_results,
                                           sizeof(results),
                                           results,
                                           sizeof(uint64_t),
                                           flags);
   if (status != VK_SUCCESS)
      return false;

   if (query->type == PIPE_QUERY_PRIMITIVES_GENERATED) {
      status = vkGetQueryPoolResults(screen->dev, query->xfb_query_pool,
                                              query->last_start, num_results,
                                              sizeof(xfb_results),
                                              xfb_results,
                                              2 * sizeof(uint64_t),
                                              flags | VK_QUERY_RESULT_64_BIT);
      if (status != VK_SUCCESS)
         return false;

   }

   uint64_t last_val = 0;
   for (int i = 0; i < num_results * result_size; i += result_size) {
      switch (query->type) {
      case PIPE_QUERY_OCCLUSION_PREDICATE:
      case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
      case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
      case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
      case PIPE_QUERY_GPU_FINISHED:
         result->b |= results[i] != 0;
         break;

      case PIPE_QUERY_TIME_ELAPSED:
      case PIPE_QUERY_TIMESTAMP:
         /* the application can sum the differences between all N queries to determine the total execution time.
          * - 17.5. Timestamp Queries
          */
         if (query->type != PIPE_QUERY_TIME_ELAPSED || i > 0)
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
            result->u32 += ((uint32_t*)results)[i + query->have_gs[query->last_start + i / 2]];
         break;
      case PIPE_QUERY_PRIMITIVES_EMITTED:
         /* A query pool created with this type will capture 2 integers -
          * numPrimitivesWritten and numPrimitivesNeeded -
          * for the specified vertex stream output from the last vertex processing stage.
          * - from VK_EXT_transform_feedback spec
          */
         result->u64 += results[i];
         break;

      default:
         debug_printf("unhandled query type: %s\n",
                      util_str_query_type(query->type, true));
         unreachable("unexpected query type");
      }
   }

   if (is_time_query(query))
      timestamp_to_nanoseconds(screen, &result->u64);

   return TRUE;
}

static void
reset_pool(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   /* This command must only be called outside of a render pass instance
    *
    * - vkCmdResetQueryPool spec
    */
   batch = zink_batch_no_rp(ctx);

   if (q->type != PIPE_QUERY_TIMESTAMP)
      get_query_result(&ctx->base, (struct pipe_query*)q, false, &q->accumulated_result);
   vkCmdResetQueryPool(batch->cmdbuf, q->query_pool, 0, q->num_queries);
   if (q->type == PIPE_QUERY_PRIMITIVES_GENERATED)
      vkCmdResetQueryPool(batch->cmdbuf, q->xfb_query_pool, 0, q->num_queries);
   memset(q->have_gs, 0, sizeof(q->have_gs));
   memset(q->have_xfb, 0, sizeof(q->have_xfb));
   q->last_start = q->curr_query = 0;
   q->needs_reset = false;
}

static void
begin_query(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   VkQueryControlFlags flags = 0;

   if (q->needs_reset)
      reset_pool(ctx, batch, q);
   assert(q->curr_query < q->num_queries);
   q->active = true;
   if (q->type == PIPE_QUERY_TIME_ELAPSED)
      vkCmdWriteTimestamp(batch->cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, q->query_pool, q->curr_query++);
   /* ignore the rest of begin_query for timestamps */
   if (is_time_query(q))
      return;
   if (q->precise)
      flags |= VK_QUERY_CONTROL_PRECISE_BIT;
   if (q->type == PIPE_QUERY_PRIMITIVES_EMITTED || q->type == PIPE_QUERY_PRIMITIVES_GENERATED) {
      zink_screen(ctx->base.screen)->vk_CmdBeginQueryIndexedEXT(batch->cmdbuf,
                                                                q->xfb_query_pool ? q->xfb_query_pool : q->query_pool,
                                                                q->curr_query,
                                                                flags,
                                                                q->index);
      q->xfb_running = true;
   }
   if (q->vkqtype != VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT)
      vkCmdBeginQuery(batch->cmdbuf, q->query_pool, q->curr_query, flags);
   if (!batch->active_queries)
      batch->active_queries = _mesa_set_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
   assert(batch->active_queries);
   if (q->type == PIPE_QUERY_PRIMITIVES_GENERATED)
      list_addtail(&q->stats_list, &ctx->primitives_generated_queries);
   p_atomic_inc(&q->fences);
   q->batch_id = batch->batch_id;
   _mesa_set_add(batch->active_queries, q);
}

static bool
zink_begin_query(struct pipe_context *pctx,
                 struct pipe_query *q)
{
   struct zink_query *query = (struct zink_query *)q;
   struct zink_context *ctx = zink_context(pctx);
   struct zink_batch *batch = zink_curr_batch(ctx);

   query->last_start = query->curr_query;

   util_query_clear_result(&query->accumulated_result, query->type);

   begin_query(ctx, batch, query);

   return true;
}

static void
end_query(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   q->active = q->type == PIPE_QUERY_TIMESTAMP;
   if (is_time_query(q)) {
      vkCmdWriteTimestamp(batch->cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                          q->query_pool, q->curr_query);
      q->batch_id = batch->batch_id;
   } else if (q->type == PIPE_QUERY_PRIMITIVES_EMITTED || q->type == PIPE_QUERY_PRIMITIVES_GENERATED)
      screen->vk_CmdEndQueryIndexedEXT(batch->cmdbuf, q->xfb_query_pool ? q->xfb_query_pool : q->query_pool, q->curr_query, q->index);
   if (q->vkqtype != VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT && !is_time_query(q))
      vkCmdEndQuery(batch->cmdbuf, q->query_pool, q->curr_query);
   if (q->type == PIPE_QUERY_PRIMITIVES_GENERATED)
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
   struct zink_batch *batch = zink_curr_batch(ctx);

   if (query->type == PIPE_QUERY_PRIMITIVES_GENERATED)
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
   if (wait) {
      zink_fence_wait(pctx);
   } else
      pctx->flush(pctx, NULL, 0);
   return get_query_result(pctx, q, wait, result);
}

void
zink_suspend_queries(struct zink_context *ctx, struct zink_batch *batch)
{
   if (!batch->active_queries)
      return;
   set_foreach(batch->active_queries, entry) {
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

   struct zink_batch *batch = zink_curr_batch(ctx);
   if (ctx->queries_disabled)
      zink_suspend_queries(ctx, batch);
   else
      zink_resume_queries(ctx, batch);
}

static void
zink_render_condition(struct pipe_context *pctx,
                      struct pipe_query *pquery,
                      bool condition,
                      enum pipe_render_cond_flag mode)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = (struct zink_query *)pquery;
   struct zink_batch *batch = zink_batch_no_rp(ctx);
   VkQueryResultFlagBits flags = 0;

   if (query == NULL) {
      screen->vk_CmdEndConditionalRenderingEXT(batch->cmdbuf);
      ctx->render_condition_active = false;
      return;
   }

   struct pipe_resource *pres;
   struct zink_resource *res;
   struct pipe_resource templ = {};
   templ.width0 = 8;
   templ.height0 = 1;
   templ.depth0 = 1;
   templ.format = PIPE_FORMAT_R8_UINT;
   templ.target = PIPE_BUFFER;

   /* need to create a vulkan buffer to copy the data into */
   pres = pctx->screen->resource_create(pctx->screen, &templ);
   if (!pres)
      return;

   res = (struct zink_resource *)pres;

   if (mode == PIPE_RENDER_COND_WAIT || mode == PIPE_RENDER_COND_BY_REGION_WAIT)
      flags |= VK_QUERY_RESULT_WAIT_BIT;

   if (query->use_64bit)
      flags |= VK_QUERY_RESULT_64_BIT;
   int num_results = query->curr_query - query->last_start;
   vkCmdCopyQueryPoolResults(batch->cmdbuf, query->query_pool, query->last_start, num_results,
                             res->buffer, 0, 0, flags);

   VkConditionalRenderingFlagsEXT begin_flags = 0;
   if (condition)
      begin_flags = VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
   VkConditionalRenderingBeginInfoEXT begin_info = {};
   begin_info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
   begin_info.buffer = res->buffer;
   begin_info.flags = begin_flags;
   screen->vk_CmdBeginConditionalRenderingEXT(batch->cmdbuf, &begin_info);
   ctx->render_condition_active = true;

   zink_batch_reference_resource_rw(batch, res, true);

   pipe_resource_reference(&pres, NULL);
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
   pctx->set_active_query_state = zink_set_active_query_state;
   pctx->render_condition = zink_render_condition;
   pctx->get_timestamp = zink_get_timestamp;
}
